/* ------------------------------------------------------------------ */
/*  tsttim.c - WP-CPS-01 TIME/DATE BIF unit tests                    */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o /tmp/tsttim \                   */
/*        test/mvs/tsttim.c \                                         */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \       */
/*        'src/irx#anch.c' 'src/irx#env.c'  'src/irx#uid.c' \        */
/*        'src/irx#msid.c' 'src/irx#cond.c' 'src/irx#bif.c' \        */
/*        'src/irx#bifs.c' 'src/irx#io.c'   'src/irx#lstr.c' \       */
/*        'src/irx#tokn.c' 'src/irx#vpol.c' 'src/irx#pars.c' \       */
/*        'src/irx#ctrl.c' 'src/irx#exec.c' 'src/irx#arith.c' \      */
/*        /path/to/liblstring.a                                        */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VAR_BUF_LEN    32
#define STRTOL_DECIMAL 10

#include "irx.h"
#include "irxbif.h"
#include "irxcond.h"
#include "irxfunc.h"
#include "irxlstr.h"
#include "irxpars.h"
#include "irxtokn.h"
#include "irxvpool.h"
#include "irxwkblk.h"
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
/*  Fixture                                                            */
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
    f->alloc = irx_lstr_init(f->env);
    if (f->alloc == NULL)
    {
        irxterm(f->env);
        f->env = NULL;
        return -1;
    }
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

/* Return the stored length of variable `name`, or -1 if not found. */
static int var_len(struct fixture *f, const char *name)
{
    Lstr key;
    Lstr val;

    Lzeroinit(&key);
    Lzeroinit(&val);
    Lscpy(f->alloc, &key, name);

    int rc = vpool_get(f->pool, &key, &val);
    int len = (rc == VPOOL_OK) ? (int)val.len : -1;

    Lfree(f->alloc, &key);
    Lfree(f->alloc, &val);
    return len;
}

/* Return the character at 0-based `pos`, or '\0' on miss. */
static int var_char_at(struct fixture *f, const char *name, int pos)
{
    Lstr key;
    Lstr val;
    int c = '\0';

    Lzeroinit(&key);
    Lzeroinit(&val);
    Lscpy(f->alloc, &key, name);

    int rc = vpool_get(f->pool, &key, &val);
    if (rc == VPOOL_OK && pos >= 0 && pos < (int)val.len)
    {
        c = (int)(unsigned char)val.pstr[pos];
    }
    Lfree(f->alloc, &key);
    Lfree(f->alloc, &val);
    return c;
}

/* Return 1 if variable starts with `prefix`, 0 otherwise. */
static int var_starts_with(struct fixture *f, const char *name,
                           const char *prefix)
{
    Lstr key;
    Lstr val;
    int ok = 0;

    Lzeroinit(&key);
    Lzeroinit(&val);
    Lscpy(f->alloc, &key, name);

    int rc = vpool_get(f->pool, &key, &val);
    if (rc == VPOOL_OK)
    {
        size_t plen = strlen(prefix);
        ok = (val.len >= plen) &&
             (memcmp(val.pstr, prefix, plen) == 0);
        if (!ok)
        {
            printf("    %s = '%.*s' (expected prefix '%s')\n",
                   name, (int)val.len, (const char *)val.pstr, prefix);
        }
    }
    Lfree(f->alloc, &key);
    Lfree(f->alloc, &val);
    return ok;
}

/* Return 1 if every character of variable `name` satisfies isdigit. */
static int var_all_digits(struct fixture *f, const char *name)
{
    Lstr key;
    Lstr val;
    int ok = 0;

    Lzeroinit(&key);
    Lzeroinit(&val);
    Lscpy(f->alloc, &key, name);

    int rc = vpool_get(f->pool, &key, &val);
    if (rc == VPOOL_OK && val.len > 0)
    {
        ok = 1;
        for (size_t i = 0; i < val.len; i++)
        {
            if (!isdigit((unsigned char)val.pstr[i]))
            {
                ok = 0;
                break;
            }
        }
        if (!ok)
        {
            printf("    %s = '%.*s' (expected all-digits)\n",
                   name, (int)val.len, (const char *)val.pstr);
        }
    }
    Lfree(f->alloc, &key);
    Lfree(f->alloc, &val);
    return ok;
}

/* Return 1 if every character of variable `name` satisfies isalpha. */
static int var_all_alpha(struct fixture *f, const char *name)
{
    Lstr key;
    Lstr val;
    int ok = 0;

    Lzeroinit(&key);
    Lzeroinit(&val);
    Lscpy(f->alloc, &key, name);

    int rc = vpool_get(f->pool, &key, &val);
    if (rc == VPOOL_OK && val.len > 0)
    {
        ok = 1;
        for (size_t i = 0; i < val.len; i++)
        {
            if (!isalpha((unsigned char)val.pstr[i]))
            {
                ok = 0;
                break;
            }
        }
        if (!ok)
        {
            printf("    %s = '%.*s' (expected all-alpha)\n",
                   name, (int)val.len, (const char *)val.pstr);
        }
    }
    Lfree(f->alloc, &key);
    Lfree(f->alloc, &val);
    return ok;
}

/* Return 1 if variable ends with `suffix`. */
static int var_ends_with(struct fixture *f, const char *name,
                         const char *suffix)
{
    Lstr key;
    Lstr val;
    int ok = 0;

    Lzeroinit(&key);
    Lzeroinit(&val);
    Lscpy(f->alloc, &key, name);

    int rc = vpool_get(f->pool, &key, &val);
    if (rc == VPOOL_OK)
    {
        size_t slen = strlen(suffix);
        ok = (val.len >= slen) &&
             (memcmp(val.pstr + val.len - slen, suffix, slen) == 0);
        if (!ok)
        {
            printf("    %s = '%.*s' (expected suffix '%s')\n",
                   name, (int)val.len, (const char *)val.pstr, suffix);
        }
    }
    Lfree(f->alloc, &key);
    Lfree(f->alloc, &val);
    return ok;
}

/* Parse variable as long integer; return 1 if in [lo, hi]. */
static int var_in_range(struct fixture *f, const char *name, long lo, long hi)
{
    Lstr key;
    Lstr val;
    int ok = 0;

    Lzeroinit(&key);
    Lzeroinit(&val);
    Lscpy(f->alloc, &key, name);

    int rc = vpool_get(f->pool, &key, &val);
    if (rc == VPOOL_OK && val.len > 0)
    {
        char buf[VAR_BUF_LEN];
        size_t copy = (val.len < sizeof(buf) - 1) ? val.len : sizeof(buf) - 1;
        memcpy(buf, val.pstr, copy);
        buf[copy] = '\0';
        char *end = NULL;
        errno = 0;
        long v = strtol(buf, &end, STRTOL_DECIMAL);
        ok = (v >= lo && v <= hi);
        if (!ok)
        {
            printf("    %s = %ld (expected [%ld, %ld])\n",
                   name, v, lo, hi);
        }
    }
    Lfree(f->alloc, &key);
    Lfree(f->alloc, &val);
    return ok;
}

/* Check that running `src` raises a SYNTAX condition with want_code /
 * want_subcode. want_subcode == 0 means "don't check subcode". */
static int run_expect_fail(const char *src, int want_code,
                           int want_subcode, const char *msg)
{
    struct fixture fx;
    if (fixture_open(&fx) != 0)
    {
        return 0;
    }
    int rc = run_src(&fx, src);

    int code = 0;
    int subcode = 0;
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
        printf("    rc=%d code=%d subcode=%d (want rc!=0 code=%d sub=%d)\n",
               rc, code, subcode, want_code, want_subcode);
    }
    CHECK(ok, msg);
    fixture_close(&fx);
    return ok;
}

/* Open fixture, run "x = EXPR", then execute CHECKS block accessing fx. */
#define WITH_RESULT(expr, checks)                              \
    do                                                         \
    {                                                          \
        struct fixture fx;                                     \
        if (fixture_open(&fx) != 0)                            \
        {                                                      \
            CHECK(0, "fixture_open: " expr);                   \
            break;                                             \
        }                                                      \
        int _rc = run_src(&fx, "x = " expr "\n");              \
        if (_rc != IRXPARS_OK)                                 \
        {                                                      \
            printf("    parser rc=%d for: %s\n", _rc, (expr)); \
            CHECK(0, "parse ok: " expr);                       \
        }                                                      \
        else                                                   \
        {                                                      \
            checks;                                            \
        }                                                      \
        fixture_close(&fx);                                    \
    } while (0)

/* ================================================================== */
/*  TIME tests                                                         */
/* ================================================================== */

static void test_time_normal(void)
{
    printf("\n--- TIME: N (normal HH:MM:SS) ---\n");

    WITH_RESULT("TIME()", {
        CHECK(var_len(&fx, "X") == 8, "TIME() length 8");
        CHECK(var_char_at(&fx, "X", 2) == ':', "TIME() colon at 2");
        CHECK(var_char_at(&fx, "X", 5) == ':', "TIME() colon at 5");
        CHECK(isdigit(var_char_at(&fx, "X", 0)), "TIME() digit 0");
        CHECK(isdigit(var_char_at(&fx, "X", 1)), "TIME() digit 1");
        CHECK(isdigit(var_char_at(&fx, "X", 3)), "TIME() digit 3");
        CHECK(isdigit(var_char_at(&fx, "X", 4)), "TIME() digit 4");
        CHECK(isdigit(var_char_at(&fx, "X", 6)), "TIME() digit 6");
        CHECK(isdigit(var_char_at(&fx, "X", 7)), "TIME() digit 7");
    });

    WITH_RESULT("TIME('N')", {
        CHECK(var_len(&fx, "X") == 8, "TIME('N') length 8");
        CHECK(var_char_at(&fx, "X", 2) == ':', "TIME('N') colon at 2");
        CHECK(var_char_at(&fx, "X", 5) == ':', "TIME('N') colon at 5");
    });

    /* Case insensitivity: first char only, lower case */
    WITH_RESULT("TIME('n')", {
        CHECK(var_len(&fx, "X") == 8, "TIME('n') length 8");
        CHECK(var_char_at(&fx, "X", 2) == ':', "TIME('n') colon at 2");
    });
}

static void test_time_elapsed(void)
{
    printf("\n--- TIME: E (elapsed) ---\n");

    /* First call in a fresh env — elapsed should be < 1 second. */
    WITH_RESULT("TIME('E')", {
        int len = var_len(&fx, "X");
        CHECK(len > 1, "TIME('E') non-empty");
        CHECK(var_starts_with(&fx, "X", "0."), "TIME('E') first call < 1s");
    });

    /* Case insensitivity */
    WITH_RESULT("TIME('e')", {
        CHECK(var_starts_with(&fx, "X", "0."), "TIME('e') case insensitive");
    });
}

static void test_time_reset(void)
{
    printf("\n--- TIME: R (reset elapsed) ---\n");

    /* First call in fresh env — reset_stamp == init_stamp so delta ~ 0. */
    WITH_RESULT("TIME('R')", {
        CHECK(var_starts_with(&fx, "X", "0."), "TIME('R') first call < 1s");
    });

    /* Two calls in same env: second call measures delta since first reset. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "x = TIME('R')\n");
            CHECK(rc == IRXPARS_OK, "TIME('R') first call ok");
            rc = run_src(&fx, "y = TIME('R')\n");
            CHECK(rc == IRXPARS_OK, "TIME('R') second call ok");
            /* y must have the decimal-point format. */
            CHECK(var_char_at(&fx, "Y", 1) == '.' ||
                      var_char_at(&fx, "Y", 2) == '.',
                  "TIME('R') second call has decimal point");
            fixture_close(&fx);
        }
    }
}

static void test_time_seconds(void)
{
    printf("\n--- TIME: S (seconds since midnight) ---\n");

    WITH_RESULT("TIME('S')", {
        CHECK(var_all_digits(&fx, "X"), "TIME('S') all digits");
        /* 0 <= S < 86400 */
        CHECK(var_in_range(&fx, "X", 0L, 86399L), "TIME('S') in range");
    });
}

static void test_time_minutes(void)
{
    printf("\n--- TIME: M (minutes since midnight) ---\n");

    WITH_RESULT("TIME('M')", {
        CHECK(var_all_digits(&fx, "X"), "TIME('M') all digits");
        /* 0 <= M < 1440 */
        CHECK(var_in_range(&fx, "X", 0L, 1439L), "TIME('M') in range");
    });
}

static void test_time_hours(void)
{
    printf("\n--- TIME: H (hours since midnight) ---\n");

    WITH_RESULT("TIME('H')", {
        CHECK(var_all_digits(&fx, "X"), "TIME('H') all digits");
        CHECK(var_in_range(&fx, "X", 0L, 23L), "TIME('H') in range");
    });
}

static void test_time_long(void)
{
    printf("\n--- TIME: L (long HH:MM:SS.uuuuuu) ---\n");

    WITH_RESULT("TIME('L')", {
        /* HH:MM:SS.uuuuuu = 15 chars */
        CHECK(var_len(&fx, "X") == 15, "TIME('L') length 15");
        CHECK(var_char_at(&fx, "X", 2) == ':', "TIME('L') colon at 2");
        CHECK(var_char_at(&fx, "X", 5) == ':', "TIME('L') colon at 5");
        CHECK(var_char_at(&fx, "X", 8) == '.', "TIME('L') dot at 8");
    });
}

static void test_time_civil(void)
{
    printf("\n--- TIME: C (civil 12-hour) ---\n");

    WITH_RESULT("TIME('C')", {
        int len = var_len(&fx, "X");
        CHECK(len >= 6, "TIME('C') min length");
        /* ends with am or pm */
        int ends_am = var_ends_with(&fx, "X", "am");
        int ends_pm = var_ends_with(&fx, "X", "pm");
        CHECK(ends_am || ends_pm, "TIME('C') ends am or pm");
    });
}

static void test_time_errors(void)
{
    printf("\n--- TIME: error paths ---\n");

    run_expect_fail("x = TIME('Z')\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "TIME bad subform 'Z'");
    /* 'NX' first char is 'N' = valid; only first char matters per spec. */
    run_expect_fail("x = TIME('ZX')\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "TIME bad subform 'ZX'");
}

/* ================================================================== */
/*  DATE tests                                                         */
/* ================================================================== */

static void test_date_normal(void)
{
    printf("\n--- DATE: N (dd Mon yyyy) ---\n");

    WITH_RESULT("DATE()", {
        int len = var_len(&fx, "X");
        /* Shortest: "1 Jan 2000" = 10; longest: "31 Dec 2999" = 11 */
        CHECK(len >= 10 && len <= 11, "DATE() length 10-11");
    });

    WITH_RESULT("DATE('N')", {
        int len = var_len(&fx, "X");
        CHECK(len >= 10 && len <= 11, "DATE('N') length 10-11");
        /* Space at index 1 or 2 (1- or 2-digit day) */
        int sp1 = var_char_at(&fx, "X", 1);
        int sp2 = var_char_at(&fx, "X", 2);
        CHECK(sp1 == ' ' || sp2 == ' ', "DATE('N') space after day");
    });

    /* Case insensitivity */
    WITH_RESULT("DATE('n')", {
        int len = var_len(&fx, "X");
        CHECK(len >= 10 && len <= 11, "DATE('n') case insensitive");
    });
}

static void test_date_standard(void)
{
    printf("\n--- DATE: S (yyyymmdd) ---\n");

    WITH_RESULT("DATE('S')", {
        CHECK(var_len(&fx, "X") == 8, "DATE('S') length 8");
        CHECK(var_all_digits(&fx, "X"), "DATE('S') all digits");
        /* Year part >= 2000 */
        CHECK(var_in_range(&fx, "X", 20000101L, 29991231L),
              "DATE('S') plausible year");
    });
}

static void test_date_european(void)
{
    printf("\n--- DATE: E (dd/mm/yy) ---\n");

    WITH_RESULT("DATE('E')", {
        CHECK(var_len(&fx, "X") == 8, "DATE('E') length 8");
        CHECK(var_char_at(&fx, "X", 2) == '/', "DATE('E') slash at 2");
        CHECK(var_char_at(&fx, "X", 5) == '/', "DATE('E') slash at 5");
        CHECK(isdigit(var_char_at(&fx, "X", 0)), "DATE('E') digit 0");
        CHECK(isdigit(var_char_at(&fx, "X", 1)), "DATE('E') digit 1");
        CHECK(isdigit(var_char_at(&fx, "X", 3)), "DATE('E') digit 3");
        CHECK(isdigit(var_char_at(&fx, "X", 4)), "DATE('E') digit 4");
        CHECK(isdigit(var_char_at(&fx, "X", 6)), "DATE('E') digit 6");
        CHECK(isdigit(var_char_at(&fx, "X", 7)), "DATE('E') digit 7");
    });
}

static void test_date_usa(void)
{
    printf("\n--- DATE: U (mm/dd/yy) ---\n");

    WITH_RESULT("DATE('U')", {
        CHECK(var_len(&fx, "X") == 8, "DATE('U') length 8");
        CHECK(var_char_at(&fx, "X", 2) == '/', "DATE('U') slash at 2");
        CHECK(var_char_at(&fx, "X", 5) == '/', "DATE('U') slash at 5");
    });
}

static void test_date_base(void)
{
    printf("\n--- DATE: B (days since 1 Jan 0001) ---\n");

    WITH_RESULT("DATE('B')", {
        CHECK(var_all_digits(&fx, "X"), "DATE('B') all digits");
        /* 2026-04-29 = day 739770 approx; any value > 700000 is plausible */
        CHECK(var_in_range(&fx, "X", 700000L, 999999L),
              "DATE('B') plausible base day");
    });
}

static void test_date_day(void)
{
    printf("\n--- DATE: D (day of year, Julian ordinal) ---\n");

    WITH_RESULT("DATE('D')", {
        CHECK(var_all_digits(&fx, "X"), "DATE('D') all digits");
        CHECK(var_in_range(&fx, "X", 1L, 366L), "DATE('D') 1..366");
    });
}

static void test_date_month(void)
{
    printf("\n--- DATE: M (full month name) ---\n");

    WITH_RESULT("DATE('M')", {
        int len = var_len(&fx, "X");
        /* Shortest: "May" = 3; longest: "September" = 9 */
        CHECK(len >= 3 && len <= 9, "DATE('M') length 3-9");
        CHECK(var_all_alpha(&fx, "X"), "DATE('M') all alpha");
    });
}

static void test_date_weekday(void)
{
    printf("\n--- DATE: W (full weekday name) ---\n");

    WITH_RESULT("DATE('W')", {
        int len = var_len(&fx, "X");
        /* Shortest: "Monday" = 6; longest: "Wednesday" = 9 */
        CHECK(len >= 6 && len <= 9, "DATE('W') length 6-9");
        CHECK(var_all_alpha(&fx, "X"), "DATE('W') all alpha");
    });
}

static void test_date_julian(void)
{
    printf("\n--- DATE: J (yyddd Julian) ---\n");

    WITH_RESULT("DATE('J')", {
        CHECK(var_len(&fx, "X") == 5, "DATE('J') length 5");
        CHECK(var_all_digits(&fx, "X"), "DATE('J') all digits");
    });
}

static void test_date_errors(void)
{
    printf("\n--- DATE: error paths ---\n");

    run_expect_fail("x = DATE('Z')\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "DATE bad subform 'Z'");
    /* 'SX' first char is 'S' = valid; only first char matters per spec. */
    run_expect_fail("x = DATE('ZX')\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "DATE bad subform 'ZX'");
}

/* ================================================================== */
/*  Env isolation                                                      */
/* ================================================================== */

static void test_env_isolation(void)
{
    printf("\n--- Env isolation: TIME('R') is per-env ---\n");

    struct fixture fa;
    struct fixture fb;

    if (fixture_open(&fa) != 0)
    {
        CHECK(0, "iso: fixture_open(fa)");
        return;
    }
    if (fixture_open(&fb) != 0)
    {
        CHECK(0, "iso: fixture_open(fb)");
        fixture_close(&fa);
        return;
    }

    /* Both envs are fresh; their first TIME('R') should be near 0. */
    int rc = run_src(&fa, "a = TIME('R')\n");
    CHECK(rc == IRXPARS_OK, "iso: env-a TIME('R') ok");

    rc = run_src(&fb, "b = TIME('R')\n");
    CHECK(rc == IRXPARS_OK, "iso: env-b TIME('R') ok");

    /* Both should start with "0." — independent init_stamps. */
    CHECK(var_starts_with(&fa, "A", "0."), "iso: env-a starts 0.");
    CHECK(var_starts_with(&fb, "B", "0."), "iso: env-b starts 0.");

    fixture_close(&fa);
    fixture_close(&fb);
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

int main(void)
{
    printf("=== WP-CPS-01: TIME/DATE BIFs ===\n");

    test_time_normal();
    test_time_elapsed();
    test_time_reset();
    test_time_seconds();
    test_time_minutes();
    test_time_hours();
    test_time_long();
    test_time_civil();
    test_time_errors();

    test_date_normal();
    test_date_standard();
    test_date_european();
    test_date_usa();
    test_date_base();
    test_date_day();
    test_date_month();
    test_date_weekday();
    test_date_julian();
    test_date_errors();

    test_env_isolation();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
