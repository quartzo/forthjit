/*
 * F->c - a tiny indirect-threaded Forth for Linux/gcc
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

/* ===================================================================== */
/*  interpreter context                                                  */
/*                                                                       */
/*  All mutable interpreter state lives in one struct so it is a single  */
/*  addressable object instead of scattered globals.                     */
/* ===================================================================== */

/* ---- dictionary ------------------------------------------------------ */
typedef struct Word Word;
struct forth;
typedef void (*Prim)(struct forth *F);

struct Word {
    Word   *link;
    char    name[NAME_LEN];
    uint8_t flags;
    Prim    code;
    int     body;             /* start index in fcode[] for colon words */
    int     body_end;         /* end index (exclusive) for colon words */
    cell    data;             /* payload for VARIABLE / CONSTANT */
    void   *native;           /* machine code pointer for native words */
    char   *irbody;           /* IR source of a CODE word, for INLINE */
    char   *src;              /* reconstructed source of a colon word, for SEE */
};

/* ---- locals { a b -- c } --------------------------------------------- */
#define MAX_LOCALS 16
#define LOCALS_MAX 4096

typedef struct {
    int  n;                    /* total declared locals */
    int  ni;                   /* number of input locals */
    char name[MAX_LOCALS][NAME_LEN];
} LocalsState;

/* ---- error handling: ABORT / CATCH / THROW --------------------------- */
#define ERR_GENERIC  (-256)
#define ERR_ABORT    (-1)
#define ERR_ABORTQ   (-2)
#define ERR_OVF      (-3)
#define ERR_UNF      (-4)
#define ERR_RSTK     (-5)
#define ERR_DIVZERO  (-10)
#define ERR_UNKNOWN  (-13)

#define MAX_XFRAME 64
typedef struct {
    jmp_buf env;
    int     fx_sp, fx_rp;
    cell    fx_ip, fx_state;
    Word   *fx_compiling;
    cell    fx_code;
} XFrame;

/* ---- dictionary markers ---------------------------------------------- */
#define MAX_MARKERS 64
typedef struct { int mk_nwords; Word *mk_latest; int mk_memtop; int mk_here; } MarkerState;

/* ---- dynamic library registry ---------------------------------------- */
#define MAX_DYNLIB 64
#define MAX_DYNSYM 256
#define DYN_NAME   128
typedef struct { char name[DYN_NAME]; void *handle; int flags; } DynLib;
typedef struct { void *handle; char name[DYN_NAME]; void *addr; } DynSym;

/* ---- runtime patch registry (rewritable jumps / constants) ----------- */
#define MAX_PATCH 256
typedef struct { char name[32]; sljit_uw addr; } PatchJump;
typedef struct { char name[32]; sljit_uw addr; sljit_s32 op; } PatchConst;

/* ---- IR assembler limits --------------------------------------------- */
#define IRBUF_SIZE (1 << 16)
#define MAX_IRTOK  8192
#define MAX_IRLAB  256
#define MAX_IRJMP  512

/* ---- generated code registry ----------------------------------------- */
#define MAX_JIT 1024

struct forth {
    /* stacks */
    cell dstack[STACK_SIZE];
    int  sp;
    cell rstack[STACK_SIZE];
    int  rp;

    /* threaded code */
    cell fcode[CODE_SIZE];
    cell here;
    cell ip;

    /* data memory */
    cell mem[MEM_SIZE];
    int  memtop;

    /* dictionary */
    Word  dict[DICT_WORDS];
    int   nwords;
    Word *latest;
    Word *compiling;
    Word *curr;
    cell  state;
    Word *last_created;
    Word *W_EXIT, *W_LIT, *W_BRANCH, *W_0BRANCH, *W_ABORTQ, *W_DOES;
    Word *W_DO, *W_LOOP, *W_PLOOP, *W_QDO, *W_LEAVE;
    Word *W_LOCAL, *W_LOCALS_ENTER, *W_LOCALS_EXIT, *W_LOCAL_STORE;
    MarkerState markers[MAX_MARKERS];
    int nmarkers;

    /* locals */
    cell        locals[LOCALS_MAX];
    int         lfbase;
    int         lfree;
    LocalsState cur_locals;

    /* input */
    char  inbuf[4096];
    char *inbuf_ptr;
    int   cond_skip, cond_depth;

    /* compile log (SEE) */
    char lbuf[4096];
    int  llen;

    /* errors */
    jmp_buf abort_env;
    int     abort_active;
    int     booting;
    cell    err_code;
    char    err_msg[256];
    XFrame  xframes[MAX_XFRAME];
    int     xsp;

    /* generated code registry */
    void *jit_codes[MAX_JIT];
    int   njit;

    /* SLJIT assembler */
    struct sljit_compiler *jcomp;
    Word *jit_exit;
    char  irbuf[IRBUF_SIZE];
    int   irlen;
    char  irtok[MAX_IRTOK][64];
    int   irtok_count;
    int   irtok_pos;
    char  label_names[MAX_IRLAB][32];
    struct sljit_label *label_ptrs[MAX_IRLAB];
    int   nlabels;
    char  jump_names[MAX_IRJMP][32];
    struct sljit_jump  *jump_ptrs[MAX_IRJMP];
    int   njumps;
    int   forth_abi, forth_local, entered, returned;
    int   enter_fsc, enter_fsv, enter_vsc, enter_vsv;
    PatchJump  patch_jumps[MAX_PATCH];
    int        npatch_jumps;
    PatchConst patch_consts[MAX_PATCH];
    int        npatch_consts;
    sljit_sw   patch_exec_off;
    unsigned long last_gen_size;
    struct sljit_jump  *pend_jumps[MAX_PATCH];
    char   pend_jump_names[MAX_PATCH][32];
    int    n_pend_jumps;
    struct sljit_const *pend_consts[MAX_PATCH];
    char   pend_const_names[MAX_PATCH][32];
    sljit_s32 pend_const_ops[MAX_PATCH];
    int    n_pend_consts;
    sljit_s32 cur_ret;
    sljit_s32 cur_argtypes;
    int       cur_nargs;
    int    inline_id, inline_depth;

    /* dynamic linking */
    DynLib dynlibs[MAX_DYNLIB];
    int    nlibs;
    DynSym dynsyms[MAX_DYNSYM];
    int    nsyms;
    char   dyn_err[256];
};


static void set_err(struct forth *F, int code, const char *msg) {
    F->err_code = code;
    if (msg) {
        strncpy(F->err_msg, msg, sizeof F->err_msg - 1);
        F->err_msg[sizeof F->err_msg - 1] = 0;
    } else {
        F->err_msg[0] = 0;
    }
}

static void raise(struct forth *F, int code, const char *msg) {
    set_err(F, code, msg);
    if (F->jcomp) { sljit_free_compiler(F->jcomp); F->jcomp = NULL; }
    if (F->xsp > 0) {
        XFrame *f = &F->xframes[F->xsp - 1];
        f->fx_code = code;
        longjmp(f->env, 1);
    }
    if (F->abort_active) longjmp(F->abort_env, 1);
    fputs(F->err_msg[0] ? F->err_msg : "error", stderr);
    fputc('\n', stderr);
    exit(1);
}

static void throw_error(struct forth *F, const char *msg) { raise(F, ERR_GENERIC, msg); }

/* entry point for native code: icall &forth_raise with (code -- ) */
static void forth_raise(cell code, struct forth *F) { raise(F, (int)code, "native throw"); }
static void forth_divzero(struct forth *F) { raise(F, ERR_DIVZERO, "div by zero"); }
/* native stack-underflow guard: icall &forth_need with (have need -- ) */
static void forth_need(cell have, cell need, struct forth *F) {
    if (have < need) raise(F, ERR_UNF, "stack underflow");
}
/* native stack-overflow guard: icall &forth_room with (have need -- ) */
static void forth_room(cell have, cell need, struct forth *F) {
    if (have + need > STACK_SIZE) raise(F, ERR_OVF, "stack overflow");
}
/* icall &forth_type with (c-addr u -- ), uses stdio like EMIT */
static void forth_type(const char *s, int len, struct forth *F) {
    (void)F;
    if (len > 0) fwrite(s, 1, (size_t)len, stdout);
}

/* ---- forward declarations ------------------------------------------- */
static void p_docol(struct forth *F);
static void p_push_addr(struct forth *F);
static void p_does(struct forth *F);
static void *jit_compile(struct forth *F, Word *w);
static void p_push_const(struct forth *F);
static void run_colon(struct forth *F, Word *w);
static void execute_word(struct forth *F, Word *w);
static void interpret_token(struct forth *F, const char *tok);
static int  next_token(struct forth *F, char *out);
static void push(struct forth *F, cell v);
static cell pop(struct forth *F);
static void p_endcode(struct forth *F);
static void p_irquote(struct forth *F);
static void ir_put(struct forth *F, const char *tok);
static void p_native(struct forth *F);

/* ---- stack helpers --------------------------------------------------- */
static void push(struct forth *F, cell v) {
    if (F->sp >= STACK_SIZE) { raise(F, ERR_OVF, "stack overflow"); }
    F->dstack[F->sp++] = v;
}

static cell pop(struct forth *F) {
    if (F->sp <= 0) { raise(F, ERR_UNF, "stack underflow"); }
    return F->dstack[--F->sp];
}


/* ---- code emission --------------------------------------------------- */
static void emit(struct forth *F, cell v) {
    if (F->here >= CODE_SIZE) { throw_error(F, "code space full"); }
    F->fcode[F->here++] = v;
}

static void compile_xt(struct forth *F, Word *w) { emit(F, (cell)w); }

/* ---- compile log: reconstruct source for SEE -------------------------- */


static void log_reset(struct forth *F) { F->llen = 0; F->lbuf[0] = 0; }

static void log_put(struct forth *F, const char *s) {
    int n = (int)strlen(s);
    if (F->llen && F->llen + n + 2 < (int)sizeof F->lbuf) F->lbuf[F->llen++] = ' ';
    if (F->llen + n + 1 < (int)sizeof F->lbuf) { memcpy(F->lbuf + F->llen, s, (size_t)n); F->llen += n; }
    F->lbuf[F->llen] = 0;
}

/* ---- dictionary construction ---------------------------------------- */
static Word *newword(struct forth *F, const char *name, Prim code) {
    if (F->nwords >= DICT_WORDS) { throw_error(F, "dictionary full"); }
    Word *w = &F->dict[F->nwords++];
    w->link = F->latest;
    F->latest = w;
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

static Word *define_prim(struct forth *F, const char *name, Prim code) {
    return newword(F, name, code);
}

static Word *find(struct forth *F, const char *name) {
    for (Word *w = F->latest; w; w = w->link)
        if (!(w->flags & F_HIDDEN) && strcmp(w->name, name) == 0)
            return w;
    return NULL;
}

/* ---- dictionary access helpers (callable from native CODE via &name) --- */
static Word *forth_find(const char *s, int len, struct forth *F) {
    if (len < 0 || len >= NAME_LEN) return NULL;
    char buf[NAME_LEN];
    memcpy(buf, s, (size_t)len);
    buf[len] = 0;
    return find(F, buf);
}
static Word *forth_latest(struct forth *F) { return F->latest; }
static Word *forth_link(Word *w, struct forth *F) { (void)F; return w ? w->link : NULL; }
static const char *forth_name(Word *w, struct forth *F) { (void)F; return w ? w->name : ""; }
static int   forth_flags(Word *w, struct forth *F) { (void)F; return w ? w->flags : 0; }
static void *forth_code(Word *w, struct forth *F) { (void)F; return w ? (void *)w->code : NULL; }
static void *forth_native(Word *w, struct forth *F) { (void)F; return w ? w->native : NULL; }
static cell  forth_data(Word *w, struct forth *F) { (void)F; return w ? w->data : 0; }
static int   forth_body_start(Word *w, struct forth *F) { (void)F; return w ? w->body : -1; }
static int   forth_body_end(Word *w, struct forth *F) { (void)F; return w ? w->body_end : -1; }
static cell  forth_immediate(Word *w, struct forth *F) { (void)F; return (w && (w->flags & F_IMMEDIATE)) ? -1 : 0; }
static cell  forth_hidden(Word *w, struct forth *F) { (void)F; return (w && (w->flags & F_HIDDEN)) ? -1 : 0; }
static cell  forth_colon_p(Word *w, struct forth *F) { (void)F; return (w && w->body >= 0) ? -1 : 0; }
static cell  forth_native_p(Word *w, struct forth *F) { (void)F; return (w && w->code == p_native) ? -1 : 0; }
static cell  forth_variable_p(Word *w, struct forth *F) { (void)F; return (w && w->code == p_push_addr) ? -1 : 0; }
static void *forth_body(Word *w, struct forth *F) { return (w && (w->code == p_push_addr || w->code == p_does)) ? (void *)(F->mem + w->data) : NULL; }

/* ===================================================================== */
/*  VM primitives                                                        */
/* ===================================================================== */

/* (exit) -- return from a colon word */
static void p_exit(struct forth *F) {
    if (F->rp <= 0) raise(F, ERR_RSTK, "return stack underflow");
    F->ip = F->rstack[--F->rp];
}

/* (lit) -- push inline literal */
static void p_lit(struct forth *F) { push(F, F->fcode[F->ip++]); }

/* (branch) off -- unconditional jump */
static void p_branch(struct forth *F) { int off = (int)F->fcode[F->ip++]; F->ip += off; }

/* (0branch) off -- conditional jump */
static void p_0branch(struct forth *F) {
    int off = (int)F->fcode[F->ip++];
    if (pop(F) == 0) F->ip += off;
}

/* (do) -- move limit,start from data stack onto return stack */
static void rt_do(struct forth *F) {
    if (F->rp + 2 > STACK_SIZE) { raise(F, ERR_RSTK, "return stack overflow"); }
    cell start = pop(F);
    cell limit = pop(F);
    F->rstack[F->rp++] = limit;
    F->rstack[F->rp++] = start;
}

/* (?do) -- like (do) but skip the loop when start == limit */
static void rt_qdo(struct forth *F) {
    if (F->rp + 2 > STACK_SIZE) { raise(F, ERR_RSTK, "return stack overflow"); }
    cell start = pop(F);
    cell limit = pop(F);
    int off = (int)F->fcode[F->ip++];
    if (start == limit) F->ip += off;
    else { F->rstack[F->rp++] = limit; F->rstack[F->rp++] = start; }
}

/* (leave) -- drop the loop frame and jump past LOOP */
static void rt_leave(struct forth *F) {
    if (F->rp < 2) raise(F, ERR_RSTK, "LEAVE outside a loop");
    F->rp -= 2;
    int off = (int)F->fcode[F->ip++];
    F->ip += off;
}

/* true when the loop index crosses the limit */
static int loop_finished(cell old, cell nw, cell lim, cell step) {
    if (step >= 0) return old < lim && nw >= lim;
    return old >= lim && nw < lim;
}

/* (loop) off -- increment index by 1, branch back if unfinished */
static void rt_loop(struct forth *F) {
    cell old = F->rstack[F->rp - 1];
    cell lim = F->rstack[F->rp - 2];
    cell nw  = old + 1;
    F->rstack[F->rp - 1] = nw;
    if (loop_finished(old, nw, lim, 1)) { F->rp -= 2; F->ip++; }
    else { int off = (int)F->fcode[F->ip++]; F->ip += off; }
}

/* (+loop) off -- increment index by n, branch back if unfinished */
static void rt_ploop(struct forth *F) {
    cell step = pop(F);
    cell old = F->rstack[F->rp - 1];
    cell lim = F->rstack[F->rp - 2];
    cell nw  = old + step;
    F->rstack[F->rp - 1] = nw;
    if (loop_finished(old, nw, lim, step)) { F->rp -= 2; F->ip++; }
    else { int off = (int)F->fcode[F->ip++]; F->ip += off; }
}


/* ===================================================================== */
/*  compiler / interpreter words                                         */
/* ===================================================================== */

static void p_colon(struct forth *F) {
    char name[NAME_LEN];
    if (!next_token(F, name)) { fputs("name expected after :\n", stderr); return; }
    Word *w = newword(F, name, p_docol);
    w->flags |= F_HIDDEN;
    w->body = F->here;
    F->compiling = w;
    F->cur_locals.n = 0;
    F->cur_locals.ni = 0;
    log_reset(F);
    F->state = 1;
}

static void p_semicolon(struct forth *F) {
    if (F->cur_locals.n > 0) compile_xt(F, F->W_LOCALS_EXIT);
    compile_xt(F, F->W_EXIT);
    if (F->compiling) {
        F->compiling->body_end = F->here;
        F->compiling->flags &= ~F_HIDDEN;
        free(F->compiling->src);
        F->compiling->src = strdup(F->lbuf);
    }
    F->compiling = NULL;
    F->state = 0;
}

static void p_recurse(struct forth *F) {
    if (!F->compiling) { fputs("recurse outside definition\n", stderr); return; }
    compile_xt(F, F->compiling);
}

/* ---- locals ---------------------------------------------------------- */
/* { a b -- c }: save the old frame on the return stack, allocate a fresh
   one, and move the inputs from the data stack into their slots. */
static void p_locals_enter(struct forth *F) {
    cell no = pop(F);
    cell ni = pop(F);
    if (ni < 0 || no < 0) throw_error(F, "bad locals frame");
    if (F->rp + 2 > STACK_SIZE) raise(F, ERR_RSTK, "return stack overflow");
    if (F->lfree + (int)(ni + no) > LOCALS_MAX) throw_error(F, "too many locals");
    F->rstack[F->rp++] = F->lfbase;
    F->rstack[F->rp++] = F->lfree;
    F->lfbase = F->lfree;
    F->lfree += (int)(ni + no);
    for (cell i = 0; i < ni; i++) F->locals[F->lfbase + (ni - 1 - i)] = pop(F);
    for (cell i = ni; i < ni + no; i++) F->locals[F->lfbase + i] = 0;
}

static void p_locals_exit(struct forth *F) {
    if (F->rp < 2) raise(F, ERR_RSTK, "locals frame underflow");
    F->lfree  = (int)F->rstack[--F->rp];
    F->lfbase = (int)F->rstack[--F->rp];
}

static void p_local(struct forth *F) {
    cell k = pop(F);
    if (k < 0 || k >= MAX_LOCALS) throw_error(F, "bad local index");
    push(F, F->locals[F->lfbase + (int)k]);
}

static void p_local_store(struct forth *F) {
    cell k = pop(F);
    cell v = pop(F);
    if (k < 0 || k >= MAX_LOCALS) throw_error(F, "bad local index");
    F->locals[F->lfbase + (int)k] = v;
}

static void p_locals_brace(struct forth *F) {
    if (F->state != 1 || !F->compiling) { fputs("{ outside a definition\n", stderr); return; }
    char t[NAME_LEN];
    int n = 0, ni = 0, seen_dash = 0, closed = 0;
    while (next_token(F, t)) {
        if (strcmp(t, "}") == 0) { closed = 1; break; }
        if (strcmp(t, "--") == 0) { seen_dash = 1; continue; }
        if (n >= MAX_LOCALS) throw_error(F, "too many locals");
        snprintf(F->cur_locals.name[n], NAME_LEN, "%s", t);
        n++;
        if (!seen_dash) ni++;
    }
    if (!closed) throw_error(F, "{ without }");
    F->cur_locals.n = n;
    F->cur_locals.ni = ni;
    {
        char decl[256];
        int p = snprintf(decl, sizeof decl, "{ ");
        for (int i = 0; i < n; i++) {
            if (i == ni) p += snprintf(decl + p, sizeof decl - p, "-- ");
            p += snprintf(decl + p, sizeof decl - p, "%s ", F->cur_locals.name[i]);
        }
        snprintf(decl + p, sizeof decl - p, "}");
        log_put(F, decl);
    }
    compile_xt(F, F->W_LIT); emit(F, ni);
    compile_xt(F, F->W_LIT); emit(F, n - ni);
    compile_xt(F, F->W_LOCALS_ENTER);
}

/* TO name -- store into a local declared by { ... } */
static void p_to(struct forth *F) {
    char name[NAME_LEN];
    if (!next_token(F, name)) { fputs("name expected after TO\n", stderr); return; }
    if (F->state == 1 && F->compiling) log_put(F, name);
    if (F->state == 1 && F->compiling && F->cur_locals.n > 0) {
        for (int i = 0; i < F->cur_locals.n; i++)
            if (strcmp(F->cur_locals.name[i], name) == 0) {
                compile_xt(F, F->W_LIT); emit(F, i);
                compile_xt(F, F->W_LOCAL_STORE);
                return;
            }
    }
    char msg[NAME_LEN + 8];
    snprintf(msg, sizeof msg, "TO ? %s", name);
    throw_error(F, msg);
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
static void p_push_addr(struct forth *F)  { push(F, (cell)(F->mem + F->curr->data)); }
static void p_push_const(struct forth *F) { push(F, F->curr->data); }

static void p_immediate(struct forth *F) { if (F->latest) F->latest->flags |= F_IMMEDIATE; }

/* ---- CREATE / DOES> --------------------------------------------------- */


static void p_create(struct forth *F) {
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after CREATE");
    Word *w = newword(F, name, p_push_addr);
    w->data = F->memtop;
    F->last_created = w;
}

static void p_does_setup(struct forth *F) {        /* (does>) */
    Word *w = F->last_created;
    if (!w) throw_error(F, "DOES> without CREATE");
    w->code = p_does;
    w->body = (int)F->ip;                  /* does-body starts right after (does>) */
    F->ip = F->rstack[--F->rp];                  /* return from the defining word */
}

static void p_does(struct forth *F) {              /* runtime of a CREATE..DOES> word */
    push(F, (cell)(F->mem + F->curr->data));
    cell saved = F->ip;
    run_colon(F, F->curr);
    F->ip = saved;
}

static void p_does_quote(struct forth *F) {        /* DOES> (immediate) */
    if (F->state != 1) throw_error(F, "DOES> outside a definition");
    compile_xt(F, F->W_DOES);
}

/* JIT name -- compile a colon definition to native code */
static void p_jit(struct forth *F) {
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after JIT");
    Word *w = find(F, name);
    if (!w) throw_error(F, "? JIT");
    void *jit = jit_compile(F, w);
    if (!jit) throw_error(F, "JIT: cannot compile word");
    w->native = jit;
    w->code = p_native;
}

/* JIT-ALL -- compile every colon word that can be compiled */
static void p_jit_all(struct forth *F) {
    int count = 0;
    for (int i = 0; i < F->nwords; i++) {
        Word *w = &F->dict[i];
        if (w->body >= 0 && w->code == p_docol) {
            void *jit = jit_compile(F, w);
            if (jit) { w->native = jit; w->code = p_native; count++; }
        }
    }
    push(F, count);
}

/* ---- deferred words and anonymous definitions ------------------------- */
static void p_defer_run(struct forth *F) {
    Word *target = (Word *)F->curr->data;
    if (!target) throw_error(F, "deferred word not set");
    cell saved = F->ip;
    execute_word(F, target);
    F->ip = saved;
}

static void p_defer(struct forth *F) {
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after DEFER");
    Word *w = newword(F, name, p_defer_run);
    w->data = 0;
}

static void p_is(struct forth *F) {
    if (F->state == 1) throw_error(F, "IS only in interpret state");
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after IS");
    Word *w = find(F, name);
    if (!w || w->code != p_defer_run) throw_error(F, "IS: not a deferred word");
    w->data = pop(F);
}

static void p_action_of(struct forth *F) {
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after ACTION-OF");
    Word *w = find(F, name);
    if (!w || w->code != p_defer_run) throw_error(F, "ACTION-OF: not a deferred word");
    push(F, w->data);
}

static void p_noname(struct forth *F) {
    char name[NAME_LEN];
    snprintf(name, sizeof name, "anon#%d", F->nwords);
    Word *w = newword(F, name, p_docol);
    w->flags |= F_HIDDEN;
    w->body = F->here;
    F->compiling = w;
    F->cur_locals.n = 0;
    F->cur_locals.ni = 0;
    log_reset(F);
    F->state = 1;
    push(F, (cell)(intptr_t)w);
}

/* ---- synonyms, markers, forget ---------------------------------------- */
static void p_synonym(struct forth *F) {          /* SYNONYM new old */
    char nname[NAME_LEN], oname[NAME_LEN];
    if (!next_token(F, nname)) throw_error(F, "name expected after SYNONYM");
    if (!next_token(F, oname)) throw_error(F, "name expected after SYNONYM");
    Word *ow = find(F, oname);
    if (!ow) throw_error(F, "? SYNONYM");
    Word *w = newword(F, nname, ow->code);
    w->flags = ow->flags & (uint8_t)~F_HIDDEN;
    w->body = ow->body;
    w->body_end = ow->body_end;
    w->data = ow->data;
    w->native = ow->native;
    w->irbody = ow->irbody ? strdup(ow->irbody) : NULL;
}



static void p_marker_run(struct forth *F) {
    int idx = (int)F->curr->data;
    if (idx < 0 || idx >= F->nmarkers) throw_error(F, "bad marker");
    MarkerState *m = &F->markers[idx];
    F->nwords = m->mk_nwords;
    F->latest = m->mk_latest;
    F->memtop = m->mk_memtop;
    F->here = m->mk_here;
    F->nmarkers = idx;
    F->compiling = NULL;
    F->state = 0;
}

static void p_marker(struct forth *F) {
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after MARKER");
    if (F->nmarkers >= MAX_MARKERS) throw_error(F, "too many markers");
    MarkerState *m = &F->markers[F->nmarkers];
    m->mk_nwords = F->nwords;
    m->mk_latest = F->latest;
    m->mk_memtop = F->memtop;
    m->mk_here = F->here;
    Word *w = newword(F, name, p_marker_run);
    w->data = F->nmarkers;
    F->nmarkers++;
}

static void p_forget(struct forth *F) {           /* forget name and everything after */
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after FORGET");
    int i;
    for (i = 0; i < F->nwords; i++)
        if (strcmp(F->dict[i].name, name) == 0) break;
    if (i >= F->nwords) throw_error(F, "? FORGET");
    F->nwords = i;
    F->latest = (i > 0) ? &F->dict[i - 1] : NULL;
}

/* ---- compiler kit ----------------------------------------------------- */
static void p_compile_comma(struct forth *F) { Word *w = (Word *)pop(F); compile_xt(F, w); }

/* ---- WORDS / SEE (kept in C: iteration + formatting) ------------------ */
static void p_words(struct forth *F) {
    int col = 0;
    for (Word *w = F->latest; w; w = w->link) {
        if (w->flags & F_HIDDEN) continue;
        printf("%s ", w->name);
        if (++col % 8 == 0) putchar('\n');
    }
    putchar('\n');
}

static void p_see(struct forth *F) {
    Word *w = (Word *)pop(F);
    if (!w) { printf("(null)\n"); return; }
    if (w->code == p_push_addr)  { printf("VARIABLE %s\n", w->name); return; }
    if (w->code == p_does)       { printf("CREATE %s ... DOES>\n", w->name); return; }
    if (w->code == p_push_const) { printf("CONSTANT %s = %ld\n", w->name, (long)w->data); return; }
    if (w->body >= 0) {
        if (w->src) { printf(": %s %s\n", w->name, w->src); return; }
        printf(": %s", w->name);
        int i = w->body, end = w->body_end;
        while (i >= 0 && i < end) {
            Word *x = (Word *)F->fcode[i];
            if (x == F->W_LIT) { printf(" %ld", (long)F->fcode[i + 1]); i += 2; }
            else if (x == F->W_BRANCH || x == F->W_0BRANCH) {
                printf(" %s %d", x == F->W_BRANCH ? "BRANCH" : "0BRANCH", (int)F->fcode[i + 1]);
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
static void p_backslash(struct forth *F) { while (*F->inbuf_ptr) F->inbuf_ptr++; }

static void p_paren(struct forth *F) {
    char t[NAME_LEN];
    while (next_token(F, t)) if (strcmp(t, ")") == 0) return;
}

/* ---- PARSE-NAME and conditional compilation --------------------------- */
static void p_parse_name(struct forth *F) {
    static char pn[NAME_LEN];
    if (!next_token(F, pn)) { push(F, 0); push(F, 0); return; }
    push(F, (cell)(intptr_t)pn);
    push(F, (cell)strlen(pn));
}

/* conditional compilation: when cond_skip is set, run_line drops every token
   until the matching [ELSE]/[THEN]; this persists across input lines. */


static void p_bracket_if(struct forth *F) {
    if (pop(F) == 0) { F->cond_skip = 1; F->cond_depth = 0; }
}

static void p_bracket_else(struct forth *F) { F->cond_skip = 1; F->cond_depth = 0; }

static void p_bracket_then(struct forth *F) { (void)F; /* no-op */ }

/* ===================================================================== */
/*  tokenizer                                                            */
/* ===================================================================== */
static int next_token(struct forth *F, char *out) {
    while (*F->inbuf_ptr && isspace((unsigned char)*F->inbuf_ptr)) F->inbuf_ptr++;
    if (!*F->inbuf_ptr) return 0;
    int n = 0;
    while (*F->inbuf_ptr && !isspace((unsigned char)*F->inbuf_ptr)) {
        if (n < NAME_LEN - 1) out[n++] = *F->inbuf_ptr;
        F->inbuf_ptr++;
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
static void run_colon(struct forth *F, Word *w) {
    if (F->rp >= STACK_SIZE) { raise(F, ERR_RSTK, "return stack overflow"); }
    F->rstack[F->rp++] = -1;          /* sentinel: end of outer frame */
    F->ip = w->body;
    while (F->ip >= 0) {
        Word *x = (Word *)F->fcode[F->ip++];
        F->curr = x;
        x->code(F);
    }
}

static void p_docol(struct forth *F) {
    if (F->rp >= STACK_SIZE) { raise(F, ERR_RSTK, "return stack overflow"); }
    F->rstack[F->rp++] = F->ip;
    F->ip = F->curr->body;
}

static void execute_word(struct forth *F, Word *w) {
    if (w->code == p_docol) run_colon(F, w);
    else { F->curr = w; w->code(F); }
}

/* ===================================================================== */
/*  call-threaded JIT for colon definitions                              */
/* ===================================================================== */
typedef struct { struct sljit_jump *j; cell target; } JitJump;

/* the prelude EXIT word, whose body must return from the JIT, not the VM */


/* Translate a colon body (fcode[body..body_end)) to a native void(void)
   function that calls primitives directly with native branches. Bodies
   containing DO/LOOP are left to the VM. Returns NULL on failure. */
/* copy S1 (register stack pointer) into the global sp */
static void jit_sync_out(struct forth *F, struct sljit_compiler *c) {
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S1, 0);
    sljit_emit_op2(c, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)(intptr_t)F->dstack);
    sljit_emit_op2(c, SLJIT_LSHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 3);
    sljit_emit_op1(c, SLJIT_MOV32, SLJIT_MEM0(), (sljit_sw)&F->sp, SLJIT_R0, 0);
}

/* reload S1 from the global sp */
static void jit_sync_in(struct forth *F, struct sljit_compiler *c) {
    sljit_emit_op1(c, SLJIT_MOV_U32, SLJIT_R0, 0, SLJIT_MEM0(), (sljit_sw)&F->sp);
    sljit_emit_op2(c, SLJIT_SHL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 3);
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)(intptr_t)F->dstack);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_R0, 0);
}

/* restore the caller's ip and return S1 */
static void jit_emit_ret(struct forth *F, struct sljit_compiler *c) {
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_SP), 0);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM0(), (sljit_sw)&F->ip, SLJIT_R0, 0);
    sljit_emit_return(c, SLJIT_MOV_P, SLJIT_S1, 0);
}

/* Translate a colon body to a native (cell* sp) -> (cell* sp) function that
   keeps the stack pointer in a register. Prelude/native words are called
   through that same register ABI; C primitives (global stack) are synced.
   Bodies with DO/LOOP are left to the VM. Returns NULL on failure. */
static void *jit_compile(struct forth *F, Word *w) {
    if (!w || w->body < 0 || w->body_end <= w->body) return NULL;
    if (!F->jit_exit) F->jit_exit = find(F, "EXIT");

    cell start = w->body, end = w->body_end;
    cell n = end - start;

    unsigned char *istarget = calloc((size_t)n + 1, 1);
    if (!istarget) return NULL;
    for (cell pc = start; pc < end; ) {
        Word *x = (Word *)F->fcode[pc];
        if (x == F->W_LIT) { pc += 2; }
        else if (x == F->W_BRANCH || x == F->W_0BRANCH) {
            cell t = pc + 2 + F->fcode[pc + 1];
            if (t < start || t > end) { free(istarget); return NULL; }
            istarget[t - start] = 1;
            pc += 2;
        } else if (x == F->W_DO || x == F->W_LOOP || x == F->W_PLOOP) {
            free(istarget); return NULL;
        } else { pc += 1; }
    }

    struct sljit_compiler *c = sljit_create_compiler(NULL);
    if (!c) { free(istarget); return NULL; }
    sljit_emit_enter(c, 0, SLJIT_ARGS1(P, P), 2, 2, (sljit_s32)sizeof(cell));
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_S0, 0);       /* S1 = sp */
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM0(), (sljit_sw)&F->ip);
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
        Word *x = (Word *)F->fcode[pc];
        if (x == F->W_LIT) {
            sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_S1), 0, SLJIT_IMM, F->fcode[pc + 1]);
            sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, sizeof(cell));
            pc += 2;
        } else if (x == F->W_BRANCH) {
            cell t = pc + 2 + F->fcode[pc + 1];
            struct sljit_jump *j = sljit_emit_jump(c, SLJIT_JUMP);
            if (labels[t - start]) sljit_set_label(j, labels[t - start]);
            else { jr[njr].j = j; jr[njr].target = t; njr++; }
            pc += 2;
        } else if (x == F->W_0BRANCH) {
            cell t = pc + 2 + F->fcode[pc + 1];
            sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1), -(sljit_sw)sizeof(cell));
            sljit_emit_op2(c, SLJIT_SUB, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, sizeof(cell));
            struct sljit_jump *j = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
            if (labels[t - start]) sljit_set_label(j, labels[t - start]);
            else { jr[njr].j = j; jr[njr].target = t; njr++; }
            pc += 2;
        } else if (x == F->W_EXIT || (F->jit_exit && x == F->jit_exit)) {
            jit_emit_ret(F, c);
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
            sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_MEM0(), (sljit_sw)&F->curr,
                           SLJIT_IMM, (sljit_sw)(intptr_t)x);
            jit_sync_out(F, c);
            if (x->code == p_docol) {
                sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)(intptr_t)F);
                sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)(intptr_t)x);
                sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2V(P, P),
                                 SLJIT_IMM, (sljit_sw)(intptr_t)execute_word);
            } else {
                sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)(intptr_t)F);
                sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS1V(P),
                                 SLJIT_IMM, (sljit_sw)(intptr_t)x->code);
            }
            jit_sync_in(F, c);
            pc += 1;
        }
    }
    if (!labels[n]) labels[n] = sljit_emit_label(c);
    for (int i = 0; i < njr; i++)
        if (jr[i].target == end) sljit_set_label(jr[i].j, labels[n]);
    jit_emit_ret(F, c);

    if (sljit_get_compiler_error(c) != SLJIT_SUCCESS) {
        free(istarget); free(labels); free(jr);
        sljit_free_compiler(c);
        return NULL;
    }
    void *jit = sljit_generate_code(c, 0, NULL);
    sljit_free_compiler(c);
    free(istarget); free(labels); free(jr);
    if (!jit) return NULL;
    if (F->njit < MAX_JIT) F->jit_codes[F->njit++] = jit;
    return jit;
}

static void interpret_token(struct forth *F, const char *tok) {
    if (F->state == 2) {
        if (strcmp(tok, ";CODE") == 0) { p_endcode(F); return; }
        if (strcmp(tok, "IR\"") == 0) { p_irquote(F); return; }
        if (strcmp(tok, "\\") == 0) { while (*F->inbuf_ptr) F->inbuf_ptr++; return; }
        if (strcmp(tok, "(") == 0) {
            char t[NAME_LEN];
            while (next_token(F, t)) if (strcmp(t, ")") == 0) return;
            throw_error(F, "unterminated ( in CODE");
        }
        ir_put(F, tok);
        return;
    }
    if (F->state == 1 && F->compiling &&
        strcmp(tok, "\\") != 0 && strcmp(tok, "(") != 0 && strcmp(tok, "{") != 0)
        log_put(F, tok);
    if (F->state == 1 && F->compiling && F->cur_locals.n > 0) {
        for (int i = 0; i < F->cur_locals.n; i++)
            if (strcmp(F->cur_locals.name[i], tok) == 0) {
                compile_xt(F, F->W_LIT); emit(F, i);
                compile_xt(F, F->W_LOCAL);
                return;
            }
    }
    Word *w = find(F, tok);
    if (w) {
        if (F->state == 1 && !(w->flags & F_IMMEDIATE)) {
            if (F->cur_locals.n > 0 && w->code == p_native && strcmp(w->name, "EXIT") == 0)
                compile_xt(F, F->W_LOCALS_EXIT);
            compile_xt(F, w);
        } else execute_word(F, w);
        return;
    }
    cell v;
    if (parse_number(tok, &v)) {
        if (F->state == 1) { compile_xt(F, F->W_LIT); emit(F, v); }
        else push(F, v);
        return;
    }
    char msg[NAME_LEN + 8];
    snprintf(msg, sizeof msg, "? %s", tok);
    raise(F, ERR_UNKNOWN, msg);
}

/* ===================================================================== */
/*  dynamic library registry (dlopen / dlsym)                            */
/* ===================================================================== */



static void dyn_seterr(struct forth *F, const char *m) {
    if (!m) m = "unknown dl error";
    strncpy(F->dyn_err, m, sizeof F->dyn_err - 1);
    F->dyn_err[sizeof F->dyn_err - 1] = 0;
}

/* open a library, reusing an existing handle; errors are reported via dyn_err */
static void *dyn_open(struct forth *F, const char *name, int flags) {
    for (int i = 0; i < F->nlibs; i++)
        if (strcmp(F->dynlibs[i].name, name) == 0) return F->dynlibs[i].handle;
    if (F->nlibs >= MAX_DYNLIB) { dyn_seterr(F, "too many libraries"); return NULL; }
    dlerror();
    void *h = dlopen(name, flags);
    if (!h) { const char *e = dlerror(); dyn_seterr(F, e ? e : "dlopen failed"); return NULL; }
    strncpy(F->dynlibs[F->nlibs].name, name, DYN_NAME - 1);
    F->dynlibs[F->nlibs].name[DYN_NAME - 1] = 0;
    F->dynlibs[F->nlibs].handle = h;
    F->dynlibs[F->nlibs].flags = flags;
    F->nlibs++;
    F->dyn_err[0] = 0;
    return h;
}

/* resolve a symbol in a handle, with a cache; NULL on failure */
static void *dyn_sym(struct forth *F, void *handle, const char *name) {
    for (int i = 0; i < F->nsyms; i++)
        if (F->dynsyms[i].handle == handle && strcmp(F->dynsyms[i].name, name) == 0)
            return F->dynsyms[i].addr;
    if (F->nsyms >= MAX_DYNSYM) { dyn_seterr(F, "too many symbols"); return NULL; }
    dlerror();
    void *a = dlsym(handle, name);
    const char *e = dlerror();
    if (e) { dyn_seterr(F, e); return NULL; }
    F->dynsyms[F->nsyms].handle = handle;
    strncpy(F->dynsyms[F->nsyms].name, name, DYN_NAME - 1);
    F->dynsyms[F->nsyms].name[DYN_NAME - 1] = 0;
    F->dynsyms[F->nsyms].addr = a;
    F->nsyms++;
    F->dyn_err[0] = 0;
    return a;
}

/* lib empty -> global scope (RTLD_DEFAULT); otherwise open lib on demand */
static void *dyn_resolve(struct forth *F, const char *lib, const char *name) {
    void *handle = RTLD_DEFAULT;
    if (lib && *lib) {
        handle = dyn_open(F, lib, RTLD_NOW | RTLD_GLOBAL);
        if (!handle) return NULL;
    }
    return dyn_sym(F, handle, name);
}

static void dyn_forget_handle(struct forth *F, void *handle) {
    int j = 0;
    for (int i = 0; i < F->nsyms; i++)
        if (F->dynsyms[i].handle != handle) F->dynsyms[j++] = F->dynsyms[i];
    F->nsyms = j;
}

static void dyn_close(struct forth *F, void *handle) {
    for (int i = 0; i < F->nlibs; i++) {
        if (F->dynlibs[i].handle == handle) {
            dyn_forget_handle(F, handle);
            dlclose(handle);
            for (int j = i; j < F->nlibs - 1; j++) F->dynlibs[j] = F->dynlibs[j + 1];
            F->nlibs--;
            return;
        }
    }
    dyn_forget_handle(F, handle);
    dlclose(handle);
}

static void dyn_close_all(struct forth *F) {
    for (int i = F->nlibs - 1; i >= 0; i--) dlclose(F->dynlibs[i].handle);
    F->nlibs = 0;
    F->nsyms = 0;
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
static void ir_put_text(struct forth *F, const char *s, int len) {
    if (F->irlen + len + 2 >= IRBUF_SIZE) throw_error(F, "IR buffer full");
    if (F->irlen) F->irbuf[F->irlen++] = ' ';
    memcpy(F->irbuf + F->irlen, s, (size_t)len);
    F->irlen += len;
    F->irbuf[F->irlen] = 0;
}

static void ir_put(struct forth *F, const char *tok) { ir_put_text(F, tok, (int)strlen(tok)); }

static void tokenize_text(struct forth *F, const char *text, char (*out)[64], int *count, int max) {
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
        if (*count >= max) throw_error(F, "too many IR tokens");
    }
}

static void append_token(struct forth *F, const char *t) {
    if (F->irtok_count >= MAX_IRTOK) throw_error(F, "too many IR tokens");
    size_t L = strlen(t);
    if (L > 63) L = 63;
    memcpy(F->irtok[F->irtok_count], t, L);
    F->irtok[F->irtok_count][L] = 0;
    F->irtok_count++;
}

/* Expand a CODE word's stored IR body, renaming its labels so several
   expansions of the same word do not collide. */


static void expand_body(struct forth *F, Word *w, int id) {
    if (!w->irbody) throw_error(F, "inline: word has no native body");
    if (++F->inline_depth > 16) throw_error(F, "inline: too deep");
    static char btok[2048][64];
    int bn = 0;
    tokenize_text(F, w->irbody, btok, &bn, 2048);
    for (int i = 0; i < bn; i++) {
        if (strcmp(btok[i], "inline") == 0) {
            if (i + 1 >= bn) throw_error(F, "inline: missing name");
            Word *t = find(F, btok[++i]);
            if (!t) throw_error(F, "inline: unknown word");
            expand_body(F, t, ++F->inline_id);
        } else if (strcmp(btok[i], "label:") == 0 || strcmp(btok[i], "setlabel") == 0) {
            append_token(F, btok[i]);
            if (i + 1 >= bn) throw_error(F, "inline: label name missing");
            char nm[80];
            snprintf(nm, sizeof nm, "%s#%d", btok[++i], id);
            append_token(F, nm);
        } else if (strcmp(btok[i], "aligned_label") == 0) {
            append_token(F, btok[i]);
            if (i + 2 >= bn) throw_error(F, "inline: aligned_label has too few arguments");
            append_token(F, btok[++i]);
            char nm[80];
            snprintf(nm, sizeof nm, "%s#%d", btok[++i], id);
            append_token(F, nm);
        } else if (btok[i][0] == '@') {
            char nm[80];
            snprintf(nm, sizeof nm, "@%s#%d", btok[i] + 1, id);
            append_token(F, nm);
        } else {
            append_token(F, btok[i]);
        }
    }
    F->inline_depth--;
}

static void ir_tokenize(struct forth *F) {
    static char raw[MAX_IRTOK][64];
    int rn = 0;
    tokenize_text(F, F->irbuf, raw, &rn, MAX_IRTOK);
    F->irtok_count = 0;
    F->inline_id = 0;
    F->inline_depth = 0;
    for (int i = 0; i < rn; i++) {
        if (strcmp(raw[i], "inline") == 0) {
            if (i + 1 >= rn) throw_error(F, "inline: missing name");
            Word *w = find(F, raw[++i]);
            if (!w) throw_error(F, "inline: unknown word");
            expand_body(F, w, ++F->inline_id);
        } else {
            append_token(F, raw[i]);
        }
    }
}

static const char *ir_need(struct forth *F) {
    if (F->irtok_pos >= F->irtok_count) throw_error(F, "unexpected end of native code");
    return F->irtok[F->irtok_pos++];
}

/* ---- operands --------------------------------------------------------- */
static sljit_s32 parse_reg(struct forth *F, const char *s) {
    if (s[0] == 'R' && isdigit((unsigned char)s[1])) {
        int i = atoi(s + 1);
        if (i < 0 || i >= SLJIT_NUMBER_OF_REGISTERS) throw_error(F, "bad register");
        return (sljit_s32)SLJIT_R(i);
    }
    if (s[0] == 'S' && isdigit((unsigned char)s[1])) {
        int i = atoi(s + 1);
        if (i < 0 || i >= SLJIT_NUMBER_OF_SAVED_REGISTERS) throw_error(F, "bad saved register");
        return (sljit_s32)SLJIT_S(i);
    }
    if (s[0] == 'F' && s[1] == 'S' && isdigit((unsigned char)s[2])) {
        int i = atoi(s + 2);
        if (i < 0 || i >= SLJIT_NUMBER_OF_SAVED_FLOAT_REGISTERS) throw_error(F, "bad saved float register");
        return (sljit_s32)SLJIT_FS(i);
    }
    if (s[0] == 'F' && isdigit((unsigned char)s[1])) {
        int i = atoi(s + 1);
        if (i < 0 || i >= SLJIT_NUMBER_OF_FLOAT_REGISTERS) throw_error(F, "bad float register");
        return (sljit_s32)SLJIT_FR(i);
    }
    if (s[0] == 'V' && s[1] == 'S' && isdigit((unsigned char)s[2])) {
        int i = atoi(s + 2);
        if (i < 0 || i >= SLJIT_NUMBER_OF_SAVED_VECTOR_REGISTERS) throw_error(F, "bad saved vector register");
        return (sljit_s32)SLJIT_VS(i);
    }
    if (s[0] == 'V' && isdigit((unsigned char)s[1])) {
        int i = atoi(s + 1);
        if (i < 0 || i >= SLJIT_NUMBER_OF_VECTOR_REGISTERS) throw_error(F, "bad vector register");
        return (sljit_s32)SLJIT_VR(i);
    }
    if (strcmp(s, "SP") == 0) return SLJIT_SP;
    throw_error(F, "bad register");
    return 0;
}

static void parse_mem(struct forth *F, const char *tok, sljit_s32 *para, sljit_sw *w) {
    char buf[64];
    size_t len = strlen(tok) - 2;
    if (len >= sizeof buf) throw_error(F, "memory operand too long");
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
        if (!plus) throw_error(F, "bad indexed memory operand");
        *plus = 0;
        sljit_s32 r1 = parse_reg(F, buf);
        sljit_s32 r2 = parse_reg(F, plus + 1);
        *para = SLJIT_MEM2(r1, r2);
        return;
    }
    char *plus = strchr(buf, '+');
    if (plus) {
        *plus = 0;
        *para = SLJIT_MEM1(parse_reg(F, buf));
        *w = (sljit_sw)strtol(plus + 1, NULL, 0);
        return;
    }
    char *minus = strchr(buf, '-');
    if (minus) {
        *minus = 0;
        *para = SLJIT_MEM1(parse_reg(F, buf));
        *w = -(sljit_sw)strtol(minus + 1, NULL, 0);
        return;
    }
    *para = SLJIT_MEM1(parse_reg(F, buf));
    *w = 0;
}

static void parse_operand(struct forth *F, const char *tok, sljit_s32 *para, sljit_sw *w) {
    size_t len = strlen(tok);
    if (tok[0] == '#') { *para = SLJIT_IMM; *w = (sljit_sw)strtol(tok + 1, NULL, 0); return; }
    if (tok[0] == '&') {
        const char *spec = tok + 1;
        char lib[DYN_NAME];
        char sym[DYN_NAME];
        const char *sep = strstr(spec, "::");
        if (sep) {
            size_t ll = (size_t)(sep - spec);
            if (ll >= DYN_NAME) throw_error(F, "library name too long");
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
            if      (strcmp(sym, "dstack") == 0) pv = F->dstack;
            else if (strcmp(sym, "rstack") == 0) pv = F->rstack;
            else if (strcmp(sym, "mem") == 0)    pv = F->mem;
            else if (strcmp(sym, "sp") == 0)     pv = &F->sp;
            else if (strcmp(sym, "rp") == 0)     pv = &F->rp;
            else if (strcmp(sym, "memtop") == 0) pv = &F->memtop;
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
            else if (strcmp(sym, "code") == 0) pv = (void *)F->fcode;
            else if (strcmp(sym, "here") == 0) pv = &F->here;
            else if (strcmp(sym, "ip") == 0) pv = &F->ip;
            else if (strcmp(sym, "state") == 0) pv = &F->state;
            else if (strcmp(sym, "compiling") == 0) pv = &F->compiling;
            else if (strcmp(sym, "latest") == 0) pv = &F->latest;
            else if (strcmp(sym, "nwords") == 0) pv = &F->nwords;
            else if (strcmp(sym, "dict") == 0) pv = (void *)F->dict;
            if (pv) { *para = SLJIT_IMM; *w = (sljit_sw)(intptr_t)pv; return; }
        }
        void *a = NULL;
        Word *fw = find(F, sym);            /* prefer a native Forth word */
        if (fw && fw->native) a = fw->native;
        else a = dyn_resolve(F, lib, sym);
        if (!a) throw_error(F, F->dyn_err[0] ? F->dyn_err : "symbol not found");
        *para = SLJIT_IMM;
        *w = (sljit_sw)(intptr_t)a;
        return;
    }
    if (len >= 2 && tok[0] == '[' && tok[len - 1] == ']') { parse_mem(F, tok, para, w); return; }
    *para = parse_reg(F, tok);
    *w = 0;
}

/* parse an operand when only the base/register code matters */
static void parse_operand_p(struct forth *F, const char *tok, sljit_s32 *para) {
    sljit_sw z;
    parse_operand(F, tok, para, &z);
}

static int looks_like_operand(const char *s) {
    if (s[0] == '#' || s[0] == '[' || s[0] == '&') return 1;
    if ((s[0] == 'R' || s[0] == 'S' || s[0] == 'F' || s[0] == 'V') &&
        (isdigit((unsigned char)s[1]) ||
         ((s[1] == 'S') && isdigit((unsigned char)s[2])))) return 1;
    return strcmp(s, "SP") == 0;
}

static sljit_s32 parse_opnum(struct forth *F, const char *s) {
    long v;
    if (sljit_const_lookup(s, &v)) return (sljit_s32)v;
    char *end;
    long n = strtol(s, &end, 0);
    if (*end) throw_error(F, "bad opcode");
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

static sljit_s32 ret_movop(struct forth *F) {
    switch (F->cur_ret) {
    case SLJIT_ARG_TYPE_32:  return SLJIT_MOV32;
    case SLJIT_ARG_TYPE_P:   return SLJIT_MOV_P;
    case SLJIT_ARG_TYPE_F64: return SLJIT_MOV_F64;
    case SLJIT_ARG_TYPE_F32: return SLJIT_MOV_F32;
    default:                 return SLJIT_MOV;
    }
}

/* ---- labels and jumps ------------------------------------------------- */
static const char *label_ref(const char *s) { return (s[0] == '@') ? s + 1 : s; }

static void define_label_ptr(struct forth *F, struct sljit_label *l, const char *name) {
    if (F->nlabels >= MAX_IRLAB) throw_error(F, "too many labels");
    F->label_ptrs[F->nlabels] = l;
    strncpy(F->label_names[F->nlabels], name, sizeof F->label_names[0] - 1);
    F->label_names[F->nlabels][sizeof F->label_names[0] - 1] = 0;
    F->nlabels++;
}

static void define_label(struct forth *F, const char *name) {
    define_label_ptr(F, sljit_emit_label(F->jcomp), name);
}

static void record_jump(struct forth *F, struct sljit_jump *j, const char *name) {
    if (!j) throw_error(F, "sljit could not emit jump");
    if (F->njumps >= MAX_IRJMP) throw_error(F, "too many jumps");
    F->jump_ptrs[F->njumps] = j;
    strncpy(F->jump_names[F->njumps], name, sizeof F->jump_names[0] - 1);
    F->jump_names[F->njumps][sizeof F->jump_names[0] - 1] = 0;
    F->njumps++;
}

static void resolve_jumps(struct forth *F) {
    for (int i = 0; i < F->njumps; i++) {
        int found = 0;
        for (int j = 0; j < F->nlabels; j++)
            if (strcmp(F->jump_names[i], F->label_names[j]) == 0) {
                sljit_set_label(F->jump_ptrs[i], F->label_ptrs[j]);
                found = 1;
                break;
            }
        if (!found) throw_error(F, "undefined native label");
    }
}

static void ensure_enter(struct forth *F) { if (!F->entered) throw_error(F, "instruction before 'enter'"); }

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
static void emit_raw(struct forth *F, const void *p, int n) {
    sljit_emit_op_custom(F->jcomp, (void *)p, (sljit_u32)n);
}

/* a data value: #imm, &symbol, or a plain number */
static uint64_t data_value(struct forth *F, const char *tok) {
    if (tok[0] == '&') {
        sljit_s32 para; sljit_sw w;
        parse_operand(F, tok, &para, &w);
        return (uint64_t)(uintptr_t)w;
    }
    if (tok[0] == '#') return strtoull(tok + 1, NULL, 0);
    return strtoull(tok, NULL, 0);
}

static void emit_data(struct forth *F, const char *tok, int bytes) {
    uint64_t v = data_value(F, tok);
    unsigned char b[8];
    for (int i = 0; i < bytes; i++) b[i] = (unsigned char)(v >> (8 * i));
    emit_raw(F, b, bytes);
}

#if (defined SLJIT_CONFIG_X86_64 && SLJIT_CONFIG_X86_64)
/* physical x86-64 register index for an SLJIT register or a plain name */
static int asm_reg(struct forth *F, const char *s) {
    if ((s[0] == 'R' && isdigit((unsigned char)s[1])) ||
        (s[0] == 'S' && isdigit((unsigned char)s[1])) || strcmp(s, "SP") == 0) {
        int idx = sljit_get_register_index(SLJIT_GP_REGISTER, parse_reg(F, s));
        if (idx < 0) throw_error(F, "asm: virtual register");
        return idx;
    }
    static const struct { const char *n; int i; } t[] = {
        {"RAX",0},{"RCX",1},{"RDX",2},{"RBX",3},{"RSP",4},{"RBP",5},{"RSI",6},{"RDI",7},
        {"R8",8},{"R9",9},{"R10",10},{"R11",11},{"R12",12},{"R13",13},{"R14",14},{"R15",15},
        {NULL,0}
    };
    for (int i = 0; t[i].n; i++) if (strcmp(t[i].n, s) == 0) return t[i].i;
    throw_error(F, "asm: bad register");
    return 0;
}

static void asm_mov(struct forth *F, int d, int s) {              /* mov d, s (r64) */
    unsigned char b[3];
    b[0] = (unsigned char)(0x48 | ((s >= 8) ? 4 : 0) | ((d >= 8) ? 1 : 0));
    b[1] = 0x89;
    b[2] = (unsigned char)(0xC0 | ((s & 7) << 3) | (d & 7));
    emit_raw(F, b, 3);
}

static void asm_mov_imm(struct forth *F, int d, uint64_t v) {     /* mov d, imm64 */
    unsigned char b[10];
    b[0] = (unsigned char)(0x48 | ((d >= 8) ? 1 : 0));
    b[1] = (unsigned char)(0xB8 | (d & 7));
    for (int i = 0; i < 8; i++) b[2 + i] = (unsigned char)(v >> (8 * i));
    emit_raw(F, b, 10);
}

static void asm_call(struct forth *F, int r) {                    /* call r64 */
    unsigned char b[3]; int n = 0;
    if (r >= 8) b[n++] = 0x41;
    b[n++] = 0xFF;
    b[n++] = (unsigned char)(0xD0 | (r & 7));
    emit_raw(F, b, n);
}

static void asm_pushpop(struct forth *F, int r, int is_push) {    /* push/pop r64 */
    unsigned char b[2]; int n = 0;
    if (r >= 8) b[n++] = 0x41;
    b[n++] = (unsigned char)((is_push ? 0x50 : 0x58) + (r & 7));
    emit_raw(F, b, n);
}

static void asm_mov_al(struct forth *F, int v) {                  /* mov al, imm8 */
    unsigned char b[2] = { 0xB0, (unsigned char)(v & 0xFF) };
    emit_raw(F, b, 2);
}
#endif /* SLJIT_CONFIG_X86_64 */

/* ---- instruction dispatch --------------------------------------------- */
static void ir_instruction(struct forth *F, const char *m) {
    if (strcmp(m, "enter") == 0) {
        if (F->entered) throw_error(F, "duplicate enter");
        int t = type_from_name(ir_need(F));
        if (t < 0) throw_error(F, "bad return type");
        F->cur_ret = (sljit_s32)t;
        int sc = atoi(ir_need(F));
        int sv = atoi(ir_need(F));
        int lc = atoi(ir_need(F));
        sc |= SLJIT_ENTER_FLOAT(F->enter_fsc) | SLJIT_ENTER_VECTOR(F->enter_vsc);
        sv |= SLJIT_ENTER_FLOAT(F->enter_fsv) | SLJIT_ENTER_VECTOR(F->enter_vsv);
        sljit_s32 at = F->cur_ret;
        int idx = 1;
        while (F->irtok_pos < F->irtok_count) {
            int a = type_from_name(F->irtok[F->irtok_pos]);
            if (a < 0 || a == SLJIT_ARG_TYPE_RET_VOID || idx > 4) break;
            at |= (sljit_s32)(a << (idx * SLJIT_ARG_SHIFT));
            idx++;
            F->irtok_pos++;
        }
        F->cur_argtypes = at;
        sljit_emit_enter(F->jcomp, 0, at, sc, sv, lc);
        F->entered = 1;
        return;
    }
    if (strcmp(m, "fscratches") == 0) { F->enter_fsc = atoi(ir_need(F)); return; }
    if (strcmp(m, "fsaveds") == 0)    { F->enter_fsv = atoi(ir_need(F)); return; }
    if (strcmp(m, "vscratches") == 0) { F->enter_vsc = atoi(ir_need(F)); return; }
    if (strcmp(m, "vsaveds") == 0)    { F->enter_vsv = atoi(ir_need(F)); return; }
    if (strcmp(m, "locals") == 0) {
        if (F->entered) throw_error(F, "'locals' must precede the body");
        F->forth_local = atoi(ir_need(F));
        return;
    }
    if (strcmp(m, "sig") == 0) {
        int t = type_from_name(ir_need(F));
        if (t < 0) throw_error(F, "bad signature type");
        F->cur_ret = (sljit_s32)t;
        sljit_s32 at = F->cur_ret;
        int idx = 1;
        while (F->irtok_pos < F->irtok_count) {
            int a = type_from_name(F->irtok[F->irtok_pos]);
            if (a < 0 || a == SLJIT_ARG_TYPE_RET_VOID || idx > 4) break;
            at |= (sljit_s32)(a << (idx * SLJIT_ARG_SHIFT));
            idx++;
            F->irtok_pos++;
        }
        F->cur_argtypes = at;
        F->cur_nargs = idx - 1;
        return;
    }
    if (strcmp(m, "dlopen") == 0) {
        const char *lib = ir_need(F);
        if (!dyn_open(F, lib, RTLD_NOW | RTLD_GLOBAL))
            throw_error(F, F->dyn_err[0] ? F->dyn_err : "dlopen failed");
        return;
    }
    if (strcmp(m, "dlclose") == 0) {
        const char *lib = ir_need(F);
        for (int i = 0; i < F->nlibs; i++)
            if (strcmp(F->dynlibs[i].name, lib) == 0) { dyn_close(F, F->dynlibs[i].handle); return; }
        throw_error(F, "library not open");
    }
    if (strcmp(m, "label:") == 0 || strcmp(m, "setlabel") == 0) {
        define_label(F, ir_need(F));
        return;
    }
    if (strcmp(m, "op0") == 0) {
        ensure_enter(F);
        sljit_emit_op0(F->jcomp, parse_opnum(F, ir_need(F)));
        return;
    }
    if (strcmp(m, "op1") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 d, s; sljit_sw dw, sw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &s, &sw);
        sljit_emit_op1(F->jcomp, op, d, dw, s, sw);
        return;
    }
    if (strcmp(m, "op2") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        sljit_emit_op2(F->jcomp, op, d, dw, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "cmp") == 0) {
        ensure_enter(F);
        const char *first = ir_need(F);
        sljit_s32 ty = cond_lookup(first);
        if (ty < 0) ty = parse_opnum(F, first);
        sljit_s32 a, b; sljit_sw aw, bw;
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        const char *lb = ir_need(F);
        record_jump(F, sljit_emit_cmp(F->jcomp, ty, a, aw, b, bw), label_ref(lb));
        return;
    }
    if (strcmp(m, "jump") == 0) {
        ensure_enter(F);
        sljit_s32 ty = parse_opnum(F, ir_need(F));
        const char *target = ir_need(F);
        struct sljit_jump *j = sljit_emit_jump(F->jcomp, ty);
        if (target[0] == '#') sljit_set_target(j, (sljit_uw)strtoull(target + 1, NULL, 0));
        else record_jump(F, j, label_ref(target));
        return;
    }
    if (strcmp(m, "rewjump") == 0) {
        ensure_enter(F);
        const char *name = ir_need(F);
        sljit_s32 ty = parse_opnum(F, ir_need(F));
        const char *target = ir_need(F);
        struct sljit_jump *j = sljit_emit_jump(F->jcomp, ty | SLJIT_REWRITABLE_JUMP);
        if (target[0] == '#') sljit_set_target(j, (sljit_uw)strtoull(target + 1, NULL, 0));
        else record_jump(F, j, label_ref(target));
        if (F->n_pend_jumps >= MAX_PATCH) throw_error(F, "too many rewritable jumps");
        F->pend_jumps[F->n_pend_jumps] = j;
        strncpy(F->pend_jump_names[F->n_pend_jumps], name, 31);
        F->pend_jump_names[F->n_pend_jumps][31] = 0;
        F->n_pend_jumps++;
        return;
    }
    if (strcmp(m, "const") == 0 || strcmp(m, "rwconst") == 0) {
        ensure_enter(F);
        const char *name = NULL;
        if (m[0] == 'r') name = ir_need(F);      /* rwconst <name> <op> <dst> <value> */
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 d; sljit_sw dw;
        parse_operand(F, ir_need(F), &d, &dw);
        sljit_sw val = (sljit_sw)strtol(ir_need(F), NULL, 0);
        struct sljit_const *c = sljit_emit_const(F->jcomp, op, d, dw, val);
        if (name) {
            if (F->n_pend_consts >= MAX_PATCH) throw_error(F, "too many rewritable consts");
            F->pend_consts[F->n_pend_consts] = c;
            F->pend_const_ops[F->n_pend_consts] = op;
            strncpy(F->pend_const_names[F->n_pend_consts], name, 31);
            F->pend_const_names[F->n_pend_consts][31] = 0;
            F->n_pend_consts++;
        }
        return;
    }
    if (strcmp(m, "jmp") == 0) {
        ensure_enter(F);
        const char *lb = ir_need(F);
        record_jump(F, sljit_emit_jump(F->jcomp, SLJIT_JUMP), label_ref(lb));
        return;
    }
    if (strcmp(m, "call") == 0 || strcmp(m, "icall") == 0) {
        ensure_enter(F);
        sljit_s32 p; sljit_sw w;
        const char *tok = ir_need(F);
        parse_operand(F, tok, &p, &w);
        /* interpreter helpers (&forth_*) receive the context as last argument */
        if (tok[0] == '&' && strncmp(tok + 1, "forth_", 6) == 0 && F->cur_nargs < 4) {
            sljit_emit_op1(F->jcomp, SLJIT_MOV_P, SLJIT_R(F->cur_nargs), 0,
                           SLJIT_IMM, (sljit_sw)(intptr_t)F);
            F->cur_argtypes |= (sljit_s32)(SLJIT_ARG_TYPE_P << ((F->cur_nargs + 1) * SLJIT_ARG_SHIFT));
        }
        sljit_emit_icall(F->jcomp, SLJIT_CALL, F->cur_argtypes, p, w);
        return;
    }
    if (strcmp(m, "icall.reg") == 0) {
        ensure_enter(F);
        sljit_s32 p; sljit_sw w;
        parse_operand(F, ir_need(F), &p, &w);
        sljit_emit_icall(F->jcomp, SLJIT_CALL_REG_ARG, F->cur_argtypes, p, w);
        return;
    }
    if (strcmp(m, "callw") == 0) {
        ensure_enter(F);
        const char *name = ir_need(F);
        Word *w = find(F, name);
        if (!w) throw_error(F, "callw: unknown word");
        if (!w->native) throw_error(F, "callw: not a native word");
        sljit_emit_op1(F->jcomp, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S1, 0);
        sljit_emit_icall(F->jcomp, SLJIT_CALL, SLJIT_ARGS1(P, P), SLJIT_IMM,
                         (sljit_sw)(intptr_t)w->native);
        sljit_emit_op1(F->jcomp, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_R0, 0);
        return;
    }
    if (strcmp(m, "ret") == 0) {
        ensure_enter(F);
        if (F->irtok_pos < F->irtok_count && looks_like_operand(F->irtok[F->irtok_pos])) {
            sljit_s32 s; sljit_sw sw;
            parse_operand(F, ir_need(F), &s, &sw);
            sljit_emit_return(F->jcomp, ret_movop(F), s, sw);
        } else {
            sljit_emit_return_void(F->jcomp);
        }
        F->returned = 1;
        return;
    }
    if (strcmp(m, "nop") == 0)  { ensure_enter(F); sljit_emit_op0(F->jcomp, SLJIT_NOP); return; }
    if (strcmp(m, "int3") == 0) { ensure_enter(F); sljit_emit_op0(F->jcomp, SLJIT_BREAKPOINT); return; }

    sljit_s32 op;
    if ((op = op1_lookup(m)) != 0) {
        ensure_enter(F);
        sljit_s32 d, s; sljit_sw dw, sw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &s, &sw);
        sljit_emit_op1(F->jcomp, op, d, dw, s, sw);
        return;
    }
    int is32 = 0;
    if ((op = op2_lookup(m, &is32)) != 0) {
        ensure_enter(F);
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        sljit_emit_op2(F->jcomp, op | (is32 ? SLJIT_32 : 0), d, dw, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "neg") == 0) {
        ensure_enter(F);
        sljit_s32 d, s; sljit_sw dw, sw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &s, &sw);
        sljit_emit_op2(F->jcomp, SLJIT_SUB, d, dw, SLJIT_IMM, 0, s, sw);
        return;
    }
    if (strcmp(m, "not") == 0) {
        ensure_enter(F);
        sljit_s32 d, s; sljit_sw dw, sw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &s, &sw);
        sljit_emit_op2(F->jcomp, SLJIT_XOR, d, dw, s, sw, SLJIT_IMM, -1);
        return;
    }
    if (strcmp(m, "op2u") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 a, b; sljit_sw aw, bw;
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        sljit_emit_op2u(F->jcomp, op, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "op2r") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        sljit_emit_op2r(F->jcomp, op, d, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "op2shift") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 d, a, b; sljit_sw dw, aw, bw, sh;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        sh = (sljit_sw)strtol(ir_need(F), NULL, 0);
        sljit_emit_op2_shift(F->jcomp, op, d, dw, a, aw, b, bw, sh);
        return;
    }
    if (strcmp(m, "op2cmpz") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        const char *lb = ir_need(F);
        record_jump(F, sljit_emit_op2cmpz(F->jcomp, op, d, dw, a, aw, b, bw), label_ref(lb));
        return;
    }
    if (strcmp(m, "setflags") == 0) {
        ensure_enter(F);
        const char *first = ir_need(F);
        sljit_s32 ty = cond_lookup(first);
        if (ty < 0) ty = parse_opnum(F, first);
        sljit_s32 a, b; sljit_sw aw, bw;
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        sljit_emit_op2u(F->jcomp, SLJIT_SUB | cmp_set_flag(ty), a, aw, b, bw);
        return;
    }
    if (strcmp(m, "flags") == 0) {
        ensure_enter(F);
        const char *first = ir_need(F);
        sljit_s32 ty = cond_lookup(first);
        if (ty < 0) ty = parse_opnum(F, first);
        sljit_s32 d; sljit_sw dw;
        parse_operand(F, ir_need(F), &d, &dw);
        sljit_emit_op_flags(F->jcomp, SLJIT_MOV, d, dw, ty);
        return;
    }
    if (strcmp(m, "select") == 0) {
        ensure_enter(F);
        const char *first = ir_need(F);
        sljit_s32 ty = cond_lookup(first);
        if (ty < 0) ty = parse_opnum(F, first);
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        sljit_emit_select(F->jcomp, ty, d, a, aw, b);
        return;
    }
    if (strcmp(m, "ijump") == 0) {
        ensure_enter(F);
        sljit_s32 ty = parse_opnum(F, ir_need(F));
        sljit_s32 p; sljit_sw w;
        parse_operand(F, ir_need(F), &p, &w);
        sljit_emit_ijump(F->jcomp, ty, p, w);
        return;
    }
    if (strcmp(m, "opsrc") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 p; sljit_sw w;
        parse_operand(F, ir_need(F), &p, &w);
        sljit_emit_op_src(F->jcomp, op, p, w);
        return;
    }
    if (strcmp(m, "opdst") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 p; sljit_sw w;
        parse_operand(F, ir_need(F), &p, &w);
        sljit_emit_op_dst(F->jcomp, op, p, w);
        return;
    }
    if (strcmp(m, "ret_to") == 0) {
        ensure_enter(F);
        sljit_s32 p; sljit_sw w;
        parse_operand(F, ir_need(F), &p, &w);
        sljit_emit_return_to(F->jcomp, p, w);
        F->returned = 1;
        return;
    }
    if (strcmp(m, "opaddr") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 d; sljit_sw dw;
        parse_operand(F, ir_need(F), &d, &dw);
        const char *lb = ir_need(F);
        record_jump(F, sljit_emit_op_addr(F->jcomp, op, d, dw), label_ref(lb));
        return;
    }
    if (strcmp(m, "aligned_label") == 0) {
        sljit_s32 al = parse_opnum(F, ir_need(F));
        const char *name = ir_need(F);
        define_label_ptr(F, sljit_emit_aligned_label(F->jcomp, al, NULL), name);
        return;
    }
    if (strcmp(m, "custom") == 0) {
        ensure_enter(F);
        int n = atoi(ir_need(F));
        if (n < 0 || n > 256) throw_error(F, "custom: bad size");
        unsigned char bytes[256];
        for (int i = 0; i < n; i++)
            bytes[i] = (unsigned char)strtol(ir_need(F), NULL, 0);
        sljit_emit_op_custom(F->jcomp, bytes, (sljit_u32)n);
        return;
    }
    if (strcmp(m, "db") == 0 || strcmp(m, "dw") == 0 ||
        strcmp(m, "dd") == 0 || strcmp(m, "dq") == 0) {
        ensure_enter(F);
        int bytes = (m[1] == 'b') ? 1 : (m[1] == 'w') ? 2 : (m[1] == 'd') ? 4 : 8;
        emit_data(F, ir_need(F), bytes);
        return;
    }
    if (strcmp(m, "require") == 0 || strcmp(m, "room") == 0) {
        /* entry guard: raise a catchable under/overflow if invalid */
        ensure_enter(F);
        int n = atoi(ir_need(F));
        void *fn = (strcmp(m, "require") == 0) ? (void *)(intptr_t)forth_need
                                               : (void *)(intptr_t)forth_room;
        sljit_emit_op1(F->jcomp, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S1, 0);                 /* r0 = sp */
        sljit_emit_op2(F->jcomp, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0,
                       SLJIT_IMM, (sljit_sw)(intptr_t)F->dstack);                        /* r0 -= dstack */
        sljit_emit_op2(F->jcomp, SLJIT_LSHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 3);    /* /8 = depth */
        sljit_emit_op1(F->jcomp, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, n);                  /* r1 = need */
        sljit_emit_op1(F->jcomp, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)(intptr_t)F);
        sljit_emit_icall(F->jcomp, SLJIT_CALL, SLJIT_ARGS3V(W, W, P), SLJIT_IMM, (sljit_sw)fn);
        return;
    }
#if (defined SLJIT_CONFIG_X86_64 && SLJIT_CONFIG_X86_64)
    if (strncmp(m, "asm.", 4) == 0) {
        ensure_enter(F);
        if (strcmp(m, "asm.mov") == 0)
            asm_mov(F, asm_reg(F, ir_need(F)), asm_reg(F, ir_need(F)));
        else if (strcmp(m, "asm.mov.imm") == 0)
            asm_mov_imm(F, asm_reg(F, ir_need(F)), data_value(F, ir_need(F)));
        else if (strcmp(m, "asm.call") == 0)
            asm_call(F, asm_reg(F, ir_need(F)));
        else if (strcmp(m, "asm.push") == 0)
            asm_pushpop(F, asm_reg(F, ir_need(F)), 1);
        else if (strcmp(m, "asm.pop") == 0)
            asm_pushpop(F, asm_reg(F, ir_need(F)), 0);
        else if (strcmp(m, "asm.mov.al") == 0)
            asm_mov_al(F, (int)strtol(ir_need(F), NULL, 0));
        else if (strcmp(m, "asm.ret") == 0)  { unsigned char b = 0xC3; emit_raw(F, &b, 1); }
        else if (strcmp(m, "asm.nop") == 0)  { unsigned char b = 0x90; emit_raw(F, &b, 1); }
        else if (strcmp(m, "asm.int3") == 0) { unsigned char b = 0xCC; emit_raw(F, &b, 1); }
        else throw_error(F, "unknown asm mnemonic");
        return;
    }
#endif
    /* ---- floating point ---- */
    if (strcmp(m, "fop1") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 d, s; sljit_sw dw, sw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &s, &sw);
        sljit_emit_fop1(F->jcomp, op, d, dw, s, sw);
        return;
    }
    if (strcmp(m, "fop2") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 d, a, b; sljit_sw dw, aw, bw;
        parse_operand(F, ir_need(F), &d, &dw);
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        sljit_emit_fop2(F->jcomp, op, d, dw, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "fop2r") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 d, a, b; sljit_sw aw, bw;
        parse_operand_p(F, ir_need(F), &d);
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        sljit_emit_fop2r(F->jcomp, op, d, a, aw, b, bw);
        return;
    }
    if (strcmp(m, "fcmp") == 0) {
        ensure_enter(F);
        const char *first = ir_need(F);
        sljit_s32 ty = fcond_lookup(first);
        if (ty < 0) ty = parse_opnum(F, first);
        sljit_s32 a, b; sljit_sw aw, bw;
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand(F, ir_need(F), &b, &bw);
        const char *lb = ir_need(F);
        record_jump(F, sljit_emit_fcmp(F->jcomp, ty, a, aw, b, bw), label_ref(lb));
        return;
    }
    if (strcmp(m, "fselect") == 0) {
        ensure_enter(F);
        const char *first = ir_need(F);
        sljit_s32 ty = fcond_lookup(first);
        if (ty < 0) ty = parse_opnum(F, first);
        sljit_s32 d, a, b; sljit_sw aw;
        parse_operand_p(F, ir_need(F), &d);
        parse_operand(F, ir_need(F), &a, &aw);
        parse_operand_p(F, ir_need(F), &b);
        sljit_emit_fselect(F->jcomp, ty, d, a, aw, b);
        return;
    }
    if (strcmp(m, "fcopy") == 0) {
        ensure_enter(F);
        sljit_s32 op = parse_opnum(F, ir_need(F));
        sljit_s32 f, r; sljit_sw rw;
        parse_operand_p(F, ir_need(F), &f);
        parse_operand(F, ir_need(F), &r, &rw);
        sljit_emit_fcopy(F->jcomp, op, f, r);
        return;
    }
    if (strcmp(m, "fset32") == 0) {
        ensure_enter(F);
        sljit_s32 f; parse_operand_p(F, ir_need(F), &f);
        sljit_f32 v = (sljit_f32)strtod(ir_need(F), NULL);
        sljit_emit_fset32(F->jcomp, f, v);
        return;
    }
    if (strcmp(m, "fset64") == 0) {
        ensure_enter(F);
        sljit_s32 f; parse_operand_p(F, ir_need(F), &f);
        sljit_f64 v = (sljit_f64)strtod(ir_need(F), NULL);
        sljit_emit_fset64(F->jcomp, f, v);
        return;
    }
    if (strcmp(m, "fmem") == 0 || strcmp(m, "fmem_update") == 0) {
        ensure_enter(F);
        sljit_s32 ty = parse_opnum(F, ir_need(F));
        sljit_s32 f; parse_operand_p(F, ir_need(F), &f);
        sljit_s32 memop; sljit_sw memw;
        parse_operand(F, ir_need(F), &memop, &memw);
        if (m[5] == '_') sljit_emit_fmem_update(F->jcomp, ty, f, memop, memw);
        else            sljit_emit_fmem(F->jcomp, ty, f, memop, memw);
        return;
    }
    throw_error(F, "unknown native mnemonic");
}

/* ---- assemble current irbuf into machine code ------------------------- */
static void *ir_compile(struct forth *F, int abi, int local) {
    F->jcomp = sljit_create_compiler(NULL);
    if (!F->jcomp) throw_error(F, "cannot create jit compiler");
    F->forth_abi = abi;
    F->forth_local = local;
    F->entered = F->returned = 0;
    F->nlabels = F->njumps = 0;
    F->cur_ret = SLJIT_ARG_TYPE_RET_VOID;
    F->cur_argtypes = 0;
    F->enter_fsc = F->enter_fsv = F->enter_vsc = F->enter_vsv = 0;
    F->n_pend_jumps = F->n_pend_consts = 0;

    ir_tokenize(F);
    F->irtok_pos = 0;

    if (F->forth_abi) {
        if (F->irtok_pos < F->irtok_count && strcmp(F->irtok[F->irtok_pos], "locals") == 0) {
            F->irtok_pos++;
            F->forth_local = atoi(ir_need(F));
        }
        /* scratch-register argument: the stack pointer arrives in R0, which
           matches the C call ABI used by callw, so native words can call
           each other directly. */
        sljit_emit_enter(F->jcomp, 0, SLJIT_ARGS1(P, P_R),
                         4 | SLJIT_ENTER_FLOAT(6), 2, F->forth_local);
        sljit_emit_op1(F->jcomp, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_R0, 0);
        F->cur_ret = SLJIT_ARG_TYPE_P;
        F->cur_argtypes = SLJIT_ARGS1(P, P);
        F->entered = 1;
    }

    while (F->irtok_pos < F->irtok_count) ir_instruction(F, ir_need(F));

    resolve_jumps(F);
    if (F->forth_abi) sljit_emit_return(F->jcomp, SLJIT_MOV_P, SLJIT_S1, 0);
    if (!F->forth_abi && !F->returned) throw_error(F, "native code needs 'ret'");
    if (sljit_get_compiler_error(F->jcomp) != SLJIT_SUCCESS)
        throw_error(F, "sljit rejected the instruction stream");

    void *code = sljit_generate_code(F->jcomp, 0, NULL);
    if (!code) {
        sljit_free_compiler(F->jcomp);
        F->jcomp = NULL;
        F->n_pend_jumps = F->n_pend_consts = 0;
        throw_error(F, "code generation failed");
    }
    F->patch_exec_off = sljit_get_executable_offset(F->jcomp);
    F->last_gen_size = sljit_get_generated_code_size(F->jcomp);
    for (int i = 0; i < F->n_pend_jumps && F->npatch_jumps < MAX_PATCH; i++) {
        F->patch_jumps[F->npatch_jumps].addr = sljit_get_jump_addr(F->pend_jumps[i]);
        memcpy(F->patch_jumps[F->npatch_jumps].name, F->pend_jump_names[i], sizeof F->patch_jumps[0].name);
        F->patch_jumps[F->npatch_jumps].name[31] = 0;
        F->npatch_jumps++;
    }
    for (int i = 0; i < F->n_pend_consts && F->npatch_consts < MAX_PATCH; i++) {
        F->patch_consts[F->npatch_consts].addr = sljit_get_const_addr(F->pend_consts[i]);
        F->patch_consts[F->npatch_consts].op = F->pend_const_ops[i];
        memcpy(F->patch_consts[F->npatch_consts].name, F->pend_const_names[i], sizeof F->patch_consts[0].name);
        F->patch_consts[F->npatch_consts].name[31] = 0;
        F->npatch_consts++;
    }
    F->n_pend_jumps = F->n_pend_consts = 0;
    sljit_free_compiler(F->jcomp);
    F->jcomp = NULL;
    if (F->njit < MAX_JIT) F->jit_codes[F->njit++] = code;
    return code;
}

/* ---- Forth-facing native words ---------------------------------------- */
static void p_code(struct forth *F) {
    if (F->state != 0) throw_error(F, "CODE not allowed here");
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after CODE");
    Word *w = newword(F, name, p_native);
    w->flags |= F_HIDDEN;
    F->compiling = w;
    F->irlen = 0;
    F->irbuf[0] = 0;
    F->state = 2;
}

static void p_endcode(struct forth *F) {
    if (F->state != 2) throw_error(F, ";CODE without CODE");
    char *body = strdup(F->irbuf);         /* keep the IR source for INLINE */
    void *code = ir_compile(F, 1, 0);
    if (F->compiling) {
        F->compiling->native = code;
        F->compiling->irbody = body;
        F->compiling->flags &= ~F_HIDDEN;
    } else {
        free(body);
    }
    F->compiling = NULL;
    F->state = 0;
}

static int read_quote(struct forth *F, char *out, int cap) {
    while (*F->inbuf_ptr == ' ' || *F->inbuf_ptr == '\t') F->inbuf_ptr++;
    int n = 0;
    while (*F->inbuf_ptr && *F->inbuf_ptr != '"') {
        if (n < cap - 1) out[n++] = *F->inbuf_ptr;
        F->inbuf_ptr++;
    }
    if (*F->inbuf_ptr != '"') return 0;
    F->inbuf_ptr++;
    out[n] = 0;
    return 1;
}

static void p_squote(struct forth *F) {
    /* distinct buffers per S" so consecutive literals do not alias */
    static char ring[8][1024];
    static int ri = 0;
    char *slot = ring[ri];
    ri = (ri + 1) & 7;
    if (!read_quote(F, slot, 1024)) throw_error(F, "unterminated S\"");
    int n = (int)strlen(slot);
    if (F->state == 1) {
        int cells = (n + (int)sizeof(cell)) / (int)sizeof(cell);
        if (F->memtop + cells > MEM_SIZE) throw_error(F, "data space full");
        char *dst = (char *)F->mem + (size_t)F->memtop * sizeof(cell);
        memcpy(dst, slot, (size_t)n);
        cell a = (cell)(intptr_t)dst;
        F->memtop += cells;
        compile_xt(F, F->W_LIT); emit(F, a);
        compile_xt(F, F->W_LIT); emit(F, n);
    } else {
        push(F, (cell)slot);
        push(F, (cell)n);
    }
}

static void p_irquote(struct forth *F) {
    char frag[2048];
    if (!read_quote(F, frag, (int)sizeof frag)) throw_error(F, "unterminated IR\"");
    ir_put_text(F, frag, (int)strlen(frag));
}

static void p_assemble(struct forth *F) {
    cell len = pop(F);
    char *a = (char *)pop(F);
    if (len < 0 || len >= IRBUF_SIZE) throw_error(F, "bad IR length");
    memcpy(F->irbuf, a, (size_t)len);
    F->irbuf[len] = 0;
    F->irlen = (int)len;
    void *code = ir_compile(F, 0, 0);
    push(F, (cell)code);
}

static void p_tick(struct forth *F) {
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after '");
    Word *w = find(F, name);
    if (!w) throw_error(F, "? tick");
    push(F, (cell)w);                 /* execution token (Word*) */
}

/* ( xt -- c-addr ) the C code pointer, for icall */
static void p_code_addr(struct forth *F) { push(F, (cell)((Word *)pop(F))->code); }

/* immediate: compile a literal XT */
static void p_bracket_tick(struct forth *F) {
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after [']");
    Word *w = find(F, name);
    if (!w) throw_error(F, "? [']");
    if (F->state != 1) throw_error(F, "['] outside a definition");
    log_put(F, name);
    compile_xt(F, F->W_LIT);
    emit(F, (cell)w);
}

/* POSTPONE name -- compile the compilation semantics of name */
static void p_postpone(struct forth *F) {
    if (F->state != 1) throw_error(F, "POSTPONE outside a definition");
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after POSTPONE");
    Word *w = find(F, name);
    if (!w) throw_error(F, "? POSTPONE");
    log_put(F, name);
    if (w->flags & F_IMMEDIATE) {
        compile_xt(F, w);                 /* run it when the enclosing word runs */
    } else {
        Word *ct = find(F, "COMPILE,");
        if (!ct) throw_error(F, "POSTPONE needs COMPILE,");
        compile_xt(F, F->W_LIT);
        emit(F, (cell)(intptr_t)w);       /* push the xt ... */
        compile_xt(F, ct);                /* ... then compile it */
    }
}

/* [COMPILE] name -- compile an immediate word instead of executing it */
static void p_bracket_compile(struct forth *F) {
    if (F->state != 1) throw_error(F, "[COMPILE] outside a definition");
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after [COMPILE]");
    Word *w = find(F, name);
    if (!w) throw_error(F, "? [COMPILE]");
    log_put(F, name);
    compile_xt(F, w);
}

/* ---- exceptions ------------------------------------------------------- */
static void p_abort(struct forth *F) { raise(F, ERR_ABORT, "ABORT"); }

static void p_execute(struct forth *F) {
    Word *w = (Word *)pop(F);
    int saved_ip = F->ip;
    execute_word(F, w);
    F->ip = saved_ip;
}

static void p_catch(struct forth *F) {
    Word *w = (Word *)pop(F);
    if (F->xsp >= MAX_XFRAME) raise(F, ERR_GENERIC, "too many CATCH frames");
    XFrame *f = &F->xframes[F->xsp++];
    f->fx_sp = F->sp; f->fx_rp = F->rp; f->fx_ip = F->ip; f->fx_state = F->state; f->fx_compiling = F->compiling;
    if (setjmp(f->env) == 0) {
        execute_word(F, w);
        F->ip = f->fx_ip;
        F->xsp--;
        push(F, 0);
    } else {
        F->sp = f->fx_sp; F->rp = f->fx_rp; F->ip = f->fx_ip;
        F->state = f->fx_state; F->compiling = f->fx_compiling;
        if (F->jcomp) { sljit_free_compiler(F->jcomp); F->jcomp = NULL; }
        F->xsp--;
        push(F, f->fx_code);
    }
}

static void p_throw(struct forth *F) {
    cell n = pop(F);
    if (n == 0) return;
    raise(F, (int)n, "THROW");
}

static void p_error_msg(struct forth *F) {
    push(F, (cell)(intptr_t)F->err_msg);
    push(F, (cell)strlen(F->err_msg));
}
static void p_error_code(struct forth *F) { push(F, F->err_code); }

/* ( flag c-addr u -- ) raise with an inline message */
static void p_abortq(struct forth *F) {
    cell len = pop(F);
    cell addr = pop(F);
    cell flag = pop(F);
    if (flag) {
        char buf[256];
        int n = (len < 0) ? 0 : (len < (cell)sizeof buf - 1 ? (int)len : (int)sizeof buf - 1);
        memcpy(buf, (void *)addr, (size_t)n);
        buf[n] = 0;
        raise(F, ERR_ABORTQ, buf);
    }
}

/* immediate: ABORT" message" — compile the string and (abort") */
/* store a string in data space and compile (lit addr)(lit len)( word ) */
static void compile_inline_string(struct forth *F, const char *s, int n, Word *emitword) {
    int cells = (n + (int)sizeof(cell)) / (int)sizeof(cell);
    if (F->memtop + cells > MEM_SIZE) throw_error(F, "data space full");
    char *dst = (char *)F->mem + (size_t)F->memtop * sizeof(cell);
    memcpy(dst, s, (size_t)n);
    cell a = (cell)(intptr_t)dst;
    F->memtop += cells;
    compile_xt(F, F->W_LIT); emit(F, a);
    compile_xt(F, F->W_LIT); emit(F, n);
    compile_xt(F, emitword);
}

static void p_abort_quote(struct forth *F) {
    static char abq[1024];
    if (F->state != 1) throw_error(F, "ABORT\" only in compile state");
    if (!read_quote(F, abq, (int)sizeof abq)) throw_error(F, "unterminated ABORT\"");
    compile_inline_string(F, abq, (int)strlen(abq), F->W_ABORTQ);
}

static void p_dot_quote(struct forth *F) {
    static char dq[1024];
    if (F->state != 1) throw_error(F, ".\" only in compile state");
    if (!read_quote(F, dq, (int)sizeof dq)) throw_error(F, "unterminated .\"");
    Word *t = find(F, "TYPE");
    if (!t) throw_error(F, ".\" requires TYPE");
    compile_inline_string(F, dq, (int)strlen(dq), t);
}

static void p_native_def(struct forth *F) {
    cell cp = pop(F);
    char name[NAME_LEN];
    if (!next_token(F, name)) throw_error(F, "name expected after NATIVE");
    Word *w = newword(F, name, p_native);
    w->native = (void *)cp;
}

/* address of libc putchar, for icall demos */
static void p_c_putchar(struct forth *F) { push(F, (cell)(intptr_t)putchar); }

/* ---- string and byte-memory words ------------------------------------- */
/* ---- dlopen / dlsym Forth surface ------------------------------------- */
static void p_dlopen(struct forth *F) {
    cell len = pop(F);
    cell addr = pop(F);
    char name[DYN_NAME];
    if (!copy_cstr(addr, len, name, (int)sizeof name)) { dyn_seterr(F, "bad library name"); push(F, 0); return; }
    push(F, (cell)dyn_open(F, name, RTLD_NOW | RTLD_GLOBAL));
}

static void p_dlsym(struct forth *F) {
    cell len = pop(F);
    cell addr = pop(F);
    cell handle = pop(F);
    char name[DYN_NAME];
    if (!copy_cstr(addr, len, name, (int)sizeof name)) { dyn_seterr(F, "bad symbol name"); push(F, 0); return; }
    push(F, (cell)dyn_sym(F, (void *)handle, name));
}

static void p_dlclose(struct forth *F) { dyn_close(F, (void *)pop(F)); }

static void p_dlerror(struct forth *F) {
    push(F, (cell)(intptr_t)F->dyn_err);
    push(F, (cell)strlen(F->dyn_err));
}

static void p_dllibs(struct forth *F) {
    for (int i = 0; i < F->nlibs; i++)
        printf("%s %p\n", F->dynlibs[i].name, (void *)F->dynlibs[i].handle);
}

/* ---- runtime patching / introspection --------------------------------- */
static void p_set_jump_addr(struct forth *F) {
    cell target = pop(F);
    cell len = pop(F);
    cell addr = pop(F);
    char name[32];
    if (!copy_cstr(addr, len, name, (int)sizeof name)) throw_error(F, "bad name");
    for (int i = 0; i < F->npatch_jumps; i++)
        if (strcmp(F->patch_jumps[i].name, name) == 0) {
            sljit_set_jump_addr(F->patch_jumps[i].addr, (sljit_uw)target, F->patch_exec_off);
            return;
        }
    throw_error(F, "set-jump-addr: unknown name");
}

static void p_set_const(struct forth *F) {
    cell value = pop(F);
    cell len = pop(F);
    cell addr = pop(F);
    char name[32];
    if (!copy_cstr(addr, len, name, (int)sizeof name)) throw_error(F, "bad name");
    for (int i = 0; i < F->npatch_consts; i++)
        if (strcmp(F->patch_consts[i].name, name) == 0) {
            sljit_set_const(F->patch_consts[i].addr, F->patch_consts[i].op, (sljit_sw)value, F->patch_exec_off);
            return;
        }
    throw_error(F, "set-const: unknown name");
}

static void p_cpu_feature(struct forth *F) { push(F, (cell)sljit_has_cpu_feature((sljit_s32)pop(F))); }
static void p_jit_size(struct forth *F) { push(F, (cell)F->last_gen_size); }

static void p_native(struct forth *F) {
    typedef cell *(*Nat)(cell *);
    Nat fn = (Nat)F->curr->native;
    cell *r = fn(F->dstack + F->sp);
    ptrdiff_t n = r - F->dstack;
    if (n < 0 || n > STACK_SIZE) throw_error(F, "native stack error");
    F->sp = (int)n;
}

/* ===================================================================== */
/*  dictionary setup                                                     */
/* ===================================================================== */
static void def_imm(struct forth *F, const char *name, Prim code) {
    define_prim(F, name, code)->flags |= F_IMMEDIATE;
}

static void init_dict(struct forth *F) {
    F->W_EXIT    = define_prim(F, "(exit)",    p_exit);
    F->W_LIT     = define_prim(F, "(lit)",     p_lit);
    F->W_BRANCH  = define_prim(F, "(branch)",  p_branch);
    F->W_0BRANCH = define_prim(F, "(0branch)", p_0branch);
    F->W_DO      = define_prim(F, "(do)",      rt_do);
    F->W_QDO     = define_prim(F, "(?do)",     rt_qdo);
    F->W_LEAVE   = define_prim(F, "(leave)",   rt_leave);
    F->W_LOOP    = define_prim(F, "(loop)",    rt_loop);
    F->W_PLOOP   = define_prim(F, "(+loop)",   rt_ploop);
    F->W_ABORTQ  = define_prim(F, "(abort\")", p_abortq);
    F->W_DOES    = define_prim(F, "(does>)",   p_does_setup);

    /* EXIT lives in the native prelude */

    /* Everything else lives in the native prelude: stack, arithmetic,
       logic, comparisons, ?DUP, @ !, EMIT CR, DEPTH , ALLOT, / MOD, I J. */

    /* memory / dictionary */
    define_prim(F, "CREATE", p_create);
    def_imm(F, "DOES>", p_does_quote);
    define_prim(F, "JIT", p_jit);
    define_prim(F, "JIT-ALL", p_jit_all);
    define_prim(F, "WORDS", p_words);
    define_prim(F, "SEE", p_see);
    /* strings / byte memory live in the native prelude */

    /* compiler */
    def_imm(F, ":", p_colon);
    def_imm(F, ";", p_semicolon);
    def_imm(F, "RECURSE", p_recurse);
    define_prim(F, "SYNONYM", p_synonym);
    define_prim(F, "ALIAS", p_synonym);
    define_prim(F, "MARKER", p_marker);
    define_prim(F, "FORGET", p_forget);
    define_prim(F, "DEFER", p_defer);
    define_prim(F, "IS", p_is);
    define_prim(F, "ACTION-OF", p_action_of);
    define_prim(F, ":NONAME", p_noname);
    def_imm(F, "POSTPONE", p_postpone);
    def_imm(F, "[COMPILE]", p_bracket_compile);
    define_prim(F, "IMMEDIATE", p_immediate);
    define_prim(F, "COMPILE,", p_compile_comma);
    F->W_LOCAL       = define_prim(F, "(local)",       p_local);
    F->W_LOCAL_STORE = define_prim(F, "(local!)",      p_local_store);
    F->W_LOCALS_ENTER = define_prim(F, "(locals-enter)", p_locals_enter);
    F->W_LOCALS_EXIT  = define_prim(F, "(locals-exit)",  p_locals_exit);
    def_imm(F, "{", p_locals_brace);
    def_imm(F, "TO", p_to);
    def_imm(F, "\\", p_backslash);
    def_imm(F, "(", p_paren);
    define_prim(F, "PARSE-NAME", p_parse_name);
    def_imm(F, "[IF]", p_bracket_if);
    def_imm(F, "[ELSE]", p_bracket_else);
    def_imm(F, "[THEN]", p_bracket_then);

    /* native code / SLJIT */
    def_imm(F, "CODE", p_code);
    def_imm(F, ";CODE", p_endcode);
    def_imm(F, "IR\"", p_irquote);
    def_imm(F, "S\"", p_squote);
    define_prim(F, "ASSEMBLE", p_assemble);
    define_prim(F, "NATIVE", p_native_def);
    define_prim(F, "C-PUTCHAR", p_c_putchar);
    define_prim(F, "'", p_tick);
    define_prim(F, "CODE-ADDR", p_code_addr);
    def_imm(F, "[']", p_bracket_tick);
    define_prim(F, "EXECUTE", p_execute);
    define_prim(F, "CATCH", p_catch);
    define_prim(F, "THROW", p_throw);
    define_prim(F, "ABORT", p_abort);
    def_imm(F, "ABORT\"", p_abort_quote);
    def_imm(F, ".\"", p_dot_quote);
    define_prim(F, "ERROR-MSG", p_error_msg);
    define_prim(F, "ERROR-CODE", p_error_code);
    define_prim(F, "DLOPEN", p_dlopen);
    define_prim(F, "DLSYM", p_dlsym);
    define_prim(F, "DLCLOSE", p_dlclose);
    define_prim(F, "DLERROR", p_dlerror);
    define_prim(F, "DLLIBS", p_dllibs);
    define_prim(F, "SET-JUMP-ADDR", p_set_jump_addr);
    define_prim(F, "SET-CONST", p_set_const);
    define_prim(F, "CPU-FEATURE?", p_cpu_feature);
    define_prim(F, "JIT-SIZE", p_jit_size);

    for (int i = 0; sljit_consts[i].name; i++) {
        Word *cw = define_prim(F, sljit_consts[i].name, p_push_const);
        cw->data = sljit_consts[i].val;
    }

    /* target architecture code (see FORTH_ARCH) */
    define_prim(F, "ARCH", p_push_const)->data = FORTH_ARCH;
}

/* ===================================================================== */
/*  top level                                                            */
/* ===================================================================== */
static void run_line(struct forth *F) {
    if (setjmp(F->abort_env) != 0) {
        if (F->booting) {
            fprintf(stderr, "fatal: prelude error: %s\n", F->err_msg[0] ? F->err_msg : "(unknown)");
            exit(1);
        }
        if (F->err_msg[0]) { fputs(F->err_msg, stderr); fputc('\n', stderr); }
        /* an error aborted this line: reset to a clean interpreter state */
        F->sp = 0;
        F->rp = 0;
        F->state = 0;
        F->compiling = NULL;
        F->ip = -1;
        F->xsp = 0;
        F->lfbase = 0;
        F->lfree = 0;
        F->cur_locals.n = 0;
        F->abort_active = 0;
        return;
    }
    char tok[NAME_LEN];
    F->abort_active = 1;
    F->inbuf_ptr = F->inbuf;
    while (next_token(F, tok)) {
        if (F->cond_skip) {
            if (strcmp(tok, "[IF]") == 0) F->cond_depth++;
            else if (strcmp(tok, "[THEN]") == 0) { if (F->cond_depth == 0) F->cond_skip = 0; else F->cond_depth--; }
            else if (strcmp(tok, "[ELSE]") == 0 && F->cond_depth == 0) F->cond_skip = 0;
            continue;
        }
        interpret_token(F, tok);
    }
    F->abort_active = 0;
}

static void run_file(struct forth *F, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }
    while (fgets(F->inbuf, sizeof F->inbuf, f)) run_line(F);
    fclose(f);
}

/* Decompress and evaluate the embedded native prelude before the REPL. */
static void run_prelude(struct forth *F) {
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

    F->booting = 1;
    char *p = (char *)raw;
    while (*p) {
        char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof F->inbuf) len = sizeof F->inbuf - 1;
        memcpy(F->inbuf, p, len);
        F->inbuf[len] = 0;
        run_line(F);
        if (!nl) break;
        p = nl + 1;
    }
    F->booting = 0;
    free(raw);
}

int main(int argc, char **argv) {
    struct forth *F = calloc(1, sizeof *F);
    if (!F) { fputs("cannot allocate interpreter state\n", stderr); return 1; }

    F->inbuf_ptr = F->inbuf;
    init_dict(F);
    run_prelude(F);

    if (argc > 1) run_file(F, argv[1]);

    for (;;) {
        fputs("> ", stdout);
        fflush(stdout);
        if (!fgets(F->inbuf, sizeof F->inbuf, stdin)) break;
        run_line(F);
    }

    for (int i = 0; i < F->njit; i++) sljit_free_code(F->jit_codes[i], NULL);
    for (int i = 0; i < F->nwords; i++) { free(F->dict[i].irbody); free(F->dict[i].src); }
    dyn_close_all(F);
    free(F);
    return 0;
}
