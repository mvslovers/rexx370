/* ------------------------------------------------------------------ */
/*  test_procedure.c - WP-17 PROCEDURE EXPOSE acceptance tests        */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_procedure \           */
/*        test/test_procedure.c \                                      */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \       */
/*        'src/irx#rab.c'  'src/irx#uid.c'  'src/irx#msid.c' \       */
/*        'src/irx#io.c'   'src/irx#lstr.c' 'src/irx#tokn.c' \       */
/*        'src/irx#vpol.c' 'src/irx#pars.c' 'src/irx#ctrl.c' \       */
/*        'src/irx#exec.c' \                                           */
/*        ../lstring370/src/'lstr#cor.c'                              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxwkblk.h"
#include "irxfunc.h"
#include "irxexec.h"
#include "lstring.h"

#ifndef __MVS__
void *_simulated_tcbuser = NULL;
#endif

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) \
    do { \
        tests_run++; \
        if (cond) { \
            tests_passed++; \
            printf("  PASS: %s\n", msg); \
        } else { \
            tests_failed++; \
            printf("  FAIL: %s\n", msg); \
        } \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Mock I/O                                                          */
/* ------------------------------------------------------------------ */

#define OUT_BUF_SIZE 4096

static char g_out[OUT_BUF_SIZE];
static int  g_out_len = 0;

static void reset_output(void)
{
    g_out_len = 0;
    g_out[0]  = '\0';
}

static int output_contains(const char *line)
{
    int line_len = (int)strlen(line);
    int i;
    for (i = 0; i <= g_out_len - line_len; i++) {
        if (memcmp(g_out + i, line, (size_t)line_len) == 0) return 1;
    }
    return 0;
}

static int mock_io(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;
    if (function == RXFWRITE && data != NULL
            && data->pstr != NULL && data->len > 0) {
        int n = (int)data->len;
        if (g_out_len + n + 1 < OUT_BUF_SIZE) {
            memcpy(g_out + g_out_len, data->pstr, (size_t)n);
            g_out_len          += n;
            g_out[g_out_len++]  = '\n';
            g_out[g_out_len]    = '\0';
        }
    }
    return 0;
}

static void install_mock(struct envblock *env)
{
    struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
        exte->io_routine = (void *)mock_io;
}

static int run_src(const char *src, int *exit_rc_out)
{
    struct envblock *env = NULL;
    int rc;

    rc = irxinit(NULL, &env);
    if (rc != 0) return rc;

    install_mock(env);
    reset_output();

    rc = irx_exec_run(src, (int)strlen(src), NULL, 0, exit_rc_out, env);

    irxterm(env);
    return rc;
}

static int run_src_with_args(const char *src, const char *args,
                              int *exit_rc_out)
{
    struct envblock *env = NULL;
    int rc;

    rc = irxinit(NULL, &env);
    if (rc != 0) return rc;

    install_mock(env);
    reset_output();

    rc = irx_exec_run(src, (int)strlen(src),
                      args, (int)strlen(args),
                      exit_rc_out, env);

    irxterm(env);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Test: CALL without PROCEDURE - caller variable is visible         */
/* ------------------------------------------------------------------ */

static void test_pr01_call_no_procedure(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#01: CALL without PROCEDURE (shared scope) ---\n");

    rc = run_src(
        "/* REXX */\n"
        "x = 42\n"
        "call mysub\n"
        "say 'x =' x\n"
        "exit 0\n"
        "mysub:\n"
        "  x = 99\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0,     "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("x = 99"),
          "SAY shows x=99 (sub modified caller's var)");
}

/* ------------------------------------------------------------------ */
/*  Test: PROCEDURE isolates scope                                    */
/* ------------------------------------------------------------------ */

static void test_pr02_procedure_isolates(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#02: PROCEDURE isolates caller scope ---\n");

    rc = run_src(
        "/* REXX */\n"
        "x = 42\n"
        "call mysub\n"
        "say 'x =' x\n"
        "exit 0\n"
        "mysub:\n"
        "  procedure\n"
        "  x = 99\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("x = 42"),
          "SAY shows x=42 (PROCEDURE kept sub's x private)");
}

/* ------------------------------------------------------------------ */
/*  Test: PROCEDURE EXPOSE shares named variable                      */
/* ------------------------------------------------------------------ */

static void test_pr03_expose_var(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#03: PROCEDURE EXPOSE shares named variable ---\n");

    rc = run_src(
        "/* REXX */\n"
        "x = 10\n"
        "y = 20\n"
        "call mysub\n"
        "say 'x =' x\n"
        "say 'y =' y\n"
        "exit 0\n"
        "mysub:\n"
        "  procedure expose x\n"
        "  x = 99\n"
        "  y = 77\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("x = 99"),
          "SAY shows x=99 (exposed var modified)");
    CHECK(output_contains("y = 20"),
          "SAY shows y=20 (non-exposed var unchanged)");
}

/* ------------------------------------------------------------------ */
/*  Test: RETURN value stored in RESULT                               */
/* ------------------------------------------------------------------ */

static void test_pr04_return_value(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#04: RETURN value stored in RESULT ---\n");

    rc = run_src(
        "/* REXX */\n"
        "call double 21\n"
        "say 'result =' result\n"
        "exit 0\n"
        "double:\n"
        "  procedure\n"
        "  arg n\n"
        "  return n + n\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("result = 42"),
          "SAY shows result=42 (RETURN value)");
}

/* ------------------------------------------------------------------ */
/*  Test: CALL passes multiple arguments                              */
/* ------------------------------------------------------------------ */

static void test_pr05_call_multiple_args(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#05: CALL passes multiple arguments ---\n");

    rc = run_src(
        "/* REXX */\n"
        "call add 3, 7\n"
        "say 'sum =' result\n"
        "exit 0\n"
        "add:\n"
        "  procedure\n"
        "  arg a, b\n"
        "  return a + b\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("sum = 10"),
          "SAY shows sum=10 (3+7)");
}

/* ------------------------------------------------------------------ */
/*  Test: ARG() BIF returns argument count                            */
/* ------------------------------------------------------------------ */

static void test_pr06_arg_bif_count(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#06: ARG() BIF returns argument count ---\n");

    rc = run_src(
        "/* REXX */\n"
        "call check 'a', 'b', 'c'\n"
        "say 'count =' result\n"
        "exit 0\n"
        "check:\n"
        "  procedure\n"
        "  return arg()\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("count = 3"),
          "ARG() returns 3");
}

/* ------------------------------------------------------------------ */
/*  Test: ARG(n) BIF returns nth argument                             */
/* ------------------------------------------------------------------ */

static void test_pr07_arg_bif_nth(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#07: ARG(n) BIF returns nth argument ---\n");

    rc = run_src(
        "/* REXX */\n"
        "call check 'hello', 'world'\n"
        "say result\n"
        "exit 0\n"
        "check:\n"
        "  procedure\n"
        "  return arg(2)\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("world"),
          "ARG(2) returns 'world'");
}

/* ------------------------------------------------------------------ */
/*  Test: ARG(n,'E') and ARG(n,'O')                                   */
/* ------------------------------------------------------------------ */

static void test_pr08_arg_bif_exists_omitted(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#08: ARG(n,'E') / ARG(n,'O') ---\n");

    rc = run_src(
        "/* REXX */\n"
        "call check 'x', , 'z'\n"
        "exit 0\n"
        "check:\n"
        "  procedure\n"
        "  say arg(1,'E') arg(2,'E') arg(3,'E')\n"
        "  say arg(1,'O') arg(2,'O') arg(3,'O')\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("1 0 1"),
          "exists: arg1=1, arg2=0 (omitted), arg3=1");
    CHECK(output_contains("0 1 0"),
          "omitted: arg1=0, arg2=1, arg3=0");
}

/* ------------------------------------------------------------------ */
/*  Test: Nested CALL with PROCEDURE                                  */
/* ------------------------------------------------------------------ */

static void test_pr09_nested_calls(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#09: nested CALL with PROCEDURE ---\n");

    rc = run_src(
        "/* REXX */\n"
        "x = 1\n"
        "call outer\n"
        "say 'x =' x\n"
        "exit 0\n"
        "outer:\n"
        "  procedure\n"
        "  x = 2\n"
        "  call inner\n"
        "  return\n"
        "inner:\n"
        "  procedure\n"
        "  x = 3\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("x = 1"),
          "x stays 1 in caller after nested PROCEDURE calls");
}

/* ------------------------------------------------------------------ */
/*  Test: ARG instruction uppercases value                            */
/* ------------------------------------------------------------------ */

static void test_pr10_arg_uppercase(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#10: ARG instruction uppercases value ---\n");

    rc = run_src(
        "/* REXX */\n"
        "call greet 'hello world'\n"
        "exit 0\n"
        "greet:\n"
        "  procedure\n"
        "  arg msg\n"
        "  say msg\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("HELLO WORLD"),
          "ARG uppercases 'hello world' to 'HELLO WORLD'");
}

/* ------------------------------------------------------------------ */
/*  Test: ARG with multiple variables splits by words                 */
/* ------------------------------------------------------------------ */

static void test_pr11_arg_word_split(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#11: ARG splits by words ---\n");

    rc = run_src(
        "/* REXX */\n"
        "call split 'alpha beta gamma'\n"
        "exit 0\n"
        "split:\n"
        "  procedure\n"
        "  arg a b rest\n"
        "  say a\n"
        "  say b\n"
        "  say rest\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("ALPHA"),  "first word: ALPHA");
    CHECK(output_contains("BETA"),   "second word: BETA");
    CHECK(output_contains("GAMMA"),  "rest: GAMMA");
}

/* ------------------------------------------------------------------ */
/*  Test: top-level ARG() BIF with no args                            */
/* ------------------------------------------------------------------ */

static void test_pr12_toplevel_arg_bif_no_args(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#12: top-level ARG() with no args passed ---\n");

    rc = run_src(
        "/* REXX */\n"
        "say arg()\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("0"),
          "ARG() at top level with no args returns 0");
}

/* ------------------------------------------------------------------ */
/*  Test: top-level ARG with passed argument string                   */
/* ------------------------------------------------------------------ */

static void test_pr13_toplevel_arg_with_args(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#13: top-level ARG with argument string ---\n");

    rc = run_src_with_args(
        "/* REXX */\n"
        "arg first rest\n"
        "say first\n"
        "say arg()\n"
        "exit 0\n",
        "hello world",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("HELLO"),  "ARG first word = HELLO");
    CHECK(output_contains("1"),      "ARG() = 1 (one arg string passed)");
}

/* ------------------------------------------------------------------ */
/*  Test: PROCEDURE without EXPOSE (all vars private)                 */
/* ------------------------------------------------------------------ */

static void test_pr14_procedure_all_private(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#14: PROCEDURE without EXPOSE (all vars private) ---\n");

    rc = run_src(
        "/* REXX */\n"
        "a = 1\n"
        "b = 2\n"
        "c = 3\n"
        "call mysub\n"
        "say a b c\n"
        "exit 0\n"
        "mysub:\n"
        "  procedure\n"
        "  a = 10\n"
        "  b = 20\n"
        "  c = 30\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("1 2 3"),
          "a b c unchanged (all private via PROCEDURE)");
}

/* ------------------------------------------------------------------ */
/*  Test: PROCEDURE EXPOSE multiple variables                         */
/* ------------------------------------------------------------------ */

static void test_pr15_expose_multiple(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#15: PROCEDURE EXPOSE multiple variables ---\n");

    rc = run_src(
        "/* REXX */\n"
        "a = 1\n"
        "b = 2\n"
        "c = 3\n"
        "call mysub\n"
        "say a b c\n"
        "exit 0\n"
        "mysub:\n"
        "  procedure expose a c\n"
        "  a = 10\n"
        "  b = 20\n"
        "  c = 30\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("10 2 30"),
          "a=10 (exposed), b=2 (private), c=30 (exposed)");
}

/* ------------------------------------------------------------------ */
/*  Test: PROCEDURE EXPOSE (indirect)                                 */
/* ------------------------------------------------------------------ */

static void test_pr16_expose_indirect(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PR#16: PROCEDURE EXPOSE (indirect) ---\n");

    /* NAMES holds 'A B STEM.' — parenthesized indirection in
     * EXPOSE should look up NAMES in the caller's pool, split
     * its value by whitespace, and expose A, B, and STEM. (stem). */
    rc = run_src(
        "/* REXX */\n"
        "a = 1\n"
        "b = 2\n"
        "stem.1 = 'one'\n"
        "stem.2 = 'two'\n"
        "names = 'A B STEM.'\n"
        "call mysub\n"
        "say a b stem.1 stem.2\n"
        "exit 0\n"
        "mysub:\n"
        "  procedure expose (names)\n"
        "  a = 11\n"
        "  b = 22\n"
        "  stem.1 = 'ONE'\n"
        "  stem.2 = 'TWO'\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0,      "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("11 22 ONE TWO"),
          "a, b, stem.* all exposed via (names) indirect");
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== WP-17: PROCEDURE EXPOSE tests ===\n");

    test_pr01_call_no_procedure();
    test_pr02_procedure_isolates();
    test_pr03_expose_var();
    test_pr04_return_value();
    test_pr05_call_multiple_args();
    test_pr06_arg_bif_count();
    test_pr07_arg_bif_nth();
    test_pr08_arg_bif_exists_omitted();
    test_pr09_nested_calls();
    test_pr10_arg_uppercase();
    test_pr11_arg_word_split();
    test_pr12_toplevel_arg_bif_no_args();
    test_pr13_toplevel_arg_with_args();
    test_pr14_procedure_all_private();
    test_pr15_expose_multiple();
    test_pr16_expose_indirect();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
