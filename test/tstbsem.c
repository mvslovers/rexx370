/* ------------------------------------------------------------------ */
/*  tstbsem.c - WP-BC-SEMI: ';' as a bytecode clause separator        */
/*                                                                    */
/*  The tokenizer maps both ';' and ':' to TOK_SEMICOLON.  A ';'      */
/*  terminates a clause exactly like a logical newline (TOK_EOC); a   */
/*  ':' marks a label.  Before WP-BC-SEMI the bytecode clause drivers */
/*  consumed only TOK_EOC, so any ';'-terminated clause left a stray  */
/*  TOK_SEMICOLON that forced the whole program onto the token-walk   */
/*  fallback (REXXCPS line 91: `do count; end`).                      */
/*                                                                    */
/*  This test verifies that:                                          */
/*    1. ';'-separated programs produce identical output on the       */
/*       token-walk and bytecode paths (equiv).                       */
/*    2. those programs actually compile and run on the bytecode      */
/*       path WITHOUT falling back (no_fallback).                     */
/*    3. the ':' label special-case still works alongside ';'.        */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c */
/*          ../lstring370/src/lstr#xlt.c"                             */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbsem test/mvs/tstbsem.c \                        */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c \           */
/*        $LSRC && /tmp/tstbsem                                       */
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
/*  equiv: run src via token-walk, then via bytecode, check that      */
/*  SAY output matches.                                               */
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
/*  no_fallback: run src via the bytecode path and verify both that   */
/*  the SAY output matches `expected` AND that the program compiled   */
/*  and ran on the bytecode path without any token-walk fallback.     */
/*  This is the discriminating check — equiv() alone would pass even  */
/*  if BOTH paths ran the token-walk interpreter (via fallback).      */
/* ------------------------------------------------------------------ */

static int no_fallback(struct envblock *env, const char *src,
                       const char *expected, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int rc;
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
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    (void)rc;
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
/*  Trailing ';' and single-clause separators                         */
/* ------------------------------------------------------------------ */

static void test_trailing_semi(struct envblock *env)
{
    printf("\n[trailing ';']\n");

    /* The original REXXCPS blocker reproducers (issue #169). */
    equiv(env, "say 1;", "trailing ; after SAY");
    equiv(env, "x = 1; y = 2", "two assignments on one line");
    equiv(env, "say 1;\nsay 2;", "trailing ; on consecutive lines");

    no_fallback(env, "say 1;", "1\n", "trailing ; after SAY");
    no_fallback(env, "x = 1; y = 2\nsay x; say y", "1\n2\n",
                "assignments and SAY separated by ;");
    no_fallback(env, "say 9;", "9\n", "trailing ; emits the value");
}

/* ------------------------------------------------------------------ */
/*  Multiple clauses on one line                                       */
/* ------------------------------------------------------------------ */

static void test_multi_clause(struct envblock *env)
{
    printf("\n[multiple clauses on one line]\n");

    equiv(env, "say 1; say 2; say 3", "three SAYs on one line");
    equiv(env, "a = 1; b = 2; c = a + b; say c", "chained assignments");
    equiv(env, "say 1;; say 2", "consecutive ;; treated as blank");
    equiv(env, ";say 1", "leading ; before SAY");
    equiv(env, "say 1 ; say 2", "; with surrounding spaces");

    no_fallback(env, "say 1; say 2; say 3", "1\n2\n3\n",
                "three SAYs on one line");
    no_fallback(env, "a = 1; b = 2; c = a + b; say c", "3\n",
                "chained assignments then SAY");
    no_fallback(env, "say 1;; say 2", "1\n2\n",
                "consecutive ;; skipped as blank clause");
    no_fallback(env, ";say 1", "1\n", "leading ; skipped");
}

/* ------------------------------------------------------------------ */
/*  ';' in DO headers and bodies (REXXCPS line 91 pattern)            */
/* ------------------------------------------------------------------ */

static void test_do_semi(struct envblock *env)
{
    printf("\n[';' in DO]\n");

    /* The literal REXXCPS-91 construct: DO count; END (empty body). */
    equiv(env, "count = 3\ncnt = 0\ndo count; cnt = cnt + 1; end\nsay cnt",
          "do count; <body>; end");
    equiv(env, "do i = 1 to 3; say i; end", "do TO with ; separators");
    equiv(env, "n = 0\ndo i = 1 to 5; n = n + i; end; say n",
          "do TO body + trailing ; before END");
    equiv(env, "i = 0\ndo while i < 3; i = i + 1; end; say i",
          "do WHILE with ; separators");

    no_fallback(env, "count = 3\ndo count; end\nsay 'done'", "done\n",
                "do count; end (empty body) compiles on bytecode path");
    no_fallback(env, "do i = 1 to 3; say i; end", "1\n2\n3\n",
                "do TO with ; separators");
    no_fallback(env, "n = 0\ndo i = 1 to 5; n = n + i; end; say n", "15\n",
                "do TO sum with ; separators");
}

/* ------------------------------------------------------------------ */
/*  ';' in IF / SELECT bodies                                          */
/* ------------------------------------------------------------------ */

static void test_if_select_semi(struct envblock *env)
{
    printf("\n[';' in IF / SELECT]\n");

    equiv(env, "if 1 then do; say 'a'; say 'b'; end",
          "IF THEN DO block with ; separators");
    equiv(env, "if 0 then do; say 'x'; end; else do; say 'y'; end",
          "IF/ELSE DO blocks with ; separators");
    equiv(env,
          "x = 2\n"
          "select; when x = 1 then say 'one'; when x = 2 then say 'two';"
          " otherwise say 'other'; end",
          "SELECT one-line with ; separators");

    no_fallback(env, "if 1 then do; say 'a'; say 'b'; end", "a\nb\n",
                "IF THEN DO block with ; separators");
    no_fallback(env,
                "x = 2\n"
                "select; when x = 1 then say 'one';"
                " when x = 2 then say 'two';"
                " otherwise say 'other'; end",
                "two\n",
                "SELECT one-line with ; separators");
}

/* ------------------------------------------------------------------ */
/*  Null clause after THEN / ELSE / WHEN via ';'                       */
/*                                                                    */
/*  A ';' immediately after THEN/ELSE/WHEN-THEN is a null clause —    */
/*  the conditional body is empty and the following clause runs       */
/*  unconditionally.  consume_eoc() now swallows that ';', so these   */
/*  must stay byte-identical to the token-walk interpreter (which is  */
/*  the reference): a divergence here would be a silent correctness   */
/*  bug, not a fallback.                                              */
/* ------------------------------------------------------------------ */

static void test_then_null_clause(struct envblock *env)
{
    printf("\n[null clause after THEN/ELSE/WHEN via ';']\n");

    equiv(env, "if 0 then; say 'after'", "IF 0 THEN; then SAY");
    equiv(env, "if 1 then; say 'after'", "IF 1 THEN; then SAY");
    equiv(env, "if 0 then; else say 'e'", "IF 0 THEN; ELSE SAY");
    equiv(env, "if 1 then say 'a'; else; say 'b'",
          "IF 1 ELSE; then SAY");
    equiv(env, "if 0 then say 'a'; else; say 'b'",
          "IF 0 ELSE; then SAY");
    equiv(env, "select; when 0 then; otherwise say 'o'; end",
          "SELECT WHEN 0 THEN; OTHERWISE");
    equiv(env, "select; when 1 then; otherwise say 'o'; end",
          "SELECT WHEN 1 THEN; OTHERWISE");
}

/* ------------------------------------------------------------------ */
/*  ':' label special-case must survive alongside ';'                  */
/*                                                                    */
/*  The tokenizer emits TOK_SEMICOLON for BOTH ';' and ':'.  These    */
/*  tests confirm that a ':' is still recognised as a label boundary  */
/*  (never a clause end) while a ';' is now a clause separator.        */
/* ------------------------------------------------------------------ */

static void test_label_preserved(struct envblock *env)
{
    printf("\n[':' label preserved with ';' separators]\n");

    /* CALL a label, then ';'-separated clauses inside the subroutine. */
    equiv(env,
          "call greet; exit\n"
          "greet:; say 'hello'; return",
          "label + ; separators in subroutine");

    /* Label immediately followed by ';' (empty clause after label). */
    equiv(env,
          "call sub; exit\n"
          "sub:\n"
          "say 'in'; return",
          "label then ; separated body");

    /* Fall-through label at top, ';' separators throughout. */
    equiv(env,
          "start: a = 1; b = 2; say a + b; exit\n"
          "never: say 'unreached'; return",
          "fall-through label with ; separators");

    no_fallback(env,
                "call greet; exit\n"
                "greet: say 'hello'; return",
                "hello\n",
                "label recognised; ; separators compile on bytecode path");

    no_fallback(env,
                "start: x = 5; say x; exit\n"
                "helper: say 'h'; return",
                "5\n",
                "fall-through label + ; separators, no fallback");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-SEMI: ';' clause-separator Tests ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbsem: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_trailing_semi(env);
    test_multi_clause(env);
    test_do_semi(env);
    test_if_select_semi(env);
    test_then_null_clause(env);
    test_label_preserved(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
