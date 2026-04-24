/* ------------------------------------------------------------------ */
/*  irx_init.h - IRXINIT C-Core API (WP-I1c.1)                       */
/*                                                                    */
/*  Internal signature for irx_init_initenvb(), the 9-step C-core    */
/*  that implements the INITENVB function code of IRXINIT:            */
/*  previous-env lookup, PARMBLOCK inheritance, ENVBLOCK allocation,  */
/*  IRXANCHR slot claim, and ECTENVBK update for TSO environments.    */
/*                                                                    */
/*  Ref: CON-1 §6.2 (env-type detection), §6.3 (previous-env lookup) */
/*  Ref: CON-3 (ECTENVBK unconditional overwrite, live-verified)      */
/*  Ref: WP-I1c.1                                                     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#ifndef IRX_INIT_H
#define IRX_INIT_H

#include <stdint.h>

#include "irx.h"

/* ------------------------------------------------------------------ */
/*  irx_init_initenvb - 9-step IRXINIT C-core                        */
/*                                                                    */
/*  Allocates and initializes a minimal REXX Language Processor       */
/*  Environment (ENVBLOCK + PARMBLOCK copy + placeholder IRXEXTE +   */
/*  IRXANCHR slot + optional ECTENVBK update for TSO).               */
/*                                                                    */
/*  Parameters:                                                       */
/*    prev_envblock    - explicit previous-env hint; NULL triggers    */
/*                       automatic lookup (TCB-based or ECTENVBK).   */
/*                       Ignored if eye-catcher is invalid.           */
/*    caller_parmblock - caller-supplied PARMBLOCK; flags/masks are   */
/*                       merged with prev_envblock's parmblock per    */
/*                       CON-1 §3.2 inheritance rules. NULL = all     */
/*                       defaults.                                    */
/*    user_field       - initial value for envblock_userfield;        */
/*                       compat wrapper passes 0.                     */
/*    out_envblock     - [OUT] newly allocated ENVBLOCK on success.   */
/*    out_reason_code  - [OUT] detail code: 0=ok, 1=envblock alloc,  */
/*                       2=parmblock alloc, 3=irxexte alloc.          */
/*                                                                    */
/*  Returns: 0 on success, 20 on error (out_reason_code set).        */
/* ------------------------------------------------------------------ */
int irx_init_initenvb(struct envblock *prev_envblock,
                      struct parmblock *caller_parmblock,
                      uint32_t user_field,
                      struct envblock **out_envblock,
                      int *out_reason_code);

#endif /* IRX_INIT_H */
