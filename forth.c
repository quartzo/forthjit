/*
 * forth.c - a tiny indirect-threaded Forth for Linux/gcc
 *
 * Build:  make
 * Run:    ./forth            (REPL)
 *         ./forth test.fs    (run a script, then REPL)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>
#include <dlfcn.h>
#include "tinf.h"

#include "sljitLir.h"
#include "prelude_blob.h"

/* target architecture code exposed to Forth as ARCH */
#if (defined SLJIT_CONFIG_X86_64 && SLJIT_CONFIG_X86_64)
#define FORTH_ARCH 1
#elif (defined SLJIT_CONFIG_X86_32 && SLJIT_CONFIG_X86_32)
#define FORTH_ARCH 2
#elif (defined SLJIT_CONFIG_ARM_64 && SLJIT_CONFIG_ARM_64)
#define FORTH_ARCH 3
#elif (defined SLJIT_CONFIG_ARM_32 && SLJIT_CONFIG_ARM_32)
#define FORTH_ARCH 4
#elif (defined SLJIT_CONFIG_RISCV_64 && SLJIT_CONFIG_RISCV_64)
#define FORTH_ARCH 5
#elif (defined SLJIT_CONFIG_RISCV_32 && SLJIT_CONFIG_RISCV_32)
#define FORTH_ARCH 6
#elif (defined SLJIT_CONFIG_MIPS && SLJIT_CONFIG_MIPS)
#define FORTH_ARCH 7
#elif (defined SLJIT_CONFIG_PPC && SLJIT_CONFIG_PPC)
#define FORTH_ARCH 8
#elif (defined SLJIT_CONFIG_S390X && SLJIT_CONFIG_S390X)
#define FORTH_ARCH 9
#elif (defined SLJIT_CONFIG_LOONGARCH && SLJIT_CONFIG_LOONGARCH)
#define FORTH_ARCH 10
#else
#define FORTH_ARCH 0
#endif

typedef long cell;

#define TRUE  (-1L)
#define FALSE (0L)

#define STACK_SIZE 1024
#define CODE_SIZE  (1 << 16)
#define MEM_SIZE   (1 << 16)
#define DICT_WORDS 1024
#define NAME_LEN   32

#define F_IMMEDIATE 0x01
#define F_HIDDEN    0x02

/* ---- stacks ---------------------------------------------------------- */
static cell dstack[STACK_SIZE];
static int  sp = 0;
static cell rstack[STACK_SIZE];
static int  rp = 0;

/* ---- threaded code --------------------------------------------------- */
static cell code[CODE_SIZE];
static cell here = 0;
static cell ip = -1;          /* instruction pointer (index into code[]) */

/* ---- data memory ----------------------------------------------------- */
static cell mem[MEM_SIZE];
static int  memtop = 0;

/* ---- dictionary ------------------------------------------------------ */
typedef struct Word Word;
typedef void (*Prim)(void);

struct Word {
    Word   *link;
    char    name[NAME_LEN];
    uint8_t flags;
    Prim    code;
    int     body;             /* start index in code[] for colon words */
    int     body_end;         /* end index (exclusive) for colon words */
    cell    data;             /* payload for VARIABLE / CONSTANT */
    void   *native;           /* machine code pointer for native words */
    char   *irbody;           /* IR source of a CODE word, for INLINE */
    char   *src;              /* reconstructed source of a colon word, for SEE */
};

static Word  dict[DICT_WORDS];
static int   nwords = 0;
static Word *latest = NULL;
static Word *compiling = NULL;
static Word *curr = NULL;     /* word currently being executed */

static cell state = 0;         /* 0 = interpret, 1 = compile, 2 = native IR */

/* internal words referenced directly */
static Word *W_EXIT, *W_LIT, *W_BRANCH, *W_0BRANCH, *W_ABORTQ, *W_DOES;
static Word *W_DO, *W_LOOP, *W_PLOOP, *W_QDO, *W_LEAVE;
static Word *W_LOCAL, *W_LOCALS_ENTER, *W_LOCALS_EXIT, *W_LOCAL_STORE;

/* ---- locals { a b -- c } --------------------------------------------- */
#define MAX_LOCALS 16
#define LOCALS_MAX 4096

static cell locals[LOCALS_MAX];
static int  lfbase = 0;        /* base slot of the current locals frame */
static int  lfree  = 0;        /* next free locals slot */

/* declaration of the definition currently being compiled */
static struct {
    int  n;                    /* total declared locals */
    int  ni;                   /* number of input locals */
    char name[MAX_LOCALS][NAME_LEN];
} cur_locals;

/* ---- input buffer ---------------------------------------------------- */
static char  inbuf[4096];
static char *inbuf_ptr = inbuf;

/* ---- error handling: ABORT / CATCH / THROW --------------------------- */
#define ERR_GENERIC  (-256)
#define ERR_ABORT    (-1)
#define ERR_ABORTQ   (-2)
#define ERR_OVF      (-3)
#define ERR_UNF      (-4)
#define ERR_RSTK     (-5)
#define ERR_DIVZERO  (-10)
#define ERR_UNKNOWN  (-13)

static jmp_buf abort_env;
static int     abort_active = 0;
static int     booting = 0;

static cell err_code = 0;
static char err_msg[256] = "";

/* active SLJIT compiler while assembling native code */
struct sljit_compiler *jcomp = NULL;

#define MAX_XFRAME 64
typedef struct {
    jmp_buf env;
    int     sp, rp;
    cell    ip, state;
    Word   *compiling;
    cell    code;
} XFrame;
static XFrame xframes[MAX_XFRAME];
static int    xsp = 0;

/* registry of generated code, freed at exit */
#define MAX_JIT 1024
/* registry of generated code, freed at exit */
#define MAX_JIT 1024
static void *jit_codes[MAX_JIT];
static int   njit = 0;

static void set_err(int code, const char *msg) {
    err_code = code;
    if (msg) {
        strncpy(err_msg, msg, sizeof err_msg - 1);
        err_msg[sizeof err_msg - 1] = 0;
    } else {
        err_msg[0] = 0;
    }
}

static void raise(int code, const char *msg) {
    set_err(code, msg);
    if (jcomp) { sljit_free_compiler(jcomp); jcomp = NULL; }
    if (xsp > 0) {
        XFrame *f = &xframes[xsp - 1];
        f->code = code;
        longjmp(f->env, 1);
    }
    if (abort_active) longjmp(abort_env, 1);
    fputs(err_msg[0] ? err_msg : "error", stderr);
    fputc('\n', stderr);
    exit(1);
}

static void throw_error(const char *msg) { raise(ERR_GENERIC, msg); }

/* entry point for native code: icall &forth_raise with (code -- ) */
static void forth_raise(cell code) { raise((int)code, "native throw"); }
static void forth_divzero(void) { raise(ERR_DIVZERO, "div by zero"); }
/* native stack-underflow guard: icall &forth_need with (have need -- ) */
static void forth_need(cell have, cell need) {
    if (have < need) raise(ERR_UNF, "stack underflow");
}
/* native stack-overflow guard: icall &forth_room with (have need -- ) */
static void forth_room(cell have, cell need) {
    if (have + need > STACK_SIZE) raise(ERR_OVF, "stack overflow");
}
/* icall &forth_type with (c-addr u -- ), uses stdio like EMIT */
static void forth_type(const char *s, int len) {
    if (len > 0) fwrite(s, 1, (size_t)len, stdout);
}

/* ---- forward declarations ------------------------------------------- */
static void p_docol(void);
static void p_push_addr(void);
static void p_does(void);
static void *jit_compile(Word *w);
static void p_push_const(void);
static void run_colon(Word *w);
static void execute_word(Word *w);
static void interpret_token(const char *tok);
static int  next_token(char *out);
static void push(cell v);
static cell pop(void);
static void p_endcode(void);
static void p_irquote(void);
static void ir_put(const char *tok);
static void p_native(void);

/* ---- stack helpers --------------------------------------------------- */
static void push(cell v) {
    if (sp >= STACK_SIZE) { raise(ERR_OVF, "stack overflow"); }
    dstack[sp++] = v;
}

static cell pop(void) {
    if (sp <= 0) { raise(ERR_UNF, "stack underflow"); }
    return dstack[--sp];
}


/* ---- code emission --------------------------------------------------- */
static void emit(cell v) {
    if (here >= CODE_SIZE) { throw_error("code space full"); }
    code[here++] = v;
}

static void compile_xt(Word *w) { emit((cell)w); }

/* ---- compile log: reconstruct source for SEE -------------------------- */
static char lbuf[4096];
static int  llen = 0;

static void log_reset(void) { llen = 0; lbuf[0] = 0; }

static void log_put(const char *s) {
    int n = (int)strlen(s);
    if (llen && llen + n + 2 < (int)sizeof lbuf) lbuf[llen++] = ' ';
    if (llen + n + 1 < (int)sizeof lbuf) { memcpy(lbuf + llen, s, (size_t)n); llen += n; }
    lbuf[llen] = 0;
}

/* ---- dictionary construction ---------------------------------------- */
static Word *newword(const char *name, Prim code) {
    if (nwords >= DICT_WORDS) { throw_error("dictionary full"); }
    Word *w = &dict[nwords++];
    w->link = latest;
    latest = w;
    strncpy(w->name, name, NAME_LEN - 1);
    w->name[NAME_LEN - 1] = 0;
    w->flags = 0;
    w->code = code;
    w->body = -1;
    w->body_end = -1;
    w->data = 0;
    free(w->irbody);
    free(w->src);
    w->native = NULL;
    w->irbody = NULL;
    w->src = NULL;
    return w;
}

static Word *define_prim(const char *name, Prim code) {
    return newword(name, code);
}

static Word *find(const char *name) {
    for (Word *w = latest; w; w = w->link)
        if (!(w->flags & F_HIDDEN) && strcmp(w->name, name) == 0)
            return w;
    return NULL;
}

/* ---- dictionary access helpers (callable from native CODE via &name) --- */
static Word *forth_find(const char *s, int len) {
    if (len < 0 || len >= NAME_LEN) return NULL;
    char buf[NAME_LEN];
    memcpy(buf, s, (size_t)len);
    buf[len] = 0;
    return find(buf);
}
static Word *forth_latest(void) { return latest; }
static Word *forth_link(Word *w) { return w ? w->link : NULL; }
static const char *forth_name(Word *w) { return w ? w->name : ""; }
static int   forth_flags(Word *w) { return w ? w->flags : 0; }
static void *forth_code(Word *w) { return w ? (void *)w->code : NULL; }
static void *forth_native(Word *w) { return w ? w->native : NULL; }
static cell  forth_data(Word *w) { return w ? w->data : 0; }
static int   forth_body_start(Word *w) { return w ? w->body : -1; }
static int   forth_body_end(Word *w) { return w ? w->body_end : -1; }
static cell  forth_immediate(Word *w) { return (w && (w->flags & F_IMMEDIATE)) ? -1 : 0; }
static cell  forth_hidden(Word *w) { return (w && (w->flags & F_HIDDEN)) ? -1 : 0; }
static cell  forth_colon_p(Word *w) { return (w && w->body >= 0) ? -1 : 0; }
static cell  forth_native_p(Word *w) { return (w && w->code == p_native) ? -1 : 0; }
static cell  forth_variable_p(Word *w) { return (w && w->code == p_push_addr) ? -1 : 0; }
static void *forth_body(Word *w) { return (w && (w->code == p_push_addr || w->code == p_does)) ? (void *)(mem + w->data) : NULL; }

/* ===================================================================== */
/*  VM primitives                                                        */
/* ===================================================================== */

/* (exit) -- return from a colon word */
static void p_exit(void) {
    if (rp <= 0) raise(ERR_RSTK, "return stack underflow");
    ip = rstack[--rp];
}

/* (lit) -- push inline literal */
static void p_lit(void) { push(code[ip++]); }

/* (branch) off -- unconditional jump */
static void p_branch(void) { int off = (int)code[ip++]; ip += off; }

/* (0branch) off -- conditional jump */
static void p_0branch(void) {
    int off = (int)code[ip++];
    if (pop() == 0) ip += off;
}

/* (do) -- move limit,start from data stack onto return stack */
static void rt_do(void) {
    if (rp + 2 > STACK_SIZE) { raise(ERR_RSTK, "return stack overflow"); }
    cell start = pop();
    cell limit = pop();
    rstack[rp++] = limit;
    rstack[rp++] = start;
}

/* (?do) -- like (do) but skip the loop when start == limit */
static void rt_qdo(void) {
    if (rp + 2 > STACK_SIZE) { raise(ERR_RSTK, "return stack overflow"); }
    cell start = pop();
    cell limit = pop();
    int off = (int)code[ip++];
    if (start == limit) ip += off;
    else { rstack[rp++] = limit; rstack[rp++] = start; }
}

/* (leave) -- drop the loop frame and jump past LOOP */
static void rt_leave(void) {
    if (rp < 2) raise(ERR_RSTK, "LEAVE outside a loop");
    rp -= 2;
    int off = (int)code[ip++];
    ip += off;
}

/* true when the loop index crosses the limit */
static int loop_finished(cell old, cell nw, cell lim, cell step) {
    if (step >= 0) return old < lim && nw >= lim;
    return old >= lim && nw < lim;
}

/* (loop) off -- increment index by 1, branch back if unfinished */
static void rt_loop(void) {
    cell old = rstack[rp - 1];
    cell lim = rstack[rp - 2];
    cell nw  = old + 1;
    rstack[rp - 1] = nw;
    if (loop_finished(old, nw, lim, 1)) { rp -= 2; ip++; }
    else { int off = (int)code[ip++]; ip += off; }
}

/* (+loop) off -- increment index by n, branch back if unfinished */
static void rt_ploop(void) {
    cell step = pop();
    cell old = rstack[rp - 1];
    cell lim = rstack[rp - 2];
    cell nw  = old + step;
    rstack[rp - 1] = nw;
    if (loop_finished(old, nw, lim, step)) { rp -= 2; ip++; }
    else { int off = (int)code[ip++]; ip += off; }
}


/* ===================================================================== */
/*  compiler / interpreter words                                         */
/* ===================================================================== */

static void p_colon(void) {
    char name[NAME_LEN];
    if (!next_token(name)) { fputs("name expected after :\n", stderr); return; }
    Word *w = newword(name, p_docol);
    w->flags |= F_HIDDEN;
    w->body = here;
    compiling = w;
    cur_locals.n = 0;
    cur_locals.ni = 0;
    log_reset();
    state = 1;
}

static void p_semicolon(void) {
    if (cur_locals.n > 0) compile_xt(W_LOCALS_EXIT);
    compile_xt(W_EXIT);
    if (compiling) {
        compiling->body_end = here;
        compiling->flags &= ~F_HIDDEN;
        free(compiling->src);
        compiling->src = strdup(lbuf);
    }
    compiling = NULL;
    state = 0;
}

static void p_recurse(void) {
    if (!compiling) { fputs("recurse outside definition\n", stderr); return; }
    compile_xt(compiling);
}

/* ---- locals ---------------------------------------------------------- */
/* { a b -- c }: save the old frame on the return stack, allocate a fresh
   one, and move the inputs from the data stack into their slots. */
static void p_locals_enter(void) {
    cell no = pop();
    cell ni = pop();
    if (ni < 0 || no < 0) throw_error("bad locals frame");
    if (rp + 2 > STACK_SIZE) raise(ERR_RSTK, "return stack overflow");
    if (lfree + (int)(ni + no) > LOCALS_MAX) throw_error("too many locals");
    rstack[rp++] = lfbase;
    rstack[rp++] = lfree;
    lfbase = lfree;
    lfree += (int)(ni + no);
    for (cell i = 0; i < ni; i++) locals[lfbase + (ni - 1 - i)] = pop();
    for (cell i = ni; i < ni + no; i++) locals[lfbase + i] = 0;
}

static void p_locals_exit(void) {
    if (rp < 2) raise(ERR_RSTK, "locals frame underflow");
    lfree  = (int)rstack[--rp];
    lfbase = (int)rstack[--rp];
}

static void p_local(void) {
    cell k = pop();
    if (k < 0 || k >= MAX_LOCALS) throw_error("bad local index");
    push(locals[lfbase + (int)k]);
}

static void p_local_store(void) {
    cell k = pop();
    cell v = pop();
    if (k < 0 || k >= MAX_LOCALS) throw_error("bad local index");
    locals[lfbase + (int)k] = v;
}

static void p_locals_brace(void) {
    if (state != 1 || !compiling) { fputs("{ outside a definition\n", stderr); return; }
    char t[NAME_LEN];
    int n = 0, ni = 0, seen_dash = 0, closed = 0;
    while (next_token(t)) {
        if (strcmp(t, "}") == 0) { closed = 1; break; }
        if (strcmp(t, "--") == 0) { seen_dash = 1; continue; }
        if (n >= MAX_LOCALS) throw_error("too many locals");
        snprintf(cur_locals.name[n], NAME_LEN, "%s", t);
        n++;
        if (!seen_dash) ni++;
    }
    if (!closed) throw_error("{ without }");
    cur_locals.n = n;
    cur_locals.ni = ni;
    {
        char decl[256];
        int p = snprintf(decl, sizeof decl, "{ ");
        for (int i = 0; i < n; i++) {
            if (i == ni) p += snprintf(decl + p, sizeof decl - p, "-- ");
            p += snprintf(decl + p, sizeof decl - p, "%s ", cur_locals.name[i]);
        }
        snprintf(decl + p, sizeof decl - p, "}");
        log_put(decl);
    }
    compile_xt(W_LIT); emit(ni);
    compile_xt(W_LIT); emit(n - ni);
    compile_xt(W_LOCALS_ENTER);
}

/* TO name -- store into a local declared by { ... } */
static void p_to(void) {
    char name[NAME_LEN];
    if (!next_token(name)) { fputs("name expected after TO\n", stderr); return; }
    if (state == 1 && compiling) log_put(name);
    if (state == 1 && compiling && cur_locals.n > 0) {
        for (int i = 0; i < cur_locals.n; i++)
            if (strcmp(cur_locals.name[i], name) == 0) {
                compile_xt(W_LIT); emit(i);
                compile_xt(W_LOCAL_STORE);
                return;
            }
    }
    char msg[NAME_LEN + 8];
    snprintf(msg, sizeof msg, "TO ? %s", name);
    throw_error(msg);
}

/* ===================================================================== */
/*  stack / arithmetic / logic                                           */
/* ===================================================================== */
/* DUP DROP SWAP OVER NIP TUCK ROT, + - *, NEGATE ABS 1+ 1- MIN MAX,
   AND OR XOR INVERT LSHIFT RSHIFT, the comparisons, ?DUP, @ ! EMIT CR,
   DEPTH , ALLOT, / MOD I J, . .S and PICK all live in the prelude. */

/* ===================================================================== */
/*  memory / variables                                                   */
/* ===================================================================== */
static void p_push_addr(void)  { push((cell)(mem + curr->data)); }
static void p_push_const(void) { push(curr->data); }

static void p_immediate(void) { if (latest) latest->flags |= F_IMMEDIATE; }

/* ---- CREATE / DOES> --------------------------------------------------- */
static Word *last_created = NULL;

static void p_create(void) {
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after CREATE");
    Word *w = newword(name, p_push_addr);
    w->data = memtop;
    last_created = w;
}

static void p_does_setup(void) {        /* (does>) */
    Word *w = last_created;
    if (!w) throw_error("DOES> without CREATE");
    w->code = p_does;
    w->body = (int)ip;                  /* does-body starts right after (does>) */
    ip = rstack[--rp];                  /* return from the defining word */
}

static void p_does(void) {              /* runtime of a CREATE..DOES> word */
    push((cell)(mem + curr->data));
    cell saved = ip;
    run_colon(curr);
    ip = saved;
}

static void p_does_quote(void) {        /* DOES> (immediate) */
    if (state != 1) throw_error("DOES> outside a definition");
    compile_xt(W_DOES);
}

/* JIT name -- compile a colon definition to native code */
static void p_jit(void) {
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after JIT");
    Word *w = find(name);
    if (!w) throw_error("? JIT");
    void *jit = jit_compile(w);
    if (!jit) throw_error("JIT: cannot compile word");
    w->native = jit;
    w->code = p_native;
}

/* JIT-ALL -- compile every colon word that can be compiled */
static void p_jit_all(void) {
    int count = 0;
    for (int i = 0; i < nwords; i++) {
        Word *w = &dict[i];
        if (w->body >= 0 && w->code == p_docol) {
            void *jit = jit_compile(w);
            if (jit) { w->native = jit; w->code = p_native; count++; }
        }
    }
    push(count);
}

/* ---- deferred words and anonymous definitions ------------------------- */
static void p_defer_run(void) {
    Word *target = (Word *)curr->data;
    if (!target) throw_error("deferred word not set");
    cell saved = ip;
    execute_word(target);
    ip = saved;
}

static void p_defer(void) {
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after DEFER");
    Word *w = newword(name, p_defer_run);
    w->data = 0;
}

static void p_is(void) {
    if (state == 1) throw_error("IS only in interpret state");
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after IS");
    Word *w = find(name);
    if (!w || w->code != p_defer_run) throw_error("IS: not a deferred word");
    w->data = pop();
}

static void p_action_of(void) {
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after ACTION-OF");
    Word *w = find(name);
    if (!w || w->code != p_defer_run) throw_error("ACTION-OF: not a deferred word");
    push(w->data);
}

static void p_noname(void) {
    char name[NAME_LEN];
    snprintf(name, sizeof name, "anon#%d", nwords);
    Word *w = newword(name, p_docol);
    w->flags |= F_HIDDEN;
    w->body = here;
    compiling = w;
    cur_locals.n = 0;
    cur_locals.ni = 0;
    log_reset();
    state = 1;
    push((cell)(intptr_t)w);
}

/* ---- synonyms, markers, forget ---------------------------------------- */
static void p_synonym(void) {          /* SYNONYM new old */
    char nname[NAME_LEN], oname[NAME_LEN];
    if (!next_token(nname)) throw_error("name expected after SYNONYM");
    if (!next_token(oname)) throw_error("name expected after SYNONYM");
    Word *ow = find(oname);
    if (!ow) throw_error("? SYNONYM");
    Word *w = newword(nname, ow->code);
    w->flags = ow->flags & (uint8_t)~F_HIDDEN;
    w->body = ow->body;
    w->body_end = ow->body_end;
    w->data = ow->data;
    w->native = ow->native;
    w->irbody = ow->irbody ? strdup(ow->irbody) : NULL;
}

typedef struct { int nwords; Word *latest; int memtop; int here; } MarkerState;
#define MAX_MARKERS 64
static MarkerState markers[MAX_MARKERS];
static int nmarkers = 0;

static void p_marker_run(void) {
    int idx = (int)curr->data;
    if (idx < 0 || idx >= nmarkers) throw_error("bad marker");
    MarkerState *m = &markers[idx];
    nwords = m->nwords;
    latest = m->latest;
    memtop = m->memtop;
    here = m->here;
    nmarkers = idx;
    compiling = NULL;
    state = 0;
}

static void p_marker(void) {
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after MARKER");
    if (nmarkers >= MAX_MARKERS) throw_error("too many markers");
    MarkerState *m = &markers[nmarkers];
    m->nwords = nwords;
    m->latest = latest;
    m->memtop = memtop;
    m->here = here;
    Word *w = newword(name, p_marker_run);
    w->data = nmarkers;
    nmarkers++;
}

static void p_forget(void) {           /* forget name and everything after */
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after FORGET");
    int i;
    for (i = 0; i < nwords; i++)
        if (strcmp(dict[i].name, name) == 0) break;
    if (i >= nwords) throw_error("? FORGET");
    nwords = i;
    latest = (i > 0) ? &dict[i - 1] : NULL;
}

/* ---- compiler kit ----------------------------------------------------- */
static void p_compile_comma(void) { Word *w = (Word *)pop(); compile_xt(w); }

/* ---- WORDS / SEE (kept in C: iteration + formatting) ------------------ */
static void p_words(void) {
    int col = 0;
    for (Word *w = latest; w; w = w->link) {
        if (w->flags & F_HIDDEN) continue;
        printf("%s ", w->name);
        if (++col % 8 == 0) putchar('\n');
    }
    putchar('\n');
}

static void p_see(void) {
    Word *w = (Word *)pop();
    if (!w) { printf("(null)\n"); return; }
    if (w->code == p_push_addr)  { printf("VARIABLE %s\n", w->name); return; }
    if (w->code == p_does)       { printf("CREATE %s ... DOES>\n", w->name); return; }
    if (w->code == p_push_const) { printf("CONSTANT %s = %ld\n", w->name, (long)w->data); return; }
    if (w->body >= 0) {
        if (w->src) { printf(": %s %s\n", w->name, w->src); return; }
        printf(": %s", w->name);
        int i = w->body, end = w->body_end;
        while (i >= 0 && i < end) {
            Word *x = (Word *)code[i];
            if (x == W_LIT) { printf(" %ld", (long)code[i + 1]); i += 2; }
            else if (x == W_BRANCH || x == W_0BRANCH) {
                printf(" %s %d", x == W_BRANCH ? "BRANCH" : "0BRANCH", (int)code[i + 1]);
                i += 2;
            } else { printf(" %s", x->name); i++; }
        }
        printf(" ;\n");
        return;
    }
    if (w->code == p_native && w->irbody) { printf("CODE %s %s\n", w->name, w->irbody); return; }
    printf("%s (primitive)\n", w->name);
}

/* ===================================================================== */
/*  comments                                                             */
/* ===================================================================== */
static void p_backslash(void) { while (*inbuf_ptr) inbuf_ptr++; }

static void p_paren(void) {
    char t[NAME_LEN];
    while (next_token(t)) if (strcmp(t, ")") == 0) return;
}

/* ---- PARSE-NAME and conditional compilation --------------------------- */
static void p_parse_name(void) {
    static char pn[NAME_LEN];
    if (!next_token(pn)) { push(0); push(0); return; }
    push((cell)(intptr_t)pn);
    push((cell)strlen(pn));
}

/* conditional compilation: when cond_skip is set, run_line drops every token
   until the matching [ELSE]/[THEN]; this persists across input lines. */
static int cond_skip = 0, cond_depth = 0;

static void p_bracket_if(void) {
    if (pop() == 0) { cond_skip = 1; cond_depth = 0; }
}

static void p_bracket_else(void) { cond_skip = 1; cond_depth = 0; }

static void p_bracket_then(void) { /* no-op */ }

/* ===================================================================== */
/*  tokenizer                                                            */
/* ===================================================================== */
static int next_token(char *out) {
    while (*inbuf_ptr && isspace((unsigned char)*inbuf_ptr)) inbuf_ptr++;
    if (!*inbuf_ptr) return 0;
    int n = 0;
    while (*inbuf_ptr && !isspace((unsigned char)*inbuf_ptr)) {
        if (n < NAME_LEN - 1) out[n++] = *inbuf_ptr;
        inbuf_ptr++;
    }
    out[n] = 0;
    return 1;
}

static int parse_number(const char *s, cell *out) {
    int base = 10;
    if (s[0] == '$') base = 16, s++;
    else if (s[0] == '#' && s[1]) base = 10, s++;
    else if (s[0] == '%' && s[1]) base = 2, s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) base = 16, s += 2;
    if (!*s) return 0;
    char *end;
    long v = strtol(s, &end, base);
    if (*end != 0) return 0;
    *out = v;
    return 1;
}

/* ===================================================================== */
/*  execution                                                            */
/* ===================================================================== */

/* run a colon word on the shared VM until it returns */
static void run_colon(Word *w) {
    if (rp >= STACK_SIZE) { raise(ERR_RSTK, "return stack overflow"); }
    rstack[rp++] = -1;          /* sentinel: end of outer frame */
    ip = w->body;
    while (ip >= 0) {
        Word *x = (Word *)code[ip++];
        curr = x;
        x->code();
    }
}

static void p_docol(void) {
    if (rp >= STACK_SIZE) { raise(ERR_RSTK, "return stack overflow"); }
    rstack[rp++] = ip;
    ip = curr->body;
}

static void execute_word(Word *w) {
    if (w->code == p_docol) run_colon(w);
    else { curr = w; w->code(); }
}

/* ===================================================================== */
/*  call-threaded JIT for colon definitions                              */
/* ===================================================================== */
typedef struct { struct sljit_jump *j; cell target; } JitJump;

/* the prelude EXIT word, whose body must return from the JIT, not the VM */
static Word *jit_exit = NULL;

/* Translate a colon body (code[body..body_end)) to a native void(void)
   function that calls primitives directly with native branches. Bodies
   containing DO/LOOP are left to the VM. Returns NULL on failure. */
/* copy S1 (register stack pointer) into the global sp */
static void jit_sync_out(struct sljit_compiler *c) {
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S1, 0);
    sljit_emit_op2(c, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)(intptr_t)dstack);
    sljit_emit_op2(c, SLJIT_LSHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 3);
    sljit_emit_op1(c, SLJIT_MOV32, SLJIT_MEM0(), (sljit_sw)&sp, SLJIT_R0, 0);
}

/* reload S1 from the global sp */
static void jit_sync_in(struct sljit_compiler *c) {
    sljit_emit_op1(c, SLJIT_MOV_U32, SLJIT_R0, 0, SLJIT_MEM0(), (sljit_sw)&sp);
    sljit_emit_op2(c, SLJIT_SHL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 3);
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)(intptr_t)dstack);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_R0, 0);
}

/* restore the caller's ip and return S1 */
static void jit_emit_ret(struct sljit_compiler *c) {
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_SP), 0);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM0(), (sljit_sw)&ip, SLJIT_R0, 0);
    sljit_emit_return(c, SLJIT_MOV_P, SLJIT_S1, 0);
}

/* Translate a colon body to a native (cell* sp) -> (cell* sp) function that
   keeps the stack pointer in a register. Prelude/native words are called
   through that same register ABI; C primitives (global stack) are synced.
   Bodies with DO/LOOP are left to the VM. Returns NULL on failure. */
static void *jit_compile(Word *w) {
    if (!w || w->body < 0 || w->body_end <= w->body) return NULL;
    if (!jit_exit) jit_exit = find("EXIT");

    cell start = w->body, end = w->body_end;
    cell n = end - start;

    unsigned char *istarget = calloc((size_t)n + 1, 1);
    if (!istarget) return NULL;
    for (cell pc = start; pc < end; ) {
        Word *x = (Word *)code[pc];
        if (x == W_LIT) { pc += 2; }
        else if (x == W_BRANCH || x == W_0BRANCH) {
            cell t = pc + 2 + code[pc + 1];
            if (t < start || t > end) { free(istarget); return NULL; }
            istarget[t - start] = 1;
            pc += 2;
        } else if (x == W_DO || x == W_LOOP || x == W_PLOOP) {
            free(istarget); return NULL;
        } else { pc += 1; }
    }

    struct sljit_compiler *c = sljit_create_compiler(NULL);
    if (!c) { free(istarget); return NULL; }
    sljit_emit_enter(c, 0, SLJIT_ARGS1(P, P), 2, 2, (sljit_s32)sizeof(cell));
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_S0, 0);       /* S1 = sp */
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM0(), (sljit_sw)&ip);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_SP), 0, SLJIT_R0, 0); /* save ip */

    struct sljit_label **labels = calloc((size_t)n + 1, sizeof(*labels));
    JitJump *jr = calloc((size_t)n + 1, sizeof(*jr));
    int njr = 0;
    if (!labels || !jr) {
        free(istarget); free(labels); free(jr);
        sljit_free_compiler(c);
        return NULL;
    }

    for (cell pc = start; pc < end; ) {
        cell k = pc - start;
        if (istarget[k] && !labels[k]) {
            struct sljit_label *L = sljit_emit_label(c);
            labels[k] = L;
            for (int i = 0; i < njr; i++)
                if (jr[i].target == pc) sljit_set_label(jr[i].j, L);
        }
        Word *x = (Word *)code[pc];
        if (x == W_LIT) {
            sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_S1), 0, SLJIT_IMM, code[pc + 1]);
            sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, sizeof(cell));
            pc += 2;
        } else if (x == W_BRANCH) {
            cell t = pc + 2 + code[pc + 1];
            struct sljit_jump *j = sljit_emit_jump(c, SLJIT_JUMP);
            if (labels[t - start]) sljit_set_label(j, labels[t - start]);
            else { jr[njr].j = j; jr[njr].target = t; njr++; }
            pc += 2;
        } else if (x == W_0BRANCH) {
            cell t = pc + 2 + code[pc + 1];
            sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1), -(sljit_sw)sizeof(cell));
            sljit_emit_op2(c, SLJIT_SUB, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, sizeof(cell));
            struct sljit_jump *j = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
            if (labels[t - start]) sljit_set_label(j, labels[t - start]);
            else { jr[njr].j = j; jr[njr].target = t; njr++; }
            pc += 2;
        } else if (x == W_EXIT || (jit_exit && x == jit_exit)) {
            jit_emit_ret(c);
            pc += 1;
        } else if (x->code == p_native && x->native) {
            /* prelude/native word: call it with the register ABI */
            sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S1, 0);
            sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS1(P, P),
                             SLJIT_IMM, (sljit_sw)(intptr_t)x->native);
            sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_R0, 0);
            pc += 1;
        } else {
            /* threaded colon or C primitive (global stack): sync around it */
            sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_MEM0(), (sljit_sw)&curr,
                           SLJIT_IMM, (sljit_sw)(intptr_t)x);
            jit_sync_out(c);
            if (x->code == p_docol) {
                sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)(intptr_t)x);
                sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS1V(P),
                                 SLJIT_IMM, (sljit_sw)(intptr_t)execute_word);
            } else {
                sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS0V(),
                                 SLJIT_IMM, (sljit_sw)(intptr_t)x->code);
            }
            jit_sync_in(c);
            pc += 1;
        }
    }
    if (!labels[n]) labels[n] = sljit_emit_label(c);
    for (int i = 0; i < njr; i++)
        if (jr[i].target == end) sljit_set_label(jr[i].j, labels[n]);
    jit_emit_ret(c);

    if (sljit_get_compiler_error(c) != SLJIT_SUCCESS) {
        free(istarget); free(labels); free(jr);
        sljit_free_compiler(c);
        return NULL;
    }
    void *jit = sljit_generate_code(c, 0, NULL);
    sljit_free_compiler(c);
    free(istarget); free(labels); free(jr);
    if (!jit) return NULL;
    if (njit < MAX_JIT) jit_codes[njit++] = jit;
    return jit;
}

static void interpret_token(const char *tok) {
    if (state == 2) {
        if (strcmp(tok, ";CODE") == 0) { p_endcode(); return; }
        if (strcmp(tok, "IR\"") == 0) { p_irquote(); return; }
        if (strcmp(tok, "\\") == 0) { while (*inbuf_ptr) inbuf_ptr++; return; }
        if (strcmp(tok, "(") == 0) {
            char t[NAME_LEN];
            while (next_token(t)) if (strcmp(t, ")") == 0) return;
            throw_error("unterminated ( in CODE");
        }
        ir_put(tok);
        return;
    }
    if (state == 1 && compiling &&
        strcmp(tok, "\\") != 0 && strcmp(tok, "(") != 0 && strcmp(tok, "{") != 0)
        log_put(tok);
    if (state == 1 && compiling && cur_locals.n > 0) {
        for (int i = 0; i < cur_locals.n; i++)
            if (strcmp(cur_locals.name[i], tok) == 0) {
                compile_xt(W_LIT); emit(i);
                compile_xt(W_LOCAL);
                return;
            }
    }
    Word *w = find(tok);
    if (w) {
        if (state == 1 && !(w->flags & F_IMMEDIATE)) {
            if (cur_locals.n > 0 && w->code == p_native && strcmp(w->name, "EXIT") == 0)
                compile_xt(W_LOCALS_EXIT);
            compile_xt(w);
        } else execute_word(w);
        return;
    }
    cell v;
    if (parse_number(tok, &v)) {
        if (state == 1) { compile_xt(W_LIT); emit(v); }
        else push(v);
        return;
    }
    char msg[NAME_LEN + 8];
    snprintf(msg, sizeof msg, "? %s", tok);
    raise(ERR_UNKNOWN, msg);
}

/* ===================================================================== */
/*  dynamic library registry (dlopen / dlsym)                            */
/* ===================================================================== */

#define MAX_DYNLIB 64
#define MAX_DYNSYM 256
#define DYN_NAME   128

typedef struct { char name[DYN_NAME]; void *handle; int flags; } DynLib;
typedef struct { void *handle; char name[DYN_NAME]; void *addr; } DynSym;

static DynLib dynlibs[MAX_DYNLIB];
static int    nlibs = 0;
static DynSym dynsyms[MAX_DYNSYM];
static int    nsyms = 0;
static char   dyn_err[256] = "";

static void dyn_seterr(const char *m) {
    if (!m) m = "unknown dl error";
    strncpy(dyn_err, m, sizeof dyn_err - 1);
    dyn_err[sizeof dyn_err - 1] = 0;
}

/* open a library, reusing an existing handle; errors are reported via dyn_err */
static void *dyn_open(const char *name, int flags) {
    for (int i = 0; i < nlibs; i++)
        if (strcmp(dynlibs[i].name, name) == 0) return dynlibs[i].handle;
    if (nlibs >= MAX_DYNLIB) { dyn_seterr("too many libraries"); return NULL; }
    dlerror();
    void *h = dlopen(name, flags);
    if (!h) { const char *e = dlerror(); dyn_seterr(e ? e : "dlopen failed"); return NULL; }
    strncpy(dynlibs[nlibs].name, name, DYN_NAME - 1);
    dynlibs[nlibs].name[DYN_NAME - 1] = 0;
    dynlibs[nlibs].handle = h;
    dynlibs[nlibs].flags = flags;
    nlibs++;
    dyn_err[0] = 0;
    return h;
}

/* resolve a symbol in a handle, with a cache; NULL on failure */
static void *dyn_sym(void *handle, const char *name) {
    for (int i = 0; i < nsyms; i++)
        if (dynsyms[i].handle == handle && strcmp(dynsyms[i].name, name) == 0)
            return dynsyms[i].addr;
    if (nsyms >= MAX_DYNSYM) { dyn_seterr("too many symbols"); return NULL; }
    dlerror();
    void *a = dlsym(handle, name);
    const char *e = dlerror();
    if (e) { dyn_seterr(e); return NULL; }
    dynsyms[nsyms].handle = handle;
    strncpy(dynsyms[nsyms].name, name, DYN_NAME - 1);
    dynsyms[nsyms].name[DYN_NAME - 1] = 0;
    dynsyms[nsyms].addr = a;
    nsyms++;
    dyn_err[0] = 0;
    return a;
}

/* lib empty -> global scope (RTLD_DEFAULT); otherwise open lib on demand */
static void *dyn_resolve(const char *lib, const char *name) {
    void *handle = RTLD_DEFAULT;
    if (lib && *lib) {
        handle = dyn_open(lib, RTLD_NOW | RTLD_GLOBAL);
        if (!handle) return NULL;
    }
    return dyn_sym(handle, name);
}

static void dyn_forget_handle(void *handle) {
    int j = 0;
    for (int i = 0; i < nsyms; i++)
        if (dynsyms[i].handle != handle) dynsyms[j++] = dynsyms[i];
    nsyms = j;
}

static void dyn_close(void *handle) {
    for (int i = 0; i < nlibs; i++) {
        if (dynlibs[i].handle == handle) {
            dyn_forget_handle(handle);
            dlclose(handle);
            for (int j = i; j < nlibs - 1; j++) dynlibs[j] = dynlibs[j + 1];
            nlibs--;
            return;
        }
    }
    dyn_forget_handle(handle);
    dlclose(handle);
}

static void dyn_close_all(void) {
    for (int i = nlibs - 1; i >= 0; i--) dlclose(dynlibs[i].handle);
    nlibs = 0;
    nsyms = 0;
}

/* copy a Forth ( addr len ) string into a NUL-terminated buffer */
static int copy_cstr(cell addr, cell len, char *buf, int cap) {
    if (len < 0 || len >= cap) return 0;
    memcpy(buf, (void *)addr, (size_t)len);
    buf[len] = 0;
    return 1;
}

/* ===================================================================== */
/*  native code: SLJIT LIR assembler                                     */
/* ===================================================================== */

#define IRBUF_SIZE (1 << 16)
#define MAX_IRTOK  8192
#define MAX_IRLAB  256
#define MAX_IRJMP  512

static char  irbuf[IRBUF_SIZE];
static int   irlen = 0;

static char  irtok[MAX_IRTOK][64];
static int   irtok_count = 0;
static int   irtok_pos = 0;

static char  label_names[MAX_IRLAB][32];
static struct sljit_label *label_ptrs[MAX_IRLAB];
static int   nlabels = 0;
static char  jump_names[MAX_IRJMP][32];
static struct sljit_jump  *jump_ptrs[MAX_IRJMP];
static int   njumps = 0;

static int   forth_abi = 0;
static int   forth_local = 0;
static int   entered = 0;
static int   returned = 0;
static int   enter_fsc = 0, enter_fsv = 0, enter_vsc = 0, enter_vsv = 0;

/* runtime patch registry (rewritable jumps / constants) */
#define MAX_PATCH 256
typedef struct { char name[32]; sljit_uw addr; } PatchJump;
typedef struct { char name[32]; sljit_uw addr; sljit_s32 op; } PatchConst;
static PatchJump patch_jumps[MAX_PATCH];
static int       npatch_jumps = 0;
static PatchConst patch_consts[MAX_PATCH];
static int       npatch_consts = 0;
static sljit_sw  patch_exec_off = 0;
static unsigned long last_gen_size = 0;

static struct sljit_jump  *pend_jumps[MAX_PATCH];
static char   pend_jump_names[MAX_PATCH][32];
static int    n_pend_jumps = 0;
static struct sljit_const *pend_consts[MAX_PATCH];
static char   pend_const_names[MAX_PATCH][32];
static sljit_s32 pend_const_ops[MAX_PATCH];
static int    n_pend_consts = 0;
static sljit_s32 cur_ret = SLJIT_ARG_TYPE_RET_VOID;
static sljit_s32 cur_argtypes = 0;

typedef struct { const char *name; long val; } NameVal;
typedef struct { const char *name; sljit_s32 op; } NameOp;

/* SLJIT constants exposed both as Forth words and as IR tokens */
#define CONST(name) {#name, name}
static const NameVal sljit_consts[] = {
    /* registers */
    CONST(SLJIT_R0), CONST(SLJIT_R1), CONST(SLJIT_R2), CONST(SLJIT_R3),
    CONST(SLJIT_R4), CONST(SLJIT_R5), CONST(SLJIT_R6), CONST(SLJIT_R7),
    CONST(SLJIT_R8), CONST(SLJIT_R9),
    CONST(SLJIT_S0), CONST(SLJIT_S1), CONST(SLJIT_S2), CONST(SLJIT_S3),
    CONST(SLJIT_S4), CONST(SLJIT_S5), CONST(SLJIT_S6), CONST(SLJIT_S7),
    CONST(SLJIT_S8), CONST(SLJIT_S9), CONST(SLJIT_SP),
    CONST(SLJIT_FR0), CONST(SLJIT_FR1), CONST(SLJIT_FR2), CONST(SLJIT_FR3),
    CONST(SLJIT_FR4), CONST(SLJIT_FR5), CONST(SLJIT_FR6), CONST(SLJIT_FR7),
    CONST(SLJIT_FR8), CONST(SLJIT_FR9),
    CONST(SLJIT_FS0), CONST(SLJIT_FS1), CONST(SLJIT_FS2), CONST(SLJIT_FS3),
    CONST(SLJIT_FS4), CONST(SLJIT_FS5), CONST(SLJIT_FS6), CONST(SLJIT_FS7),
    CONST(SLJIT_FS8), CONST(SLJIT_FS9),
    CONST(SLJIT_VR0), CONST(SLJIT_VR1), CONST(SLJIT_VR2), CONST(SLJIT_VR3),
    CONST(SLJIT_VR4), CONST(SLJIT_VR5), CONST(SLJIT_VR6), CONST(SLJIT_VR7),
    CONST(SLJIT_VR8), CONST(SLJIT_VR9),
    CONST(SLJIT_VS0), CONST(SLJIT_VS1), CONST(SLJIT_VS2), CONST(SLJIT_VS3),
    CONST(SLJIT_VS4), CONST(SLJIT_VS5), CONST(SLJIT_VS6), CONST(SLJIT_VS7),
    CONST(SLJIT_VS8), CONST(SLJIT_VS9),
    /* operands */
    CONST(SLJIT_MEM), CONST(SLJIT_IMM), CONST(SLJIT_32), CONST(SLJIT_SET_Z),
    CONST(SLJIT_MEM_LOAD), CONST(SLJIT_MEM_STORE), CONST(SLJIT_MEM_UNALIGNED),
    CONST(SLJIT_MEM_ALIGNED_16), CONST(SLJIT_MEM_ALIGNED_32),
    CONST(SLJIT_MEM_PRE), CONST(SLJIT_MEM_POST), CONST(SLJIT_MEM_SUPP),
    /* integer op1 */
    CONST(SLJIT_MOV), CONST(SLJIT_MOV_P), CONST(SLJIT_MOV_U8), CONST(SLJIT_MOV_S8),
    CONST(SLJIT_MOV_U16), CONST(SLJIT_MOV_S16), CONST(SLJIT_MOV_U32),
    CONST(SLJIT_MOV_S32), CONST(SLJIT_MOV32),
    CONST(SLJIT_CLZ), CONST(SLJIT_CTZ), CONST(SLJIT_REV),
    /* integer op2 / op0 */
    CONST(SLJIT_ADD), CONST(SLJIT_SUB), CONST(SLJIT_MUL), CONST(SLJIT_AND),
    CONST(SLJIT_OR), CONST(SLJIT_XOR), CONST(SLJIT_SHL), CONST(SLJIT_LSHR),
    CONST(SLJIT_ASHR), CONST(SLJIT_ROTL), CONST(SLJIT_ROTR), CONST(SLJIT_MULADD),
    CONST(SLJIT_LMUL_UW), CONST(SLJIT_LMUL_SW), CONST(SLJIT_DIVMOD_UW),
    CONST(SLJIT_DIVMOD_SW), CONST(SLJIT_DIV_UW), CONST(SLJIT_DIV_SW),
    CONST(SLJIT_NOP), CONST(SLJIT_BREAKPOINT), CONST(SLJIT_MEMORY_BARRIER),
    CONST(SLJIT_ENDBR), CONST(SLJIT_SKIP_FRAMES_BEFORE_RETURN),
    /* flags and conditions */
    CONST(SLJIT_SET_LESS), CONST(SLJIT_SET_GREATER), CONST(SLJIT_SET_GREATER_EQUAL),
    CONST(SLJIT_SET_LESS_EQUAL), CONST(SLJIT_SET_SIG_LESS),
    CONST(SLJIT_SET_SIG_GREATER), CONST(SLJIT_SET_SIG_GREATER_EQUAL),
    CONST(SLJIT_SET_SIG_LESS_EQUAL), CONST(SLJIT_SET_OVERFLOW), CONST(SLJIT_SET_CARRY),
    CONST(SLJIT_SET_ATOMIC_STORED),
    CONST(SLJIT_JUMP_IF_ZERO), CONST(SLJIT_JUMP_IF_NON_ZERO),
    CONST(SLJIT_EQUAL), CONST(SLJIT_NOT_EQUAL), CONST(SLJIT_LESS),
    CONST(SLJIT_LESS_EQUAL), CONST(SLJIT_GREATER), CONST(SLJIT_GREATER_EQUAL),
    CONST(SLJIT_SIG_LESS), CONST(SLJIT_SIG_LESS_EQUAL), CONST(SLJIT_SIG_GREATER),
    CONST(SLJIT_SIG_GREATER_EQUAL), CONST(SLJIT_ZERO), CONST(SLJIT_NOT_ZERO),
    CONST(SLJIT_OVERFLOW), CONST(SLJIT_NOT_OVERFLOW), CONST(SLJIT_CARRY),
    CONST(SLJIT_NOT_CARRY), CONST(SLJIT_ATOMIC_STORED), CONST(SLJIT_ATOMIC_NOT_STORED),
    /* control / calls / addresses */
    CONST(SLJIT_JUMP), CONST(SLJIT_FAST_CALL), CONST(SLJIT_CALL),
    CONST(SLJIT_CALL_REG_ARG), CONST(SLJIT_REWRITABLE_JUMP), CONST(SLJIT_CALL_RETURN),
    CONST(SLJIT_FAST_RETURN), CONST(SLJIT_SKIP_FRAMES_BEFORE_FAST_RETURN),
    CONST(SLJIT_PREFETCH_L1), CONST(SLJIT_PREFETCH_L2), CONST(SLJIT_PREFETCH_L3),
    CONST(SLJIT_PREFETCH_ONCE), CONST(SLJIT_FAST_ENTER), CONST(SLJIT_GET_RETURN_ADDRESS),
    CONST(SLJIT_MOV_ADDR), CONST(SLJIT_MOV_ABS_ADDR), CONST(SLJIT_ADD_ABS_ADDR),
    CONST(SLJIT_LABEL_ALIGN_1), CONST(SLJIT_LABEL_ALIGN_2), CONST(SLJIT_LABEL_ALIGN_4),
    CONST(SLJIT_LABEL_ALIGN_8), CONST(SLJIT_LABEL_ALIGN_16),
    CONST(SLJIT_LABEL_ALIGN_W), CONST(SLJIT_LABEL_ALIGN_P),
    /* floating point */
    CONST(SLJIT_MOV_F64), CONST(SLJIT_MOV_F32),
    CONST(SLJIT_CONV_F64_FROM_F32), CONST(SLJIT_CONV_F32_FROM_F64),
    CONST(SLJIT_CONV_SW_FROM_F64), CONST(SLJIT_CONV_SW_FROM_F32),
    CONST(SLJIT_CONV_S32_FROM_F64), CONST(SLJIT_CONV_S32_FROM_F32),
    CONST(SLJIT_CONV_F64_FROM_SW), CONST(SLJIT_CONV_F32_FROM_SW),
    CONST(SLJIT_CONV_F64_FROM_S32), CONST(SLJIT_CONV_F32_FROM_S32),
    CONST(SLJIT_CONV_F64_FROM_UW), CONST(SLJIT_CONV_F32_FROM_UW),
    CONST(SLJIT_CONV_F64_FROM_U32), CONST(SLJIT_CONV_F32_FROM_U32),
    CONST(SLJIT_CMP_F64), CONST(SLJIT_CMP_F32),
    CONST(SLJIT_NEG_F64), CONST(SLJIT_NEG_F32), CONST(SLJIT_ABS_F64), CONST(SLJIT_ABS_F32),
    CONST(SLJIT_ADD_F64), CONST(SLJIT_ADD_F32), CONST(SLJIT_SUB_F64), CONST(SLJIT_SUB_F32),
    CONST(SLJIT_MUL_F64), CONST(SLJIT_MUL_F32), CONST(SLJIT_DIV_F64), CONST(SLJIT_DIV_F32),
    CONST(SLJIT_COPYSIGN_F64), CONST(SLJIT_COPYSIGN_F32),
    CONST(SLJIT_COPY_TO_F64), CONST(SLJIT_COPY_FROM_F64),
    CONST(SLJIT_COPY32_TO_F32), CONST(SLJIT_COPY32_FROM_F32),
    CONST(SLJIT_F_EQUAL), CONST(SLJIT_F_NOT_EQUAL), CONST(SLJIT_F_LESS),
    CONST(SLJIT_F_GREATER_EQUAL), CONST(SLJIT_F_GREATER), CONST(SLJIT_F_LESS_EQUAL),
    CONST(SLJIT_UNORDERED), CONST(SLJIT_ORDERED), CONST(SLJIT_ORDERED_EQUAL),
    CONST(SLJIT_ORDERED_LESS), CONST(SLJIT_ORDERED_GREATER),
    CONST(SLJIT_ORDERED_LESS_EQUAL), CONST(SLJIT_ORDERED_GREATER_EQUAL),
    /* signatures */
    CONST(SLJIT_ARG_TYPE_W), CONST(SLJIT_ARG_TYPE_P), CONST(SLJIT_ARG_TYPE_32),
    CONST(SLJIT_ARG_TYPE_F64), CONST(SLJIT_ARG_TYPE_F32),
    /* cpu feature queries */
    CONST(SLJIT_HAS_FPU), CONST(SLJIT_HAS_CLZ), CONST(SLJIT_HAS_CTZ),
    CONST(SLJIT_HAS_REV), CONST(SLJIT_HAS_ROT), CONST(SLJIT_HAS_CMOV),
    CONST(SLJIT_HAS_ATOMIC), CONST(SLJIT_HAS_SIMD),
    {NULL, 0}
};
#undef CONST

static const NameOp op1_table[] = {
    {"mov", SLJIT_MOV}, {"mov.p", SLJIT_MOV_P},
    {"mov.u8", SLJIT_MOV_U8}, {"mov.s8", SLJIT_MOV_S8},
    {"mov.u16", SLJIT_MOV_U16}, {"mov.s16", SLJIT_MOV_S16},
    {"mov.u32", SLJIT_MOV_U32}, {"mov.s32", SLJIT_MOV_S32},
    {"mov32", SLJIT_MOV32},
    {"clz", SLJIT_CLZ}, {"ctz", SLJIT_CTZ}, {"rev", SLJIT_REV},
    {NULL, 0}
};

static const NameOp op2_table[] = {
    {"add", SLJIT_ADD}, {"sub", SLJIT_SUB}, {"mul", SLJIT_MUL},
    {"and", SLJIT_AND}, {"or", SLJIT_OR}, {"xor", SLJIT_XOR},
    {"shl", SLJIT_SHL}, {"lshr", SLJIT_LSHR}, {"ashr", SLJIT_ASHR},
    {"rotl", SLJIT_ROTL}, {"rotr", SLJIT_ROTR},
    {NULL, 0}
};

static int sljit_const_lookup(const char *name, long *out) {
    for (int i = 0; sljit_consts[i].name; i++)
        if (strcmp(sljit_consts[i].name, name) == 0) { *out = sljit_consts[i].val; return 1; }
    return 0;
}

/* ---- IR buffer -------------------------------------------------------- */
static void ir_put_text(const char *s, int len) {
    if (irlen + len + 2 >= IRBUF_SIZE) throw_error("IR buffer full");
    if (irlen) irbuf[irlen++] = ' ';
    memcpy(irbuf + irlen, s, (size_t)len);
    irlen += len;
    irbuf[irlen] = 0;
}

static void ir_put(const char *tok) { ir_put_text(tok, (int)strlen(tok)); }

static void tokenize_text(const char *text, char (*out)[64], int *count, int max) {
    const char *p = text;
    *count = 0;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        int n = 0;
        while (*p && !isspace((unsigned char)*p) && *p != ',') {
            if (n < 63) out[*count][n++] = *p;
            p++;
        }
        out[*count][n] = 0;
        (*count)++;
        if (*count >= max) throw_error("too many IR tokens");
    }
}

static void append_token(const char *t) {
    if (irtok_count >= MAX_IRTOK) throw_error("too many IR tokens");
    size_t L = strlen(t);
    if (L > 63) L = 63;
    memcpy(irtok[irtok_count], t, L);
    irtok[irtok_count][L] = 0;
    irtok_count++;
}

/* Expand a CODE word's stored IR body, renaming its labels so several
   expansions of the same word do not collide. */
static int inline_id = 0;
static int inline_depth = 0;

static void expand_body(Word *w, int id) {
    if (!w->irbody) throw_error("inline: word has no native body");
    if (++inline_depth > 16) throw_error("inline: too deep");
    static char btok[2048][64];
    int bn = 0;
    tokenize_text(w->irbody, btok, &bn, 2048);
    for (int i = 0; i < bn; i++) {
        if (strcmp(btok[i], "inline") == 0) {
            if (i + 1 >= bn) throw_error("inline: missing name");
            Word *t = find(btok[++i]);
            if (!t) throw_error("inline: unknown word");
            expand_body(t, ++inline_id);
        } else if (strcmp(btok[i], "label:") == 0 || strcmp(btok[i], "setlabel") == 0) {
            append_token(btok[i]);
            if (i + 1 >= bn) throw_error("inline: label name missing");
            char nm[80];
            snprintf(nm, sizeof nm, "%s#%d", btok[++i], id);
            append_token(nm);
        } else if (strcmp(btok[i], "aligned_label") == 0) {
            append_token(btok[i]);
            if (i + 2 >= bn) throw_error("inline: aligned_label has too few arguments");
            append_token(btok[++i]);
            char nm[80];
            snprintf(nm, sizeof nm, "%s#%d", btok[++i], id);
            append_token(nm);
        } else if (btok[i][0] == '@') {
            char nm[80];
            snprintf(nm, sizeof nm, "@%s#%d", btok[i] + 1, id);
            append_token(nm);
        } else {
            append_token(btok[i]);
        }
    }
    inline_depth--;
}

static void ir_tokenize(void) {
    static char raw[MAX_IRTOK][64];
    int rn = 0;
    tokenize_text(irbuf, raw, &rn, MAX_IRTOK);
    irtok_count = 0;
    inline_id = 0;
    inline_depth = 0;
    for (int i = 0; i < rn; i++) {
        if (strcmp(raw[i], "inline") == 0) {
            if (i + 1 >= rn) throw_error("inline: missing name");
            Word *w = find(raw[++i]);
            if (!w) throw_error("inline: unknown word");
            expand_body(w, ++inline_id);
        } else {
            append_token(raw[i]);
        }
    }
}

static const char *ir_need(void) {
    if (irtok_pos >= irtok_count) throw_error("unexpected end of native code");
    return irtok[irtok_pos++];
}

/* ---- operands --------------------------------------------------------- */
static sljit_s32 parse_reg(const char *s) {
    if (s[0] == 'R' && isdigit((unsigned char)s[1])) {
        int i = atoi(s + 1);
        if (i < 0 || i >= SLJIT_NUMBER_OF_REGISTERS) throw_error("bad register");
        return (sljit_s32)SLJIT_R(i);
    }
    if (s[0] == 'S' && isdigit((unsigned char)s[1])) {
        int i = atoi(s + 1);
        if (i < 0 || i >= SLJIT_NUMBER_OF_SAVED_REGISTERS) throw_error("bad saved register");
        return (sljit_s32)SLJIT_S(i);
    }
    if (s[0] == 'F' && s[1] == 'S' && isdigit((unsigned char)s[2])) {
        int i = atoi(s + 2);
        if (i < 0 || i >= SLJIT_NUMBER_OF_SAVED_FLOAT_REGISTERS) throw_error("bad saved float register");
        return (sljit_s32)SLJIT_FS(i);
    }
    if (s[0] == 'F' && isdigit((unsigned char)s[1])) {
        int i = atoi(s + 1);
        if (i < 0 || i >= SLJIT_NUMBER_OF_FLOAT_REGISTERS) throw_error("bad float register");
        return (sljit_s32)SLJIT_FR(i);
    }
    if (s[0] == 'V' && s[1] == 'S' && isdigit((unsigned char)s[2])) {
        int i = atoi(s + 2);
        if (i < 0 || i >= SLJIT_NUMBER_OF_SAVED_VECTOR_REGISTERS) throw_error("bad saved vector register");
        return (sljit_s32)SLJIT_VS(i);
    }
    if (s[0] == 'V' && isdigit((unsigned char)s[1])) {
        int i = atoi(s + 1);
        if (i < 0 || i >= SLJIT_NUMBER_OF_VECTOR_REGISTERS) throw_error("bad vector register");
        return (sljit_s32)SLJIT_VR(i);
    }
    if (strcmp(s, "SP") == 0) return SLJIT_SP;
    throw_error("bad register");
    return 0;
}

static void parse_mem(const char *tok, sljit_s32 *para, sljit_sw *w) {
    char buf[64];
    size_t len = strlen(tok) - 2;
    if (len >= sizeof buf) throw_error("memory operand too long");
    memcpy(buf, tok + 1, len);
    buf[len] = 0;

    if (buf[0] == '#') {           /* absolute address: [#123] */
        *para = SLJIT_MEM0();
        *w = (sljit_sw)strtol(buf + 1, NULL, 0);
        return;
    }

    char *sh = strstr(buf, "<<");
    if (sh) {
        *w = (sljit_sw)strtol(sh + 2, NULL, 0);
        *sh = 0;
        char *plus = strchr(buf, '+');
        if (!plus) throw_error("bad indexed memory operand");
        *plus = 0;
        sljit_s32 r1 = parse_reg(buf);
        sljit_s32 r2 = parse_reg(plus + 1);
        *para = SLJIT_MEM2(r1, r2);
        return;
    }
    char *plus = strchr(buf, '+');
    if (plus) {
        *plus = 0;
        *para = SLJIT_MEM1(parse_reg(buf));
        *w = (sljit_sw)strtol(plus + 1, NULL, 0);
        return;
    }
    char *minus = strchr(buf, '-');
    if (minus) {
        *minus = 0;
        *para = SLJIT_MEM1(parse_reg(buf));
        *w = -(sljit_sw)strtol(minus + 1, NULL, 0);
        return;
    }
    *para = SLJIT_MEM1(parse_reg(buf));
    *w = 0;
}

static void parse_operand(const char *tok, sljit_s32 *para, sljit_sw *w) {
    size_t len = strlen(tok);
    if (tok[0] == '#') { *para = SLJIT_IMM; *w = (sljit_sw)strtol(tok + 1, NULL, 0); return; }
    if (tok[0] == '&') {
        const char *spec = tok + 1;
        char lib[DYN_NAME];
        char sym[DYN_NAME];
        const char *sep = strstr(spec, "::");
        if (sep) {
            size_t ll = (size_t)(sep - spec);
            if (ll >= DYN_NAME) throw_error("library name too long");
            memcpy(lib, spec, ll);
            lib[ll] = 0;
            strncpy(sym, sep + 2, DYN_NAME - 1);
            sym[DYN_NAME - 1] = 0;
        } else {
            lib[0] = 0;
            strncpy(sym, spec, DYN_NAME - 1);
            sym[DYN_NAME - 1] = 0;
        }
        if (!lib[0]) {                   /* interpreter state addresses */
            void *pv = NULL;
            if      (strcmp(sym, "dstack") == 0) pv = dstack;
            else if (strcmp(sym, "rstack") == 0) pv = rstack;
            else if (strcmp(sym, "mem") == 0)    pv = mem;
            else if (strcmp(sym, "sp") == 0)     pv = &sp;
            else if (strcmp(sym, "rp") == 0)     pv = &rp;
            else if (strcmp(sym, "memtop") == 0) pv = &memtop;
            else if (strcmp(sym, "forth_raise") == 0) pv = (void *)(intptr_t)forth_raise;
            else if (strcmp(sym, "forth_divzero") == 0) pv = (void *)(intptr_t)forth_divzero;
            else if (strcmp(sym, "forth_need") == 0) pv = (void *)(intptr_t)forth_need;
            else if (strcmp(sym, "forth_room") == 0) pv = (void *)(intptr_t)forth_room;
            else if (strcmp(sym, "forth_type") == 0) pv = (void *)(intptr_t)forth_type;
            /* dictionary access helpers */
            else if (strcmp(sym, "forth_find") == 0) pv = (void *)(intptr_t)forth_find;
            else if (strcmp(sym, "forth_latest") == 0) pv = (void *)(intptr_t)forth_latest;
            else if (strcmp(sym, "forth_link") == 0) pv = (void *)(intptr_t)forth_link;
            else if (strcmp(sym, "forth_name") == 0) pv = (void *)(intptr_t)forth_name;
            else if (strcmp(sym, "forth_flags") == 0) pv = (void *)(intptr_t)forth_flags;
            else if (strcmp(sym, "forth_code") == 0) pv = (void *)(intptr_t)forth_code;
            else if (strcmp(sym, "forth_native") == 0) pv = (void *)(intptr_t)forth_native;
            else if (strcmp(sym, "forth_data") == 0) pv = (void *)(intptr_t)forth_data;
            else if (strcmp(sym, "forth_body_start") == 0) pv = (void *)(intptr_t)forth_body_start;
            else if (strcmp(sym, "forth_body_end") == 0) pv = (void *)(intptr_t)forth_body_end;
            else if (strcmp(sym, "forth_body") == 0) pv = (void *)(intptr_t)forth_body;
            else if (strcmp(sym, "forth_immediate") == 0) pv = (void *)(intptr_t)forth_immediate;
            else if (strcmp(sym, "forth_hidden") == 0) pv = (void *)(intptr_t)forth_hidden;
            else if (strcmp(sym, "forth_colon_p") == 0) pv = (void *)(intptr_t)forth_colon_p;
            else if (strcmp(sym, "forth_native_p") == 0) pv = (void *)(intptr_t)forth_native_p;
            else if (strcmp(sym, "forth_variable_p") == 0) pv = (void *)(intptr_t)forth_variable_p;
            /* interpreter / compiler state */
            else if (strcmp(sym, "code") == 0) pv = (void *)code;
            else if (strcmp(sym, "here") == 0) pv = &here;
            else if (strcmp(sym, "ip") == 0) pv = &ip;
            else if (strcmp(sym, "state") == 0) pv = &state;
            else if (strcmp(sym, "compiling") == 0) pv = &compiling;
            else if (strcmp(sym, "latest") == 0) pv = &latest;
            else if (strcmp(sym, "nwords") == 0) pv = &nwords;
            else if (strcmp(sym, "dict") == 0) pv = (void *)dict;
            if (pv) { *para = SLJIT_IMM; *w = (sljit_sw)(intptr_t)pv; return; }
        }
        void *a = NULL;
        Word *fw = find(sym);            /* prefer a native Forth word */
        if (fw && fw->native) a = fw->native;
        else a = dyn_resolve(lib, sym);
        if (!a) throw_error(dyn_err[0] ? dyn_err : "symbol not found");
        *para = SLJIT_IMM;
        *w = (sljit_sw)(intptr_t)a;
        return;
    }
    if (len >= 2 && tok[0] == '[' && tok[len - 1] == ']') { parse_mem(tok, para, w); return; }
    *para = parse_reg(tok);
    *w = 0;
}

/* parse an operand when only the base/register code matters */
static void parse_operand_p(const char *tok, sljit_s32 *para) {
    sljit_sw z;
    parse_operand(tok, para, &z);
}

static int looks_like_operand(const char *s) {
    if (s[0] == '#' || s[0] == '[' || s[0] == '&') return 1;
    if ((s[0] == 'R' || s[0] == 'S' || s[0] == 'F' || s[0] == 'V') &&
        (isdigit((unsigned char)s[1]) ||
         ((s[1] == 'S') && isdigit((unsigned char)s[2])))) return 1;
    return strcmp(s, "SP") == 0;
}

static sljit_s32 parse_opnum(const char *s) {
    long v;
    if (sljit_const_lookup(s, &v)) return (sljit_s32)v;
    char *end;
    long n = strtol(s, &end, 0);
    if (*end) throw_error("bad opcode");
    return (sljit_s32)n;
}

static int type_from_name(const char *s) {
    if (strcmp(s, "V") == 0)   return SLJIT_ARG_TYPE_RET_VOID;
    if (strcmp(s, "W") == 0)   return SLJIT_ARG_TYPE_W;
    if (strcmp(s, "32") == 0)  return SLJIT_ARG_TYPE_32;
    if (strcmp(s, "P") == 0)   return SLJIT_ARG_TYPE_P;
    if (strcmp(s, "F64") == 0) return SLJIT_ARG_TYPE_F64;
    if (strcmp(s, "F32") == 0) return SLJIT_ARG_TYPE_F32;
    return -1;
}

static sljit_s32 cond_lookup(const char *s) {
    if (strcmp(s, "eq") == 0)   return SLJIT_EQUAL;
    if (strcmp(s, "ne") == 0)   return SLJIT_NOT_EQUAL;
    if (strcmp(s, "lt") == 0)   return SLJIT_SIG_LESS;
    if (strcmp(s, "le") == 0)   return SLJIT_SIG_LESS_EQUAL;
    if (strcmp(s, "gt") == 0)   return SLJIT_SIG_GREATER;
    if (strcmp(s, "ge") == 0)   return SLJIT_SIG_GREATER_EQUAL;
    if (strcmp(s, "ult") == 0)  return SLJIT_LESS;
    if (strcmp(s, "ule") == 0)  return SLJIT_LESS_EQUAL;
    if (strcmp(s, "ugt") == 0)  return SLJIT_GREATER;
    if (strcmp(s, "uge") == 0)  return SLJIT_GREATER_EQUAL;
    if (strcmp(s, "zero") == 0) return SLJIT_ZERO;
    if (strcmp(s, "nz") == 0)   return SLJIT_NOT_ZERO;
    return -1;
}

/* the status-flag setter that corresponds to a comparison type */
static sljit_s32 cmp_set_flag(sljit_s32 ty) {
    switch (ty) {
    case SLJIT_EQUAL: case SLJIT_NOT_EQUAL:         return SLJIT_SET_Z;
    case SLJIT_LESS:                                return SLJIT_SET_LESS;
    case SLJIT_GREATER_EQUAL:                       return SLJIT_SET_GREATER_EQUAL;
    case SLJIT_GREATER:                             return SLJIT_SET_GREATER;
    case SLJIT_LESS_EQUAL:                          return SLJIT_SET_LESS_EQUAL;
    case SLJIT_SIG_LESS:                            return SLJIT_SET_SIG_LESS;
    case SLJIT_SIG_GREATER_EQUAL:                   return SLJIT_SET_SIG_GREATER_EQUAL;
    case SLJIT_SIG_GREATER:                         return SLJIT_SET_SIG_GREATER;
    case SLJIT_SIG_LESS_EQUAL:                      return SLJIT_SET_SIG_LESS_EQUAL;
    case SLJIT_OVERFLOW:                            return SLJIT_SET_OVERFLOW;
    case SLJIT_CARRY:                               return SLJIT_SET_CARRY;
    default:                                        return SLJIT_SET(ty);
    }
}

static sljit_s32 fcond_lookup(const char *s) {
    if (strcmp(s, "feq") == 0)       return SLJIT_F_EQUAL;
    if (strcmp(s, "fne") == 0)       return SLJIT_F_NOT_EQUAL;
    if (strcmp(s, "flt") == 0)       return SLJIT_F_LESS;
    if (strcmp(s, "fle") == 0)       return SLJIT_F_LESS_EQUAL;
    if (strcmp(s, "fgt") == 0)       return SLJIT_F_GREATER;
    if (strcmp(s, "fge") == 0)       return SLJIT_F_GREATER_EQUAL;
    if (strcmp(s, "ordered") == 0)   return SLJIT_ORDERED;
    if (strcmp(s, "unordered") == 0) return SLJIT_UNORDERED;
    return -1;
}

static sljit_s32 ret_movop(void) {
    switch (cur_ret) {
    case SLJIT_ARG_TYPE_32:  return SLJIT_MOV32;
    case SLJIT_ARG_TYPE_P:   return SLJIT_MOV_P;
    case SLJIT_ARG_TYPE_F64: return SLJIT_MOV_F64;
    case SLJIT_ARG_TYPE_F32: return SLJIT_MOV_F32;
    default:                 return SLJIT_MOV;
    }
}

/* ---- labels and jumps ------------------------------------------------- */
static const char *label_ref(const char *s) { return (s[0] == '@') ? s + 1 : s; }

static void define_label_ptr(struct sljit_label *l, const char *name) {
    if (nlabels >= MAX_IRLAB) throw_error("too many labels");
    label_ptrs[nlabels] = l;
    strncpy(label_names[nlabels], name, sizeof label_names[0] - 1);
    label_names[nlabels][sizeof label_names[0] - 1] = 0;
    nlabels++;
}

static void define_label(const char *name) {
    define_label_ptr(sljit_emit_label(jcomp), name);
}

static void record_jump(struct sljit_jump *j, const char *name) {
    if (!j) throw_error("sljit could not emit jump");
    if (njumps >= MAX_IRJMP) throw_error("too many jumps");
    jump_ptrs[njumps] = j;
    strncpy(jump_names[njumps], name, sizeof jump_names[0] - 1);
    jump_names[njumps][sizeof jump_names[0] - 1] = 0;
    njumps++;
}

static void resolve_jumps(void) {
    for (int i = 0; i < njumps; i++) {
        int found = 0;
        for (int j = 0; j < nlabels; j++)
            if (strcmp(jump_names[i], label_names[j]) == 0) {
                sljit_set_label(jump_ptrs[i], label_ptrs[j]);
                found = 1;
                break;
            }
        if (!found) throw_error("undefined native label");
    }
}

static void ensure_enter(void) { if (!entered) throw_error("instruction before 'enter'"); }

static sljit_s32 op1_lookup(const char *n) {
    for (int i = 0; op1_table[i].name; i++)
        if (strcmp(op1_table[i].name, n) == 0) return op1_table[i].op;
    return 0;
}

static sljit_s32 op2_lookup(const char *n, int *is32) {
    char base[16];
    size_t len = strlen(n);
    *is32 = 0;
    if (len > 2 && len < sizeof base && n[len - 2] == '3' && n[len - 1] == '2') {
        memcpy(base, n, len - 2);
        base[len - 2] = 0;
        *is32 = 1;
    } else if (len < sizeof base) {
        memcpy(base, n, len + 1);
    } else {
        return 0;
    }
    for (int i = 0; op2_table[i].name; i++)
        if (strcmp(op2_table[i].name, base) == 0) return op2_table[i].op;
    return 0;
}

/* ---- per-architecture raw emission (escape hatch) --------------------- */
static void emit_raw(const void *p, int n) {
    sljit_emit_op_custom(jcomp, (void *)p, (sljit_u32)n);
}

/* a data value: #imm, &symbol, or a plain number */
static uint64_t data_value(const char *tok) {
    if (tok[0] == '&') {
        sljit_s32 para; sljit_sw w;
        parse_operand(tok, &para, &w);
        return (uint64_t)(uintptr_t)w;
    }
    if (tok[0] == '#') return strtoull(tok + 1, NULL, 0);
    return strtoull(tok, NULL, 0);
}

static void emit_data(const char *tok, int bytes) {
    uint64_t v = data_value(tok);
    unsigned char b[8];
    for (int i = 0; i < bytes; i++) b[i] = (unsigned char)(v >> (8 * i));
    emit_raw(b, bytes);
}

#if (defined SLJIT_CONFIG_X86_64 && SLJIT_CONFIG_X86_64)
/* physical x86-64 register index for an SLJIT register or a plain name */
static int asm_reg(const char *s) {
    if ((s[0] == 'R' && isdigit((unsigned char)s[1])) ||
        (s[0] == 'S' && isdigit((unsigned char)s[1])) || strcmp(s, "SP") == 0) {
        int idx = sljit_get_register_index(SLJIT_GP_REGISTER, parse_reg(s));
        if (idx < 0) throw_error("asm: virtual register");
        return idx;
    }
    static const struct { const char *n; int i; } t[] = {
        {"RAX",0},{"RCX",1},{"RDX",2},{"RBX",3},{"RSP",4},{"RBP",5},{"RSI",6},{"RDI",7},
        {"R8",8},{"R9",9},{"R10",10},{"R11",11},{"R12",12},{"R13",13},{"R14",14},{"R15",15},
        {NULL,0}
    };
    for (int i = 0; t[i].n; i++) if (strcmp(t[i].n, s) == 0) return t[i].i;
    throw_error("asm: bad register");
    return 0;
}

static void asm_mov(int d, int s) {              /* mov d, s (r64) */
    unsigned char b[3];
    b[0] = (unsigned char)(0x48 | ((s >= 8) ? 4 : 0) | ((d >= 8) ? 1 : 0));
    b[1] = 0x89;
    b[2] = (unsigned char)(0xC0 | ((s & 7) << 3) | (d & 7));
    emit_raw(b, 3);
}

static void asm_mov_imm(int d, uint64_t v) {     /* mov d, imm64 */
    unsigned char b[10];
    b[0] = (unsigned char)(0x48 | ((d >= 8) ? 1 : 0));
    b[1] = (unsigned char)(0xB8 | (d & 7));
    for (int i = 0; i < 8; i++) b[2 + i] = (unsigned char)(v >> (8 * i));
    emit_raw(b, 10);
}

static void asm_call(int r) {                    /* call r64 */
    unsigned char b[3]; int n = 0;
    if (r >= 8) b[n++] = 0x41;
    b[n++] = 0xFF;
    b[n++] = (unsigned char)(0xD0 | (r & 7));
    emit_raw(b, n);
}

static void asm_pushpop(int r, int is_push) {    /* push/pop r64 */
    unsigned char b[2]; int n = 0;
    if (r >= 8) b[n++] = 0x41;
    b[n++] = (unsigned char)((is_push ? 0x50 : 0x58) + (r & 7));
    emit_raw(b, n);
}

static void asm_mov_al(int v) {                  /* mov al, imm8 */
    unsigned char b[2] = { 0xB0, (unsigned char)(v & 0xFF) };
    emit_raw(b, 2);
}
#endif /* SLJIT_CONFIG_X86_64 */

/* ---- instruction dispatch --------------------------------------------- */
static void ir_instruction(const char *m) {
    if (strcmp(m, "enter") == 0) {
        if (entered) throw_error("duplicate enter");
        int t = type_from_name(ir_need());
        if (t < 0) throw_error("bad return type");
        cur_ret = (sljit_s32)t;
        int sc = atoi(ir_need());
        int sv = atoi(ir_need());
        int lc = atoi(ir_need());
        sc |= SLJIT_ENTER_FLOAT(enter_fsc) | SLJIT_ENTER_VECTOR(enter_vsc);
        sv |= SLJIT_ENTER_FLOAT(enter_fsv) | SLJIT_ENTER_VECTOR(enter_vsv);
        sljit_s32 at = cur_ret;
        int idx = 1;
        while (irtok_pos < irtok_count) {
            int a = type_from_name(irtok[irtok_pos]);
            if (a < 0 || a == SLJIT_ARG_TYPE_RET_VOID || idx > 4) break;
            at |= (sljit_s32)(a << (idx * SLJIT_ARG_SHIFT));
            idx++;
            irtok_pos++;
        }
        cur_argtypes = at;
        sljit_emit_enter(jcomp, 0, at, sc, sv, lc);
        entered = 1;
        return;
    }
    if (strcmp(m, "fscratches") == 0) { enter_fsc = atoi(ir_need()); return; }
    if (strcmp(m, "fsaveds") == 0)    { enter_fsv = atoi(ir_need()); return; }
    if (strcmp(m, "vscratches") == 0) { enter_vsc = atoi(ir_need()); return; }
    if (strcmp(m, "vsaveds") == 0)    { enter_vsv = atoi(ir_need()); return; }
    if (strcmp(m, "locals") == 0) {
        if (entered) throw_error("'locals' must precede the body");
        forth_local = atoi(ir_need());
        return;
    }
    if (strcmp(m, "sig") == 0) {
        int t = type_from_name(ir_need());
        if (t < 0) throw_error("bad signature type");
        cur_ret = (sljit_s32)t;
        sljit_s32 at = cur_ret;
        int idx = 1;
        while (irtok_pos < irtok_count) {
            int a = type_from_name(irtok[irtok_pos]);
            if (a < 0 || a == SLJIT_ARG_TYPE_RET_VOID || idx > 4) break;
            at |= (sljit_s32)(a << (idx * SLJIT_ARG_SHIFT));
            idx++;
            irtok_pos++;
        }
        cur_argtypes = at;
        return;
    }
    if (strcmp(m, "dlopen") == 0) {
        const char *lib = ir_need();
        if (!dyn_open(lib, RTLD_NOW | RTLD_GLOBAL))
            throw_error(dyn_err[0] ? dyn_err : "dlopen failed");
        return;
    }
    if (strcmp(m, "dlclose") == 0) {
        const char *lib = ir_need();
        for (int i = 0; i < nlibs; i++)
            if (strcmp(dynlibs[i].name, lib) == 0) { dyn_close(dynlibs[i].handle); return; }
        throw_error("library not open");
    }
    if (strcmp(m, "label:") == 0 || strcmp(m, "setlabel") == 0) {
        define_label(ir_need());
        return;
    }
    if (strcmp(m, "op0") == 0) {
        ensure_enter();
        sljit_emit_op0(jcomp, parse_opnum(ir_need()));
        return;
    }
    if (strcmp(m, "op1") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 d, s; sljit_sw dw, sw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &s, &sw);
        sljit_emit_op1(jcomp, op, d, dw, s, sw);
        return;
    }
    if (strcmp(m, "op2") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        sljit_emit_op2(jcomp, op, d, dw, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "cmp") == 0) {
        ensure_enter();
        const char *first = ir_need();
        sljit_s32 ty = cond_lookup(first);
        if (ty < 0) ty = parse_opnum(first);
        sljit_s32 a, b; sljit_sw aw, bw;
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        const char *lb = ir_need();
        record_jump(sljit_emit_cmp(jcomp, ty, a, aw, b, bw), label_ref(lb));
        return;
    }
    if (strcmp(m, "jump") == 0) {
        ensure_enter();
        sljit_s32 ty = parse_opnum(ir_need());
        const char *target = ir_need();
        struct sljit_jump *j = sljit_emit_jump(jcomp, ty);
        if (target[0] == '#') sljit_set_target(j, (sljit_uw)strtoull(target + 1, NULL, 0));
        else record_jump(j, label_ref(target));
        return;
    }
    if (strcmp(m, "rewjump") == 0) {
        ensure_enter();
        const char *name = ir_need();
        sljit_s32 ty = parse_opnum(ir_need());
        const char *target = ir_need();
        struct sljit_jump *j = sljit_emit_jump(jcomp, ty | SLJIT_REWRITABLE_JUMP);
        if (target[0] == '#') sljit_set_target(j, (sljit_uw)strtoull(target + 1, NULL, 0));
        else record_jump(j, label_ref(target));
        if (n_pend_jumps >= MAX_PATCH) throw_error("too many rewritable jumps");
        pend_jumps[n_pend_jumps] = j;
        strncpy(pend_jump_names[n_pend_jumps], name, 31);
        pend_jump_names[n_pend_jumps][31] = 0;
        n_pend_jumps++;
        return;
    }
    if (strcmp(m, "const") == 0 || strcmp(m, "rwconst") == 0) {
        ensure_enter();
        const char *name = NULL;
        if (m[0] == 'r') name = ir_need();      /* rwconst <name> <op> <dst> <value> */
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 d; sljit_sw dw;
        parse_operand(ir_need(), &d, &dw);
        sljit_sw val = (sljit_sw)strtol(ir_need(), NULL, 0);
        struct sljit_const *c = sljit_emit_const(jcomp, op, d, dw, val);
        if (name) {
            if (n_pend_consts >= MAX_PATCH) throw_error("too many rewritable consts");
            pend_consts[n_pend_consts] = c;
            pend_const_ops[n_pend_consts] = op;
            strncpy(pend_const_names[n_pend_consts], name, 31);
            pend_const_names[n_pend_consts][31] = 0;
            n_pend_consts++;
        }
        return;
    }
    if (strcmp(m, "jmp") == 0) {
        ensure_enter();
        const char *lb = ir_need();
        record_jump(sljit_emit_jump(jcomp, SLJIT_JUMP), label_ref(lb));
        return;
    }
    if (strcmp(m, "call") == 0 || strcmp(m, "icall") == 0) {
        ensure_enter();
        sljit_s32 p; sljit_sw w;
        parse_operand(ir_need(), &p, &w);
        sljit_emit_icall(jcomp, SLJIT_CALL, cur_argtypes, p, w);
        return;
    }
    if (strcmp(m, "icall.reg") == 0) {
        ensure_enter();
        sljit_s32 p; sljit_sw w;
        parse_operand(ir_need(), &p, &w);
        sljit_emit_icall(jcomp, SLJIT_CALL_REG_ARG, cur_argtypes, p, w);
        return;
    }
    if (strcmp(m, "callw") == 0) {
        ensure_enter();
        const char *name = ir_need();
        Word *w = find(name);
        if (!w) throw_error("callw: unknown word");
        if (!w->native) throw_error("callw: not a native word");
        sljit_emit_op1(jcomp, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S1, 0);
        sljit_emit_icall(jcomp, SLJIT_CALL, SLJIT_ARGS1(P, P), SLJIT_IMM,
                         (sljit_sw)(intptr_t)w->native);
        sljit_emit_op1(jcomp, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_R0, 0);
        return;
    }
    if (strcmp(m, "ret") == 0) {
        ensure_enter();
        if (irtok_pos < irtok_count && looks_like_operand(irtok[irtok_pos])) {
            sljit_s32 s; sljit_sw sw;
            parse_operand(ir_need(), &s, &sw);
            sljit_emit_return(jcomp, ret_movop(), s, sw);
        } else {
            sljit_emit_return_void(jcomp);
        }
        returned = 1;
        return;
    }
    if (strcmp(m, "nop") == 0)  { ensure_enter(); sljit_emit_op0(jcomp, SLJIT_NOP); return; }
    if (strcmp(m, "int3") == 0) { ensure_enter(); sljit_emit_op0(jcomp, SLJIT_BREAKPOINT); return; }

    sljit_s32 op;
    if ((op = op1_lookup(m)) != 0) {
        ensure_enter();
        sljit_s32 d, s; sljit_sw dw, sw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &s, &sw);
        sljit_emit_op1(jcomp, op, d, dw, s, sw);
        return;
    }
    int is32 = 0;
    if ((op = op2_lookup(m, &is32)) != 0) {
        ensure_enter();
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        sljit_emit_op2(jcomp, op | (is32 ? SLJIT_32 : 0), d, dw, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "neg") == 0) {
        ensure_enter();
        sljit_s32 d, s; sljit_sw dw, sw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &s, &sw);
        sljit_emit_op2(jcomp, SLJIT_SUB, d, dw, SLJIT_IMM, 0, s, sw);
        return;
    }
    if (strcmp(m, "not") == 0) {
        ensure_enter();
        sljit_s32 d, s; sljit_sw dw, sw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &s, &sw);
        sljit_emit_op2(jcomp, SLJIT_XOR, d, dw, s, sw, SLJIT_IMM, -1);
        return;
    }
    if (strcmp(m, "op2u") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 a, b; sljit_sw aw, bw;
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        sljit_emit_op2u(jcomp, op, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "op2r") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        sljit_emit_op2r(jcomp, op, d, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "op2shift") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 d, a, b; sljit_sw dw, aw, bw, sh;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        sh = (sljit_sw)strtol(ir_need(), NULL, 0);
        sljit_emit_op2_shift(jcomp, op, d, dw, a, aw, b, bw, sh);
        return;
    }
    if (strcmp(m, "op2cmpz") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        const char *lb = ir_need();
        record_jump(sljit_emit_op2cmpz(jcomp, op, d, dw, a, aw, b, bw), label_ref(lb));
        return;
    }
    if (strcmp(m, "setflags") == 0) {
        ensure_enter();
        const char *first = ir_need();
        sljit_s32 ty = cond_lookup(first);
        if (ty < 0) ty = parse_opnum(first);
        sljit_s32 a, b; sljit_sw aw, bw;
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        sljit_emit_op2u(jcomp, SLJIT_SUB | cmp_set_flag(ty), a, aw, b, bw);
        return;
    }
    if (strcmp(m, "flags") == 0) {
        ensure_enter();
        const char *first = ir_need();
        sljit_s32 ty = cond_lookup(first);
        if (ty < 0) ty = parse_opnum(first);
        sljit_s32 d; sljit_sw dw;
        parse_operand(ir_need(), &d, &dw);
        sljit_emit_op_flags(jcomp, SLJIT_MOV, d, dw, ty);
        return;
    }
    if (strcmp(m, "select") == 0) {
        ensure_enter();
        const char *first = ir_need();
        sljit_s32 ty = cond_lookup(first);
        if (ty < 0) ty = parse_opnum(first);
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        sljit_emit_select(jcomp, ty, d, a, aw, b);
        return;
    }
    if (strcmp(m, "ijump") == 0) {
        ensure_enter();
        sljit_s32 ty = parse_opnum(ir_need());
        sljit_s32 p; sljit_sw w;
        parse_operand(ir_need(), &p, &w);
        sljit_emit_ijump(jcomp, ty, p, w);
        return;
    }
    if (strcmp(m, "opsrc") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 p; sljit_sw w;
        parse_operand(ir_need(), &p, &w);
        sljit_emit_op_src(jcomp, op, p, w);
        return;
    }
    if (strcmp(m, "opdst") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 p; sljit_sw w;
        parse_operand(ir_need(), &p, &w);
        sljit_emit_op_dst(jcomp, op, p, w);
        return;
    }
    if (strcmp(m, "ret_to") == 0) {
        ensure_enter();
        sljit_s32 p; sljit_sw w;
        parse_operand(ir_need(), &p, &w);
        sljit_emit_return_to(jcomp, p, w);
        returned = 1;
        return;
    }
    if (strcmp(m, "opaddr") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 d; sljit_sw dw;
        parse_operand(ir_need(), &d, &dw);
        const char *lb = ir_need();
        record_jump(sljit_emit_op_addr(jcomp, op, d, dw), label_ref(lb));
        return;
    }
    if (strcmp(m, "aligned_label") == 0) {
        sljit_s32 al = parse_opnum(ir_need());
        const char *name = ir_need();
        define_label_ptr(sljit_emit_aligned_label(jcomp, al, NULL), name);
        return;
    }
    if (strcmp(m, "custom") == 0) {
        ensure_enter();
        int n = atoi(ir_need());
        if (n < 0 || n > 256) throw_error("custom: bad size");
        unsigned char bytes[256];
        for (int i = 0; i < n; i++)
            bytes[i] = (unsigned char)strtol(ir_need(), NULL, 0);
        sljit_emit_op_custom(jcomp, bytes, (sljit_u32)n);
        return;
    }
    if (strcmp(m, "db") == 0 || strcmp(m, "dw") == 0 ||
        strcmp(m, "dd") == 0 || strcmp(m, "dq") == 0) {
        ensure_enter();
        int bytes = (m[1] == 'b') ? 1 : (m[1] == 'w') ? 2 : (m[1] == 'd') ? 4 : 8;
        emit_data(ir_need(), bytes);
        return;
    }
    if (strcmp(m, "require") == 0 || strcmp(m, "room") == 0) {
        /* entry guard: raise a catchable under/overflow if invalid */
        ensure_enter();
        int n = atoi(ir_need());
        void *fn = (strcmp(m, "require") == 0) ? (void *)(intptr_t)forth_need
                                               : (void *)(intptr_t)forth_room;
        sljit_emit_op1(jcomp, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S1, 0);                 /* r0 = sp */
        sljit_emit_op2(jcomp, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0,
                       SLJIT_IMM, (sljit_sw)(intptr_t)dstack);                        /* r0 -= dstack */
        sljit_emit_op2(jcomp, SLJIT_LSHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 3);    /* /8 = depth */
        sljit_emit_op1(jcomp, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, n);                  /* r1 = need */
        sljit_emit_icall(jcomp, SLJIT_CALL, SLJIT_ARGS2V(W, W), SLJIT_IMM, (sljit_sw)fn);
        return;
    }
#if (defined SLJIT_CONFIG_X86_64 && SLJIT_CONFIG_X86_64)
    if (strncmp(m, "asm.", 4) == 0) {
        ensure_enter();
        if (strcmp(m, "asm.mov") == 0)
            asm_mov(asm_reg(ir_need()), asm_reg(ir_need()));
        else if (strcmp(m, "asm.mov.imm") == 0)
            asm_mov_imm(asm_reg(ir_need()), data_value(ir_need()));
        else if (strcmp(m, "asm.call") == 0)
            asm_call(asm_reg(ir_need()));
        else if (strcmp(m, "asm.push") == 0)
            asm_pushpop(asm_reg(ir_need()), 1);
        else if (strcmp(m, "asm.pop") == 0)
            asm_pushpop(asm_reg(ir_need()), 0);
        else if (strcmp(m, "asm.mov.al") == 0)
            asm_mov_al((int)strtol(ir_need(), NULL, 0));
        else if (strcmp(m, "asm.ret") == 0)  { unsigned char b = 0xC3; emit_raw(&b, 1); }
        else if (strcmp(m, "asm.nop") == 0)  { unsigned char b = 0x90; emit_raw(&b, 1); }
        else if (strcmp(m, "asm.int3") == 0) { unsigned char b = 0xCC; emit_raw(&b, 1); }
        else throw_error("unknown asm mnemonic");
        return;
    }
#endif
    /* ---- floating point ---- */
    if (strcmp(m, "fop1") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 d, s; sljit_sw dw, sw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &s, &sw);
        sljit_emit_fop1(jcomp, op, d, dw, s, sw);
        return;
    }
    if (strcmp(m, "fop2") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(ir_need(), &d, &dw);
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        sljit_emit_fop2(jcomp, op, d, dw, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "fop2r") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 d, a, b; sljit_sw aw, bw;
        parse_operand_p(ir_need(), &d);
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        sljit_emit_fop2r(jcomp, op, d, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "fcmp") == 0) {
        ensure_enter();
        const char *first = ir_need();
        sljit_s32 ty = fcond_lookup(first);
        if (ty < 0) ty = parse_opnum(first);
        sljit_s32 a, b; sljit_sw aw, bw;
        parse_operand(ir_need(), &a, &aw);
        parse_operand(ir_need(), &b, &bw);
        const char *lb = ir_need();
        record_jump(sljit_emit_fcmp(jcomp, ty, a, aw, b, bw), label_ref(lb));
        return;
    }
    if (strcmp(m, "fselect") == 0) {
        ensure_enter();
        const char *first = ir_need();
        sljit_s32 ty = fcond_lookup(first);
        if (ty < 0) ty = parse_opnum(first);
        sljit_s32 d, a, b; sljit_sw aw;
        parse_operand_p(ir_need(), &d);
        parse_operand(ir_need(), &a, &aw);
        parse_operand_p(ir_need(), &b);
        sljit_emit_fselect(jcomp, ty, d, a, aw, b);
        return;
    }
    if (strcmp(m, "fcopy") == 0) {
        ensure_enter();
        sljit_s32 op = parse_opnum(ir_need());
        sljit_s32 f, r; sljit_sw rw;
        parse_operand_p(ir_need(), &f);
        parse_operand(ir_need(), &r, &rw);
        sljit_emit_fcopy(jcomp, op, f, r);
        return;
    }
    if (strcmp(m, "fset32") == 0) {
        ensure_enter();
        sljit_s32 f; parse_operand_p(ir_need(), &f);
        sljit_f32 v = (sljit_f32)strtod(ir_need(), NULL);
        sljit_emit_fset32(jcomp, f, v);
        return;
    }
    if (strcmp(m, "fset64") == 0) {
        ensure_enter();
        sljit_s32 f; parse_operand_p(ir_need(), &f);
        sljit_f64 v = (sljit_f64)strtod(ir_need(), NULL);
        sljit_emit_fset64(jcomp, f, v);
        return;
    }
    if (strcmp(m, "fmem") == 0 || strcmp(m, "fmem_update") == 0) {
        ensure_enter();
        sljit_s32 ty = parse_opnum(ir_need());
        sljit_s32 f; parse_operand_p(ir_need(), &f);
        sljit_s32 mem; sljit_sw memw;
        parse_operand(ir_need(), &mem, &memw);
        if (m[5] == '_') sljit_emit_fmem_update(jcomp, ty, f, mem, memw);
        else            sljit_emit_fmem(jcomp, ty, f, mem, memw);
        return;
    }
    throw_error("unknown native mnemonic");
}

/* ---- assemble current irbuf into machine code ------------------------- */
static void *ir_compile(int abi, int local) {
    jcomp = sljit_create_compiler(NULL);
    if (!jcomp) throw_error("cannot create jit compiler");
    forth_abi = abi;
    forth_local = local;
    entered = returned = 0;
    nlabels = njumps = 0;
    cur_ret = SLJIT_ARG_TYPE_RET_VOID;
    cur_argtypes = 0;
    enter_fsc = enter_fsv = enter_vsc = enter_vsv = 0;
    n_pend_jumps = n_pend_consts = 0;

    ir_tokenize();
    irtok_pos = 0;

    if (forth_abi) {
        if (irtok_pos < irtok_count && strcmp(irtok[irtok_pos], "locals") == 0) {
            irtok_pos++;
            forth_local = atoi(ir_need());
        }
        /* scratch-register argument: the stack pointer arrives in R0, which
           matches the C call ABI used by callw, so native words can call
           each other directly. */
        sljit_emit_enter(jcomp, 0, SLJIT_ARGS1(P, P_R),
                         4 | SLJIT_ENTER_FLOAT(6), 2, forth_local);
        sljit_emit_op1(jcomp, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_R0, 0);
        cur_ret = SLJIT_ARG_TYPE_P;
        cur_argtypes = SLJIT_ARGS1(P, P);
        entered = 1;
    }

    while (irtok_pos < irtok_count) ir_instruction(ir_need());

    resolve_jumps();
    if (forth_abi) sljit_emit_return(jcomp, SLJIT_MOV_P, SLJIT_S1, 0);
    if (!forth_abi && !returned) throw_error("native code needs 'ret'");
    if (sljit_get_compiler_error(jcomp) != SLJIT_SUCCESS)
        throw_error("sljit rejected the instruction stream");

    void *code = sljit_generate_code(jcomp, 0, NULL);
    if (!code) {
        sljit_free_compiler(jcomp);
        jcomp = NULL;
        n_pend_jumps = n_pend_consts = 0;
        throw_error("code generation failed");
    }
    patch_exec_off = sljit_get_executable_offset(jcomp);
    last_gen_size = sljit_get_generated_code_size(jcomp);
    for (int i = 0; i < n_pend_jumps && npatch_jumps < MAX_PATCH; i++) {
        patch_jumps[npatch_jumps].addr = sljit_get_jump_addr(pend_jumps[i]);
        memcpy(patch_jumps[npatch_jumps].name, pend_jump_names[i], sizeof patch_jumps[0].name);
        patch_jumps[npatch_jumps].name[31] = 0;
        npatch_jumps++;
    }
    for (int i = 0; i < n_pend_consts && npatch_consts < MAX_PATCH; i++) {
        patch_consts[npatch_consts].addr = sljit_get_const_addr(pend_consts[i]);
        patch_consts[npatch_consts].op = pend_const_ops[i];
        memcpy(patch_consts[npatch_consts].name, pend_const_names[i], sizeof patch_consts[0].name);
        patch_consts[npatch_consts].name[31] = 0;
        npatch_consts++;
    }
    n_pend_jumps = n_pend_consts = 0;
    sljit_free_compiler(jcomp);
    jcomp = NULL;
    if (njit < MAX_JIT) jit_codes[njit++] = code;
    return code;
}

/* ---- Forth-facing native words ---------------------------------------- */
static void p_code(void) {
    if (state != 0) throw_error("CODE not allowed here");
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after CODE");
    Word *w = newword(name, p_native);
    w->flags |= F_HIDDEN;
    compiling = w;
    irlen = 0;
    irbuf[0] = 0;
    state = 2;
}

static void p_endcode(void) {
    if (state != 2) throw_error(";CODE without CODE");
    char *body = strdup(irbuf);         /* keep the IR source for INLINE */
    void *code = ir_compile(1, 0);
    if (compiling) {
        compiling->native = code;
        compiling->irbody = body;
        compiling->flags &= ~F_HIDDEN;
    } else {
        free(body);
    }
    compiling = NULL;
    state = 0;
}

static int read_quote(char *out, int cap) {
    while (*inbuf_ptr == ' ' || *inbuf_ptr == '\t') inbuf_ptr++;
    int n = 0;
    while (*inbuf_ptr && *inbuf_ptr != '"') {
        if (n < cap - 1) out[n++] = *inbuf_ptr;
        inbuf_ptr++;
    }
    if (*inbuf_ptr != '"') return 0;
    inbuf_ptr++;
    out[n] = 0;
    return 1;
}

static void p_squote(void) {
    /* distinct buffers per S" so consecutive literals do not alias */
    static char ring[8][1024];
    static int ri = 0;
    char *slot = ring[ri];
    ri = (ri + 1) & 7;
    if (!read_quote(slot, 1024)) throw_error("unterminated S\"");
    int n = (int)strlen(slot);
    if (state == 1) {
        int cells = (n + (int)sizeof(cell)) / (int)sizeof(cell);
        if (memtop + cells > MEM_SIZE) throw_error("data space full");
        char *dst = (char *)mem + (size_t)memtop * sizeof(cell);
        memcpy(dst, slot, (size_t)n);
        cell a = (cell)(intptr_t)dst;
        memtop += cells;
        compile_xt(W_LIT); emit(a);
        compile_xt(W_LIT); emit(n);
    } else {
        push((cell)slot);
        push((cell)n);
    }
}

static void p_irquote(void) {
    char frag[2048];
    if (!read_quote(frag, (int)sizeof frag)) throw_error("unterminated IR\"");
    ir_put_text(frag, (int)strlen(frag));
}

static void p_assemble(void) {
    cell len = pop();
    char *a = (char *)pop();
    if (len < 0 || len >= IRBUF_SIZE) throw_error("bad IR length");
    memcpy(irbuf, a, (size_t)len);
    irbuf[len] = 0;
    irlen = (int)len;
    void *code = ir_compile(0, 0);
    push((cell)code);
}

static void p_tick(void) {
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after '");
    Word *w = find(name);
    if (!w) throw_error("? tick");
    push((cell)w);                 /* execution token (Word*) */
}

/* ( xt -- c-addr ) the C code pointer, for icall */
static void p_code_addr(void) { push((cell)((Word *)pop())->code); }

/* immediate: compile a literal XT */
static void p_bracket_tick(void) {
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after [']");
    Word *w = find(name);
    if (!w) throw_error("? [']");
    if (state != 1) throw_error("['] outside a definition");
    log_put(name);
    compile_xt(W_LIT);
    emit((cell)w);
}

/* POSTPONE name -- compile the compilation semantics of name */
static void p_postpone(void) {
    if (state != 1) throw_error("POSTPONE outside a definition");
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after POSTPONE");
    Word *w = find(name);
    if (!w) throw_error("? POSTPONE");
    log_put(name);
    if (w->flags & F_IMMEDIATE) {
        compile_xt(w);                 /* run it when the enclosing word runs */
    } else {
        Word *ct = find("COMPILE,");
        if (!ct) throw_error("POSTPONE needs COMPILE,");
        compile_xt(W_LIT);
        emit((cell)(intptr_t)w);       /* push the xt ... */
        compile_xt(ct);                /* ... then compile it */
    }
}

/* [COMPILE] name -- compile an immediate word instead of executing it */
static void p_bracket_compile(void) {
    if (state != 1) throw_error("[COMPILE] outside a definition");
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after [COMPILE]");
    Word *w = find(name);
    if (!w) throw_error("? [COMPILE]");
    log_put(name);
    compile_xt(w);
}

/* ---- exceptions ------------------------------------------------------- */
static void p_abort(void) { raise(ERR_ABORT, "ABORT"); }

static void p_execute(void) {
    Word *w = (Word *)pop();
    int saved_ip = ip;
    execute_word(w);
    ip = saved_ip;
}

static void p_catch(void) {
    Word *w = (Word *)pop();
    if (xsp >= MAX_XFRAME) raise(ERR_GENERIC, "too many CATCH frames");
    XFrame *f = &xframes[xsp++];
    f->sp = sp; f->rp = rp; f->ip = ip; f->state = state; f->compiling = compiling;
    if (setjmp(f->env) == 0) {
        execute_word(w);
        ip = f->ip;
        xsp--;
        push(0);
    } else {
        sp = f->sp; rp = f->rp; ip = f->ip;
        state = f->state; compiling = f->compiling;
        if (jcomp) { sljit_free_compiler(jcomp); jcomp = NULL; }
        xsp--;
        push(f->code);
    }
}

static void p_throw(void) {
    cell n = pop();
    if (n == 0) return;
    raise((int)n, "THROW");
}

static void p_error_msg(void) {
    push((cell)(intptr_t)err_msg);
    push((cell)strlen(err_msg));
}
static void p_error_code(void) { push(err_code); }

/* ( flag c-addr u -- ) raise with an inline message */
static void p_abortq(void) {
    cell len = pop();
    cell addr = pop();
    cell flag = pop();
    if (flag) {
        char buf[256];
        int n = (len < 0) ? 0 : (len < (cell)sizeof buf - 1 ? (int)len : (int)sizeof buf - 1);
        memcpy(buf, (void *)addr, (size_t)n);
        buf[n] = 0;
        raise(ERR_ABORTQ, buf);
    }
}

/* immediate: ABORT" message" — compile the string and (abort") */
/* store a string in data space and compile (lit addr)(lit len)( word ) */
static void compile_inline_string(const char *s, int n, Word *emitword) {
    int cells = (n + (int)sizeof(cell)) / (int)sizeof(cell);
    if (memtop + cells > MEM_SIZE) throw_error("data space full");
    char *dst = (char *)mem + (size_t)memtop * sizeof(cell);
    memcpy(dst, s, (size_t)n);
    cell a = (cell)(intptr_t)dst;
    memtop += cells;
    compile_xt(W_LIT); emit(a);
    compile_xt(W_LIT); emit(n);
    compile_xt(emitword);
}

static void p_abort_quote(void) {
    static char abq[1024];
    if (state != 1) throw_error("ABORT\" only in compile state");
    if (!read_quote(abq, (int)sizeof abq)) throw_error("unterminated ABORT\"");
    compile_inline_string(abq, (int)strlen(abq), W_ABORTQ);
}

static void p_dot_quote(void) {
    static char dq[1024];
    if (state != 1) throw_error(".\" only in compile state");
    if (!read_quote(dq, (int)sizeof dq)) throw_error("unterminated .\"");
    Word *t = find("TYPE");
    if (!t) throw_error(".\" requires TYPE");
    compile_inline_string(dq, (int)strlen(dq), t);
}

static void p_native_def(void) {
    cell cp = pop();
    char name[NAME_LEN];
    if (!next_token(name)) throw_error("name expected after NATIVE");
    Word *w = newword(name, p_native);
    w->native = (void *)cp;
}

/* address of libc putchar, for icall demos */
static void p_c_putchar(void) { push((cell)(intptr_t)putchar); }

/* ---- string and byte-memory words ------------------------------------- */
/* ---- dlopen / dlsym Forth surface ------------------------------------- */
static void p_dlopen(void) {
    cell len = pop();
    cell addr = pop();
    char name[DYN_NAME];
    if (!copy_cstr(addr, len, name, (int)sizeof name)) { dyn_seterr("bad library name"); push(0); return; }
    push((cell)dyn_open(name, RTLD_NOW | RTLD_GLOBAL));
}

static void p_dlsym(void) {
    cell len = pop();
    cell addr = pop();
    cell handle = pop();
    char name[DYN_NAME];
    if (!copy_cstr(addr, len, name, (int)sizeof name)) { dyn_seterr("bad symbol name"); push(0); return; }
    push((cell)dyn_sym((void *)handle, name));
}

static void p_dlclose(void) { dyn_close((void *)pop()); }

static void p_dlerror(void) {
    push((cell)(intptr_t)dyn_err);
    push((cell)strlen(dyn_err));
}

static void p_dllibs(void) {
    for (int i = 0; i < nlibs; i++)
        printf("%s %p\n", dynlibs[i].name, (void *)dynlibs[i].handle);
}

/* ---- runtime patching / introspection --------------------------------- */
static void p_set_jump_addr(void) {
    cell target = pop();
    cell len = pop();
    cell addr = pop();
    char name[32];
    if (!copy_cstr(addr, len, name, (int)sizeof name)) throw_error("bad name");
    for (int i = 0; i < npatch_jumps; i++)
        if (strcmp(patch_jumps[i].name, name) == 0) {
            sljit_set_jump_addr(patch_jumps[i].addr, (sljit_uw)target, patch_exec_off);
            return;
        }
    throw_error("set-jump-addr: unknown name");
}

static void p_set_const(void) {
    cell value = pop();
    cell len = pop();
    cell addr = pop();
    char name[32];
    if (!copy_cstr(addr, len, name, (int)sizeof name)) throw_error("bad name");
    for (int i = 0; i < npatch_consts; i++)
        if (strcmp(patch_consts[i].name, name) == 0) {
            sljit_set_const(patch_consts[i].addr, patch_consts[i].op, (sljit_sw)value, patch_exec_off);
            return;
        }
    throw_error("set-const: unknown name");
}

static void p_cpu_feature(void) { push((cell)sljit_has_cpu_feature((sljit_s32)pop())); }
static void p_jit_size(void) { push((cell)last_gen_size); }

static void p_native(void) {
    typedef cell *(*Nat)(cell *);
    Nat fn = (Nat)curr->native;
    cell *r = fn(dstack + sp);
    ptrdiff_t n = r - dstack;
    if (n < 0 || n > STACK_SIZE) throw_error("native stack error");
    sp = (int)n;
}

/* ===================================================================== */
/*  dictionary setup                                                     */
/* ===================================================================== */
static void def_imm(const char *name, Prim code) {
    define_prim(name, code)->flags |= F_IMMEDIATE;
}

static void init_dict(void) {
    W_EXIT    = define_prim("(exit)",    p_exit);
    W_LIT     = define_prim("(lit)",     p_lit);
    W_BRANCH  = define_prim("(branch)",  p_branch);
    W_0BRANCH = define_prim("(0branch)", p_0branch);
    W_DO      = define_prim("(do)",      rt_do);
    W_QDO     = define_prim("(?do)",     rt_qdo);
    W_LEAVE   = define_prim("(leave)",   rt_leave);
    W_LOOP    = define_prim("(loop)",    rt_loop);
    W_PLOOP   = define_prim("(+loop)",   rt_ploop);
    W_ABORTQ  = define_prim("(abort\")", p_abortq);
    W_DOES    = define_prim("(does>)",   p_does_setup);

    /* EXIT lives in the native prelude */

    /* Everything else lives in the native prelude: stack, arithmetic,
       logic, comparisons, ?DUP, @ !, EMIT CR, DEPTH , ALLOT, / MOD, I J. */

    /* memory / dictionary */
    define_prim("CREATE", p_create);
    def_imm("DOES>", p_does_quote);
    define_prim("JIT", p_jit);
    define_prim("JIT-ALL", p_jit_all);
    define_prim("WORDS", p_words);
    define_prim("SEE", p_see);
    /* strings / byte memory live in the native prelude */

    /* compiler */
    def_imm(":", p_colon);
    def_imm(";", p_semicolon);
    def_imm("RECURSE", p_recurse);
    define_prim("SYNONYM", p_synonym);
    define_prim("ALIAS", p_synonym);
    define_prim("MARKER", p_marker);
    define_prim("FORGET", p_forget);
    define_prim("DEFER", p_defer);
    define_prim("IS", p_is);
    define_prim("ACTION-OF", p_action_of);
    define_prim(":NONAME", p_noname);
    def_imm("POSTPONE", p_postpone);
    def_imm("[COMPILE]", p_bracket_compile);
    define_prim("IMMEDIATE", p_immediate);
    define_prim("COMPILE,", p_compile_comma);
    W_LOCAL       = define_prim("(local)",       p_local);
    W_LOCAL_STORE = define_prim("(local!)",      p_local_store);
    W_LOCALS_ENTER = define_prim("(locals-enter)", p_locals_enter);
    W_LOCALS_EXIT  = define_prim("(locals-exit)",  p_locals_exit);
    def_imm("{", p_locals_brace);
    def_imm("TO", p_to);
    def_imm("\\", p_backslash);
    def_imm("(", p_paren);
    define_prim("PARSE-NAME", p_parse_name);
    def_imm("[IF]", p_bracket_if);
    def_imm("[ELSE]", p_bracket_else);
    def_imm("[THEN]", p_bracket_then);

    /* native code / SLJIT */
    def_imm("CODE", p_code);
    def_imm(";CODE", p_endcode);
    def_imm("IR\"", p_irquote);
    def_imm("S\"", p_squote);
    define_prim("ASSEMBLE", p_assemble);
    define_prim("NATIVE", p_native_def);
    define_prim("C-PUTCHAR", p_c_putchar);
    define_prim("'", p_tick);
    define_prim("CODE-ADDR", p_code_addr);
    def_imm("[']", p_bracket_tick);
    define_prim("EXECUTE", p_execute);
    define_prim("CATCH", p_catch);
    define_prim("THROW", p_throw);
    define_prim("ABORT", p_abort);
    def_imm("ABORT\"", p_abort_quote);
    def_imm(".\"", p_dot_quote);
    define_prim("ERROR-MSG", p_error_msg);
    define_prim("ERROR-CODE", p_error_code);
    define_prim("DLOPEN", p_dlopen);
    define_prim("DLSYM", p_dlsym);
    define_prim("DLCLOSE", p_dlclose);
    define_prim("DLERROR", p_dlerror);
    define_prim("DLLIBS", p_dllibs);
    define_prim("SET-JUMP-ADDR", p_set_jump_addr);
    define_prim("SET-CONST", p_set_const);
    define_prim("CPU-FEATURE?", p_cpu_feature);
    define_prim("JIT-SIZE", p_jit_size);

    for (int i = 0; sljit_consts[i].name; i++) {
        Word *cw = define_prim(sljit_consts[i].name, p_push_const);
        cw->data = sljit_consts[i].val;
    }

    /* target architecture code (see FORTH_ARCH) */
    define_prim("ARCH", p_push_const)->data = FORTH_ARCH;
}

/* ===================================================================== */
/*  top level                                                            */
/* ===================================================================== */
static void run_line(void) {
    if (setjmp(abort_env) != 0) {
        if (booting) {
            fprintf(stderr, "fatal: prelude error: %s\n", err_msg[0] ? err_msg : "(unknown)");
            exit(1);
        }
        if (err_msg[0]) { fputs(err_msg, stderr); fputc('\n', stderr); }
        /* an error aborted this line: reset to a clean interpreter state */
        sp = 0;
        rp = 0;
        state = 0;
        compiling = NULL;
        ip = -1;
        xsp = 0;
        lfbase = 0;
        lfree = 0;
        cur_locals.n = 0;
        abort_active = 0;
        return;
    }
    char tok[NAME_LEN];
    abort_active = 1;
    inbuf_ptr = inbuf;
    while (next_token(tok)) {
        if (cond_skip) {
            if (strcmp(tok, "[IF]") == 0) cond_depth++;
            else if (strcmp(tok, "[THEN]") == 0) { if (cond_depth == 0) cond_skip = 0; else cond_depth--; }
            else if (strcmp(tok, "[ELSE]") == 0 && cond_depth == 0) cond_skip = 0;
            continue;
        }
        interpret_token(tok);
    }
    abort_active = 0;
}

static void run_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }
    while (fgets(inbuf, sizeof inbuf, f)) run_line();
    fclose(f);
}

/* Decompress and evaluate the embedded native prelude before the REPL. */
static void run_prelude(void) {
    const char *off = getenv("FORTH_NO_PRELUDE");
    if (off && *off) return;

    unsigned char *raw = malloc((size_t)prelude_raw_len + 1);
    if (!raw) { fputs("cannot allocate prelude buffer\n", stderr); exit(1); }
    unsigned int out = (unsigned int)prelude_raw_len;
    if (tinf_uncompress(raw, &out, prelude_z, (unsigned int)prelude_z_len) != TINF_OK) {
        free(raw);
        fputs("cannot decompress prelude\n", stderr);
        exit(1);
    }
    raw[out] = 0;

    booting = 1;
    char *p = (char *)raw;
    while (*p) {
        char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof inbuf) len = sizeof inbuf - 1;
        memcpy(inbuf, p, len);
        inbuf[len] = 0;
        run_line();
        if (!nl) break;
        p = nl + 1;
    }
    booting = 0;
    free(raw);
}

int main(int argc, char **argv) {
    init_dict();
    run_prelude();

    if (argc > 1) run_file(argv[1]);

    for (;;) {
        fputs("> ", stdout);
        fflush(stdout);
        if (!fgets(inbuf, sizeof inbuf, stdin)) break;
        run_line();
    }

    for (int i = 0; i < njit; i++) sljit_free_code(jit_codes[i], NULL);
    for (int i = 0; i < nwords; i++) free(dict[i].irbody);
    dyn_close_all();
    return 0;
}
