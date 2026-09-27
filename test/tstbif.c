/* ------------------------------------------------------------------ */
/*  tstbif.c - BIF lookup unit tests (WP-21a, #254)                  */
/*                                                                    */
/*  Verifies irx_bif_find_local(): core BIFs resolve with the right   */
/*  arity, unknown and malformed names do not, and irxinit() builds   */
/*  no per-environment BIF registry any more (#254).                  */
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
#include "irxwkblk.h"
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

static void check_bif(const char *name, int min_args, int max_args)
{
    char msg[64];
    const struct irx_bif_entry *e =
        irx_bif_find_local((const unsigned char *)name, strlen(name));

    snprintf(msg, sizeof(msg), "%s resolves", name);
    CHECK(e != NULL, msg);
    snprintf(msg, sizeof(msg), "%s arity %d..%d", name, min_args, max_args);
    CHECK(e != NULL && strcmp(e->name, name) == 0 &&
              e->min_args == min_args && e->max_args == max_args &&
              e->handler != NULL,
          msg);
}

static void test_core_bifs(void)
{
    printf("\n--- BIF#1: core BIFs resolve locally ---\n");
    check_bif("LENGTH", 1, 1);
    check_bif("ARG", 0, 2);
    check_bif("SUBSTR", 2, 4);
    check_bif("XRANGE", 0, 2);
}

static void test_not_found(void)
{
    const unsigned char bar[] = {'B', 'A', 'R'};
    const unsigned char len_prefix[] = {'L', 'E', 'N'};
    const unsigned char too_long[IRX_BIF_NAME_MAX] = {'L', 'E', 'N', 'G',
                                                      'T', 'H'};

    printf("\n--- BIF#2: unknown and malformed names ---\n");
    CHECK(irx_bif_find_local(bar, sizeof(bar)) == NULL, "BAR not found");
    CHECK(irx_bif_find_local(len_prefix, sizeof(len_prefix)) == NULL,
          "prefix LEN of LENGTH not found");
    CHECK(irx_bif_find_local(NULL, 1) == NULL, "NULL name");
    CHECK(irx_bif_find_local((const unsigned char *)"X", 0) == NULL,
          "zero-length name");
    CHECK(irx_bif_find_local(too_long, sizeof(too_long)) == NULL,
          "name of IRX_BIF_NAME_MAX bytes");
}

static void test_irxinit_builds_no_registry(void)
{
    /* #254: IRXINIT builds the environment only.  The per-env registry
     * it used to fill is what linked the whole interpreter into the
     * IRXINIT load module, and nothing has read it since #200. */
    struct envblock *env = NULL;

    printf("\n--- BIF#3: irxinit builds no BIF registry ---\n");
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)env->envblock_workblok_ext;
    CHECK(wk != NULL, "work block present");
    CHECK(wk != NULL && wk->wkbi_reserved_bifreg == NULL,
          "former registry slot stays NULL");
    CHECK(irxterm(env) == 0, "irxterm OK");
}

int main(void)
{
    printf("=== WP-21a / #254: BIF lookup tests ===\n");

    test_core_bifs();
    test_not_found();
    test_irxinit_builds_no_registry();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
