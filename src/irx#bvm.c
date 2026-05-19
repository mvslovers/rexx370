/* ------------------------------------------------------------------ */
/*  irx#bvm.c - REXX/370 Bytecode VM Loop Skeleton (WP-BC-01)        */
/*                                                                    */
/*  irx_bc_execute() — Entry point.                                  */
/*                                                                    */
/*  Phase 1 scope:                                                    */
/*    - OP_NOP (0x00)      — advance PC                              */
/*    - OP_EXIT (0x01)     — terminate with RC=0                     */
/*    - OP_NEWCLAUSE (0x02)— clause boundary (no-op for Phase 1)    */
/*                                                                    */
/*  The big-switch dispatch is the canonical form: c2asm370 (GCC     */
/*  3.2.3) generates an optimised branch table from a dense switch   */
/*  on an unsigned char, matching BREXX-style dispatch performance.  */
/*                                                                    */
/*  Stack: bc_stack_slot is defined in irxbvm.h but no slot array   */
/*  is allocated in Phase 1 — no opcode pushes or pops yet.  WP-BC- */
/*  02 introduces the first stack-consuming opcode.                  */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include "irx.h"
#include "irxbops.h"
#include "irxbvm.h"
#include "irxexbl.h"

/* ------------------------------------------------------------------ */
/*  irx_bc_execute                                                    */
/* ------------------------------------------------------------------ */
int irx_bc_execute(struct envblock *envblock,
                   struct irx_bc_execblk *bc,
                   int *rc_out)
{
    const unsigned char *pc;
    unsigned char op;

    (void)envblock; /* not yet consumed in Phase 1 */

    if (bc == NULL)
    {
        return IRXBC_ERR_OPCODE;
    }

    pc = IRXBC_ENTRY(bc);

    for (;;)
    {
        op = *pc++;

        switch (op)
        {
            case OP_NOP:
                break;

            case OP_NEWCLAUSE:
                /* Phase 1: no-op — TRACE hook reserved for WP-BC-03. */
                break;

            case OP_EXIT:
                if (rc_out != NULL)
                {
                    *rc_out = 0;
                }
                return IRXBC_OK;

            default:
                return IRXBC_ERR_OPCODE;
        }
    }
}
