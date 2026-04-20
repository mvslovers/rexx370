/* ------------------------------------------------------------------ */
/*  tstprsr.c - WP-13 parser + expression evaluator unit tests    */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_parser \               */
/*        test/test_parser.c \                                         */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \         */
/*        'src/irx#anch.c'  'src/irx#uid.c'  'src/irx#msid.c' \         */
/*        'src/irx#lstr.c' 'src/irx#tokn.c' 'src/irx#vpol.c' \         */
/*        'src/irx#pars.c' \                                           */
/*        ../lstring370/src/'lstr#cor.c'                              */
/*                                                                    */
/*  Each test drives a short REXX source through the tokenizer +      */
/*  parser and verifies observable state via vpool_get.               */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxpars.h"
#include "irxtokn.h"
#include "irxvpool.h"
#include "lstralloc.h"
#include "lstring.h"

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

static int lstr_eq_cstr(const Lstr *s, const char *cstr)
{
    size_t n = strlen(cstr);
    if (s->len != n)
    {
        return 0;
    }
    return memcmp(s->pstr, cstr, n) == 0;
}

static void set_lstr(struct lstr_alloc *a, PLstr s, const char *c)
{
    Lscpy(a, s, c);
}

/* Run a complete REXX source through tokenizer + parser, optionally
 * tied to an ENVBLOCK so BIF dispatch can reach the per-env registry.
 * Returns the final parser return code (IRXPARS_*). */
static int run_source_env(struct lstr_alloc *a, struct irx_vpool *pool,
                          const char *src, struct envblock *env)
{
    struct irx_token *tokens = NULL;
    int count = 0;
    struct irx_tokn_error tok_err;
    struct irx_parser parser;
    int rc;

    rc = irx_tokn_run(NULL, src, (int)strlen(src), &tokens, &count,
                      &tok_err);
    if (rc != 0)
    {
        printf("    tokenizer failed: rc=%d code=%d line=%d col=%d\n",
               rc, tok_err.error_code, tok_err.error_line,
               tok_err.error_col);
        return -1;
    }

    rc = irx_pars_init(&parser, tokens, count, pool, a, env);
    if (rc != IRXPARS_OK)
    {
        irx_tokn_free(NULL, tokens, count);
        printf("    irx_pars_init failed: rc=%d\n", rc);
        return rc;
    }

    rc = irx_pars_run(&parser);
    if (rc != IRXPARS_OK)
    {
        printf("    parser rc=%d error=%d line=%d\n", rc,
               parser.error_code, parser.error_line);
    }

    irx_pars_cleanup(&parser);
    irx_tokn_free(NULL, tokens, count);
    return rc;
}

static int run_source(struct lstr_alloc *a, struct irx_vpool *pool,
                      const char *src)
{
    return run_source_env(a, pool, src, NULL);
}

/* Convenience: run_source then vpool_get("X") and compare. */
static int get_var_eq(struct lstr_alloc *a, struct irx_vpool *pool,
                      const char *name, const char *expected)
{
    Lstr key, val;
    int rc;
    int eq;

    Lzeroinit(&key);
    Lzeroinit(&val);
    set_lstr(a, &key, name);

    rc = vpool_get(pool, &key, &val);
    if (rc != VPOOL_OK)
    {
        printf("    vpool_get(%s) rc=%d\n", name, rc);
        Lfree(a, &key);
        Lfree(a, &val);
        return 0;
    }
    eq = lstr_eq_cstr(&val, expected);
    if (!eq)
    {
        printf("    %s = '%.*s' (expected '%s')\n",
               name, (int)val.len, (const char *)val.pstr, expected);
    }
    Lfree(a, &key);
    Lfree(a, &val);
    return eq;
}

/* ------------------------------------------------------------------ */
/*  Test cases                                                        */
/* ------------------------------------------------------------------ */

static void test_ac1_assignment_arithmetic(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- AC#1: x = 2 + 3 ---\n");
    CHECK(run_source(a, pool, "x = 2 + 3\n") == IRXPARS_OK, "parser OK");
    CHECK(get_var_eq(a, pool, "X", "5"), "X = '5'");
    vpool_destroy(pool);
}

static void test_ac2_blank_concat(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- AC#2: x = 'hello' 'world' ---\n");
    CHECK(run_source(a, pool, "x = 'hello' 'world'\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "hello world"), "X = 'hello world'");
    vpool_destroy(pool);
}

static void test_ac3_explicit_concat(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- AC#3: x = a || b  (a='foo', b='bar') ---\n");
    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "A");
    set_lstr(a, &v, "foo");
    vpool_set(pool, &k, &v);
    set_lstr(a, &k, "B");
    set_lstr(a, &v, "bar");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool, "x = a || b\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "foobar"), "X = 'foobar'");
    vpool_destroy(pool);
}

static void test_ac4_precedence(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- AC#4: x = 2 + 3 * 4 ---\n");
    CHECK(run_source(a, pool, "x = 2 + 3 * 4\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "14"), "X = '14'");
    vpool_destroy(pool);
}

static void test_ac5_power_right_assoc(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- AC#5: x = 2 ** 3 ** 2 ---\n");
    CHECK(run_source(a, pool, "x = 2 ** 3 ** 2\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "512"), "X = '512' (2**9)");
    vpool_destroy(pool);
}

static void test_ac6_eq_case_sensitive(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- AC#6: normal '=' is case-sensitive, blank-padded ---\n");

    CHECK(run_source(a, pool, "r = ('abc' = 'ABC')\n") == IRXPARS_OK,
          "parser OK (abc = ABC)");
    CHECK(get_var_eq(a, pool, "R", "0"),
          "'abc' = 'ABC' -> 0 (different case)");

    CHECK(run_source(a, pool, "r = ('abc' = 'abc')\n") == IRXPARS_OK,
          "parser OK (abc = abc)");
    CHECK(get_var_eq(a, pool, "R", "1"),
          "'abc' = 'abc' -> 1 (identical bytes)");

    CHECK(run_source(a, pool, "r = ('abc ' = 'abc')\n") == IRXPARS_OK,
          "parser OK ('abc ' = 'abc')");
    CHECK(get_var_eq(a, pool, "R", "1"),
          "'abc ' = 'abc' -> 1 (blank-padded equal)");

    CHECK(run_source(a, pool, "r = ('ABC' = 'ABC')\n") == IRXPARS_OK,
          "parser OK (ABC = ABC)");
    CHECK(get_var_eq(a, pool, "R", "1"), "'ABC' = 'ABC' -> 1");

    vpool_destroy(pool);
}

static void test_ac7_strict_eq(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- AC#7: r = ('abc' == 'ABC') ---\n");
    CHECK(run_source(a, pool, "r = ('abc' == 'ABC')\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "R", "0"), "R = '0'");
    vpool_destroy(pool);
}

static void test_ac8_function_length(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    struct envblock *env = NULL;

    printf("\n--- AC#8: x = LENGTH('hello') ---\n");
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    CHECK(run_source_env(a, pool, "x = LENGTH('hello')\n", env) ==
              IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "5"), "X = '5'");
    if (env != NULL)
    {
        irxterm(env);
    }
    vpool_destroy(pool);
}

static void test_ac9_nested_parens(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- AC#9: x = (2 + 3) * (4 + 5) ---\n");
    CHECK(run_source(a, pool, "x = (2 + 3) * (4 + 5)\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "45"), "X = '45'");
    vpool_destroy(pool);
}

static void test_ac10_compound(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- AC#10: I='FOO'; STEM.FOO='bar'; x = STEM.I ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "I");
    set_lstr(a, &v, "FOO");
    vpool_set(pool, &k, &v);
    set_lstr(a, &k, "STEM.FOO");
    set_lstr(a, &v, "bar");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool, "x = STEM.I\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "bar"),
          "X = 'bar' (STEM.I -> STEM.FOO -> 'bar')");
    vpool_destroy(pool);
}

static void test_ac11_say_equals(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- AC#11: SAY = 5  (assignment, not keyword) ---\n");
    CHECK(run_source(a, pool, "SAY = 5\n") == IRXPARS_OK, "parser OK");
    CHECK(get_var_eq(a, pool, "SAY", "5"), "SAY = '5'");
    vpool_destroy(pool);
}

static void test_ac12_strict_not_assignment(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    int rc_set;
    int not_set;

    printf("\n--- AC#12: x == 5  (command, not assignment) ---\n");

    /* Pre-populate X so that a mistaken assignment would overwrite it. */
    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "preset");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool, "x == 5\n") == IRXPARS_OK, "parser OK");
    rc_set = get_var_eq(a, pool, "X", "preset");
    not_set = rc_set;
    CHECK(not_set, "X still 'preset' (no assignment happened)");
    vpool_destroy(pool);
}

static void test_ac13_abuttal_vs_blank(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- AC#13: doubled quotes and blank concat ---\n");

    CHECK(run_source(a, pool, "y = 'a''b'\n") == IRXPARS_OK,
          "parser OK (y)");
    CHECK(get_var_eq(a, pool, "Y", "a'b"),
          "Y = \"a'b\" (doubled single quote escape)");

    CHECK(run_source(a, pool, "z = 'a' 'b'\n") == IRXPARS_OK,
          "parser OK (z)");
    CHECK(get_var_eq(a, pool, "Z", "a b"), "Z = 'a b' (blank concat)");
    vpool_destroy(pool);
}

static void test_ac14_no_global_state(void)
{
    /* Two independent pools run side-by-side must not interfere. */
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool_a = vpool_create(a, NULL);
    struct irx_vpool *pool_b = vpool_create(a, NULL);

    printf("\n--- AC#14: two pools side-by-side (no globals) ---\n");

    CHECK(run_source(a, pool_a, "n = 1 + 2\n") == IRXPARS_OK,
          "parser OK (pool A)");
    CHECK(run_source(a, pool_b, "n = 10 * 10\n") == IRXPARS_OK,
          "parser OK (pool B)");
    CHECK(get_var_eq(a, pool_a, "N", "3"), "pool A: N = '3'");
    CHECK(get_var_eq(a, pool_b, "N", "100"), "pool B: N = '100'");

    vpool_destroy(pool_a);
    vpool_destroy(pool_b);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== WP-13: Parser + Expression Evaluator tests ===\n");

    test_ac1_assignment_arithmetic();
    test_ac2_blank_concat();
    test_ac3_explicit_concat();
    test_ac4_precedence();
    test_ac5_power_right_assoc();
    test_ac6_eq_case_sensitive();
    test_ac7_strict_eq();
    test_ac8_function_length();
    test_ac9_nested_parens();
    test_ac10_compound();
    test_ac11_say_equals();
    test_ac12_strict_not_assignment();
    test_ac13_abuttal_vs_blank();
    test_ac14_no_global_state();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
