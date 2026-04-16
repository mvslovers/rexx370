/* ------------------------------------------------------------------ */
/*  irxarith.h - REXX/370 Arithmetic Engine (WP-20)                  */
/*                                                                    */
/*  Arbitrary-precision BCD decimal arithmetic per SC28-1883-0 §9.   */
/*  NUMERIC DIGITS controls precision (default 9, max 1000).         */
/*  NUMERIC FUZZ controls comparison tolerance.                       */
/*  NUMERIC FORM controls output format (SCIENTIFIC / ENGINEERING).  */
/*                                                                    */
/*  All allocation goes through irxstor (RXSMGET / RXSMFRE).         */
/*  No globals, no statics — fully reentrant.                         */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXARITH_H
#define IRXARITH_H

#include "irx.h"
#include "lstring.h"

/* ================================================================== */
/*  Operation codes                                                   */
/* ================================================================== */

enum irx_arith_opcode
{
    ARITH_ADD = 0, /* a + b                                        */
    ARITH_SUB,     /* a - b                                        */
    ARITH_MUL,     /* a * b                                        */
    ARITH_DIV,     /* a / b  (normal division — may produce decimal)*/
    ARITH_INTDIV,  /* a % b  (integer division — truncates toward 0)*/
    ARITH_MOD,     /* a // b (remainder — same sign as a)          */
    ARITH_POWER,   /* a ** b (b must be a non-negative integer)    */
    ARITH_NEG      /* -a     (b must be NULL)                      */
};

/* ================================================================== */
/*  Public entry points                                               */
/* ================================================================== */

/* Perform a binary arithmetic operation and write the result.
 *
 *   env     - owning environment (provides NUMERIC settings + allocator)
 *   a       - left operand  (Lstr containing a REXX number string)
 *   b       - right operand (NULL only for ARITH_NEG)
 *   op      - operation code (enum irx_arith_opcode)
 *   result  - output Lstr; caller owns storage; may alias a but NOT b
 *
 * Return codes (from irxpars.h):
 *   IRXPARS_OK      on success
 *   IRXPARS_SYNTAX  if an operand is not a valid REXX number
 *   IRXPARS_DIVZERO if b is zero and op is DIV / INTDIV / MOD
 *   IRXPARS_NOMEM   if storage allocation fails
 */
int irx_arith_op(struct envblock *env,
                 PLstr a, PLstr b,
                 enum irx_arith_opcode op,
                 PLstr result);

/* Compare two REXX numbers with NUMERIC FUZZ applied.
 *
 *   env     - owning environment (provides FUZZ setting + allocator)
 *   a, b    - operands (Lstr containing REXX number strings)
 *   cmp_out - written with -1, 0, or +1  (a < b, a == b, a > b)
 *
 * Return codes:
 *   IRXPARS_OK      on success
 *   IRXPARS_SYNTAX  if either operand is not a valid REXX number
 *   IRXPARS_NOMEM   if storage allocation fails
 */
int irx_arith_compare(struct envblock *env,
                      PLstr a, PLstr b,
                      int *cmp_out);

#endif /* IRXARITH_H */
