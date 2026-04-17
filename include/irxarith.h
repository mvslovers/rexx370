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
/*  Formatting constants (SC28-1883-0 §9.4)                          */
/* ================================================================== */

#define IRX_FIXED_POINT_MIN_EXP -5 /* lower bound for fixed-point  */
#define IRX_ENG_EXP_MULTIPLE    3  /* engineering exponent divisor  */

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
    ARITH_NEG,     /* -a     (b must be NULL)                      */
    ARITH_ABS      /* |a|    (b must be NULL)                      */
};

/* ================================================================== */
/*  Public entry points                                               */
/*                                                                    */
/*  asm() aliases are required because irx_arith_op and               */
/*  irx_arith_compare share the first 8 characters ("irx_arit") and   */
/*  would collide under c2asm370's 8-character identifier truncation. */
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
                 PLstr result) asm("IRXARIOP");

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
                      int *cmp_out) asm("IRXARICM");

/* ================================================================== */
/*  String-oriented helpers (WP-21b Phase B)                         */
/*                                                                    */
/*  These functions extend the IRXARITH public surface with the       */
/*  building blocks that the numeric and conversion BIFs will need    */
/*  in later phases. They take and return plain Lstr — struct         */
/*  irx_number stays internal to irx#arith.c. All signatures use      */
/*  struct envblock * for consistency with irx_arith_op /             */
/*  irx_arith_compare; the parser-side wk is reachable through        */
/*  env->envblock_userfield when needed.                              */
/* ================================================================== */

/* TRUNC(number, decimals): truncate (no rounding) to exactly
 * `decimals` fractional digits. Pads trailing zeros when the input
 * has fewer fractional digits than requested. `decimals` must be
 * non-negative; negative values produce IRXPARS_SYNTAX.
 *
 * Examples:
 *   TRUNC('12.345', 2)  -> '12.34'
 *   TRUNC('12.345', 5)  -> '12.34500'    (padded)
 *   TRUNC('12345',  2)  -> '12345.00'    (padded)
 *   TRUNC('12.345', 0)  -> '12'          (no decimal point)
 *   TRUNC('-0.9',   0)  -> '0'           (truncate toward zero)
 */
int irx_arith_trunc(struct envblock *env,
                    PLstr in, long decimals,
                    PLstr result) asm("IRXARITR");

/* FORMAT(number [, before [, after [, expp [, expt]]]]): SC28-1883-0
 * §4 FORMAT built-in. Any of the four integer arguments may be
 * negative to mean "omitted" — callers pass IRX_FORMAT_OMIT (-1) for
 * optional args that were not supplied by the REXX user. When
 * supplied, each integer must be non-negative (negatives raise
 * IRXPARS_SYNTAX inside this routine).
 *
 *   before  width of the integer part (space-padded on the left)
 *   after   fractional digit count (rounded/padded)
 *   expp    exponent digit count (0 forces fixed-point notation)
 *   expt    exponent threshold (|adj_exp| > expt -> exponential)
 *
 * Non-zero return codes are IRXPARS_SYNTAX, IRXPARS_OVERFLOW or
 * IRXPARS_NOMEM per the usual convention.
 */
#define IRX_FORMAT_OMIT (-1L)

int irx_arith_format(struct envblock *env,
                     PLstr in,
                     long before, long after, long expp, long expt,
                     PLstr result) asm("IRXARIFM");

/* Build a REXX number Lstr from raw BCD input. `digits[0..len-1]`
 * contains values 0..9 (not ASCII/EBCDIC characters — raw decimal
 * values). `sign` is 0 (non-negative) or 1 (negative). `exponent`
 * places the least-significant digit: value = sign * digits * 10^exp
 * where `digits` is the integer formed by concatenating the byte
 * values. Empty input (len == 0) is treated as zero.
 *
 * The result is normalized (leading / trailing zero stripped) and
 * rendered via the same formatter used by irx_arith_op, honouring
 * the current NUMERIC DIGITS and NUMERIC FORM settings.
 */
int irx_arith_from_digits(struct envblock *env,
                          const char *digits, int digits_len,
                          int sign, long exponent,
                          PLstr result) asm("IRXARIFD");

/* Parse a REXX number Lstr into raw BCD output. The caller supplies
 * `digits_cap` bytes of scratch; on success `*digits_len` is set to
 * the number of significant digits written (0..digits_cap). Digits
 * are raw 0..9 values, MSB first. `*sign`, `*exponent` receive the
 * same fields as struct irx_number.
 *
 * Returns IRXPARS_SYNTAX if `in` is not a valid REXX number, or
 * IRXPARS_OVERFLOW if the significant-digit count exceeds
 * `digits_cap`. IRXPARS_NOMEM if the internal parse buffer cannot
 * be allocated.
 */
int irx_arith_to_digits(struct envblock *env,
                        PLstr in,
                        char *digits, int digits_cap, int *digits_len,
                        int *sign, long *exponent) asm("IRXARITD");

#endif /* IRXARITH_H */
