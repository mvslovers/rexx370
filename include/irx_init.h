/* ------------------------------------------------------------------ */
/*  irx_init.h - IRXINIT C-Core API (WP-I1c.1 / WP-I1c.2)           */
/*                                                                    */
/*  Internal signatures for IRXINIT function-code implementations:   */
/*    irx_init_initenvb  - INITENVB 9-step C-core (WP-I1c.1)         */
/*    irx_init_findenvb  - FINDENVB: locate non-reentrant env on TCB */
/*    irx_init_chekenvb  - CHEKENVB: validate an ENVBLOCK address    */
/*    irx_init_dispatch  - central dispatcher keyed on CL8 funccode  */
/*                                                                    */
/*  Ref: SC28-1883-0 §14 (IRXINIT FINDENVB / CHEKENVB)               */
/*  Ref: CON-1 §6.2 (env-type detection), §6.3 (previous-env lookup) */
/*  Ref: CON-3 (ECTENVBK semantics — greenfield-verified,             */
/*       non-greenfield behavior TBD pending IRXPROBE)                */
/*  Ref: WP-I1c.1, WP-I1c.2                                          */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#ifndef IRX_INIT_H
#define IRX_INIT_H

#include <stdint.h>

#include "irx.h"

/* CL8 function-code field width — IBM IRXINIT VLIST convention. */
#define IRXINIT_FUNCCODE_LEN 8

/* c2asm370 truncates C names to 8 chars with '_' mapped to '@'.
 * The prefix irx_init_ is already 8 chars (IRX@INIT), leaving no
 * room for the function-specific suffix — all four functions would
 * collapse to the same MVS symbol and IFOX00 would reject the
 * duplicate ENTRY.  The asm() aliases below give each function a
 * distinct 8-char MVS symbol under the IRXI* namespace. */

/* ------------------------------------------------------------------ */
/*  irx_init_initenvb - 9-step IRXINIT C-core (WP-I1c.1)             */
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
                      int *out_reason_code) asm("IRXIINIT");

/* ------------------------------------------------------------------ */
/*  irx_init_findenvb - locate non-reentrant env on caller TCB       */
/*                                                                    */
/*  Searches the IRXANCHR table for the most recently allocated       */
/*  (highest token) active, non-reentrant slot whose TCB matches      */
/*  PSATOLD (the caller's TCB).                                       */
/*                                                                    */
/*  Parameters:                                                       */
/*    out_envblock    - [OUT] ENVBLOCK address on success.            */
/*    out_reason_code - [OUT] 0=ok, 4=no non-reentrant env found.    */
/*                                                                    */
/*  Returns: 0=found (RC=0), 4=not found (RC=4, RSN=4).              */
/* ------------------------------------------------------------------ */
int irx_init_findenvb(struct envblock **out_envblock,
                      int *out_reason_code) asm("IRXIFIND");

/* ------------------------------------------------------------------ */
/*  irx_init_chekenvb - validate an ENVBLOCK address                 */
/*                                                                    */
/*  Checks (1) that the caller-supplied address carries the           */
/*  'ENVBLOCK' eye-catcher and (2) that it is registered in an        */
/*  active IRXANCHR slot.                                             */
/*                                                                    */
/*  Parameters:                                                       */
/*    envblock        - address to validate (may be NULL).            */
/*    out_reason_code - [OUT] 0=ok, 4=bad eye-catcher,               */
/*                       8=not in IRXANCHR.                           */
/*                                                                    */
/*  Returns: 0=valid (RC=0), 20=invalid (RC=20, RSN set).            */
/* ------------------------------------------------------------------ */
int irx_init_chekenvb(struct envblock *envblock,
                      int *out_reason_code) asm("IRXICHEK");

/* ------------------------------------------------------------------ */
/*  irx_init_term - 5-step IRXTERM C-core (WP-I1c.3)                 */
/*                                                                    */
/*  Reverses irx_init_initenvb: releases IRXEXTE, PARMBLOCK copy,    */
/*  IRXANCHR slot, and ENVBLOCK itself, in reverse-allocation order.  */
/*  For TSO-attached envs, rolls ECTENVBK back to the predecessor     */
/*  TSO-attached env (or NULL), provided we still own the slot        */
/*  (CON-14 / IRXPROBE Phase alpha). Non-TSO envs: ECTENVBK unchanged.*/
/*                                                                    */
/*  Parameters:                                                       */
/*    envblock        - ENVBLOCK to terminate; must be non-NULL and   */
/*                      carry the 'ENVBLOCK' eye-catcher.             */
/*    out_reason_code - [OUT] 0=ok, 4=bad eye-catcher or envblock     */
/*                       not in IRXANCHR (idempotency guard).         */
/*                                                                    */
/*  Returns: 0 on success, 20 on error (out_reason_code set).        */
/*  After a successful return, envblock is freed and must not be      */
/*  dereferenced.                                                     */
/* ------------------------------------------------------------------ */
int irx_init_term(struct envblock *envblock,
                  int *out_reason_code) asm("IRXITERM");

/* ------------------------------------------------------------------ */
/*  irx_init_dispatch - central IRXINIT function-code dispatcher     */
/*                                                                    */
/*  Routes on the 8-byte (CL8) function code string to the           */
/*  appropriate C-core function.  Designed for WP-I1c.5 (HLASM       */
/*  entry-point wrapper) which parses the caller VLIST and then       */
/*  calls this dispatcher uniformly for all function codes.           */
/*                                                                    */
/*  Parameters:                                                       */
/*    funccode         - 8-byte function code (space-padded):         */
/*                       "INITENVB", "FINDENVB", or "CHEKENVB".      */
/*    prev_envblock    - previous-env hint (INITENVB only).           */
/*    caller_parmblock - caller PARMBLOCK (INITENVB only).            */
/*    user_field       - user field value (INITENVB only).            */
/*    envblock_inout   - semantics depend on function code:           */
/*                       INITENVB: [OUT] newly allocated ENVBLOCK.    */
/*                       FINDENVB: [OUT] located non-reentrant env.  */
/*                       CHEKENVB: [IN]  *envblock_inout is the       */
/*                                 ENVBLOCK address to validate.      */
/*    out_reason_code  - [OUT] reason code (0 on success).            */
/*                                                                    */
/*  Returns: as per the dispatched function code, or 20 on unknown   */
/*           function code (out_reason_code = 12).                   */
/* ------------------------------------------------------------------ */
int irx_init_dispatch(const char funccode[IRXINIT_FUNCCODE_LEN],
                      struct envblock *prev_envblock,
                      struct parmblock *caller_parmblock,
                      uint32_t user_field,
                      struct envblock **envblock_inout,
                      int *out_reason_code) asm("IRXIDISP");

#endif /* IRX_INIT_H */
