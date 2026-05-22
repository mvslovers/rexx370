/* ------------------------------------------------------------------ */
/*  tstbpro.c - WP-BC-05 PR B: PROCEDURE EXPOSE bytecode tests        */
/*                                                                    */
/*  For each REXX snippet, runs via the token-walk interpreter and    */
/*  the bytecode VM, then verifies both produce identical SAY output. */
/*  bc_only() tests are used where expected output is hard-coded.    */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c */
/*          ../lstring370/src/lstr#xlt.c"                             */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbpro test/mvs/tstbpro.c \                        */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   $LSRC && /tmp/tstbpro      */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxbops.h"
#include "irxbvm.h"
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
/*  Output capture                                                    */
/* ------------------------------------------------------------------ */

#define CAPBUF_SIZE 8192

static char g_cap[CAPBUF_SIZE];
static int g_cap_len = 0;

static void cap_reset(void)
{
    g_cap_len = 0;
    g_cap[0] = '\0';
}

static int capture_io(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;
    if (function == RXFWRITE && data != NULL && Lpstr(data) != NULL)
    {
        int n = (int)Llen(data);
        if (g_cap_len + n + 1 < CAPBUF_SIZE)
        {
            memcpy(g_cap + g_cap_len, Lpstr(data), (size_t)n);
            g_cap_len += n;
            g_cap[g_cap_len++] = '\n';
            g_cap[g_cap_len] = '\0';
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  equiv: run via token-walk and bytecode; check outputs match.      */
/* ------------------------------------------------------------------ */

static int equiv(struct envblock *env, const char *src, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int rc;
    int exit_rc = 0;
    char tw_out[CAPBUF_SIZE];
    char bc_out[CAPBUF_SIZE];
    char label[128];

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        printf("  FAIL: %s — no work block\n", tag);
        tests_run++;
        tests_failed++;
        return 0;
    }

    cap_reset();
    wk->wkbi_use_bytecode = 0;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    (void)rc;
    memcpy(tw_out, g_cap, (size_t)(g_cap_len + 1));

    cap_reset();
    wk->wkbi_use_bytecode = 1;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    (void)rc;
    memcpy(bc_out, g_cap, (size_t)(g_cap_len + 1));

    wk->wkbi_use_bytecode = 0;

    snprintf(label, sizeof(label), "equiv: %s", tag);
    CHECK(strcmp(tw_out, bc_out) == 0, label);

    if (strcmp(tw_out, bc_out) != 0)
    {
        printf("    tokwalk: [%s]\n", tw_out);
        printf("    bytecode:[%s]\n", bc_out);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  bc_only: run only via bytecode VM; check expected output.         */
/* ------------------------------------------------------------------ */

static int bc_only(struct envblock *env, const char *src,
                   const char *expected, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int rc;
    int exit_rc = 0;
    char label[128];

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        printf("  FAIL: %s — no work block\n", tag);
        tests_run++;
        tests_failed++;
        return 0;
    }

    cap_reset();
    wk->wkbi_use_bytecode = 1;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    (void)rc;
    wk->wkbi_use_bytecode = 0;

    snprintf(label, sizeof(label), "bc_only: %s", tag);
    CHECK(strcmp(g_cap, expected) == 0, label);

    if (strcmp(g_cap, expected) != 0)
    {
        printf("    expected:[%s]\n", expected);
        printf("    got:     [%s]\n", g_cap);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  PROCEDURE — scope isolation (no EXPOSE)                           */
/* ------------------------------------------------------------------ */

static void test_proc_isolation(struct envblock *env)
{
    printf("\n[PROCEDURE — scope isolation]\n");

    /* Callee with PROCEDURE cannot see caller's variable */
    equiv(env,
          "x = 'outer'\n"
          "CALL sub\n"
          "SAY x\n"
          "EXIT\n"
          "sub:\n"
          "PROCEDURE\n"
          "SAY x\n" /* x is uninitialized inside sub */
          "RETURN\n",
          "PROCEDURE hides caller x");

    /* PROCEDURE callee's local variable does not reach caller */
    equiv(env,
          "x = 'original'\n"
          "CALL sub\n"
          "SAY x\n"
          "EXIT\n"
          "sub:\n"
          "PROCEDURE\n"
          "x = 'modified'\n"
          "RETURN\n",
          "PROCEDURE callee write does not reach caller");

    /* Multiple caller variables all hidden */
    equiv(env,
          "a = 1\n"
          "b = 2\n"
          "c = 3\n"
          "CALL sub\n"
          "SAY a b c\n"
          "EXIT\n"
          "sub:\n"
          "PROCEDURE\n"
          "a = 99\n"
          "RETURN\n",
          "PROCEDURE hides multiple caller vars");

    /* PROCEDURE with ARG — argument is accessible */
    equiv(env,
          "CALL greet 'world'\n"
          "EXIT\n"
          "greet:\n"
          "PROCEDURE\n"
          "ARG who\n"
          "SAY 'hello' who\n"
          "RETURN\n",
          "PROCEDURE ARG accessible");
}

/* ------------------------------------------------------------------ */
/*  PROCEDURE EXPOSE — direct variable sharing                        */
/* ------------------------------------------------------------------ */

static void test_proc_expose(struct envblock *env)
{
    printf("\n[PROCEDURE EXPOSE — direct sharing]\n");

    /* Callee modification of exposed var is visible in caller */
    equiv(env,
          "x = 5\n"
          "CALL inc\n"
          "SAY x\n"
          "EXIT\n"
          "inc:\n"
          "PROCEDURE EXPOSE x\n"
          "x = x + 1\n"
          "RETURN\n",
          "EXPOSE: callee increment visible in caller");

    /* Multiple exposed variables */
    equiv(env,
          "x = 1\n"
          "y = 2\n"
          "CALL swap\n"
          "SAY x\n"
          "SAY y\n"
          "EXIT\n"
          "swap:\n"
          "PROCEDURE EXPOSE x y\n"
          "tmp = x\n"
          "x = y\n"
          "y = tmp\n"
          "RETURN\n",
          "EXPOSE: swap two variables");

    /* Non-exposed callee variable does not reach caller */
    equiv(env,
          "a = 10\n"
          "CALL sub\n"
          "SAY a\n"
          "SAY b\n" /* b is uninitialized in caller */
          "EXIT\n"
          "sub:\n"
          "PROCEDURE EXPOSE a\n"
          "b = 99\n"
          "a = a + 1\n"
          "RETURN\n",
          "EXPOSE: non-exposed local does not reach caller");

    /* Exposed var initialised in callee is visible in caller */
    equiv(env,
          "CALL sub\n"
          "SAY result\n"
          "EXIT\n"
          "sub:\n"
          "PROCEDURE EXPOSE result\n"
          "result = 'done'\n"
          "RETURN\n",
          "EXPOSE: callee initializes exposed var in caller");

    /* Caller's value of exposed var visible to callee */
    equiv(env,
          "acc = 100\n"
          "CALL add5\n"
          "SAY acc\n"
          "EXIT\n"
          "add5:\n"
          "PROCEDURE EXPOSE acc\n"
          "acc = acc + 5\n"
          "RETURN\n",
          "EXPOSE: callee reads then writes exposed var");
}

/* ------------------------------------------------------------------ */
/*  PROCEDURE EXPOSE — function call (return value)                   */
/* ------------------------------------------------------------------ */

static void test_proc_return_value(struct envblock *env)
{
    printf("\n[PROCEDURE EXPOSE — return value]\n");

    /* Function expression with PROCEDURE and ARG */
    bc_only(env,
            "SAY double(6)\n"
            "EXIT\n"
            "double:\n"
            "PROCEDURE\n"
            "ARG n\n"
            "RETURN n * 2\n",
            "12\n",
            "PROCEDURE function returns computed value");

    /* Function with EXPOSE accumulates across calls */
    bc_only(env,
            "acc = 0\n"
            "SAY addup(3)\n"
            "SAY addup(4)\n"
            "SAY acc\n"
            "EXIT\n"
            "addup:\n"
            "PROCEDURE EXPOSE acc\n"
            "ARG n\n"
            "acc = acc + n\n"
            "RETURN acc\n",
            "3\n7\n7\n",
            "EXPOSE function accumulates and returns");

    /* RETURN without value: RESULT cleared in caller's scope */
    equiv(env,
          "result = 'old'\n"
          "CALL sub\n"
          "SAY result\n" /* 'result' should be its own name after RETURN */
          "EXIT\n"
          "sub:\n"
          "PROCEDURE\n"
          "RETURN\n",
          "PROCEDURE RETURN clears RESULT in caller");
}

/* ------------------------------------------------------------------ */
/*  PROCEDURE EXPOSE — indirect expose (names)                        */
/* ------------------------------------------------------------------ */

static void test_proc_expose_indirect(struct envblock *env)
{
    printf("\n[PROCEDURE EXPOSE — indirect expose]\n");

    /* Indirect EXPOSE: names = 'a b', expose a and b */
    equiv(env,
          "names = 'a b'\n"
          "a = 10\n"
          "b = 20\n"
          "CALL sub\n"
          "SAY a\n"
          "SAY b\n"
          "EXIT\n"
          "sub:\n"
          "PROCEDURE EXPOSE (names)\n"
          "a = a + 1\n"
          "b = b + 1\n"
          "RETURN\n",
          "EXPOSE (indirect): two vars exposed via name list");

    /* Indirect EXPOSE — empty list is a no-op */
    equiv(env,
          "names = ''\n"
          "x = 42\n"
          "CALL sub\n"
          "SAY x\n"
          "EXIT\n"
          "sub:\n"
          "PROCEDURE EXPOSE (names)\n"
          "x = 99\n"
          "RETURN\n",
          "EXPOSE (indirect): empty list — caller x unchanged");
}

/* ------------------------------------------------------------------ */
/*  PROCEDURE EXPOSE — nested calls                                   */
/* ------------------------------------------------------------------ */

static void test_proc_nested(struct envblock *env)
{
    printf("\n[PROCEDURE EXPOSE — nested calls]\n");

    /* Inner call (no PROCEDURE) inside PROCEDURE EXPOSE function */
    equiv(env,
          "x = 10\n"
          "CALL outer\n"
          "SAY x\n"
          "EXIT\n"
          "outer:\n"
          "PROCEDURE EXPOSE x\n"
          "x = x * 2\n"
          "CALL inner\n"
          "RETURN\n"
          "inner:\n"
          "x = x + 1\n" /* sees outer's scope which has EXPOSE ref */
          "RETURN\n",
          "nested: inner shares outer EXPOSE scope");

    /* Two functions each with PROCEDURE EXPOSE on same variable */
    equiv(env,
          "n = 0\n"
          "CALL f1\n"
          "SAY n\n"
          "EXIT\n"
          "f1:\n"
          "PROCEDURE EXPOSE n\n"
          "n = n + 10\n"
          "CALL f2\n"
          "RETURN\n"
          "f2:\n"
          "PROCEDURE EXPOSE n\n"
          "n = n + 1\n"
          "RETURN\n",
          "nested: both f1 and f2 EXPOSE n");

    /* PROCEDURE function called from expression context */
    bc_only(env,
            "base = 100\n"
            "SAY compute(5)\n"
            "SAY base\n"
            "EXIT\n"
            "compute:\n"
            "PROCEDURE EXPOSE base\n"
            "ARG n\n"
            "base = base + n\n"
            "RETURN base\n",
            "105\n105\n",
            "nested: expression-context call with EXPOSE");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-05 PR B: PROCEDURE EXPOSE Tests ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbpro: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_proc_isolation(env);
    test_proc_expose(env);
    test_proc_return_value(env);
    test_proc_expose_indirect(env);
    test_proc_nested(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
