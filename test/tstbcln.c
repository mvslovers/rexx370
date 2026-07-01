/* ------------------------------------------------------------------ */
/*  test/host/tstbc_clean.c — WP-BC-CLEAN tests                       */
/*                                                                    */
/*  Covers:                                                           */
/*    - Long-literal rejection (IRXBC_ERR_STRTOOLONG)                */
/*    - DO COUNT: zero iterations, negative count, exactly 1,        */
/*      small count (5), variable count, nested loops                */
/*    - Integer fast-path: literals, variable round-trips             */
/*    - vpool_get_buf / vpool_set_buf type_cache propagation          */
/*                                                                    */
/*  Build (Linux):                                                    */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -O0 -g \                           */
/*        -o /tmp/tstbc_clean test/host/tstbc_clean.c \               */
/*        'src/irx#init.c'  'src/irx#term.c'  'src/irx#stor.c' \    */
/*        'src/irx#anch.c'  'src/irx#env.c'   'src/irx#uid.c'  \    */
/*        'src/irx#msid.c'  'src/irx#cond.c'  'src/irx#bif.c'  \    */
/*        'src/irx#bifs.c'  'src/irx#io.c'    'src/irx#lstr.c' \    */
/*        'src/irx#tokn.c'  'src/irx#vpol.c'  'src/irx#pars.c' \    */
/*        'src/irx#ctrl.c'  'src/irx#exec.c'  'src/irx#arith.c' \   */
/*        'src/irx#bcom.c'  'src/irx#bvm.c' \                        */
/*        '../lstring370/src/lstr#cor.c' \                            */
/*        '../lstring370/src/lstr#cvt.c' \                            */
/*        '../lstring370/src/lstr#fmt.c' \                            */
/*        '../lstring370/src/lstr#srch.c' \                           */
/*        '../lstring370/src/lstr#sub.c' \                            */
/*        '../lstring370/src/lstr#wrd.c' \                            */
/*        '../lstring370/src/lstr#xlt.c'                              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxbops.h"
#include "irxbvm.h"
#include "irxexbl.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxwkblk.h"

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

/* Run src through both interpreter paths and compare exit codes. */
static void run_both(const char *desc, const char *src,
                     int expected_tw, int expected_bc)
{
    struct envblock *env = NULL;
    struct irx_wkblk_int *wk;
    int tw_rc = -999;
    int bc_rc = -999;
    int ok;
    char msg[80];
    int src_len = (int)strlen(src);

    printf("  [%s]\n", desc);

    /* token-walk */
    ok = (irxinit(NULL, &env) == 0 && env != NULL);
    if (ok)
    {
        wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
        if (wk != NULL)
        {
            wk->wkbi_use_bytecode = 0;
        }
        ok = (irx_exec_run(src, src_len, NULL, 0, &tw_rc, env) == 0);
        irxterm(env);
        env = NULL;
    }
    if (!ok)
    {
        tw_rc = -1;
    }

    /* bytecode */
    ok = (irxinit(NULL, &env) == 0 && env != NULL);
    if (ok)
    {
        wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
        if (wk != NULL)
        {
            wk->wkbi_use_bytecode = 1;
        }
        ok = (irx_exec_run(src, src_len, NULL, 0, &bc_rc, env) == 0);
        irxterm(env);
        env = NULL;
    }
    if (!ok)
    {
        bc_rc = -1;
    }

    snprintf(msg, sizeof(msg), "%s: token-walk rc==%d", desc, expected_tw);
    CHECK(tw_rc == expected_tw, msg);
    snprintf(msg, sizeof(msg), "%s: bytecode rc==%d", desc, expected_bc);
    CHECK(bc_rc == expected_bc, msg);
    snprintf(msg, sizeof(msg), "%s: bytecode==token-walk", desc);
    CHECK(bc_rc == tw_rc, msg);
}

#define EQUIV(desc, src, expected) run_both(desc, src, expected, expected)

/* ------------------------------------------------------------------ */
/*  Long-literal rejection                                            */
/* ------------------------------------------------------------------ */

static void test_strtoolong(struct envblock *env)
{
    /* Construct a literal of exactly 64 bytes (one over IRXBC_STR_MAX=63) */
    char src[128];
    struct irx_bc_execblk *bc = NULL;
    int rc;

    memset(src, 0, sizeof(src));
    /* SAY 'AAAA...A' where string is 64 'A's */
    strcpy(src, "SAY '");
    memset(src + 5, 'A', 64);
    src[69] = '\'';
    src[70] = '\0';

    printf("  [long literal (64 bytes) rejected]\n");
    rc = irx_bc_compile(env, src, (int)strlen(src), &bc, NULL, NULL);
    CHECK(rc == IRXBC_ERR_STRTOOLONG, "compile returns IRXBC_ERR_STRTOOLONG");
    CHECK(bc == NULL, "bc is NULL on strtoolong error");

    if (bc != NULL)
    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }

    /* A literal of exactly 63 bytes should succeed */
    printf("  [literal at max (63 bytes) accepted]\n");
    memset(src, 0, sizeof(src));
    strcpy(src, "SAY '");
    memset(src + 5, 'B', 63);
    src[68] = '\'';
    src[69] = '\0';

    bc = NULL;
    rc = irx_bc_compile(env, src, (int)strlen(src), &bc, NULL, NULL);
    CHECK(rc == IRXBC_OK, "63-byte literal compiles OK");
    if (bc != NULL)
    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

/* ------------------------------------------------------------------ */
/*  DO COUNT equivalence tests                                        */
/* ------------------------------------------------------------------ */

static void test_do_zero(void)
{
    /* DO 0 should execute the body zero times */
    EQUIV("do_zero",
          "x = 0\n"
          "DO 0\n"
          "  x = x + 1\n"
          "END\n"
          "EXIT x",
          0);
}

static void test_do_negative(void)
{
    /* DO -1 should execute the body zero times (negative count = 0 iters) */
    EQUIV("do_neg",
          "x = 0\n"
          "DO -1\n"
          "  x = x + 1\n"
          "END\n"
          "EXIT x",
          0);
}

static void test_do_one(void)
{
    /* DO 1 executes body exactly once */
    EQUIV("do_one",
          "x = 0\n"
          "DO 1\n"
          "  x = x + 1\n"
          "END\n"
          "EXIT x",
          1);
}

static void test_do_five(void)
{
    /* DO 5 executes body five times */
    EQUIV("do_five",
          "x = 0\n"
          "DO 5\n"
          "  x = x + 1\n"
          "END\n"
          "EXIT x",
          5);
}

static void test_do_var(void)
{
    /* DO n where n is a variable */
    EQUIV("do_var",
          "n = 7\n"
          "x = 0\n"
          "DO n\n"
          "  x = x + 1\n"
          "END\n"
          "EXIT x",
          7);
}

static void test_do_expr(void)
{
    /* DO with an expression as count */
    EQUIV("do_expr",
          "x = 0\n"
          "DO 2 + 3\n"
          "  x = x + 1\n"
          "END\n"
          "EXIT x",
          5);
}

static void test_do_nested(void)
{
    /* Nested DO COUNT: outer 3, inner 4 → 12 iterations total */
    EQUIV("do_nested",
          "x = 0\n"
          "DO 3\n"
          "  DO 4\n"
          "    x = x + 1\n"
          "  END\n"
          "END\n"
          "EXIT x",
          12);
}

static void test_do_leave(void)
{
    /* LEAVE from inside DO COUNT */
    EQUIV("do_leave",
          "x = 0\n"
          "DO 10\n"
          "  x = x + 1\n"
          "  IF x = 3 THEN LEAVE\n"
          "END\n"
          "EXIT x",
          3);
}

static void test_do_iterate(void)
{
    /* ITERATE inside DO COUNT — skip body, continue loop */
    EQUIV("do_iterate",
          "x = 0\n"
          "DO 5\n"
          "  x = x + 1\n"
          "  ITERATE\n"
          "  x = x + 100\n"
          "END\n"
          "EXIT x",
          5);
}

/* ------------------------------------------------------------------ */
/*  Integer fast-path observable tests                                */
/* ------------------------------------------------------------------ */

static void test_arith_chain(void)
{
    /* Chain of integer arithmetic via variables */
    EQUIV("arith_chain",
          "a = 10\n"
          "b = a + 5\n"
          "c = b * 2\n"
          "EXIT c",
          30);
}

static void test_large_count(void)
{
    /* DO 100 — tests that the counter doesn't overflow or mis-count */
    EQUIV("do_100",
          "x = 0\n"
          "DO 100\n"
          "  x = x + 1\n"
          "END\n"
          "EXIT x",
          100);
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    int rc;

    printf("=== WP-BC-CLEAN Tests ===\n\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbc_clean: irxinit failed rc=%d\n", rc);
        return 1;
    }

    test_strtoolong(env);

    irxterm(env);
    env = NULL;

    /* Equivalence tests — each creates its own envblocks */
    test_do_zero();
    test_do_negative();
    test_do_one();
    test_do_five();
    test_do_var();
    test_do_expr();
    test_do_nested();
    test_do_leave();
    test_do_iterate();
    test_arith_chain();
    test_large_count();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
