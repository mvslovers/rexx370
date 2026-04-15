/* ------------------------------------------------------------------ */
/*  irxexec.h - REXX/370 End-to-End Execution (WP-18)                 */
/*                                                                    */
/*  irx_exec_run() is the top-level entry point that ties all Phase 2  */
/*  components together: environment, tokenizer, variable pool,        */
/*  parser, control flow, and I/O.                                     */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 1 (Introduction), Chapter 8              */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef __IRXEXEC_H__
#define __IRXEXEC_H__

#include "irx.h"

/* Execute a REXX program from source text.
 *
 *   source     - REXX source text (need not be NUL-terminated)
 *   source_len - length of source in bytes
 *   rc_out     - receives the EXIT return code (0 if no EXIT clause)
 *                May be NULL if the caller does not need the RC.
 *   envblock   - pre-existing Language Processor Environment, or NULL
 *                to have irx_exec_run create (and destroy) one.
 *
 * Pipeline:
 *   irxinit -> irx_lstr_init -> irx_tokn_run -> vpool_create
 *   -> irx_pars_init -> irx_ctrl_init -> irx_ctrl_label_scan
 *   -> irx_pars_run -> cleanup -> irxterm (if own_env)
 *
 * Returns:
 *   0        success (exit_rc in *rc_out)
 *   20       IRXINIT or allocator failure
 *   TOKERR_* tokenizer error (30-36)
 *   IRXPARS_* parser / runtime error (20-25)
 */
int irx_exec_run(const char *source, int source_len,
                 int *rc_out, struct envblock *envblock);

#endif /* __IRXEXEC_H__ */
