/* ------------------------------------------------------------------ */
/*  tstbcnum.c - WP-BC-NUMERIC: NUMERIC DIGITS/FUZZ/FORM statement     */
/*                                                                    */
/*  Before this WP the bytecode compiler had no handler for the        */
/*  NUMERIC statement (the only NUMERIC opcode, OP_PUSH_NUMERIC, is    */
/*  PARSE NUMERIC — a different construct).  Any program using a        */
/*  NUMERIC DIGITS/FUZZ/FORM statement hit BC_UNSUP_STATEMENT and fell */
/*  back to the token-walk interpreter for the WHOLE program — an open */
/*  decommission gate (CON-20, ROADMAP Axis 2).                        */
/*                                                                    */
/*  WP-BC-NUMERIC adds OP_SET_NUMERIC (src/irx#bcom.c compiler +       */
/*  src/irx#bvm.c VM).  The arith engine already READS the settings    */
/*  (irx#arith.c get_numeric), so writing wkbi_digits/wkbi_fuzz/       */
/*  wkbi_form in the VM is all that is needed; arithmetic, comparison  */
/*  and the OC-ARITH/OC-12 fast-path gates pull the new values         */
/*  automatically.                                                     */
/*                                                                    */
/*  This test verifies:                                               */
/*    1. equivalence vs. token-walk (CON-18 reference) for DIGITS,     */
/*       FUZZ and FORM — constant AND expression value forms;          */
/*    2. the gate is CLOSED: NUMERIC programs now run on the VM        */
/*       (exec>0, fallback==0).  This assertion is RED before the fix  */
/*       (fallback==1) and is the real proof the gate shut — equiv()   */
/*       alone is blind to it, because on UNSUP exec.c falls through    */
/*       to token-walk and both equiv sub-runs are then identical;     */
/*    3. the OC-ARITH/OC-12 fast-path gates still fire: under          */
/*       NUMERIC DIGITS != 9 (rounding) and NUMERIC FUZZ > 0, the       */
/*       results FLIP if the gates were removed and the int32          */
/*       fast-path wrongly took over.                                  */
/*                                                                    */
/*  NUMERIC settings live in the shared work block, so each sub-run    */
/*  resets wkbi_digits/fuzz/form to their defaults first — otherwise   */
/*  a setting from one case would leak into the next.                  */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c  */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c  */
/*          ../lstring370/src/lstr#xlt.c"                              */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbcnum test/mvs/tstbcnum.c \                       */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \            */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \            */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \            */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \            */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \            */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \           */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c \            */
/*        $LSRC && /tmp/tstbcnum                                      */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxbctl.h"
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
/*  NUMERIC settings live in the shared work block.  Reset to the      */
/*  language defaults so a setting from one case cannot leak into the  */
/*  next (and so both equiv sub-runs start from the same state).       */
/* ------------------------------------------------------------------ */

static void numeric_reset(struct irx_wkblk_int *wk)
{
    if (wk != NULL)
    {
        wk->wkbi_digits = NUMERIC_DIGITS_DEFAULT;
        wk->wkbi_fuzz = NUMERIC_FUZZ_DEFAULT;
        wk->wkbi_form = NUMFORM_SCIENTIFIC;
    }
}

/* ------------------------------------------------------------------ */
/*  equiv: token-walk vs bytecode output match (CON-18 reference).     */
/*                                                                    */
/*  NOTE: equiv() proves output correctness but is BLIND to whether    */
/*  the VM actually ran — when the compiler UNSUPs, exec.c falls       */
/*  through to token-walk, so the "bytecode" sub-run IS token-walk and */
/*  the two outputs match trivially.  Gate-closed claims must use      */
/*  bc_exact() (fallback==0), never equiv().                           */
/* ------------------------------------------------------------------ */

static int equiv(struct envblock *env, const char *src, const char *tag)
{
    struct irx_wkblk_int *wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
    int src_len = (int)strlen(src);
    int exit_rc = 0;
    char tw_out[CAPBUF_SIZE];
    char bc_out[CAPBUF_SIZE];
    char label[160];

    if (wk == NULL)
    {
        printf("  FAIL: %s — no work block\n", tag);
        tests_run++;
        tests_failed++;
        return 0;
    }

    cap_reset();
    numeric_reset(wk);
    wk->wkbi_use_bytecode = 0;
    (void)irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    memcpy(tw_out, g_cap, (size_t)(g_cap_len + 1));

    cap_reset();
    numeric_reset(wk);
    wk->wkbi_use_bytecode = 1;
    (void)irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    memcpy(bc_out, g_cap, (size_t)(g_cap_len + 1));

    wk->wkbi_use_bytecode = 0;
    numeric_reset(wk);

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
/*  bc_exact: bytecode run is RC=0, runs fully on the VM (exec>0,       */
/*  fallback==0) and produces exactly the expected output.             */
/*                                                                    */
/*  This is the gate-closed proof AND the fast-path-gate teeth:         */
/*    - fallback==0 is RED before WP-BC-NUMERIC (NUMERIC -> UNSUP ->    */
/*      token-walk, fallback==1) and GREEN after;                       */
/*    - the expected string FLIPS if the OC-ARITH (DIGITS) or OC-12     */
/*      (FUZZ) fast-path gate is removed and the int32 path misfires.   */
/* ------------------------------------------------------------------ */

static int bc_exact(struct envblock *env, const char *src,
                    const char *expected, const char *tag)
{
    struct irx_wkblk_int *wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
    int src_len = (int)strlen(src);
    int exit_rc = 0;
    int rc;
    char label[160];

    if (wk == NULL)
    {
        printf("  FAIL: %s — no work block\n", tag);
        tests_run++;
        tests_failed++;
        return 0;
    }

    cap_reset();
    numeric_reset(wk);
    wk->wkbi_use_bytecode = 1;
    wk->wkbi_bc_exec_count = 0;
    wk->wkbi_bc_fallback_count = 0;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;
    numeric_reset(wk);

    snprintf(label, sizeof(label), "bc-exact: %s", tag);
    CHECK(rc == 0 && exit_rc == 0 && strcmp(g_cap, expected) == 0 &&
              wk->wkbi_bc_exec_count > 0 && wk->wkbi_bc_fallback_count == 0,
          label);

    if (rc != 0 || exit_rc != 0 || strcmp(g_cap, expected) != 0 ||
        wk->wkbi_bc_exec_count == 0 || wk->wkbi_bc_fallback_count != 0)
    {
        printf("    rc=%d exit_rc=%d exec=%d fallback=%d\n", rc, exit_rc,
               wk->wkbi_bc_exec_count, wk->wkbi_bc_fallback_count);
        printf("    expected:[%s]\n", expected);
        printf("    got:     [%s]\n", g_cap);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  NUMERIC DIGITS — constant and expression value forms.              */
/* ------------------------------------------------------------------ */

static void test_digits(struct envblock *env)
{
    printf("\n[NUMERIC DIGITS]\n");

    /* Acceptance #1: 15-digit division result, run on the VM. */
    equiv(env, "numeric digits 15\nsay 1/3", "digits 15: 1/3");
    bc_exact(env, "numeric digits 15\nsay 1/3", "0.333333333333333\n",
             "digits 15: 1/3 = 15 threes (exec>0, fallback==0)");

    /* Smaller DIGITS. */
    equiv(env, "numeric digits 5\nsay 1/3", "digits 5: 1/3");
    equiv(env, "numeric digits 1\nsay 1/3", "digits 1: 1/3");

    /* Acceptance #4: expression value (d=15; NUMERIC DIGITS d). */
    equiv(env, "d=15\nnumeric digits d\nsay 1/3", "digits d (=15): 1/3");
    bc_exact(env, "d=15\nnumeric digits d\nsay 1/3", "0.333333333333333\n",
             "digits expression value (d=15)");

    /* Expression value with an operator (n+2 = 7). */
    equiv(env, "n=5\nnumeric digits n + 2\nsay 1/3",
          "digits expression (n+2=7): 1/3");

    /* Larger DIGITS reach a multi-digit exact product the default-9     */
    /* path would round.  Proves BCD runs at the new DIGITS.            */
    equiv(env, "numeric digits 12\nsay 123456 * 1234567",
          "digits 12: 123456 * 1234567");

    /* DIGITS setting persists across clauses within one program. */
    equiv(env, "numeric digits 4\nsay 2/3\nsay 1/7", "digits 4: persists");
}

/* ------------------------------------------------------------------ */
/*  NUMERIC FUZZ — comparison tolerance.                               */
/* ------------------------------------------------------------------ */

static void test_fuzz(struct envblock *env)
{
    printf("\n[NUMERIC FUZZ]\n");

    /* Gate-closed proof: a FUZZ program now runs on the VM. */
    equiv(env, "numeric fuzz 1\nsay 5 = 5", "fuzz 1: 5 = 5");
    bc_exact(env, "numeric fuzz 1\nsay 5 = 5", "1\n",
             "fuzz 1: trivial equal (exec>0, fallback==0)");

    /* Acceptance #2 + OC-12 gate teeth: two DISTINCT int32-cacheable     */
    /* integers (1000, 1005) that irx_arith_compare declares EQUAL under  */
    /* FUZZ 2 at DIGITS 4 (tolerance 10^(3-4+2)=10).  Both are LINTEGER,  */
    /* so the OC-12 fast-path would compare them raw and print "0" if its */
    /* fuzz==0 guard were removed; with the guard the BCD path prints     */
    /* "1".  equiv also catches gate removal (bytecode would then         */
    /* diverge from the token-walk).                                      */
    equiv(env, "numeric digits 4\nnumeric fuzz 2\nsay 1000 = 1005",
          "fuzz 2: 1000 = 1005 within tolerance");
    bc_exact(env, "numeric digits 4\nnumeric fuzz 2\nsay 1000 = 1005", "1\n",
             "fuzz 2: distinct ints equal (OC-12 gate fires)");

    /* Same pair with FUZZ 0 is NOT equal — the fast-path is correct.   */
    equiv(env, "numeric digits 4\nsay 1000 = 1005",
          "fuzz 0: 1000 = 1005 distinct");
    bc_exact(env, "numeric digits 4\nsay 1000 = 1005", "0\n",
             "fuzz 0: distinct integers (fast-path correct)");

    /* FUZZ with an expression value. */
    equiv(env, "f=2\nnumeric digits 4\nnumeric fuzz f\nsay 1000 = 1005",
          "fuzz expression value (f=2)");
}

/* ------------------------------------------------------------------ */
/*  NUMERIC FORM — SCIENTIFIC vs ENGINEERING formatting.               */
/*                                                                    */
/*  The value must be large enough to force exponential notation, or   */
/*  SCIENTIFIC and ENGINEERING print identically.                      */
/* ------------------------------------------------------------------ */

static void test_form(struct envblock *env)
{
    printf("\n[NUMERIC FORM]\n");

    /* Acceptance #3: ENGINEERING affects the exponent (multiple of 3). */
    equiv(env, "numeric form engineering\nsay 1e10 + 0",
          "form engineering: 1e10");
    equiv(env, "numeric form scientific\nsay 1e10 + 0",
          "form scientific: 1e10");
    bc_exact(env, "numeric form engineering\nsay 1e10 + 0", "10E+9\n",
             "form engineering: 1e10 -> 10E+9 (exec>0, fallback==0)");
    bc_exact(env, "numeric form scientific\nsay 1e10 + 0", "1E+10\n",
             "form scientific: 1e10 -> 1E+10 (exec>0, fallback==0)");

    /* A value whose SCI and ENG renderings clearly differ. */
    equiv(env, "numeric form engineering\nsay 12345678 * 1000",
          "form engineering: 12345678*1000");
    equiv(env, "numeric form scientific\nsay 12345678 * 1000",
          "form scientific: 12345678*1000");

    /* FORM combined with DIGITS in the same program. */
    equiv(env, "numeric digits 5\nnumeric form engineering\nsay 1/3",
          "form engineering + digits 5");
}

/* ------------------------------------------------------------------ */
/*  Fast-path-gate teeth: at DIGITS != 9 the OC-ARITH int32 fast-path  */
/*  MUST yield to the BCD engine.  '999 + 1' rounds to exponential at   */
/*  DIGITS 3; a misfiring fast-path would print "1000".  The result    */
/*  flips the instant the gate is removed.                             */
/* ------------------------------------------------------------------ */

static void test_gate_teeth(struct envblock *env)
{
    printf("\n[fast-path-gate teeth: DIGITS != 9 -> BCD path]\n");

    /* 123*123 = 15129 (5 significant digits).  At DIGITS 3 the BCD       */
    /* engine rounds it to "1.51E+4"; the int32 fast-path (if the         */
    /* DIGITS==9 gate were removed) would print the un-rounded "15129".   */
    /* The result lies inside the 9-digit window, so try_arith_fast does  */
    /* NOT bail on its own — only the gate keeps it on the BCD path.      */
    /* equiv pins the rounded form against the token-walk; bc_exact       */
    /* proves it ran on the VM and did NOT take the int32 path.           */
    equiv(env, "numeric digits 3\nsay 123 * 123", "digits 3: 123*123 rounds");
    bc_exact(env, "numeric digits 3\nsay 123 * 123", "1.51E+4\n",
             "digits 3: 123*123 -> 1.51E+4 (OC-ARITH gate fires, not 15129)");

    /* At DIGITS 12 a 12-digit product survives un-rounded; here the      */
    /* int32 fast-path bails on the >9-digit window regardless of the     */
    /* gate, so this just confirms multi-digit arithmetic at raised       */
    /* DIGITS runs correctly on the VM.                                   */
    equiv(env, "numeric digits 12\nsay 123456 * 1234567",
          "digits 12: 12-digit product exact");
}

/* ------------------------------------------------------------------ */
/*  Regression: a program with NO NUMERIC statement is unchanged and   */
/*  still runs the int32 fast-path at the default DIGITS 9.            */
/* ------------------------------------------------------------------ */

static void test_regression(struct envblock *env)
{
    printf("\n[regression: no NUMERIC -> default DIGITS 9, fast-path]\n");

    equiv(env, "say 1/3", "default: 1/3 = 9 threes");
    bc_exact(env, "say 1/3", "0.333333333\n", "default: 1/3 -> 9 threes");
    bc_exact(env, "say 5 + 7", "12\n", "default: 5+7 fast-path intact");
    bc_exact(env, "say 100000000 = 100000001", "0\n",
             "default: distinct ints, fuzz 0 fast-path");
}

/* ------------------------------------------------------------------ */
/*  NUMERIC as a variable name — `numeric = 5` is an assignment, NOT   */
/*  the NUMERIC instruction (mirrors the token-walk tok_is_kw).  The   */
/*  dispatch guard must let it compile in-bytecode (no fallback).      */
/* ------------------------------------------------------------------ */

static void test_keyword_as_variable(struct envblock *env)
{
    printf("\n[NUMERIC as a variable name (assignment, not instruction)]\n");

    equiv(env, "numeric = 5\nsay numeric", "numeric = 5 (assignment)");
    bc_exact(env, "numeric = 5\nsay numeric", "5\n",
             "numeric=5 assignment runs on VM (guard, fallback==0)");
}

/* ------------------------------------------------------------------ */
/*  OP_SIZE(OP_SET_NUMERIC) correctness.  The VM's label pre-scan walks */
/*  the WHOLE bytecode via OP_SIZE for every program.  A wrong size     */
/*  (e.g. the default 1 instead of 2) desyncs the scan and mis-resolves */
/*  any label that follows a NUMERIC statement.  This program CALLs a   */
/*  label defined AFTER the NUMERIC, so a bad OP_SIZE corrupts the      */
/*  label_pc table and the CALL result.                                 */
/* ------------------------------------------------------------------ */

static void test_opsize_label_prescan(struct envblock *env)
{
    static const char *prog =
        "numeric digits 5\n"
        "call sub\n"
        "say result\n"
        "exit\n"
        "sub:\n"
        "return 1 / 7";

    printf("\n[OP_SIZE: label pre-scan skips OP_SET_NUMERIC correctly]\n");

    equiv(env, prog, "label after NUMERIC resolves (CALL sub)");
    bc_exact(env, prog, "0.14286\n",
             "CALL label after NUMERIC (OP_SIZE correct, fallback==0)");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-NUMERIC: NUMERIC DIGITS/FUZZ/FORM statement ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbcnum: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_digits(env);
    test_fuzz(env);
    test_form(env);
    test_gate_teeth(env);
    test_keyword_as_variable(env);
    test_opsize_label_prescan(env);
    test_regression(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
