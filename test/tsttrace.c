/* ------------------------------------------------------------------ */
/*  tsttrace.c - WP-CPS-02 TRACE() BIF + WP-CPS-04 TRACE instruction  */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o /tmp/tsttrace \                 */
/*        test/mvs/tsttrace.c \                                       */
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        printf("    %s: not found (expected '%s')\n", name, want);
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

/* Run src, expect SYNTAX condition with want_code / want_subcode.
 * want_subcode == 0 means "don't check subcode". */
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
/*  AC-1: TRACE() default returns 'N'                                 */
/* ================================================================== */

static void test_trace_default(void)
{
    printf("\n--- TRACE(): default setting ---\n");

    WITH_RESULT("TRACE()", {
        CHECK(var_eq(&fx, "X", "N"), "TRACE() default -> 'N'");
    });
}

/* ================================================================== */
/*  AC-2: TRACE('Off') Read-Modify-Write                              */
/* ================================================================== */

static void test_trace_rmw(void)
{
    printf("\n--- TRACE(): Read-Modify-Write ---\n");

    /* trace('Off') returns previous 'N' and sets 'O'. */
    {
        struct fixture fx;
        if (fixture_open(&fx) != 0)
        {
            CHECK(0, "rmw: fixture_open");
            return;
        }
        int rc = run_src(&fx, "prev = TRACE('Off')\n");
        CHECK(rc == IRXPARS_OK, "rmw: TRACE('Off') executes");
        CHECK(var_eq(&fx, "PREV", "N"), "rmw: TRACE('Off') returns previous 'N'");

        rc = run_src(&fx, "cur = TRACE()\n");
        CHECK(rc == IRXPARS_OK, "rmw: TRACE() after set executes");
        CHECK(var_eq(&fx, "CUR", "O"), "rmw: setting is now 'O'");

        fixture_close(&fx);
    }

    /* trace('A') RMW from default. */
    {
        struct fixture fx;
        if (fixture_open(&fx) != 0)
        {
            CHECK(0, "rmw-A: fixture_open");
            return;
        }
        int rc = run_src(&fx, "prev = TRACE('A')\n");
        CHECK(rc == IRXPARS_OK, "rmw-A: executes");
        CHECK(var_eq(&fx, "PREV", "N"), "rmw-A: returns previous 'N'");

        rc = run_src(&fx, "cur = TRACE()\n");
        CHECK(rc == IRXPARS_OK, "rmw-A: TRACE() ok");
        CHECK(var_eq(&fx, "CUR", "A"), "rmw-A: setting is 'A'");

        fixture_close(&fx);
    }
}

/* ================================================================== */
/*  AC-3: All 9 valid letters accepted (case-insensitive)             */
/* ================================================================== */

static void test_trace_all_letters(void)
{
    printf("\n--- TRACE(): all valid letters ---\n");

    /* Each test: set, then read back. Also verify case-insensitivity. */
    /* Each letter: set via TRACE(letter), read back via TRACE(), compare. */
#define CHECK_LETTER(letter, set_stmt, expect)                       \
    do                                                               \
    {                                                                \
        struct fixture _fx;                                          \
        if (fixture_open(&_fx) == 0)                                 \
        {                                                            \
            int _r = run_src(&_fx, set_stmt);                        \
            CHECK(_r == IRXPARS_OK, "TRACE('" letter "') executes"); \
            _r = run_src(&_fx, "y = TRACE()\n");                     \
            CHECK(_r == IRXPARS_OK && var_eq(&_fx, "Y", expect),     \
                  "TRACE('" letter "') sets correctly");             \
            fixture_close(&_fx);                                     \
        }                                                            \
    } while (0)

    CHECK_LETTER("A", "x = TRACE('A')\n", "A");
    CHECK_LETTER("I", "x = TRACE('I')\n", "I");
    CHECK_LETTER("L", "x = TRACE('L')\n", "L");
    CHECK_LETTER("R", "x = TRACE('R')\n", "R");
    CHECK_LETTER("C", "x = TRACE('C')\n", "C");
    CHECK_LETTER("F", "x = TRACE('F')\n", "F");
    CHECK_LETTER("E", "x = TRACE('E')\n", "E");
    CHECK_LETTER("N", "x = TRACE('N')\n", "N");
    CHECK_LETTER("O", "x = TRACE('O')\n", "O");
#undef CHECK_LETTER

    /* Case insensitivity: lowercase. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "x = TRACE('a')\n");
            CHECK(rc == IRXPARS_OK, "TRACE('a') lowercase executes");
            rc = run_src(&fx, "y = TRACE()\n");
            CHECK(rc == IRXPARS_OK && var_eq(&fx, "Y", "A"),
                  "TRACE('a') stores 'A' (uppercased)");
            fixture_close(&fx);
        }
    }

    /* First-char rule: 'Off' -> 'O'. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "x = TRACE('Off')\n");
            CHECK(rc == IRXPARS_OK, "TRACE('Off') first-char rule executes");
            rc = run_src(&fx, "y = TRACE()\n");
            CHECK(rc == IRXPARS_OK && var_eq(&fx, "Y", "O"),
                  "TRACE('Off') stores 'O'");
            fixture_close(&fx);
        }
    }
}

/* ================================================================== */
/*  AC-4: '?' prefix — toggle-flag form                               */
/* ================================================================== */

static void test_trace_toggle(void)
{
    printf("\n--- TRACE(): '?' prefix toggle ---\n");

    /* trace('?A') sets letter 'A' and toggle; trace() returns "?A". */
    {
        struct fixture fx;
        if (fixture_open(&fx) != 0)
        {
            CHECK(0, "toggle: fixture_open");
            return;
        }
        int rc = run_src(&fx, "prev = TRACE('?A')\n");
        CHECK(rc == IRXPARS_OK, "TRACE('?A') executes");
        CHECK(var_eq(&fx, "PREV", "N"), "TRACE('?A') returns previous 'N'");

        rc = run_src(&fx, "cur = TRACE()\n");
        CHECK(rc == IRXPARS_OK, "TRACE() after ?A ok");
        CHECK(var_eq(&fx, "CUR", "?A"), "TRACE() returns '?A' when toggle set");

        fixture_close(&fx);
    }

    /* Plain letter after toggle-set clears the toggle (no sticky toggle). */
    {
        struct fixture fx;
        if (fixture_open(&fx) != 0)
        {
            CHECK(0, "toggle-clear: fixture_open");
            return;
        }
        int rc = run_src(&fx, "x = TRACE('?A')\n");
        CHECK(rc == IRXPARS_OK, "toggle-clear: set ?A");

        rc = run_src(&fx, "y = TRACE('R')\n");
        CHECK(rc == IRXPARS_OK, "toggle-clear: TRACE('R') after ?A executes");
        CHECK(var_eq(&fx, "Y", "?A"),
              "toggle-clear: TRACE('R') returns old '?A'");

        rc = run_src(&fx, "z = TRACE()\n");
        CHECK(rc == IRXPARS_OK, "toggle-clear: TRACE() after 'R' ok");
        CHECK(var_eq(&fx, "Z", "R"),
              "toggle-clear: plain 'R' cleared the toggle");

        fixture_close(&fx);
    }

    /* '?' prefix with lowercase letter. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "x = TRACE('?i')\n");
            CHECK(rc == IRXPARS_OK, "TRACE('?i') lowercase executes");
            rc = run_src(&fx, "y = TRACE()\n");
            CHECK(rc == IRXPARS_OK && var_eq(&fx, "Y", "?I"),
                  "TRACE('?i') stores '?I'");
            fixture_close(&fx);
        }
    }
}

/* ================================================================== */
/*  AC-5: Invalid options -> SYNTAX 40.23                             */
/* ================================================================== */

static void test_trace_errors(void)
{
    printf("\n--- TRACE(): error paths ---\n");

    run_expect_fail("x = TRACE('X')\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "TRACE bad option 'X'");
    run_expect_fail("x = TRACE('Z')\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "TRACE bad option 'Z'");
    run_expect_fail("x = TRACE('?')\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "TRACE bare '?' without letter");
    run_expect_fail("x = TRACE('?X')\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "TRACE '?X' invalid letter");
}

/* ================================================================== */
/*  AC-6: Env isolation — wkbi_trace is per-Exec (per workblock)      */
/* ================================================================== */

static void test_trace_isolation(void)
{
    printf("\n--- TRACE(): per-Exec isolation ---\n");

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

    /* Set different settings in each env. */
    int rc = run_src(&fa, "x = TRACE('A')\n");
    CHECK(rc == IRXPARS_OK, "iso: env-a TRACE('A') ok");

    rc = run_src(&fb, "x = TRACE('E')\n");
    CHECK(rc == IRXPARS_OK, "iso: env-b TRACE('E') ok");

    /* Read back from each env independently. */
    rc = run_src(&fa, "a = TRACE()\n");
    CHECK(rc == IRXPARS_OK, "iso: env-a TRACE() ok");
    CHECK(var_eq(&fa, "A", "A"), "iso: env-a has 'A'");

    rc = run_src(&fb, "b = TRACE()\n");
    CHECK(rc == IRXPARS_OK, "iso: env-b TRACE() ok");
    CHECK(var_eq(&fb, "B", "E"), "iso: env-b has 'E'");

    fixture_close(&fa);
    fixture_close(&fb);
}

/* ================================================================== */
/*  AC-1 edge: empty/NULL arg treated as read-only                    */
/* ================================================================== */

static void test_trace_empty_arg(void)
{
    printf("\n--- TRACE(): empty arg = read-only ---\n");

    /* trace('') should be read-only (does not change setting). */
    {
        struct fixture fx;
        if (fixture_open(&fx) != 0)
        {
            CHECK(0, "empty-arg: fixture_open");
            return;
        }
        int rc = run_src(&fx, "x = TRACE('A')\n");
        CHECK(rc == IRXPARS_OK, "empty-arg: set 'A' first");

        rc = run_src(&fx, "y = TRACE('')\n");
        CHECK(rc == IRXPARS_OK, "empty-arg: TRACE('') executes");
        CHECK(var_eq(&fx, "Y", "A"),
              "empty-arg: TRACE('') returns current 'A' unchanged");

        rc = run_src(&fx, "z = TRACE()\n");
        CHECK(rc == IRXPARS_OK, "empty-arg: TRACE() after empty-arg ok");
        CHECK(var_eq(&fx, "Z", "A"),
              "empty-arg: setting unchanged after TRACE('')");

        fixture_close(&fx);
    }
}

/* ================================================================== */
/*  WP-CPS-04: TRACE instruction-form tests                           */
/* ================================================================== */

/* Read wkbi_trace/wkbi_interactive directly from the fixture. */
static int wk_letter(struct fixture *f)
{
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)f->env->envblock_userfield;
    return (wk != NULL) ? wk->wkbi_trace : (int)TRACE_NORMAL;
}

static int wk_interactive(struct fixture *f)
{
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)f->env->envblock_userfield;
    return (wk != NULL) ? wk->wkbi_interactive : 0;
}

/* AC-1 + AC-5: String-form sets trace letter. */
static void test_instr_string_form(void)
{
    printf("\n--- TRACE instr: string-form ---\n");

    /* trace off -> 'O' */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "trace off\n");
            CHECK(rc == IRXPARS_OK, "instr: 'trace off' executes");
            CHECK(wk_letter(&fx) == 'O', "instr: 'trace off' sets 'O'");
            fixture_close(&fx);
        }
    }

    /* trace 'I' (string literal) -> 'I' */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "trace 'I'\n");
            CHECK(rc == IRXPARS_OK, "instr: trace 'I' executes");
            CHECK(wk_letter(&fx) == 'I', "instr: trace 'I' sets 'I'");
            fixture_close(&fx);
        }
    }

    /* trace I (bare symbol) -> 'I' */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "trace I\n");
            CHECK(rc == IRXPARS_OK, "instr: trace I (symbol) executes");
            CHECK(wk_letter(&fx) == 'I', "instr: trace I sets 'I'");
            fixture_close(&fx);
        }
    }

    /* trace A -> 'A'; verify via TRACE() BIF */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "trace A\n");
            CHECK(rc == IRXPARS_OK, "instr: trace A executes");
            rc = run_src(&fx, "x = TRACE()\n");
            CHECK(rc == IRXPARS_OK && var_eq(&fx, "X", "A"),
                  "instr: trace A verified via TRACE()");
            fixture_close(&fx);
        }
    }
}

/* AC-2 + AC-3: VALUE-form evaluates expression. */
static void test_instr_value_form(void)
{
    printf("\n--- TRACE instr: VALUE-form ---\n");

    /* trace value 'A' -> 'A' */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "trace value 'A'\n");
            CHECK(rc == IRXPARS_OK, "instr: trace value 'A' executes");
            CHECK(wk_letter(&fx) == 'A', "instr: trace value 'A' sets 'A'");
            fixture_close(&fx);
        }
    }

    /* trace value tracevar; tracevar='Off' -> 'O' */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "tracevar = 'Off'\n");
            CHECK(rc == IRXPARS_OK, "instr: set tracevar='Off'");
            rc = run_src(&fx, "trace value tracevar\n");
            CHECK(rc == IRXPARS_OK, "instr: trace value tracevar executes");
            CHECK(wk_letter(&fx) == 'O',
                  "instr: trace value tracevar sets 'O'");
            fixture_close(&fx);
        }
    }

    /* AC-4: trace value trace() round-trip */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "trace value 'E'\n");
            CHECK(rc == IRXPARS_OK, "instr: set to 'E' first");
            rc = run_src(&fx, "trace value trace()\n");
            CHECK(rc == IRXPARS_OK, "instr: trace value trace() executes");
            CHECK(wk_letter(&fx) == 'E',
                  "instr: round-trip trace value trace() preserves 'E'");
            fixture_close(&fx);
        }
    }
}

/* AC-6: Toggle-form flips wkbi_interactive; letter unchanged. */
static void test_instr_toggle_form(void)
{
    printf("\n--- TRACE instr: toggle-form ---\n");

    /* Default: interactive=0, letter='N'.
     * After bare 'trace': interactive=1, letter='N'. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            CHECK(wk_interactive(&fx) == 0,
                  "toggle: initial wkbi_interactive is 0");
            CHECK(wk_letter(&fx) == 'N',
                  "toggle: initial wkbi_trace is 'N'");

            int rc = run_src(&fx, "trace\n");
            CHECK(rc == IRXPARS_OK, "toggle: bare 'trace' executes");
            CHECK(wk_interactive(&fx) == 1,
                  "toggle: wkbi_interactive becomes 1");
            CHECK(wk_letter(&fx) == 'N',
                  "toggle: wkbi_trace unchanged after toggle");

            /* Second toggle -> back to 0. */
            rc = run_src(&fx, "trace\n");
            CHECK(rc == IRXPARS_OK, "toggle: second 'trace' executes");
            CHECK(wk_interactive(&fx) == 0,
                  "toggle: wkbi_interactive back to 0");
            CHECK(wk_letter(&fx) == 'N',
                  "toggle: wkbi_trace still 'N' after two toggles");

            fixture_close(&fx);
        }
    }

    /* Toggle does not touch letter even when letter was changed. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "trace A\n");
            CHECK(rc == IRXPARS_OK, "toggle: set letter to 'A'");
            rc = run_src(&fx, "trace\n");
            CHECK(rc == IRXPARS_OK, "toggle: bare trace after 'A'");
            CHECK(wk_interactive(&fx) == 1,
                  "toggle: interactive toggled to 1");
            CHECK(wk_letter(&fx) == 'A',
                  "toggle: letter 'A' unchanged");
            fixture_close(&fx);
        }
    }
}

/* AC-7: Invalid option -> SYNTAX 40.23 from instruction form. */
static void test_instr_errors(void)
{
    printf("\n--- TRACE instr: error paths ---\n");

    run_expect_fail("trace X\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID, "instr: 'trace X' bad option");
    run_expect_fail("trace value 'Z'\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID,
                    "instr: 'trace value \"Z\"' bad option");
    run_expect_fail("trace value '?'\n", SYNTAX_BAD_CALL,
                    ERR40_OPTION_INVALID,
                    "instr: 'trace value \"?\"' bare ? invalid");
}

/* AC-11: Skip-form is a no-op; no SYNTAX raised. */
static void test_instr_skip_form(void)
{
    printf("\n--- TRACE instr: skip-form ---\n");

    /* trace 0 -> no-op; letter unchanged. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "trace 0\n");
            CHECK(rc == IRXPARS_OK, "skip: trace 0 no SYNTAX");
            CHECK(wk_letter(&fx) == 'N', "skip: trace 0 letter unchanged");
            fixture_close(&fx);
        }
    }

    /* trace 5 -> no-op. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "trace 5\n");
            CHECK(rc == IRXPARS_OK, "skip: trace 5 no SYNTAX");
            fixture_close(&fx);
        }
    }

    /* trace -5 -> no-op (signed skip). */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "trace -5\n");
            CHECK(rc == IRXPARS_OK, "skip: trace -5 no SYNTAX");
            CHECK(wk_letter(&fx) == 'N', "skip: trace -5 letter unchanged");
            fixture_close(&fx);
        }
    }
}

/* AC-10: REXXCPS round-trip pattern. */
static void test_instr_rexxcps_pattern(void)
{
    printf("\n--- TRACE instr: REXXCPS pattern ---\n");

    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "tracevar = 'Off'\n"
                                  "trace value tracevar\n");
            CHECK(rc == IRXPARS_OK, "rexxcps: set Off via variable");
            CHECK(wk_letter(&fx) == 'O', "rexxcps: letter is 'O'");

            rc = run_src(&fx, "trace value trace()\n");
            CHECK(rc == IRXPARS_OK, "rexxcps: round-trip executes");
            CHECK(wk_letter(&fx) == 'O', "rexxcps: round-trip preserves 'O'");

            fixture_close(&fx);
        }
    }
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

int main(void)
{
    printf("=== WP-CPS-02: TRACE() BIF ===\n");

    test_trace_default();
    test_trace_rmw();
    test_trace_all_letters();
    test_trace_toggle();
    test_trace_errors();
    test_trace_isolation();
    test_trace_empty_arg();

    printf("\n=== WP-CPS-04: TRACE instruction ===\n");

    test_instr_string_form();
    test_instr_value_form();
    test_instr_toggle_form();
    test_instr_errors();
    test_instr_skip_form();
    test_instr_rexxcps_pattern();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
