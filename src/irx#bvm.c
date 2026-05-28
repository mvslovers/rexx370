/* ------------------------------------------------------------------ */
/*  irx#bvm.c - REXX/370 Bytecode VM Loop (WP-BC-03/04)              */
/*                                                                    */
/*  irx_bc_execute() — Entry point.                                  */
/*                                                                    */
/*  WP-BC-02: stack slots, variable pool, arithmetic/comparison/     */
/*  logical/string opcodes, PUSH_LIT / LOAD / STORE / POP.          */
/*  WP-BC-03: control flow (JMP/JF/JT), SAY, DO loop ops            */
/*  (FORINIT/BYINIT/DECFOR stubs), ITERATE, LEAVE.                  */
/*  WP-BC-04: internal function CALL/RETURN, BIF dispatch via        */
/*  OP_CALL_BIF; label table pre-scan; proxy irx_parser for BIFs.   */
/*                                                                    */
/*  Stack discipline: SP always points to the next FREE slot.        */
/*    push: stack[sp++]                                              */
/*    pop:  stack[--sp]                                              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <string.h>

#include "irx.h"
#include "irxarith.h"
#include "irxbif.h"
#include "irxbops.h"
#include "irxbvm.h"
#include "irxexbl.h"
#include "irxfunc.h"
#include "irxlstr.h"
#include "irxpars.h"
#include "irxvpool.h"
#include "irxwkblk.h"

/* ================================================================== */
/*  Stack configuration                                               */
/* ================================================================== */

#define IRXBC_STACK_DEPTH 256

/* ================================================================== */
/*  DO-loop frame (one per nesting level; up to IRXBC_DO_DEPTH deep)  */
/* ================================================================== */

#define IRXBC_DO_DEPTH 16

struct bc_do_frame
{
    int32_t counter;
};

/* ================================================================== */
/*  Call frame (WP-BC-04)                                             */
/*                                                                    */
/*  One frame per active CALL.  Args are copied from the eval stack   */
/*  at call time and freed when the frame is popped by RETURN.        */
/* ================================================================== */

#define IRXBC_CALL_DEPTH 16

struct bc_call_frame
{
    const unsigned char *return_pc;
    int argc;
    int push_result; /* 1 = leave return value on eval stack (expr call) */
    Lstr args[IRX_MAX_ARGS];
    int arg_exists[IRX_MAX_ARGS];
    struct irx_vpool *prev_vpool; /* caller's vpool saved by OP_PROC */
    int has_isolated_scope;       /* 1 if OP_PROC created a child scope */
    /* Condition-trap state snapshot (WP-BC-07 PR B): callee changes
     * to SIGNAL ON/OFF are reverted on RETURN, per SC28-1883-0 §7.  */
    unsigned char cond_enabled_save;
    int cond_lsi_save[COND_COUNT];
};

/* ================================================================== */
/*  BIF registry access (WP-BC-04)                                   */
/* ================================================================== */

static const struct irx_bif_registry *
bvm_get_bif_registry(struct envblock *envblock)
{
    struct irx_wkblk_int *wk;

    if (envblock == NULL || envblock->envblock_userfield == NULL)
    {
        return NULL;
    }
    wk = (struct irx_wkblk_int *)envblock->envblock_userfield;
    return (const struct irx_bif_registry *)wk->wkbi_bif_registry;
}

/* ================================================================== */
/*  Read a little-endian u16 from the bytecode stream.               */
/* ================================================================== */

static int read_u16(const unsigned char *pc)
{
    return (int)pc[0] | ((int)pc[1] << 8);
}

/* Read a little-endian i16 (sign-extended) from the bytecode stream. */
static int read_i16(const unsigned char *pc)
{
    unsigned int u = (unsigned int)pc[0] | ((unsigned int)pc[1] << 8);
    if (u >= 0x8000u)
    {
        return (int)u - 0x10000;
    }
    return (int)u;
}

/* Map a COND_* bitmask constant to a 0-based index into cond_lsi[]. */
static int cond_bit_index(unsigned int cond)
{
    switch (cond)
    {
        case COND_ERROR:
            return 0;
        case COND_HALT:
            return 1;
        case COND_NOVALUE:
            return 2;
        case COND_NOTREADY:
            return 3;
        case COND_SYNTAX:
            return 4;
        case COND_FAILURE:
            return 5;
        default:
            return -1;
    }
}

/* ================================================================== */
/*  Retrieve a table entry string by index; place result in dst.     */
/*  Returns the string length, or -1 on bad index.                   */
/* ================================================================== */

static int get_entry(const char *table_base, int table_count, int idx,
                     const char **data_out)
{
    const char *entry;
    int len;

    if (idx < 0 || idx >= table_count)
    {
        *data_out = NULL;
        return -1;
    }
    entry = table_base + idx * IRXBC_ENTRY_SIZE;
    len = (int)(unsigned char)entry[0];
    *data_out = entry + 1;
    return len;
}

/* ================================================================== */
/*  Lstr helpers                                                      */
/* ================================================================== */

/* Copy a PLstr to an Lstr by value (used for call frame arg saving). */
static int lstr_copy(struct lstr_alloc *alloc, Lstr *dst, PLstr src)
{
    size_t len = Llen(src);

    Lzeroinit(dst);
    if (len == 0)
    {
        return LSTR_OK;
    }
    if (Lfx(alloc, dst, len) != LSTR_OK)
    {
        return LSTR_ERR_NOMEM;
    }
    memcpy(Lpstr(dst), Lpstr(src), len);
    Llen(dst) = len;
    return LSTR_OK;
}

/* Copy a raw byte buffer into a stack slot's Lstr, using the given
 * lstring allocator.  Resets type_cache. */
static int slot_set_buf(struct bc_stack_slot *slot,
                        struct lstr_alloc *alloc,
                        const char *buf, int len)
{
    int rc = Lfx(alloc, slot->str, (size_t)len);
    if (rc != LSTR_OK)
    {
        return rc;
    }
    if (len > 0)
    {
        memcpy(Lpstr(slot->str), buf, (size_t)len);
    }
    slot->str->len = (size_t)len;
    slot->type_cache = 0;
    slot->int_cache = 0;
    return LSTR_OK;
}

/* Copy one slot's Lstr into another. */
static int slot_copy(struct bc_stack_slot *dst,
                     struct lstr_alloc *alloc,
                     const struct bc_stack_slot *src)
{
    int rc = Lstrcpy(alloc, dst->str, src->str);
    if (rc != LSTR_OK)
    {
        return rc;
    }
    dst->type_cache = src->type_cache;
    dst->int_cache = src->int_cache;
    return LSTR_OK;
}

/* Write "0" or "1" into a slot. */
static int slot_set_bool(struct bc_stack_slot *slot,
                         struct lstr_alloc *alloc, int val)
{
    const char *s = val ? "1" : "0";
    return slot_set_buf(slot, alloc, s, 1);
}

/* ================================================================== */
/*  Boolean evaluation (REXX: any non-zero integer is truthy, but    */
/*  the spec requires "0" or "1" on the stack for boolean ops).      */
/*  Returns 0 or 1, or -1 on error (not a valid REXX number / bool). */
/* ================================================================== */

static int slot_to_bool(const struct bc_stack_slot *slot)
{
    PLstr s = slot->str;
    /* Accept only "0" or "1" for boolean operands per SC28-1883-0 §7. */
    if (Llen(s) == 1 && Lpstr(s) != NULL)
    {
        char c = (char)Lpstr(s)[0];
        if (c == '0')
        {
            return 0;
        }
        if (c == '1')
        {
            return 1;
        }
    }
    return -1; /* not a boolean */
}

/* ================================================================== */
/*  Integer fast-path helpers                                         */
/* ================================================================== */

/* If data[0..len-1] is a plain decimal integer (optional leading sign),
 * populate slot->type_cache and slot->int_cache.  Otherwise no-op. */
static void try_parse_int_cache(struct bc_stack_slot *slot,
                                const char *data, int len)
{
    const unsigned char *p = (const unsigned char *)data;
    int32_t v = 0;
    int neg = 0;
    int i = 0;

    if (len <= 0)
    {
        return;
    }
    if (p[i] == (unsigned char)'-')
    {
        neg = 1;
        i++;
    }
    else if (p[i] == (unsigned char)'+')
    {
        i++;
    }
    if (i >= len)
    {
        return; /* sign only */
    }
    for (; i < len; i++)
    {
        if (p[i] < (unsigned char)'0' || p[i] > (unsigned char)'9')
        {
            return; /* non-digit — not a plain integer */
        }
        if (v > 99999999)
        {
            return; /* would overflow int32 fast path */
        }
        v = v * 10 + (int32_t)(p[i] - (unsigned char)'0');
    }
    slot->type_cache = IRXBC_STACK_LINTEGER;
    slot->int_cache = neg ? -v : v;
}

/* Convert a stack slot to int32.  Uses cached value when available;
 * otherwise parses the string.  Returns 0 for non-integer strings. */
static int32_t slot_to_int32(const struct bc_stack_slot *slot)
{
    const unsigned char *p;
    size_t len;
    int32_t v = 0;
    int neg = 0;
    size_t i = 0;

    if (slot->type_cache == IRXBC_STACK_LINTEGER)
    {
        return slot->int_cache;
    }
    p = (const unsigned char *)Lpstr(slot->str);
    len = Llen(slot->str);
    if (p == NULL || len == 0)
    {
        return 0;
    }
    if (p[i] == (unsigned char)'-')
    {
        neg = 1;
        i++;
    }
    else if (p[i] == (unsigned char)'+')
    {
        i++;
    }
    for (; i < len; i++)
    {
        if (p[i] < (unsigned char)'0' || p[i] > (unsigned char)'9')
        {
            return 0; /* non-integer string — 0 iterations */
        }
        v = v * 10 + (int32_t)(p[i] - (unsigned char)'0');
    }
    return neg ? -v : v;
}

/* Format int32 into buf (no NUL terminator); return length. */
static int i32toa(int32_t v, char *buf)
{
    char tmp[21];
    unsigned int uv;
    int i = 0;
    int neg = 0;
    int len;
    int j;

    if (v < 0)
    {
        neg = 1;
        uv = (unsigned int)(-(v + 1)) + 1u;
    }
    else
    {
        uv = (unsigned int)v;
    }
    if (uv == 0)
    {
        tmp[i++] = '0';
    }
    else
    {
        while (uv > 0)
        {
            tmp[i++] = (char)('0' + (int)(uv % 10u));
            uv /= 10u;
        }
        if (neg)
        {
            tmp[i++] = '-';
        }
    }
    len = i;
    for (j = 0; i > 0; j++)
    {
        buf[j] = tmp[--i];
    }
    return len;
}

/* Attempt integer fast-path for binary arithmetic ops.
 * Returns 1 and writes result into dst on success; 0 to fall back. */
static int try_arith_fast(struct bc_stack_slot *dst,
                          const struct bc_stack_slot *a,
                          const struct bc_stack_slot *b,
                          unsigned char op,
                          struct lstr_alloc *alloc)
{
    int32_t va;
    int32_t vb;
    int32_t result;
    char buf[24];
    int len;

    if (a->type_cache != IRXBC_STACK_LINTEGER ||
        b->type_cache != IRXBC_STACK_LINTEGER)
    {
        return 0;
    }
    va = a->int_cache;
    vb = b->int_cache;

    switch (op)
    {
        case OP_ADD:
            result = va + vb;
            /* Overflow: same-sign operands with different-sign result */
            if (((va ^ vb) >= 0) && ((result ^ va) < 0))
            {
                return 0;
            }
            break;
        case OP_SUB:
            result = va - vb;
            if (((va ^ vb) < 0) && ((result ^ va) < 0))
            {
                return 0;
            }
            break;
        case OP_MUL:
            /* Avoid overflow: bail if either operand is large */
            if (va > 1000000000 || va < -1000000000 ||
                vb > 1000000000 || vb < -1000000000)
            {
                return 0;
            }
            result = va * vb;
            break;
        case OP_IDIV:
            if (vb == 0)
            {
                return 0; /* division by zero — let arith engine handle */
            }
            result = va / vb;
            break;
        case OP_MOD:
            if (vb == 0)
            {
                return 0;
            }
            result = va % vb;
            break;
        default:
            return 0;
    }

    len = i32toa(result, buf);
    if (slot_set_buf(dst, alloc, buf, len) != LSTR_OK)
    {
        return 0;
    }
    dst->type_cache = IRXBC_STACK_LINTEGER;
    dst->int_cache = result;
    return 1;
}

/* ================================================================== */
/*  PARSE sub-VM frame (WP-BC-05 PR A)                               */
/* ================================================================== */

struct bc_parse_frame
{
    Lstr source;
    int source_len;
    int scan;
    int upper;
    int cur_sym;                      /* sym_idx of pending PVAR, or -1 */
    int cur_dot;                      /* 1 if pending PDOT */
    int active;                       /* 1 while inside PARSE_BEGIN/PARSE_END */
    char cur_cmpd[IRXBC_STR_MAX + 1]; /* compound name for pending PVAR_STEM */
    int cur_cmpd_len;                 /* 0 = no compound pending */
};

/*
 * Assign a sub-string of pframe->source to the pending PVAR/PDOT
 * target and advance the scan pointer.
 *
 * seg_end  — exclusive end of the segment (characters beyond it belong
 *             to the next template item).
 * new_scan — where to place pframe->scan after this assignment
 *            (used by TR_LIT/ABS/REL/END; for TR_SPACE the caller
 *            passes seg_end=source_len but the function advances scan
 *            to the end of the matched word, not to seg_end).
 * last_real — 0 for TR_SPACE (one-word semantics),
 *             1 for all other triggers (leading-blank-then-rest).
 */
static int pframe_assign(struct bc_parse_frame *pframe,
                         struct irx_vpool *vpool,
                         struct lstr_alloc *alloc,
                         const char *sym_base, int n_syms,
                         int seg_end, int new_scan, int last_real)
{
    const char *src =
        Lpstr(&pframe->source) ? (const char *)Lpstr(&pframe->source) : "";
    int scan = pframe->scan;
    int content_start, value_end;
    int vrc;
    Lstr val;

    if (seg_end > pframe->source_len)
    {
        seg_end = pframe->source_len;
    }
    if (seg_end < scan)
    {
        seg_end = scan;
    }

    /* Skip leading whitespace */
    content_start = scan;
    while (content_start < seg_end &&
           isspace((unsigned char)src[content_start]))
    {
        content_start++;
    }

    if (last_real)
    {
        /* TR_LIT/ABS/REL/END: take from content_start to seg_end */
        value_end = seg_end;
        pframe->scan = new_scan;
    }
    else
    {
        /* TR_SPACE: take one word (non-space characters) */
        value_end = content_start;
        while (value_end < seg_end &&
               !isspace((unsigned char)src[value_end]))
        {
            value_end++;
        }
        pframe->scan = value_end;
    }

    /* Dot placeholder — consume the segment but assign nothing */
    if (pframe->cur_dot)
    {
        pframe->cur_dot = 0;
        pframe->cur_sym = -1;
        pframe->cur_cmpd_len = 0;
        return IRXBC_OK;
    }

    if (pframe->cur_sym >= 0)
    {
        const char *name_data;
        int name_len =
            get_entry(sym_base, n_syms, pframe->cur_sym, &name_data);
        if (name_len < 0)
        {
            pframe->cur_sym = -1;
            return IRXBC_ERR_OPCODE;
        }
        Lzeroinit(&val);
        if (value_end > content_start)
        {
            size_t vlen = (size_t)(value_end - content_start);
            if (Lfx(alloc, &val, vlen) != LSTR_OK)
            {
                return IRXBC_ERR_STOR;
            }
            memcpy(Lpstr(&val), src + content_start, vlen);
            Llen(&val) = vlen;
        }
        vrc = vpool_set_buf(vpool, name_data, name_len, &val, 0, 0);
        Lfree(alloc, &val);
        if (vrc != VPOOL_OK)
        {
            pframe->cur_sym = -1;
            return IRXBC_ERR_STOR;
        }
    }
    else if (pframe->cur_cmpd_len > 0)
    {
        /* Compound variable target set by OP_PVAR_STEM */
        Lzeroinit(&val);
        if (value_end > content_start)
        {
            size_t vlen = (size_t)(value_end - content_start);
            if (Lfx(alloc, &val, vlen) != LSTR_OK)
            {
                pframe->cur_cmpd_len = 0;
                return IRXBC_ERR_STOR;
            }
            memcpy(Lpstr(&val), src + content_start, vlen);
            Llen(&val) = vlen;
        }
        vrc = vpool_set_buf(vpool, pframe->cur_cmpd, pframe->cur_cmpd_len,
                            &val, 0, 0);
        Lfree(alloc, &val);
        if (vrc != VPOOL_OK)
        {
            pframe->cur_cmpd_len = 0;
            return IRXBC_ERR_STOR;
        }
    }
    pframe->cur_sym = -1;
    pframe->cur_dot = 0;
    pframe->cur_cmpd_len = 0;
    return IRXBC_OK;
}

/* ================================================================== */
/*  irx_bc_execute                                                    */
/* ================================================================== */

int irx_bc_execute(struct envblock *envblock,
                   struct irx_bc_execblk *bc,
                   const char *args, int args_len,
                   int *rc_out)
{
    const unsigned char *pc;
    const unsigned char *code_base;
    unsigned char op;
    struct lstr_alloc *alloc = NULL;
    struct irx_vpool *vpool = NULL;
    struct bc_stack_slot *stack = NULL;
    struct bc_do_frame *frames = NULL;
    struct bc_call_frame *call_frames = NULL;
    struct irx_parser *proxy_parser = NULL;
    int *label_pc = NULL;
    Lstr *lstrs = NULL;
    void *stack_mem = NULL;
    void *lstr_mem = NULL;
    void *frames_mem = NULL;
    void *call_frame_mem = NULL;
    void *proxy_parser_mem = NULL;
    void *label_pc_mem = NULL;
    int32_t *const_type_cache = NULL;
    int32_t *const_int_cache = NULL;
    void *const_cache_mem = NULL;
    int sp = 0; /* next free slot */
    int call_sp = 0;
    int n_consts;
    int n_syms;
    const char *const_base;
    const char *sym_base;
    struct bc_parse_frame pframe;
    int vm_rc = IRXBC_OK;
    int i;
    /* Condition-trap state (WP-BC-07 PR B) */
    unsigned char cond_enabled = 0; /* active COND_* bitmask */
    int cond_lsi[COND_COUNT];       /* handler sym_idx per cond; -1 = none */
    int trap_target = -1;           /* target pc offset; set before goto trap_jump */
    int fired_cond = 0;             /* COND_* bit of the firing condition */

    memset(&pframe, 0, sizeof(pframe));
    pframe.cur_sym = -1;

    if (bc == NULL)
    {
        return IRXBC_ERR_OPCODE;
    }

    /* --- Allocator -------------------------------------------------- */
    alloc = irx_lstr_init(envblock);
    if (alloc == NULL)
    {
        return IRXBC_ERR_STOR;
    }

    /* --- Stack: bc_stack_slot array ---------------------------------- */
    if (irxstor(RXSMGET,
                IRXBC_STACK_DEPTH * (int)sizeof(struct bc_stack_slot),
                &stack_mem, envblock) != 0)
    {
        vm_rc = IRXBC_ERR_STOR;
        goto done;
    }
    memset(stack_mem, 0, IRXBC_STACK_DEPTH * sizeof(struct bc_stack_slot));
    stack = (struct bc_stack_slot *)stack_mem;

    /* --- Stack: backing Lstr array ----------------------------------- */
    if (irxstor(RXSMGET,
                IRXBC_STACK_DEPTH * (int)sizeof(Lstr),
                &lstr_mem, envblock) != 0)
    {
        vm_rc = IRXBC_ERR_STOR;
        goto done;
    }
    memset(lstr_mem, 0, IRXBC_STACK_DEPTH * sizeof(Lstr));
    lstrs = (Lstr *)lstr_mem;

    for (i = 0; i < IRXBC_STACK_DEPTH; i++)
    {
        stack[i].str = &lstrs[i];
    }

    /* --- DO-loop frame array ----------------------------------------- */
    if (irxstor(RXSMGET,
                IRXBC_DO_DEPTH * (int)sizeof(struct bc_do_frame),
                &frames_mem, envblock) != 0)
    {
        vm_rc = IRXBC_ERR_STOR;
        goto done;
    }
    memset(frames_mem, 0, IRXBC_DO_DEPTH * sizeof(struct bc_do_frame));
    frames = (struct bc_do_frame *)frames_mem;

    /* --- Variable pool ----------------------------------------------- */
    vpool = vpool_create(alloc, NULL);
    if (vpool == NULL)
    {
        vm_rc = IRXBC_ERR_STOR;
        goto done;
    }

    /* --- Fetch constants / symbol table pointers --------------------- */
    const_base = IRXBC_CONST_TBL(bc);
    sym_base = IRXBC_SYM_TBL(bc);
    n_consts = (int)bc->const_count;
    n_syms = (int)bc->symbol_count;
    code_base = IRXBC_CODE(bc);

    /* --- Call frame array (WP-BC-04) --------------------------------- */
    if (irxstor(RXSMGET,
                IRXBC_CALL_DEPTH * (int)sizeof(struct bc_call_frame),
                &call_frame_mem, envblock) != 0)
    {
        vm_rc = IRXBC_ERR_STOR;
        goto done;
    }
    memset(call_frame_mem, 0,
           IRXBC_CALL_DEPTH * sizeof(struct bc_call_frame));
    call_frames = (struct bc_call_frame *)call_frame_mem;

    /* --- Label PC table (WP-BC-04) — indexed by sym_idx -------------- */
    if (n_syms > 0)
    {
        if (irxstor(RXSMGET, n_syms * (int)sizeof(int),
                    &label_pc_mem, envblock) != 0)
        {
            vm_rc = IRXBC_ERR_STOR;
            goto done;
        }
        label_pc = (int *)label_pc_mem;
        for (i = 0; i < n_syms; i++)
        {
            label_pc[i] = -1;
        }
        /* Pre-scan bytecode for OP_LABEL instructions */
        {
            int scan_pos = 0;
            int code_len = (int)bc->code_length;

            while (scan_pos < code_len)
            {
                unsigned char scan_op = code_base[scan_pos];
                int opsz = OP_SIZE(scan_op);

                if (scan_op == OP_LABEL && scan_pos + 2 < code_len)
                {
                    int lsi = (int)code_base[scan_pos + 1] |
                              ((int)code_base[scan_pos + 2] << 8);
                    if (lsi >= 0 && lsi < n_syms)
                    {
                        label_pc[lsi] = scan_pos + 3;
                    }
                }
                scan_pos += opsz;
            }
        }
    }

    /* --- OC-07: pre-compute integer cache for all constants (WP-BC-06) */
    if (n_consts > 0)
    {
        if (irxstor(RXSMGET, 2 * n_consts * (int)sizeof(int32_t),
                    &const_cache_mem, envblock) != 0)
        {
            vm_rc = IRXBC_ERR_STOR;
            goto done;
        }
        memset(const_cache_mem, 0,
               (size_t)(2 * n_consts) * sizeof(int32_t));
        const_type_cache = (int32_t *)const_cache_mem;
        const_int_cache = const_type_cache + n_consts;
        for (i = 0; i < n_consts; i++)
        {
            const char *cdata;
            int clen = get_entry(const_base, n_consts, i, &cdata);
            if (clen > 0)
            {
                struct bc_stack_slot s;
                s.str = NULL;
                s.type_cache = 0;
                s.int_cache = 0;
                try_parse_int_cache(&s, cdata, clen);
                const_type_cache[i] = s.type_cache;
                const_int_cache[i] = s.int_cache;
            }
        }
    }

    /* --- Proxy parser for BIF dispatch (WP-BC-04) -------------------- */
    if (irxstor(RXSMGET, (int)sizeof(struct irx_parser),
                &proxy_parser_mem, envblock) != 0)
    {
        vm_rc = IRXBC_ERR_STOR;
        goto done;
    }
    proxy_parser = (struct irx_parser *)proxy_parser_mem;
    memset(proxy_parser, 0, sizeof(struct irx_parser));
    proxy_parser->alloc = alloc;
    proxy_parser->envblock = envblock;
    proxy_parser->vpool = vpool;

    /* Top-level argument string for PARSE ARG / ARG() at program entry. */
    if (args != NULL && args_len > 0)
    {
        Lstr *la;
        int *le;
        la = (Lstr *)alloc->alloc(
            (size_t)IRX_MAX_ARGS * sizeof(Lstr), alloc->ctx);
        le = (int *)alloc->alloc(
            (size_t)IRX_MAX_ARGS * sizeof(int), alloc->ctx);
        if (la == NULL || le == NULL)
        {
            if (la != NULL)
            {
                alloc->dealloc(la, (size_t)IRX_MAX_ARGS * sizeof(Lstr),
                               alloc->ctx);
            }
            if (le != NULL)
            {
                alloc->dealloc(le, (size_t)IRX_MAX_ARGS * sizeof(int),
                               alloc->ctx);
            }
            vm_rc = IRXBC_ERR_STOR;
            goto done;
        }
        memset(la, 0, (size_t)IRX_MAX_ARGS * sizeof(Lstr));
        memset(le, 0, (size_t)IRX_MAX_ARGS * sizeof(int));
        if (Lfx(alloc, &la[0], (size_t)args_len) != LSTR_OK)
        {
            alloc->dealloc(la, (size_t)IRX_MAX_ARGS * sizeof(Lstr),
                           alloc->ctx);
            alloc->dealloc(le, (size_t)IRX_MAX_ARGS * sizeof(int),
                           alloc->ctx);
            vm_rc = IRXBC_ERR_STOR;
            goto done;
        }
        memcpy(la[0].pstr, args, (size_t)args_len);
        la[0].len = (size_t)args_len;
        la[0].type = LSTRING_TY;
        le[0] = 1;
        proxy_parser->call_args = la;
        proxy_parser->call_arg_exists = le;
        proxy_parser->call_argc = 1;
    }

    /* --- Init condition-trap handler table (WP-BC-07 PR B) ------------ */
    for (i = 0; i < COND_COUNT; i++)
    {
        cond_lsi[i] = -1;
    }

    {
        pc = IRXBC_ENTRY(bc);

        /* ---- VM loop ------------------------------------------------ */
        for (;;)
        {
            op = *pc++;

            switch (op)
            {
                /* ---- Phase 1 ---------------------------------------- */
                case OP_NOP:
                    break;

                case OP_NEWCLAUSE:
                    break;

                case OP_EXIT:
                    if (rc_out != NULL)
                    {
                        *rc_out = 0;
                    }
                    goto done;

                case OP_EXIT_RC:
                {
                    int rcval = 0;

                    if (sp >= 1)
                    {
                        PLstr s = stack[sp - 1].str;
                        const unsigned char *p = Lpstr(s);
                        size_t len = Llen(s);
                        size_t ki;
                        int neg = 0;

                        if (p != NULL && len > 0)
                        {
                            ki = 0;
                            if (p[ki] == (unsigned char)'-')
                            {
                                neg = 1;
                                ki++;
                            }
                            else if (p[ki] == (unsigned char)'+')
                            {
                                ki++;
                            }
                            while (ki < len &&
                                   p[ki] >= (unsigned char)'0' &&
                                   p[ki] <= (unsigned char)'9')
                            {
                                rcval = rcval * 10 +
                                        (int)(p[ki] - (unsigned char)'0');
                                ki++;
                            }
                            if (neg)
                            {
                                rcval = -rcval;
                            }
                        }
                        sp--;
                    }
                    if (rc_out != NULL)
                    {
                        *rc_out = rcval;
                    }
                    goto done;
                }

                /* ---- Stack ops -------------------------------------- */
                case OP_PUSH_LIT:
                {
                    int idx = read_u16(pc);
                    const char *data;
                    int len;

                    pc += 2;
                    if (sp >= IRXBC_STACK_DEPTH)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    len = get_entry(const_base, n_consts, idx, &data);
                    if (len < 0)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    if (slot_set_buf(&stack[sp], alloc,
                                     data, len) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    if (const_type_cache != NULL)
                    {
                        stack[sp].type_cache = const_type_cache[idx];
                        stack[sp].int_cache = const_int_cache[idx];
                    }
                    else
                    {
                        try_parse_int_cache(&stack[sp], data, len);
                    }
                    sp++;
                    break;
                }

                case OP_POP:
                {
                    int n = (int)*pc++;
                    if (sp < n)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    sp -= n;
                    break;
                }

                case OP_DUP:
                {
                    if (sp < 1 || sp >= IRXBC_STACK_DEPTH)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    if (slot_copy(&stack[sp], alloc,
                                  &stack[sp - 1]) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    sp++;
                    break;
                }

                /* ---- Variable ops ---------------------------------- */
                case OP_LOAD:
                {
                    int idx = read_u16(pc);
                    const char *name_data;
                    int name_len;
                    int vrc;

                    pc += 2;
                    if (sp >= IRXBC_STACK_DEPTH)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    name_len =
                        get_entry(sym_base, n_syms, idx, &name_data);
                    if (name_len < 0)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    stack[sp].type_cache = 0;
                    stack[sp].int_cache = 0;
                    vrc = vpool_get_buf(vpool, name_data, name_len,
                                        stack[sp].str,
                                        &stack[sp].type_cache,
                                        &stack[sp].int_cache);
                    if (vrc == VPOOL_NOT_FOUND)
                    {
                        /* NOVALUE trap: fire if SIGNAL ON NOVALUE is active */
                        if (cond_enabled & COND_NOVALUE)
                        {
                            int ci_nv = cond_bit_index(COND_NOVALUE);
                            if (ci_nv >= 0 && cond_lsi[ci_nv] >= 0 &&
                                label_pc != NULL &&
                                label_pc[cond_lsi[ci_nv]] >= 0)
                            {
                                char nv_desc[IRXBC_STR_MAX + 1];
                                int nd = name_len < IRXBC_STR_MAX
                                             ? name_len
                                             : IRXBC_STR_MAX;
                                memcpy(nv_desc, name_data, (size_t)nd);
                                nv_desc[nd] = '\0';
                                irx_cond_raise(envblock, 0, 0, nv_desc);
                                fired_cond = COND_NOVALUE;
                                trap_target = label_pc[cond_lsi[ci_nv]];
                                goto trap_jump;
                            }
                        }
                        /* Normal NOVALUE: value is the variable name itself */
                        if (slot_set_buf(&stack[sp], alloc,
                                         name_data, name_len) != LSTR_OK)
                        {
                            vm_rc = IRXBC_ERR_STOR;
                            goto done;
                        }
                    }
                    else if (vrc != VPOOL_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    sp++;
                    break;
                }

                case OP_STORE:
                {
                    int idx = read_u16(pc);
                    const char *name_data;
                    int name_len;
                    int vrc;

                    pc += 2;
                    if (sp < 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    name_len =
                        get_entry(sym_base, n_syms, idx, &name_data);
                    if (name_len < 0)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    sp--;
                    vrc = vpool_set_buf(vpool, name_data, name_len,
                                        stack[sp].str,
                                        stack[sp].type_cache,
                                        stack[sp].int_cache);
                    if (vrc != VPOOL_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    break;
                }

                case OP_DROP:
                {
                    int idx = read_u16(pc);
                    const char *name_data;
                    int name_len;

                    pc += 2;
                    name_len =
                        get_entry(sym_base, n_syms, idx, &name_data);
                    if (name_len < 0)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    vpool_drop_buf(vpool, name_data, name_len);
                    break;
                }

                /* ---- Compound variable ops (WP-BC-05 PR C) --------- */
                case OP_LOAD_STEM:
                {
                    int stem_idx = read_u16(pc);
                    int tail_cnt = (int)pc[2];
                    const char *stem_data;
                    int stem_len;
                    char name_buf[IRXBC_STR_MAX + 1];
                    int name_pos;
                    int i;
                    int vrc;

                    pc += 3;

                    if (sp < tail_cnt || sp >= IRXBC_STACK_DEPTH)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    stem_len =
                        get_entry(sym_base, n_syms, stem_idx, &stem_data);
                    if (stem_len < 0 || stem_len > IRXBC_STR_MAX)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    /* Build compound name: stem + tails (uppercased). */
                    memcpy(name_buf, stem_data, (size_t)stem_len);
                    name_pos = stem_len;
                    for (i = 0; i < tail_cnt; i++)
                    {
                        const char *tv;
                        int tl;
                        int j;

                        tv = (const char *)Lpstr(stack[sp - tail_cnt + i].str);
                        tl = (int)Llen(stack[sp - tail_cnt + i].str);
                        if (i > 0)
                        {
                            if (name_pos >= IRXBC_STR_MAX)
                            {
                                vm_rc = IRXBC_ERR_STOR;
                                goto done;
                            }
                            name_buf[name_pos++] = '.';
                        }
                        for (j = 0; j < tl; j++)
                        {
                            if (name_pos >= IRXBC_STR_MAX)
                            {
                                vm_rc = IRXBC_ERR_STOR;
                                goto done;
                            }
                            name_buf[name_pos++] =
                                (char)toupper((unsigned char)tv[j]);
                        }
                    }
                    name_buf[name_pos] = '\0';
                    /* Pop tails, then push result. */
                    sp -= tail_cnt;
                    stack[sp].type_cache = 0;
                    stack[sp].int_cache = 0;
                    vrc = vpool_get_buf(vpool, name_buf, name_pos,
                                        stack[sp].str,
                                        &stack[sp].type_cache,
                                        &stack[sp].int_cache);
                    if (vrc == VPOOL_NOT_FOUND)
                    {
                        /* NOVALUE trap: fire if SIGNAL ON NOVALUE is active */
                        if (cond_enabled & COND_NOVALUE)
                        {
                            int ci_nv = cond_bit_index(COND_NOVALUE);
                            if (ci_nv >= 0 && cond_lsi[ci_nv] >= 0 &&
                                label_pc != NULL &&
                                label_pc[cond_lsi[ci_nv]] >= 0)
                            {
                                /* name_buf is null-terminated at name_pos */
                                irx_cond_raise(envblock, 0, 0, name_buf);
                                fired_cond = COND_NOVALUE;
                                trap_target = label_pc[cond_lsi[ci_nv]];
                                goto trap_jump;
                            }
                        }
                        /* Normal NOVALUE: value is the compound name itself. */
                        if (slot_set_buf(&stack[sp], alloc,
                                         name_buf, name_pos) != LSTR_OK)
                        {
                            vm_rc = IRXBC_ERR_STOR;
                            goto done;
                        }
                    }
                    else if (vrc != VPOOL_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    sp++;
                    break;
                }

                case OP_STORE_STEM:
                {
                    int stem_idx = read_u16(pc);
                    int tail_cnt = (int)pc[2];
                    const char *stem_data;
                    int stem_len;
                    char name_buf[IRXBC_STR_MAX + 1];
                    int name_pos;
                    int i;
                    int vrc;

                    pc += 3;

                    if (sp < tail_cnt + 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    stem_len =
                        get_entry(sym_base, n_syms, stem_idx, &stem_data);
                    if (stem_len < 0 || stem_len > IRXBC_STR_MAX)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    /* Stack: ..., tail[0], ..., tail[n-1], value
                     * Value is at sp-1; tails at sp-tail_cnt-1..sp-2. */
                    memcpy(name_buf, stem_data, (size_t)stem_len);
                    name_pos = stem_len;
                    for (i = 0; i < tail_cnt; i++)
                    {
                        const char *tv;
                        int tl;
                        int j;

                        tv = (const char *)Lpstr(
                            stack[sp - tail_cnt - 1 + i].str);
                        tl = (int)Llen(stack[sp - tail_cnt - 1 + i].str);
                        if (i > 0)
                        {
                            if (name_pos >= IRXBC_STR_MAX)
                            {
                                vm_rc = IRXBC_ERR_STOR;
                                goto done;
                            }
                            name_buf[name_pos++] = '.';
                        }
                        for (j = 0; j < tl; j++)
                        {
                            if (name_pos >= IRXBC_STR_MAX)
                            {
                                vm_rc = IRXBC_ERR_STOR;
                                goto done;
                            }
                            name_buf[name_pos++] =
                                (char)toupper((unsigned char)tv[j]);
                        }
                    }
                    name_buf[name_pos] = '\0';
                    /* Pop value (now at stack[sp-1]). */
                    sp--;
                    vrc = vpool_set_buf(vpool, name_buf, name_pos,
                                        stack[sp].str,
                                        stack[sp].type_cache,
                                        stack[sp].int_cache);
                    sp -= tail_cnt; /* pop tails */
                    if (vrc != VPOOL_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    break;
                }

                case OP_DROP_STEM:
                {
                    int stem_idx = read_u16(pc);
                    int tail_cnt = (int)pc[2];
                    const char *stem_data;
                    int stem_len;

                    pc += 3;

                    stem_len =
                        get_entry(sym_base, n_syms, stem_idx, &stem_data);
                    if (stem_len < 0 || stem_len > IRXBC_STR_MAX)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    if (tail_cnt == 0)
                    {
                        /* DROP STEM. — remove all entries with this prefix. */
                        vpool_drop_stem_all(vpool, stem_data, stem_len);
                    }
                    else
                    {
                        char name_buf[IRXBC_STR_MAX + 1];
                        int name_pos;
                        int i;

                        if (sp < tail_cnt)
                        {
                            vm_rc = IRXBC_ERR_STACK;
                            goto done;
                        }
                        memcpy(name_buf, stem_data, (size_t)stem_len);
                        name_pos = stem_len;
                        for (i = 0; i < tail_cnt; i++)
                        {
                            const char *tv;
                            int tl;
                            int j;

                            tv = (const char *)Lpstr(
                                stack[sp - tail_cnt + i].str);
                            tl = (int)Llen(stack[sp - tail_cnt + i].str);
                            if (i > 0)
                            {
                                if (name_pos >= IRXBC_STR_MAX)
                                {
                                    vm_rc = IRXBC_ERR_STOR;
                                    goto done;
                                }
                                name_buf[name_pos++] = '.';
                            }
                            for (j = 0; j < tl; j++)
                            {
                                if (name_pos >= IRXBC_STR_MAX)
                                {
                                    vm_rc = IRXBC_ERR_STOR;
                                    goto done;
                                }
                                name_buf[name_pos++] =
                                    (char)toupper((unsigned char)tv[j]);
                            }
                        }
                        name_buf[name_pos] = '\0';
                        sp -= tail_cnt;
                        vpool_drop_buf(vpool, name_buf, name_pos);
                    }
                    break;
                }

                /* ---- Arithmetic ------------------------------------ */
                case OP_ADD:
                case OP_SUB:
                case OP_MUL:
                case OP_DIV:
                case OP_IDIV:
                case OP_MOD:
                case OP_POW:
                {
                    static const enum irx_arith_opcode arith_map[] = {
                        ARITH_ADD,    /* OP_ADD  0x30 */
                        ARITH_SUB,    /* OP_SUB  0x31 */
                        ARITH_MUL,    /* OP_MUL  0x32 */
                        ARITH_DIV,    /* OP_DIV  0x33 */
                        ARITH_INTDIV, /* OP_IDIV 0x34 */
                        ARITH_MOD,    /* OP_MOD  0x35 */
                        ARITH_POWER   /* OP_POW  0x36 */
                    };
                    enum irx_arith_opcode aop =
                        arith_map[op - OP_ADD];
                    int arc;

                    if (sp < 2)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    /* Integer fast-path: skip REXX arithmetic for simple ops */
                    if (op != OP_DIV && op != OP_POW &&
                        try_arith_fast(&stack[sp - 2], &stack[sp - 2],
                                       &stack[sp - 1], op, alloc))
                    {
                        sp--;
                        break;
                    }
                    /* stack[sp-2] = a, stack[sp-1] = b */
                    arc = irx_arith_op(envblock,
                                       stack[sp - 2].str,
                                       stack[sp - 1].str,
                                       aop,
                                       stack[sp - 2].str);
                    sp--;
                    if (arc != IRXPARS_OK)
                    {
                        vm_rc = IRXBC_ERR_ARITH;
                        goto check_syntax_trap;
                    }
                    stack[sp - 1].type_cache = 0;
                    break;
                }

                case OP_NEG:
                {
                    int arc;

                    if (sp < 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    arc = irx_arith_op(envblock,
                                       stack[sp - 1].str, NULL,
                                       ARITH_NEG,
                                       stack[sp - 1].str);
                    if (arc != IRXPARS_OK)
                    {
                        vm_rc = IRXBC_ERR_ARITH;
                        goto check_syntax_trap;
                    }
                    stack[sp - 1].type_cache = 0;
                    break;
                }

                /* ---- Comparison ------------------------------------ */
                case OP_EQ:
                case OP_NE:
                case OP_LT:
                case OP_LE:
                case OP_GT:
                case OP_GE:
                {
                    int cmp;
                    int result;
                    int arc;

                    if (sp < 2)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    arc = irx_arith_compare(envblock,
                                            stack[sp - 2].str,
                                            stack[sp - 1].str,
                                            &cmp);
                    sp--;
                    if (arc != IRXPARS_OK)
                    {
                        /* Fall back to string comparison */
                        int slen_a = (int)Llen(stack[sp - 1].str);
                        int slen_b = (int)Llen(stack[sp].str);
                        int min_len =
                            slen_a < slen_b ? slen_a : slen_b;
                        int scmp = (Lpstr(stack[sp - 1].str) &&
                                    Lpstr(stack[sp].str))
                                       ? memcmp(Lpstr(stack[sp - 1].str),
                                                Lpstr(stack[sp].str),
                                                (size_t)min_len)
                                       : 0;
                        if (scmp == 0)
                        {
                            cmp = slen_a - slen_b;
                        }
                        else
                        {
                            cmp = scmp < 0 ? -1 : 1;
                        }
                    }
                    switch (op)
                    {
                        case OP_EQ:
                            result = (cmp == 0);
                            break;
                        case OP_NE:
                            result = (cmp != 0);
                            break;
                        case OP_LT:
                            result = (cmp < 0);
                            break;
                        case OP_LE:
                            result = (cmp <= 0);
                            break;
                        case OP_GT:
                            result = (cmp > 0);
                            break;
                        case OP_GE:
                            result = (cmp >= 0);
                            break;
                        default:
                            result = 0;
                            break;
                    }
                    if (slot_set_bool(&stack[sp - 1], alloc,
                                      result) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    break;
                }

                case OP_DEQ:
                case OP_DNE:
                case OP_DLT:
                case OP_DLE:
                case OP_DGT:
                case OP_DGE:
                {
                    /* Strict comparison: no numeric coercion, no FUZZ. */
                    int la, lb, min_len, cmp;
                    int result;

                    if (sp < 2)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    la = (int)Llen(stack[sp - 2].str);
                    lb = (int)Llen(stack[sp - 1].str);
                    min_len = la < lb ? la : lb;
                    if (Lpstr(stack[sp - 2].str) &&
                        Lpstr(stack[sp - 1].str))
                    {
                        cmp = memcmp(Lpstr(stack[sp - 2].str),
                                     Lpstr(stack[sp - 1].str),
                                     (size_t)min_len);
                    }
                    else
                    {
                        cmp = 0;
                    }
                    if (cmp == 0)
                    {
                        cmp = la - lb;
                    }
                    sp--;
                    switch (op)
                    {
                        case OP_DEQ:
                            result = (cmp == 0);
                            break;
                        case OP_DNE:
                            result = (cmp != 0);
                            break;
                        case OP_DLT:
                            result = (cmp < 0);
                            break;
                        case OP_DLE:
                            result = (cmp <= 0);
                            break;
                        case OP_DGT:
                            result = (cmp > 0);
                            break;
                        case OP_DGE:
                            result = (cmp >= 0);
                            break;
                        default:
                            result = 0;
                            break;
                    }
                    if (slot_set_bool(&stack[sp - 1], alloc,
                                      result) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    break;
                }

                /* ---- Logical ops ----------------------------------- */
                case OP_AND:
                {
                    int a, b;

                    if (sp < 2)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    a = slot_to_bool(&stack[sp - 2]);
                    b = slot_to_bool(&stack[sp - 1]);
                    if (a < 0 || b < 0)
                    {
                        vm_rc = IRXBC_ERR_ARITH;
                        goto check_syntax_trap;
                    }
                    sp--;
                    if (slot_set_bool(&stack[sp - 1], alloc,
                                      a & b) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    break;
                }

                case OP_OR:
                {
                    int a, b;

                    if (sp < 2)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    a = slot_to_bool(&stack[sp - 2]);
                    b = slot_to_bool(&stack[sp - 1]);
                    if (a < 0 || b < 0)
                    {
                        vm_rc = IRXBC_ERR_ARITH;
                        goto check_syntax_trap;
                    }
                    sp--;
                    if (slot_set_bool(&stack[sp - 1], alloc,
                                      a | b) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    break;
                }

                case OP_XOR:
                {
                    int a, b;

                    if (sp < 2)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    a = slot_to_bool(&stack[sp - 2]);
                    b = slot_to_bool(&stack[sp - 1]);
                    if (a < 0 || b < 0)
                    {
                        vm_rc = IRXBC_ERR_ARITH;
                        goto check_syntax_trap;
                    }
                    sp--;
                    if (slot_set_bool(&stack[sp - 1], alloc,
                                      a ^ b) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    break;
                }

                case OP_NOT:
                {
                    int a;

                    if (sp < 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    a = slot_to_bool(&stack[sp - 1]);
                    if (a < 0)
                    {
                        vm_rc = IRXBC_ERR_ARITH;
                        goto check_syntax_trap;
                    }
                    if (slot_set_bool(&stack[sp - 1], alloc,
                                      !a) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    break;
                }

                /* ---- String ops ------------------------------------ */
                case OP_CONCAT:
                {
                    if (sp < 2)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    /* Append stack[sp-1] to stack[sp-2] in-place. */
                    if (Lstrcat(alloc, stack[sp - 2].str,
                                stack[sp - 1].str) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    sp--;
                    stack[sp - 1].type_cache = 0;
                    break;
                }

                case OP_BCONCAT:
                {
                    if (sp < 2)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    /* Insert one blank then append. */
                    if (Lcat(alloc, stack[sp - 2].str, " ") != LSTR_OK ||
                        Lstrcat(alloc, stack[sp - 2].str,
                                stack[sp - 1].str) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    sp--;
                    stack[sp - 1].type_cache = 0;
                    break;
                }

                /* ---- Control flow (WP-BC-03) ----------------------- */
                case OP_JMP:
                case OP_ITERATE:
                case OP_LEAVE:
                {
                    int off = read_i16(pc);
                    pc += 2;
                    pc += off;
                    break;
                }

                case OP_JF:
                {
                    int off = read_i16(pc);
                    int bval;
                    pc += 2;
                    if (sp < 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    bval = slot_to_bool(&stack[--sp]);
                    if (bval < 0)
                    {
                        vm_rc = IRXBC_ERR_ARITH;
                        goto check_syntax_trap;
                    }
                    if (!bval)
                    {
                        pc += off;
                    }
                    break;
                }

                case OP_JT:
                {
                    int off = read_i16(pc);
                    int bval;
                    pc += 2;
                    if (sp < 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    bval = slot_to_bool(&stack[--sp]);
                    if (bval < 0)
                    {
                        vm_rc = IRXBC_ERR_ARITH;
                        goto check_syntax_trap;
                    }
                    if (bval)
                    {
                        pc += off;
                    }
                    break;
                }

                /* ---- I/O (WP-BC-03) -------------------------------- */
                case OP_SAY:
                {
                    struct irxexte *exte;
                    int (*io_fn)(int, PLstr, struct envblock *);
                    int irc;

                    if (sp < 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    sp--;
                    exte = (struct irxexte *)envblock->envblock_irxexte;
                    if (exte == NULL || exte->io_routine == NULL)
                    {
                        vm_rc = IRXBC_ERR_IO;
                        goto done;
                    }
                    io_fn = (int (*)(int, PLstr,
                                     struct envblock *))exte->io_routine;
                    irc = io_fn(RXFWRITE, stack[sp].str, envblock);
                    if (irc != 0)
                    {
                        vm_rc = IRXBC_ERR_IO;
                        goto done;
                    }
                    break;
                }

                /* ---- DO loop ops (WP-BC-03) ----------------------- */
                case OP_TOINT:
                case OP_DOTEST:
                    break;

                case OP_BYINIT:
                    pc++; /* skip u8 operand (reserved) */
                    break;

                case OP_FORINIT:
                {
                    unsigned char n = *pc++;
                    int32_t count;

                    if (sp < 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    if (n >= IRXBC_DO_DEPTH)
                    {
                        vm_rc = IRXBC_ERR_LOOP;
                        goto done;
                    }
                    sp--;
                    count = slot_to_int32(&stack[sp]);
                    frames[n].counter = count;
                    if (slot_set_bool(&stack[sp], alloc,
                                      count > 0) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    sp++;
                    break;
                }

                case OP_DECFOR:
                {
                    unsigned char n = *pc++;
                    int off = read_i16(pc);
                    pc += 2;
                    if (n >= IRXBC_DO_DEPTH)
                    {
                        vm_rc = IRXBC_ERR_LOOP;
                        goto done;
                    }
                    frames[n].counter--;
                    if (frames[n].counter <= 0)
                    {
                        pc += off;
                    }
                    break;
                }

                    /* ---- WP-BC-04: function / CALL / RETURN ----------- */

                case OP_LABEL:
                    /* No-op at runtime; label table built during pre-scan. */
                    pc += 2;
                    break;

                case OP_CALL:
                {
                    int sym_idx = read_u16(pc);
                    int nargs = (int)pc[2];
                    int target = (label_pc != NULL && sym_idx >= 0 &&
                                  sym_idx < n_syms)
                                     ? label_pc[sym_idx]
                                     : -1;
                    pc += 3;

                    if (sp < nargs)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }

                    if (target >= 0)
                    {
                        /* Internal function call — push call frame */
                        struct bc_call_frame *cf;
                        int ci;

                        if (call_sp >= IRXBC_CALL_DEPTH)
                        {
                            vm_rc = IRXBC_ERR_CALL;
                            goto done;
                        }
                        cf = &call_frames[call_sp];
                        memset(cf, 0, sizeof(struct bc_call_frame));
                        cf->return_pc = pc;
                        cf->argc = nargs;
                        cf->push_result = 0;
                        /* Snapshot trap state so callee changes revert on RETURN */
                        cf->cond_enabled_save = cond_enabled;
                        memcpy(cf->cond_lsi_save, cond_lsi,
                               COND_COUNT * (int)sizeof(int));
                        for (ci = 0; ci < nargs; ci++)
                        {
                            PLstr src = stack[sp - nargs + ci].str;
                            if (lstr_copy(alloc, &cf->args[ci], src) != LSTR_OK)
                            {
                                vm_rc = IRXBC_ERR_STOR;
                                goto done;
                            }
                            cf->arg_exists[ci] = 1;
                        }
                        sp -= nargs;
                        call_sp++;
                        proxy_parser->call_args = cf->args;
                        proxy_parser->call_arg_exists = cf->arg_exists;
                        proxy_parser->call_argc = nargs;
                        pc = code_base + target;
                    }
                    else
                    {
                        /* BIF fallback for CALL stmt */
                        const struct irx_bif_registry *reg;
                        const struct irx_bif_entry *bife;
                        const char *name_data;
                        int name_len;
                        PLstr argv_arr[IRX_MAX_ARGS];
                        int ci;
                        int brc;

                        name_len = get_entry(sym_base, n_syms,
                                             sym_idx, &name_data);
                        if (name_len < 0)
                        {
                            vm_rc = IRXBC_ERR_OPCODE;
                            goto done;
                        }
                        reg = bvm_get_bif_registry(envblock);
                        bife = irx_bif_find(
                            reg,
                            (const unsigned char *)name_data,
                            (size_t)name_len);
                        if (bife == NULL)
                        {
                            vm_rc = IRXBC_ERR_UNSUP;
                            goto done;
                        }
                        if (nargs < bife->min_args ||
                            nargs > bife->max_args)
                        {
                            vm_rc = IRXBC_ERR_UNSUP;
                            goto done;
                        }
                        for (ci = 0; ci < nargs; ci++)
                        {
                            argv_arr[ci] = stack[sp - nargs + ci].str;
                        }
                        brc = bife->handler(proxy_parser, nargs,
                                            argv_arr,
                                            &proxy_parser->result);
                        sp -= nargs;
                        if (brc != IRXPARS_OK)
                        {
                            vm_rc = IRXBC_ERR_ARITH;
                            goto check_syntax_trap;
                        }
                        /* Store result as RESULT variable (CALL stmt) */
                        if (vpool_set_buf(vpool, "RESULT", 6,
                                          &proxy_parser->result,
                                          0, 0) != VPOOL_OK)
                        {
                            vm_rc = IRXBC_ERR_STOR;
                            goto done;
                        }
                    }
                    break;
                }

                case OP_CALL_BIF:
                {
                    int sym_idx = read_u16(pc);
                    int nargs = (int)pc[2];
                    int target = (label_pc != NULL && sym_idx >= 0 &&
                                  sym_idx < n_syms)
                                     ? label_pc[sym_idx]
                                     : -1;
                    const char *name_data;
                    int name_len;
                    int ci;

                    pc += 3;
                    if (sp < nargs)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }

                    if (target >= 0)
                    {
                        /* User-defined function: jump to label.
                         * push_result=1 causes RETURNV to leave the
                         * value on the eval stack for the caller. */
                        struct bc_call_frame *cf;

                        if (call_sp >= IRXBC_CALL_DEPTH)
                        {
                            vm_rc = IRXBC_ERR_CALL;
                            goto done;
                        }
                        cf = &call_frames[call_sp];
                        memset(cf, 0, sizeof(struct bc_call_frame));
                        cf->return_pc = pc;
                        cf->argc = nargs;
                        cf->push_result = 1;
                        /* Snapshot trap state so callee changes revert on RETURN */
                        cf->cond_enabled_save = cond_enabled;
                        memcpy(cf->cond_lsi_save, cond_lsi,
                               COND_COUNT * (int)sizeof(int));
                        for (ci = 0; ci < nargs; ci++)
                        {
                            PLstr src = stack[sp - nargs + ci].str;
                            if (lstr_copy(alloc, &cf->args[ci], src) != LSTR_OK)
                            {
                                vm_rc = IRXBC_ERR_STOR;
                                goto done;
                            }
                            cf->arg_exists[ci] = 1;
                        }
                        sp -= nargs;
                        call_sp++;
                        proxy_parser->call_args = cf->args;
                        proxy_parser->call_arg_exists = cf->arg_exists;
                        proxy_parser->call_argc = nargs;
                        pc = code_base + target;
                    }
                    else
                    {
                        /* BIF dispatch */
                        const struct irx_bif_registry *reg;
                        const struct irx_bif_entry *bife;
                        PLstr argv_arr[IRX_MAX_ARGS];
                        int brc;

                        /* net stack delta = -nargs+1; overflow only when nargs==0 */
                        if (sp - nargs + 1 > IRXBC_STACK_DEPTH)
                        {
                            vm_rc = IRXBC_ERR_STACK;
                            goto done;
                        }
                        name_len = get_entry(sym_base, n_syms,
                                             sym_idx, &name_data);
                        if (name_len < 0)
                        {
                            vm_rc = IRXBC_ERR_OPCODE;
                            goto done;
                        }
                        reg = bvm_get_bif_registry(envblock);
                        bife = irx_bif_find(
                            reg,
                            (const unsigned char *)name_data,
                            (size_t)name_len);
                        if (bife == NULL)
                        {
                            vm_rc = IRXBC_ERR_UNSUP;
                            goto done;
                        }
                        if (nargs < bife->min_args ||
                            nargs > bife->max_args)
                        {
                            vm_rc = IRXBC_ERR_UNSUP;
                            goto done;
                        }
                        for (ci = 0; ci < nargs; ci++)
                        {
                            argv_arr[ci] = stack[sp - nargs + ci].str;
                        }
                        brc = bife->handler(proxy_parser, nargs,
                                            argv_arr,
                                            &proxy_parser->result);
                        sp -= nargs;
                        if (brc != IRXPARS_OK)
                        {
                            vm_rc = IRXBC_ERR_ARITH;
                            goto check_syntax_trap;
                        }
                        if (slot_set_buf(
                                &stack[sp], alloc,
                                (const char *)Lpstr(&proxy_parser->result),
                                (int)Llen(&proxy_parser->result)) != LSTR_OK)
                        {
                            vm_rc = IRXBC_ERR_STOR;
                            goto done;
                        }
                        sp++;
                    }
                    break;
                }

                case OP_RETURN:
                {
                    if (call_sp > 0)
                    {
                        struct bc_call_frame *cf;
                        int ci;

                        call_sp--;
                        cf = &call_frames[call_sp];

                        /* Restore trap state (callee changes revert per §7) */
                        cond_enabled = cf->cond_enabled_save;
                        memcpy(cond_lsi, cf->cond_lsi_save,
                               COND_COUNT * (int)sizeof(int));

                        /* Scope teardown (PROCEDURE EXPOSE) */
                        if (cf->has_isolated_scope)
                        {
                            vpool_destroy(vpool);
                            vpool = cf->prev_vpool;
                            proxy_parser->vpool = vpool;
                            /* Isolated scope: RESULT in caller's pool is
                             * untouched — matches token-walk kw_return. */
                        }
                        else
                        {
                            /* §4.3.3: no return value — RESULT reverts to
                             * uninitialized in the shared variable pool. */
                            vpool_drop_buf(vpool, "RESULT", 6);
                        }

                        for (ci = 0; ci < IRX_MAX_ARGS; ci++)
                        {
                            Lfree(alloc, &cf->args[ci]);
                        }
                        pc = cf->return_pc;
                        if (call_sp > 0)
                        {
                            struct bc_call_frame *parent =
                                &call_frames[call_sp - 1];
                            proxy_parser->call_args = parent->args;
                            proxy_parser->call_arg_exists =
                                parent->arg_exists;
                            proxy_parser->call_argc = parent->argc;
                        }
                        else
                        {
                            proxy_parser->call_args = NULL;
                            proxy_parser->call_arg_exists = NULL;
                            proxy_parser->call_argc = 0;
                        }
                    }
                    else
                    {
                        if (rc_out != NULL)
                        {
                            *rc_out = 0;
                        }
                        goto done;
                    }
                    break;
                }

                case OP_RETURNV:
                {
                    int vrc;
                    int push_r;

                    if (sp < 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    sp--;

                    if (call_sp > 0)
                    {
                        struct bc_call_frame *cf;
                        int ci;

                        call_sp--;
                        cf = &call_frames[call_sp];
                        push_r = cf->push_result;

                        /* Restore trap state (callee changes revert per §7) */
                        cond_enabled = cf->cond_enabled_save;
                        memcpy(cond_lsi, cf->cond_lsi_save,
                               COND_COUNT * (int)sizeof(int));

                        /* Scope teardown before storing RESULT in
                         * caller's pool (PROCEDURE EXPOSE). */
                        if (cf->has_isolated_scope)
                        {
                            vpool_destroy(vpool);
                            vpool = cf->prev_vpool;
                            proxy_parser->vpool = vpool;
                        }

                        vrc = vpool_set_buf(vpool, "RESULT", 6,
                                            stack[sp].str,
                                            stack[sp].type_cache,
                                            stack[sp].int_cache);
                        if (vrc != VPOOL_OK)
                        {
                            vm_rc = IRXBC_ERR_STOR;
                            goto done;
                        }

                        for (ci = 0; ci < IRX_MAX_ARGS; ci++)
                        {
                            Lfree(alloc, &cf->args[ci]);
                        }
                        pc = cf->return_pc;
                        if (call_sp > 0)
                        {
                            struct bc_call_frame *parent =
                                &call_frames[call_sp - 1];
                            proxy_parser->call_args = parent->args;
                            proxy_parser->call_arg_exists =
                                parent->arg_exists;
                            proxy_parser->call_argc = parent->argc;
                        }
                        else
                        {
                            proxy_parser->call_args = NULL;
                            proxy_parser->call_arg_exists = NULL;
                            proxy_parser->call_argc = 0;
                        }
                        if (push_r)
                        {
                            /* Expression context: leave return value
                             * on the eval stack for the caller. */
                            sp++;
                        }
                    }
                    else
                    {
                        vrc = vpool_set_buf(vpool, "RESULT", 6,
                                            stack[sp].str,
                                            stack[sp].type_cache,
                                            stack[sp].int_cache);
                        if (vrc != VPOOL_OK)
                        {
                            vm_rc = IRXBC_ERR_STOR;
                            goto done;
                        }
                        if (rc_out != NULL)
                        {
                            *rc_out = 0;
                        }
                        goto done;
                    }
                    break;
                }

                    /* ---- SIGNAL (WP-BC-07 PR A) ------------------------- */

                case OP_SIGNAL:
                {
                    int lsi = read_u16(pc);
                    int target;
                    int fi, ci;
                    struct irx_wkblk_int *wk;

                    pc += 2;

                    if (label_pc == NULL || lsi < 0 || lsi >= n_syms ||
                        label_pc[lsi] < 0)
                    {
                        vm_rc = IRXBC_ERR_UNSUP;
                        goto done;
                    }
                    target = label_pc[lsi];

                    /* Close active parse frame */
                    if (pframe.active)
                    {
                        Lfree(alloc, &pframe.source);
                        Lzeroinit(&pframe.source);
                        pframe.active = 0;
                    }

                    /* Unwind call frames innermost-first */
                    for (fi = call_sp - 1; fi >= 0; fi--)
                    {
                        struct bc_call_frame *cf = &call_frames[fi];
                        for (ci = 0; ci < IRX_MAX_ARGS; ci++)
                        {
                            Lfree(alloc, &cf->args[ci]);
                        }
                        if (cf->has_isolated_scope && cf->prev_vpool != NULL)
                        {
                            vpool_destroy(vpool);
                            vpool = cf->prev_vpool;
                            proxy_parser->vpool = vpool;
                        }
                    }
                    call_sp = 0;
                    proxy_parser->call_args = NULL;
                    proxy_parser->call_arg_exists = NULL;
                    proxy_parser->call_argc = 0;

                    /* Clear eval stack */
                    sp = 0;

                    /* SIGL — line tracking not yet available; set to 0 */
                    wk = (struct irx_wkblk_int *)envblock->envblock_userfield;
                    if (wk != NULL)
                    {
                        wk->wkbi_sigl = 0;
                    }

                    pc = code_base + target;
                    break;
                }

                case OP_SIGNAL_VALUE:
                {
                    int lsi = -1;
                    int target;
                    int fi, ci, k;
                    struct irx_wkblk_int *wk;
                    char name_upper[IRXBC_STR_MAX + 1];
                    int nlen;

                    if (sp < 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    sp--;

                    /* Uppercase the popped label name */
                    {
                        PLstr s = stack[sp].str;
                        nlen = (int)Llen(s);
                        if (nlen > IRXBC_STR_MAX)
                        {
                            vm_rc = IRXBC_ERR_UNSUP;
                            goto done;
                        }
                        for (k = 0; k < nlen; k++)
                        {
                            name_upper[k] =
                                (char)toupper((unsigned char)Lpstr(s)[k]);
                        }
                    }

                    /* Search symbol table for matching label name */
                    for (k = 0; k < n_syms; k++)
                    {
                        const char *sdata;
                        int slen = get_entry(sym_base, n_syms, k, &sdata);
                        if (slen == nlen &&
                            memcmp(sdata, name_upper, (size_t)nlen) == 0)
                        {
                            lsi = k;
                            break;
                        }
                    }

                    if (lsi < 0 || label_pc == NULL || label_pc[lsi] < 0)
                    {
                        vm_rc = IRXBC_ERR_UNSUP;
                        goto done;
                    }
                    target = label_pc[lsi];

                    /* Close active parse frame */
                    if (pframe.active)
                    {
                        Lfree(alloc, &pframe.source);
                        Lzeroinit(&pframe.source);
                        pframe.active = 0;
                    }

                    /* Unwind call frames innermost-first */
                    for (fi = call_sp - 1; fi >= 0; fi--)
                    {
                        struct bc_call_frame *cf = &call_frames[fi];
                        for (ci = 0; ci < IRX_MAX_ARGS; ci++)
                        {
                            Lfree(alloc, &cf->args[ci]);
                        }
                        if (cf->has_isolated_scope && cf->prev_vpool != NULL)
                        {
                            vpool_destroy(vpool);
                            vpool = cf->prev_vpool;
                            proxy_parser->vpool = vpool;
                        }
                    }
                    call_sp = 0;
                    proxy_parser->call_args = NULL;
                    proxy_parser->call_arg_exists = NULL;
                    proxy_parser->call_argc = 0;

                    /* Clear eval stack */
                    sp = 0;

                    /* SIGL — line tracking not yet available; set to 0 */
                    wk = (struct irx_wkblk_int *)envblock->envblock_userfield;
                    if (wk != NULL)
                    {
                        wk->wkbi_sigl = 0;
                    }

                    pc = code_base + target;
                    break;
                }

                case OP_SIGNAL_ON:
                {
                    /* WP-BC-07 PR B: enable condition trap */
                    int cond_byte = (int)*pc++; /* cond:u8 */
                    int lsi = read_u16(pc);
                    pc += 2; /* sym_idx:u16 */
                    int ci = cond_bit_index((unsigned int)cond_byte);
                    if (ci >= 0)
                    {
                        cond_enabled |= (unsigned char)cond_byte;
                        cond_lsi[ci] = lsi;
                        {
                            struct irx_wkblk_int *wk =
                                (struct irx_wkblk_int *)
                                    envblock->envblock_userfield;
                            if (wk != NULL)
                            {
                                wk->wkbi_condflags |= cond_byte;
                            }
                        }
                    }
                    break;
                }

                case OP_SIGNAL_OFF:
                {
                    /* WP-BC-07 PR B: disable condition trap */
                    int cond_byte = (int)*pc++; /* cond:u8 */
                    int ci = cond_bit_index((unsigned int)cond_byte);
                    if (ci >= 0)
                    {
                        cond_enabled &=
                            (unsigned char)(~(unsigned int)cond_byte);
                        {
                            struct irx_wkblk_int *wk =
                                (struct irx_wkblk_int *)
                                    envblock->envblock_userfield;
                            if (wk != NULL)
                            {
                                wk->wkbi_condflags &= ~cond_byte;
                            }
                        }
                    }
                    break;
                }

                    /* ---- PARSE sub-VM (WP-BC-05 PR A) ------------------- */

                case OP_PARSE_BEGIN:
                {
                    unsigned char pbflags = *pc++;
                    if (sp < 1)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    sp--;
                    Lfree(alloc, &pframe.source);
                    Lzeroinit(&pframe.source);
                    pframe.upper = (pbflags & 1);
                    pframe.scan = 0;
                    pframe.cur_sym = -1;
                    pframe.cur_dot = 0;
                    if (Lstrcpy(alloc, &pframe.source, stack[sp].str) !=
                        LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    pframe.source_len = (int)Llen(&pframe.source);
                    if (pframe.upper && pframe.source_len > 0)
                    {
                        size_t ui;
                        unsigned char *up =
                            (unsigned char *)Lpstr(&pframe.source);
                        for (ui = 0; ui < (size_t)pframe.source_len; ui++)
                        {
                            up[ui] = (unsigned char)toupper((int)up[ui]);
                        }
                    }
                    pframe.active = 1;
                    break;
                }

                case OP_PARSE_END:
                    Lfree(alloc, &pframe.source);
                    Lzeroinit(&pframe.source);
                    pframe.active = 0;
                    pframe.source_len = 0;
                    pframe.scan = 0;
                    pframe.cur_cmpd_len = 0;
                    break;

                case OP_PVAR:
                    pframe.cur_sym = read_u16(pc);
                    pc += 2;
                    pframe.cur_dot = 0;
                    pframe.cur_cmpd_len = 0;
                    break;

                case OP_PVAR_STEM:
                {
                    int stem_idx = read_u16(pc);
                    int tail_cnt = (int)pc[2];
                    const char *stem_data;
                    int stem_len;
                    char name_buf[IRXBC_STR_MAX + 1];
                    int name_pos;
                    int k;

                    pc += 3;

                    if (sp < tail_cnt)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    stem_len =
                        get_entry(sym_base, n_syms, stem_idx, &stem_data);
                    if (stem_len < 0 || stem_len > IRXBC_STR_MAX)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    memcpy(name_buf, stem_data, (size_t)stem_len);
                    name_pos = stem_len;
                    for (k = 0; k < tail_cnt; k++)
                    {
                        const char *tv;
                        int tl, m;
                        tv = (const char *)Lpstr(
                            stack[sp - tail_cnt + k].str);
                        tl = (int)Llen(stack[sp - tail_cnt + k].str);
                        if (k > 0)
                        {
                            if (name_pos >= IRXBC_STR_MAX)
                            {
                                vm_rc = IRXBC_ERR_STOR;
                                goto done;
                            }
                            name_buf[name_pos++] = '.';
                        }
                        for (m = 0; m < tl; m++)
                        {
                            if (name_pos >= IRXBC_STR_MAX)
                            {
                                vm_rc = IRXBC_ERR_STOR;
                                goto done;
                            }
                            name_buf[name_pos++] =
                                (char)toupper((unsigned char)tv[m]);
                        }
                    }
                    name_buf[name_pos] = '\0';
                    sp -= tail_cnt;
                    memcpy(pframe.cur_cmpd, name_buf, (size_t)name_pos);
                    pframe.cur_cmpd_len = name_pos;
                    pframe.cur_sym = -1;
                    pframe.cur_dot = 0;
                    break;
                }

                case OP_PULL_FROM_QUEUE:
                    /* WP-33b: external data queue not yet implemented. */
                    vm_rc = IRXBC_ERR_UNSUP;
                    goto done;

                case OP_PDOT:
                    pframe.cur_sym = -1;
                    pframe.cur_dot = 1;
                    break;

                case OP_TR_SPACE:
                {
                    int prc = pframe_assign(&pframe, vpool, alloc,
                                            sym_base, n_syms,
                                            pframe.source_len, 0, 0);
                    if (prc != IRXBC_OK)
                    {
                        vm_rc = prc;
                        goto done;
                    }
                    break;
                }

                case OP_TR_LIT:
                {
                    const char *lit_data;
                    int lit_idx = read_u16(pc);
                    int lit_len, seg_end_lit, new_scan, found, si;
                    int prc;
                    pc += 2;
                    lit_len = get_entry(const_base, n_consts,
                                        lit_idx, &lit_data);
                    if (lit_len <= 0)
                    {
                        pframe.cur_sym = -1;
                        pframe.cur_dot = 0;
                        break;
                    }
                    found = -1;
                    for (si = pframe.scan;
                         si + lit_len <= pframe.source_len; si++)
                    {
                        if (Lpstr(&pframe.source) != NULL &&
                            memcmp((const char *)Lpstr(&pframe.source) + si,
                                   lit_data, (size_t)lit_len) == 0)
                        {
                            found = si;
                            break;
                        }
                    }
                    if (found >= 0)
                    {
                        seg_end_lit = found;
                        new_scan = found + lit_len;
                    }
                    else
                    {
                        seg_end_lit = pframe.source_len;
                        new_scan = pframe.source_len;
                    }
                    prc = pframe_assign(&pframe, vpool, alloc, sym_base,
                                        n_syms, seg_end_lit, new_scan, 1);
                    if (prc != IRXBC_OK)
                    {
                        vm_rc = prc;
                        goto done;
                    }
                    break;
                }

                case OP_TR_ABS:
                {
                    int col = read_u16(pc);
                    int npos = (col >= 1) ? col - 1 : 0;
                    int seg_end_abs, prc;
                    pc += 2;
                    if (npos > pframe.source_len)
                    {
                        npos = pframe.source_len;
                    }
                    seg_end_abs = (npos > pframe.scan) ? npos : pframe.scan;
                    prc = pframe_assign(&pframe, vpool, alloc, sym_base,
                                        n_syms, seg_end_abs, npos, 1);
                    if (prc != IRXBC_OK)
                    {
                        vm_rc = prc;
                        goto done;
                    }
                    break;
                }

                case OP_TR_REL:
                {
                    int off = read_i16(pc);
                    int new_scan, seg_end_rel, prc;
                    pc += 2;
                    if (off >= 0)
                    {
                        new_scan = pframe.scan + off;
                        if (new_scan > pframe.source_len)
                        {
                            new_scan = pframe.source_len;
                        }
                        seg_end_rel = new_scan;
                    }
                    else
                    {
                        int n = -off;
                        new_scan = (pframe.scan >= n) ? pframe.scan - n : 0;
                        seg_end_rel = pframe.scan;
                    }
                    prc = pframe_assign(&pframe, vpool, alloc, sym_base,
                                        n_syms, seg_end_rel, new_scan, 1);
                    if (prc != IRXBC_OK)
                    {
                        vm_rc = prc;
                        goto done;
                    }
                    break;
                }

                case OP_TR_END:
                {
                    int prc = pframe_assign(&pframe, vpool, alloc,
                                            sym_base, n_syms,
                                            pframe.source_len,
                                            pframe.source_len, 1);
                    if (prc != IRXBC_OK)
                    {
                        vm_rc = prc;
                        goto done;
                    }
                    break;
                }

                case OP_PUSH_SOURCE:
                {
                    const char *calltype =
                        (call_sp > 0) ? "SUBROUTINE" : "COMMAND";
                    int cl = (int)strlen(calltype);
                    char buf[32];
                    int blen;
                    /* "MVS " + calltype + " ?" */
                    memcpy(buf, "MVS ", 4);
                    memcpy(buf + 4, calltype, (size_t)cl);
                    memcpy(buf + 4 + cl, " ?", 2);
                    blen = 4 + cl + 2;
                    if (sp >= IRXBC_STACK_DEPTH)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    if (slot_set_buf(&stack[sp], alloc, buf, blen) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    sp++;
                    break;
                }

                case OP_PUSH_NUMERIC:
                {
                    struct irx_wkblk_int *wk = NULL;
                    int digits = NUMERIC_DIGITS_DEFAULT;
                    int fuzz = NUMERIC_FUZZ_DEFAULT;
                    int form = NUMFORM_SCIENTIFIC;
                    char buf[64];
                    char tmp[12];
                    int blen = 0, n;
                    if (envblock != NULL &&
                        envblock->envblock_userfield != NULL)
                    {
                        wk = (struct irx_wkblk_int *)
                                 envblock->envblock_userfield;
                        digits = wk->wkbi_digits;
                        fuzz = wk->wkbi_fuzz;
                        form = wk->wkbi_form;
                    }
                    n = i32toa(digits, tmp);
                    memcpy(buf + blen, tmp, (size_t)n);
                    blen += n;
                    buf[blen++] = ' ';
                    n = i32toa(fuzz, tmp);
                    memcpy(buf + blen, tmp, (size_t)n);
                    blen += n;
                    buf[blen++] = ' ';
                    if (form == NUMFORM_ENGINEERING)
                    {
                        memcpy(buf + blen, "ENGINEERING", 11);
                        blen += 11;
                    }
                    else
                    {
                        memcpy(buf + blen, "SCIENTIFIC", 10);
                        blen += 10;
                    }
                    if (sp >= IRXBC_STACK_DEPTH)
                    {
                        vm_rc = IRXBC_ERR_STACK;
                        goto done;
                    }
                    if (slot_set_buf(&stack[sp], alloc, buf, blen) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    sp++;
                    break;
                }

                    /* ---- PROCEDURE EXPOSE opcodes (WP-BC-05 PR B) --- */

                case OP_PROC:
                {
                    unsigned char nexposed = *pc++;
                    struct bc_call_frame *cf;
                    struct irx_vpool *new_vpool;

                    (void)nexposed; /* informational; OP_EXPOSE follow */
                    if (call_sp < 1)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    cf = &call_frames[call_sp - 1];
                    new_vpool = vpool_create(alloc, vpool);
                    if (new_vpool == NULL)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    cf->prev_vpool = vpool;
                    cf->has_isolated_scope = 1;
                    vpool = new_vpool;
                    proxy_parser->vpool = vpool;
                    break;
                }

                case OP_EXPOSE:
                {
                    int sym_idx = read_u16(pc);
                    const char *name_data;
                    int name_len;
                    Lstr name_lstr; /* stack Lstr wrapping sym table — read-only */
                    int erc;

                    pc += 2;
                    name_len = get_entry(sym_base, n_syms, sym_idx, &name_data);
                    if (name_len < 0)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    Lzeroinit(&name_lstr);
                    name_lstr.pstr = (unsigned char *)name_data;
                    name_lstr.len = (size_t)name_len;
                    if (name_len > 0 && name_data[name_len - 1] == '.')
                    {
                        erc = vpool_expose_stem(vpool, &name_lstr);
                    }
                    else
                    {
                        erc = vpool_expose_var(vpool, &name_lstr);
                    }
                    if (erc != VPOOL_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    break;
                }

                case OP_EXPOSE_INDIRECT:
                {
                    int sym_idx = read_u16(pc);
                    const char *name_data;
                    int name_len;
                    struct bc_call_frame *cf;
                    Lstr iname; /* stack Lstr wrapping sym table — read-only */
                    Lstr ival;
                    size_t ipos;
                    size_t ilen;

                    pc += 2;
                    if (call_sp < 1)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    cf = &call_frames[call_sp - 1];
                    if (!cf->has_isolated_scope || cf->prev_vpool == NULL)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    name_len = get_entry(sym_base, n_syms, sym_idx, &name_data);
                    if (name_len < 0)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }
                    Lzeroinit(&iname);
                    iname.pstr = (unsigned char *)name_data;
                    iname.len = (size_t)name_len;
                    Lzeroinit(&ival);
                    vpool_get(cf->prev_vpool, &iname, &ival);
                    ilen = ival.len;
                    ipos = 0;
                    while (ipos < ilen)
                    {
                        size_t wstart;
                        size_t wend;
                        Lstr ename;
                        size_t ui;
                        int erc;

                        while (ipos < ilen &&
                               isspace((unsigned char)ival.pstr[ipos]))
                        {
                            ipos++;
                        }
                        wstart = ipos;
                        while (ipos < ilen &&
                               !isspace((unsigned char)ival.pstr[ipos]))
                        {
                            ipos++;
                        }
                        wend = ipos;
                        if (wend == wstart)
                        {
                            break;
                        }
                        Lzeroinit(&ename);
                        if (Lfx(alloc, &ename, wend - wstart) != LSTR_OK)
                        {
                            Lfree(alloc, &ival);
                            vm_rc = IRXBC_ERR_STOR;
                            goto done;
                        }
                        memcpy(ename.pstr, ival.pstr + wstart, wend - wstart);
                        ename.len = wend - wstart;
                        for (ui = 0; ui < ename.len; ui++)
                        {
                            ename.pstr[ui] =
                                (unsigned char)toupper((int)ename.pstr[ui]);
                        }
                        if (ename.len > 0 &&
                            ename.pstr[ename.len - 1] == '.')
                        {
                            erc = vpool_expose_stem(vpool, &ename);
                        }
                        else
                        {
                            erc = vpool_expose_var(vpool, &ename);
                        }
                        Lfree(alloc, &ename);
                        if (erc != VPOOL_OK)
                        {
                            Lfree(alloc, &ival);
                            vm_rc = IRXBC_ERR_STOR;
                            goto done;
                        }
                    }
                    Lfree(alloc, &ival);
                    break;
                }

                default:
                    vm_rc = IRXBC_ERR_OPCODE;
                    goto done;
            }
            goto dispatch_next; /* normal dispatch: restart loop */

        check_syntax_trap:
            /* Intercept IRXBC_ERR_ARITH for SIGNAL ON SYNTAX (WP-BC-07 PR B).
             * ERROR/HALT/FAILURE/NOTREADY: trap infrastructure prepared (ON/OFF
             * sets enabled bit) but trigger deferred to WP-33 / Attention. */
            if ((cond_enabled & COND_SYNTAX) != 0 &&
                vm_rc == IRXBC_ERR_ARITH)
            {
                int ci_sx = cond_bit_index(COND_SYNTAX);
                if (ci_sx >= 0 && cond_lsi[ci_sx] >= 0 &&
                    label_pc != NULL &&
                    label_pc[cond_lsi[ci_sx]] >= 0)
                {
                    irx_cond_raise(envblock, SYNTAX_BAD_ARITH,
                                   ERR41_NONNUMERIC,
                                   "arithmetic/conversion error");
                    fired_cond = COND_SYNTAX;
                    trap_target = label_pc[cond_lsi[ci_sx]];
                    vm_rc = IRXBC_OK;
                    goto trap_jump;
                }
            }
            goto done;

        trap_jump:
        {
            int fi_t, ci_t;
            struct irx_wkblk_int *wk_t;
            const char *fn;
            size_t fnl;

            if (pframe.active)
            {
                Lfree(alloc, &pframe.source);
                Lzeroinit(&pframe.source);
                pframe.active = 0;
            }
            for (fi_t = call_sp - 1; fi_t >= 0; fi_t--)
            {
                struct bc_call_frame *cf_t = &call_frames[fi_t];
                for (ci_t = 0; ci_t < IRX_MAX_ARGS; ci_t++)
                {
                    Lfree(alloc, &cf_t->args[ci_t]);
                }
                if (cf_t->has_isolated_scope &&
                    cf_t->prev_vpool != NULL)
                {
                    vpool_destroy(vpool);
                    vpool = cf_t->prev_vpool;
                    proxy_parser->vpool = vpool;
                }
            }
            call_sp = 0;
            proxy_parser->call_args = NULL;
            proxy_parser->call_arg_exists = NULL;
            proxy_parser->call_argc = 0;
            sp = 0;

            wk_t = (struct irx_wkblk_int *)envblock->envblock_userfield;
            if (wk_t != NULL)
            {
                /* SIGL: line tracking deferred (no trace-map yet) */
                wk_t->wkbi_sigl = 0;
                /* Auto-disable fired condition (SC28-1883-0 §7) */
                cond_enabled &=
                    (unsigned char)(~(unsigned int)fired_cond);
                wk_t->wkbi_condflags &= ~fired_cond;
                /* Record condition name for future CONDITION() BIF */
                if (wk_t->wkbi_last_condition != NULL)
                {
                    switch ((unsigned int)fired_cond)
                    {
                        case COND_NOVALUE:
                            fn = "NOVALUE";
                            break;
                        case COND_SYNTAX:
                            fn = "SYNTAX";
                            break;
                        case COND_ERROR:
                            fn = "ERROR";
                            break;
                        case COND_HALT:
                            fn = "HALT";
                            break;
                        case COND_NOTREADY:
                            fn = "NOTREADY";
                            break;
                        case COND_FAILURE:
                            fn = "FAILURE";
                            break;
                        default:
                            fn = "";
                            break;
                    }
                    fnl = strlen(fn);
                    if (fnl >= IRX_COND_NAME_LEN)
                    {
                        fnl = IRX_COND_NAME_LEN - 1;
                    }
                    memcpy(wk_t->wkbi_last_condition->cond_name,
                           fn, fnl);
                    wk_t->wkbi_last_condition->cond_name[fnl] = '\0';
                }
            }
            fired_cond = 0;
            pc = code_base + trap_target;
            trap_target = -1;
        }

        dispatch_next:; /* null statement; VM loop restarts here */
        }
    }

done:
    /* Free parse frame source if active */
    if (pframe.active)
    {
        Lfree(alloc, &pframe.source);
    }

    /* Free any active call frame arg Lstr buffers */
    if (call_frames != NULL)
    {
        int fi, ci;
        for (fi = 0; fi < call_sp; fi++)
        {
            for (ci = 0; ci < IRX_MAX_ARGS; ci++)
            {
                Lfree(alloc, &call_frames[fi].args[ci]);
            }
        }
    }

    /* Free proxy parser result Lstr and any top-level call_args */
    if (proxy_parser != NULL)
    {
        Lfree(alloc, &proxy_parser->result);
        if (proxy_parser->call_args != NULL)
        {
            int ci;
            for (ci = 0; ci < proxy_parser->call_argc; ci++)
            {
                Lfree(alloc, &proxy_parser->call_args[ci]);
            }
            alloc->dealloc(proxy_parser->call_args,
                           (size_t)IRX_MAX_ARGS * sizeof(Lstr),
                           alloc->ctx);
        }
        if (proxy_parser->call_arg_exists != NULL)
        {
            alloc->dealloc(proxy_parser->call_arg_exists,
                           (size_t)IRX_MAX_ARGS * sizeof(int),
                           alloc->ctx);
        }
    }

    /* Free Lstr buffers */
    if (lstrs != NULL)
    {
        for (i = 0; i < IRXBC_STACK_DEPTH; i++)
        {
            Lfree(alloc, &lstrs[i]);
        }
    }

    /* Unwind any active PROCEDURE EXPOSE isolated scopes */
    if (call_frames != NULL)
    {
        int fi;
        for (fi = call_sp - 1; fi >= 0; fi--)
        {
            struct bc_call_frame *cf = &call_frames[fi];
            if (cf->has_isolated_scope && cf->prev_vpool != NULL)
            {
                if (vpool != NULL)
                {
                    vpool_destroy(vpool);
                }
                vpool = cf->prev_vpool;
            }
        }
    }

    /* Destroy variable pool */
    if (vpool != NULL)
    {
        vpool_destroy(vpool);
    }

    /* Free heap arrays */
    if (const_cache_mem != NULL)
    {
        void *p = const_cache_mem;
        irxstor(RXSMFRE, 0, &p, envblock);
    }
    if (label_pc_mem != NULL)
    {
        void *p = label_pc_mem;
        irxstor(RXSMFRE, 0, &p, envblock);
    }
    if (proxy_parser_mem != NULL)
    {
        void *p = proxy_parser_mem;
        irxstor(RXSMFRE, 0, &p, envblock);
    }
    if (call_frame_mem != NULL)
    {
        void *p = call_frame_mem;
        irxstor(RXSMFRE, 0, &p, envblock);
    }
    if (frames_mem != NULL)
    {
        void *p = frames_mem;
        irxstor(RXSMFRE, 0, &p, envblock);
    }
    if (lstr_mem != NULL)
    {
        void *p = lstr_mem;
        irxstor(RXSMFRE, 0, &p, envblock);
    }
    if (stack_mem != NULL)
    {
        void *p = stack_mem;
        irxstor(RXSMFRE, 0, &p, envblock);
    }

    return vm_rc;
}
