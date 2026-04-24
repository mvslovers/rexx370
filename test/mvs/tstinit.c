/* ------------------------------------------------------------------ */
/*  tstinit.c - WP-I1c.1 IRXINIT INITENVB C-Core Tests               */
/*                                                                    */
/*  Tests irx_init_initenvb() (9-step C-core) and the irxinit()       */
/*  compatibility wrapper.                                            */
/*                                                                    */
/*  Test cases:                                                       */
/*  T1: Basic call — ENVBLOCK eye-catcher + version '0042'            */
/*  T2: PARMBLOCK copy — eye-catcher and defaults                     */
/*  T3: IRXEXTE placeholder — COUNT set, routine slots zero           */
/*  T4: IRXANCHR slot — find_by_envblock returns slot for new env     */
/*  T5: Two concurrent envs — distinct ENVBLOCKs, distinct slots      */
/*  T6: irxterm after irx_init_initenvb — returns 0                   */
/*  T7: irxinit() compat wrapper — IRXEXTE fully wired (irxuid != 0)  */
/*  T8: Bad prev_envblock eye-catcher — step 1 skip, falls through    */
/*                                                                    */
/*  Cross-compile build:                                              */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99                                    */
/*        -o /tmp/tstinit test/mvs/tstinit.c $ALL_SRC                 */
/*  (see CLAUDE.md for $ALL_SRC definition)                           */
/*                                                                    */
/*  MVS invocation:                                                   */
/*    TSO   :  CALL 'hlq.LOAD(TSTINIT)'                               */
/*    Batch :  EXEC PGM=TSTINIT                                       */
/*  Expected: CC=0                                                    */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irx_init.h"
#include "irxanchr.h"
#include "irxfunc.h"
#include "irxwkblk.h"

/* Simulated ECTENVBK slot for host cross-compile tests.
 * irx#anch.c references this extern on non-MVS builds. */
#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

/* Forward-declare the slot accessor from irx#anch.c. */
struct envblock **ectenvbk_slot(void);

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
/*  T1: Basic call — ENVBLOCK eye-catcher + version '0042'            */
/* ------------------------------------------------------------------ */

static void test_t1_basic_envblock(void)
{
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T1: basic ENVBLOCK fields ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);

    CHECK(rc == 0, "irx_init_initenvb returns 0");
    CHECK(envblk != NULL, "out_envblock is non-NULL");
    CHECK(reason == 0, "reason_code is 0");

    if (envblk != NULL)
    {
        CHECK(memcmp(envblk->envblock_id, ENVBLOCK_ID, 8) == 0,
              "ENVBLOCK eye-catcher is 'ENVBLOCK'");
        CHECK(memcmp(envblk->envblock_version, ENVBLOCK_VERSION_0042, 4) == 0,
              "VERSION field is '0042' (rexx370 deviation, CON-4)");
        CHECK(memcmp(envblk->envblock_version, ENVBLOCK_VERSION_0100, 4) != 0,
              "VERSION field is NOT '0100' (not IBM default)");
        CHECK(envblk->envblock_length == (int)sizeof(struct envblock),
              "envblock_length matches sizeof(struct envblock)");
        CHECK(envblk->envblock_parmblock != NULL, "envblock_parmblock is set");
        CHECK(envblk->envblock_irxexte != NULL, "envblock_irxexte is set");

        /* Cleanup — irxterm handles storage; slot already allocated */
        irxterm(envblk);
    }
}

/* ------------------------------------------------------------------ */
/*  T2: PARMBLOCK copy — eye-catcher and defaults                     */
/* ------------------------------------------------------------------ */

static void test_t2_parmblock_copy(void)
{
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T2: PARMBLOCK copy ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);

    CHECK(rc == 0, "irx_init_initenvb returns 0");

    if (envblk != NULL && envblk->envblock_parmblock != NULL)
    {
        struct parmblock *pb = (struct parmblock *)envblk->envblock_parmblock;

        CHECK(memcmp(pb->parmblock_id, PARMBLOCK_ID, 8) == 0,
              "PARMBLOCK eye-catcher is 'IRXPARMS'");
        CHECK(memcmp(pb->parmblock_version, PARMBLOCK_VERSION_0200, 4) == 0,
              "PARMBLOCK version is '0200'");
        CHECK(pb->parmblock_subpool == 0, "default subpool is 0");

        irxterm(envblk);
    }
}

/* ------------------------------------------------------------------ */
/*  T3: IRXEXTE placeholder — COUNT set, all routine slots NULL       */
/* ------------------------------------------------------------------ */

static void test_t3_irxexte_placeholder(void)
{
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T3: IRXEXTE placeholder ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);

    CHECK(rc == 0, "irx_init_initenvb returns 0");

    if (envblk != NULL && envblk->envblock_irxexte != NULL)
    {
        struct irxexte *exte = (struct irxexte *)envblk->envblock_irxexte;

        CHECK(exte->irxexte_entry_count == IRXEXTE_ENTRY_COUNT,
              "IRXEXTE entry count matches IRXEXTE_ENTRY_COUNT");
        /* Placeholder: all routine pointers are NULL */
        CHECK(exte->irxuid == NULL,
              "placeholder IRXEXTE: irxuid is NULL");
        CHECK(exte->irxmsgid == NULL,
              "placeholder IRXEXTE: irxmsgid is NULL");
        CHECK(exte->irxinout == NULL,
              "placeholder IRXEXTE: irxinout is NULL");

        irxterm(envblk);
    }
}

/* ------------------------------------------------------------------ */
/*  T4: IRXANCHR slot — irx_anchor_find_by_envblock returns the slot  */
/* ------------------------------------------------------------------ */

static void test_t4_anchor_slot_alloc(void)
{
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T4: IRXANCHR slot allocation ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);

    CHECK(rc == 0, "irx_init_initenvb returns 0");

    if (envblk != NULL)
    {
        irxanchr_entry_t *slot = irx_anchor_find_by_envblock(envblk);

        CHECK(slot != NULL, "irx_anchor_find_by_envblock returns non-NULL slot");
        if (slot != NULL)
        {
            CHECK((slot->flags & IRXANCHR_FLAG_IN_USE) != 0,
                  "slot flag IN_USE is set");
        }

        irxterm(envblk);
    }
}

/* ------------------------------------------------------------------ */
/*  T5: Two concurrent envs — distinct ENVBLOCKs, distinct slots      */
/* ------------------------------------------------------------------ */

static void test_t5_two_concurrent_envs(void)
{
    struct envblock *env1 = NULL;
    struct envblock *env2 = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T5: two concurrent environments ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &env1, &reason);
    CHECK(rc == 0 && env1 != NULL, "env1 created");

    rc = irx_init_initenvb(NULL, NULL, 0, &env2, &reason);
    CHECK(rc == 0 && env2 != NULL, "env2 created");

    CHECK(env1 != env2, "env1 and env2 are distinct");

    if (env1 != NULL && env2 != NULL)
    {
        irxanchr_entry_t *slot1 = irx_anchor_find_by_envblock(env1);
        irxanchr_entry_t *slot2 = irx_anchor_find_by_envblock(env2);

        CHECK(slot1 != NULL, "slot1 found for env1");
        CHECK(slot2 != NULL, "slot2 found for env2");
        CHECK(slot1 != slot2, "env1 and env2 occupy distinct slots");
    }

    if (env1 != NULL)
    {
        irxterm(env1);
    }
    if (env2 != NULL)
    {
        irxterm(env2);
    }
}

/* ------------------------------------------------------------------ */
/*  T6: irxterm after irx_init_initenvb — returns 0                   */
/* ------------------------------------------------------------------ */

static void test_t6_irxterm_after_initenvb(void)
{
    struct envblock *envblk = NULL;
    int reason = -1;
    int term_rc;
    int rc;

    printf("\n--- T6: irxterm after irx_init_initenvb ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);
    CHECK(rc == 0 && envblk != NULL, "irx_init_initenvb succeeded");

    if (envblk != NULL)
    {
        term_rc = irxterm(envblk);
        CHECK(term_rc == 0, "irxterm returns 0 after irx_init_initenvb");
    }
}

/* ------------------------------------------------------------------ */
/*  T7: irxinit() compat wrapper — IRXEXTE fully wired                */
/* ------------------------------------------------------------------ */

static void test_t7_irxinit_compat_wrapper(void)
{
    struct envblock *envblk = NULL;
    int rc;

    printf("\n--- T7: irxinit() compat wrapper ---\n");

    irx_anchor_table_reset();
#ifndef __MVS__
    /* Reset host simulation slot for clean state. */
    {
        struct envblock **slot = ectenvbk_slot();
        if (slot != NULL)
        {
            *slot = NULL;
        }
    }
#endif

    rc = irxinit(NULL, &envblk);

    CHECK(rc == 0, "irxinit returns 0");
    CHECK(envblk != NULL, "irxinit produced an ENVBLOCK");

    if (envblk != NULL)
    {
        CHECK(memcmp(envblk->envblock_id, ENVBLOCK_ID, 8) == 0,
              "ENVBLOCK eye-catcher correct");
        CHECK(memcmp(envblk->envblock_version, ENVBLOCK_VERSION_0042, 4) == 0,
              "VERSION is '0042' from compat wrapper");

        if (envblk->envblock_irxexte != NULL)
        {
            struct irxexte *exte = (struct irxexte *)envblk->envblock_irxexte;
            /* Compat wrapper fills in real function pointers. */
            CHECK(exte->irxuid != NULL,
                  "compat wrapper wired irxuid in IRXEXTE");
            CHECK(exte->irxmsgid != NULL,
                  "compat wrapper wired irxmsgid in IRXEXTE");
            CHECK(exte->irxinout != NULL,
                  "compat wrapper wired irxinout in IRXEXTE");
        }

        /* Compat wrapper installs interpreter Work Block. */
        CHECK(envblk->envblock_userfield != NULL,
              "compat wrapper installed internal Work Block");

        rc = irxterm(envblk);
        CHECK(rc == 0, "irxterm returns 0 after irxinit");
    }
}

/* ------------------------------------------------------------------ */
/*  T8: Bad prev_envblock eye-catcher — step 1 skip, falls through    */
/*                                                                    */
/*  A prev_envblock with wrong eye-catcher must not crash;            */
/*  irx_init_initenvb() should skip it and use defaults.              */
/* ------------------------------------------------------------------ */

static void test_t8_bad_prev_eyecatcher(void)
{
    struct envblock fake_prev;
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T8: bad prev_envblock eye-catcher ---\n");

    irx_anchor_table_reset();

    memset(&fake_prev, 0, sizeof(fake_prev));
    memcpy(fake_prev.envblock_id, "GARBAGE!", 8); /* wrong eye-catcher */

    rc = irx_init_initenvb(&fake_prev, NULL, 0, &envblk, &reason);

    CHECK(rc == 0, "irx_init_initenvb returns 0 despite bad prev eye-catcher");
    CHECK(envblk != NULL, "ENVBLOCK allocated even with bad prev hint");

    if (envblk != NULL)
    {
        CHECK(memcmp(envblk->envblock_id, ENVBLOCK_ID, 8) == 0,
              "new ENVBLOCK has correct eye-catcher");
        CHECK(memcmp(envblk->envblock_version, ENVBLOCK_VERSION_0042, 4) == 0,
              "new ENVBLOCK has version '0042'");
        irxterm(envblk);
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("TSTINIT: WP-I1c.1 IRXINIT INITENVB C-Core\n");

    test_t1_basic_envblock();
    test_t2_parmblock_copy();
    test_t3_irxexte_placeholder();
    test_t4_anchor_slot_alloc();
    test_t5_two_concurrent_envs();
    test_t6_irxterm_after_initenvb();
    test_t7_irxinit_compat_wrapper();
    test_t8_bad_prev_eyecatcher();

    printf("\n--- Results ---\n");
    printf("  Run:    %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);

    if (tests_failed > 0)
    {
        printf("TSTINIT FAILED\n");
        return 1;
    }

    printf("TSTINIT PASSED\n");
    return 0;
}
