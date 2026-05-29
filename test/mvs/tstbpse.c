/* ------------------------------------------------------------------ */
/*  tstbpse.c - WP-BC-05 PR A: PARSE sub-VM equivalence tests         */
/*                                                                    */
/*  For each REXX snippet, runs via the token-walk interpreter and    */
/*  the bytecode VM, then verifies both produce identical SAY output. */
/*  bc_only() tests are used where token-walk behaviour diverges.    */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c */
/*          ../lstring370/src/lstr#xlt.c"                             */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbpse test/mvs/tstbpse.c \                        */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   $LSRC && /tmp/tstbpse      */
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
/*  PARSE ARG — basic word splitting                                   */
/* ------------------------------------------------------------------ */

static void test_parse_arg(struct envblock *env)
{
    printf("\n[PARSE ARG — word splitting]\n");

    /* PARSE ARG with no args: var gets empty string, SAY outputs blank line */
    bc_only(env,
            "PARSE ARG a\nSAY a",
            "\n",
            "PARSE ARG single var (no args) -> empty");

    equiv(env,
          "CALL sub \"hello world\"\nEXIT\n"
          "sub:\n"
          "  PARSE ARG a b\n"
          "  SAY a\n"
          "  SAY b\n"
          "RETURN\n",
          "PARSE ARG two vars");

    /* Trailing spaces: c gets empty string (trailing blanks stripped) */
    bc_only(env,
            "CALL sub \"  hello  world  \"\nEXIT\n"
            "sub:\n"
            "  PARSE ARG a b c\n"
            "  SAY a\n"
            "  SAY b\n"
            "  SAY c\n"
            "RETURN\n",
            "hello\nworld\n\n",
            "PARSE ARG three vars leading/trailing spaces");

    /* Last var gets rest of string */
    equiv(env,
          "CALL sub \"one two three four\"\nEXIT\n"
          "sub:\n"
          "  PARSE ARG a b rest\n"
          "  SAY a\n"
          "  SAY b\n"
          "  SAY rest\n"
          "RETURN\n",
          "PARSE ARG last var gets rest");

    /* Dot placeholder */
    equiv(env,
          "CALL sub \"one two three\"\nEXIT\n"
          "sub:\n"
          "  PARSE ARG . b .\n"
          "  SAY b\n"
          "RETURN\n",
          "PARSE ARG dot placeholder");
}

/* ------------------------------------------------------------------ */
/*  PARSE ARG — multi-template (comma syntax)                          */
/* ------------------------------------------------------------------ */

static void test_parse_arg_multi(struct envblock *env)
{
    printf("\n[PARSE ARG — multi-template]\n");

    /* Comma-separated args for multi-template */
    bc_only(env,
            "CALL sub \"hello\", \"world\"\nEXIT\n"
            "sub:\n"
            "  PARSE ARG a, b\n"
            "  SAY a\n"
            "  SAY b\n"
            "RETURN\n",
            "hello\nworld\n",
            "PARSE ARG two templates");

    bc_only(env,
            "CALL sub \"a b\", \"c d\", \"e f\"\nEXIT\n"
            "sub:\n"
            "  PARSE ARG x y, p q, r s\n"
            "  SAY x\n"
            "  SAY y\n"
            "  SAY p\n"
            "  SAY q\n"
            "  SAY r\n"
            "  SAY s\n"
            "RETURN\n",
            "a\nb\nc\nd\ne\nf\n",
            "PARSE ARG three templates two vars each");
}

/* ------------------------------------------------------------------ */
/*  PARSE UPPER ARG — uppercasing                                      */
/* ------------------------------------------------------------------ */

static void test_parse_upper(struct envblock *env)
{
    printf("\n[PARSE UPPER]\n");

    equiv(env,
          "CALL sub \"hello world\"\nEXIT\n"
          "sub:\n"
          "  PARSE UPPER ARG a b\n"
          "  SAY a\n"
          "  SAY b\n"
          "RETURN\n",
          "PARSE UPPER ARG uppercases");

    equiv(env,
          "x = \"Hello World\"\n"
          "PARSE UPPER VAR x a b\n"
          "SAY a\n"
          "SAY b\n",
          "PARSE UPPER VAR uppercases");
}

/* ------------------------------------------------------------------ */
/*  PARSE VAR — from variable                                          */
/* ------------------------------------------------------------------ */

static void test_parse_var(struct envblock *env)
{
    printf("\n[PARSE VAR]\n");

    equiv(env,
          "x = \"one two three\"\n"
          "PARSE VAR x a b c\n"
          "SAY a\n"
          "SAY b\n"
          "SAY c\n",
          "PARSE VAR three words");

    equiv(env,
          "x = \"hello\"\n"
          "PARSE VAR x a\n"
          "SAY a\n",
          "PARSE VAR single word");

    /* PARSE VAR does not modify original variable */
    equiv(env,
          "x = \"hello world\"\n"
          "PARSE VAR x a b\n"
          "SAY x\n",
          "PARSE VAR source var unchanged");
}

/* ------------------------------------------------------------------ */
/*  PARSE with literal string triggers                                 */
/* ------------------------------------------------------------------ */

static void test_parse_literal(struct envblock *env)
{
    printf("\n[PARSE literal triggers]\n");

    equiv(env,
          "x = \"one,two,three\"\n"
          "PARSE VAR x a ',' b ',' c\n"
          "SAY a\n"
          "SAY b\n"
          "SAY c\n",
          "PARSE VAR literal comma splits");

    equiv(env,
          "x = \"hello world\"\n"
          "PARSE VAR x a ' ' b\n"
          "SAY a\n"
          "SAY b\n",
          "PARSE VAR literal space split");

    /* Literal not found: var gets rest */
    equiv(env,
          "x = \"hello world\"\n"
          "PARSE VAR x a ','\n"
          "SAY a\n",
          "PARSE VAR literal not found var gets all");
}

/* ------------------------------------------------------------------ */
/*  PARSE with absolute position triggers                              */
/* ------------------------------------------------------------------ */

static void test_parse_abs(struct envblock *env)
{
    printf("\n[PARSE absolute position]\n");

    equiv(env,
          "x = \"abcdefghij\"\n"
          "PARSE VAR x 1 a 4 b\n"
          "SAY a\n"
          "SAY b\n",
          "PARSE VAR abs 1 4 split");

    equiv(env,
          "x = \"abcdefghij\"\n"
          "PARSE VAR x 1 a 4 . 7 b\n"
          "SAY a\n"
          "SAY b\n",
          "PARSE VAR abs 1 4 7 with dot");

    /* Column beyond string length */
    equiv(env,
          "x = \"abc\"\n"
          "PARSE VAR x 1 a 99\n"
          "SAY a\n",
          "PARSE VAR abs beyond end");
}

/* ------------------------------------------------------------------ */
/*  PARSE VALUE expr WITH template                                     */
/* ------------------------------------------------------------------ */

static void test_parse_value(struct envblock *env)
{
    printf("\n[PARSE VALUE]\n");

    equiv(env,
          "PARSE VALUE \"hello world\" WITH a b\n"
          "SAY a\n"
          "SAY b\n",
          "PARSE VALUE string literal");

    equiv(env,
          "n = 42\n"
          "PARSE VALUE n + 1 WITH x\n"
          "SAY x\n",
          "PARSE VALUE expression");

    equiv(env,
          "PARSE VALUE LENGTH(\"hello\") WITH n\n"
          "SAY n\n",
          "PARSE VALUE BIF expression");
}

/* ------------------------------------------------------------------ */
/*  PARSE SOURCE / VERSION / NUMERIC                                   */
/* ------------------------------------------------------------------ */

static void test_parse_special(struct envblock *env)
{
    printf("\n[PARSE SOURCE / VERSION / NUMERIC]\n");

    /* PARSE SOURCE — smoke test: just check it assigns something */
    bc_only(env,
            "PARSE SOURCE env calltype pgm\n"
            "SAY calltype\n",
            "COMMAND\n",
            "PARSE SOURCE calltype is COMMAND at top level");

    /* PARSE VERSION — must start with REXX370 */
    bc_only(env,
            "PARSE VERSION lang ver date\n"
            "SAY lang\n",
            "REXX370\n",
            "PARSE VERSION lang = REXX370");

    /* PARSE NUMERIC — smoke test: digits is a number */
    bc_only(env,
            "PARSE NUMERIC d f form\n"
            "SAY d + 0\n",
            "9\n",
            "PARSE NUMERIC default digits = 9");
}

/* ------------------------------------------------------------------ */
/*  Compound variable targets in PARSE templates (WP-BC-05 PR C)      */
/* ------------------------------------------------------------------ */

static void test_parse_compound_target(struct envblock *env)
{
    printf("\n[PARSE compound-variable targets]\n");

    /* Constant tail: PARSE ARG a.1 */
    bc_only(env,
            "CALL sub \"hello\"\nEXIT\n"
            "sub:\n"
            "PARSE ARG a.1\n"
            "SAY a.1\n"
            "RETURN\n",
            "hello\n",
            "PARSE ARG a.1 constant tail target");

    /* Variable tail: PARSE ARG a.i where i=2 at call time */
    bc_only(env,
            "i = 2\n"
            "CALL sub \"world\"\nEXIT\n"
            "sub:\n"
            "PARSE ARG a.i\n"
            "SAY a.2\n"
            "RETURN\n",
            "world\n",
            "PARSE ARG a.i variable tail target");

    /* Two compound targets in one template */
    bc_only(env,
            "PARSE VALUE \"first second\" WITH a.1 b.1\n"
            "SAY a.1\n"
            "SAY b.1\n",
            "first\nsecond\n",
            "PARSE VALUE two compound targets in template");

    /* Compound and simple targets mixed */
    bc_only(env,
            "PARSE VALUE \"one two\" WITH a.1 b\n"
            "SAY a.1\n"
            "SAY b\n",
            "one\ntwo\n",
            "PARSE VALUE compound and simple targets mixed");
}

/* ------------------------------------------------------------------ */
/*  Indirect pattern (var) — WP-BC-09                                  */
/* ------------------------------------------------------------------ */

static void test_parse_indirect(struct envblock *env)
{
    struct irx_wkblk_int *wk;

    printf("\n[PARSE indirect pattern (var)]\n");

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        printf("  FAIL: no work block\n");
        tests_run++;
        tests_failed++;
        return;
    }

    /* Basic (var): split on value of p0 */
    equiv(env,
          "p0 = 'b'\n"
          "rc = 'This is an awfully boring program'\n"
          "PARSE VAR rc p1 (p0) p5\n"
          "SAY p1\n"
          "SAY p5\n",
          "PARSE VAR indirect (p0) basic split on 'b'");

    /* REXXCPS hot-loop pattern: multiple vars + indirect */
    equiv(env,
          "sep = ' '\n"
          "x = 'hello world foo'\n"
          "PARSE VAR x a (sep) b\n"
          "SAY a\n"
          "SAY b\n",
          "PARSE VAR indirect (sep) space separator");

    /* Multiple occurrences: split on first match only */
    equiv(env,
          "d = ':'\n"
          "s = 'usr:bin:lib'\n"
          "PARSE VAR s h (d) t\n"
          "SAY h\n"
          "SAY t\n",
          "PARSE VAR indirect (d) colon first match");

    /* Null delimiter (empty sep): split at current scan pos, no advance.
     * a='', b='abc'.  Use concat to avoid empty-string SAY divergence. */
    equiv(env,
          "sep = ''\n"
          "x = 'abc'\n"
          "PARSE VAR x a (sep) b\n"
          "SAY a || '|' || b\n",
          "PARSE VAR indirect (sep) empty delimiter -> a empty b all");

    /* Unset variable: null delimiter → split at current pos.  a='', b=all.
     * No NOVALUE trap.  Use concat to avoid empty-SAY divergence.       */
    equiv(env,
          "DROP mysep\n"
          "x = 'hello world'\n"
          "PARSE VAR x a (mysep) b\n"
          "SAY a || '|' || b\n",
          "PARSE VAR indirect (mysep) unset -> empty split -> b gets all");

    /* Indirect not found: p1 gets all, p5=''.
     * Use concat to avoid empty-string SAY divergence between paths. */
    equiv(env,
          "p0 = 'XYZ'\n"
          "rc = 'This is an awfully boring program'\n"
          "PARSE VAR rc p1 (p0) p5\n"
          "SAY p1 || '|' || p5\n",
          "PARSE VAR indirect (p0) delimiter not found");

    /* Verify bytecode path taken (AC #6): no fallback for (var) */
    {
        const char *src =
            "p0 = 'b'\n"
            "rc = 'This is an awfully boring program'\n"
            "PARSE VAR rc p1 (p0) p5\n"
            "SAY p1\n"
            "SAY p5\n";
        int src_len = (int)strlen(src);
        int exec_rc;
        int exec_exit_rc = 0;

        wk->wkbi_bc_exec_count = 0;
        wk->wkbi_bc_fallback_count = 0;
        cap_reset();
        wk->wkbi_use_bytecode = 1;
        exec_rc = irx_exec_run(src, src_len, NULL, 0, &exec_exit_rc, env);
        (void)exec_rc;
        wk->wkbi_use_bytecode = 0;

        CHECK(wk->wkbi_bc_exec_count > 0,
              "indirect pattern: BC exec path taken");
        CHECK(wk->wkbi_bc_fallback_count == 0,
              "indirect pattern: no UNSUP fallback");
    }

    /* Verify counter discriminates: INTERPRET is still UNSUP */
    {
        const char *src = "INTERPRET 'SAY 1'\n";
        int src_len = (int)strlen(src);
        int interp_rc;
        int interp_exit_rc = 0;

        wk->wkbi_bc_exec_count = 0;
        wk->wkbi_bc_fallback_count = 0;
        wk->wkbi_use_bytecode = 1;
        interp_rc = irx_exec_run(src, src_len, NULL, 0, &interp_exit_rc, env);
        (void)interp_rc;
        wk->wkbi_use_bytecode = 0;

        CHECK(wk->wkbi_bc_fallback_count > 0,
              "INTERPRET still UNSUP: fallback_count incremented");
        CHECK(wk->wkbi_bc_exec_count == 0,
              "INTERPRET still UNSUP: exec_count not incremented");
    }
}

/* ------------------------------------------------------------------ */
/*  PARSE PULL — WP-33b stub (compiles; fails at runtime)             */
/* ------------------------------------------------------------------ */

static void test_parse_pull(struct envblock *env)
{
    struct irx_wkblk_int *wk;
    int src_len, rc, exit_rc = 0;
    const char *src = "PARSE PULL x\nSAY x\n";

    printf("\n[PARSE PULL — WP-33b stub]\n");

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        printf("  FAIL: no work block\n");
        tests_run++;
        tests_failed++;
        return;
    }

    src_len = (int)strlen(src);
    cap_reset();
    wk->wkbi_use_bytecode = 1;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;

    CHECK(rc != 0, "PARSE PULL returns error at runtime (WP-33b not implemented)");
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-05 PR A + PR C: PARSE sub-VM Tests ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbpse: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_parse_arg(env);
    test_parse_arg_multi(env);
    test_parse_upper(env);
    test_parse_var(env);
    test_parse_literal(env);
    test_parse_abs(env);
    test_parse_value(env);
    test_parse_special(env);
    test_parse_compound_target(env);
    test_parse_indirect(env);
    test_parse_pull(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
