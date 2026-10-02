#ifndef FORTH_H
#define FORTH_H
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <setjmp.h>
#include <dlfcn.h>
#include "sljitLir.h"

typedef long cell;

#define TRUE  (-1L)
#define FALSE (0L)

#define STACK_RESERVE ((size_t)1 << 26)   /* 64 MiB virtual per stack */
#define CODE_RESERVE  ((size_t)1 << 30)   /* 1 GiB virtual for threaded code */
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
#define LOCALS_MAX 4096                       /* per-definition, not a hard cap */
#define LOCALS_RESERVE ((size_t)1 << 20)      /* 8 MiB virtual per context */

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
#define XFRAME_RESERVE ((size_t)1 << 12)   /* 4096 virtual CATCH frames */
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
#define MAX_LOADED 64
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

/* ---- generated code registry (growable) ------------------------------ */

/* ---- compile/JIT state (single instance: compilation is serialized by the
   shared-memory lock; only the root context ever compiles) --------------- */
struct Ir {
    /* generated code registry (process-lifetime code: boot/session words) */
    void **jit_codes;
    int   njit;
    int   jit_cap;

    /* SLJIT assembler + IR token stream */
    struct sljit_compiler *jcomp;
    Word *jit_exit;
    char  irbuf[IRBUF_SIZE];
    int   irlen;
    char  ir_raw[MAX_IRTOK][64];
    char  irtok[MAX_IRTOK][64];
    int   irtok_count;
    int   irtok_pos;
    char  inline_toks[2048][64];
    int   inline_id, inline_depth;
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

    /* SEE compile log */
    char  lbuf[4096];
    int   llen;
};
extern struct Ir g_ir;

/* ---- process-wide shared state (dynamic libs, include tracking, markers) */
struct Shared {
    MarkerState markers[MAX_MARKERS];
    int    nmarkers;
    DynLib dynlibs[MAX_DYNLIB];
    int    nlibs;
    DynSym dynsyms[MAX_DYNSYM];
    int    nsyms;
    char   dyn_err[256];
    char   loaded[MAX_LOADED][256];
    int    nloaded;
};
extern struct Shared g_sh;

/* ---- stable-address arena -------------------------------------------- */
/* Blocks are malloc'd and never moved nor returned to the OS; freeing is
   administrative (arena_reset rewinds every block). */
typedef struct ArenaBlock { struct ArenaBlock *next; size_t used, cap; } ArenaBlock;
typedef struct {
    ArenaBlock *head;   /* most recently allocated block */
    ArenaBlock *cur;    /* block currently bump-allocated */
    size_t      block;  /* default block size */
} Arena;

/* ---- contiguous growable region -------------------------------------- */
/* Reserves virtual address space (PROT_NONE) and commits pages on demand.
   The base never moves and is never returned to the OS, so addresses baked
   into generated code stay valid; release is administrative (rewind). */
typedef struct {
    char  *base;
    size_t used, committed, reserved;
} Region;

int region_ensure(Region *r, size_t need);

struct forth {
    /* shared state root: tasks point at the root context */
    struct forth *root;

    /* this context's object store handle (0 until one is attached) */
    cell store;

    /* stacks: pointers into reserved regions (stable base, grow by commit) */
    Region dstack_r;
    cell  *dstack;
    int    sp;
    Region rstack_r;
    cell  *rstack;
    int    rp;

    /* threaded code: reserved region with a stable base (never moves) */
    Region code_r;
    cell  *fcode;
    cell  here;
    cell ip;

    /* data memory: reserved region + cell cursor (memtop) */
    Region data;
    cell   memtop;

    /* dictionary: malloc'd nodes linked backwards from `latest` */
    int   nwords;
    Word *latest;
    Word *compiling;
    Word *curr;
    cell  state;
    Word *last_created;
    Arena heap;                 /* user/guest arena (ALLOC / ARENA-RESET) */
    Arena words;                /* arena for dictionary Word nodes */
    Word *W_EXIT, *W_LIT, *W_BRANCH, *W_0BRANCH, *W_ABORTQ, *W_DOES;
    Word *W_DO, *W_LOOP, *W_PLOOP, *W_QDO, *W_LEAVE;
    Word *W_LOCAL, *W_LOCALS_ENTER, *W_LOCALS_EXIT, *W_LOCAL_STORE;

    /* locals: lazily-committed region (stable base), not an inline array */
    Region      locals_r;
    cell       *locals;
    int         lfbase;
    int         lfree;
    LocalsState cur_locals;

    /* input */
    char  inbuf[4096];
    char *inbuf_ptr;
    char  rline[4096];          /* buffer for READ-LINE */
    int   cond_skip, cond_depth;


    /* per-context scratch buffers (previously function-local statics) */
    char pn[NAME_LEN];              /* PARSE-NAME */
    char quote_ring[8][1024];       /* S" literal ring */
    int  quote_ri;
    char abq[1024];                 /* ABORT" */
    char dq[1024];                  /* ." */

    /* errors */
    jmp_buf abort_env;
    int     abort_active;
    int     booting;
    cell    err_code;
    char    err_msg[256];
    Region  xframes_r;
    XFrame *xframes;
    int     xsp;



};

typedef struct { const char *name; long val; } NameVal;

/* shared helpers defined in forth.c, used by the CODE parser (code.c) */
void  throw_error(struct forth *F, const char *msg);
Word *find(struct forth *F, const char *name);
void *dyn_open(struct forth *F, const char *name, int flags);
void *dyn_resolve(struct forth *F, const char *lib, const char *name);
void  dyn_close(struct forth *F, void *handle);

/* the CODE parser, defined in code.c and used by forth.c */
void  ir_put(struct forth *F, const char *tok);
void  ir_put_text(struct forth *F, const char *s, int len);
void *ir_compile(struct forth *F, int abi, int local, int keep);
/* register a generated code pointer as process-lifetime (freed at shutdown) */
void  ir_track_code(void *code);
extern const NameVal sljit_consts[];

/* context register and pool size, shared with the CODE parser */
#define CTX_REG SLJIT_S2
extern int g_nworkers;

/* runtime helpers callable from generated CODE (resolved via &forth_*) */
void  forth_raise(cell code, struct forth *F);
void  forth_divzero(struct forth *F);
void  forth_need(cell have, cell need, struct forth *F);
void  forth_room(cell have, cell need, struct forth *F);
void  forth_type(const char *s, int len, struct forth *F);
Word *forth_latest(struct forth *F);
void *forth_body(Word *w, struct forth *F);
Word *forth_find(const char *s, int len, struct forth *F);
Word *forth_link(Word *w, struct forth *F);
const char *forth_name(Word *w, struct forth *F);
int   forth_flags(Word *w, struct forth *F);
void *forth_code(Word *w, struct forth *F);
void *forth_native(Word *w, struct forth *F);
cell  forth_data(Word *w, struct forth *F);
int   forth_body_start(Word *w, struct forth *F);
int   forth_body_end(Word *w, struct forth *F);
cell  forth_immediate(Word *w, struct forth *F);
cell  forth_hidden(Word *w, struct forth *F);
cell  forth_colon_p(Word *w, struct forth *F);
cell  forth_native_p(Word *w, struct forth *F);
cell  forth_variable_p(Word *w, struct forth *F);

#endif /* FORTH_H */
