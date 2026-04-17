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

/* ================================================================== */
/*  Phase C — Numeric BIFs (WP-21b #31)                               */
/*  AC-C1..AC-C7 from the issue body.                                 */
/* ================================================================== */

static void test_phase_c_numeric(void)
{
    printf("\n--- Phase C: numeric BIFs (MAX/MIN/ABS/SIGN/TRUNC/FORMAT/RANDOM) ---\n");
    /* AC-C1 */
    EXPECT_OK("MAX(1,2,3)", "3", "AC-C1 MAX triple");
    EXPECT_OK("MIN(-1,0,1)", "-1", "AC-C1 MIN triple");
    EXPECT_OK("MAX(7)", "7", "MAX single arg");
    EXPECT_OK("MIN(5,5,5)", "5", "MIN all equal");
    EXPECT_OK("MAX(-3,-1,-2)", "-1", "MAX of negatives");
    EXPECT_OK("MIN('1.5','1.4','1.6')", "1.4", "MIN of decimals");

    /* AC-C2 */
    EXPECT_OK("ABS(-5.5)", "5.5", "AC-C2 ABS negative decimal");
    EXPECT_OK("ABS(0)", "0", "ABS zero");
    EXPECT_OK("ABS(42)", "42", "ABS positive");
    EXPECT_OK("ABS('-0')", "0", "ABS minus zero");

    /* AC-C3 */
    EXPECT_OK("SIGN(-3)", "-1", "AC-C3 SIGN negative");
    EXPECT_OK("SIGN(0)", "0", "AC-C3 SIGN zero");
    EXPECT_OK("SIGN('4.2')", "1", "AC-C3 SIGN positive decimal");
    EXPECT_OK("SIGN('-0.0')", "0", "SIGN minus zero");
    EXPECT_OK("SIGN('+7')", "1", "SIGN explicit plus");

    /* AC-C4 */
    EXPECT_OK("TRUNC('12.345',2)", "12.34", "AC-C4 TRUNC two decimals");
    EXPECT_OK("TRUNC('12.345')", "12", "AC-C4 TRUNC default zero decimals");
    EXPECT_OK("TRUNC('-0.9')", "0", "TRUNC rounds toward zero");
    EXPECT_OK("TRUNC('12',3)", "12.000", "TRUNC pads trailing zeros");

    /* AC-C5 */
    EXPECT_OK("FORMAT('123.45',6,2)", "   123.45", "AC-C5 FORMAT before/after");
    EXPECT_OK("FORMAT('5')", "5", "FORMAT defaults");
    EXPECT_OK("FORMAT('5',4)", "   5", "FORMAT pad integer part");
    EXPECT_OK("FORMAT('1.2',,3)", "1.200", "FORMAT fractional pad");

    /* AC-C6 — seeded RANDOM is reproducible across two calls. */
    {
        struct fixture fx;
        if (fixture_open(&fx) != 0)
        {
            CHECK(0, "AC-C6 fixture_open");
        }
        else
        {
            int rc = run_src(&fx, "x = RANDOM(,,12345)\n");
            CHECK(rc == IRXPARS_OK, "AC-C6 RANDOM(,,seed) returns OK");
            CHECK(var_eq(&fx, "X", "0"),
                  "AC-C6 RANDOM(,,seed) result is '0'");
            rc = run_src(&fx, "y = RANDOM(1,10)\n");
            CHECK(rc == IRXPARS_OK, "AC-C6 RANDOM(1,10) OK after seed");

            Lstr key;
            Lstr val;
            Lzeroinit(&key);
            Lzeroinit(&val);
            Lscpy(fx.alloc, &key, "Y");
            int vpool_rc = vpool_get(fx.pool, &key, &val);
            int in_range = 0;
            if (vpool_rc == VPOOL_OK && val.len > 0)
            {
                long n = 0;
                size_t i;
                int parse_ok = 1;
                for (i = 0; i < val.len; i++)
                {
                    unsigned char c = val.pstr[i];
                    if (c < (unsigned char)'0' || c > (unsigned char)'9')
                    {
                        parse_ok = 0;
                        break;
                    }
                    n = n * 10 + (c - (unsigned char)'0');
                }
                in_range = parse_ok && (n >= 1 && n <= 10);
            }
            CHECK(in_range, "AC-C6 RANDOM(1,10) result in [1,10]");
            Lfree(fx.alloc, &key);
            Lfree(fx.alloc, &val);
            fixture_close(&fx);
        }
    }

    /* Seeded RANDOM is deterministic: same seed → same sequence across
     * two independent environments. Sequential fixture open/close so
     * that a late fixture_open failure never leaves an un-paired
     * fixture_close or a skipped Lfree. */
    {
        struct fixture fa;
        struct fixture fb;
        if (fixture_open(&fa) != 0)
        {
            CHECK(0, "RANDOM repro: fixture_open(fa)");
        }
        else if (fixture_open(&fb) != 0)
        {
            fixture_close(&fa);
            CHECK(0, "RANDOM repro: fixture_open(fb)");
        }
        else
        {
            /* max-min must be <= 100000 per RANDOM spec. */
            run_src(&fa,
                    "r = RANDOM(,,42)\n"
                    "x = RANDOM(0,100000)\n");
            run_src(&fb,
                    "r = RANDOM(,,42)\n"
                    "x = RANDOM(0,100000)\n");

            Lstr key;
            Lstr va;
            Lstr vb;
            Lzeroinit(&key);
            Lzeroinit(&va);
            Lzeroinit(&vb);
            Lscpy(fa.alloc, &key, "X");
            vpool_get(fa.pool, &key, &va);
            vpool_get(fb.pool, &key, &vb);
            int ok = (va.len == vb.len) && (va.len > 0) &&
                     (memcmp(va.pstr, vb.pstr, va.len) == 0);
            Lfree(fa.alloc, &key);
            Lfree(fa.alloc, &va);
            Lfree(fb.alloc, &vb);
            fixture_close(&fa);
            fixture_close(&fb);
            CHECK(ok, "RANDOM reproducible across seeded envs");
        }
    }

    /* Regression for the 15-bit output bug fixed by the two-step LCG.
     * A 15-bit LCG output would cap RANDOM(0, 100000) at 32767, so any
     * run of samples would stay well below 50000. With a 30-bit output
     * the sample distribution covers the full range. 100 draws from a
     * fixed seed is enough: the probability of 100 consecutive values
     * all below 50001 is (50001/100001)^100 ≈ 7.9e-31, which is zero
     * for practical purposes and makes the test a hard regression
     * guard rather than a flaky statistical check. */
    {
        struct fixture fx;
        if (fixture_open(&fx) != 0)
        {
            CHECK(0, "RANDOM wide-range: fixture_open");
        }
        else
        {
            run_src(&fx, "r = RANDOM(,,1)\n");
            int saw_high = 0;
            int k;
            for (k = 0; k < 100 && !saw_high; k++)
            {
                run_src(&fx, "x = RANDOM(0,100000)\n");
                Lstr key;
                Lstr val;
                Lzeroinit(&key);
                Lzeroinit(&val);
                Lscpy(fx.alloc, &key, "X");
                if (vpool_get(fx.pool, &key, &val) == VPOOL_OK &&
                    val.len > 0)
                {
                    long n = 0;
                    size_t i;
                    int parse_ok = 1;
                    for (i = 0; i < val.len; i++)
                    {
                        unsigned char c = val.pstr[i];
                        if (c < (unsigned char)'0' ||
                            c > (unsigned char)'9')
                        {
                            parse_ok = 0;
                            break;
                        }
                        n = n * 10 + (c - (unsigned char)'0');
                    }
                    if (parse_ok && n > 50000)
                    {
                        saw_high = 1;
                    }
                }
                Lfree(fx.alloc, &key);
                Lfree(fx.alloc, &val);
            }
            CHECK(saw_high,
                  "RANDOM(0,100000) reaches values > 50000 "
                  "(guards 15-bit truncation)");
            fixture_close(&fx);
        }
    }
}

/* AC-C7: MAX/MIN with a non-numeric operand raises SYNTAX 41.1. */
static void test_phase_c_nonnumeric(void)
{
    printf("\n--- Phase C: AC-C7 non-numeric operand ---\n");
    run_expect_fail("x = MAX('abc',1)\n", SYNTAX_BAD_ARITH,
                    ERR41_NONNUMERIC, "AC-C7 MAX non-numeric first arg");
    run_expect_fail("x = MAX(1,'abc')\n", SYNTAX_BAD_ARITH,
                    ERR41_NONNUMERIC, "MAX non-numeric second arg");
    run_expect_fail("x = MIN('x')\n", SYNTAX_BAD_ARITH,
                    ERR41_NONNUMERIC, "MIN non-numeric single arg");
    run_expect_fail("x = ABS('abc')\n", SYNTAX_BAD_ARITH,
                    ERR41_NONNUMERIC, "ABS non-numeric");
    run_expect_fail("x = SIGN('xyz')\n", SYNTAX_BAD_ARITH,
                    ERR41_NONNUMERIC, "SIGN non-numeric");
    run_expect_fail("x = TRUNC('abc')\n", SYNTAX_BAD_ARITH,
                    ERR41_NONNUMERIC, "TRUNC non-numeric value");
    run_expect_fail("x = FORMAT('abc')\n", SYNTAX_BAD_ARITH,
                    ERR41_NONNUMERIC, "FORMAT non-numeric value");

    /* Bad whole-number parameters still surface as SYNTAX 40.x. */
    run_expect_fail("x = TRUNC('12.3','bad')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "TRUNC bad decimals arg");
    run_expect_fail("x = FORMAT('12.3',,'bad')\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "FORMAT bad after arg");
    run_expect_fail("x = RANDOM(5,2)\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "RANDOM max < min");
    run_expect_fail("x = RANDOM(0,200000)\n", SYNTAX_BAD_CALL,
                    ERR40_ARG_LENGTH, "RANDOM range too large");

    /* TRUNC / FORMAT: integer arg exceeding NUMERIC DIGITS_MAX (1000)
     * surfaces as 40.4, not 41.1. Hygiene check for the error-code
     * split between argument-shape and bad-arithmetic failures. */
    run_expect_fail("x = TRUNC('1.5',5000)\n", SYNTAX_BAD_CALL,
                    ERR40_ARG_LENGTH, "TRUNC decimals > DIGITS_MAX");
    run_expect_fail("x = FORMAT('1.5',,5000)\n", SYNTAX_BAD_CALL,
                    ERR40_ARG_LENGTH, "FORMAT after > DIGITS_MAX");
}

/* Boundary / edge-case coverage requested in review #9. */
static void test_phase_c_edges(void)
{
    printf("\n--- Phase C: boundary and edge cases ---\n");

    /* SIGN accepts whitespace and exponential notation. */
    EXPECT_OK("SIGN('0.0E5')", "0", "SIGN exponential zero");
    EXPECT_OK("SIGN('   0   ')", "0", "SIGN whitespace around zero");
    EXPECT_OK("SIGN(' 42 ')", "1", "SIGN whitespace around value");

    /* FORMAT exponential output uses expp-zero-padded exponent. */
    EXPECT_OK("FORMAT('1E10',,,3,6)", "1E+010",
              "FORMAT explicit exponential");

    /* RANDOM(0,0) collapses to a single value. */
    EXPECT_OK("RANDOM(0,0)", "0", "RANDOM zero-width range");

    /* RANDOM(min) default max=999; result must land in [5,999]. */
    {
        struct fixture fx;
        if (fixture_open(&fx) != 0)
        {
            CHECK(0, "RANDOM(min): fixture_open");
        }
        else
        {
            int rc = run_src(&fx, "r = RANDOM(,,7)\nx = RANDOM(5)\n");
            CHECK(rc == IRXPARS_OK, "RANDOM(5) with seed OK");
            Lstr key;
            Lstr val;
            Lzeroinit(&key);
            Lzeroinit(&val);
            Lscpy(fx.alloc, &key, "X");
            int in_range = 0;
            if (vpool_get(fx.pool, &key, &val) == VPOOL_OK && val.len > 0)
            {
                long n = 0;
                size_t i;
                int parse_ok = 1;
                for (i = 0; i < val.len; i++)
                {
                    unsigned char c = val.pstr[i];
                    if (c < (unsigned char)'0' || c > (unsigned char)'9')
                    {
                        parse_ok = 0;
                        break;
                    }
                    n = n * 10 + (c - (unsigned char)'0');
                }
                in_range = parse_ok && (n >= 5 && n <= 999);
            }
            CHECK(in_range, "RANDOM(5) result in [5,999]");
            Lfree(fx.alloc, &key);
            Lfree(fx.alloc, &val);
            fixture_close(&fx);
        }
    }

    /* MAX/MIN at exactly IRX_MAX_ARGS (16) — dispatcher accepts. */
    EXPECT_OK("MAX(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16)", "16",
              "MAX at IRX_MAX_ARGS boundary");
    EXPECT_OK("MIN(16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1)", "1",
              "MIN at IRX_MAX_ARGS boundary");

    /* MAX() — dispatcher rejects argc < min_args before the BIF runs;
     * no condition is raised so want_code=0 asserts "failure with no
     * condition set". */
    run_expect_fail("x = MAX()\n", 0, 0,
                    "MAX() rejected by dispatcher min_args");

    /* One argument over IRX_MAX_ARGS — parser refuses. */
    run_expect_fail("x = MAX(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17)\n",
                    0, 0, "MAX(17 args) rejected by parser");
}

/* ================================================================== */
/*  Phase D — Conversion BIFs (WP-21b #32)                            */
/*  AC-D1..AC-D11. Charset-sensitive expected values are wrapped with */
/*  C2X(...) on the REXX side so assertions remain byte-exact on both */
/*  ASCII (cross-compile) and EBCDIC (future MVS build).              */
/*                                                                    */
/*  Bytes for test inputs are built via X2C('...'). REXX '41'x hex    */
/*  literal syntax is tokenized but not yet decoded by the parser     */
/*  (see irx#pars.c:3743 — deferred from WP-13), so '41'x currently   */
/*  surfaces as the raw 2-char string "41". Tests that want a byte    */
/*  value of 0x41 must go through X2C('41') instead.                  */
/* ================================================================== */

/* Like EXPECT_OK, but wraps the source with `numeric digits 20` so    */
/* results longer than the 9-digit default don't get rounded. Used for */
/* BCD-path tests whose expected value has 10+ decimal digits.         */
#define EXPECT_OK_BIG(src, want, msg)                                  \
    do                                                                 \
    {                                                                  \
        struct fixture fx;                                             \
        if (fixture_open(&fx) != 0)                                    \
        {                                                              \
            CHECK(0, "fixture_open " msg);                             \
            break;                                                     \
        }                                                              \
        int _rc = run_src(&fx, "numeric digits 20\nx = " src "\n");    \
        if (_rc != IRXPARS_OK)                                         \
        {                                                              \
            printf("    parser rc=%d\n", _rc);                         \
            CHECK(0, msg);                                             \
        }                                                              \
        else                                                           \
        {                                                              \
            CHECK(var_eq(&fx, "X", want), msg);                        \
        }                                                              \
        fixture_close(&fx);                                            \
    } while (0)

static void test_phase_d_byte_conv(void)
{
    printf("\n--- Phase D: byte-based conversions (C2X/X2C/B2X/X2B) ---\n");

    /* C2X — pure byte → hex. Uppercase always. */
    EXPECT_OK("C2X('')", "", "C2X empty");
    EXPECT_OK("C2X(X2C('41'))", "41", "C2X single byte");
    EXPECT_OK("C2X(X2C('4142'))", "4142", "C2X two bytes");
    EXPECT_OK("C2X(X2C('ff'))", "FF", "AC-D2 C2X uppercase output");
    EXPECT_OK("C2X(X2C('00'))", "00", "C2X NUL byte");

    /* X2C — hex → bytes. Verified via C2X wrapper (charset-neutral). */
    EXPECT_OK("C2X(X2C(''))", "", "X2C empty round-trip");
    EXPECT_OK("C2X(X2C('41'))", "41", "X2C basic");
    EXPECT_OK("C2X(X2C('fF'))", "FF", "X2C mixed-case input");
    EXPECT_OK("C2X(X2C('4 1 4 2'))", "4142", "X2C strips blanks");
    EXPECT_OK("C2X(X2C('F'))", "0F", "X2C odd length left-pads '0'");
    EXPECT_OK("C2X(X2C('ABC'))", "0ABC", "X2C 3 digits → 4 digits padded");

    /* B2X — binary → hex. */
    EXPECT_OK("B2X('')", "", "B2X empty");
    EXPECT_OK("B2X('11111111')", "FF", "AC-D5 B2X byte FF");
    EXPECT_OK("B2X('10101010')", "AA", "B2X byte AA");
    EXPECT_OK("B2X('1')", "1", "B2X single bit pads left");
    EXPECT_OK("B2X('1010')", "A", "B2X nibble");
    EXPECT_OK("B2X('1 1010 0101')", "1A5", "B2X blanks + 9 bits");

    /* X2B — hex → binary. */
    EXPECT_OK("X2B('')", "", "X2B empty");
    EXPECT_OK("X2B('FF')", "11111111", "AC-D5 X2B FF");
    EXPECT_OK("X2B('AA')", "10101010", "X2B AA");
    EXPECT_OK("X2B('0')", "0000", "X2B single 0 digit");
    EXPECT_OK("X2B('a b')", "10101011", "X2B blanks + lowercase");
    EXPECT_OK("X2B('DEADBEEF')", "11011110101011011011111011101111",
              "X2B 8 digits");

    /* Byte-conversion round-trips. */
    EXPECT_OK("X2B(B2X('10101010'))", "10101010", "RT X2B(B2X) 8 bits");
    EXPECT_OK("X2B(B2X('1'))", "0001", "RT X2B(B2X) left-pads to 4");
    EXPECT_OK("X2B(B2X('101'))", "0101", "RT X2B(B2X) 3 bits → 0101");
    EXPECT_OK("C2X(X2C('deadbeef'))", "DEADBEEF",
              "RT C2X(X2C) uppercases");
    EXPECT_OK("C2X(X2C('00112233'))", "00112233",
              "RT C2X(X2C) preserves zeros");
}

static void test_phase_d_c2d_x2d(void)
{
    printf("\n--- Phase D: C2D / X2D ---\n");

    /* AC-D1 — C2D without n: unsigned big-endian interpretation. */
    EXPECT_OK("C2D('')", "0", "C2D empty → 0");
    EXPECT_OK("C2D(X2C('00'))", "0", "C2D one zero byte");
    EXPECT_OK("C2D(X2C('01'))", "1", "C2D single byte 01");
    EXPECT_OK("C2D(X2C('FF'))", "255", "C2D single byte FF");
    EXPECT_OK("C2D(X2C('FFFF'))", "65535", "C2D 2 bytes unsigned");
    EXPECT_OK("C2D(X2C('FFFFFF'))", "16777215", "C2D 3 bytes unsigned");
    EXPECT_OK("C2D(X2C('01000000'))", "16777216", "C2D 4 bytes = 2^24");

    /* C2D with n — two's complement. */
    EXPECT_OK("C2D('',0)", "0", "C2D empty n=0");
    EXPECT_OK("C2D(X2C('FF'),0)", "0", "C2D nonempty n=0 → 0");
    EXPECT_OK("C2D(X2C('81'),1)", "-127", "AC-D10 C2D 81 n=1 → -127");
    EXPECT_OK("C2D(X2C('81'),2)", "129", "C2D 81 n=2 (pad left) → 129");
    EXPECT_OK("C2D(X2C('FF'),1)", "-1", "C2D FF n=1 → -1");
    EXPECT_OK("C2D(X2C('FF'),2)", "255", "C2D FF n=2 → 255 (pad)");
    EXPECT_OK("C2D(X2C('7F'),1)", "127", "AC-D10 MSB-off boundary");
    EXPECT_OK("C2D(X2C('80'),1)", "-128", "AC-D10 MSB-on boundary");
    EXPECT_OK("C2D(X2C('FFFF'),1)", "-1", "C2D FFFF n=1 truncates left");
    EXPECT_OK("C2D(X2C('00FF'),4)", "255", "C2D 00FF n=4 pads left");

    /* X2D without n. */
    EXPECT_OK("X2D('')", "0", "X2D empty → 0");
    EXPECT_OK("X2D('0')", "0", "X2D single '0'");
    EXPECT_OK("X2D('FF')", "255", "X2D FF unsigned");
    EXPECT_OK("X2D('ff')", "255", "X2D lowercase");
    EXPECT_OK("X2D('F F')", "255", "AC-D8 X2D blanks tolerated");
    EXPECT_OK("X2D('100')", "256", "X2D 100 → 256");

    /* X2D with n — n counts HEX DIGITS, not bytes. */
    EXPECT_OK("X2D('81',2)", "-127", "X2D 81 n=2 → -127");
    EXPECT_OK("X2D('81',3)", "129", "X2D 81 n=3 pad → 129");
    EXPECT_OK("X2D('81',4)", "129", "X2D 81 n=4 pad → 129");
    EXPECT_OK("X2D('7F',2)", "127", "X2D 7F n=2 MSB off");
    EXPECT_OK("X2D('80',2)", "-128", "X2D 80 n=2 MSB on");
    EXPECT_OK("X2D('F',1)", "-1", "X2D F n=1 single-nibble signed");
    EXPECT_OK("X2D('7',1)", "7", "X2D 7 n=1 single-nibble positive");
    EXPECT_OK("X2D('8',1)", "-8", "X2D 8 n=1 → -8");
    EXPECT_OK("X2D('FFFF',2)", "-1", "X2D truncated to n=2 → -1");
    EXPECT_OK("X2D('0080',3)", "128", "X2D 0080 n=3 truncates → 080 = 128");

    /* Odd-nibble two's-complement round-trip sanity. */
    EXPECT_OK("X2D('FFF',3)", "-1", "X2D FFF n=3 (12-bit -1)");
    EXPECT_OK("X2D('800',3)", "-2048", "X2D 800 n=3 (12-bit min)");
    EXPECT_OK("X2D('7FF',3)", "2047", "X2D 7FF n=3 (12-bit max)");
}

static void test_phase_d_d2c_d2x(void)
{
    printf("\n--- Phase D: D2C / D2X ---\n");

    /* AC-D3 charset-independent byte checks via C2X wrapper. */
    EXPECT_OK("D2C(0)", "", "D2C(0) → empty");
    EXPECT_OK("C2X(D2C(0,0))", "", "D2C(0,0) → empty");
    EXPECT_OK("C2X(D2C(0,1))", "00", "D2C(0,1) → 00x");
    EXPECT_OK("C2X(D2C(1))", "01", "D2C(1) → 01x");
    EXPECT_OK("C2X(D2C(255))", "FF", "D2C(255) → FFx");
    EXPECT_OK("C2X(D2C(256))", "0100", "D2C(256) → 0100x");
    EXPECT_OK("C2X(D2C(65))", "41", "AC-D3 D2C(65) → 0x41 byte");
    EXPECT_OK("C2X(D2C(193))", "C1", "AC-D3 D2C(193) → 0xC1 byte");

    /* D2C with length — padding and truncation. */
    EXPECT_OK("C2X(D2C(1,4))", "00000001", "D2C(1,4) left-pads 00s");
    EXPECT_OK("C2X(D2C(256,1))", "00", "D2C(256,1) truncates to low byte");
    EXPECT_OK("C2X(D2C(256,2))", "0100", "D2C(256,2) fits exactly");

    /* D2C negative — two's complement. */
    EXPECT_OK("C2X(D2C(-1,1))", "FF", "D2C(-1,1) → FFx");
    EXPECT_OK("C2X(D2C(-1,2))", "FFFF", "D2C(-1,2) → FFFFx");
    EXPECT_OK("C2X(D2C(-1,4))", "FFFFFFFF", "D2C(-1,4) → FFFFFFFFx");
    EXPECT_OK("C2X(D2C(-128,1))", "80", "D2C(-128,1) → 80x");
    EXPECT_OK("C2X(D2C(-256,2))", "FF00", "D2C(-256,2) → FF00x");
    EXPECT_OK("C2X(D2C(-257,2))", "FEFF", "D2C(-257,2) → FEFFx");

    /* AC-D4 — D2X / X2D basic. */
    EXPECT_OK("D2X(0)", "0", "AC-D4 D2X(0) → '0'");
    EXPECT_OK("D2X(0,0)", "", "D2X(0,0) → empty");
    EXPECT_OK("D2X(0,4)", "0000", "D2X(0,4) → 0000");
    EXPECT_OK("D2X(1)", "1", "D2X(1) no leading zero");
    EXPECT_OK("D2X(15)", "F", "D2X(15) single digit");
    EXPECT_OK("D2X(16)", "10", "D2X(16) → 10");
    EXPECT_OK("D2X(255)", "FF", "AC-D4 D2X(255) → FF");
    EXPECT_OK("D2X(256)", "100", "D2X(256) → 100");
    EXPECT_OK("D2X(255,4)", "00FF", "D2X(255,4) pad");
    EXPECT_OK("D2X(-1,2)", "FF", "D2X(-1,2) → FF");
    EXPECT_OK("D2X(-1,3)", "FFF", "D2X(-1,3) → FFF (odd length)");
    EXPECT_OK("D2X(-1,4)", "FFFF", "D2X(-1,4) → FFFF");
    EXPECT_OK("D2X(-256,3)", "F00", "D2X(-256,3) → F00 (odd, MSB nibble)");

    /* Whole-number inputs expressed as decimals/scientific still work. */
    EXPECT_OK("C2X(D2C('1E2'))", "64", "D2C scientific → 100 → 0x64");
    EXPECT_OK("C2X(D2C('1.0'))", "01", "D2C '1.0' whole");
}

static void test_phase_d_roundtrips(void)
{
    printf("\n--- Phase D: round-trips (AC-D4/D10) ---\n");

    /* X2D(D2X(n)) = n for a spectrum of values including BCD-path ones. */
    EXPECT_OK("X2D(D2X(0))", "0", "RT X2D∘D2X 0");
    EXPECT_OK("X2D(D2X(1))", "1", "RT X2D∘D2X 1");
    EXPECT_OK("X2D(D2X(127))", "127", "RT X2D∘D2X 127");
    EXPECT_OK("X2D(D2X(128))", "128", "RT X2D∘D2X 128");
    EXPECT_OK("X2D(D2X(255))", "255", "RT X2D∘D2X 255");
    EXPECT_OK("X2D(D2X(256))", "256", "RT X2D∘D2X 256");
    EXPECT_OK("X2D(D2X(65535))", "65535", "RT X2D∘D2X 65535");
    EXPECT_OK_BIG("X2D(D2X(1073741824))", "1073741824", "RT X2D∘D2X 2^30");

    /* Two's-complement C2D(D2C(n,k),k) = n. */
    EXPECT_OK("C2D(D2C(-1,1),1)", "-1", "RT 2c -1 @1B");
    EXPECT_OK("C2D(D2C(1,1),1)", "1", "RT 2c 1 @1B");
    EXPECT_OK("C2D(D2C(127,1),1)", "127", "RT 2c 127 @1B");
    EXPECT_OK("C2D(D2C(-128,1),1)", "-128", "RT 2c -128 @1B");
    EXPECT_OK("C2D(D2C(-1,2),2)", "-1", "RT 2c -1 @2B");
    EXPECT_OK("C2D(D2C(-32768,2),2)", "-32768", "RT 2c int16 min");
    EXPECT_OK("C2D(D2C(32767,2),2)", "32767", "RT 2c int16 max");
    EXPECT_OK("C2D(D2C(-1,4),4)", "-1", "RT 2c -1 @4B");
    EXPECT_OK_BIG("C2D(D2C(2147483647,4),4)", "2147483647",
                  "RT 2c int32 max");
    EXPECT_OK_BIG("C2D(D2C(-2147483648,4),4)", "-2147483648",
                  "RT 2c int32 min");
}

static void test_phase_d_bcd_path(void)
{
    printf("\n--- Phase D: BCD path (AC-D9) ---\n");

    /* Values produced here exceed the default 9-digit NUMERIC DIGITS, so */
    /* every assertion uses EXPECT_OK_BIG which sets DIGITS 20 up-front.  */

    /* C2D with 5-byte input routes through irx_arith_from_digits.      */
    /* Construct input via X2C so the test is byte-exact regardless of */
    /* host code page.                                                 */
    EXPECT_OK_BIG("C2D(X2C('0100000000'))", "4294967296",
                  "AC-D9 C2D 5 bytes → 2^32");
    EXPECT_OK_BIG("C2D(X2C('FF00000000'))", "1095216660480",
                  "AC-D9 C2D 5 bytes 0xFF00000000");

    /* Unsigned 4-byte with MSB set: signed `long` on MVS overflows;   */
    /* the 4-byte BCD path covers this too.                            */
    EXPECT_OK_BIG("C2D(X2C('FFFFFFFF'))", "4294967295",
                  "AC-D9 C2D 4 bytes FFFFFFFF (unsigned overflow) → BCD");
    EXPECT_OK_BIG("C2D(X2C('80000000'))", "2147483648",
                  "C2D 4 bytes 0x80000000 unsigned → BCD");

    /* X2D 9+ hex digits exceeds 32-bit unsigned — BCD path. */
    EXPECT_OK_BIG("X2D('100000000')", "4294967296",
                  "AC-D9 X2D 9 digits → 2^32");
    EXPECT_OK_BIG("X2D('FFFFFFFF')", "4294967295",
                  "X2D 8 digits FFFFFFFF unsigned overflow → BCD");
    EXPECT_OK_BIG("X2D('1234567890')", "78187493520",
                  "AC-D9 X2D 10 digits");

    /* D2C(2^32) → 5-byte output via BCD (irx_arith_to_digits path). */
    EXPECT_OK_BIG("C2X(D2C(4294967296))", "0100000000",
                  "AC-D9 D2C(2^32) → 0100000000x");
    EXPECT_OK_BIG("C2X(D2C(4294967296,5))", "0100000000",
                  "D2C(2^32,5) BCD fixed-width");
    EXPECT_OK_BIG("C2X(D2C(4294967296,4))", "00000000",
                  "D2C(2^32,4) truncates to 0");

    /* Two's-complement in BCD width. */
    EXPECT_OK_BIG("C2X(D2C(-1,5))", "FFFFFFFFFF",
                  "D2C(-1,5) BCD two's complement");

    /* D2C/C2D symmetric BCD round-trip. */
    EXPECT_OK_BIG("C2D(D2C(4294967296,5),5)", "4294967296",
                  "RT BCD 5-byte 2^32");
    EXPECT_OK_BIG("C2D(D2C(-4294967296,5),5)", "-4294967296",
                  "RT BCD 5-byte negative 2^32");

    /* D2X BCD. */
    EXPECT_OK_BIG("D2X(4294967296)", "100000000",
                  "D2X(2^32) BCD → 9 digits");
    EXPECT_OK_BIG("D2X(4294967296,12)", "000100000000",
                  "D2X(2^32,12) left-pads");
}

static void test_phase_d_errors(void)
{
    printf("\n--- Phase D: error paths ---\n");

    /* AC-D7 — X2C / X2D / X2B non-hex → SYNTAX 40.25. */
    run_expect_fail("x = X2C('GG')\n", SYNTAX_BAD_CALL, ERR40_BAD_HEX,
                    "X2C non-hex → 40.25");
    run_expect_fail("x = X2D('GG')\n", SYNTAX_BAD_CALL, ERR40_BAD_HEX,
                    "AC-D7 X2D non-hex → 40.25");
    run_expect_fail("x = X2B('GG')\n", SYNTAX_BAD_CALL, ERR40_BAD_HEX,
                    "X2B non-hex → 40.25");

    /* B2X non-binary → SYNTAX 40.24. */
    run_expect_fail("x = B2X('12')\n", SYNTAX_BAD_CALL, ERR40_BAD_BINARY,
                    "B2X contains '2' → 40.24");
    run_expect_fail("x = B2X('10102')\n", SYNTAX_BAD_CALL,
                    ERR40_BAD_BINARY, "B2X bad digit mid-string");

    /* D2C / D2X negative without length → 40.11. */
    run_expect_fail("x = D2C(-1)\n", SYNTAX_BAD_CALL, ERR40_NONNEG_WHOLE,
                    "D2C(-1) no length");
    run_expect_fail("x = D2X(-1)\n", SYNTAX_BAD_CALL, ERR40_NONNEG_WHOLE,
                    "D2X(-1) no length");

    /* D2C / D2X fractional → 40.5 (whole-number required). */
    run_expect_fail("x = D2C('1.5')\n", SYNTAX_BAD_CALL,
                    ERR40_WHOLE_NUMBER, "D2C fractional");
    run_expect_fail("x = D2X('3.14')\n", SYNTAX_BAD_CALL,
                    ERR40_WHOLE_NUMBER, "D2X fractional");

    /* D2C / D2X non-numeric → 41.1. */
    run_expect_fail("x = D2C('abc')\n", SYNTAX_BAD_ARITH,
                    ERR41_NONNUMERIC, "D2C non-numeric");
    run_expect_fail("x = D2X('xy')\n", SYNTAX_BAD_ARITH,
                    ERR41_NONNUMERIC, "D2X non-numeric");

    /* C2D / X2D negative n → 40.11 from irx_bif_whole_nonneg. */
    run_expect_fail("x = C2D('abc',-1)\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "C2D negative n");
    run_expect_fail("x = X2D('41',-1)\n", SYNTAX_BAD_CALL,
                    ERR40_NONNEG_WHOLE, "X2D negative n");

    /* D2C / D2X negative length → 40.11. */
    run_expect_fail("x = D2C(5,-1)\n", SYNTAX_BAD_CALL, ERR40_NONNEG_WHOLE,
                    "D2C negative length");
    run_expect_fail("x = D2X(5,-2)\n", SYNTAX_BAD_CALL, ERR40_NONNEG_WHOLE,
                    "D2X negative length");
}

int main(void)
{
    printf("=== WP-21a + WP-21b Phase C+D: BIFs ===\n");
    test_phase_b();
    test_phase_c();
    test_phase_d();
    test_phase_e();
    test_phase_f();
    test_phase_c_numeric();
    test_phase_c_nonnumeric();
    test_phase_c_edges();
    test_phase_d_byte_conv();
    test_phase_d_c2d_x2d();
    test_phase_d_d2c_d2x();
    test_phase_d_roundtrips();
    test_phase_d_bcd_path();
    test_phase_d_errors();
    test_error_paths();
    test_find_phrase_cap();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
