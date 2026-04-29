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
/*  T9: TSOFL=1 parmblock → IRXANCHR slot flags=0x40000000            */
/*  T10: TSOFL=0 parmblock → IRXANCHR slot flags=0x00000000           */
/*  T11: TSOFL=1 with non-NULL ECTENVBK → slot is overwritten         */
/*  T12: TSOFL=0 with non-NULL ECTENVBK → slot is untouched           */
/*  T13: Two stacked TSOFL=1 IRXINITs → slot points at the latest     */
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
static int tests_skipped = 0;

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
        CHECK(memcmp(pb->parmblock_version, PARMBLOCK_VERSION_0042, 4) == 0,
              "PARMBLOCK version is '0042' (rexx370 deviation, CON-4)");
        CHECK(pb->parmblock_subpool == 0, "default subpool is 0");

        irxterm(envblk);
    }
}

/* ------------------------------------------------------------------ */
/*  T3: IRXEXTE — COUNT set, default routine pointers installed       */
/*                                                                    */
/*  The C-core (irx_init_initenvb step 6) installs default routines    */
/*  for IRXUID, IRXMSGID, IRXINOUT (and the active-routine peers).    */
/*  Slots whose service is not yet implemented (IRXEXEC, IRXLOAD, …)  */
/*  remain NULL. T7 covers the same expectations through the compat  */
/*  wrapper irxinit().                                                */
/* ------------------------------------------------------------------ */

static void test_t3_irxexte_defaults(void)
{
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T3: IRXEXTE defaults ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);

    CHECK(rc == 0, "irx_init_initenvb returns 0");

    if (envblk != NULL && envblk->envblock_irxexte != NULL)
    {
        struct irxexte *exte = (struct irxexte *)envblk->envblock_irxexte;

        CHECK(exte->irxexte_entry_count == IRXEXTE_ENTRY_COUNT,
              "IRXEXTE entry count matches IRXEXTE_ENTRY_COUNT");
        CHECK(exte->irxuid != NULL,
              "C-core wired irxuid in IRXEXTE");
        CHECK(exte->userid_routine != NULL,
              "C-core wired userid_routine in IRXEXTE");
        CHECK(exte->irxmsgid != NULL,
              "C-core wired irxmsgid in IRXEXTE");
        CHECK(exte->msgid_routine != NULL,
              "C-core wired msgid_routine in IRXEXTE");
        CHECK(exte->irxinout != NULL,
              "C-core wired irxinout in IRXEXTE");
        CHECK(exte->io_routine != NULL,
              "C-core wired io_routine in IRXEXTE");
        /* Slots without an implementation yet stay NULL. */
        CHECK(exte->irxexec == NULL,
              "IRXEXTE: irxexec stays NULL (not yet implemented)");
        CHECK(exte->irxload == NULL,
              "IRXEXTE: irxload stays NULL (not yet implemented)");

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
            /* On the cross-compile host is_tso() returns 0 and no
             * caller_parmblock was supplied, so tso_flag=0. The default
             * init path produces a non-TSO slot with flags=0. T9/T10
             * cover the TSOFL-driven flag values explicitly. */
#ifdef __MVS__
            /* TSO foreground or batch — is_tso() may report either.
             * Verify the flag matches one of the two valid values. */
            CHECK(slot->flags == 0U ||
                      slot->flags == IRXANCHR_FLAG_TSO_ATTACHED,
                  "slot flag is 0 or TSO_ATTACHED");
#else
            CHECK(slot->flags == 0U,
                  "host non-TSO env: slot flags == 0");
#endif
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
/*  T9 / T10 helper: build a minimal valid PARMBLOCK with explicit    */
/*  TSOFL bit. Mask byte 0 MSB selects tsofl as caller-overridden;    */
/*  flag byte 0 MSB carries the desired value (1=TSO, 0=non-TSO).     */
/* ------------------------------------------------------------------ */

static void build_parmblock_with_tsofl(struct parmblock *pb, int tso)
{
    memset(pb, 0, sizeof(*pb));
    memcpy(pb->parmblock_id, PARMBLOCK_ID, 8);
    memcpy(pb->parmblock_version, PARMBLOCK_VERSION_0042, 4);
    /* Use the bit-field accessors so layout differences between the
     * MVS toolchain (MSB-first) and the cross-compile host (LSB-first)
     * do not silently misroute the bit. The fields are signed 1-bit
     * bit-fields, so -1 is the only representable "true" value (matches
     * the convention in test/mvs/tstfind.c). */
    pb->tsofl_mask = -1;
    pb->tsofl = tso ? -1 : 0;
    memset(pb->parmblock_addrspn, ' ', 8);
    memset(pb->parmblock_ffff, 0xFF, 8);
}

/* ------------------------------------------------------------------ */
/*  T9: TSOFL=1 → IRXANCHR slot flags == 0x40000000                   */
/* ------------------------------------------------------------------ */

static void test_t9_tsofl_set_flag(void)
{
    struct parmblock pb;
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T9: TSOFL=1 → IRXANCHR slot TSO_ATTACHED ---\n");

    irx_anchor_table_reset();
    build_parmblock_with_tsofl(&pb, /*tso=*/1);

    rc = irx_init_initenvb(NULL, &pb, 0, &envblk, &reason);
    CHECK(rc == 0, "irx_init_initenvb returns 0");

    if (envblk != NULL)
    {
        irxanchr_entry_t *slot = irx_anchor_find_by_envblock(envblk);
        CHECK(slot != NULL, "T9: slot found");
        if (slot != NULL)
        {
            CHECK(slot->flags == IRXANCHR_FLAG_TSO_ATTACHED,
                  "T9: TSOFL=1 → flags == 0x40000000");
        }

        irxterm(envblk);
    }
}

/* ------------------------------------------------------------------ */
/*  T10: TSOFL=0 explicit → IRXANCHR slot flags == 0x00000000         */
/* ------------------------------------------------------------------ */

static void test_t10_tsofl_clear_flag(void)
{
    struct parmblock pb;
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T10: TSOFL=0 → IRXANCHR slot non-TSO ---\n");

    irx_anchor_table_reset();
    build_parmblock_with_tsofl(&pb, /*tso=*/0);

    rc = irx_init_initenvb(NULL, &pb, 0, &envblk, &reason);
    CHECK(rc == 0, "irx_init_initenvb returns 0");

    if (envblk != NULL)
    {
        irxanchr_entry_t *slot = irx_anchor_find_by_envblock(envblk);
        CHECK(slot != NULL, "T10: slot found");
        if (slot != NULL)
        {
            CHECK(slot->flags == 0U,
                  "T10: TSOFL=0 → flags == 0x00000000");
        }

        irxterm(envblk);
    }
}

/* ------------------------------------------------------------------ */
/*  Sentinel for T11–T13: any value that cannot collide with a real   */
/*  ENVBLOCK address returned by the host allocator.                  */
/* ------------------------------------------------------------------ */

static struct envblock *const ECT_SENTINEL =
    (struct envblock *)(unsigned long)0xDEADBEEFUL;

/* ------------------------------------------------------------------ */
/*  T11–T13 share a slot-reachability gate: pure-batch MVS reaches    */
/*  IRXINIT through tstall.jcl with no LWA (and therefore no ECT),    */
/*  so ectenvbk_slot() returns NULL. The slot-state assertions below  */
/*  cannot hold in that mode — emit SKIP and return early instead of  */
/*  failing. Host and TSO-batch (IKJEFT01) always see a reachable     */
/*  slot, so the assertions execute as before.                        */
/* ------------------------------------------------------------------ */

static int slot_reachable_or_skip(const char *test_label)
{
    if (ectenvbk_slot() != NULL)
    {
        return 1;
    }
    printf("  SKIP: %s (ECTENVBK slot unreachable — pure batch)\n",
           test_label);
    tests_skipped++;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  T11: TSOFL=1 with a non-NULL ECTENVBK → slot is overwritten       */
/*                                                                    */
/*  IRXPROBE Phase α (CON-14 case A1) verified IBM writes ECTENVBK    */
/*  unconditionally on TSOFL=1, regardless of the prior slot value.   */
/*  This test pins the same behaviour for the irxinit() compat        */
/*  wrapper (which exercises the host simulation slot).               */
/* ------------------------------------------------------------------ */

static void test_t11_tsofl1_overwrites_slot(void)
{
    struct parmblock pb;
    struct envblock *envblk = NULL;
    int rc;

    printf("\n--- T11: TSOFL=1 overwrites non-NULL ECTENVBK ---\n");

    irx_anchor_table_reset();
    if (!slot_reachable_or_skip("T11"))
    {
        return;
    }

    struct envblock **slot = ectenvbk_slot();
    *slot = ECT_SENTINEL;
    CHECK(*slot == ECT_SENTINEL, "T11: pre-seed: slot holds sentinel");

    build_parmblock_with_tsofl(&pb, /*tso=*/1);
    rc = irxinit(&pb, &envblk);
    CHECK(rc == 0, "T11: irxinit returns 0");
    CHECK(envblk != NULL, "T11: irxinit produced an ENVBLOCK");

    if (envblk != NULL)
    {
        CHECK(*slot == envblk, "T11: slot was overwritten with new env");
        CHECK(*slot != ECT_SENTINEL,
              "T11: sentinel is gone (unconditional overwrite)");
        irxterm(envblk);
        /* Caller-side cleanup so later tests start clean. */
        *slot = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  T12: TSOFL=0 with a non-NULL ECTENVBK → slot is untouched         */
/*                                                                    */
/*  IRXPROBE Phase α (CON-14 case A3) verified IBM leaves ECTENVBK    */
/*  alone on TSOFL=0 IRXINIT. The env is registered in IRXANCHR but   */
/*  not bound to ECTENVBK.                                            */
/* ------------------------------------------------------------------ */

static void test_t12_tsofl0_leaves_slot(void)
{
    struct parmblock pb;
    struct envblock *envblk = NULL;
    int rc;

    printf("\n--- T12: TSOFL=0 leaves ECTENVBK untouched ---\n");

    irx_anchor_table_reset();
    if (!slot_reachable_or_skip("T12"))
    {
        return;
    }

    struct envblock **slot = ectenvbk_slot();
    *slot = ECT_SENTINEL;

    build_parmblock_with_tsofl(&pb, /*tso=*/0);
    rc = irxinit(&pb, &envblk);
    CHECK(rc == 0, "T12: irxinit returns 0");
    CHECK(envblk != NULL, "T12: irxinit produced an ENVBLOCK");

    if (envblk != NULL)
    {
        CHECK(*slot == ECT_SENTINEL,
              "T12: slot still sentinel (TSOFL=0 no-op)");
        CHECK(*slot != envblk, "T12: slot does NOT point at the new env");
        irxterm(envblk);
        *slot = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  T13: Two stacked TSOFL=1 IRXINITs → slot tracks the latest        */
/*                                                                    */
/*  Each TSOFL=1 IRXINIT performs an unconditional overwrite, so the  */
/*  most recent caller wins — there is no "first claimant" guard.     */
/* ------------------------------------------------------------------ */

static void test_t13_stacked_tsofl1_latest_wins(void)
{
    struct parmblock pb;
    struct envblock *first = NULL;
    struct envblock *second = NULL;
    int rc;

    printf("\n--- T13: stacked TSOFL=1 IRXINITs — latest wins ---\n");

    irx_anchor_table_reset();
    if (!slot_reachable_or_skip("T13"))
    {
        return;
    }

    struct envblock **slot = ectenvbk_slot();
    *slot = NULL;

    build_parmblock_with_tsofl(&pb, /*tso=*/1);

    rc = irxinit(&pb, &first);
    CHECK(rc == 0 && first != NULL, "T13: first irxinit succeeded");
    CHECK(*slot == first, "T13: slot holds first env after first irxinit");

    rc = irxinit(&pb, &second);
    CHECK(rc == 0 && second != NULL, "T13: second irxinit succeeded");
    CHECK(second != first, "T13: second env is distinct from first");
    CHECK(*slot == second,
          "T13: slot holds second env (latest IRXINIT wins)");
    CHECK(*slot != first, "T13: first env is no longer in the slot");

    if (second != NULL)
    {
        irxterm(second);
    }
    if (first != NULL)
    {
        irxterm(first);
    }
    *slot = NULL;
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("TSTINIT: WP-I1c.1 IRXINIT INITENVB C-Core\n");

    test_t1_basic_envblock();
    test_t2_parmblock_copy();
    test_t3_irxexte_defaults();
    test_t4_anchor_slot_alloc();
    test_t5_two_concurrent_envs();
    test_t6_irxterm_after_initenvb();
    test_t7_irxinit_compat_wrapper();
    test_t8_bad_prev_eyecatcher();
    test_t9_tsofl_set_flag();
    test_t10_tsofl_clear_flag();
    test_t11_tsofl1_overwrites_slot();
    test_t12_tsofl0_leaves_slot();
    test_t13_stacked_tsofl1_latest_wins();

    printf("\n--- Results ---\n");
    printf("  Run:     %d\n", tests_run);
    printf("  Passed:  %d\n", tests_passed);
    printf("  Failed:  %d\n", tests_failed);
    printf("  Skipped: %d\n", tests_skipped);

    if (tests_failed > 0)
    {
        printf("TSTINIT FAILED\n");
        return 1;
    }

    printf("TSTINIT PASSED\n");
    return 0;
}
