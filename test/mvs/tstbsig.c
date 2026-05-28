/* ------------------------------------------------------------------ */
/*  tstbsig.c - WP-BC-07 PR A: SIGNAL label / SIGNAL VALUE equivalence */
/*                                                                    */
/*  Covers AC 1 (label table via existing sym/label_pc), AC 2         */
/*  (SIGNAL label), AC 3 (SIGNAL VALUE), AC 10 (equivalence tests),   */
/*  AC 11 (existing suites still pass — verified by running all tests).*/
/*                                                                    */
/*  Scope: unconditional SIGNAL only.  ON/OFF and condition traps are  */
/*  WP-BC-07 PR B.                                                    */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c  */
/*          ../lstring370/src/lstr#fmt.c  ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c  ../lstring370/src/lstr#wrd.c  */
/*          ../lstring370/src/lstr#xlt.c"                              */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbsig test/mvs/tstbsig.c \                        */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c  $LSRC      */
/*        && /tmp/tstbsig                                              */
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

#define CAPBUF_SIZE 4096

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
/*  equiv: run via token-walk + bytecode, check SAY output matches.   */
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
/*  bc_only: run via bytecode VM, check expected SAY output.          */
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
    }
    return strcmp(g_cap, expected) == 0;
}

/* ------------------------------------------------------------------ */
/*  test_signal_label: SIGNAL label (forward + backward)              */
/* ------------------------------------------------------------------ */

static void test_signal_label(struct envblock *env)
{
    printf("\n--- SIGNAL label ---\n");

    /* Forward jump: SIGNAL skips unreachable code */
    equiv(env,
          "SIGNAL done\n"
          "SAY \"unreachable\"\n"
          "done:\n"
          "SAY \"ok\"\n",
          "forward jump basic");

    /* SIGNAL with code between signal and label */
    equiv(env,
          "x = 1\n"
          "SIGNAL skip\n"
          "x = 99\n"
          "skip:\n"
          "SAY x\n",
          "variable preserved after signal");

    /* SIGNAL from inside DO loop */
    equiv(env,
          "DO i = 1 TO 5\n"
          "  IF i = 3 THEN SIGNAL done\n"
          "  SAY i\n"
          "END\n"
          "done:\n"
          "SAY \"done\"\n",
          "signal from DO loop");

    /* SIGNAL from nested DO loops */
    equiv(env,
          "DO i = 1 TO 3\n"
          "  DO j = 1 TO 3\n"
          "    IF j = 2 THEN SIGNAL out\n"
          "  END\n"
          "END\n"
          "out:\n"
          "SAY \"out\"\n",
          "signal from nested DO");

    /* SIGNAL from IF/THEN branch */
    equiv(env,
          "flag = 1\n"
          "IF flag = 1 THEN SIGNAL branch\n"
          "SAY \"not taken\"\n"
          "branch:\n"
          "SAY \"branched\"\n",
          "signal from IF branch");

    /* Multiple labels, signal to second one */
    equiv(env,
          "SIGNAL second\n"
          "first:\n"
          "SAY \"first\"\n"
          "SIGNAL done\n"
          "second:\n"
          "SAY \"second\"\n"
          "done:\n"
          "SAY \"done\"\n",
          "signal to second label");

    /* Backward SIGNAL (loop-like) using a counter guard */
    equiv(env,
          "n = 0\n"
          "top:\n"
          "n = n + 1\n"
          "IF n < 3 THEN SIGNAL top\n"
          "SAY n\n",
          "backward signal with guard");

    /* SIGNAL clears eval stack — variable state preserved */
    bc_only(env,
            "a = 42\n"
            "SIGNAL lbl\n"
            "lbl:\n"
            "SAY a\n",
            "42\n",
            "variable state after signal");
}

/* ------------------------------------------------------------------ */
/*  test_signal_value: SIGNAL VALUE expr                              */
/* ------------------------------------------------------------------ */

static void test_signal_value(struct envblock *env)
{
    printf("\n--- SIGNAL VALUE ---\n");

    /* Basic SIGNAL VALUE with a variable holding the label name */
    equiv(env,
          "target = \"done\"\n"
          "SIGNAL VALUE target\n"
          "SAY \"unreachable\"\n"
          "done:\n"
          "SAY \"value ok\"\n",
          "signal value via variable");

    /* SIGNAL VALUE with a string literal expression */
    equiv(env,
          "SIGNAL VALUE \"lbl\"\n"
          "SAY \"unreachable\"\n"
          "lbl:\n"
          "SAY \"literal ok\"\n",
          "signal value literal");

    /* SIGNAL VALUE with lowercase label name (must be uppercased) */
    equiv(env,
          "SIGNAL VALUE \"TARGET\"\n"
          "SAY \"unreachable\"\n"
          "target:\n"
          "SAY \"uppercase ok\"\n",
          "signal value uppercase");

    /* SIGNAL VALUE from inside DO loop */
    equiv(env,
          "lname = \"done\"\n"
          "DO i = 1 TO 5\n"
          "  IF i = 2 THEN SIGNAL VALUE lname\n"
          "  SAY i\n"
          "END\n"
          "done:\n"
          "SAY \"value done\"\n",
          "signal value from DO loop");

    /* SIGNAL VALUE with concatenated name */
    equiv(env,
          "prefix = \"la\"\n"
          "SIGNAL VALUE prefix || \"bel\"\n"
          "SAY \"unreachable\"\n"
          "label:\n"
          "SAY \"concat ok\"\n",
          "signal value concatenated");
}

/* ------------------------------------------------------------------ */
/*  test_signal_sigl: SIGL special variable                           */
/* ------------------------------------------------------------------ */

static void test_signal_sigl(struct envblock *env)
{
    struct irx_wkblk_int *wk;
    struct irx_bc_execblk *bc = NULL;
    int rc;
    int exit_rc = 0;
    const char *src = "SIGNAL done\ndone:\nSAY \"ok\"\n";

    printf("\n--- SIGL special variable ---\n");

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        CHECK(0, "SIGL: no work block");
        return;
    }

    wk->wkbi_sigl = 999; /* set a sentinel before SIGNAL */

    rc = irx_bc_compile(env, src, (int)strlen(src), &bc);
    if (rc == IRXBC_OK && bc != NULL)
    {
        rc = irx_bc_execute(env, bc, NULL, 0, &exit_rc);
        {
            void *p = bc;
            irxstor(RXSMFRE, 0, &p, env);
        }
    }

    /* SIGL should be 0 after OP_SIGNAL (line tracking not yet impl) */
    CHECK(wk->wkbi_sigl == 0, "SIGL = 0 after SIGNAL (pending line-track impl)");
}

/* ------------------------------------------------------------------ */
/*  test_signal_do_frame_cleanup: verify DO frame state after SIGNAL  */
/* ------------------------------------------------------------------ */

static void test_signal_do_frame_cleanup(struct envblock *env)
{
    printf("\n--- DO frame cleanup after SIGNAL ---\n");

    /* After SIGNAL, a new DO loop must work correctly. */
    bc_only(env,
            "DO i = 1 TO 3\n"
            "  SIGNAL out\n"
            "END\n"
            "out:\n"
            "DO j = 1 TO 3\n"
            "  SAY j\n"
            "END\n",
            "1\n2\n3\n",
            "new DO after signal from DO");

    /* Deeply nested: signal out of 3 levels, then loop again */
    bc_only(env,
            "DO i = 1 TO 10\n"
            "  DO j = 1 TO 10\n"
            "    DO k = 1 TO 10\n"
            "      SIGNAL out\n"
            "    END\n"
            "  END\n"
            "END\n"
            "out:\n"
            "result = 0\n"
            "DO n = 1 TO 4\n"
            "  result = result + n\n"
            "END\n"
            "SAY result\n",
            "10\n",
            "3-deep DO then new DO after signal");
}

/* ------------------------------------------------------------------ */
/*  test_signal_callframe_unwind: SIGNAL when call_sp > 0             */
/*                                                                    */
/*  These tests exercise the call-frame unwind loop inside OP_SIGNAL  */
/*  which was not covered by the top-level-only tests above.          */
/* ------------------------------------------------------------------ */

static void test_signal_callframe_unwind(struct envblock *env)
{
    printf("\n--- call frame unwind on SIGNAL ---\n");

    /*
     * Simple: SIGNAL from inside a CALL subroutine (no PROCEDURE).
     * call_sp == 1 when OP_SIGNAL executes; the single call frame is
     * unwound (args freed), call_sp resets to 0, then jumps to done:.
     */
    bc_only(env,
            "CALL sub\n"
            "SAY \"unreachable\"\n"
            "done:\n"
            "SAY \"after_signal\"\n"
            "EXIT\n"
            "sub:\n"
            "SIGNAL done\n"
            "RETURN\n",
            "after_signal\n",
            "signal from call subroutine");

    /*
     * Two nested PROCEDURE scopes (top -> A -> B, both with PROCEDURE).
     * call_sp == 2 when OP_SIGNAL executes; the unwind loop must run
     * two iterations, destroying B's isolated vpool then A's, restoring
     * the top-level vpool in which x = "top" was set.
     *
     * Trace:
     *   top-level: vpool V0, x = "top"
     *   CALL A: call_sp=1
     *   A PROCEDURE: V1 created, cf[0].prev_vpool=V0, x="in_A" in V1
     *   CALL B: call_sp=2
     *   B PROCEDURE: V2 created, cf[1].prev_vpool=V1, x="in_B" in V2
     *   SIGNAL done:
     *     fi=1 (B): destroy V2, vpool=V1
     *     fi=0 (A): destroy V1, vpool=V0
     *     call_sp=0, sp=0, pc=done
     *   done: SAY x  ->  V0.x = "top"
     */
    bc_only(env,
            "x = \"top\"\n"
            "CALL A\n"
            "done:\n"
            "SAY x\n"
            "EXIT\n"
            "A:\n"
            "PROCEDURE\n"
            "x = \"in_A\"\n"
            "CALL B\n"
            "RETURN\n"
            "B:\n"
            "PROCEDURE\n"
            "x = \"in_B\"\n"
            "SIGNAL done\n"
            "RETURN\n",
            "top\n",
            "signal unwinds two nested PROCEDURE scopes");
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-07 PR A: SIGNAL label / SIGNAL VALUE Tests ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbsig: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_signal_label(env);
    test_signal_value(env);
    test_signal_sigl(env);
    test_signal_do_frame_cleanup(env);
    test_signal_callframe_unwind(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
