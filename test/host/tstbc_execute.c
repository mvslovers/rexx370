/* ------------------------------------------------------------------ */
/*  test/host/tstbc_execute.c — WP-BC-01 VM execution tests           */
/*                                                                    */
/*  Verifies that irx_bc_execute() runs compiled bytecode correctly   */
/*  and that the end-to-end path (irx_exec_run with wkbi_use_bytecode */
/*  = 1) produces the correct exit codes.                             */
/*                                                                    */
/*  Build (Linux):                                                     */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -O0 -g \                           */
/*        -o /tmp/tstbc_execute test/host/tstbc_execute.c \           */
/*        'src/irx#init.c'  'src/irx#term.c'  'src/irx#stor.c' \    */
/*        'src/irx#anch.c'  'src/irx#env.c'   'src/irx#uid.c'  \    */
/*        'src/irx#msid.c'  'src/irx#cond.c'  'src/irx#bif.c'  \    */
/*        'src/irx#bifs.c'  'src/irx#io.c'    'src/irx#lstr.c' \    */
/*        'src/irx#tokn.c'  'src/irx#vpol.c'  'src/irx#pars.c' \    */
/*        'src/irx#ctrl.c'  'src/irx#exec.c'  'src/irx#arith.c' \   */
/*        'src/irx#bcom.c'  'src/irx#bvm.c' \                        */
/*        '../lstring370/src/lstr#cor.c'  \                           */
/*        '../lstring370/src/lstr#cvt.c'  \                           */
/*        '../lstring370/src/lstr#fmt.c'  \                           */
/*        '../lstring370/src/lstr#srch.c' \                           */
/*        '../lstring370/src/lstr#sub.c'  \                           */
/*        '../lstring370/src/lstr#wrd.c'  \                           */
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

/* ------------------------------------------------------------------ */
/*  Compile source and execute via the VM directly.                   */
/* ------------------------------------------------------------------ */
static void test_execute_empty(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    int bc_rc = -1;
    int rc;

    printf("  [execute: empty source]\n");

    rc = irx_bc_compile(env, "/* nothing */",
                        (int)strlen("/* nothing */"), &bc);
    CHECK(rc == IRXBC_OK, "compile succeeds");
    if (bc == NULL)
    {
        return;
    }

    rc = irx_bc_execute(env, bc, NULL, 0, &bc_rc);
    CHECK(rc == IRXBC_OK, "execute returns IRXBC_OK");
    CHECK(bc_rc == 0, "program RC == 0");

    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

static void test_execute_exit(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    int bc_rc = -1;
    int rc;

    printf("  [execute: exit]\n");

    rc = irx_bc_compile(env, "exit", (int)strlen("exit"), &bc);
    CHECK(rc == IRXBC_OK, "compile succeeds");
    if (bc == NULL)
    {
        return;
    }

    rc = irx_bc_execute(env, bc, NULL, 0, &bc_rc);
    CHECK(rc == IRXBC_OK, "execute returns IRXBC_OK");
    CHECK(bc_rc == 0, "program RC == 0");

    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

static void test_execute_null(struct envblock *env)
{
    int bc_rc = -1;
    int rc;

    printf("  [execute: NULL container]\n");

    rc = irx_bc_execute(env, NULL, NULL, 0, &bc_rc);
    CHECK(rc != IRXBC_OK, "execute rejects NULL container");
}

/* ------------------------------------------------------------------ */
/*  End-to-end: irx_exec_run with wkbi_use_bytecode = 1.             */
/* ------------------------------------------------------------------ */
static void test_e2e_bytecode_flag(void)
{
    struct envblock *env = NULL;
    struct irx_wkblk_int *wk;
    int exit_rc = -1;
    int rc;

    printf("  [e2e: wkbi_use_bytecode=1, source=exit]\n");

    rc = irxinit(NULL, &env);
    CHECK(rc == 0 && env != NULL, "irxinit succeeds");
    if (rc != 0 || env == NULL)
    {
        return;
    }

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    CHECK(wk != NULL, "work block is present");
    if (wk == NULL)
    {
        irxterm(env);
        return;
    }

    wk->wkbi_use_bytecode = 1;

    rc = irx_exec_run("exit", (int)strlen("exit"), NULL, 0, &exit_rc, env);
    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit_rc == 0");

    wk->wkbi_use_bytecode = 0;

    /* Token-walk path still works after disabling bytecode. */
    rc = irx_exec_run("exit", (int)strlen("exit"), NULL, 0, &exit_rc, env);
    CHECK(rc == 0, "token-walk path still returns 0");
    CHECK(exit_rc == 0, "token-walk exit_rc == 0");

    irxterm(env);
}

static void test_e2e_empty_bytecode(void)
{
    struct envblock *env = NULL;
    struct irx_wkblk_int *wk;
    int exit_rc = -1;
    int rc;

    printf("  [e2e: wkbi_use_bytecode=1, source=empty]\n");

    rc = irxinit(NULL, &env);
    CHECK(rc == 0 && env != NULL, "irxinit succeeds");
    if (rc != 0 || env == NULL)
    {
        return;
    }

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        irxterm(env);
        return;
    }

    wk->wkbi_use_bytecode = 1;

    rc = irx_exec_run("/* nothing */", (int)strlen("/* nothing */"),
                      NULL, 0, &exit_rc, env);
    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit_rc == 0");

    irxterm(env);
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    struct envblock *env = NULL;
    int rc;

    printf("=== WP-BC-01 VM Execution Tests ===\n\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbc_execute: irxinit failed rc=%d\n", rc);
        return 1;
    }

    test_execute_empty(env);
    test_execute_exit(env);
    test_execute_null(env);

    irxterm(env);

    /* End-to-end tests create their own environments. */
    test_e2e_bytecode_flag();
    test_e2e_empty_bytecode();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
