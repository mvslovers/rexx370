/* ------------------------------------------------------------------ */
/*  irxanchor.h - ECTENVBK read-mostly anchor for REXX/370            */
/*                                                                    */
/*  A REXX Language Processor Environment is anchored in the TSO ECT  */
/*  at offset +30 (ECTENVBK). rexx370 treats that slot as read-mostly */
/*  per CON-1 §6.1: at any point it is in one of three states —       */
/*                                                                    */
/*    (a) NULL            no REXX environment active; rexx370 may     */
/*                        claim the slot on IRXINIT                   */
/*                                                                    */
/*    (b) non-rexx370     another REXX holds the anchor (on MVS 3.8j  */
/*                        typically BREXX/370, but the slot is also   */
/*                        compatible with any other env). rexx370     */
/*                        leaves the slot untouched and the caller    */
/*                        tracks its own ENVBLOCK via the IRXINIT     */
/*                        return value                                */
/*                                                                    */
/*    (c) our own env     we claimed the slot earlier; IRXTERM may    */
/*                        clear it back to NULL                       */
/*                                                                    */
/*  Consequence: anch_push writes ECTENVBK *only* when it observes    */
/*  state (a), and anch_pop clears it *only* when the slot still      */
/*  equals the environment being terminated. We never save a prior    */
/*  value and we never restore one — there is no chain.               */
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
/*  the walk returns NULL and anch_push / anch_pop reduce to local    */
/*  field updates on the ENVBLOCK.                                    */
/*                                                                    */
/*  Ref: CON-1 §3.1 (ENVBLOCK layout), §6.1 (read-mostly anchor),     */
/*       §6.3 step 8 (IRXINIT), §6.4 step 2 (IRXTERM),                */
/*       §14.2 (20-April decision with coexistence rationale).        */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#ifndef IRXANCHOR_H
#define IRXANCHOR_H

#include "irx.h"

/* c2asm370 truncates snake_case C names to 8 chars with underscores
 * mapped to `@`. Without distinct C-level names, anch_push and
 * anch_pop both collapse to ANCHOR@P and IFOX00 rejects the
 * duplicate ENTRY. asm() attributes are not honored by c2asm370
 * for definitions, so the names below are already distinct at the
 * C level; the asm() overrides just give them tidier MVS symbols. */

/* Cold-path walk PSA -> ASCB -> ASXB -> LWA -> ECT.
 * Returns ECT address, or NULL if any link in the chain is NULL
 * (typical in batch, where LWA is not established). */
void *anch_walk(void) asm("ANCHWALK");

/* TSO detection via crent370's CLIBPPA.ppaflag
 * (PPAFLAG_TSOFG for TSO foreground, PPAFLAG_TSOBG for TSO background
 * invoked via IKJEFT01). Returns 1 when either bit is set, 0 in pure
 * batch and on non-MVS builds. */
int anch_tso(void) asm("ANCHISTS");

/* Read ECTENVBK — the currently installed ENVBLOCK, or NULL when
 * the slot is empty or unreachable. */
struct envblock *anch_curr(void) asm("ANCHCURR");

/* Populate new_env->envblock_ectptr with the ECT address (so later
 * reflection routines have a cheap back-pointer), then claim
 * ECTENVBK = new_env *only if* the slot is currently NULL.
 *
 * If the slot is non-NULL, leave it alone — another REXX already
 * holds the anchor. IRXINIT still succeeds in that case; the caller
 * owns the new ENVBLOCK via the return value.
 *
 * In batch (no ECT reachable) this reduces to setting
 * new_env->envblock_ectptr = NULL. */
void anch_push(struct envblock *new_env) asm("ANCHPUSH");

/* Clear ECTENVBK *only if* the slot currently equals env. Any other
 * value (NULL, some other REXX's ENVBLOCK, a later rexx370 env) is
 * a no-op.
 *
 * In batch this is always a no-op. */
void anch_pop(struct envblock *env) asm("ANCHPOP");

#endif /* IRXANCHOR_H */
