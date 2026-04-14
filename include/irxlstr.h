/* ------------------------------------------------------------------ */
/*  irxlstr.h - REXX/370 <-> lstring370 adapter                       */
/*                                                                    */
/*  Thin layer that bridges lstring370 into the REXX interpreter:     */
/*                                                                    */
/*  - Allocator bridge: injects irxstor as the lstring370 allocator,  */
/*    routing every Lfx/Lfree through the per-environment storage    */
/*    management replaceable routine.                                  */
/*                                                                    */
/*  - REXX type caching: extends Lstr.type with LINTEGER_TY and       */
/*    LREAL_TY so _Lisnum() can return a cached answer on repeated    */
/*    queries of the same (unmodified) string.                        */
/*                                                                    */
/*  - REXX numeric detection: _Lisnum() checks if a string is a       */
/*    valid REXX number per SC28-1883-0 Chapter 6.                    */
/*                                                                    */
/*  - DATATYPE() support: irx_datatype() implements the REXX          */
/*    DATATYPE() built-in classification.                             */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 3 (Variables), Chapter 6 (Numbers)      */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef __IRXLSTR_H__
#define __IRXLSTR_H__

#include "irx.h"
#include "lstring.h"

/* ================================================================== */
/*  Extended type tags for Lstr.type                                  */
/*                                                                    */
/*  lstring370 defines LSTRING_TY == 0 in <lstring.h>. The REXX       */
/*  adapter extends this with cached numeric classifications.         */
/*                                                                    */
/*  IMPORTANT: cache validity. lstring370 does not reset Lstr.type    */
/*  on mutation (Lscpy, Lcat, Lstrcpy, Lstrcat do not touch the       */
/*  type field). Callers that mutate a string after classifying it    */
/*  MUST manually reset s->type to LSTRING_TY before re-querying      */
/*  _Lisnum() / irx_datatype().                                       */
/* ================================================================== */

#define LINTEGER_TY   1   /* validated as an integer literal          */
#define LREAL_TY      2   /* validated as a real / exponent literal   */

/* ================================================================== */
/*  _Lisnum() result values                                           */
/* ================================================================== */

#define LNUM_NOT_NUM  0   /* not a valid REXX number                  */
#define LNUM_INTEGER  1   /* valid REXX integer (no dot, no exponent) */
#define LNUM_REAL     2   /* valid REXX real (has dot or exponent)    */

/* ================================================================== */
/*  Public API                                                        */
/* ================================================================== */

/* Initialise (or return) the REXX allocator for an environment.
 *
 * On first call for a given envblock, allocates a struct lstr_alloc
 * via irxstor, fills it with the rexx_lstr_alloc / rexx_lstr_free
 * bridges (ctx = envblock), and stores it in wkbi_lstr_alloc. On
 * subsequent calls, returns the cached pointer without reallocating.
 *
 * Returns NULL if envblock is invalid or the underlying irxstor
 * call fails.
 *
 * Ownership: the allocator struct is owned by the envblock's work
 * block and is freed by irxterm().
 */
struct lstr_alloc *irx_lstr_init(struct envblock *envblock);

/* Check if s contains a valid REXX number literal.
 *
 * Accepts:
 *   - Optional leading / trailing whitespace
 *   - Optional sign (+ / -)
 *   - digits [ . digits ] OR  . digits
 *   - Optional exponent  E [ + / - ] digits
 *
 * Returns LNUM_NOT_NUM, LNUM_INTEGER, or LNUM_REAL. On a successful
 * classification, caches the result in s->type (LINTEGER_TY or
 * LREAL_TY). On a failed classification, leaves s->type untouched.
 *
 * If s->type is already LINTEGER_TY or LREAL_TY on entry, returns
 * the cached value without re-scanning.
 */
int _Lisnum(PLstr s);

/* REXX DATATYPE(string [, type]) built-in.
 *
 *   option  Meaning                           Accepted chars
 *   ------  --------------------------------  ----------------------
 *   '\0'    no-option form: NUM / not NUM     (returns 1 iff _Lisnum)
 *   'N'     NUM                                same as '\0'
 *   'A'     alphanumeric                       letters + digits
 *   'B'     binary                             '0' | '1'
 *   'L'     lowercase alphabetic only          a-z
 *   'M'     mixed / any alphabetic             a-z | A-Z
 *   'S'     symbol characters                  letters digits _ @ # $ ? ! .
 *   'U'     uppercase alphabetic only          A-Z
 *   'W'     whole number                       integer with no frac part
 *   'X'     hex digits (blanks permitted)      0-9 a-f A-F + blanks
 *
 * Per SC28-1883-0: DATATYPE returns 1 only if the string is non-empty
 * and every character satisfies the option. (Option 'W' also accepts
 * leading sign.) Returns 0 for empty strings and any mismatch.
 *
 * Unknown option characters return 0.
 */
int irx_datatype(PLstr s, char option);

#endif /* __IRXLSTR_H__ */
