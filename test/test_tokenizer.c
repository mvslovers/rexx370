/* ------------------------------------------------------------------ */
/*  test_tokenizer.c - WP-10 tokenizer unit tests                     */
/*                                                                    */
/*  Build (Linux/gcc cross-compile):                                  */
/*    gcc -I include -Wall -Wextra -std=gnu99 -o test/test_tokenizer  */
/*        test/test_tokenizer.c \                                     */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \        */
/*        'src/irx#anch.c'  'src/irx#uid.c'  'src/irx#msid.c' \        */
/*        'src/irx#tokn.c'                                             */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxanchor.h"
#include "irxfunc.h"
#include "irxtokn.h"
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
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static int tok_text_eq(const struct irx_token *t, const char *s)
{
    int n = (int)strlen(s);
    if (t->tok_length != (unsigned short)n)
    {
        return 0;
    }
    return memcmp(t->tok_text, s, (size_t)n) == 0;
}

static int run(const char *src, struct irx_token **out, int *count,
               struct irx_tokn_error *err)
{
    return irx_tokn_run(NULL, src, (int)strlen(src), out, count, err);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

static void test_hello_world(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;
    int rc;

    printf("\n--- Test: say 'Hello World' ---\n");

    rc = run("say 'Hello World'", &toks, &n, &err);
    CHECK(rc == 0, "tokenizer returns 0");
    CHECK(n == 4, "produces 4 tokens (SYMBOL STRING EOC EOF)");
    if (n >= 4)
    {
        CHECK(toks[0].tok_type == TOK_SYMBOL && tok_text_eq(&toks[0], "say"),
              "[0] SYMBOL 'say'");
        CHECK(toks[1].tok_type == TOK_STRING && tok_text_eq(&toks[1], "Hello World"),
              "[1] STRING 'Hello World'");
        CHECK(toks[2].tok_type == TOK_EOC, "[2] EOC");
        CHECK(toks[3].tok_type == TOK_EOF, "[3] EOF");
    }
    irx_tokn_free(NULL, toks, n);
}

static void test_compound_symbol(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;

    printf("\n--- Test: compound symbol stem.i.j ---\n");

    run("stem.i.j", &toks, &n, &err);
    CHECK(n == 3, "3 tokens (SYMBOL EOC EOF)");
    if (n >= 1)
    {
        CHECK(toks[0].tok_type == TOK_SYMBOL,
              "[0] is SYMBOL");
        CHECK(tok_text_eq(&toks[0], "stem.i.j"),
              "[0] text == 'stem.i.j'");
        CHECK((toks[0].tok_flags & TOKF_COMPOUND) != 0,
              "[0] flagged COMPOUND");
    }
    irx_tokn_free(NULL, toks, n);
}

static void test_hex_string(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;

    printf("\n--- Test: hex string 'FF'x ---\n");

    run("'FF'x", &toks, &n, &err);
    CHECK(n == 3, "3 tokens (HEXSTRING EOC EOF)");
    if (n >= 1)
    {
        CHECK(toks[0].tok_type == TOK_HEXSTRING, "[0] HEXSTRING");
        CHECK(tok_text_eq(&toks[0], "FF"), "[0] body 'FF'");
    }
    irx_tokn_free(NULL, toks, n);
}

static void test_bin_string(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;

    printf("\n--- Test: bin string '1010'b ---\n");

    run("'1010'b", &toks, &n, &err);
    CHECK(n == 3, "3 tokens (BINSTRING EOC EOF)");
    if (n >= 1)
    {
        CHECK(toks[0].tok_type == TOK_BINSTRING, "[0] BINSTRING");
        CHECK(tok_text_eq(&toks[0], "1010"), "[0] body '1010'");
    }
    irx_tokn_free(NULL, toks, n);
}

static void test_string_doubling(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;

    printf("\n--- Test: string with doubled quote 'it''s' ---\n");

    run("'it''s'", &toks, &n, &err);
    CHECK(n == 3, "3 tokens");
    if (n >= 1)
    {
        CHECK(toks[0].tok_type == TOK_STRING, "[0] STRING");
        /* Body retains the doubling - decoding is the parser's job. */
        CHECK(tok_text_eq(&toks[0], "it''s"),
              "[0] body keeps doubled quote (decoded later)");
        CHECK((toks[0].tok_flags & TOKF_QUOTE_DBL) != 0,
              "[0] TOKF_QUOTE_DBL set");
    }
    irx_tokn_free(NULL, toks, n);
}

static void test_operators_single_char(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;
    int rc;

    printf("\n--- Test: single-character operator emission ---\n");

    /* Each operator character is emitted as its own token; composite
     * forms (||, **, etc.) are the parser's job. */
    rc = run("a||b", &toks, &n, &err);
    CHECK(rc == 0, "'a||b' tokenizes");
    CHECK(n == 6, "6 tokens (SYM | | SYM EOC EOF)");
    if (n >= 6)
    {
        CHECK(toks[0].tok_type == TOK_SYMBOL && tok_text_eq(&toks[0], "a"),
              "[0] SYMBOL a");
        CHECK(toks[1].tok_type == TOK_LOGICAL && tok_text_eq(&toks[1], "|"),
              "[1] LOGICAL '|' (first of ||)");
        CHECK(toks[2].tok_type == TOK_LOGICAL && tok_text_eq(&toks[2], "|"),
              "[2] LOGICAL '|' (second of ||)");
        CHECK(toks[3].tok_type == TOK_SYMBOL && tok_text_eq(&toks[3], "b"),
              "[3] SYMBOL b");
    }
    irx_tokn_free(NULL, toks, n);

    /* ** as two TOK_OPERATOR */
    toks = NULL;
    n = 0;
    rc = run("2**3", &toks, &n, &err);
    CHECK(rc == 0, "'2**3' tokenizes");
    if (n >= 4)
    {
        CHECK(toks[0].tok_type == TOK_NUMBER, "[0] NUMBER 2");
        CHECK(toks[1].tok_type == TOK_OPERATOR && tok_text_eq(&toks[1], "*"),
              "[1] OPERATOR '*'");
        CHECK(toks[2].tok_type == TOK_OPERATOR && tok_text_eq(&toks[2], "*"),
              "[2] OPERATOR '*'");
        CHECK(toks[3].tok_type == TOK_NUMBER, "[3] NUMBER 3");
    }
    irx_tokn_free(NULL, toks, n);

    /* \= as NOT followed by COMPARISON */
    toks = NULL;
    n = 0;
    rc = run("a\\=b", &toks, &n, &err);
    CHECK(rc == 0, "'a\\=b' tokenizes");
    if (n >= 4)
    {
        CHECK(toks[1].tok_type == TOK_NOT && tok_text_eq(&toks[1], "\\"),
              "[1] NOT '\\'");
        CHECK(toks[2].tok_type == TOK_COMPARISON && tok_text_eq(&toks[2], "="),
              "[2] COMPARISON '='");
    }
    irx_tokn_free(NULL, toks, n);

    /* >= as two COMPARISONs */
    toks = NULL;
    n = 0;
    rc = run("a>=b", &toks, &n, &err);
    CHECK(rc == 0, "'a>=b' tokenizes");
    if (n >= 4)
    {
        CHECK(toks[1].tok_type == TOK_COMPARISON && tok_text_eq(&toks[1], ">"),
              "[1] COMPARISON '>'");
        CHECK(toks[2].tok_type == TOK_COMPARISON && tok_text_eq(&toks[2], "="),
              "[2] COMPARISON '='");
    }
    irx_tokn_free(NULL, toks, n);

    /* Comment between || characters - parser will still see two | */
    toks = NULL;
    n = 0;
    rc = run("a| /* gap */ |b", &toks, &n, &err);
    CHECK(rc == 0, "'a| /* gap */ |b' tokenizes (comment inside ||)");
    if (n >= 4)
    {
        CHECK(toks[1].tok_type == TOK_LOGICAL && tok_text_eq(&toks[1], "|"),
              "first | preserved");
        CHECK(toks[2].tok_type == TOK_LOGICAL && tok_text_eq(&toks[2], "|"),
              "second | preserved across the comment");
    }
    irx_tokn_free(NULL, toks, n);
}

static void test_punctuation(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;
    int rc;

    printf("\n--- Test: ; and : are TOK_SEMICOLON ---\n");

    rc = run("a;b", &toks, &n, &err);
    CHECK(rc == 0, "'a;b' tokenizes");
    if (n >= 4)
    {
        CHECK(toks[1].tok_type == TOK_SEMICOLON, "[1] ';' is SEMICOLON");
    }
    irx_tokn_free(NULL, toks, n);

    toks = NULL;
    n = 0;
    rc = run("loop:\nsay 'x'", &toks, &n, &err);
    CHECK(rc == 0, "'loop:' label tokenizes");
    if (n >= 2)
    {
        CHECK(toks[0].tok_type == TOK_SYMBOL && tok_text_eq(&toks[0], "loop"),
              "[0] SYMBOL 'loop'");
        CHECK(toks[1].tok_type == TOK_SEMICOLON,
              "[1] ':' is SEMICOLON");
    }
    irx_tokn_free(NULL, toks, n);
}

static void test_comments_and_lines(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;
    const char *src =
        "/* line 1 */\n"
        "say /* nested /* deeper */ done */ 'hi'\n"
        "x = 1\n";
    int rc;

    printf("\n--- Test: nested comments + line numbers ---\n");

    rc = run(src, &toks, &n, &err);
    CHECK(rc == 0, "tokenizer returns 0");
    if (rc == 0)
    {
        CHECK(toks[0].tok_type == TOK_SYMBOL && tok_text_eq(&toks[0], "say"),
              "[0] 'say' on line 2");
        CHECK(toks[0].tok_line == 2, "[0] tok_line == 2");
        CHECK(toks[1].tok_type == TOK_STRING && tok_text_eq(&toks[1], "hi"),
              "[1] STRING 'hi' on line 2");
        CHECK(toks[1].tok_line == 2, "[1] tok_line == 2");
        /* After EOC, we have x = 1 on line 3. */
        int i;
        int found_x = 0;
        for (i = 0; i < n; i++)
        {
            if (toks[i].tok_type == TOK_SYMBOL &&
                tok_text_eq(&toks[i], "x"))
            {
                found_x = (toks[i].tok_line == 3);
                break;
            }
        }
        CHECK(found_x, "'x' is on line 3 (line numbers preserved)");
    }
    irx_tokn_free(NULL, toks, n);
}

static void test_continuation(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;
    const char *src = "say 'a',\n'b'\n";
    int rc;

    printf("\n--- Test: trailing-comma continuation ---\n");

    rc = run(src, &toks, &n, &err);
    CHECK(rc == 0, "tokenizer returns 0");
    /* Comma stays (argument separator), only EOC suppressed:
     * SYM STR COMMA STR EOC EOF = 6 */
    CHECK(n == 6, "6 tokens (comma kept, EOC suppressed)");
    if (n >= 5)
    {
        CHECK(toks[0].tok_type == TOK_SYMBOL, "[0] SYMBOL say");
        CHECK(toks[1].tok_type == TOK_STRING, "[1] STRING 'a'");
        CHECK(toks[2].tok_type == TOK_COMMA, "[2] COMMA (kept for parser)");
        CHECK(toks[3].tok_type == TOK_STRING, "[3] STRING 'b'");
        CHECK(toks[4].tok_type == TOK_EOC, "[4] EOC");
    }
    irx_tokn_free(NULL, toks, n);
}

static void test_numbers(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;

    printf("\n--- Test: numbers ---\n");

    run("42 3.14 1E5 1.5e-2 .5", &toks, &n, &err);
    CHECK(n == 7, "5 numbers + EOC + EOF");
    if (n >= 5)
    {
        CHECK(toks[0].tok_type == TOK_NUMBER && tok_text_eq(&toks[0], "42"),
              "42");
        CHECK(toks[1].tok_type == TOK_NUMBER && tok_text_eq(&toks[1], "3.14"),
              "3.14");
        CHECK(toks[2].tok_type == TOK_NUMBER && tok_text_eq(&toks[2], "1E5"),
              "1E5");
        CHECK(toks[3].tok_type == TOK_NUMBER && tok_text_eq(&toks[3], "1.5e-2"),
              "1.5e-2");
        CHECK(toks[4].tok_type == TOK_NUMBER && tok_text_eq(&toks[4], ".5"),
              ".5");
    }
    irx_tokn_free(NULL, toks, n);
}

static void test_unterminated_string(void)
{
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;
    int rc;

    printf("\n--- Test: unterminated string error ---\n");

    rc = run("say 'oops", &toks, &n, &err);
    CHECK(rc != 0, "returns error");
    CHECK(err.error_code == TOKERR_UNTERMINATED_STR,
          "error_code == TOKERR_UNTERMINATED_STR");
    CHECK(err.error_line == 1, "error_line == 1");
    CHECK(toks == NULL, "out_tokens cleared on error");
}

static void test_stress_1000_lines(void)
{
    /* Build "say 'line N'\n" 1000 times. */
    char *buf;
    int buf_len = 0;
    int buf_cap = 64 * 1024;
    int i;
    struct irx_token *toks = NULL;
    int n = 0;
    struct irx_tokn_error err;
    int rc;

    printf("\n--- Test: stress 1000 lines ---\n");

    buf = (char *)malloc((size_t)buf_cap);
    if (buf == NULL)
    {
        CHECK(0, "malloc");
        return;
    }

    for (i = 1; i <= 1000; i++)
    {
        int written = sprintf(buf + buf_len, "say 'line %d'\n", i);
        buf_len += written;
    }

    rc = run(buf, &toks, &n, &err);
    CHECK(rc == 0, "tokenizes 1000 lines without error");
    /* Each line: SYMBOL STRING EOC = 3 tokens, plus trailing EOF */
    CHECK(n == 1000 * 3 + 1, "produces 3001 tokens (3 per line + EOF)");

    irx_tokn_free(NULL, toks, n);
    free(buf);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== REXX/370 WP-10 Tokenizer Tests ===\n");

    test_hello_world();
    test_compound_symbol();
    test_hex_string();
    test_bin_string();
    test_string_doubling();
    test_operators_single_char();
    test_punctuation();
    test_comments_and_lines();
    test_continuation();
    test_numbers();
    test_unterminated_string();
    test_stress_1000_lines();

    printf("\n=== Results: %d/%d passed",
           tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
