/* ------------------------------------------------------------------ */
/*  test_say.c - WP-14 SAY instruction + IRXINOUT unit tests          */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_say \                  */
/*        test/test_say.c \                                            */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \        */
/*        'src/irx#anch.c'  'src/irx#uid.c'  'src/irx#msid.c' \        */
/*        'src/irx#io.c'   'src/irx#lstr.c' 'src/irx#tokn.c' \        */
/*        'src/irx#vpol.c' 'src/irx#pars.c' \                         */
/*        ../lstring370/src/'lstr#cor.c'                              */
/*                                                                    */
/*  Tests install a mock I/O routine in IRXEXTE so that SAY output    */
/*  is captured without going to stdout, then verifies the captured   */
/*  string matches the expected REXX result.                          */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxio.h"
#include "irxpars.h"
#include "irxtokn.h"
#include "irxvpool.h"
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

/* ------------------------------------------------------------------ */
/*  Mock I/O routine — captures the last SAY output in a buffer       */
/* ------------------------------------------------------------------ */

static char g_captured[512];
static int g_captured_len = 0;
static int g_mock_called = 0;
static int g_mock_function = -1;

static int mock_io(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;
    g_mock_called++;
    g_mock_function = function;
    g_captured_len = 0;
    g_captured[0] = '\0';
    if (data != NULL && data->pstr != NULL && data->len > 0)
    {
        int n = (int)data->len;
        if (n > (int)sizeof(g_captured) - 1)
        {
            n = (int)sizeof(g_captured) - 1;
        }
        memcpy(g_captured, data->pstr, (size_t)n);
        g_captured_len = n;
        g_captured[n] = '\0';
    }
    return 0;
}

static void reset_mock(void)
{
    g_mock_called = 0;
    g_mock_function = -1;
    g_captured_len = 0;
    g_captured[0] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Test harness helpers                                               */
/* ------------------------------------------------------------------ */

/* Install mock_io as the active io_routine in the environment. */
static void install_mock(struct envblock *env)
{
    struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)mock_io;
    }
}

/* Run REXX source through tokenizer + parser in the given environment.
 * Returns IRXPARS_OK or an error code. */
static int run_source(struct envblock *env, const char *src)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_token *tokens = NULL;
    int count = 0;
    struct irx_tokn_error tok_err;
    struct irx_parser parser;
    struct irx_wkblk_int *wk;
    struct irx_vpool *pool;
    int rc;

    rc = irx_tokn_run(NULL, src, (int)strlen(src), &tokens, &count, &tok_err);
    if (rc != 0)
    {
        printf("    tokenizer failed: rc=%d code=%d line=%d col=%d\n",
               rc, tok_err.error_code, tok_err.error_line, tok_err.error_col);
        return -1;
    }

    /* Obtain (or create) the variable pool from the work block */
    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    pool = (struct irx_vpool *)wk->wkbi_varpool;
    if (pool == NULL)
    {
        pool = vpool_create(a, NULL);
        wk->wkbi_varpool = pool;
    }

    rc = irx_pars_init(&parser, tokens, count, pool, a, env);
    if (rc != IRXPARS_OK)
    {
        irx_tokn_free(NULL, tokens, count);
        printf("    irx_pars_init failed: rc=%d\n", rc);
        return rc;
    }

    rc = irx_pars_run(&parser);
    if (rc != IRXPARS_OK)
    {
        printf("    parser rc=%d error=%d line=%d\n",
               rc, parser.error_code, parser.error_line);
    }

    irx_pars_cleanup(&parser);
    irx_tokn_free(NULL, tokens, count);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Test cases                                                        */
/* ------------------------------------------------------------------ */

static void test_sa1_say_literal(void)
{
    struct envblock *env = NULL;
    printf("\n--- SA#1: SAY 'hello' ---\n");
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    if (env == NULL)
    {
        return;
    }
    install_mock(env);
    reset_mock();

    CHECK(run_source(env, "SAY 'hello'\n") == IRXPARS_OK, "parser OK");
    CHECK(g_mock_called == 1, "io_routine called once");
    CHECK(g_mock_function == RXFWRITE, "function is RXFWRITE");
    CHECK(g_captured_len == 5 &&
              memcmp(g_captured, "hello", 5) == 0,
          "output is 'hello'");

    irxterm(env);
}

static void test_sa2_say_expression(void)
{
    struct envblock *env = NULL;
    printf("\n--- SA#2: SAY 2 + 3 ---\n");
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    if (env == NULL)
    {
        return;
    }
    install_mock(env);
    reset_mock();

    CHECK(run_source(env, "SAY 2 + 3\n") == IRXPARS_OK, "parser OK");
    CHECK(g_mock_called == 1, "io_routine called once");
    CHECK(g_captured_len == 1 &&
              g_captured[0] == '5',
          "output is '5'");

    irxterm(env);
}

static void test_sa3_say_empty(void)
{
    struct envblock *env = NULL;
    printf("\n--- SA#3: SAY (no expression -> empty line) ---\n");
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    if (env == NULL)
    {
        return;
    }
    install_mock(env);
    reset_mock();

    CHECK(run_source(env, "SAY\n") == IRXPARS_OK, "parser OK");
    CHECK(g_mock_called == 1, "io_routine called once");
    CHECK(g_captured_len == 0, "output is empty string");

    irxterm(env);
}

static void test_sa4_say_concat(void)
{
    struct envblock *env = NULL;
    printf("\n--- SA#4: SAY 'foo' || 'bar' ---\n");
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    if (env == NULL)
    {
        return;
    }
    install_mock(env);
    reset_mock();

    CHECK(run_source(env, "SAY 'foo' || 'bar'\n") == IRXPARS_OK, "parser OK");
    CHECK(g_mock_called == 1, "io_routine called once");
    CHECK(g_captured_len == 6 &&
              memcmp(g_captured, "foobar", 6) == 0,
          "output is 'foobar'");

    irxterm(env);
}

static void test_sa5_say_multiple(void)
{
    struct envblock *env = NULL;
    printf("\n--- SA#5: three SAY instructions in sequence ---\n");
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    if (env == NULL)
    {
        return;
    }
    install_mock(env);
    reset_mock();

    CHECK(run_source(env,
                     "SAY 'one'\n"
                     "SAY 'two'\n"
                     "SAY 'three'\n") == IRXPARS_OK,
          "parser OK");
    CHECK(g_mock_called == 3, "io_routine called 3 times");
    /* Last captured value is 'three' */
    CHECK(g_captured_len == 5 &&
              memcmp(g_captured, "three", 5) == 0,
          "last output is 'three'");

    irxterm(env);
}

static void test_sa6_irxinout_direct(void)
{
    /* Test the default irxinout function directly (no envblock needed) */
    Lstr s;
    printf("\n--- SA#6: irxinout RXFWRITE direct call ---\n");

    Lzeroinit(&s);
    /* Use a stack buffer — point pstr at a string literal */
    s.pstr = (unsigned char *)"test";
    s.len = 4;

    /* Just verify it returns 0 and does not crash */
    CHECK(irxinout(RXFWRITE, &s, NULL) == 0, "RXFWRITE returns 0");
    CHECK(irxinout(RXFREAD, &s, NULL) == 20, "RXFREAD returns 20 (stub)");
    CHECK(irxinout(RXFREADP, &s, NULL) == 20, "RXFREADP returns 20 (stub)");
}

static void test_sa7_say_no_envblock(void)
{
    /* SAY with NULL envblock in parser must not crash.
     * The output is silently dropped (no io_routine to call). */
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    struct irx_token *tokens = NULL;
    int count = 0;
    struct irx_tokn_error tok_err;
    struct irx_parser parser;
    int rc;

    printf("\n--- SA#7: SAY with NULL envblock (no crash) ---\n");

    rc = irx_tokn_run(NULL, "SAY 'silent'\n", 13, &tokens, &count, &tok_err);
    CHECK(rc == 0, "tokenizer OK");
    if (rc != 0)
    {
        vpool_destroy(pool);
        return;
    }

    rc = irx_pars_init(&parser, tokens, count, pool, a, NULL);
    CHECK(rc == IRXPARS_OK, "irx_pars_init OK");
    if (rc == IRXPARS_OK)
    {
        rc = irx_pars_run(&parser);
        CHECK(rc == IRXPARS_OK, "parser OK (output silently dropped)");
        irx_pars_cleanup(&parser);
    }

    irx_tokn_free(NULL, tokens, count);
    vpool_destroy(pool);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== WP-14: SAY + IRXINOUT tests ===\n");

    test_sa1_say_literal();
    test_sa2_say_expression();
    test_sa3_say_empty();
    test_sa4_say_concat();
    test_sa5_say_multiple();
    test_sa6_irxinout_direct();
    test_sa7_say_no_envblock();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
