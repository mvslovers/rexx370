/* ------------------------------------------------------------------ */
/*  tstbdq.c - WP-BC-RT03: quote de-doubling in string literals         */
/*                                                                    */
/*  Before WP-BC-RT03 the bytecode compiler's string-literal branch    */
/*  (src/irx#bcom.c, bc_exp8, the TOK_STRING case) called              */
/*  add_const(t->tok_text, t->tok_length) directly.  The tokenizer     */
/*  stores a quoted body raw, with doubled quotes intact, and only     */
/*  flags it (TOKF_QUOTE_DBL); de-doubling is the consumer's job.       */
/*  bc_exp8 omitted that step, so say 'p''q''r' printed p''q''r        */
/*  instead of the REXX-correct p'q'r (a doubled quote inside a string  */
/*  is an escaped single quote).  The PARSE-template path already did   */
/*  the right thing via bpse_dedouble (bc_parse_template); the fix      */
/*  replicates that pattern in bc_exp8.                                */
/*                                                                    */
/*  This test verifies:                                               */
/*    1. spec-correct de-doubling on the bytecode path with no         */
/*       fallback to token-walk (bc_exact: 'p''q''r' -> p'q'r, etc.);   */
/*    2. single-kind self-detection: a body doubling only one quote     */
/*       kind de-doubles that kind ("a""b" -> a"b, 'it''s a "test"' ->  */
/*       it's a "test").  A "-delimited body that mixes a literal ''    */
/*       with an escaped "" mis-detects the delimiter — a pre-existing   */
/*       shared defect in bpse_dedouble AND token-walk's dedouble_string */
/*       (both default to '), so tw==bc yet both are spec-wrong; pinned  */
/*       as an equivalence case below, fix tracked outside WP-BC-RT03;   */
/*    3. equivalence vs. token-walk across all variants (CON-18):       */
/*       token-walk is the correct reference for de-doubling            */
/*       (project_bc_dquote_divergence: token-walk = p'q'r);            */
/*    4. the unquoted-flag path is unchanged (no-double regression).    */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c */
/*          ../lstring370/src/lstr#xlt.c"                             */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbdq test/mvs/tstbdq.c \                           */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c \           */
/*        $LSRC && /tmp/tstbdq                                        */
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
/*  For RT03 the token-walk is the *correct* reference — it de-doubles  */
/*  quotes and gives p'q'r (project_bc_dquote_divergence).             */
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
/*  and produces exactly the spec-correct output.  This pins the       */
/*  de-doubled string — equiv() alone would pass if both paths were    */
/*  wrong the same way.                                                */
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
/*  De-doubling: spec-correct bytecode output, no token-walk fallback. */
/* ------------------------------------------------------------------ */

static void test_dedouble(struct envblock *env)
{
    printf("\n[de-double: bytecode produces the escaped single quote]\n");

    /* Single-quote pairs collapse to one quote. */
    bc_exact(env, "say 'p''q''r'", "p'q'r\n", "'p''q''r' -> p'q'r");

    /* Double-quote pairs collapse to one double-quote. */
    bc_exact(env, "say \"a\"\"b\"", "a\"b\n", "\"a\"\"b\" -> a\"b");

    /* Four quotes = one literal quote (empty body around a doubled pair). */
    bc_exact(env, "say ''''", "'\n", "'''' -> '");

    /* Mixed: only ' is doubled; the inner " are single and pass through. */
    bc_exact(env, "say 'it''s a \"test\"'", "it's a \"test\"\n",
             "'it''s a \"test\"' -> it's a \"test\"");

    /* Leading and trailing doubled pairs: '''x''' = ' + x + ' . */
    bc_exact(env, "say '''x'''", "'x'\n", "'''x''' -> 'x'");

    /* Two escaped pairs in a row: six quotes -> two literal quotes. */
    bc_exact(env, "say ''''''", "''\n", "'''''' -> ''");
}

/* ------------------------------------------------------------------ */
/*  Regression: the unquoted-flag (no doubled quote) path is unchanged. */
/* ------------------------------------------------------------------ */

static void test_no_double(struct envblock *env)
{
    printf("\n[no-double regression: unflagged literals pass through]\n");

    /* No doubled quote -> TOKF_QUOTE_DBL not set -> unchanged else branch. */
    bc_exact(env, "say 'no quotes'", "no quotes\n", "'no quotes' unchanged");
    bc_exact(env, "say \"plain\"", "plain\n", "\"plain\" unchanged");

    /* A lone quote of the other kind is a literal char, not a pair. */
    bc_exact(env, "say 'a\"b'", "a\"b\n", "single \" inside ' literal");
    bc_exact(env, "say \"a'b\"", "a'b\n", "single ' inside \" literal");
}

/* ------------------------------------------------------------------ */
/*  Equivalence vs token-walk (CON-18) — token-walk is correct here.   */
/* ------------------------------------------------------------------ */

static void test_equivalence(struct envblock *env)
{
    printf("\n[equivalence: de-doubling variants vs token-walk]\n");

    equiv(env, "say 'p''q''r'", "'p''q''r'");
    equiv(env, "say \"a\"\"b\"", "\"a\"\"b\"");
    equiv(env, "say ''''", "''''");
    equiv(env, "say ''", "'' empty literal");
    equiv(env, "say 'no quotes'", "'no quotes' (no flag)");
    equiv(env, "say 'it''s a \"test\"'", "'it''s a \"test\"'");
    equiv(env, "say '''x'''", "'''x'''");

    /* De-doubling inside a concatenation and an assignment RHS. */
    equiv(env, "v='X'\nsay 'a''b'v", "doubled literal abuttal var");
    equiv(env, "x='he said ''hi'''\nsay x", "doubled literal in assignment");

    /* Mixed '' + "" in one "-delimited literal.  Both paths mis-detect
     * the delimiter (shared bpse_dedouble / dedouble_string defect) and
     * give a'b""c instead of the spec-correct a''b"c — but they agree,
     * which is the property decommission needs.  Fix tracked outside RT03. */
    equiv(env, "say \"a''b\"\"c\"", "mixed ''+\"\" (shared defect; tw==bc)");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-RT03: quote de-doubling in string literals ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbdq: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_dedouble(env);
    test_no_double(env);
    test_equivalence(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
