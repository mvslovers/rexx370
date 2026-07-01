/* ------------------------------------------------------------------ */
/*  tstbcont.c - WP-BC-CONTCOMMA: trailing-comma line continuation     */
/*                                                                    */
/*  The tokenizer keeps the trailing comma that ends a physical line   */
/*  (suppressing only the EOC) and marks it TOKF_CONTINUATION, so the  */
/*  parser can tell a continuation-comma from an argument separator    */
/*  (src/irx#tokn.c).  Before WP-BC-CONTCOMMA the bytecode compiler    */
/*  never inspected the flag: the marked comma reached the bc_stmt     */
/*  catch-all → BC_UNSUP_STATEMENT → whole-program token-walk          */
/*  fallback.  This was REXXCPS line 166-167.                          */
/*                                                                    */
/*  The compiler now treats a TOKF_CONTINUATION comma in a general     */
/*  expression context (SAY, IF/WHEN condition, assignment RHS,        */
/*  RETURN, EXIT, the *_VALUE forms) as a blank concatenation with the */
/*  following sub-expression — mirroring the token-walk                */
/*  irx_pars_eval_expr.  In an argument list (CALL / function call) a  */
/*  comma stays an argument separator, with or without the flag.       */
/*                                                                    */
/*  This test verifies:                                               */
/*    1. multi-line clauses joined by a trailing comma compile and RUN */
/*       on the bytecode path WITHOUT falling back;                    */
/*    2. they produce output identical to the token-walk (CON-18);     */
/*    3. a continuation-comma is a BLANK concatenation ("1 2"), not    */
/*       "12" and not a literal comma;                                 */
/*    4. a comma in an argument list stays an argument separator,      */
/*       whether or not it carries the continuation flag;              */
/*    5. the REXXCPS line-166 clause shape compiles on the bytecode    */
/*       path.                                                         */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c */
/*          ../lstring370/src/lstr#xlt.c"                             */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbcont test/mvs/tstbcont.c \                       */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c \           */
/*        $LSRC && /tmp/tstbcont                                      */
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
/*  equiv: token-walk vs bytecode output match (CON-18 reference).     */
/* ------------------------------------------------------------------ */

static int equiv(struct envblock *env, const char *src, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int exit_rc = 0;
    char tw_out[CAPBUF_SIZE];
    char bc_out[CAPBUF_SIZE];
    char label[160];

    wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
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
/*  no_fallback: bytecode output == expected AND no token-walk         */
/*  fallback (exec>0, fallback==0).                                    */
/* ------------------------------------------------------------------ */

static int no_fallback(struct envblock *env, const char *src,
                       const char *expected, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int exit_rc = 0;
    char label[160];

    wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
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
    (void)irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;

    snprintf(label, sizeof(label), "no-fallback: %s", tag);
    CHECK(strcmp(g_cap, expected) == 0 &&
              wk->wkbi_bc_exec_count > 0 &&
              wk->wkbi_bc_fallback_count == 0,
          label);

    if (strcmp(g_cap, expected) != 0 ||
        wk->wkbi_bc_exec_count == 0 ||
        wk->wkbi_bc_fallback_count != 0)
    {
        printf("    expected:[%s] exec=%d fallback=%d\n",
               expected, wk->wkbi_bc_exec_count, wk->wkbi_bc_fallback_count);
        printf("    got:     [%s]\n", g_cap);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  ran_bc: bytecode compiled and ran (exec>0, fallback==0), output    */
/*  not checked.  Used for the exact REXXCPS-166 clause shape, whose   */
/*  zero-space funcall+string abuttal output may still differ from the */
/*  token-walk via the separate WP-BC-09 divergence (not this WP).     */
/* ------------------------------------------------------------------ */

static int ran_bc(struct envblock *env, const char *src, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int exit_rc = 0;
    char label[160];

    wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
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
    (void)irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;

    snprintf(label, sizeof(label), "ran-on-bytecode: %s", tag);
    CHECK(wk->wkbi_bc_exec_count > 0 && wk->wkbi_bc_fallback_count == 0,
          label);

    if (wk->wkbi_bc_exec_count == 0 || wk->wkbi_bc_fallback_count != 0)
    {
        printf("    exec=%d fallback=%d  out=[%s]\n",
               wk->wkbi_bc_exec_count, wk->wkbi_bc_fallback_count, g_cap);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Continuation in a SAY clause — the core case (REXXCPS:166).        */
/*  A continuation-comma is a BLANK concatenation.                     */
/* ------------------------------------------------------------------ */

static void test_say_continuation(struct envblock *env)
{
    printf("\n[continuation in SAY — blank concatenation]\n");

    /* The minimal reproducer: two numbers across a line continuation. */
    equiv(env, "say 1,\n    2", "say 1,<nl>2");
    no_fallback(env, "say 1,\n    2", "1 2\n", "say 1,<nl>2 -> '1 2'");

    /* Strings across the continuation. */
    equiv(env, "say 'a',\n    'b'", "say 'a',<nl>'b'");
    no_fallback(env, "say 'a',\n    'b'", "a b\n", "say 'a',<nl>'b' -> 'a b'");

    /* Several terms before and after the continuation comma. */
    equiv(env, "say 'x' 'y',\n    'z'", "say 'x' 'y',<nl>'z'");
    no_fallback(env, "say 'x' 'y',\n    'z'", "x y z\n",
                "three terms across continuation");

    /* A continuation-comma is a blank, not a literal comma and not a
     * tight join: 1,<nl>2 is "1 2", never "1,2" or "12". */
    no_fallback(env, "say 12,\n    34", "12 34\n",
                "continuation is a blank, not a tight join");
}

/* ------------------------------------------------------------------ */
/*  Continuation in assignment RHS and IF condition.                  */
/* ------------------------------------------------------------------ */

static void test_other_expr_continuation(struct envblock *env)
{
    printf("\n[continuation in assignment / IF]\n");

    /* Assignment RHS spanning a continuation. */
    equiv(env, "v = 1,\n    2\nsay v", "assignment RHS continuation");
    no_fallback(env, "v = 1,\n    2\nsay v", "1 2\n",
                "v = 1,<nl>2 -> v is '1 2'");

    /* Continuation-comma immediately before THEN: a bare line join,
     * the condition is just the preceding expression. */
    equiv(env, "a = 1\nif a = 1,\n    then say 'eq'", "IF cond , THEN");
    no_fallback(env, "a = 1\nif a = 1,\n    then say 'eq'", "eq\n",
                "IF cond continuation before THEN");

    /* Continuation within the condition expression itself. */
    equiv(env, "if 1,\n    then say 'y'; else say 'n'",
          "IF 1,<nl>THEN");
}

/* ------------------------------------------------------------------ */
/*  Argument lists: a comma stays an argument separator, with or       */
/*  without the continuation flag.  This is the abgrenzung to the      */
/*  expression-context blank-concatenation above.                      */
/* ------------------------------------------------------------------ */

static void test_arg_separator_preserved(struct envblock *env)
{
    printf("\n[comma stays an argument separator in CALL]\n");

    /* Plain (non-continuation) comma: two arguments. */
    equiv(env, "call sub 1, 2\nexit\n"
               "sub: say arg(); return",
          "CALL 1, 2 -> arg()=2");
    no_fallback(env, "call sub 1, 2\nexit\n"
                     "sub: say arg(); return",
                "2\n", "CALL plain comma -> 2 args");

    /* Continuation comma in a CALL arg list: STILL two arguments —
     * the flag does not turn it into a blank concatenation here. */
    equiv(env, "call sub 1,\n    2\nexit\n"
               "sub: say arg(); return",
          "CALL 1,<nl>2 -> arg()=2");
    no_fallback(env, "call sub 1,\n    2\nexit\n"
                     "sub: say arg(); return",
                "2\n", "CALL continuation comma -> still 2 args");

    /* And the argument values survive intact across the continuation. */
    no_fallback(env, "call sub 'a',\n    'b'\nexit\n"
                     "sub: say arg(1) arg(2); return",
                "a b\n", "CALL continuation comma -> args 'a' and 'b'");
}

/* ------------------------------------------------------------------ */
/*  The REXXCPS line 166-167 clause shape.                            */
/*  Output-agnostic (ran_bc): the clause contains zero-space           */
/*  funcall+string abuttal (format(total,,1)'s)') whose output may     */
/*  still differ from the token-walk via the separate WP-BC-09         */
/*  abuttal divergence.  This WP's success metric is that it COMPILES  */
/*  on the bytecode path.                                             */
/* ------------------------------------------------------------------ */

static void test_rexxcps_shape(struct envblock *env)
{
    printf("\n[REXXCPS line 166-167 clause shape]\n");

    ran_bc(env,
           "total = 3\ncount = 5\naveraging = 10\n"
           "say '        Averaged:' count 'x' averaging 'iterations',\n"
           "    'of 1000 clauses (over' format(total,,1)'s)'",
           "REXXCPS:166-167 compiles on bytecode path");

    /* REXXCPS line 176-177 shape (continuation after a symbol). */
    ran_bc(env,
           "total = 3\naveraging = 10\n"
           "say 'Total (full DOs):' total 'secs (average of' averaging ,\n"
           "    'measures of' count 'iterations)'",
           "REXXCPS:176-177 compiles on bytecode path");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-CONTCOMMA: trailing-comma line continuation ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbcont: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_say_continuation(env);
    test_other_expr_continuation(env);
    test_arg_separator_preserved(env);
    test_rexxcps_shape(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
