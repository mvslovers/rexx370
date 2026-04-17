/* ------------------------------------------------------------------ */
/*  test_bifs.c - WP-21a string BIF unit tests                        */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_bifs \                 */
/*        test/test_bifs.c \                                           */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \        */
/*        'src/irx#rab.c'  'src/irx#uid.c'  'src/irx#msid.c' \        */
/*        'src/irx#cond.c' 'src/irx#bif.c'  'src/irx#bifs.c' \        */
/*        'src/irx#io.c'   'src/irx#lstr.c' 'src/irx#tokn.c' \        */
/*        'src/irx#vpol.c' 'src/irx#pars.c' 'src/irx#ctrl.c' \        */
/*        'src/irx#arith.c' 'src/irx#exec.c' \                         */
/*        ../lstring370/src/lstr#*.c                                   */
/*                                                                    */
/*  Drives each BIF through the parser end-to-end and checks that     */
/*  the resulting variable matches expectation. Also covers the       */
/*  SYNTAX 40.x error paths.                                          */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxbif.h"
#include "irxcond.h"
#include "irxfunc.h"
#include "irxpars.h"
#include "irxtokn.h"
#include "irxvpool.h"
#include "irxwkblk.h"
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
/*  Fixture: irxinit + fresh vpool per test; run REXX source, fetch X */
/* ------------------------------------------------------------------ */

struct fixture
{
    struct envblock *env;
    struct lstr_alloc *alloc;
    struct irx_vpool *pool;
};

static int fixture_open(struct fixture *f)
{
    memset(f, 0, sizeof(*f));
    if (irxinit(NULL, &f->env) != 0)
    {
        return -1;
    }
    f->alloc = lstr_default_alloc();
    f->pool = vpool_create(f->alloc, NULL);
    return (f->pool != NULL) ? 0 : -1;
}

static void fixture_close(struct fixture *f)
{
    if (f->pool != NULL)
    {
        vpool_destroy(f->pool);
    }
    if (f->env != NULL)
    {
        irxterm(f->env);
    }
    memset(f, 0, sizeof(*f));
}

static int run_src(struct fixture *f, const char *src)
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
        return -1;
    }
    rc = irx_pars_init(&parser, tokens, count, f->pool, f->alloc, f->env);
    if (rc != IRXPARS_OK)
    {
        irx_tokn_free(NULL, tokens, count);
        return rc;
    }
    rc = irx_pars_run(&parser);
    irx_pars_cleanup(&parser);
    irx_tokn_free(NULL, tokens, count);
    return rc;
}

static int var_eq(struct fixture *f, const char *name, const char *want)
{
    Lstr key;
    Lstr val;
    int rc;
    int eq;

    Lzeroinit(&key);
    Lzeroinit(&val);
    Lscpy(f->alloc, &key, name);

    rc = vpool_get(f->pool, &key, &val);
    if (rc != VPOOL_OK)
    {
        Lfree(f->alloc, &key);
        Lfree(f->alloc, &val);
        return 0;
    }

    size_t wl = strlen(want);
    eq = (val.len == wl) && (memcmp(val.pstr, want, wl) == 0);
    if (!eq)
    {
        printf("    %s = '%.*s' (expected '%s')\n",
               name, (int)val.len, (const char *)val.pstr, want);
    }
    Lfree(f->alloc, &key);
    Lfree(f->alloc, &val);
    return eq;
}

/* Expect the parser to succeed and X to equal want. */
#define EXPECT_OK(src, want, msg)                \
    do                                           \
    {                                            \
        struct fixture fx;                       \
        if (fixture_open(&fx) != 0)              \
        {                                        \
            CHECK(0, "fixture_open " msg);       \
            break;                               \
        }                                        \
        int _rc = run_src(&fx, "x = " src "\n"); \
        if (_rc != IRXPARS_OK)                   \
        {                                        \
            printf("    parser rc=%d\n", _rc);   \
            CHECK(0, msg);                       \
        }                                        \
        else                                     \
        {                                        \
            CHECK(var_eq(&fx, "X", want), msg);  \
        }                                        \
        fixture_close(&fx);                      \
    } while (0)

/* ================================================================== */
/*  Phase B — LENGTH, LEFT, RIGHT, SUBSTR, POS, LASTPOS, INDEX        */
/* ================================================================== */

static void test_phase_b(void)
{
    printf("\n--- Phase B: substring & position ---\n");
    EXPECT_OK("LENGTH('hello')", "5", "LENGTH basic");
    EXPECT_OK("LENGTH('')", "0", "LENGTH empty");
    EXPECT_OK("LEFT('abcdef',3)", "abc", "LEFT basic");
    EXPECT_OK("LEFT('ab',5)", "ab   ", "LEFT pad default");
    EXPECT_OK("LEFT('ab',5,'.')", "ab...", "LEFT pad explicit");
    EXPECT_OK("LEFT('abcdef',0)", "", "LEFT zero");
    EXPECT_OK("RIGHT('abcdef',3)", "def", "RIGHT basic");
    EXPECT_OK("RIGHT('ab',5,'.')", "...ab", "RIGHT pad");
    EXPECT_OK("SUBSTR('abcdef',3)", "cdef", "SUBSTR from pos");
    EXPECT_OK("SUBSTR('abcdef',3,2)", "cd", "SUBSTR fixed length");
    EXPECT_OK("SUBSTR('ab',1,5)", "ab   ", "SUBSTR pad");
    EXPECT_OK("SUBSTR('ab',1,5,'*')", "ab***", "SUBSTR explicit pad");
    EXPECT_OK("POS('cd','abcdef')", "3", "POS found");
    EXPECT_OK("POS('x','abcdef')", "0", "POS not found");
    EXPECT_OK("POS('ab','abcabc',2)", "4", "POS with start");
    EXPECT_OK("INDEX('abcdef','cd')", "3", "INDEX found (args flipped)");
    EXPECT_OK("INDEX('abcdef','x')", "0", "INDEX not found");
    EXPECT_OK("LASTPOS('ab','ababab')", "5", "LASTPOS basic");
    EXPECT_OK("LASTPOS('x','abcdef')", "0", "LASTPOS not found");
}

/* ================================================================== */
/*  Phase C — word ops                                                 */
/* ================================================================== */

static void test_phase_c(void)
{
    printf("\n--- Phase C: word operations ---\n");
    EXPECT_OK("WORDS('foo bar baz')", "3", "WORDS 3");
    EXPECT_OK("WORDS('')", "0", "WORDS empty");
    EXPECT_OK("WORDS('   one   ')", "1", "WORDS trim");
    EXPECT_OK("WORD('foo bar baz',2)", "bar", "WORD 2");
    EXPECT_OK("WORD('foo bar',5)", "", "WORD out of range");
    EXPECT_OK("WORDINDEX('foo bar baz',2)", "5", "WORDINDEX 2");
    EXPECT_OK("WORDINDEX('foo bar',5)", "0", "WORDINDEX out of range");
    EXPECT_OK("WORDLENGTH('foo bar',2)", "3", "WORDLENGTH 2");
    EXPECT_OK("WORDLENGTH('foo bar',5)", "0", "WORDLENGTH oob");
    EXPECT_OK("SUBWORD('a b c d e',2,2)", "b c", "SUBWORD 2..3");
    EXPECT_OK("SUBWORD('a b c d e',3)", "c d e", "SUBWORD rest");
    EXPECT_OK("WORDPOS('bar baz','foo bar baz qux')", "2", "WORDPOS found");
    EXPECT_OK("WORDPOS('xx','foo bar')", "0", "WORDPOS not found");
}

/* ================================================================== */
/*  Phase D — padding/stripping/formatting                             */
/* ================================================================== */

static void test_phase_d(void)
{
    printf("\n--- Phase D: padding & formatting ---\n");
    EXPECT_OK("CENTER('abc',7)", "  abc  ", "CENTER default");
    EXPECT_OK("CENTRE('abc',7,'-')", "--abc--", "CENTRE alias + pad");
    EXPECT_OK("CENTER('abc',2)", "ab", "CENTER truncates (odd remainder from end)");
    EXPECT_OK("STRIP('   abc   ')", "abc", "STRIP both");
    EXPECT_OK("STRIP('   abc   ','L')", "abc   ", "STRIP leading");
    EXPECT_OK("STRIP('   abc   ','T')", "   abc", "STRIP trailing");
    EXPECT_OK("STRIP('xxxabcxxx','B','x')", "abc", "STRIP char");
    EXPECT_OK("SPACE('  foo   bar   baz  ')", "foo bar baz", "SPACE default");
    EXPECT_OK("SPACE('foo   bar',2)", "foo  bar", "SPACE n=2");
    EXPECT_OK("SPACE('foo   bar',1,'.')", "foo.bar", "SPACE pad");
    EXPECT_OK("COPIES('ab',3)", "ababab", "COPIES 3");
    EXPECT_OK("COPIES('ab',0)", "", "COPIES 0");
    EXPECT_OK("REVERSE('abc')", "cba", "REVERSE");
    EXPECT_OK("REVERSE('')", "", "REVERSE empty");
    EXPECT_OK("JUSTIFY('foo bar baz',15)", "foo   bar   baz", "JUSTIFY 15");
    EXPECT_OK("JUSTIFY('foo bar',7)", "foo bar", "JUSTIFY exact");
    EXPECT_OK("JUSTIFY('foo bar',3)", "foo", "JUSTIFY truncate");
    EXPECT_OK("JUSTIFY('hello',8,'-')", "hello---", "JUSTIFY single word");
}

/* ================================================================== */
/*  Phase E — insert/delete/overlay                                   */
/* ================================================================== */

static void test_phase_e(void)
{
    printf("\n--- Phase E: insert / delete / overlay ---\n");
    EXPECT_OK("INSERT('XY','abcdef',3)", "abcXYdef", "INSERT at 3");
    EXPECT_OK("INSERT('XY','abc',0)", "XYabc", "INSERT at 0 (prepend)");
    EXPECT_OK("INSERT('XY','abc',5)", "abc  XY", "INSERT pad");
    EXPECT_OK("INSERT('XY','abc',3,5)", "abcXY   ", "INSERT pad length");
    EXPECT_OK("INSERT('XY','abc',3,5,'.')", "abcXY...", "INSERT pad char");
    EXPECT_OK("OVERLAY('XY','abcdef',3)", "abXYef", "OVERLAY at 3");
    EXPECT_OK("OVERLAY('XY','abc',5)", "abc XY", "OVERLAY past end");
    EXPECT_OK("OVERLAY('XY','abcdef',3,4,'.')", "abXY..", "OVERLAY len+pad");
    EXPECT_OK("DELSTR('abcdef',3)", "ab", "DELSTR from 3");
    EXPECT_OK("DELSTR('abcdef',3,2)", "abef", "DELSTR 3,2");
    EXPECT_OK("DELWORD('a b c d e',3)", "a b", "DELWORD from 3");
    EXPECT_OK("DELWORD('a b c d e',2,2)", "a d e", "DELWORD 2,2");
}

/* ================================================================== */
/*  Phase F — translation, verification, compare, abbrev, xrange, find*/
/* ================================================================== */

static void test_phase_f(void)
{
    printf("\n--- Phase F: translate / verify / compare / abbrev ---\n");
    EXPECT_OK("TRANSLATE('abc')", "ABC", "TRANSLATE uppercase");
    EXPECT_OK("TRANSLATE('abc','xyz','abc')", "xyz", "TRANSLATE map");
    EXPECT_OK("TRANSLATE('abc','XY','ab')", "XYc", "TRANSLATE partial");
    EXPECT_OK("TRANSLATE('abcd','*','bc','.')", "a*.d", "TRANSLATE short-output uses pad");
    EXPECT_OK("VERIFY('abc123','abcdefghijklmnopqrstuvwxyz')", "4",
              "VERIFY finds digit at 4");
    EXPECT_OK("VERIFY('abc','abc')", "0", "VERIFY all match");
    EXPECT_OK("VERIFY('abc123','0123456789','M')", "4", "VERIFY match mode");
    EXPECT_OK("COMPARE('abc','abc')", "0", "COMPARE equal");
    EXPECT_OK("COMPARE('abc','abd')", "3", "COMPARE diff pos");
    EXPECT_OK("COMPARE('ab  ','ab')", "0", "COMPARE blank-pad");
    EXPECT_OK("ABBREV('program','prog')", "1", "ABBREV prefix");
    EXPECT_OK("ABBREV('program','prog',5)", "0", "ABBREV too short");
    EXPECT_OK("ABBREV('program','xx')", "0", "ABBREV no");
    EXPECT_OK("FIND('foo bar baz','bar')", "2", "FIND single word");
    EXPECT_OK("FIND('a b c d e','c d')", "3", "FIND multi-word");
    EXPECT_OK("FIND('a b c','x')", "0", "FIND not found");
    EXPECT_OK("LENGTH(XRANGE('A','Z'))", "26", "XRANGE length 26");
    EXPECT_OK("LENGTH(XRANGE())", "256", "XRANGE default 256");
}

/* ================================================================== */
/*  Error paths — SYNTAX 40.x                                         */
/* ================================================================== */

static int run_expect_fail(const char *src, int want_code,
                           int want_subcode, const char *msg)
{
    struct fixture fx;
    int rc;
    int code = 0;
    int subcode = 0;

    if (fixture_open(&fx) != 0)
    {
        return 0;
    }
    rc = run_src(&fx, src);

    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)fx.env->envblock_userfield;
    if (wk != NULL && wk->wkbi_last_condition != NULL &&
        wk->wkbi_last_condition->valid)
    {
        code = wk->wkbi_last_condition->code;
        subcode = wk->wkbi_last_condition->subcode;
    }

    int ok = (rc != IRXPARS_OK) && (code == want_code) &&
             (want_subcode == 0 || subcode == want_subcode);
    if (!ok)
    {
        printf("    rc=%d code=%d subcode=%d (want rc!=0 code=%d subcode=%d)\n",
               rc, code, subcode, want_code, want_subcode);
    }
    CHECK(ok, msg);
    fixture_close(&fx);
    return ok;
}

static void test_error_paths(void)
{
    /* Every BIF that has a validation branch gets at least one       */
    /* error-path test here (SC28-1883-0 Appendix E, SYNTAX 40.x).    */
    /* BIFs whose only arg is a string (LENGTH, WORDS, REVERSE, FIND) */
    /* have no 40.x validation and are exercised only positively.     */
    printf("\n--- Error paths (SYNTAX 40.x) ---\n");

    /* Non-negative whole required */
    run_expect_fail("x = LEFT('hello','abc')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "LEFT non-numeric");
    run_expect_fail("x = LEFT('hello',-1)\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "LEFT negative");
    run_expect_fail("x = RIGHT('hello','abc')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "RIGHT non-numeric");
    run_expect_fail("x = CENTER('abc','oops')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "CENTER bad width");
    run_expect_fail("x = COPIES('ab','bad')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "COPIES bad count");
    run_expect_fail("x = JUSTIFY('a b','no')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "JUSTIFY bad width");

    /* Positive whole required */
    run_expect_fail("x = SUBSTR('abc',0)\n", SYNTAX_BAD_CALL,
                    ERR40_POSITIVE_WHOLE, "SUBSTR zero start");
    run_expect_fail("x = WORD('a b c',0)\n", SYNTAX_BAD_CALL,
                    ERR40_POSITIVE_WHOLE, "WORD zero index");
    run_expect_fail("x = WORDINDEX('a b',0)\n", SYNTAX_BAD_CALL,
                    ERR40_POSITIVE_WHOLE, "WORDINDEX zero");
    run_expect_fail("x = WORDLENGTH('a b',-2)\n", SYNTAX_BAD_CALL,
                    ERR40_POSITIVE_WHOLE, "WORDLENGTH negative");
    run_expect_fail("x = SUBWORD('a b c',0)\n", SYNTAX_BAD_CALL,
                    ERR40_POSITIVE_WHOLE, "SUBWORD zero");
    run_expect_fail("x = DELSTR('abc',0)\n", SYNTAX_BAD_CALL,
                    ERR40_POSITIVE_WHOLE, "DELSTR zero");
    run_expect_fail("x = DELWORD('a b c',0)\n", SYNTAX_BAD_CALL,
                    ERR40_POSITIVE_WHOLE, "DELWORD zero");

    /* Optional-whole validation path */
    run_expect_fail("x = POS('a','abc','bad')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "POS bad start");
    run_expect_fail("x = LASTPOS('a','abc','x')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "LASTPOS bad start");
    run_expect_fail("x = INDEX('abc','a','bad')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "INDEX bad start");
    run_expect_fail("x = WORDPOS('a','a b','bad')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "WORDPOS bad start");
    run_expect_fail("x = INSERT('X','abc','bad')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "INSERT bad pos");
    run_expect_fail("x = OVERLAY('X','abc','bad')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "OVERLAY bad pos");
    run_expect_fail("x = ABBREV('program','prog','bad')\n",
                    SYNTAX_BAD_CALL, ERR40_NONNEG_WHOLE,
                    "ABBREV bad min");

    /* Single-char validation (pad args) */
    run_expect_fail("x = SPACE('a b',1,'ab')\n", SYNTAX_BAD_CALL,
                    ERR40_SINGLE_CHAR, "SPACE bad pad");
    run_expect_fail("x = TRANSLATE('abc','x','y','ab')\n",
                    SYNTAX_BAD_CALL, ERR40_SINGLE_CHAR,
                    "TRANSLATE bad pad");
    run_expect_fail("x = COMPARE('a','b','ab')\n", SYNTAX_BAD_CALL,
                    ERR40_SINGLE_CHAR, "COMPARE bad pad");
    run_expect_fail("x = XRANGE('ab')\n", SYNTAX_BAD_CALL,
                    ERR40_SINGLE_CHAR, "XRANGE multi-char");

    /* Option validation */
    run_expect_fail("x = STRIP('abc','X')\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "STRIP bad option");
    run_expect_fail("x = VERIFY('abc','abc','X')\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "VERIFY bad option");
}

/* FIND has a hard implementation cap on the phrase argument. Exceeding
 * it must raise SYNTAX 40.4 (ERR40_ARG_LENGTH) rather than silently
 * returning 0 (which would look like a legitimate "not found"). */
static void test_find_phrase_cap(void)
{
    printf("\n--- FIND phrase-length cap ---\n");

    /* Build a REXX expression of the form
     *    x = FIND('a', 'w1 w2 w3 ... wN')
     * where N = 1050 > FIND_MAX_WORDS (1024). 5-byte tokens at most,
     * so a ~7 KB source buffer is sufficient. */
    const int num_words = 1050;
    size_t buflen = 64 + (size_t)num_words * 6;
    char *buf = (char *)malloc(buflen);
    if (buf == NULL)
    {
        CHECK(0, "malloc for FIND cap source");
        return;
    }
    size_t off = 0;
    off += (size_t)sprintf(buf + off, "x = FIND('a','");
    for (int i = 0; i < num_words; i++)
    {
        off += (size_t)sprintf(buf + off, "%sw%d",
                               (i == 0) ? "" : " ", i);
    }
    off += (size_t)sprintf(buf + off, "')\n");

    struct fixture fx;
    if (fixture_open(&fx) != 0)
    {
        free(buf);
        CHECK(0, "fixture_open FIND cap");
        return;
    }
    int rc = run_src(&fx, buf);
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)fx.env->envblock_userfield;
    int code = 0;
    int subcode = 0;
    if (wk != NULL && wk->wkbi_last_condition != NULL &&
        wk->wkbi_last_condition->valid)
    {
        code = wk->wkbi_last_condition->code;
        subcode = wk->wkbi_last_condition->subcode;
    }
    CHECK(rc != IRXPARS_OK, "FIND >cap: parser reports error");
    CHECK(code == SYNTAX_BAD_CALL,
          "FIND >cap: condition code is SYNTAX 40");
    CHECK(subcode == ERR40_ARG_LENGTH,
          "FIND >cap: subcode is ERR40_ARG_LENGTH (40.4)");
    fixture_close(&fx);
    free(buf);
}

int main(void)
{
    printf("=== WP-21a: String BIFs ===\n");
    test_phase_b();
    test_phase_c();
    test_phase_d();
    test_phase_e();
    test_phase_f();
    test_error_paths();
    test_find_phrase_cap();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
