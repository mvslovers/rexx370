/* ------------------------------------------------------------------ */
/*  tstbif.c - WP-21a BIF registry unit tests                       */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_bif \                 */
/*        test/test_bif.c \                                            */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \        */
/*        'src/irx#anch.c'  'src/irx#uid.c'  'src/irx#msid.c' \        */
/*        'src/irx#cond.c' 'src/irx#bif.c'  'src/irx#bifs.c' \        */
/*        'src/irx#io.c'   'src/irx#lstr.c' 'src/irx#tokn.c' \        */
/*        'src/irx#vpol.c' 'src/irx#pars.c' 'src/irx#ctrl.c' \        */
/*        'src/irx#arith.c' 'src/irx#exec.c' \                         */
/*        ../lstring370/src/lstr#*.c                                   */
/*                                                                    */
/*  Verifies: add/lookup/not-found/duplicate handling; correct         */
/*  min/max arg enforcement; lifecycle via irxinit/irxterm.            */
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

/* Dummy handler — never invoked by these tests. */
static int dummy_handler(struct irx_parser *p, int argc, PLstr *argv,
                         PLstr result)
{
    (void)p;
    (void)argc;
    (void)argv;
    (void)result;
    return 0;
}

/* ------------------------------------------------------------------ */

static void test_create_destroy(void)
{
    struct envblock *env = NULL;
    struct irx_bif_registry *reg = NULL;

    printf("\n--- BIF#1: create + destroy ---\n");
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    CHECK(irx_bif_create(env, &reg) == IRX_BIF_OK, "create returned OK");
    CHECK(reg != NULL, "registry allocated");
    irx_bif_destroy(env, reg);
    irxterm(env);
}

static void test_register_and_lookup(void)
{
    struct envblock *env = NULL;
    struct irx_bif_registry *reg = NULL;
    const struct irx_bif_entry *e;
    const unsigned char foo[] = {'F', 'O', 'O'};

    printf("\n--- BIF#2: register + lookup ---\n");
    irxinit(NULL, &env);
    irx_bif_create(env, &reg);

    CHECK(irx_bif_register(env, reg, "FOO", 1, 3, dummy_handler) ==
              IRX_BIF_OK,
          "register FOO");

    e = irx_bif_find(reg, foo, 3);
    CHECK(e != NULL, "find FOO");
    CHECK(e != NULL && strcmp(e->name, "FOO") == 0, "name matches");
    CHECK(e != NULL && e->min_args == 1, "min_args = 1");
    CHECK(e != NULL && e->max_args == 3, "max_args = 3");
    CHECK(e != NULL && e->handler == dummy_handler, "handler pointer");

    irx_bif_destroy(env, reg);
    irxterm(env);
}

static void test_not_found(void)
{
    struct envblock *env = NULL;
    struct irx_bif_registry *reg = NULL;
    const unsigned char bar[] = {'B', 'A', 'R'};

    printf("\n--- BIF#3: not found ---\n");
    irxinit(NULL, &env);
    irx_bif_create(env, &reg);

    CHECK(irx_bif_find(reg, bar, 3) == NULL, "find BAR returns NULL");

    irx_bif_destroy(env, reg);
    irxterm(env);
}

static void test_duplicate(void)
{
    struct envblock *env = NULL;
    struct irx_bif_registry *reg = NULL;

    printf("\n--- BIF#4: duplicate rejection ---\n");
    irxinit(NULL, &env);
    irx_bif_create(env, &reg);

    CHECK(irx_bif_register(env, reg, "DUP", 0, 0, dummy_handler) ==
              IRX_BIF_OK,
          "register DUP");
    CHECK(irx_bif_register(env, reg, "DUP", 0, 0, dummy_handler) ==
              IRX_BIF_DUPLICATE,
          "second register returns DUPLICATE");

    irx_bif_destroy(env, reg);
    irxterm(env);
}

static void test_bad_args(void)
{
    struct envblock *env = NULL;
    struct irx_bif_registry *reg = NULL;

    printf("\n--- BIF#5: bad arguments ---\n");
    irxinit(NULL, &env);
    irx_bif_create(env, &reg);

    CHECK(irx_bif_register(env, reg, NULL, 0, 0, dummy_handler) ==
              IRX_BIF_BADARG,
          "NULL name rejected");
    CHECK(irx_bif_register(env, reg, "", 0, 0, dummy_handler) ==
              IRX_BIF_BADARG,
          "empty name rejected");
    CHECK(irx_bif_register(env, reg, "X", 2, 1, dummy_handler) ==
              IRX_BIF_BADARG,
          "max < min rejected");
    CHECK(irx_bif_register(env, reg, "X", -1, 0, dummy_handler) ==
              IRX_BIF_BADARG,
          "negative min rejected");
    CHECK(irx_bif_register(env, reg, "X", 0, 0, NULL) ==
              IRX_BIF_BADARG,
          "NULL handler rejected");
    CHECK(irx_bif_find(reg, NULL, 1) == NULL, "NULL name lookup");
    CHECK(irx_bif_find(reg, (const unsigned char *)"X", 0) == NULL,
          "zero-length lookup");

    irx_bif_destroy(env, reg);
    irxterm(env);
}

static void test_table_registration(void)
{
    struct envblock *env = NULL;
    struct irx_bif_registry *reg = NULL;
    const struct irx_bif_entry table[] = {
        {"A", 0, 0, dummy_handler},
        {"B", 1, 2, dummy_handler},
        {"C", 0, 3, dummy_handler},
        {"", 0, 0, NULL},
    };
    const int count = (int)(sizeof(table) / sizeof(table[0]));

    printf("\n--- BIF#6: bulk register via table ---\n");
    irxinit(NULL, &env);
    irx_bif_create(env, &reg);

    CHECK(irx_bif_register_table(env, reg, table, count) == IRX_BIF_OK,
          "bulk register OK");
    CHECK(irx_bif_find(reg, (const unsigned char *)"A", 1) != NULL,
          "A registered");
    CHECK(irx_bif_find(reg, (const unsigned char *)"B", 1) != NULL,
          "B registered");
    CHECK(irx_bif_find(reg, (const unsigned char *)"C", 1) != NULL,
          "C registered");

    irx_bif_destroy(env, reg);
    irxterm(env);
}

static void test_core_bifs_registered(void)
{
    /* After irxinit, LENGTH, ARG, and the full string-BIF set should
     * be reachable through the per-environment registry. */
    struct envblock *env = NULL;
    struct irx_wkblk_int *wk;
    struct irx_bif_registry *reg;

    printf("\n--- BIF#7: irxinit registers core BIFs ---\n");
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    CHECK(wk != NULL && wk->wkbi_bif_registry != NULL,
          "wkbi_bif_registry populated");
    reg = (struct irx_bif_registry *)wk->wkbi_bif_registry;
    CHECK(irx_bif_find(reg, (const unsigned char *)"LENGTH", 6) != NULL,
          "LENGTH is registered");
    CHECK(irx_bif_find(reg, (const unsigned char *)"ARG", 3) != NULL,
          "ARG is registered");
    CHECK(irx_bif_find(reg, (const unsigned char *)"SUBSTR", 6) != NULL,
          "SUBSTR is registered");
    CHECK(irx_bif_find(reg, (const unsigned char *)"XRANGE", 6) != NULL,
          "XRANGE is registered");
    irxterm(env);
}

int main(void)
{
    printf("=== WP-21a: BIF registry tests ===\n");

    test_create_destroy();
    test_register_and_lookup();
    test_not_found();
    test_duplicate();
    test_bad_args();
    test_table_registration();
    test_core_bifs_registered();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
