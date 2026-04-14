/* ------------------------------------------------------------------ */
/*  test_irxlstr.c - WP-11b REXX lstring adapter tests                */
/*                                                                    */
/*  Tests the pure-C parts of irx#lstr.c that don't require the       */
/*  full lstring370 library to be linked (allocator wiring,           */
/*  _Lisnum, irx_datatype). End-to-end allocator routing via          */
/*  irxinit/irxterm is covered by a second test that links in the    */
/*  Phase 1 modules.                                                  */
/*                                                                    */
/*  Build (cross-compile, Linux/gcc):                                 */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_irxlstr \              */
/*        test/test_irxlstr.c \                                       */
/*        'src/irx#'*.c                                                */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "irx.h"
#include "irxrab.h"
#include "irxwkblk.h"
#include "irxfunc.h"
#include "lstring.h"
#include "lstralloc.h"
#include "irxlstr.h"

#ifndef __MVS__
void *_simulated_tcbuser = NULL;
#endif

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) \
    do { \
        tests_run++; \
        if (cond) { \
            tests_passed++; \
            printf("  PASS: %s\n", msg); \
        } else { \
            tests_failed++; \
            printf("  FAIL: %s\n", msg); \
        } \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Helpers: build an Lstr from a C string without depending on the   */
/*  lstring370 library being linked in.                               */
/* ------------------------------------------------------------------ */

static Lstr make_lstr(const char *cstr)
{
    Lstr s;
    s.pstr   = (unsigned char *)(uintptr_t)cstr;
    s.len    = strlen(cstr);
    s.maxlen = s.len;
    s.type   = LSTRING_TY;
    return s;
}

/* ------------------------------------------------------------------ */
/*  _Lisnum tests                                                     */
/* ------------------------------------------------------------------ */

static void test_lisnum_integers(void)
{
    Lstr s;
    printf("\n--- Test: _Lisnum integers ---\n");

    s = make_lstr("42");
    CHECK(_Lisnum(&s) == LNUM_INTEGER, "'42' is INTEGER");
    CHECK(s.type == LINTEGER_TY, "'42' type cached LINTEGER_TY");
    /* Second call hits the cache. */
    CHECK(_Lisnum(&s) == LNUM_INTEGER, "cached result still INTEGER");

    s = make_lstr("  -123  ");
    CHECK(_Lisnum(&s) == LNUM_INTEGER,
          "'  -123  ' (whitespace + sign)");

    s = make_lstr("+0");
    CHECK(_Lisnum(&s) == LNUM_INTEGER, "'+0'");
}

static void test_lisnum_reals(void)
{
    Lstr s;
    printf("\n--- Test: _Lisnum reals ---\n");

    s = make_lstr("3.14");
    CHECK(_Lisnum(&s) == LNUM_REAL, "'3.14' is REAL");
    CHECK(s.type == LREAL_TY, "'3.14' type cached LREAL_TY");

    s = make_lstr("-.5");
    CHECK(_Lisnum(&s) == LNUM_REAL, "'-.5' is REAL");

    s = make_lstr("1E5");
    CHECK(_Lisnum(&s) == LNUM_REAL, "'1E5' is REAL (has exponent)");

    s = make_lstr("1E+5");
    CHECK(_Lisnum(&s) == LNUM_REAL, "'1E+5' is REAL");

    s = make_lstr("1.5e-2");
    CHECK(_Lisnum(&s) == LNUM_REAL, "'1.5e-2' is REAL");
}

static void test_lisnum_rejects(void)
{
    Lstr s;
    printf("\n--- Test: _Lisnum rejects ---\n");

    s = make_lstr("");
    CHECK(_Lisnum(&s) == LNUM_NOT_NUM, "empty string is not a number");

    s = make_lstr("abc");
    CHECK(_Lisnum(&s) == LNUM_NOT_NUM, "'abc' is not a number");

    s = make_lstr("1..2");
    CHECK(_Lisnum(&s) == LNUM_NOT_NUM, "'1..2' (double dot) rejected");

    s = make_lstr("1E");
    CHECK(_Lisnum(&s) == LNUM_NOT_NUM,
          "'1E' (exponent without digits) rejected");

    s = make_lstr(".");
    CHECK(_Lisnum(&s) == LNUM_NOT_NUM,
          "'.' alone rejected");

    s = make_lstr("12 34");
    CHECK(_Lisnum(&s) == LNUM_NOT_NUM,
          "'12 34' (interior whitespace) rejected");
}

/* ------------------------------------------------------------------ */
/*  irx_datatype tests                                                */
/* ------------------------------------------------------------------ */

static void test_datatype_numeric(void)
{
    Lstr s;
    printf("\n--- Test: irx_datatype N / no-option ---\n");

    s = make_lstr("42");
    CHECK(irx_datatype(&s, 0)   == 1, "'42' is NUM (no option)");
    CHECK(irx_datatype(&s, 'N') == 1, "'42' is NUM ('N')");

    s = make_lstr("abc");
    CHECK(irx_datatype(&s, 'N') == 0, "'abc' is not NUM");

    s = make_lstr("");
    CHECK(irx_datatype(&s, 'N') == 0, "empty is not NUM");
}

static void test_datatype_classifiers(void)
{
    Lstr s;
    printf("\n--- Test: irx_datatype classifiers ---\n");

    s = make_lstr("abc123");
    CHECK(irx_datatype(&s, 'A') == 1, "'abc123' is Alphanumeric");
    s = make_lstr("abc-123");
    CHECK(irx_datatype(&s, 'A') == 0, "'abc-123' is not Alphanumeric");

    s = make_lstr("1010");
    CHECK(irx_datatype(&s, 'B') == 1, "'1010' is Binary");
    s = make_lstr("102");
    CHECK(irx_datatype(&s, 'B') == 0, "'102' is not Binary");

    s = make_lstr("hello");
    CHECK(irx_datatype(&s, 'L') == 1, "'hello' is Lower");
    CHECK(irx_datatype(&s, 'M') == 1, "'hello' is Mixed (alpha)");
    CHECK(irx_datatype(&s, 'U') == 0, "'hello' is not Upper");

    s = make_lstr("HELLO");
    CHECK(irx_datatype(&s, 'U') == 1, "'HELLO' is Upper");
    CHECK(irx_datatype(&s, 'M') == 1, "'HELLO' is Mixed");

    s = make_lstr("MixEd");
    CHECK(irx_datatype(&s, 'M') == 1, "'MixEd' is Mixed");
    CHECK(irx_datatype(&s, 'L') == 0, "'MixEd' is not all-Lower");
    CHECK(irx_datatype(&s, 'U') == 0, "'MixEd' is not all-Upper");

    s = make_lstr("stem.i");
    CHECK(irx_datatype(&s, 'S') == 1, "'stem.i' is Symbol");
    s = make_lstr("has space");
    CHECK(irx_datatype(&s, 'S') == 0, "'has space' is not Symbol");

    s = make_lstr("123");
    CHECK(irx_datatype(&s, 'W') == 1, "'123' is Whole");
    s = make_lstr("-42");
    CHECK(irx_datatype(&s, 'W') == 1, "'-42' is Whole (signed)");
    s = make_lstr("1.5");
    CHECK(irx_datatype(&s, 'W') == 0, "'1.5' is not Whole");

    s = make_lstr("DEAD BEEF");
    CHECK(irx_datatype(&s, 'X') == 1, "'DEAD BEEF' is Hex");
    s = make_lstr("GHOST");
    CHECK(irx_datatype(&s, 'X') == 0, "'GHOST' is not Hex");

    /* Unknown option returns 0. */
    s = make_lstr("anything");
    CHECK(irx_datatype(&s, 'Q') == 0, "unknown option returns 0");
}

/* ------------------------------------------------------------------ */
/*  End-to-end: allocator bridge through irxinit                      */
/* ------------------------------------------------------------------ */

static void test_allocator_bridge(void)
{
    struct envblock   *env = NULL;
    struct irx_wkblk_int *wkbi;
    struct lstr_alloc *alloc;
    struct lstr_alloc *alloc2;
    int rc;

    printf("\n--- Test: irx_lstr_init + allocator bridge ---\n");

    rc = irxinit(NULL, &env);
    CHECK(rc == 0 && env != NULL, "irxinit succeeds");
    if (env == NULL) return;

    alloc = irx_lstr_init(env);
    CHECK(alloc != NULL, "irx_lstr_init returns non-NULL");
    CHECK(alloc->alloc   != NULL, "alloc function pointer set");
    CHECK(alloc->dealloc != NULL, "dealloc function pointer set");
    CHECK(alloc->ctx == env,      "ctx is the envblock");

    /* Second call must return the same pointer (cached in wkbi). */
    alloc2 = irx_lstr_init(env);
    CHECK(alloc2 == alloc, "second irx_lstr_init returns cached pointer");

    wkbi = (struct irx_wkblk_int *)env->envblock_userfield;
    CHECK(wkbi != NULL && wkbi->wkbi_lstr_alloc == alloc,
          "wkbi->wkbi_lstr_alloc points at the allocator");

    /* Exercise the bridge by alloc/free via the callbacks directly. */
    {
        void *mem = (*alloc->alloc)(64, alloc->ctx);
        CHECK(mem != NULL, "bridged alloc(64) succeeds");
        if (mem != NULL) {
            memset(mem, 0xAB, 64);
            (*alloc->dealloc)(mem, 64, alloc->ctx);
        }
    }

    /* irxterm must release the allocator cleanly. */
    rc = irxterm(env);
    CHECK(rc == 0, "irxterm succeeds (frees the allocator)");
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== REXX/370 WP-11b lstring adapter tests ===\n");

    test_lisnum_integers();
    test_lisnum_reals();
    test_lisnum_rejects();
    test_datatype_numeric();
    test_datatype_classifiers();
    test_allocator_bridge();

    printf("\n=== Results: %d/%d passed",
           tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
