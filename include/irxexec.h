/* ------------------------------------------------------------------ */
/*  irxexec.h - REXX/370 End-to-End Execution (WP-18 + WP-CPS-06)    */
/*                                                                    */
/*  irx_exec_run() ties all Phase 2 components together.              */
/*  irx_exec_dispatch() is the C-core for the IRXEXEC Programming     */
/*  Service (z/OS 10-slot VLIST form, WP-CPS-06 / TSK-218).          */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 1 (Introduction), Chapter 8              */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXEXEC_H
#define IRXEXEC_H

#include "irx.h"

/* Execute a REXX program from source text.
 *
 *   source     - REXX source text (need not be NUL-terminated)
 *   source_len - length of source in bytes
 *   args       - argument string for the program (ARG / PARSE ARG),
 *                or NULL if no argument is provided
 *   args_len   - length of args in bytes (0 if args is NULL)
 *   rc_out     - receives the EXIT return code (0 if no EXIT clause)
 *                May be NULL if the caller does not need the RC.
 *   envblock   - pre-existing Language Processor Environment, or NULL
 *                to have irx_exec_run create (and destroy) one.
 *
 * Pipeline:
 *   irxinit -> irx_lstr_init -> irx_tokn_run -> vpool_create
 *   -> irx_pars_init -> irx_ctrl_label_scan
 *   -> irx_pars_run -> cleanup -> irxterm (if own_env)
 *
 * Returns:
 *   0        success (exit_rc in *rc_out)
 *   20       IRXINIT or allocator failure
 *   TOKERR_* tokenizer error (30-36)
 *   IRXPARS_* parser / runtime error (20-25)
 */
int irx_exec_run(const char *source, int source_len,
                 const char *args, int args_len,
                 int *rc_out, struct envblock *envblock);

/* ------------------------------------------------------------------ */
/*  IRXEXEC Programming Service dispatcher — called from              */
/*  asm/irxexec.asm with all 10 VLIST slots already parsed.           */
/*                                                                    */
/*  10-parameter z/OS form. SC28-1883-0 V1 had a shorter, different   */
/*  parameter layout; this dispatcher implements the z/OS-stage spec. */
/*                                                                    */
/*  Env resolution (three-path):                                      */
/*    1. envblock (P9) non-NULL → use it                              */
/*    2. envblock_r0 (R0 at entry) non-NULL → use it                  */
/*    3. FINDENVB via IRXANCHR lookup                                  */
/*    4. → IRXEXEC_NOENV (auto-init deferred to WP-CPS-06b)           */
/*                                                                    */
/*  P10 (rexx_return_code) is handled by asm/irxexec.asm after this   */
/*  returns — the wrapper stores R15 through the P10 pointer if the   */
/*  caller supplied one. This function returns int → R15 only.        */
/*                                                                    */
/*  Refs:                                                             */
/*    z/OS REXX Reference — IRXEXEC parameters and return codes       */
/*    https://www.ibm.com/docs/en/zos/2.5.0?topic=ir-parameters       */
/*    WP-CPS-06 / TSK-218 (this WP), TSK-223 (WP-CPS-06b follow-on)  */
/* ------------------------------------------------------------------ */
int irx_exec_dispatch(struct execblk *execblk,                       /* P1 */
                      void *argtable,                                /* P2 */
                      int flags,                                     /* P3 deref'd by asm */
                      struct instblk *instblk,                       /* P4 */
                      void *reserved_parm5,                          /* P5 */
                      struct evalblock *evalblock,                   /* P6 */
                      void *workarea,                                /* P7 */
                      void *userfield,                               /* P8 */
                      struct envblock *envblock,                     /* P9 */
                      struct envblock *envblock_r0) asm("IRXEDISP"); /* R0 */

#endif /* IRXEXEC_H */
