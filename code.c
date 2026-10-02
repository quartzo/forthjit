#include "forth.h"

/* ===================================================================== */
/*  native code: SLJIT LIR assembler                                     */
/* ===================================================================== */



typedef struct { const char *name; sljit_s32 op; } NameOp;

/* SLJIT constants exposed both as Forth words and as IR tokens */
#define CONST(name) {#name, name}
const NameVal sljit_consts[] = {
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
    CONST(SLJIT_ATOMIC_STORED), CONST(SLJIT_ATOMIC_NOT_STORED),
    CONST(SLJIT_ATOMIC_USE_LS), CONST(SLJIT_ATOMIC_USE_CAS), CONST(SLJIT_ATOMIC_TEST),
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
void ir_put_text(struct forth *F, const char *s, int len) {
    if (F->irlen + len + 2 >= IRBUF_SIZE) throw_error(F, "IR buffer full");
    if (F->irlen) F->irbuf[F->irlen++] = ' ';
    memcpy(F->irbuf + F->irlen, s, (size_t)len);
    F->irlen += len;
    F->irbuf[F->irlen] = 0;
}

void ir_put(struct forth *F, const char *tok) { ir_put_text(F, tok, (int)strlen(tok)); }

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
    char (*btok)[64] = F->inline_toks;
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
    char (*raw)[64] = F->ir_raw;
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

/* A memory offset: plain bytes, or a cell count with a `c` suffix.
   `1c` is sizeof(cell) bytes, `3c` is 3 cells; `c` alone is one cell. */
static sljit_sw parse_cell_off(struct forth *F, const char *s) {
    char *end;
    if (s[0] == 'c' && s[1] == 0) return (sljit_sw)sizeof(cell);
    long v = strtol(s, &end, 0);
    if (*end == 0) return (sljit_sw)v;
    if (end[0] == 'c' && end[1] == 0) return (sljit_sw)v * (sljit_sw)sizeof(cell);
    throw_error(F, "bad memory offset");
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
        *w = parse_cell_off(F, plus + 1);
        return;
    }
    char *minus = strchr(buf, '-');
    if (minus) {
        *minus = 0;
        *para = SLJIT_MEM1(parse_reg(F, buf));
        *w = -parse_cell_off(F, minus + 1);
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
            /* per-task fields live in the task context, addressed via CTX_REG */
            {
                sljit_sw coff = -1;
                if      (strcmp(sym, "dstack") == 0) coff = (sljit_sw)offsetof(struct forth, dstack);
                else if (strcmp(sym, "rstack") == 0) coff = (sljit_sw)offsetof(struct forth, rstack);
                else if (strcmp(sym, "sp") == 0)     coff = (sljit_sw)offsetof(struct forth, sp);
                else if (strcmp(sym, "rp") == 0)     coff = (sljit_sw)offsetof(struct forth, rp);
                else if (strcmp(sym, "ip") == 0)     coff = (sljit_sw)offsetof(struct forth, ip);
                else if (strcmp(sym, "curr") == 0)   coff = (sljit_sw)offsetof(struct forth, curr);
                if (coff >= 0) { *para = SLJIT_MEM1(CTX_REG); *w = coff; return; }
            }
            void *pv = NULL;
            if      (strcmp(sym, "mem") == 0)    pv = F->root->data.base;
            else if (strcmp(sym, "memtop") == 0) pv = &F->root->memtop;
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
            /* shared (root) code space and compiler state; these stay
               absolute because the code region and dictionary are shared
               with all task contexts */
            else if (strcmp(sym, "code") == 0) pv = &F->fcode;
            else if (strcmp(sym, "here") == 0) pv = &F->root->here;
            else if (strcmp(sym, "state") == 0) pv = &F->state;
            else if (strcmp(sym, "compiling") == 0) pv = &F->compiling;
            else if (strcmp(sym, "latest") == 0) pv = &F->root->latest;
            else if (strcmp(sym, "nwords") == 0) pv = &F->root->nwords;
            else if (strcmp(sym, "nworkers") == 0) pv = &g_nworkers;
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
                           CTX_REG, 0);
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
        sljit_emit_op1(F->jcomp, SLJIT_MOV_P, SLJIT_R1, 0, CTX_REG, 0);
        sljit_emit_icall(F->jcomp, SLJIT_CALL, SLJIT_ARGS2(P, P, P), SLJIT_IMM,
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
    if (strcmp(m, "spush") == 0) {
        /* push a register or immediate onto the data stack: [S1] = src; S1 += 8 */
        ensure_enter(F);
        sljit_s32 s; sljit_sw sw;
        parse_operand(F, ir_need(F), &s, &sw);
        if (s & SLJIT_MEM) throw_error(F, "spush: operand must be a register or immediate");
        sljit_emit_op1(F->jcomp, SLJIT_MOV, SLJIT_MEM1(SLJIT_S1), 0, s, sw);
        sljit_emit_op2(F->jcomp, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, (sljit_sw)sizeof(cell));
        return;
    }
    if (strcmp(m, "spop") == 0) {
        /* pop the top of the data stack into a register: S1 -= 8; dst = [S1] */
        ensure_enter(F);
        sljit_s32 d; sljit_sw dw;
        parse_operand(F, ir_need(F), &d, &dw);
        if ((d & SLJIT_MEM) || d == SLJIT_IMM) throw_error(F, "spop: operand must be a register");
        sljit_emit_op2(F->jcomp, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, -(sljit_sw)sizeof(cell));
        sljit_emit_op1(F->jcomp, SLJIT_MOV, d, dw, SLJIT_MEM1(SLJIT_S1), 0);
        return;
    }
    if (strcmp(m, "sdrop") == 0) {
        /* drop one cell (or #n cells) from the data stack: S1 -= 8 * n */
        ensure_enter(F);
        sljit_sw n = 1;
        if (F->irtok_pos < F->irtok_count && F->irtok[F->irtok_pos][0] == '#')
            n = (sljit_sw)strtol(ir_need(F) + 1, NULL, 0);
        if (n < 0) throw_error(F, "sdrop: count must be >= 0");
        if (n) sljit_emit_op2(F->jcomp, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, -(sljit_sw)sizeof(cell) * n);
        return;
    }

    /* atomics over the shared area.  SLJIT models atomics as a load/store
       pair (a CAS transaction): atomic.load starts it, atomic.store finishes
       it and sets the ATOMIC_STORED flag, retry on failure. */
    if (strcmp(m, "atomic.load") == 0) {
        ensure_enter(F);
        sljit_s32 aop = parse_opnum(F, ir_need(F));
        sljit_s32 d; sljit_sw dw;
        parse_operand(F, ir_need(F), &d, &dw);
        sljit_s32 mem; sljit_sw mw;
        parse_operand(F, ir_need(F), &mem, &mw);
        if ((d & SLJIT_MEM) || (mem & SLJIT_MEM)) throw_error(F, "atomic.load: operands must be registers");
        sljit_emit_atomic_load(F->jcomp, aop, d, mem);
        return;
    }
    if (strcmp(m, "atomic.store") == 0) {
        ensure_enter(F);
        sljit_s32 aop = parse_opnum(F, ir_need(F));
        sljit_s32 s; sljit_sw sw;
        parse_operand(F, ir_need(F), &s, &sw);
        sljit_s32 mem; sljit_sw mw;
        parse_operand(F, ir_need(F), &mem, &mw);
        sljit_s32 tmp; sljit_sw tw;
        parse_operand(F, ir_need(F), &tmp, &tw);
        if ((s & SLJIT_MEM) || (mem & SLJIT_MEM) || (tmp & SLJIT_MEM))
            throw_error(F, "atomic.store: operands must be registers");
        sljit_emit_atomic_store(F->jcomp, aop, s, mem, tmp);
        return;
    }

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
                       SLJIT_MEM1(CTX_REG), (sljit_sw)offsetof(struct forth, dstack));   /* r0 -= dstack */
        sljit_emit_op2(F->jcomp, SLJIT_LSHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 3);    /* /8 = depth */
        sljit_emit_op1(F->jcomp, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, n);                  /* r1 = need */
        sljit_emit_op1(F->jcomp, SLJIT_MOV_P, SLJIT_R2, 0, CTX_REG, 0);
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
void *ir_compile(struct forth *F, int abi, int local) {
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
        /* scratch-register arguments: the stack pointer arrives in R0 and the
           task context in R1, matching the C call ABI used by callw, so native
           words can call each other directly. */
        sljit_emit_enter(F->jcomp, 0, SLJIT_ARGS2(P, P_R, P_R),
                         4 | SLJIT_ENTER_FLOAT(6), 3, F->forth_local);
        sljit_emit_op1(F->jcomp, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_R0, 0);
        sljit_emit_op1(F->jcomp, SLJIT_MOV_P, CTX_REG, 0, SLJIT_R1, 0);
        F->cur_ret = SLJIT_ARG_TYPE_P;
        F->cur_argtypes = SLJIT_ARGS2(P, P, P);
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
