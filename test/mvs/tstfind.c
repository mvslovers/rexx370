/* ------------------------------------------------------------------ */
/*  tstfind.c - WP-I1c.2 IRXINIT FINDENVB + CHEKENVB tests           */
/*                                                                    */
/*  Tests irx_init_findenvb() and irx_init_chekenvb().                */
/*  Also exercises irx_init_dispatch() for both function codes.       */
/*                                                                    */
/*  Test cases:                                                       */
/*                                                                    */
/*  FINDENVB:                                                         */
/*  F1: Empty anchor table — RC=4, RSN=4                              */
/*  F2: One non-reentrant env on current TCB — RC=0, correct envblk  */
/*  F3: One reentrant env on current TCB — RC=4, RSN=4               */
/*  F4: Two non-reentrant envs on same TCB — returns highest token   */
/*  F5: via irx_init_dispatch("FINDENVB") — same result              */
/*                                                                    */
/*  Note: F2 and F4 require a real PSATOLD (MVS-only). On the host   */
/*  cross-compile, the TCB address is 0 and all irx_init_initenvb()  */
/*  slots record tcb_ptr=0, so the inline iteration in findenvb       */
/*  treats both as matching — the host tests exercise the same code  */
/*  path but without the MVS task-isolation guarantee.               */
/*                                                                    */
/*  CHEKENVB:                                                         */
/*  C1: NULL envblock — RC=20, RSN=4                                  */
/*  C2: Valid envblock in IRXANCHR — RC=0                             */
/*  C3: Stack envblock with bad eye-catcher — RC=20, RSN=4           */
/*  C4: Stack envblock with correct eye-catcher, not in IRXANCHR —   */
/*      RC=20, RSN=8                                                  */
/*  C5: via irx_init_dispatch("CHEKENVB") — same result as direct    */
/*                                                                    */
/*  Cross-compile build:                                              */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 \                                  */
/*        -o /tmp/tstfind test/mvs/tstfind.c $ALL_SRC                 */
/*  (see CLAUDE.md for $ALL_SRC definition)                           */
/*                                                                    */
/*  MVS invocation:                                                   */
/*    TSO   :  CALL 'hlq.LOAD(TSTFIND)'                               */
/*    Batch :  EXEC PGM=TSTFIND                                       */
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

/* Simulated ECTENVBK slot for host cross-compile tests. */
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

/* ================================================================== */
/*  FINDENVB tests                                                     */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/*  F1: Empty anchor table — RC=4, RSN=4                              */
/* ------------------------------------------------------------------ */

static void test_f1_empty_table(void)
{
    struct envblock *found = NULL;
    int reason = -1;
    int rc;

    printf("\n--- F1: findenvb — empty anchor table ---\n");

    irx_anchor_table_reset();

    rc = irx_init_findenvb(&found, &reason);

    CHECK(rc == 4, "findenvb returns 4 when table is empty");
    CHECK(found == NULL, "out_envblock is NULL when not found");
    CHECK(reason == 4, "reason code is 4 when not found");
}

/* ------------------------------------------------------------------ */
/*  F2: One non-reentrant env — RC=0, correct envblock returned        */
/*                                                                    */
/*  On MVS: the slot's tcb_ptr is PSATOLD; findenvb reads PSATOLD    */
/*  and finds the match.                                              */
/*  On host: tcb_ptr=0 and PSATOLD reads as 0; the same comparison   */
/*  matches, exercising the same code path.                           */
/* ------------------------------------------------------------------ */

static void test_f2_nonreentrant_env(void)
{
    struct envblock *envblk = NULL;
    struct envblock *found = NULL;
    int reason = -1;
    int rc;

    printf("\n--- F2: findenvb — one non-reentrant env ---\n");

    irx_anchor_table_reset();
#ifndef __MVS__
    {
        struct envblock **slot = ectenvbk_slot();
        if (slot != NULL)
        {
            *slot = NULL;
        }
    }
#endif

    /* Create a non-reentrant environment (rentrant flag = 0 by default). */
    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: irx_init_initenvb failed — cannot run F2\n");
        return;
    }

    found = NULL;
    reason = -1;
    rc = irx_init_findenvb(&found, &reason);

    CHECK(rc == 0, "findenvb returns 0 for non-reentrant env on current TCB");
    CHECK(found == envblk, "findenvb returns the correct envblock");
    CHECK(reason == 0, "reason code is 0 on success");

    irxterm(envblk);
}

/* ------------------------------------------------------------------ */
/*  F3: One reentrant env on current TCB — RC=4, RSN=4               */
/* ------------------------------------------------------------------ */

static void test_f3_reentrant_env(void)
{
    struct parmblock caller_pb;
    struct envblock *envblk = NULL;
    struct envblock *found = NULL;
    int reason = -1;
    int rc;

    printf("\n--- F3: findenvb — reentrant env skipped ---\n");

    irx_anchor_table_reset();
#ifndef __MVS__
    {
        struct envblock **slot = ectenvbk_slot();
        if (slot != NULL)
        {
            *slot = NULL;
        }
    }
#endif

    /* Build a PARMBLOCK that sets rentrant=1 with the corresponding mask. */
    memset(&caller_pb, 0, sizeof(caller_pb));
    memcpy(caller_pb.parmblock_id, PARMBLOCK_ID, 8);
    memcpy(caller_pb.parmblock_version, PARMBLOCK_VERSION_0042, 4);
    /* Signed 1-bit bit-field: -1 is "true" (only non-zero value). */
    caller_pb.rentrant = -1;
    caller_pb.rentrant_mask = -1;

    rc = irx_init_initenvb(NULL, &caller_pb, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: irx_init_initenvb failed — cannot run F3\n");
        return;
    }

    found = NULL;
    reason = -1;
    rc = irx_init_findenvb(&found, &reason);

    CHECK(rc == 4, "findenvb returns 4 when only reentrant env is present");
    CHECK(found == NULL, "out_envblock is NULL for reentrant-only table");
    CHECK(reason == 4, "reason code is 4 (not found)");

    irxterm(envblk);
}

/* ------------------------------------------------------------------ */
/*  F4: Two non-reentrant envs — returns the one with higher token    */
/* ------------------------------------------------------------------ */

static void test_f4_highest_token_returned(void)
{
    struct envblock *env1 = NULL;
    struct envblock *env2 = NULL;
    struct envblock *found = NULL;
    int reason = -1;
    int rc;

    printf("\n--- F4: findenvb — highest-token env returned ---\n");

    irx_anchor_table_reset();
#ifndef __MVS__
    {
        struct envblock **slot = ectenvbk_slot();
        if (slot != NULL)
        {
            *slot = NULL;
        }
    }
#endif

    rc = irx_init_initenvb(NULL, NULL, 0, &env1, &reason);
    if (rc != 0 || env1 == NULL)
    {
        printf("  SKIP: env1 creation failed — cannot run F4\n");
        return;
    }

    rc = irx_init_initenvb(NULL, NULL, 0, &env2, &reason);
    if (rc != 0 || env2 == NULL)
    {
        printf("  SKIP: env2 creation failed — cannot run F4\n");
        irxterm(env1);
        return;
    }

    found = NULL;
    reason = -1;
    rc = irx_init_findenvb(&found, &reason);

    CHECK(rc == 0, "findenvb returns 0 with two non-reentrant envs");
    /* env2 was allocated last → higher token → must be returned. */
    CHECK(found == env2, "findenvb returns the most recently allocated env");

    irxterm(env2);
    irxterm(env1);
}

/* ------------------------------------------------------------------ */
/*  F5: irx_init_dispatch("FINDENVB") — same result as direct call    */
/* ------------------------------------------------------------------ */

static void test_f5_dispatch_findenvb(void)
{
    struct envblock *envblk = NULL;
    struct envblock *found = NULL;
    int reason = -1;
    int rc;

    printf("\n--- F5: dispatch FINDENVB ---\n");

    irx_anchor_table_reset();
#ifndef __MVS__
    {
        struct envblock **slot = ectenvbk_slot();
        if (slot != NULL)
        {
            *slot = NULL;
        }
    }
#endif

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: irx_init_initenvb failed — cannot run F5\n");
        return;
    }

    found = NULL;
    reason = -1;
    rc = irx_init_dispatch("FINDENVB", NULL, NULL, 0, &found, &reason);

    CHECK(rc == 0, "dispatch FINDENVB returns 0");
    CHECK(found == envblk, "dispatch FINDENVB returns correct envblock");
    CHECK(reason == 0, "dispatch FINDENVB reason is 0");

    irxterm(envblk);
}

/* ================================================================== */
/*  CHEKENVB tests                                                     */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/*  C1: NULL envblock — RC=20, RSN=4                                  */
/* ------------------------------------------------------------------ */

static void test_c1_null_envblock(void)
{
    int reason = -1;
    int rc;

    printf("\n--- C1: chekenvb — NULL envblock ---\n");

    irx_anchor_table_reset();

    rc = irx_init_chekenvb(NULL, &reason);

    CHECK(rc == 20, "chekenvb returns 20 for NULL envblock");
    CHECK(reason == 4, "reason code is 4 for NULL (bad eye-catcher)");
}

/* ------------------------------------------------------------------ */
/*  C2: Valid envblock in IRXANCHR — RC=0                             */
/* ------------------------------------------------------------------ */

static void test_c2_valid_envblock(void)
{
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- C2: chekenvb — valid registered envblock ---\n");

    irx_anchor_table_reset();
#ifndef __MVS__
    {
        struct envblock **slot = ectenvbk_slot();
        if (slot != NULL)
        {
            *slot = NULL;
        }
    }
#endif

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: irx_init_initenvb failed — cannot run C2\n");
        return;
    }

    reason = -1;
    rc = irx_init_chekenvb(envblk, &reason);

    CHECK(rc == 0, "chekenvb returns 0 for valid registered envblock");
    CHECK(reason == 0, "reason code is 0 on success");

    irxterm(envblk);
}

/* ------------------------------------------------------------------ */
/*  C3: Stack envblock with wrong eye-catcher — RC=20, RSN=4          */
/* ------------------------------------------------------------------ */

static void test_c3_bad_eyecatcher(void)
{
    struct envblock fake;
    int reason = -1;
    int rc;

    printf("\n--- C3: chekenvb — bad eye-catcher ---\n");

    irx_anchor_table_reset();

    memset(&fake, 0, sizeof(fake));
    memcpy(fake.envblock_id, "GARBAGE!", 8);

    rc = irx_init_chekenvb(&fake, &reason);

    CHECK(rc == 20, "chekenvb returns 20 for bad eye-catcher");
    CHECK(reason == 4, "reason code is 4 for bad eye-catcher");
}

/* ------------------------------------------------------------------ */
/*  C4: Correct eye-catcher but not in IRXANCHR — RC=20, RSN=8        */
/*                                                                    */
/*  Use a stack-allocated envblock with the correct eye-catcher       */
/*  that was never registered via irx_anchor_alloc_slot.             */
/* ------------------------------------------------------------------ */

static void test_c4_not_in_anchor(void)
{
    struct envblock unregistered;
    int reason = -1;
    int rc;

    printf("\n--- C4: chekenvb — correct eye-catcher, not in IRXANCHR ---\n");

    irx_anchor_table_reset();

    memset(&unregistered, 0, sizeof(unregistered));
    memcpy(unregistered.envblock_id, ENVBLOCK_ID, 8);
    memcpy(unregistered.envblock_version, ENVBLOCK_VERSION_0042, 4);

    rc = irx_init_chekenvb(&unregistered, &reason);

    CHECK(rc == 20, "chekenvb returns 20 when not in IRXANCHR");
    CHECK(reason == 8, "reason code is 8 (not registered)");
}

/* ------------------------------------------------------------------ */
/*  C5: irx_init_dispatch("CHEKENVB") — same result as direct call    */
/* ------------------------------------------------------------------ */

static void test_c5_dispatch_chekenvb(void)
{
    struct envblock fake;
    struct envblock *eb;
    int reason = -1;
    int rc;

    printf("\n--- C5: dispatch CHEKENVB ---\n");

    irx_anchor_table_reset();

    memset(&fake, 0, sizeof(fake));
    memcpy(fake.envblock_id, "GARBAGE!", 8);
    eb = &fake;

    /* Pass the bad-eye-catcher case through the dispatcher. */
    rc = irx_init_dispatch("CHEKENVB", NULL, NULL, 0, &eb, &reason);

    CHECK(rc == 20, "dispatch CHEKENVB returns 20 for bad eye-catcher");
    CHECK(reason == 4, "dispatch CHEKENVB reason is 4");
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

int main(void)
{
    printf("TSTFIND: WP-I1c.2 IRXINIT FINDENVB + CHEKENVB\n");

    test_f1_empty_table();
    test_f2_nonreentrant_env();
    test_f3_reentrant_env();
    test_f4_highest_token_returned();
    test_f5_dispatch_findenvb();

    test_c1_null_envblock();
    test_c2_valid_envblock();
    test_c3_bad_eyecatcher();
    test_c4_not_in_anchor();
    test_c5_dispatch_chekenvb();

    printf("\n--- Results ---\n");
    printf("  Run:    %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);

    if (tests_failed > 0)
    {
        printf("TSTFIND FAILED\n");
        return 1;
    }

    printf("TSTFIND PASSED\n");
    return 0;
}
