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
/*  Compound variable targets — must return compile error             */
/* ------------------------------------------------------------------ */

static void test_parse_compound_reject(struct envblock *env)
{
    struct irx_wkblk_int *wk;
    int src_len, rc, exit_rc = 0;
    const char *src = "PARSE ARG a.1\nSAY a.1\n";

    printf("\n[PARSE compound reject]\n");

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

    /* Expect a non-zero rc (compile error IRXBC_ERR_PARSE_COMPOUND) */
    CHECK(rc != 0, "compound target rejected by bytecode compiler");
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-05 PR A: PARSE sub-VM Tests ===\n");

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
    test_parse_compound_reject(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
