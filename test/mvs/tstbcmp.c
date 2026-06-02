/* ------------------------------------------------------------------ */
/*  tstbcmp.c - WP-BC-OC12: numeric-compare int_cache fast-path        */
/*                                                                    */
/*  The bytecode VM comparison opcodes (OP_EQ/OP_NE/OP_LT/OP_LE/       */
/*  OP_GT/OP_GE) used to call irx_arith_compare with the .str fields   */
/*  unconditionally — re-parsing both operands through num_from_str    */
/*  (the profile's Cluster A hot spot, ~32%) even when both operands   */
/*  were already cached as int32 (type_cache == IRXBC_STACK_LINTEGER). */
/*                                                                    */
/*  WP-BC-OC12 adds a fast-path in src/irx#bvm.c: when BOTH operands   */
/*  are LINTEGER and NUMERIC FUZZ == 0, cmp is derived from a direct   */
/*  int_cache comparison and irx_arith_compare is skipped.  Any other  */
/*  case (decimal, exponent, non-numeric, or fuzz != 0) takes the      */
/*  unchanged irx_arith_compare path and its string-memcmp fallback.   */
/*                                                                    */
/*  This test verifies (CON-18: byte-identical to token-walk):         */
/*    1. equivalence vs. token-walk for the fast-path cases            */
/*       (5<10, 100=100, '08'='8', boundary, variables, negatives);    */
/*    2. equivalence vs. token-walk for the fallback cases that MUST   */
/*       NOT take the fast-path (5<5.5 decimal, 'a'<'b' string,        */
/*       -3<2 via OP_NEG which clears the cache);                      */
/*    3. teeth: decimal comparisons whose result FLIPS if the          */
/*       fast-path ever wrongly fires on a non-LINTEGER operand        */
/*       (5<5.5 -> 1 not 0; 5=5.5 -> 0 not 1; 4<4.5 -> 1 not 0).       */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c */
/*          ../lstring370/src/lstr#xlt.c"                             */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbcmp test/mvs/tstbcmp.c \                         */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c \           */
/*        $LSRC && /tmp/tstbcmp                                       */
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
/*  equiv: token-walk vs bytecode output match (CON-18 reference).     */
/* ------------------------------------------------------------------ */

static int equiv(struct envblock *env, const char *src, const char *tag)
{
    struct irx_wkblk_int *wk = (struct irx_wkblk_int *)env->envblock_userfield;
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
    wk->wkbi_use_bytecode = 0;
    (void)irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    memcpy(tw_out, g_cap, (size_t)(g_cap_len + 1));

    cap_reset();
    wk->wkbi_use_bytecode = 1;
    (void)irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
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
/*  bc_exact: bytecode run is RC=0, never falls back to token-walk,    */
/*  and produces exactly the expected output.  Used as a teeth test    */
/*  for the fast-path: a wrong int_cache compare changes the output.   */
/* ------------------------------------------------------------------ */

static int bc_exact(struct envblock *env, const char *src,
                    const char *expected, const char *tag)
{
    struct irx_wkblk_int *wk = (struct irx_wkblk_int *)env->envblock_userfield;
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
    wk->wkbi_use_bytecode = 1;
    wk->wkbi_bc_exec_count = 0;
    wk->wkbi_bc_fallback_count = 0;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;

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
/*  Fast-path cases: both operands LINTEGER, fuzz 0 -> int_cache       */
/*  compare.  Equivalence proves the fast-path result matches the      */
/*  token-walk (irx_arith_compare) reference.                          */
/* ------------------------------------------------------------------ */

static void test_fastpath_equiv(struct envblock *env)
{
    printf("\n[fast-path: both LINTEGER -> int_cache compare]\n");

    /* The ticket's required cases. */
    equiv(env, "say 5 < 10", "5 < 10");
    equiv(env, "say 100 = 100", "100 = 100");
    /* '08' = '8' is numerically true: both cached as 8 (critical pt #3). */
    equiv(env, "say '08' = '8'", "'08' = '8' (numeric equal)");

    /* All six relational operators across the fast-path. */
    equiv(env, "say 10 > 5", "10 > 5");
    equiv(env, "say 5 >= 5", "5 >= 5");
    equiv(env, "say 5 <= 4", "5 <= 4");
    equiv(env, "say 7 \\= 8", "7 \\= 8 (not-equal)");
    equiv(env, "say 9 <> 9", "9 <> 9 (not-equal alt)");

    /* int32 boundary values still compare exactly. */
    equiv(env, "say 99999999 = 99999999", "99999999 = 99999999");
    equiv(env, "say 99999999 > 99999998", "99999999 > 99999998");

    /* Leading sign / leading zero literals normalise the same way. */
    equiv(env, "say +5 = 5", "+5 = 5");
    equiv(env, "say 007 = 7", "007 = 7");
    equiv(env, "say 0 = 0", "0 = 0");

    /* Cached variables: OP_LOAD restores type_cache, so the fast-path
     * fires for variable operands too (the rexxcps loop shape). */
    equiv(env, "a=5\nb=10\nsay a < b", "var a < var b");
    equiv(env, "a=10\nb=10\nsay a = b", "var a = var b");

    /* Negative int_cache (computed values keep the cache; 0-x stays in
     * range).  These exercise the va<0 / vb<0 arms of the fast-path. */
    equiv(env, "a=1-4\nsay a < 0", "negative var < 0");
    equiv(env, "a=1-4\nb=0-2\nsay a < b", "-3 < -2 (both negative cached)");
    equiv(env, "a=0-5\nsay a < 3", "-5 < 3 (mixed sign cached)");
    equiv(env, "a=5-5\nsay a = 0", "0 = 0 (computed zero)");
}

/* ------------------------------------------------------------------ */
/*  Fallback cases: at least one operand is NOT LINTEGER, so the        */
/*  fast-path MUST be skipped.  Equivalence proves the unchanged        */
/*  irx_arith_compare path (and its string-memcmp fallback) still runs. */
/* ------------------------------------------------------------------ */

static void test_fallback_equiv(struct envblock *env)
{
    printf("\n[fallback: not both LINTEGER -> irx_arith_compare / memcmp]\n");

    /* Decimal operand: 5.5 is never cached -> numeric fallback. */
    equiv(env, "say 5 < 5.5", "5 < 5.5 (decimal -> fallback)");
    equiv(env, "say 5.5 > 5", "5.5 > 5 (decimal -> fallback)");
    equiv(env, "say 2.5 = 2.50", "2.5 = 2.50 (decimal numeric equal)");

    /* Exponential notation: never cached -> numeric fallback. */
    equiv(env, "say 1E2 = 100", "1E2 = 100 (exponent -> fallback)");

    /* Unary minus clears the cache (OP_NEG), so '-3' is not LINTEGER. */
    equiv(env, "say -3 < 2", "-3 < 2 (OP_NEG -> fallback)");

    /* Non-numeric operands: numeric compare fails, string memcmp runs. */
    equiv(env, "say 'a' < 'b'", "'a' < 'b' (string fallback)");
    equiv(env, "say 'abc' = 'abc'", "'abc' = 'abc' (string fallback)");
    equiv(env, "say 'abc' < 'abd'", "'abc' < 'abd' (string fallback)");
    equiv(env, "say 'ab' < 'abc'", "'ab' < 'abc' (string length fallback)");

    /* One numeric, one not: still fallback (mixed). */
    equiv(env, "say 5 = 'x'", "5 = 'x' (mixed -> string fallback)");
}

/* ------------------------------------------------------------------ */
/*  Teeth: bytecode output must equal the numerically-correct result.  */
/*  Each case FLIPS if the fast-path ever fires on a decimal operand   */
/*  (truncating .5).  These go RED the moment the LINTEGER guard is     */
/*  weakened.  The programs run fully on the VM (exec>0, no fallback).  */
/* ------------------------------------------------------------------ */

static void test_teeth(struct envblock *env)
{
    printf("\n[teeth: decimal results that flip if fast-path misfires]\n");

    /* 5 < 5.5 -> 1.  A truncating fast-path would yield 5 < 5 -> 0. */
    bc_exact(env, "say 5 < 5.5", "1\n", "5 < 5.5 -> 1 (not 0)");
    /* 5 = 5.5 -> 0.  A truncating fast-path would yield 5 = 5 -> 1. */
    bc_exact(env, "say 5 = 5.5", "0\n", "5 = 5.5 -> 0 (not 1)");
    /* 4 < 4.5 -> 1.  A truncating fast-path would yield 4 < 4 -> 0. */
    bc_exact(env, "say 4 < 4.5", "1\n", "4 < 4.5 -> 1 (not 0)");

    /* Confirm the genuine fast-path cases produce the exact value too,
     * and that the program really executed on the VM with no fallback. */
    bc_exact(env, "say 5 < 10", "1\n", "5 < 10 -> 1 (fast-path)");
    bc_exact(env, "say 100 = 100", "1\n", "100 = 100 -> 1 (fast-path)");
    bc_exact(env, "say '08' = '8'", "1\n", "'08' = '8' -> 1 (fast-path)");
    bc_exact(env, "say 5 <= 4", "0\n", "5 <= 4 -> 0 (fast-path)");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-OC12: numeric-compare int_cache fast-path ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbcmp: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_fastpath_equiv(env);
    test_fallback_equiv(env);
    test_teeth(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
