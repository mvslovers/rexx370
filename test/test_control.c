/* ------------------------------------------------------------------ */
/*  test_control.c - WP-15 Control Flow unit tests                    */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_control \             */
/*        test/test_control.c \                                        */
/*        'src/irx#ctrl.c' 'src/irx#lstr.c' 'src/irx#tokn.c' \       */
/*        'src/irx#vpol.c' 'src/irx#pars.c' \                         */
/*        ../lstring370/src/'lstr#cor.c'                              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irxctrl.h"
#include "irxpars.h"
#include "irxtokn.h"
#include "irxvpool.h"
#include "lstralloc.h"
#include "lstring.h"

#ifndef __MVS__
void *_simulated_tcbuser = NULL;
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

static int run_source(struct lstr_alloc *a, struct irx_vpool *pool,
                      const char *src)
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

    rc = irx_pars_init(&parser, tokens, count, pool, a, NULL);
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

static int get_var_eq(struct lstr_alloc *a, struct irx_vpool *pool,
                      const char *name, const char *expected)
{
    Lstr key, val;
    int rc, eq;

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

/* CF#1: DO i = 1 TO 5; x = x + i; END */
static void test_cf1_do_counted_loop(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#1: DO i = 1 TO 5; x = x + i; END ---\n");

    /* Pre-set x = 0 */
    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "DO i = 1 TO 5\n"
                     "  x = x + i\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "15"), "X = '15' (1+2+3+4+5)");
    CHECK(get_var_eq(a, pool, "I", "5"), "I = '5' (final value)");
    vpool_destroy(pool);
}

/* CF#2: DO WHILE x < 10 (pre-test) */
static void test_cf2_do_while(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#2: DO WHILE x < 10 ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "DO WHILE x < 10\n"
                     "  x = x + 3\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "12"), "X = '12' (0+3+3+3+3)");
    vpool_destroy(pool);
}

/* CF#3: DO UNTIL x > 10 (post-test, runs at least once) */
static void test_cf3_do_until(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#3: DO UNTIL x > 10 ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "DO UNTIL x > 10\n"
                     "  x = x + 4\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "12"), "X = '12' (0+4+4+4)");
    vpool_destroy(pool);
}

/* CF#4: Nested DO loops */
static void test_cf4_nested_do(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#4: Nested DO loops ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "N");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "DO i = 1 TO 3\n"
                     "  DO j = 1 TO 3\n"
                     "    n = n + 1\n"
                     "  END\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "N", "9"), "N = '9' (3x3 iterations)");
    vpool_destroy(pool);
}

/* CF#5: IF THEN ELSE — true branch */
static void test_cf5_if_true(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- CF#5: IF 1 THEN x = 'yes'; ELSE x = 'no' ---\n");

    CHECK(run_source(a, pool,
                     "IF 1 THEN x = 'yes'\n"
                     "ELSE x = 'no'\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "yes"), "X = 'yes' (true branch)");
    vpool_destroy(pool);
}

/* CF#6: IF THEN ELSE — false branch */
static void test_cf6_if_false(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- CF#6: IF 0 THEN x = 'yes'; ELSE x = 'no' ---\n");

    CHECK(run_source(a, pool,
                     "IF 0 THEN x = 'yes'\n"
                     "ELSE x = 'no'\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "no"), "X = 'no' (false branch)");
    vpool_destroy(pool);
}

/* CF#7: Nested IF — ELSE binds to inner IF */
static void test_cf7_nested_if_else(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#7: IF a THEN IF b THEN c ELSE d (ELSE binds to inner) ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "A");
    set_lstr(a, &v, "1");
    vpool_set(pool, &k, &v);
    set_lstr(a, &k, "B");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "IF a THEN IF b THEN x = 'c'\n"
                     "ELSE x = 'd'\n") == IRXPARS_OK,
          "parser OK");
    /* ELSE binds to inner IF (b=0), so x='d' */
    CHECK(get_var_eq(a, pool, "X", "d"), "X = 'd' (ELSE bound to inner IF)");
    vpool_destroy(pool);
}

/* CF#8: SELECT with first WHEN matching */
static void test_cf8_select_first_when(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#8: SELECT; WHEN n=1 THEN x='one'; WHEN n=2 THEN x='two'; END ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "N");
    set_lstr(a, &v, "1");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "SELECT\n"
                     "  WHEN n = 1 THEN x = 'one'\n"
                     "  WHEN n = 2 THEN x = 'two'\n"
                     "  OTHERWISE x = 'other'\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "one"), "X = 'one'");
    vpool_destroy(pool);
}

/* CF#9: SELECT with second WHEN matching */
static void test_cf9_select_second_when(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#9: SELECT n=2 matches second WHEN ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "N");
    set_lstr(a, &v, "2");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "SELECT\n"
                     "  WHEN n = 1 THEN x = 'one'\n"
                     "  WHEN n = 2 THEN x = 'two'\n"
                     "  OTHERWISE x = 'other'\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "two"), "X = 'two'");
    vpool_destroy(pool);
}

/* CF#10: SELECT with OTHERWISE */
static void test_cf10_select_otherwise(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#10: SELECT n=99 falls through to OTHERWISE ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "N");
    set_lstr(a, &v, "99");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "SELECT\n"
                     "  WHEN n = 1 THEN x = 'one'\n"
                     "  WHEN n = 2 THEN x = 'two'\n"
                     "  OTHERWISE x = 'other'\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "other"), "X = 'other'");
    vpool_destroy(pool);
}

/* CF#11: SELECT with no match and no OTHERWISE -> SYNTAX error */
static void test_cf11_select_no_match(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#11: SELECT no match, no OTHERWISE -> SYNTAX error ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "N");
    set_lstr(a, &v, "99");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "SELECT\n"
                     "  WHEN n = 1 THEN x = 'one'\n"
                     "END\n") != IRXPARS_OK,
          "returns error (no OTHERWISE, no match)");
    vpool_destroy(pool);
}

/* CF#12: CALL label -> RETURN -> resumes */
static void test_cf12_call_return(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- CF#12: CALL label; RETURN ---\n");

    CHECK(run_source(a, pool,
                     "x = 1\n"
                     "CALL mysub\n"
                     "x = x + 10\n"
                     "EXIT 0\n"
                     "mysub:\n"
                     "  x = x + 100\n"
                     "RETURN\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "111"), "X = '111' (1+100+10)");
    vpool_destroy(pool);
}

/* CF#13: CALL sets SIGL */
static void test_cf13_call_sigl(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- CF#13: CALL sets SIGL to call line ---\n");

    CHECK(run_source(a, pool,
                     "CALL sub\n" /* line 1 */
                     "EXIT 0\n"
                     "sub:\n"
                     "RETURN\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "SIGL", "1"), "SIGL = '1'");
    vpool_destroy(pool);
}

/* CF#14: RETURN sets RESULT */
static void test_cf14_return_result(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- CF#14: RETURN expr sets RESULT ---\n");

    CHECK(run_source(a, pool,
                     "CALL add\n"
                     "EXIT 0\n"
                     "add:\n"
                     "RETURN 42\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "RESULT", "42"), "RESULT = '42'");
    vpool_destroy(pool);
}

/* CF#15: EXIT terminates execution */
static void test_cf15_exit(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- CF#15: EXIT 0 terminates, code after is not run ---\n");

    CHECK(run_source(a, pool,
                     "x = 'before'\n"
                     "EXIT 0\n"
                     "x = 'after'\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "before"), "X = 'before' (EXIT stopped)");
    vpool_destroy(pool);
}

/* CF#16: SIGNAL label clears stack and transfers control */
static void test_cf16_signal(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- CF#16: SIGNAL label clears stack, transfers control ---\n");

    CHECK(run_source(a, pool,
                     "x = 'start'\n"
                     "DO i = 1 TO 100\n"
                     "  SIGNAL done\n"
                     "END\n"
                     "x = 'unreachable'\n"
                     "done:\n"
                     "x = 'signalled'\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "signalled"), "X = 'signalled'");
    vpool_destroy(pool);
}

/* CF#17: NOP is valid as THEN target */
static void test_cf17_nop(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- CF#17: NOP as THEN target ---\n");

    CHECK(run_source(a, pool,
                     "x = 'ok'\n"
                     "IF 1 THEN NOP\n"
                     "ELSE x = 'bad'\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "ok"), "X = 'ok' (NOP took THEN)");
    vpool_destroy(pool);
}

/* CF#18: ITERATE skips to next iteration */
static void test_cf18_iterate(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#18: ITERATE skips to next loop iteration ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    /* Sum only even numbers 1..6 -> 2+4+6 = 12 */
    CHECK(run_source(a, pool,
                     "DO i = 1 TO 6\n"
                     "  IF i // 2 = 1 THEN ITERATE\n" /* skip odd */
                     "  x = x + i\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "12"), "X = '12' (2+4+6)");
    vpool_destroy(pool);
}

/* CF#19: LEAVE exits the loop */
static void test_cf19_leave(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#19: LEAVE exits the loop early ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "DO i = 1 TO 100\n"
                     "  x = x + 1\n"
                     "  IF i = 5 THEN LEAVE\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "5"), "X = '5' (LEAVE after 5 iters)");
    vpool_destroy(pool);
}

/* CF#20: Label table built before execution (SIGNAL forward jump) */
static void test_cf20_signal_forward(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    printf("\n--- CF#20: SIGNAL forward-jump to a label defined later ---\n");

    CHECK(run_source(a, pool,
                     "SIGNAL target\n"
                     "x = 'skipped'\n"
                     "target:\n"
                     "x = 'reached'\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "reached"), "X = 'reached'");
    vpool_destroy(pool);
}

/* CF#21: No global state — two independent parsers */
static void test_cf21_no_global_state(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool_a = vpool_create(a, NULL);
    struct irx_vpool *pool_b = vpool_create(a, NULL);
    printf("\n--- CF#21: Two independent parsers, no shared state ---\n");

    /* Pre-initialize N=0 in each pool so n+i arithmetic works. */
    Lstr k2, v2;
    Lzeroinit(&k2);
    Lzeroinit(&v2);
    set_lstr(a, &k2, "N");
    set_lstr(a, &v2, "0");
    vpool_set(pool_a, &k2, &v2);
    vpool_set(pool_b, &k2, &v2);
    Lfree(a, &k2);
    Lfree(a, &v2);

    CHECK(run_source(a, pool_a,
                     "DO i = 1 TO 3; n = n + i; END\n") == IRXPARS_OK,
          "parser A OK");
    CHECK(run_source(a, pool_b,
                     "DO i = 1 TO 4; n = n + i; END\n") == IRXPARS_OK,
          "parser B OK");
    CHECK(get_var_eq(a, pool_a, "N", "6"), "pool A: N = '6'  (1+2+3)");
    CHECK(get_var_eq(a, pool_b, "N", "10"), "pool B: N = '10' (1+2+3+4)");

    vpool_destroy(pool_a);
    vpool_destroy(pool_b);
}

/* CF#22: DO FOREVER + LEAVE */
static void test_cf22_do_forever(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#22: DO FOREVER with LEAVE ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "DO FOREVER\n"
                     "  x = x + 1\n"
                     "  IF x >= 3 THEN LEAVE\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "3"), "X = '3'");
    vpool_destroy(pool);
}

/* CF#23: DO n (repetitive count) */
static void test_cf23_do_count(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#23: DO 4 (four iterations) ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "DO 4\n"
                     "  x = x + 1\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "4"), "X = '4'");
    vpool_destroy(pool);
}

/* CF#24: IF without ELSE */
static void test_cf24_if_no_else(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#24: IF without ELSE (false branch -> skip) ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "original");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "IF 0 THEN x = 'changed'\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "original"), "X unchanged");
    vpool_destroy(pool);
}

/* CF#25: IF with DO block in THEN branch */
static void test_cf25_if_do_block(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#25: IF THEN DO ... END ELSE simple ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "C");
    set_lstr(a, &v, "1");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "IF c THEN\n"
                     "  DO\n"
                     "    x = 'block'\n"
                     "    y = 'ran'\n"
                     "  END\n"
                     "ELSE x = 'skip'\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "block"), "X = 'block'");
    CHECK(get_var_eq(a, pool, "Y", "ran"), "Y = 'ran'");
    vpool_destroy(pool);
}

/* CF#26: DO i = 5 TO 1 BY -1 (negative step, countdown) */
static void test_cf26_do_ctrl_neg_step(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#26: DO i = 5 TO 1 BY -1 (negative step) ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "DO i = 5 TO 1 BY -1\n"
                     "  x = x + i\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "15"), "X = '15' (5+4+3+2+1)");
    CHECK(get_var_eq(a, pool, "I", "1"), "I = '1' (final value)");
    vpool_destroy(pool);
}

/* CF#27: DO i = 1 TO 10 BY 2 (step > 1) */
static void test_cf27_do_ctrl_step2(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#27: DO i = 1 TO 10 BY 2 (step > 1) ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "DO i = 1 TO 10 BY 2\n"
                     "  x = x + i\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "25"), "X = '25' (1+3+5+7+9)");
    CHECK(get_var_eq(a, pool, "I", "9"), "I = '9' (final value)");
    vpool_destroy(pool);
}

/* CF#28: DO i = 1 TO 10 BY 3 (step doesn't land on limit exactly) */
static void test_cf28_do_ctrl_step3(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool *pool = vpool_create(a, NULL);
    Lstr k, v;
    printf("\n--- CF#28: DO i = 1 TO 10 BY 3 (step doesn't land on limit) ---\n");

    Lzeroinit(&k);
    Lzeroinit(&v);
    set_lstr(a, &k, "X");
    set_lstr(a, &v, "0");
    vpool_set(pool, &k, &v);
    Lfree(a, &k);
    Lfree(a, &v);

    CHECK(run_source(a, pool,
                     "DO i = 1 TO 10 BY 3\n"
                     "  x = x + i\n"
                     "END\n") == IRXPARS_OK,
          "parser OK");
    CHECK(get_var_eq(a, pool, "X", "22"), "X = '22' (1+4+7+10)");
    CHECK(get_var_eq(a, pool, "I", "10"), "I = '10' (final value)");
    vpool_destroy(pool);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== WP-15: Control Flow tests ===\n");

    test_cf1_do_counted_loop();
    test_cf2_do_while();
    test_cf3_do_until();
    test_cf4_nested_do();
    test_cf5_if_true();
    test_cf6_if_false();
    test_cf7_nested_if_else();
    test_cf8_select_first_when();
    test_cf9_select_second_when();
    test_cf10_select_otherwise();
    test_cf11_select_no_match();
    test_cf12_call_return();
    test_cf13_call_sigl();
    test_cf14_return_result();
    test_cf15_exit();
    test_cf16_signal();
    test_cf17_nop();
    test_cf18_iterate();
    test_cf19_leave();
    test_cf20_signal_forward();
    test_cf21_no_global_state();
    test_cf22_do_forever();
    test_cf23_do_count();
    test_cf24_if_no_else();
    test_cf25_if_do_block();
    test_cf26_do_ctrl_neg_step();
    test_cf27_do_ctrl_step2();
    test_cf28_do_ctrl_step3();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
