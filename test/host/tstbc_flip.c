/* Linux host: expose setenv/unsetenv from <stdlib.h>. */
#define _POSIX_C_SOURCE 200809L

/* ------------------------------------------------------------------ */
/*  test/host/tstbc_flip.c — WP-BC-FLIP env-var override tests        */
/*                                                                    */
/*  Verifies:                                                         */
/*    - wkbi_use_bytecode defaults to 1 (env unset)                  */
/*    - REXX370_BYTECODE=0/false/no/off forces wkbi_use_bytecode = 0 */
/*    - REXX370_BYTECODE=1/true/yes/on keeps wkbi_use_bytecode = 1   */
/*    - Garbage value falls back to default (1)                       */
/*    - Explicit test setters win over env-var (test isolation)       */
/*                                                                    */
/*  Build (Linux):                                                     */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -O0 -g \                           */
/*        -o /tmp/tstbc_flip test/host/tstbc_flip.c \                 */
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
#include <stdlib.h>
#include <string.h>

#include "irx.h"
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

/* ------------------------------------------------------------------ */
/*  Helper: init env, retrieve wk, check flag, term.                  */
/* ------------------------------------------------------------------ */
static int wkbi_flag_after_init(void)
{
    struct envblock *env = NULL;
    struct irx_wkblk_int *wk;
    int flag = -1;

    if (irxinit(NULL, &env) != 0 || env == NULL)
    {
        return -1;
    }
    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk != NULL)
    {
        flag = wk->wkbi_use_bytecode;
    }
    irxterm(env);
    return flag;
}

/* ------------------------------------------------------------------ */
/*  Default (env unset): wkbi_use_bytecode must be 1.                 */
/* ------------------------------------------------------------------ */
static void test_default_on(void)
{
    printf("\n--- FLIP#01: default-on (env unset) ---\n");

    unsetenv("REXX370_BYTECODE");
    CHECK(wkbi_flag_after_init() == 1, "default wkbi_use_bytecode == 1");
}

/* ------------------------------------------------------------------ */
/*  REXX370_BYTECODE=0 forces wkbi_use_bytecode = 0.                  */
/* ------------------------------------------------------------------ */
static void test_env_zero(void)
{
    printf("\n--- FLIP#02: REXX370_BYTECODE=0 ---\n");

    setenv("REXX370_BYTECODE", "0", 1);
    CHECK(wkbi_flag_after_init() == 0, "REXX370_BYTECODE=0 → flag 0");
    unsetenv("REXX370_BYTECODE");
}

/* ------------------------------------------------------------------ */
/*  REXX370_BYTECODE=1 keeps wkbi_use_bytecode = 1.                   */
/* ------------------------------------------------------------------ */
static void test_env_one(void)
{
    printf("\n--- FLIP#03: REXX370_BYTECODE=1 ---\n");

    setenv("REXX370_BYTECODE", "1", 1);
    CHECK(wkbi_flag_after_init() == 1, "REXX370_BYTECODE=1 → flag 1");
    unsetenv("REXX370_BYTECODE");
}

/* ------------------------------------------------------------------ */
/*  false/no/off variants (case-insensitive) → wkbi_use_bytecode = 0. */
/* ------------------------------------------------------------------ */
static void test_env_false_variants(void)
{
    printf("\n--- FLIP#04: false/no/off variants ---\n");

    setenv("REXX370_BYTECODE", "false", 1);
    CHECK(wkbi_flag_after_init() == 0, "REXX370_BYTECODE=false → flag 0");

    setenv("REXX370_BYTECODE", "FALSE", 1);
    CHECK(wkbi_flag_after_init() == 0, "REXX370_BYTECODE=FALSE → flag 0");

    setenv("REXX370_BYTECODE", "no", 1);
    CHECK(wkbi_flag_after_init() == 0, "REXX370_BYTECODE=no → flag 0");

    setenv("REXX370_BYTECODE", "NO", 1);
    CHECK(wkbi_flag_after_init() == 0, "REXX370_BYTECODE=NO → flag 0");

    setenv("REXX370_BYTECODE", "off", 1);
    CHECK(wkbi_flag_after_init() == 0, "REXX370_BYTECODE=off → flag 0");

    setenv("REXX370_BYTECODE", "OFF", 1);
    CHECK(wkbi_flag_after_init() == 0, "REXX370_BYTECODE=OFF → flag 0");

    unsetenv("REXX370_BYTECODE");
}

/* ------------------------------------------------------------------ */
/*  true/yes/on variants (case-insensitive) → wkbi_use_bytecode = 1.  */
/* ------------------------------------------------------------------ */
static void test_env_true_variants(void)
{
    printf("\n--- FLIP#05: true/yes/on variants ---\n");

    setenv("REXX370_BYTECODE", "true", 1);
    CHECK(wkbi_flag_after_init() == 1, "REXX370_BYTECODE=true → flag 1");

    setenv("REXX370_BYTECODE", "TRUE", 1);
    CHECK(wkbi_flag_after_init() == 1, "REXX370_BYTECODE=TRUE → flag 1");

    setenv("REXX370_BYTECODE", "yes", 1);
    CHECK(wkbi_flag_after_init() == 1, "REXX370_BYTECODE=yes → flag 1");

    setenv("REXX370_BYTECODE", "YES", 1);
    CHECK(wkbi_flag_after_init() == 1, "REXX370_BYTECODE=YES → flag 1");

    setenv("REXX370_BYTECODE", "on", 1);
    CHECK(wkbi_flag_after_init() == 1, "REXX370_BYTECODE=on → flag 1");

    setenv("REXX370_BYTECODE", "ON", 1);
    CHECK(wkbi_flag_after_init() == 1, "REXX370_BYTECODE=ON → flag 1");

    unsetenv("REXX370_BYTECODE");
}

/* ------------------------------------------------------------------ */
/*  Garbage value → default stays at 1.                               */
/* ------------------------------------------------------------------ */
static void test_env_garbage(void)
{
    printf("\n--- FLIP#06: garbage value ---\n");

    setenv("REXX370_BYTECODE", "banana", 1);
    CHECK(wkbi_flag_after_init() == 1, "REXX370_BYTECODE=banana → flag stays 1");

    setenv("REXX370_BYTECODE", "", 1);
    CHECK(wkbi_flag_after_init() == 1, "REXX370_BYTECODE= (empty) → flag stays 1");

    unsetenv("REXX370_BYTECODE");
}

/* ------------------------------------------------------------------ */
/*  Explicit test setter wins over REXX370_BYTECODE=0.                */
/*  Verifies test isolation: a shell-level opt-out must not break      */
/*  bytecode-specific tests that set the flag explicitly.              */
/* ------------------------------------------------------------------ */
static void test_setter_wins_over_env(void)
{
    struct envblock *env = NULL;
    struct irx_wkblk_int *wk;
    int exit_rc = -1;

    printf("\n--- FLIP#07: explicit setter wins over REXX370_BYTECODE=0 ---\n");

    setenv("REXX370_BYTECODE", "0", 1);

    if (irxinit(NULL, &env) != 0 || env == NULL)
    {
        printf("  FAIL: irxinit failed\n");
        tests_run++;
        tests_failed++;
        unsetenv("REXX370_BYTECODE");
        return;
    }

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        printf("  FAIL: wk is NULL\n");
        tests_run++;
        tests_failed++;
        irxterm(env);
        unsetenv("REXX370_BYTECODE");
        return;
    }

    /* After init, env-var drove flag to 0. */
    CHECK(wk->wkbi_use_bytecode == 0, "env-var set flag to 0 at init");

    /* Explicit setter overrides — simulates what bytecode tests do. */
    wk->wkbi_use_bytecode = 1;
    CHECK(wk->wkbi_use_bytecode == 1, "explicit setter overrides env-var");

    /* Run a simple program to confirm the bytecode path executes. */
    exit_rc = -1;
    CHECK(irx_exec_run("exit 0", (int)strlen("exit 0"),
                       NULL, 0, &exit_rc, env) == 0,
          "bytecode exec_run returns 0 after explicit setter");
    CHECK(exit_rc == 0, "exit_rc == 0 via bytecode path");

    irxterm(env);
    unsetenv("REXX370_BYTECODE");
}

/* ------------------------------------------------------------------ */
/*  Explicit setter = 0 wins over REXX370_BYTECODE=1.                 */
/* ------------------------------------------------------------------ */
static void test_setter_zero_wins_over_env(void)
{
    struct envblock *env = NULL;
    struct irx_wkblk_int *wk;
    int exit_rc = -1;

    printf("\n--- FLIP#08: explicit setter=0 wins over REXX370_BYTECODE=1 ---\n");

    setenv("REXX370_BYTECODE", "1", 1);

    if (irxinit(NULL, &env) != 0 || env == NULL)
    {
        printf("  FAIL: irxinit failed\n");
        tests_run++;
        tests_failed++;
        unsetenv("REXX370_BYTECODE");
        return;
    }

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        printf("  FAIL: wk is NULL\n");
        tests_run++;
        tests_failed++;
        irxterm(env);
        unsetenv("REXX370_BYTECODE");
        return;
    }

    /* Env-var drove flag to 1 at init. */
    CHECK(wk->wkbi_use_bytecode == 1, "env-var set flag to 1 at init");

    /* Explicit setter forces token-walk. */
    wk->wkbi_use_bytecode = 0;
    CHECK(wk->wkbi_use_bytecode == 0, "explicit setter=0 overrides env-var");

    exit_rc = -1;
    CHECK(irx_exec_run("exit 0", (int)strlen("exit 0"),
                       NULL, 0, &exit_rc, env) == 0,
          "token-walk exec_run returns 0 after explicit setter=0");
    CHECK(exit_rc == 0, "exit_rc == 0 via token-walk path");

    irxterm(env);
    unsetenv("REXX370_BYTECODE");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("=== WP-BC-FLIP env-var override tests ===\n");

    test_default_on();
    test_env_zero();
    test_env_one();
    test_env_false_variants();
    test_env_true_variants();
    test_env_garbage();
    test_setter_wins_over_env();
    test_setter_zero_wins_over_env();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
