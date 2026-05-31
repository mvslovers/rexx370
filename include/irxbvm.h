/* ------------------------------------------------------------------ */
/*  irxbvm.h - REXX/370 Bytecode VM Entry Points (WP-BC-01)          */
/*                                                                    */
/*  Declares the compiler (irx_bc_compile) and VM loop               */
/*  (irx_bc_execute) entry points, and the canonical stack-slot      */
/*  layout shared by all WP-BC work packages.                        */
/*                                                                    */
/*  The stack slot type is defined here for forward compatibility.   */
/*  Phase 1 does not push or pop values, so no slot array is         */
/*  allocated; WP-BC-02 adds the first opcode that touches the stack. */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXBVM_H
#define IRXBVM_H

#include <stdint.h>

#include "irx.h"
#include "irxexbl.h"
#include "lstring.h"

/* ================================================================== */
/*  Stack slot (canonical layout for all WP-BC)                      */
/*                                                                    */
/*  Every evaluation stack entry is 24 bytes:                        */
/*    str        — always valid; canonical string form of the value  */
/*    type_cache — 0=none, LINTEGER_TY, LBCD_TY, LSTRING_TY         */
/*    int_cache  — integer fast-path (valid when type_cache==LINTEGER)*/
/*                                                                    */
/*  On OP_STORE, type_cache is written to the variable pool so a     */
/*  subsequent OP_LOAD can repopulate the cache slot without a        */
/*  string-to-integer parse.                                         */
/* ================================================================== */

#define IRXBC_STACK_LINTEGER 1

struct bc_stack_slot
{
    PLstr str;          /* canonical string form; always present      */
    int32_t type_cache; /* 0=none, 1=integer, see LINTEGER_TY etc.   */
    int32_t int_cache;  /* integer fast-path value                    */
};

/* ================================================================== */
/*  irx_bc_compile — compile REXX source to a bytecode container     */
/*                                                                    */
/*  Phase 1 handles only EXIT and empty clauses.  Any other          */
/*  construct returns IRXBC_ERR_UNSUP.                               */
/*                                                                    */
/*  Parameters:                                                       */
/*    envblock        — owning environment (used for irxstor)        */
/*    source          — REXX source text (need not be NUL-terminated)*/
/*    source_len      — length in bytes                              */
/*    bc_out          — receives pointer to allocated irx_bc_execblk */
/*                      on success; caller frees with irxstor(RXSMFRE)*/
/*    unsup_reason_out — optional (may be NULL); on IRXBC_ERR_UNSUP  */
/*                      receives a bc_unsup_reason code identifying   */
/*                      the construct the compiler could not handle.  */
/*                      0 (BC_UNSUP_NONE) on any other outcome.       */
/*    unsup_line_out  — optional (may be NULL); on IRXBC_ERR_UNSUP   */
/*                      receives the 1-based source line of the       */
/*                      offending construct.  0 on any other outcome. */
/*                                                                    */
/*  The reason/line pair is purely diagnostic (WP-BC-DIAG) — pass    */
/*  NULL for both when the caller does not need it.                   */
/*                                                                    */
/*  Returns: IRXBC_OK (0) on success, IRXBC_ERR_* on failure.       */
/* ================================================================== */

int irx_bc_compile(struct envblock *envblock,
                   const char *source, int source_len,
                   struct irx_bc_execblk **bc_out,
                   int *unsup_reason_out,
                   int *unsup_line_out) asm("IRXBCOMP");

/* ================================================================== */
/*  irx_bc_unsup_text — map a bc_unsup_reason code to short text      */
/*                                                                    */
/*  Returns a static, never-NULL human-readable string for a reason  */
/*  code reported by irx_bc_compile via unsup_reason_out.  Out-of-    */
/*  range or unmapped codes return "unknown".  Used only for the     */
/*  REXX370_BCDEBUG diagnostic output; not on any hot path.          */
/* ================================================================== */

const char *irx_bc_unsup_text(int reason) asm("IRXBCUTX");

/* ================================================================== */
/*  irx_bc_execute — execute a compiled bytecode container           */
/*                                                                    */
/*  Parameters:                                                       */
/*    envblock — owning environment                                  */
/*    bc       — container produced by irx_bc_compile               */
/*    args     — top-level argument string (may be NULL)             */
/*    args_len — length of args in bytes (0 if args is NULL)         */
/*    rc_out   — receives the program RC on success; may be NULL     */
/*                                                                    */
/*  Returns: IRXBC_OK (0) on success, IRXBC_ERR_* on failure.       */
/* ================================================================== */

int irx_bc_execute(struct envblock *envblock,
                   struct irx_bc_execblk *bc,
                   const char *args, int args_len,
                   int *rc_out) asm("IRXBEXEC");

#endif /* IRXBVM_H */
