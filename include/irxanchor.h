/* ------------------------------------------------------------------ */
/*  irxanchor.h - ECTENVBK anchor discipline for REXX/370             */
/*                                                                    */
/*  A REXX Language Processor Environment is anchored in the TSO ECT  */
/*  at offset +30 (ECTENVBK). On IRXINIT we push the new ENVBLOCK,    */
/*  saving the previous ECTENVBK value in ENVBLOCK+304 (rexx370_prev).*/
/*  On IRXTERM we pop, restoring the previous value — but only if     */
/*  we're still the current anchor (lenient pop).                     */
/*                                                                    */
/*  Cold-path walk on MVS 3.8j (validated on Hercules since 2019,     */
/*  offsets from IBM macros):                                         */
/*                                                                    */
/*     PSA + PSAAOLD    (0x224) -> ASCB                               */
/*     ASCB + ASCBASXB  (0x06C) -> ASXB                               */
/*     ASXB + ASXBLWA   (0x014) -> LWA                                */
/*     LWA  + LWAPECT   (0x020) -> ECT                                */
/*     ECT  + ECTENVBK  (0x030) -> ENVBLOCK                           */
/*                                                                    */
/*  In batch any link in this chain can be NULL (LWA is typical);     */
/*  anchor_walk_to_ect() returns NULL and anchor_push/pop become      */
/*  local-state-only operations on the ENVBLOCK itself.               */
/*                                                                    */
/*  Ref: CON-1 §3.1 (ENVBLOCK layout), §6.1 (push/pop discipline).    */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#ifndef IRXANCHOR_H
#define IRXANCHOR_H

#include "irx.h"

/* Cold-path walk PSA -> ASCB -> ASXB -> LWA -> ECT.
 * Returns ECT address, or NULL if any link in the chain is NULL
 * (typical in batch, where LWA is not established). */
void *anchor_walk_to_ect(void);

/* TSO detection via crent370's CLIBCRT.crtflag (CRTFLAG_TSO bit).
 * On non-MVS builds, returns 0 (cross-compile = batch-like). */
int anchor_is_tso(void);

/* Read ECTENVBK — the currently installed ENVBLOCK, or NULL if none
 * (batch, or between push/pop sequences). */
struct envblock *anchor_get_current(void);

/* Push new_env onto the anchor. Saves the previous ECTENVBK in
 * new_env->rexx370_prev, populates new_env->envblock_ectptr with the
 * ECT address (for later reflection routines), then writes
 * ECTENVBK = new_env.
 *
 * In batch (no ECT): sets new_env->rexx370_prev = NULL and
 * new_env->envblock_ectptr = NULL; ECTENVBK is not touched. IRXINIT
 * still succeeds — anchor_get_current() will continue to return NULL.
 *
 * This function is infallible; any structural problem has already
 * been screened by IRXINIT's storage allocation. */
void anchor_push(struct envblock *new_env);

/* Pop env from the anchor. Lenient: only restores rexx370_prev into
 * ECTENVBK when env is still the current anchor. Mismatches are a
 * no-op (a Phase-6 diagnostic hook may be wired in later).
 *
 * In batch, this is always a no-op. */
void anchor_pop(struct envblock *env);

#endif /* IRXANCHOR_H */
