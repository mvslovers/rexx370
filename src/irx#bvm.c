/* ------------------------------------------------------------------ */
/*  irx#bvm.c - REXX/370 Bytecode VM Loop (WP-BC-03)                 */
/*                                                                    */
/*  irx_bc_execute() — Entry point.                                  */
/*                                                                    */
/*  WP-BC-02: stack slots, variable pool, arithmetic/comparison/     */
/*  logical/string opcodes, PUSH_LIT / LOAD / STORE / POP.          */
/*  WP-BC-03: control flow (JMP/JF/JT), SAY, DO loop ops            */
/*  (FORINIT/BYINIT/DECFOR stubs), ITERATE, LEAVE.                  */
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

#define IRXBC_STACK_DEPTH 64

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
/*  irx_bc_execute                                                    */
/* ================================================================== */

int irx_bc_execute(struct envblock *envblock,
                   struct irx_bc_execblk *bc,
                   int *rc_out)
{
    const unsigned char *pc;
    unsigned char op;
    struct lstr_alloc *alloc = NULL;
    struct irx_vpool *vpool = NULL;
    struct bc_stack_slot *stack = NULL;
    Lstr *lstrs = NULL;
    void *stack_mem = NULL;
    void *lstr_mem = NULL;
    int sp = 0; /* next free slot */
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

    /* --- Variable pool ----------------------------------------------- */
    vpool = vpool_create(alloc, NULL);
    if (vpool == NULL)
    {
        vm_rc = IRXBC_ERR_STOR;
        goto done;
    }

    /* --- Fetch constants / symbol table pointers --------------------- */
    {
        const char *const_base = IRXBC_CONST_TBL(bc);
        const char *sym_base = IRXBC_SYM_TBL(bc);
        int n_consts = (int)bc->const_count;
        int n_syms = (int)bc->symbol_count;

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
                    Lstr name_lstr;
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

                    Lzeroinit(&name_lstr);
                    if (Lfx(alloc, &name_lstr,
                            (size_t)name_len) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    memcpy(Lpstr(&name_lstr), name_data,
                           (size_t)name_len);
                    name_lstr.len = (size_t)name_len;

                    vrc = vpool_get(vpool, &name_lstr,
                                    stack[sp].str);
                    if (vrc == VPOOL_NOT_FOUND)
                    {
                        /* NOVALUE: variable value is its own name */
                        if (Lstrcpy(alloc, stack[sp].str,
                                    &name_lstr) != LSTR_OK)
                        {
                            Lfree(alloc, &name_lstr);
                            vm_rc = IRXBC_ERR_STOR;
                            goto done;
                        }
                    }
                    else if (vrc != VPOOL_OK)
                    {
                        Lfree(alloc, &name_lstr);
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    stack[sp].type_cache = 0;
                    stack[sp].int_cache = 0;
                    sp++;
                    Lfree(alloc, &name_lstr);
                    break;
                }

                case OP_STORE:
                {
                    int idx = read_u16(pc);
                    const char *name_data;
                    int name_len;
                    Lstr name_lstr;
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

                    Lzeroinit(&name_lstr);
                    if (Lfx(alloc, &name_lstr,
                            (size_t)name_len) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    memcpy(Lpstr(&name_lstr), name_data,
                           (size_t)name_len);
                    name_lstr.len = (size_t)name_len;

                    sp--;
                    vrc = vpool_set(vpool, &name_lstr,
                                    stack[sp].str);
                    Lfree(alloc, &name_lstr);
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
                    Lstr name_lstr;

                    pc += 2;
                    name_len =
                        get_entry(sym_base, n_syms, idx, &name_data);
                    if (name_len < 0)
                    {
                        vm_rc = IRXBC_ERR_OPCODE;
                        goto done;
                    }

                    Lzeroinit(&name_lstr);
                    if (Lfx(alloc, &name_lstr,
                            (size_t)name_len) != LSTR_OK)
                    {
                        vm_rc = IRXBC_ERR_STOR;
                        goto done;
                    }
                    memcpy(Lpstr(&name_lstr), name_data,
                           (size_t)name_len);
                    name_lstr.len = (size_t)name_len;
                    vpool_drop(vpool, &name_lstr);
                    Lfree(alloc, &name_lstr);
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

                /* ---- DO loop ops (WP-BC-03) — stubs ---------------- */
                case OP_TOINT:
                case OP_DOTEST:
                    break;

                case OP_FORINIT:
                case OP_BYINIT:
                    pc++; /* skip u8 operand */
                    break;

                case OP_DECFOR:
                    pc += 2; /* skip i16 operand */
                    break;

                default:
                    vm_rc = IRXBC_ERR_OPCODE;
                    goto done;
            }
        }
    }

done:
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
