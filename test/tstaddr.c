/* ------------------------------------------------------------------ */
/*  tstaddr.c - WP-CPS-05 ADDRESS Keyword/Instruction                 */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o /tmp/tstaddr \                  */
/*        test/mvs/tstaddr.c \                                        */
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

/* Direct access to wkbi_address field. */
static int wk_address_eq(struct fixture *f, const char *want8)
{
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)f->env->envblock_userfield;
    if (wk == NULL)
    {
        return 0;
    }
    return memcmp(wk->wkbi_address, want8, sizeof(wk->wkbi_address)) == 0;
}

/* Snapshot wkbi_address into caller buffer for before/after comparison.
 * Buffer must be at least sizeof(wkbi_address) == 8 bytes. */
static void wk_address_snap(struct fixture *f, char *out)
{
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)f->env->envblock_userfield;
    if (wk != NULL)
    {
        memcpy(out, wk->wkbi_address, sizeof(wk->wkbi_address));
    }
}

/* Compare wkbi_address against a raw buffer snapped via wk_address_snap. */
static int wk_address_eq_buf(struct fixture *f, const char *buf)
{
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)f->env->envblock_userfield;
    if (wk == NULL)
    {
        return 0;
    }
    return memcmp(wk->wkbi_address, buf, sizeof(wk->wkbi_address)) == 0;
}

/* ================================================================== */
/*  AC-1: address value address() — round-trip no-op                  */
/* ================================================================== */

static void test_rexxcps_pattern(void)
{
    printf("\n--- ADDRESS instr: REXXCPS pattern (AC-1 / AC-8) ---\n");

    /* address value address() evaluates address() (returns trimmed env name),
     * store_address pads it back — net no-op regardless of the default env
     * (MVS on batch, TSO under IKJEFT01).  Snap before, compare after. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            char initial[8];
            wk_address_snap(&fx, initial);
            int rc = run_src(&fx, "address value address()\n");
            CHECK(rc == IRXPARS_OK,
                  "rexxcps: address value address() executes");
            CHECK(wk_address_eq_buf(&fx, initial),
                  "rexxcps: wkbi_address unchanged after round-trip");
            fixture_close(&fx);
        }
    }
}

/* ================================================================== */
/*  AC-2: address value 'TSO' — VALUE-Form sets setting               */
/* ================================================================== */

static void test_value_form(void)
{
    printf("\n--- ADDRESS instr: VALUE-form (AC-2) ---\n");

    /* address value 'TSO' -> wkbi_address = "TSO     " */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address value 'TSO'\n");
            CHECK(rc == IRXPARS_OK, "value-form: address value 'TSO' executes");
            CHECK(wk_address_eq(&fx, "TSO     "),
                  "value-form: wkbi_address set to 'TSO     '");
            fixture_close(&fx);
        }
    }

    /* Verify via address() BIF round-trip. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address value 'TSO'\n");
            CHECK(rc == IRXPARS_OK, "value-form BIF: set executes");
            rc = run_src(&fx, "x = address()\n");
            CHECK(rc == IRXPARS_OK, "value-form BIF: address() ok");
            CHECK(var_eq(&fx, "X", "TSO"),
                  "value-form BIF: address() returns 'TSO'");
            fixture_close(&fx);
        }
    }

    /* VALUE-Form via variable. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "env = 'ISPEXEC'\n");
            CHECK(rc == IRXPARS_OK, "value-form var: set var");
            rc = run_src(&fx, "address value env\n");
            CHECK(rc == IRXPARS_OK, "value-form var: address value env executes");
            CHECK(wk_address_eq(&fx, "ISPEXEC "),
                  "value-form var: wkbi_address = 'ISPEXEC '");
            fixture_close(&fx);
        }
    }

    /* VALUE keyword is case-insensitive (Symbol tokens are uppercased). */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address value 'CMD'\n");
            CHECK(rc == IRXPARS_OK, "value-form lower: address value 'CMD' ok");
            CHECK(wk_address_eq(&fx, "CMD     "),
                  "value-form lower: wkbi_address = 'CMD     '");
            fixture_close(&fx);
        }
    }
}

/* ================================================================== */
/*  AC-3: address TSO — Symbol-Form (String-Form)                     */
/* ================================================================== */

static void test_string_form(void)
{
    printf("\n--- ADDRESS instr: String-Form (AC-3) ---\n");

    /* address TSO (Symbol) -> wkbi_address = "TSO     " */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address TSO\n");
            CHECK(rc == IRXPARS_OK, "string-form: address TSO executes");
            CHECK(wk_address_eq(&fx, "TSO     "),
                  "string-form: wkbi_address = 'TSO     '");
            fixture_close(&fx);
        }
    }

    /* address 'CMD' (String literal, case-preserved). */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address 'CMD'\n");
            CHECK(rc == IRXPARS_OK, "string-form str: address 'CMD' executes");
            CHECK(wk_address_eq(&fx, "CMD     "),
                  "string-form str: wkbi_address = 'CMD     '");
            fixture_close(&fx);
        }
    }

    /* Verify via address() BIF. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address TSO\n");
            CHECK(rc == IRXPARS_OK, "string-form BIF: set executes");
            rc = run_src(&fx, "x = address()\n");
            CHECK(rc == IRXPARS_OK, "string-form BIF: address() ok");
            CHECK(var_eq(&fx, "X", "TSO"),
                  "string-form BIF: address() returns 'TSO'");
            fixture_close(&fx);
        }
    }

    /* Single-char env name. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address V\n");
            CHECK(rc == IRXPARS_OK, "string-form 1char: address V executes");
            CHECK(wk_address_eq(&fx, "V       "),
                  "string-form 1char: wkbi_address = 'V       '");
            fixture_close(&fx);
        }
    }
}

/* ================================================================== */
/*  AC-4: address (bare) — Toggle-Form stub                           */
/* ================================================================== */

static void test_toggle_form(void)
{
    printf("\n--- ADDRESS instr: Toggle-Form stub (AC-4) ---\n");

    /* Bare ADDRESS: parse cleanly, current setting unchanged. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address TSO\n");
            CHECK(rc == IRXPARS_OK, "toggle-stub: set TSO first");

            rc = run_src(&fx, "address\n");
            CHECK(rc == IRXPARS_OK, "toggle-stub: bare address executes");
            CHECK(wk_address_eq(&fx, "TSO     "),
                  "toggle-stub: wkbi_address unchanged (stub)");
            fixture_close(&fx);
        }
    }

    /* Default setting is also unchanged when ADDRESS is bare.
     * Snap before to avoid hardcoding "MVS" vs "TSO" default. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            char initial[8];
            wk_address_snap(&fx, initial);
            int rc = run_src(&fx, "address\n");
            CHECK(rc == IRXPARS_OK,
                  "toggle-stub default: bare address on default executes");
            CHECK(wk_address_eq_buf(&fx, initial),
                  "toggle-stub default: default unchanged");
            fixture_close(&fx);
        }
    }
}

/* ================================================================== */
/*  AC-5: address TSO 'LISTDS' — One-Shot-Form stub                   */
/* ================================================================== */

static void test_one_shot_form(void)
{
    printf("\n--- ADDRESS instr: One-Shot-Form stub (AC-5) ---\n");

    /* One-Shot: parse cleanly, current setting unchanged.
     * Snap before to avoid hardcoding "MVS" vs "TSO" default. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            char initial[8];
            wk_address_snap(&fx, initial);
            int rc = run_src(&fx, "address TSO 'LISTDS'\n");
            CHECK(rc == IRXPARS_OK,
                  "one-shot: address TSO 'LISTDS' executes");
            CHECK(wk_address_eq_buf(&fx, initial),
                  "one-shot: default setting unchanged (stub)");
            fixture_close(&fx);
        }
    }

    /* One-Shot after explicit set: default still not modified. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address TSO\n");
            CHECK(rc == IRXPARS_OK, "one-shot-after-set: set TSO");
            rc = run_src(&fx, "address CMD 'DIR'\n");
            CHECK(rc == IRXPARS_OK,
                  "one-shot-after-set: one-shot CMD executes");
            CHECK(wk_address_eq(&fx, "TSO     "),
                  "one-shot-after-set: TSO default unchanged");
            fixture_close(&fx);
        }
    }
}

/* ================================================================== */
/*  AC-6: Setting > 8 chars truncated to 8                            */
/* ================================================================== */

static void test_truncation(void)
{
    printf("\n--- ADDRESS instr: truncation (AC-6) ---\n");

    /* VALUE-Form with 12-char string: truncated to 8. */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address value 'TOOLONGNAME'\n");
            CHECK(rc == IRXPARS_OK, "trunc value: executes");
            CHECK(wk_address_eq(&fx, "TOOLONGN"),
                  "trunc value: 11-char truncated to first 8");
            fixture_close(&fx);
        }
    }

    /* String-Form Symbol with 9-char name (tokenizer allows it). */
    {
        struct fixture fx;
        if (fixture_open(&fx) == 0)
        {
            int rc = run_src(&fx, "address 'NINECHARS'\n");
            CHECK(rc == IRXPARS_OK, "trunc str: executes");
            CHECK(wk_address_eq(&fx, "NINECHA"
                                     "R"),
                  "trunc str: 9-char truncated to first 8");
            fixture_close(&fx);
        }
    }
}

/* ================================================================== */
/*  Isolation: wkbi_address is per-environment                        */
/* ================================================================== */

static void test_isolation(void)
{
    printf("\n--- ADDRESS instr: per-Exec isolation ---\n");

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

    int rc = run_src(&fa, "address TSO\n");
    CHECK(rc == IRXPARS_OK, "iso: fa set TSO ok");

    rc = run_src(&fb, "address CMD\n");
    CHECK(rc == IRXPARS_OK, "iso: fb set CMD ok");

    CHECK(wk_address_eq(&fa, "TSO     "), "iso: fa has TSO");
    CHECK(wk_address_eq(&fb, "CMD     "), "iso: fb has CMD");

    fixture_close(&fa);
    fixture_close(&fb);
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

int main(void)
{
    printf("=== WP-CPS-05: ADDRESS Keyword/Instruction ===\n");

    test_rexxcps_pattern();
    test_value_form();
    test_string_form();
    test_toggle_form();
    test_one_shot_form();
    test_truncation();
    test_isolation();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
