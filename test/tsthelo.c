/* ------------------------------------------------------------------ */
/*  tsthelo.c - WP-18 Hello World End-to-End integration tests     */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_hello \               */
/*        test/test_hello.c \                                          */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \       */
/*        'src/irx#anch.c'  'src/irx#uid.c'  'src/irx#msid.c' \       */
/*        'src/irx#io.c'   'src/irx#lstr.c' 'src/irx#tokn.c' \       */
/*        'src/irx#vpol.c' 'src/irx#pars.c' 'src/irx#ctrl.c' \       */
/*        'src/irx#exec.c' \                                           */
/*        ../lstring370/src/'lstr#cor.c'                              */
/*                                                                    */
/*  Each test calls irx_exec_run() with REXX source and checks        */
/*  output and return codes via a mock I/O routine installed into     */
/*  IRXEXTE before execution.                                          */
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
/*  Mock I/O — accumulates all SAY lines separated by '\n'            */
/* ------------------------------------------------------------------ */

#define OUT_BUF_SIZE 4096

static char g_out[OUT_BUF_SIZE];
static int g_out_len = 0;

static void reset_output(void)
{
    g_out_len = 0;
    g_out[0] = '\0';
}

/* Returns 1 if the accumulated output contains the given line. */
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
    if (function == RXFWRITE && data != NULL && data->pstr != NULL && data->len > 0)
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

/* Run source through irx_exec_run, installing mock I/O first. */
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

/* ------------------------------------------------------------------ */
/*  Test cases                                                        */
/* ------------------------------------------------------------------ */

static void test_hw1_hello_world(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- HW#1: Hello World exec ---\n");

    rc = run_with_mock(
        "/* REXX */\n"
        "say 'Hello World from REXX/370!'\n"
        "x = 2 + 3\n"
        "say 'The answer is' x\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "EXIT return code is 0");
    CHECK(output_contains("Hello World from REXX/370!"),
          "first SAY: 'Hello World from REXX/370!'");
    CHECK(output_contains("The answer is 5"),
          "second SAY: 'The answer is 5'");
}

static void test_hw2_do_loop_sum(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- HW#2: DO loop sum 1..10 ---\n");

    rc = run_with_mock(
        "/* REXX */\n"
        "sum = 0\n"
        "do i = 1 to 10\n"
        "  sum = sum + i\n"
        "end\n"
        "say 'Sum 1..10 =' sum\n"
        "exit sum\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 55, "EXIT return code is 55");
    CHECK(output_contains("Sum 1..10 = 55"),
          "SAY: 'Sum 1..10 = 55'");
}

static void test_hw3_null_envblock(void)
{
    /* irx_exec_run with envblock=NULL must create its own environment. */
    struct irxexte *exte;
    int exit_rc = -1;
    int rc;

    /* We cannot install a mock without an env, so we just verify that
     * the call succeeds and the exit code is correct. */
    printf("\n--- HW#3: envblock=NULL (auto-create environment) ---\n");

    /* Redirect irxinout output to /dev/null by hijacking the real
     * irxinout (it uses printf); just verify rc is correct. */
    const char *src = "x = 6 * 7\n"
                      "exit x\n";
    rc = irx_exec_run(src, (int)strlen(src), NULL, 0, &exit_rc, NULL);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 42, "EXIT return code is 42");

    (void)exte; /* suppress unused warning */
}

static void test_hw4_syntax_error(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- HW#4: syntax error returns non-zero ---\n");

    rc = run_with_mock(
        "say 'unterminated\n",
        &exit_rc);

    CHECK(rc != 0, "irx_exec_run returns non-zero on error");
}

static void test_hw5_exit_nonzero(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- HW#5: EXIT with non-zero code ---\n");

    rc = run_with_mock(
        "exit 7\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 7, "EXIT return code is 7");
}

static void test_hw6_no_exit_clause(void)
{
    /* When a program has no EXIT, exit_rc must be 0. */
    int exit_rc = -1;
    int rc;

    printf("\n--- HW#6: no EXIT clause -> rc_out = 0 ---\n");

    rc = run_with_mock(
        "x = 1\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit_rc is 0 (no EXIT in source)");
}

static void test_hw7_if_then_else(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- HW#7: IF/THEN/ELSE end-to-end ---\n");

    rc = run_with_mock(
        "x = 10\n"
        "if x > 5 then\n"
        "  say 'big'\n"
        "else\n"
        "  say 'small'\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(output_contains("big"), "SAY: 'big' (true branch taken)");
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== WP-18: Hello World End-to-End tests ===\n");

    test_hw1_hello_world();
    test_hw2_do_loop_sum();
    test_hw3_null_envblock();
    test_hw4_syntax_error();
    test_hw5_exit_nonzero();
    test_hw6_no_exit_clause();
    test_hw7_if_then_else();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
