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
/*  irx_bc_execute                                                    */
/* ================================================================== */

int irx_bc_execute(struct envblock *envblock,
                   struct irx_bc_execblk *bc,
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
    int sp = 0; /* next free slot */
    int call_sp = 0;
    int n_consts;
    int n_syms;
    const char *const_base;
    const char *sym_base;
    int vm_rc = IRXBC_OK;
    int i;

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
    /* call_args / call_argc updated per call */

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
                    try_parse_int_cache(&stack[sp], data, len);
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
                        /* NOVALUE: value is the variable name itself */
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
                        goto done;
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
                        goto done;
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
                        goto done;
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
                        goto done;
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
                        goto done;
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
                        goto done;
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
                        goto done;
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
                        goto done;
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
                            vm_rc = IRXBC_ERR_LOOP;
                            goto done;
                        }
                        cf = &call_frames[call_sp];
                        memset(cf, 0, sizeof(struct bc_call_frame));
                        cf->return_pc = pc;
                        cf->argc = nargs;
                        cf->push_result = 0;
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
                            vm_rc = IRXBC_ERR_ARITH;
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
                            goto done;
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
                            vm_rc = IRXBC_ERR_LOOP;
                            goto done;
                        }
                        cf = &call_frames[call_sp];
                        memset(cf, 0, sizeof(struct bc_call_frame));
                        cf->return_pc = pc;
                        cf->argc = nargs;
                        cf->push_result = 1;
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

                        if (sp >= IRXBC_STACK_DEPTH)
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
                            vm_rc = IRXBC_ERR_ARITH;
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
                            goto done;
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
                    vrc = vpool_set_buf(vpool, "RESULT", 6,
                                        stack[sp].str,
                                        stack[sp].type_cache,
                                        stack[sp].int_cache);
                    if (vrc != VPOOL_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }

                    if (call_sp > 0)
                    {
                        struct bc_call_frame *cf;
                        int ci;

                        call_sp--;
                        cf = &call_frames[call_sp];
                        push_r = cf->push_result;
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
                        if (rc_out != NULL)
                        {
                            *rc_out = 0;
                        }
                        goto done;
                    }
                    break;
                }

                default:
                    vm_rc = IRXBC_ERR_OPCODE;
                    goto done;
            }
        }
    }

done:
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

    /* Free proxy parser result Lstr and proxy parser itself */
    if (proxy_parser != NULL)
    {
        Lfree(alloc, &proxy_parser->result);
    }

    /* Free Lstr buffers */
    if (lstrs != NULL)
    {
        for (i = 0; i < IRXBC_STACK_DEPTH; i++)
        {
            Lfree(alloc, &lstrs[i]);
        }
    }

    /* Destroy variable pool */
    if (vpool != NULL)
    {
        vpool_destroy(vpool);
    }

    /* Free heap arrays */
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
