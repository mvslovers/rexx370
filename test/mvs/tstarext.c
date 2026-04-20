/* ------------------------------------------------------------------ */
/*  tstarext.c - WP-21b Phase B tests for the new public    */
/*  IRXARITH string-oriented helpers: trunc, format, from_digits,      */
/*  to_digits. These are direct C-API tests; they do not drive the     */
/*  parser.                                                            */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_arith_extended \      */
/*        test/test_arith_extended.c \                                */
/*        'src/irx#'*.c ../lstring370/liblstring.a                     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxarith.h"
#include "irxfunc.h"
#include "irxpars.h"
#include "irxwkblk.h"
#include "lstralloc.h"
#include "lstring.h"

#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        tests_run++;                     \
        if (cond)                        \
        {                                \
            tests_passed++;              \
            printf("  PASS: %s\n", msg); \
        }                                \
        else                             \
        {                                \
            tests_failed++;              \
            printf("  FAIL: %s\n", msg); \
        }                                \
    } while (0)

/* Build a non-owning Lstr from a C string. The returned Lstr borrows
 * the provided buffer; never pass it to Lfree. */
static Lstr make_lstr(const char *cstr)
{
    Lstr s;
    s.pstr = (unsigned char *)(unsigned long)cstr;
    s.len = strlen(cstr);
    s.maxlen = s.len;
    s.type = LSTRING_TY;
    return s;
}

static int lstr_matches(const Lstr *s, const char *expected)
{
    size_t n = strlen(expected);
    return s->pstr != NULL && s->len == n &&
           memcmp(s->pstr, expected, n) == 0;
}

/* ================================================================== */
/*  trunc                                                              */
/* ================================================================== */

static void test_trunc_basic(struct envblock *env)
{
    Lstr in, out;
    printf("\n--- trunc: basic chop/pad ---\n");

    in = make_lstr("12.345");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 2, &out) == IRXPARS_OK,
          "TRUNC('12.345', 2) returns OK");
    CHECK(lstr_matches(&out, "12.34"), "TRUNC('12.345', 2) -> '12.34'");
    Lfree(NULL, &out);

    in = make_lstr("12.345");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 5, &out) == IRXPARS_OK,
          "TRUNC('12.345', 5) returns OK");
    CHECK(lstr_matches(&out, "12.34500"),
          "TRUNC('12.345', 5) pads trailing zeros -> '12.34500'");
    Lfree(NULL, &out);

    in = make_lstr("12345");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 2, &out) == IRXPARS_OK,
          "TRUNC('12345', 2) returns OK");
    CHECK(lstr_matches(&out, "12345.00"),
          "TRUNC('12345', 2) pads integer -> '12345.00'");
    Lfree(NULL, &out);

    in = make_lstr("12.345");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 0, &out) == IRXPARS_OK,
          "TRUNC('12.345', 0) returns OK");
    CHECK(lstr_matches(&out, "12"), "TRUNC('12.345', 0) -> '12'");
    Lfree(NULL, &out);

    in = make_lstr("-12.345");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 1, &out) == IRXPARS_OK,
          "TRUNC('-12.345', 1) returns OK");
    CHECK(lstr_matches(&out, "-12.3"), "TRUNC('-12.345', 1) -> '-12.3'");
    Lfree(NULL, &out);
}

static void test_trunc_zero_and_near_zero(struct envblock *env)
{
    Lstr in, out;
    printf("\n--- trunc: zero and near-zero ---\n");

    in = make_lstr("0");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 2, &out) == IRXPARS_OK,
          "TRUNC('0', 2) returns OK");
    CHECK(lstr_matches(&out, "0.00"), "TRUNC('0', 2) -> '0.00'");
    Lfree(NULL, &out);

    in = make_lstr("-0.9");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 0, &out) == IRXPARS_OK,
          "TRUNC('-0.9', 0) returns OK");
    CHECK(lstr_matches(&out, "0"),
          "TRUNC('-0.9', 0) truncates toward zero, sign cleared");
    Lfree(NULL, &out);

    in = make_lstr("0.001");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 5, &out) == IRXPARS_OK,
          "TRUNC('0.001', 5) returns OK");
    CHECK(lstr_matches(&out, "0.00100"),
          "TRUNC('0.001', 5) -> '0.00100'");
    Lfree(NULL, &out);

    in = make_lstr("0.999");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 1, &out) == IRXPARS_OK,
          "TRUNC('0.999', 1) returns OK");
    CHECK(lstr_matches(&out, "0.9"),
          "TRUNC('0.999', 1) truncates without rounding");
    Lfree(NULL, &out);
}

static void test_trunc_errors(struct envblock *env)
{
    Lstr in, out;
    printf("\n--- trunc: error paths ---\n");

    in = make_lstr("12.345");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, -1, &out) == IRXPARS_SYNTAX,
          "TRUNC with decimals=-1 -> SYNTAX");

    in = make_lstr("abc");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 2, &out) == IRXPARS_SYNTAX,
          "TRUNC on non-numeric input -> SYNTAX");

    in = make_lstr("");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 2, &out) == IRXPARS_SYNTAX,
          "TRUNC on empty string -> SYNTAX");

    /* Over-large decimals would require an absurd scratch buffer;
     * routine must refuse upfront. */
    in = make_lstr("1");
    Lzeroinit(&out);
    CHECK(irx_arith_trunc(env, &in, 100000L, &out) == IRXPARS_SYNTAX,
          "TRUNC with decimals > NUMERIC_DIGITS_MAX -> SYNTAX");
}

/* ================================================================== */
/*  format                                                             */
/* ================================================================== */

static void test_format_spec_examples(struct envblock *env)
{
    Lstr in, out;
    printf("\n--- format: SC28-1883-0 spec examples ---\n");

    /* AC-B2: FORMAT('123.45', 6, 2) -> '   123.45' */
    in = make_lstr("123.45");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, 6, 2, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, &out) == IRXPARS_OK,
          "FORMAT('123.45', 6, 2) returns OK");
    CHECK(lstr_matches(&out, "   123.45"),
          "FORMAT('123.45', 6, 2) -> '   123.45'");
    Lfree(NULL, &out);

    /* FORMAT('3', 4) -> '   3' */
    in = make_lstr("3");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, 4, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, &out) == IRXPARS_OK,
          "FORMAT('3', 4) returns OK");
    CHECK(lstr_matches(&out, "   3"), "FORMAT('3', 4) -> '   3'");
    Lfree(NULL, &out);

    /* FORMAT('-3', 4) -> '  -3' */
    in = make_lstr("-3");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, 4, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, &out) == IRXPARS_OK,
          "FORMAT('-3', 4) returns OK");
    CHECK(lstr_matches(&out, "  -3"), "FORMAT('-3', 4) -> '  -3'");
    Lfree(NULL, &out);
}

static void test_format_after_rounding(struct envblock *env)
{
    Lstr in, out;
    printf("\n--- format: after rounds/pads correctly ---\n");

    /* Round half-up: FORMAT('1.235', ,2) -> '1.24' */
    in = make_lstr("1.235");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, 2, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, &out) == IRXPARS_OK,
          "FORMAT('1.235', ,2) returns OK");
    CHECK(lstr_matches(&out, "1.24"),
          "FORMAT('1.235', ,2) rounds half-up -> '1.24'");
    Lfree(NULL, &out);

    /* Pad fractional: FORMAT('1.2', ,4) -> '1.2000' */
    in = make_lstr("1.2");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, 4, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, &out) == IRXPARS_OK,
          "FORMAT('1.2', ,4) returns OK");
    CHECK(lstr_matches(&out, "1.2000"),
          "FORMAT('1.2', ,4) pads fractional -> '1.2000'");
    Lfree(NULL, &out);

    /* Round propagating: FORMAT('9.995', ,2) -> '10.00' */
    in = make_lstr("9.995");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, 2, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, &out) == IRXPARS_OK,
          "FORMAT('9.995', ,2) returns OK");
    CHECK(lstr_matches(&out, "10.00"),
          "FORMAT('9.995', ,2) carries over -> '10.00'");
    Lfree(NULL, &out);

    /* after=0 drops fractional part completely */
    in = make_lstr("3.9");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, 0, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, &out) == IRXPARS_OK,
          "FORMAT('3.9', ,0) returns OK");
    CHECK(lstr_matches(&out, "4"),
          "FORMAT('3.9', ,0) rounds half-up -> '4'");
    Lfree(NULL, &out);
}

static void test_format_exponential(struct envblock *env)
{
    Lstr in, out;
    printf("\n--- format: exponential form ---\n");

    /* expp=0 forces fixed-point */
    in = make_lstr("1.23E5");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           0, IRX_FORMAT_OMIT, &out) == IRXPARS_OK,
          "FORMAT('1.23E5', ,,0) returns OK");
    CHECK(lstr_matches(&out, "123000"),
          "FORMAT('1.23E5', ,,0) forces fixed-point -> '123000'");
    Lfree(NULL, &out);

    /* Force-fixed with a large but representable exponent (regression
     * test for the int_part sizing fix). 1E50 must yield 51 chars. */
    in = make_lstr("1E50");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           0, IRX_FORMAT_OMIT, &out) == IRXPARS_OK,
          "FORMAT('1E50', ,,0) returns OK");
    CHECK(out.len == 51 && out.pstr != NULL && out.pstr[0] == '1',
          "FORMAT('1E50', ,,0) emits 51 chars starting with '1'");
    if (out.pstr != NULL)
    {
        int all_zeros_after = 1;
        size_t k;
        for (k = 1; k < out.len; k++)
        {
            if (out.pstr[k] != '0')
            {
                all_zeros_after = 0;
                break;
            }
        }
        CHECK(all_zeros_after,
              "FORMAT('1E50', ,,0) -> '1' followed by 50 zeros");
    }
    Lfree(NULL, &out);

    /* Force-fixed with an exponent above NUMERIC_DIGITS_MAX — must
     * be rejected, not silently attempt a multi-KB allocation. */
    in = make_lstr("1E1001");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           0, IRX_FORMAT_OMIT, &out) == IRXPARS_SYNTAX,
          "FORMAT('1E1001', ,,0) -> SYNTAX (exceeds NUMERIC_DIGITS_MAX)");

    /* expt triggers exponential form when |adj| > expt */
    in = make_lstr("12345");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, 2, &out) == IRXPARS_OK,
          "FORMAT('12345', ,,,2) returns OK");
    CHECK(lstr_matches(&out, "1.2345E+4"),
          "FORMAT('12345', ,,,2) -> '1.2345E+4'");
    Lfree(NULL, &out);

    /* Negative exponent */
    in = make_lstr("0.00012");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, 2, &out) == IRXPARS_OK,
          "FORMAT('0.00012', ,,,2) returns OK");
    CHECK(lstr_matches(&out, "1.2E-4"),
          "FORMAT('0.00012', ,,,2) -> '1.2E-4'");
    Lfree(NULL, &out);

    /* expp=3 zero-pads exponent */
    in = make_lstr("12345");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           3, 2, &out) == IRXPARS_OK,
          "FORMAT('12345', ,,3,2) returns OK");
    CHECK(lstr_matches(&out, "1.2345E+004"),
          "FORMAT('12345', ,,3,2) zero-pads to 3 exponent digits");
    Lfree(NULL, &out);
}

static void test_format_errors(struct envblock *env)
{
    Lstr in, out;
    printf("\n--- format: error paths ---\n");

    /* Negative args -> SYNTAX */
    in = make_lstr("1");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, -2, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, &out) == IRXPARS_SYNTAX,
          "FORMAT with before<0 (but != OMIT) -> SYNTAX");

    in = make_lstr("1");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, -2, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, &out) == IRXPARS_SYNTAX,
          "FORMAT with after<0 -> SYNTAX");

    /* before too narrow -> SYNTAX */
    in = make_lstr("123");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, 2, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, &out) == IRXPARS_SYNTAX,
          "FORMAT('123', 2) -> SYNTAX (integer part > before)");

    /* expp too small to hold exponent -> SYNTAX */
    in = make_lstr("1E100");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           1, 2, &out) == IRXPARS_SYNTAX,
          "FORMAT('1E100', ,,1,2) -> SYNTAX (exponent needs 3 digits)");

    /* Non-numeric input */
    in = make_lstr("abc");
    Lzeroinit(&out);
    CHECK(irx_arith_format(env, &in, IRX_FORMAT_OMIT, IRX_FORMAT_OMIT,
                           IRX_FORMAT_OMIT, IRX_FORMAT_OMIT, &out) ==
              IRXPARS_SYNTAX,
          "FORMAT on non-numeric input -> SYNTAX");
}

/* ================================================================== */
/*  from_digits / to_digits round-trip                                */
/* ================================================================== */

static void test_from_digits_basic(struct envblock *env)
{
    Lstr out;
    const char d12345[] = {1, 2, 3, 4, 5};
    const char d5[] = {5, 0};
    const char zeros[] = {0, 0, 0};

    printf("\n--- from_digits: basic ---\n");

    Lzeroinit(&out);
    CHECK(irx_arith_from_digits(env, d12345, 5, 0, 0, &out) == IRXPARS_OK,
          "from_digits([1,2,3,4,5], sign=0, exp=0) OK");
    CHECK(lstr_matches(&out, "12345"),
          "from_digits -> '12345'");
    Lfree(NULL, &out);

    Lzeroinit(&out);
    CHECK(irx_arith_from_digits(env, d12345, 5, 0, -2, &out) == IRXPARS_OK,
          "from_digits([1,2,3,4,5], sign=0, exp=-2) OK");
    CHECK(lstr_matches(&out, "123.45"),
          "from_digits with exp=-2 -> '123.45'");
    Lfree(NULL, &out);

    Lzeroinit(&out);
    CHECK(irx_arith_from_digits(env, d5, 2, 1, 0, &out) == IRXPARS_OK,
          "from_digits([5,0], sign=1, exp=0) OK");
    CHECK(lstr_matches(&out, "-50"),
          "from_digits negative -> '-50'");
    Lfree(NULL, &out);

    Lzeroinit(&out);
    CHECK(irx_arith_from_digits(env, NULL, 0, 0, 0, &out) == IRXPARS_OK,
          "from_digits empty digits is canonical zero OK");
    CHECK(lstr_matches(&out, "0"), "from_digits empty -> '0'");
    Lfree(NULL, &out);

    Lzeroinit(&out);
    CHECK(irx_arith_from_digits(env, zeros, 3, 0, 0, &out) == IRXPARS_OK,
          "from_digits all-zero digits OK");
    CHECK(lstr_matches(&out, "0"),
          "from_digits([0,0,0]) -> '0'");
    Lfree(NULL, &out);

    /* Leading zeros get stripped */
    {
        const char lead[] = {0, 0, 1, 2};
        Lzeroinit(&out);
        CHECK(irx_arith_from_digits(env, lead, 4, 0, 0, &out) == IRXPARS_OK,
              "from_digits with leading zeros OK");
        CHECK(lstr_matches(&out, "12"),
              "from_digits strips leading zeros -> '12'");
        Lfree(NULL, &out);
    }

    /* Trailing zeros get normalized into the exponent. */
    {
        const char trail[] = {1, 2, 0, 0};
        Lzeroinit(&out);
        CHECK(irx_arith_from_digits(env, trail, 4, 0, 0, &out) == IRXPARS_OK,
              "from_digits with trailing zeros OK");
        CHECK(lstr_matches(&out, "1200"),
              "from_digits normalizes trailing zeros -> '1200'");
        Lfree(NULL, &out);
    }
}

static void test_from_digits_errors(struct envblock *env)
{
    Lstr out;
    const char bad[] = {1, 2, 10, 3};
    printf("\n--- from_digits: errors ---\n");

    Lzeroinit(&out);
    CHECK(irx_arith_from_digits(env, bad, 4, 0, 0, &out) == IRXPARS_SYNTAX,
          "from_digits with invalid digit byte (>9) -> SYNTAX");

    Lzeroinit(&out);
    CHECK(irx_arith_from_digits(env, NULL, -1, 0, 0, &out) == IRXPARS_SYNTAX,
          "from_digits with negative len -> SYNTAX");

    Lzeroinit(&out);
    CHECK(irx_arith_from_digits(env, "\x01", 1, 2, 0, &out) == IRXPARS_SYNTAX,
          "from_digits with invalid sign (!=0,1) -> SYNTAX");
}

static void test_to_digits_basic(struct envblock *env)
{
    Lstr in;
    char digits[16];
    int digits_len, sign;
    long expo;
    int rc;

    printf("\n--- to_digits: basic ---\n");

    in = make_lstr("12345");
    memset(digits, 0, sizeof(digits));
    rc = irx_arith_to_digits(env, &in, digits, (int)sizeof(digits),
                             &digits_len, &sign, &expo);
    CHECK(rc == IRXPARS_OK, "to_digits('12345') OK");
    CHECK(digits_len == 5 && sign == 0 && expo == 0 &&
              digits[0] == 1 && digits[1] == 2 && digits[2] == 3 &&
              digits[3] == 4 && digits[4] == 5,
          "to_digits('12345') -> digits=[1,2,3,4,5] sign=0 exp=0");

    in = make_lstr("-123.45");
    memset(digits, 0, sizeof(digits));
    rc = irx_arith_to_digits(env, &in, digits, (int)sizeof(digits),
                             &digits_len, &sign, &expo);
    CHECK(rc == IRXPARS_OK, "to_digits('-123.45') OK");
    CHECK(digits_len == 5 && sign == 1 && expo == -2,
          "to_digits('-123.45') -> 5 digits, sign=1, exp=-2");
    CHECK(digits[0] == 1 && digits[1] == 2 && digits[2] == 3 &&
              digits[3] == 4 && digits[4] == 5,
          "digits = [1,2,3,4,5]");

    in = make_lstr("0");
    memset(digits, 0, sizeof(digits));
    rc = irx_arith_to_digits(env, &in, digits, (int)sizeof(digits),
                             &digits_len, &sign, &expo);
    CHECK(rc == IRXPARS_OK && digits_len == 1 && digits[0] == 0 &&
              sign == 0 && expo == 0,
          "to_digits('0') -> canonical zero");
}

static void test_to_digits_errors(struct envblock *env)
{
    Lstr in;
    char digits[2];
    int digits_len, sign;
    long expo;
    int rc;

    printf("\n--- to_digits: errors ---\n");

    in = make_lstr("12345");
    rc = irx_arith_to_digits(env, &in, digits, (int)sizeof(digits),
                             &digits_len, &sign, &expo);
    CHECK(rc == IRXPARS_OVERFLOW,
          "to_digits('12345') with cap=2 -> OVERFLOW");

    in = make_lstr("abc");
    rc = irx_arith_to_digits(env, &in, digits, (int)sizeof(digits),
                             &digits_len, &sign, &expo);
    CHECK(rc == IRXPARS_SYNTAX, "to_digits('abc') -> SYNTAX");
}

static void test_roundtrip(struct envblock *env)
{
    Lstr out;
    char digits[16];
    int len, sign;
    long expo;
    int rc;
    int i;

    printf("\n--- roundtrip: from_digits ↔ to_digits ---\n");

    /* Build via from_digits, parse back via to_digits, compare. */
    const struct
    {
        char digits[8];
        int len;
        int sign;
        long exp;
    } cases[] = {
        {{1}, 1, 0, 0},                      /* '1' */
        {{1, 2, 3}, 3, 0, 0},                /* '123' */
        {{1, 2, 3}, 3, 0, -1},               /* '12.3' */
        {{1, 2, 3}, 3, 1, 0},                /* '-123' */
        {{5}, 1, 0, 3},                      /* '5000' */
        {{1, 0, 0, 2}, 4, 0, 0},             /* '1002' */
        {{9, 9, 9, 9, 9}, 5, 0, -2},         /* '999.99' */
        {{1, 2, 3, 4, 5, 6, 7, 8}, 8, 0, -4} /* '1234.5678' */
    };

    int nc = (int)(sizeof(cases) / sizeof(cases[0]));

    for (i = 0; i < nc; i++)
    {
        char label[64];
        Lzeroinit(&out);
        rc = irx_arith_from_digits(env, cases[i].digits, cases[i].len,
                                   cases[i].sign, cases[i].exp, &out);
        snprintf(label, sizeof(label),
                 "roundtrip case %d: from_digits OK", i);
        CHECK(rc == IRXPARS_OK, label);

        rc = irx_arith_to_digits(env, &out, digits, (int)sizeof(digits),
                                 &len, &sign, &expo);
        snprintf(label, sizeof(label),
                 "roundtrip case %d: to_digits OK", i);
        CHECK(rc == IRXPARS_OK, label);

        /* The round-trip is canonical, so sign matches unless the value
         * is zero. Exponent and digits may be normalized (leading /
         * trailing zeros stripped). For each test case above, the input
         * is already canonical. */
        snprintf(label, sizeof(label),
                 "roundtrip case %d: digits/sign/exp preserved", i);
        CHECK(len == cases[i].len && sign == cases[i].sign &&
                  expo == cases[i].exp &&
                  memcmp(digits, cases[i].digits, (size_t)len) == 0,
              label);

        Lfree(NULL, &out);
    }
}

static void test_large_value(struct envblock *env)
{
    Lstr out;
    const char big[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
    char digits[32];
    int len, sign;
    long expo;
    int rc;

    printf("\n--- large value support ---\n");

    /* 10 digits — exceeds safe-long range per the issue description. */
    Lzeroinit(&out);
    rc = irx_arith_from_digits(env, big, 10, 0, 0, &out);
    CHECK(rc == IRXPARS_OK,
          "from_digits with 10 digits OK");
    /* NUMERIC DIGITS default is 9, so output is rounded to 9 digits +
     * exponential form. The important thing is that from_digits does
     * not choke on wide inputs. */

    /* Round-trip the same 10-digit sequence explicitly (NUMERIC DIGITS
     * rounding will lose the trailing zero, so we inspect differently). */
    rc = irx_arith_to_digits(env, &out, digits, (int)sizeof(digits),
                             &len, &sign, &expo);
    CHECK(rc == IRXPARS_OK && len > 0 && len <= 10,
          "to_digits of a large from_digits result succeeds");
    Lfree(NULL, &out);

    /* AC-B3: from_digits('1234567890123', 13, positive, 0, ...) must
     * produce an Lstr that IRXARITH can consume again. With the default
     * NUMERIC DIGITS of 9 the result is rounded, but the produced Lstr
     * must still be a valid REXX number — we assert that by calling
     * to_digits on it. */
    {
        const char d13[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3};
        Lzeroinit(&out);
        rc = irx_arith_from_digits(env, d13, 13, 0, 0, &out);
        CHECK(rc == IRXPARS_OK, "AC-B3: from_digits with 13 digits OK");
        CHECK(out.len > 0, "AC-B3: output Lstr non-empty");
        rc = irx_arith_to_digits(env, &out, digits, (int)sizeof(digits),
                                 &len, &sign, &expo);
        CHECK(rc == IRXPARS_OK,
              "AC-B3: output is a valid REXX number (to_digits OK)");
        Lfree(NULL, &out);
    }
}

/* ================================================================== */
/*  ARITH_ABS (new opcode in irx_arith_op)                            */
/* ================================================================== */

static void test_arith_abs(struct envblock *env)
{
    Lstr in, out;
    printf("\n--- ARITH_ABS opcode ---\n");

    in = make_lstr("-5.5");
    Lzeroinit(&out);
    CHECK(irx_arith_op(env, &in, NULL, ARITH_ABS, &out) == IRXPARS_OK,
          "arith_op(ABS, '-5.5') OK");
    CHECK(lstr_matches(&out, "5.5"), "ABS(-5.5) -> '5.5'");
    Lfree(NULL, &out);

    in = make_lstr("42");
    Lzeroinit(&out);
    CHECK(irx_arith_op(env, &in, NULL, ARITH_ABS, &out) == IRXPARS_OK,
          "arith_op(ABS, '42') OK");
    CHECK(lstr_matches(&out, "42"), "ABS(42) -> '42' (unchanged)");
    Lfree(NULL, &out);

    in = make_lstr("0");
    Lzeroinit(&out);
    CHECK(irx_arith_op(env, &in, NULL, ARITH_ABS, &out) == IRXPARS_OK,
          "arith_op(ABS, '0') OK");
    CHECK(lstr_matches(&out, "0"), "ABS(0) -> '0'");
    Lfree(NULL, &out);
}

/* ================================================================== */
/*  main                                                              */
/* ================================================================== */

int main(void)
{
    struct envblock *env = NULL;
    if (irxinit(NULL, &env) != 0)
    {
        printf("FATAL: irxinit failed\n");
        return 1;
    }

    test_trunc_basic(env);
    test_trunc_zero_and_near_zero(env);
    test_trunc_errors(env);

    test_format_spec_examples(env);
    test_format_after_rounding(env);
    test_format_exponential(env);
    test_format_errors(env);

    test_from_digits_basic(env);
    test_from_digits_errors(env);
    test_to_digits_basic(env);
    test_to_digits_errors(env);
    test_roundtrip(env);
    test_large_value(env);

    test_arith_abs(env);

    irxterm(env);

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}
