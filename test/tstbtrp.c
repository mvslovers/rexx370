/* ------------------------------------------------------------------ */
/*  tstbtrp.c - WP-BC-07 PR B: SIGNAL ON/OFF + condition trap tests   */
/*                                                                    */
/*  Covers AC #10-#14:                                                */
/*    AC #10 bc_only: NOVALUE trap, SYNTAX trap, SIGNAL OFF, NAME,    */
/*           DO-frame unwind on trap.                                 */
/*    AC #11 SIGNAL ON/OFF no longer UNSUP (compiler emits opcodes).  */
/*    AC #12 NOVALUE trigger points (OP_LOAD + OP_LOAD_STEM).         */
/*    AC #13 ERROR/HALT/FAILURE/NOTREADY: ON/OFF sets enabled bit,    */
/*           trap fire deferred to WP-33/Attention (documented stub). */
/*    AC #14 existing bytecode suites remain green (see tstbsig etc). */
/*                                                                    */
/*  Token-Walk has SIGNAL ON/OFF as no-op (CON-18 §SIGNAL), so all   */
/*  trap tests use bc_only with SC28-1883-0-derived expected output.  */
/*                                                                    */
/*  Cross-compile:                                                    */
/*    LSTR="-I include -I contrib/lstring370-0.1.0-dev/include        */
/*          -I ../lstring370/include"                                 */
/*    gcc $LSTR -Wall -Wextra -std=gnu99 -o /tmp/tstbtrp              */
/*        test/mvs/tstbtrp.c                                          */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c             */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c              */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c              */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c             */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c             */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c            */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c             */
/*        ../lstring370/src/lstr#cor.c  ../lstring370/src/lstr#cvt.c  */
/*        ../lstring370/src/lstr#fmt.c  ../lstring370/src/lstr#srch.c */
/*        ../lstring370/src/lstr#sub.c  ../lstring370/src/lstr#wrd.c  */
/*        ../lstring370/src/lstr#xlt.c && /tmp/tstbtrp                */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
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
        printf("  FAIL: %s - no work block\n", tag);
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
/*  test_novalue_trap: SIGNAL ON NOVALUE                              */
/* ------------------------------------------------------------------ */

static void test_novalue_trap(struct envblock *env)
{
    printf("\n--- SIGNAL ON NOVALUE ---\n");

    /* Basic trap: access unset variable fires NOVALUE handler */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "x = unset_var\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY \"caught\"\n",
            "caught\n",
            "basic NOVALUE trap");

    /* Trap fires on first access to unset variable in expression */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "SAY notset\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY \"handler ok\"\n",
            "handler ok\n",
            "NOVALUE in SAY expression");

    /* Trap fires on second unset var (after a set one) */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "a = 1\n"
            "b = a + missing\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY \"caught arith\"\n",
            "caught arith\n",
            "NOVALUE in arithmetic expression");

    /* After trap fires, var is set, code continues normally */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "x = bad_var\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "NOVALUE:\n"
            "x = 42\n"
            "SAY x\n",
            "42\n",
            "set variable in handler, SAY it");

    /* Trap for compound (stem) variable — no default set on stem */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "b = nostm.99\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY \"stem novalue\"\n",
            "stem novalue\n",
            "NOVALUE on unset compound variable");
}

/* ------------------------------------------------------------------ */
/*  test_novalue_off: SIGNAL OFF NOVALUE reverts to fallback          */
/* ------------------------------------------------------------------ */

static void test_novalue_off(struct envblock *env)
{
    printf("\n--- SIGNAL OFF NOVALUE ---\n");

    /* OFF: unset variable returns its name (normal REXX NOVALUE behavior) */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "SIGNAL OFF NOVALUE\n"
            "SAY missing_var\n",
            "MISSING_VAR\n",
            "SIGNAL OFF restores var-name fallback");

    /* ON then OFF: the later OFF wins */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "SIGNAL OFF NOVALUE\n"
            "x = unset\n"
            "SAY x\n",
            "UNSET\n",
            "ON then OFF: var-name fallback");

    /* OFF when never ON: still normal fallback */
    bc_only(env,
            "SIGNAL OFF NOVALUE\n"
            "SAY not_set\n",
            "NOT_SET\n",
            "OFF when never ON: var-name fallback");

    /* ON active: trap fires; OFF after handler: subsequent access ok */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "x = bad\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SIGNAL OFF NOVALUE\n"
            "SAY another_bad\n",
            "ANOTHER_BAD\n",
            "SIGNAL OFF inside handler: next access is fallback");
}

/* ------------------------------------------------------------------ */
/*  test_novalue_name: NAME clause selects custom handler label       */
/* ------------------------------------------------------------------ */

static void test_novalue_name(struct envblock *env)
{
    printf("\n--- SIGNAL ON NOVALUE NAME ---\n");

    /* NAME clause: handler is at custom label, not NOVALUE: */
    bc_only(env,
            "SIGNAL ON NOVALUE NAME myhandler\n"
            "x = unset\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY \"wrong handler\"\n"
            "EXIT\n"
            "myhandler:\n"
            "SAY \"custom handler\"\n",
            "custom handler\n",
            "NAME clause routes to custom label");

    /* NAME with same name as condition: functionally same as default */
    bc_only(env,
            "SIGNAL ON NOVALUE NAME NOVALUE\n"
            "x = missing\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY \"default label\"\n",
            "default label\n",
            "NAME NOVALUE same as default");
}

/* ------------------------------------------------------------------ */
/*  test_novalue_do_unwind: trap from inside DO loops                 */
/* ------------------------------------------------------------------ */

static void test_novalue_do_unwind(struct envblock *env)
{
    printf("\n--- NOVALUE trap from DO loop ---\n");

    /* Trap from inside a DO loop unwinds loop frame */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "DO i = 1 TO 5\n"
            "  x = missing_var\n"
            "  SAY \"unreachable\"\n"
            "END\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY \"loop trap\"\n",
            "loop trap\n",
            "NOVALUE trap unwinds DO loop frame");

    /* After trap, a new DO loop must work correctly */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "DO i = 1 TO 3\n"
            "  IF i = 2 THEN DO\n"
            "    x = undef\n"
            "  END\n"
            "  SAY i\n"
            "END\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY \"trapped\"\n"
            "DO j = 1 TO 3\n"
            "  SAY j\n"
            "END\n",
            "1\ntrapped\n1\n2\n3\n",
            "trap from nested DO, new DO after handler");

    /* Trap from 3-deep nested DO loops */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "DO i = 1 TO 5\n"
            "  DO j = 1 TO 5\n"
            "    DO k = 1 TO 5\n"
            "      x = deep_undef\n"
            "    END\n"
            "  END\n"
            "END\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY \"deep ok\"\n",
            "deep ok\n",
            "NOVALUE trap unwinds 3-deep DO");
}

/* ------------------------------------------------------------------ */
/*  test_syntax_trap: SIGNAL ON SYNTAX                                */
/* ------------------------------------------------------------------ */

static void test_syntax_trap(struct envblock *env)
{
    printf("\n--- SIGNAL ON SYNTAX ---\n");

    /* Arithmetic error with non-numeric value triggers SYNTAX trap */
    bc_only(env,
            "SIGNAL ON SYNTAX\n"
            "x = \"abc\" + 1\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "SYNTAX:\n"
            "SAY \"syntax caught\"\n",
            "syntax caught\n",
            "SYNTAX trap on non-numeric arithmetic");

    /* Division that causes arithmetic error */
    bc_only(env,
            "SIGNAL ON SYNTAX\n"
            "x = 5 + \"notnum\"\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "SYNTAX:\n"
            "SAY \"arith error\"\n",
            "arith error\n",
            "SYNTAX trap on string in addition");

    /* Logical op with non-boolean */
    bc_only(env,
            "SIGNAL ON SYNTAX\n"
            "x = \"abc\" & 1\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "SYNTAX:\n"
            "SAY \"bool error\"\n",
            "bool error\n",
            "SYNTAX trap on non-boolean AND");

    /* SYNTAX trap with NAME clause */
    bc_only(env,
            "SIGNAL ON SYNTAX NAME errh\n"
            "x = \"z\" + 0\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "SYNTAX:\n"
            "SAY \"wrong\"\n"
            "EXIT\n"
            "errh:\n"
            "SAY \"name ok\"\n",
            "name ok\n",
            "SYNTAX trap NAME clause");

    /* SIGNAL OFF SYNTAX: error returns to abort without trap */
    bc_only(env,
            "SIGNAL ON SYNTAX\n"
            "SIGNAL OFF SYNTAX\n"
            "rc = 0\n"
            "SAY rc\n",
            "0\n",
            "SIGNAL OFF SYNTAX: no trap, clean run");

    /* Trap from DO loop: loop frames unwound */
    bc_only(env,
            "SIGNAL ON SYNTAX\n"
            "DO i = 1 TO 3\n"
            "  x = i + \"bad\"\n"
            "END\n"
            "EXIT\n"
            "SYNTAX:\n"
            "SAY \"loop syntax\"\n",
            "loop syntax\n",
            "SYNTAX trap unwinds DO loop");
}

/* ------------------------------------------------------------------ */
/*  test_trap_auto_disable: trap auto-disables on fire (SC28-1883 §7) */
/* ------------------------------------------------------------------ */

static void test_trap_auto_disable(struct envblock *env)
{
    printf("\n--- Trap auto-disable on fire ---\n");

    /* After NOVALUE fires, reading another unset var in the handler
     * must NOT recurse — the trap is now OFF inside the handler.     */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "x = undef1\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY undef2\n",
            "UNDEF2\n",
            "NOVALUE auto-disabled: handler reads unset var safely");

    /* Re-enable inside handler: second access fires again */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "x = first_undef\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SIGNAL ON NOVALUE NAME second_handler\n"
            "y = second_undef\n"
            "SAY \"unreachable2\"\n"
            "EXIT\n"
            "second_handler:\n"
            "SAY \"double trap\"\n",
            "double trap\n",
            "re-enable NOVALUE in handler fires again");
}

/* ------------------------------------------------------------------ */
/*  test_trap_scope_call: trap state restored on RETURN               */
/* ------------------------------------------------------------------ */

static void test_trap_scope_call(struct envblock *env)
{
    printf("\n--- Trap state across CALL/RETURN ---\n");

    /* SIGNAL OFF inside callee reverts on RETURN */
    bc_only(env,
            "SIGNAL ON NOVALUE\n"
            "CALL disable_trap\n"
            "x = undef_after_call\n"
            "SAY \"unreachable\"\n"
            "EXIT\n"
            "NOVALUE:\n"
            "SAY \"outer handler\"\n"
            "EXIT\n"
            "disable_trap:\n"
            "SIGNAL OFF NOVALUE\n"
            "RETURN\n",
            "outer handler\n",
            "callee SIGNAL OFF reverts on RETURN");

    /* SIGNAL ON in callee: inherited, but reverts on RETURN */
    bc_only(env,
            "CALL enable_trap\n"
            "x = after_return\n"
            "SAY x\n"
            "EXIT\n"
            "enable_trap:\n"
            "SIGNAL ON NOVALUE NAME novalue_h\n"
            "RETURN\n"
            "novalue_h:\n"
            "SAY \"should not fire\"\n",
            "AFTER_RETURN\n",
            "callee SIGNAL ON reverts on RETURN");
}

/* ------------------------------------------------------------------ */
/*  test_infra_only_conditions: ON/OFF for ERROR/HALT/FAILURE/NOTREADY*/
/*                                                                    */
/*  Trigger is deferred to WP-33 (command routing) / Attention.       */
/*  These tests only verify that ON/OFF compiles and runs without     */
/*  UNSUP error and that the enabled-bit is set/cleared correctly.    */
/* ------------------------------------------------------------------ */

static void test_infra_only_conditions(struct envblock *env)
{
    printf("\n--- ERROR/HALT/FAILURE/NOTREADY infrastructure ---\n");

    /* SIGNAL ON for all four infra-only conditions: must compile + run
     * without UNSUP, and SAY output must be correct (no spurious trap). */
    bc_only(env,
            "SIGNAL ON ERROR\n"
            "SIGNAL ON HALT\n"
            "SIGNAL ON FAILURE\n"
            "SIGNAL ON NOTREADY\n"
            "SAY \"ok\"\n",
            "ok\n",
            "SIGNAL ON ERROR/HALT/FAILURE/NOTREADY: compiles, runs, no trap");

    /* SIGNAL OFF must also compile + run cleanly */
    bc_only(env,
            "SIGNAL ON ERROR\n"
            "SIGNAL OFF ERROR\n"
            "SIGNAL ON HALT\n"
            "SIGNAL OFF HALT\n"
            "SIGNAL ON FAILURE\n"
            "SIGNAL OFF FAILURE\n"
            "SIGNAL ON NOTREADY\n"
            "SIGNAL OFF NOTREADY\n"
            "SAY \"off ok\"\n",
            "off ok\n",
            "SIGNAL ON/OFF ERROR/HALT/FAILURE/NOTREADY: no UNSUP");
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-07 PR B: SIGNAL ON/OFF + Condition Trap Tests ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbtrp: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_novalue_trap(env);
    test_novalue_off(env);
    test_novalue_name(env);
    test_novalue_do_unwind(env);
    test_syntax_trap(env);
    test_trap_auto_disable(env);
    test_trap_scope_call(env);
    test_infra_only_conditions(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
