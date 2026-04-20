/* ------------------------------------------------------------------ */
/*  test_arith.c - WP-20 Arithmetic Engine end-to-end tests           */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_arith \               */
/*        test/test_arith.c \                                         */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \       */
/*        'src/irx#anch.c'  'src/irx#uid.c'  'src/irx#msid.c' \       */
/*        'src/irx#io.c'   'src/irx#lstr.c' 'src/irx#tokn.c' \       */
/*        'src/irx#vpol.c' 'src/irx#pars.c' 'src/irx#ctrl.c' \       */
/*        'src/irx#exec.c' 'src/irx#arith.c' \                        */
/*        ../lstring370/src/'lstr#cor.c'                              */
/*                                                                    */
/*  Each test calls irx_exec_run() with REXX source and checks        */
/*  output and return codes via a mock I/O routine.                   */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxwkblk.h"
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

/* ------------------------------------------------------------------ */
/*  Mock I/O                                                          */
/* ------------------------------------------------------------------ */

#define OUT_BUF_SIZE 8192

static char g_out[OUT_BUF_SIZE];
static int g_out_len = 0;

static void reset_output(void)
{
    g_out_len = 0;
    g_out[0] = '\0';
}

static int output_contains(const char *line)
{
    int line_len = (int)strlen(line);
    int i;
    for (i = 0; i <= g_out_len - line_len; i++)
    {
        if (memcmp(g_out + i, line, (size_t)line_len) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int mock_io(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;
    if (function == RXFWRITE && data != NULL && data->pstr != NULL &&
        data->len > 0)
    {
        int n = (int)data->len;
        if (g_out_len + n + 1 < OUT_BUF_SIZE)
        {
            memcpy(g_out + g_out_len, data->pstr, (size_t)n);
            g_out_len += n;
            g_out[g_out_len++] = '\n';
            g_out[g_out_len] = '\0';
        }
    }
    return 0;
}

static void install_mock(struct envblock *env)
{
    struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)mock_io;
    }
}

static int run_with_mock(const char *src, int *exit_rc_out)
{
    struct envblock *env = NULL;
    int rc;

    rc = irxinit(NULL, &env);
    if (rc != 0)
    {
        return rc;
    }
    install_mock(env);
    reset_output();
    rc = irx_exec_run(src, (int)strlen(src), NULL, 0, exit_rc_out, env);
    irxterm(env);
    return rc;
}

/* Convenience: run and check a SAY output */
static void check_say(const char *label, const char *src,
                      const char *expected_output, int expected_rc)
{
    int exit_rc = -1;
    int rc;
    char msgbuf[256];

    printf("\n--- %s ---\n", label);
    rc = run_with_mock(src, &exit_rc);

    snprintf(msgbuf, sizeof(msgbuf), "exec rc=0");
    CHECK(rc == 0, msgbuf);
    snprintf(msgbuf, sizeof(msgbuf), "exit rc=%d", expected_rc);
    CHECK(exit_rc == expected_rc, msgbuf);
    if (expected_output != NULL)
    {
        snprintf(msgbuf, sizeof(msgbuf), "output: '%s'", expected_output);
        CHECK(output_contains(expected_output), msgbuf);
    }
}

/* ------------------------------------------------------------------ */
/*  Test cases                                                        */
/* ------------------------------------------------------------------ */

static void test_ar1_add_integers(void)
{
    check_say("AR#1: integer addition",
              "say 2 + 3\n"
              "exit 0\n",
              "5", 0);
}

static void test_ar2_subtract(void)
{
    check_say("AR#2: subtraction",
              "say 10 - 3\n"
              "exit 0\n",
              "7", 0);
}

static void test_ar3_multiply(void)
{
    check_say("AR#3: multiplication",
              "say 6 * 7\n"
              "exit 0\n",
              "42", 0);
}

static void test_ar4_divide_exact(void)
{
    check_say("AR#4: exact division",
              "say 15 / 5\n"
              "exit 0\n",
              "3", 0);
}

static void test_ar5_divide_fraction(void)
{
    check_say("AR#5: division producing decimal",
              "say 10 / 4\n"
              "exit 0\n",
              "2.5", 0);
}

static void test_ar6_integer_divide(void)
{
    check_say("AR#6: integer division (%)",
              "say 10 % 3\n"
              "exit 0\n",
              "3", 0);
}

static void test_ar7_remainder(void)
{
    check_say("AR#7: remainder (//)",
              "say 10 // 3\n"
              "exit 0\n",
              "1", 0);
}

static void test_ar8_power(void)
{
    check_say("AR#8: power (**)",
              "say 2 ** 10\n"
              "exit 0\n",
              "1024", 0);
}

static void test_ar9_unary_neg(void)
{
    check_say("AR#9: unary negation",
              "x = -5\n"
              "say x\n"
              "exit 0\n",
              "-5", 0);
}

static void test_ar10_negative_arithmetic(void)
{
    check_say("AR#10: negative + positive",
              "say -3 + 5\n"
              "exit 0\n",
              "2", 0);
}

static void test_ar11_decimal_add(void)
{
    check_say("AR#11: decimal addition",
              "say 1.5 + 2.5\n"
              "exit 0\n",
              "4", 0);
}

static void test_ar12_decimal_mul(void)
{
    check_say("AR#12: decimal multiply",
              "say 0.1 * 3\n"
              "exit 0\n",
              "0.3", 0);
}

static void test_ar13_large_add(void)
{
    /* 1111111110 has 10 digits; set DIGITS=10 so it is not rounded. */
    check_say("AR#13: large integer addition",
              "numeric digits 10\n"
              "say 123456789 + 987654321\n"
              "exit 0\n",
              "1111111110", 0);
}

static void test_ar14_divide_third(void)
{
    check_say("AR#14: 1/3 with default DIGITS=9",
              "say 1 / 3\n"
              "exit 0\n",
              "0.333333333", 0);
}

static void test_ar15_numeric_digits(void)
{
    check_say("AR#15: NUMERIC DIGITS reduces precision",
              "numeric digits 3\n"
              "say 1 / 3\n"
              "exit 0\n",
              "0.333", 0);
}

static void test_ar16_numeric_digits_large(void)
{
    check_say("AR#16: NUMERIC DIGITS 6 for pi approximation",
              "numeric digits 6\n"
              "say 355 / 113\n"
              "exit 0\n",
              "3.14159", 0);
}

static void test_ar17_do_loop_sum(void)
{
    int exit_rc = -1;
    int rc;
    printf("\n--- AR#17: DO loop accumulation ---\n");
    rc = run_with_mock(
        "sum = 0\n"
        "do i = 1 to 100\n"
        "  sum = sum + i\n"
        "end\n"
        "exit sum\n",
        &exit_rc);
    CHECK(rc == 0, "exec rc=0");
    CHECK(exit_rc == 5050, "sum 1..100 = 5050");
}

static void test_ar18_comparison_equal(void)
{
    check_say("AR#18: numeric = comparison (1.0 = 1)",
              "if 1.0 = 1 then\n"
              "  say 'equal'\n"
              "else\n"
              "  say 'notequal'\n"
              "exit 0\n",
              "equal", 0);
}

static void test_ar19_comparison_gt(void)
{
    check_say("AR#19: numeric > comparison",
              "if 10 > 9.9 then\n"
              "  say 'greater'\n"
              "else\n"
              "  say 'notgreater'\n"
              "exit 0\n",
              "greater", 0);
}

static void test_ar20_comparison_lt(void)
{
    check_say("AR#20: numeric < comparison",
              "if 3.14 < 4 then\n"
              "  say 'less'\n"
              "else\n"
              "  say 'notless'\n"
              "exit 0\n",
              "less", 0);
}

static void test_ar21_divzero(void)
{
    int exit_rc = -1;
    int rc;
    printf("\n--- AR#21: divide by zero returns non-zero ---\n");
    rc = run_with_mock(
        "say 1 / 0\n"
        "exit 0\n",
        &exit_rc);
    CHECK(rc != 0, "exec rc != 0 on divide by zero");
}

static void test_ar22_precedence(void)
{
    check_say("AR#22: operator precedence (2 + 3 * 4 = 14)",
              "say 2 + 3 * 4\n"
              "exit 0\n",
              "14", 0);
}

static void test_ar23_parens(void)
{
    check_say("AR#23: parentheses override precedence",
              "say (2 + 3) * 4\n"
              "exit 0\n",
              "20", 0);
}

static void test_ar24_power_zero(void)
{
    check_say("AR#24: anything ** 0 = 1",
              "say 999 ** 0\n"
              "exit 0\n",
              "1", 0);
}

static void test_ar25_power_one(void)
{
    check_say("AR#25: x ** 1 = x",
              "say 42 ** 1\n"
              "exit 0\n",
              "42", 0);
}

static void test_ar26_string_concat_unchanged(void)
{
    /* Verify concatenation still works alongside BCD arithmetic. */
    check_say("AR#26: string concat still works",
              "x = 'hello'\n"
              "y = 'world'\n"
              "say x y\n"
              "exit 0\n",
              "hello world", 0);
}

static void test_ar27_mixed_expr(void)
{
    check_say("AR#27: mixed arithmetic in expression",
              "x = 2 ** 3 + 10 / 2 - 1\n"
              "say x\n"
              "exit 0\n",
              "12", 0);
}

static void test_ar28_negative_result(void)
{
    check_say("AR#28: subtraction giving negative result",
              "say 3 - 7\n"
              "exit 0\n",
              "-4", 0);
}

static void test_ar29_remainder_neg(void)
{
    /* REXX // remainder has same sign as dividend (left operand) */
    check_say("AR#29: remainder of 7 // 3 = 1",
              "say 7 // 3\n"
              "exit 0\n",
              "1", 0);
}

static void test_ar30_numeric_form_scientific(void)
{
    check_say("AR#30: NUMERIC FORM SCIENTIFIC (default)",
              "numeric digits 4\n"
              "numeric form scientific\n"
              "say 1234567\n"
              "exit 0\n",
              "1234567", 0);
}

/* ------------------------------------------------------------------ */
/*  AR#31–33: NUMERIC FUZZ                                            */
/* ------------------------------------------------------------------ */

static void test_ar31_fuzz_equal(void)
{
    check_say("AR#31: FUZZ comparison treats near-equal as equal",
              "numeric digits 5\n"
              "numeric fuzz 2\n"
              "if 1.23456 = 1.23455 then\n"
              "  say 'equal'\n"
              "else\n"
              "  say 'notequal'\n"
              "exit 0\n",
              "equal", 0);
}

static void test_ar32_strict_ignores_fuzz(void)
{
    check_say("AR#32: strict == ignores FUZZ",
              "numeric digits 5\n"
              "numeric fuzz 2\n"
              "if 1.23456 == 1.23455 then\n"
              "  say 'equal'\n"
              "else\n"
              "  say 'notequal'\n"
              "exit 0\n",
              "notequal", 0);
}

static void test_ar33_fuzz_ge_digits(void)
{
    int exit_rc = -1;
    int rc;
    printf("\n--- AR#33: FUZZ >= DIGITS raises SYNTAX ---\n");
    rc = run_with_mock(
        "numeric digits 5\n"
        "numeric fuzz 9\n"
        "say 'shouldnotreach'\n"
        "exit 0\n",
        &exit_rc);
    CHECK(rc != 0, "FUZZ >= DIGITS raises SYNTAX");
}

/* ------------------------------------------------------------------ */
/*  AR#34: NUMERIC FORM ENGINEERING                                    */
/* ------------------------------------------------------------------ */

static void test_ar34_engineering(void)
{
    /* DIGITS 3 forces 1234567 into exponential range (adj_exp=6 > 3) */
    check_say("AR#34a: engineering, exponent already multiple of 3",
              "numeric digits 3\n"
              "numeric form engineering\n"
              "say 1234567 + 0\n"
              "exit 0\n",
              "1.23E+6", 0);
    check_say("AR#34b: engineering, exponent adjusted to multiple of 3",
              "numeric digits 3\n"
              "numeric form engineering\n"
              "say 123456 + 0\n"
              "exit 0\n",
              "123E+3", 0);
}

/* ------------------------------------------------------------------ */
/*  AR#35: Half-up rounding at boundary                               */
/* ------------------------------------------------------------------ */

static void test_ar35_rounding(void)
{
    check_say("AR#35a: round-half-up at exact midpoint",
              "numeric digits 3\n"
              "say 1.005 * 1\n"
              "exit 0\n",
              "1.01", 0);
    check_say("AR#35b: round-half-up with carry propagation",
              "numeric digits 1\n"
              "say 9.5 * 1\n"
              "exit 0\n",
              "10", 0);
}

/* ------------------------------------------------------------------ */
/*  AR#36: Power exponent overflow (validates Fix 2)                  */
/* ------------------------------------------------------------------ */

static void test_ar36_power_overflow(void)
{
    int exit_rc = -1;
    int rc;
    printf("\n--- AR#36: power exponent overflow ---\n");
    rc = run_with_mock(
        "say 2 ** 10000000\n"
        "exit 0\n",
        &exit_rc);
    CHECK(rc != 0, "|10000000| * 9 > 9999999 raises SYNTAX");
}

/* ------------------------------------------------------------------ */
/*  AR#37: Negative remainder and integer division                    */
/* ------------------------------------------------------------------ */

static void test_ar37_negative_rem_intdiv(void)
{
    /* SC28-1883-0 §9.5.5: remainder has same sign as dividend,
     * integer division truncates toward zero. */
    check_say("AR#37a: -7 // 3 = -1",
              "say -7 // 3\n"
              "exit 0\n",
              "-1", 0);
    check_say("AR#37b: -10 % 3 = -3",
              "say -10 % 3\n"
              "exit 0\n",
              "-3", 0);
    check_say("AR#37c: 7 // -3 = 1",
              "say 7 // -3\n"
              "exit 0\n",
              "1", 0);
    check_say("AR#37d: -7 // -3 = -1",
              "say -7 // -3\n"
              "exit 0\n",
              "-1", 0);
}

/* ------------------------------------------------------------------ */
/*  AR#38: Large-exponent power                                       */
/* ------------------------------------------------------------------ */

static void test_ar38_large_power(void)
{
    check_say("AR#38: 2**100 at DIGITS 50",
              "numeric digits 50\n"
              "say 2 ** 100\n"
              "exit 0\n",
              "1267650600228229401496703205376", 0);
}

/* ------------------------------------------------------------------ */
/*  AR#39: Fixed-point boundary regression (Fix 3)                    */
/* ------------------------------------------------------------------ */

static void test_ar39_fixed_point_boundary(void)
{
    /* adj_exp = -5: must be fixed-point */
    check_say("AR#39a: adj_exp=-5 is fixed-point",
              "say 0.00001 + 0\n"
              "exit 0\n",
              "0.00001", 0);
    /* adj_exp = -6: must be exponential */
    check_say("AR#39b: adj_exp=-6 is exponential",
              "say 0.000001 + 0\n"
              "exit 0\n",
              "1E-6", 0);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== WP-20: BCD Arithmetic Engine end-to-end tests ===\n");

    test_ar1_add_integers();
    test_ar2_subtract();
    test_ar3_multiply();
    test_ar4_divide_exact();
    test_ar5_divide_fraction();
    test_ar6_integer_divide();
    test_ar7_remainder();
    test_ar8_power();
    test_ar9_unary_neg();
    test_ar10_negative_arithmetic();
    test_ar11_decimal_add();
    test_ar12_decimal_mul();
    test_ar13_large_add();
    test_ar14_divide_third();
    test_ar15_numeric_digits();
    test_ar16_numeric_digits_large();
    test_ar17_do_loop_sum();
    test_ar18_comparison_equal();
    test_ar19_comparison_gt();
    test_ar20_comparison_lt();
    test_ar21_divzero();
    test_ar22_precedence();
    test_ar23_parens();
    test_ar24_power_zero();
    test_ar25_power_one();
    test_ar26_string_concat_unchanged();
    test_ar27_mixed_expr();
    test_ar28_negative_result();
    test_ar29_remainder_neg();
    test_ar30_numeric_form_scientific();
    test_ar31_fuzz_equal();
    test_ar32_strict_ignores_fuzz();
    test_ar33_fuzz_ge_digits();
    test_ar34_engineering();
    test_ar35_rounding();
    test_ar36_power_overflow();
    test_ar37_negative_rem_intdiv();
    test_ar38_large_power();
    test_ar39_fixed_point_boundary();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
