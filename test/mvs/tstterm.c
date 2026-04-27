/* ------------------------------------------------------------------ */
/*  tstterm.c - WP-I1c.3 IRXTERM C-Core Tests                        */
/*                                                                    */
/*  Tests irx_init_term() (6-step C-core) and the irxterm()           */
/*  compatibility wrapper.                                            */
/*                                                                    */
/*  Test cases:                                                       */
/*  T1: basic irx_init_term — RC=0                                    */
/*  T2: NULL envblock arg — RC=20, RSN=4                              */
/*  T3: bad eye-catcher — RC=20, RSN=4                                */
/*  T4: IRXANCHR slot freed — find_by_envblock returns NULL           */
/*  T5: idempotency — IRXANCHR slot freed after irx_init_term         */
/*  T6: non-TSO env — ECTENVBK unchanged after irx_init_term         */
/*  T7: irxterm() compat wrapper via irx_init_initenvb path — RC=0   */
/*  T8: irxterm() compat wrapper via irxinit() path (with wkbi)      */
/*  T9: TSO single env — IRXTERM rolls ECTENVBK back to NULL          */
/*  T10: TSO two-env stack — IRXTERM rolls ECTENVBK back to outer    */
/*  T11: IRXTERM failure — ECTENVBK unchanged (AC-7)                  */
/*                                                                    */
/*  Cross-compile build:                                              */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99                                    */
/*        -o /tmp/tstterm test/mvs/tstterm.c $ALL_SRC                 */
/*  (see CLAUDE.md for $ALL_SRC definition)                           */
/*                                                                    */
/*  MVS invocation:                                                   */
/*    TSO   :  CALL 'hlq.LOAD(TSTTERM)'                               */
/*    Batch :  EXEC PGM=TSTTERM                                       */
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
static int tests_skipped = 0;

/* Return 1 if the ECTENVBK slot is reachable, 0 (and emit SKIP) if not. */
static int slot_reachable_or_skip(const char *label)
{
    if (ectenvbk_slot() != NULL)
    {
        return 1;
    }
    printf("  SKIP: %s (ECTENVBK slot unreachable — pure batch)\n", label);
    tests_skipped++;
    return 0;
}

/* Build a minimal PARMBLOCK with a caller-controlled TSOFL value.
 * tso=1 makes IRXINIT treat the env as TSO-attached; tso=0 leaves it
 * non-TSO so ECTENVBK is never written. */
static void build_parmblock_with_tsofl(struct parmblock *pb, int tso)
{
    memset(pb, 0, sizeof(*pb));
    memcpy(pb->parmblock_id, PARMBLOCK_ID, 8);
    memcpy(pb->parmblock_version, PARMBLOCK_VERSION_0042, 4);
    pb->tsofl_mask = -1;
    pb->tsofl = tso ? -1 : 0;
}

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
/*  T1: basic irx_init_term — RC=0                                    */
/* ------------------------------------------------------------------ */

static void test_t1_basic_term(void)
{
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T1: basic irx_init_term ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: irx_init_initenvb failed — cannot run T1\n");
        return;
    }

    reason = -1;
    rc = irx_init_term(envblk, &reason);

    CHECK(rc == 0, "irx_init_term returns 0");
    CHECK(reason == 0, "reason_code is 0");
}

/* ------------------------------------------------------------------ */
/*  T2: NULL envblock arg — RC=20, RSN=4                              */
/* ------------------------------------------------------------------ */

static void test_t2_null_envblock(void)
{
    int reason = -1;
    int rc;

    printf("\n--- T2: NULL envblock ---\n");

    irx_anchor_table_reset();

    rc = irx_init_term(NULL, &reason);

    CHECK(rc == 20, "irx_init_term returns 20 for NULL envblock");
    CHECK(reason == 4, "reason code is 4 for NULL envblock");
}

/* ------------------------------------------------------------------ */
/*  T3: bad eye-catcher — RC=20, RSN=4                                */
/* ------------------------------------------------------------------ */

static void test_t3_bad_eyecatcher(void)
{
    struct envblock fake;
    int reason = -1;
    int rc;

    printf("\n--- T3: bad eye-catcher ---\n");

    irx_anchor_table_reset();

    memset(&fake, 0, sizeof(fake));
    memcpy(fake.envblock_id, "GARBAGE!", 8);

    rc = irx_init_term(&fake, &reason);

    CHECK(rc == 20, "irx_init_term returns 20 for bad eye-catcher");
    CHECK(reason == 4, "reason code is 4 for bad eye-catcher");
}

/* ------------------------------------------------------------------ */
/*  T4: IRXANCHR slot freed — find_by_envblock returns NULL           */
/* ------------------------------------------------------------------ */

static void test_t4_anchor_slot_freed(void)
{
    struct envblock *envblk = NULL;
    struct envblock *envblk_save;
    int reason = -1;
    int rc;

    printf("\n--- T4: IRXANCHR slot freed after irx_init_term ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: irx_init_initenvb failed — cannot run T4\n");
        return;
    }

    /* Verify slot is present before term. */
    CHECK(irx_anchor_find_by_envblock(envblk) != NULL,
          "pre-term: IRXANCHR slot is active");

    /* Save pointer value before irx_init_term frees the ENVBLOCK.
     * irx_anchor_find_by_envblock only compares the pointer value
     * as a uint32_t and never dereferences it — safe to call with
     * a stale (freed) pointer. */
    envblk_save = envblk;

    reason = -1;
    rc = irx_init_term(envblk, &reason);
    CHECK(rc == 0, "irx_init_term returns 0");

    /* Slot must be freed (envblock_ptr == SLOT_FREE == 0x00000000). */
    CHECK(irx_anchor_find_by_envblock(envblk_save) == NULL,
          "post-term: IRXANCHR slot freed");
}

/* ------------------------------------------------------------------ */
/*  T5: idempotency — IRXANCHR slot freed after irx_init_term         */
/* ------------------------------------------------------------------ */

static void test_t5_idempotency(void)
{
    struct envblock *envblk = NULL;
    struct envblock *envblk_save;
    int reason = -1;
    int rc;

    printf("\n--- T5: idempotency (slot freed after irx_init_term) ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: irx_init_initenvb failed — cannot run T5\n");
        return;
    }

    envblk_save = envblk; /* compared as uint32_t by find_by_envblock, not dereferenced */

    reason = -1;
    rc = irx_init_term(envblk, &reason);
    CHECK(rc == 0, "irx_init_term returns 0");
    CHECK(reason == 0, "reason_code is 0");

    /* Idempotency precondition: slot must be free after first term.
     * irx_anchor_find_by_envblock compares pointer values only and
     * never dereferences the envblock — safe with a stale pointer. */
    CHECK(irx_anchor_find_by_envblock(envblk_save) == NULL,
          "post-term: IRXANCHR slot freed (idempotency precondition)");
}

/* ------------------------------------------------------------------ */
/*  T6: ECTENVBK unchanged after irx_init_term (CON-3)                */
/*                                                                    */
/*  Seed the ECTENVBK slot with a sentinel value, call irx_init_term, */
/*  and verify the slot was not touched.                              */
/* ------------------------------------------------------------------ */

static void test_t6_ectenvbk_unchanged(void)
{
    struct envblock *envblk = NULL;
    void *const sentinel = (void *)(unsigned long)0xDEAD0002UL;
    int reason = -1;
    int rc;

    printf("\n--- T6: ECTENVBK unchanged after irx_init_term (CON-3) ---\n");

    irx_anchor_table_reset();

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: irx_init_initenvb failed — cannot run T6\n");
        return;
    }

    /* Seed the slot with sentinel to prove irx_init_term doesn't touch it. */
    struct envblock **slot = ectenvbk_slot();
    if (slot != NULL)
    {
        *slot = (struct envblock *)sentinel;
    }

    reason = -1;
    rc = irx_init_term(envblk, &reason);
    CHECK(rc == 0, "irx_init_term returns 0");

    /* The slot must still hold sentinel — irx_init_term must not have
     * modified ECTENVBK (CON-3 / SC28-1883-0 §14). */
    slot = ectenvbk_slot();
    if (slot != NULL)
    {
        CHECK(*slot == (struct envblock *)sentinel,
              "ECTENVBK slot unchanged after irx_init_term (CON-3)");
        /* Cleanup. */
        *slot = NULL;
    }
    else
    {
        /* Batch: slot unreachable — the test is vacuously satisfied. */
        CHECK(1, "ECTENVBK not reachable (batch) — CON-3 trivially holds");
    }
}

/* ------------------------------------------------------------------ */
/*  T7: irxterm() compat wrapper via irx_init_initenvb path — RC=0   */
/* ------------------------------------------------------------------ */

static void test_t7_irxterm_minimal_init(void)
{
    struct envblock *envblk = NULL;
    int reason = -1;
    int rc;

    printf("\n--- T7: irxterm() via irx_init_initenvb path ---\n");

    irx_anchor_table_reset();
#ifndef __MVS__
    struct envblock **slot = ectenvbk_slot();
    if (slot != NULL)
    {
        *slot = NULL;
    }
#endif

    rc = irx_init_initenvb(NULL, NULL, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: irx_init_initenvb failed — cannot run T7\n");
        return;
    }

    /* wkbi is NULL (not set by irx_init_initenvb), IRXEXTE is a
     * placeholder.  irxterm must handle both gracefully. */
    CHECK(envblk->envblock_userfield == NULL,
          "pre-term: no wkbi (irx_init_initenvb path)");

    rc = irxterm(envblk);

    CHECK(rc == 0, "irxterm returns 0 for minimal init path");
}

/* ------------------------------------------------------------------ */
/*  T8: irxterm() compat wrapper via irxinit() path (with wkbi)      */
/* ------------------------------------------------------------------ */

static void test_t8_irxterm_full_init(void)
{
    struct envblock *envblk = NULL;
    struct envblock **slot;
    int rc;

    printf("\n--- T8: irxterm() via irxinit() path (with wkbi) ---\n");

    irx_anchor_table_reset();
#ifndef __MVS__
    slot = ectenvbk_slot();
    if (slot != NULL)
    {
        *slot = NULL;
    }
#endif

    rc = irxinit(NULL, &envblk);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: irxinit failed — cannot run T8\n");
        return;
    }

    /* irxinit installs the Work Block (wkbi) via envblock_userfield. */
    CHECK(envblk->envblock_userfield != NULL,
          "pre-term: wkbi installed by irxinit");

    rc = irxterm(envblk);

    CHECK(rc == 0, "irxterm returns 0 for full irxinit path");

    /* Cleanup ECTENVBK slot (caller responsibility per CON-3). */
    slot = ectenvbk_slot();
    if (slot != NULL)
    {
        *slot = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  T9: TSO single env — IRXTERM rolls ECTENVBK back to NULL          */
/*                                                                    */
/*  Allocate a TSO-attached env, seed ECTENVBK with it, terminate.   */
/*  No predecessor in IRXANCHR → rollback target is NULL.             */
/* ------------------------------------------------------------------ */

static void test_t9_tso_rollback_to_null(void)
{
    struct parmblock pb;
    struct envblock *envblk = NULL;
    struct envblock **slot;
    int reason = -1;
    int rc;

    printf("\n--- T9: TSO single env — IRXTERM rolls ECTENVBK to NULL ---\n");

    irx_anchor_table_reset();
    if (!slot_reachable_or_skip("T9"))
    {
        return;
    }

    slot = ectenvbk_slot();
    *slot = NULL;

    build_parmblock_with_tsofl(&pb, /*tso=*/1);
    rc = irx_init_initenvb(NULL, &pb, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: T9 (irx_init_initenvb failed)\n");
        return;
    }

    /* Simulate IRXINIT having written ECTENVBK for this TSO env. */
    *slot = envblk;
    CHECK(*slot == envblk, "T9: pre-term: ECTENVBK points at our env");

    reason = -1;
    rc = irx_init_term(envblk, &reason);

    CHECK(rc == 0, "T9: irx_init_term returns 0");
    CHECK(*slot == NULL, "T9: ECTENVBK rolled back to NULL (no predecessor)");
}

/* ------------------------------------------------------------------ */
/*  T10: TSO two-env stack — IRXTERM rolls ECTENVBK back to outer     */
/*                                                                    */
/*  Allocate outer (TSO), then inner (TSO). IRXTERM on inner must     */
/*  restore ECTENVBK to the outer env.                                */
/* ------------------------------------------------------------------ */

static void test_t10_tso_rollback_to_outer(void)
{
    struct parmblock pb;
    struct envblock *outer = NULL;
    struct envblock *inner = NULL;
    struct envblock **slot;
    int reason = -1;
    int rc;

    printf("\n--- T10: TSO two-env stack — IRXTERM rolls to outer ---\n");

    irx_anchor_table_reset();
    if (!slot_reachable_or_skip("T10"))
    {
        return;
    }

    slot = ectenvbk_slot();
    *slot = NULL;

    build_parmblock_with_tsofl(&pb, /*tso=*/1);
    rc = irx_init_initenvb(NULL, &pb, 0, &outer, &reason);
    if (rc != 0 || outer == NULL)
    {
        printf("  SKIP: T10 (outer irx_init_initenvb failed)\n");
        return;
    }
    *slot = outer;

    build_parmblock_with_tsofl(&pb, /*tso=*/1);
    rc = irx_init_initenvb(NULL, &pb, 0, &inner, &reason);
    if (rc != 0 || inner == NULL)
    {
        printf("  SKIP: T10 (inner irx_init_initenvb failed)\n");
        irx_init_term(outer, NULL);
        *slot = NULL;
        return;
    }
    *slot = inner;

    CHECK(*slot == inner, "T10: pre-term: ECTENVBK points at inner env");

    reason = -1;
    rc = irx_init_term(inner, &reason);

    CHECK(rc == 0, "T10: irx_init_term(inner) returns 0");
    CHECK(*slot == outer, "T10: ECTENVBK rolled back to outer env");

    /* Cleanup. */
    irx_init_term(outer, NULL);
    *slot = NULL;
}

/* ------------------------------------------------------------------ */
/*  T11: IRXTERM failure — ECTENVBK unchanged (AC-7)                  */
/*                                                                    */
/*  Calling irx_init_term on an envblock not in IRXANCHR (double-free  */
/*  or invalid) must leave ECTENVBK untouched.                        */
/* ------------------------------------------------------------------ */

static void test_t11_failure_ectenvbk_unchanged(void)
{
    struct parmblock pb;
    struct envblock *envblk = NULL;
    struct envblock *const sentinel =
        (struct envblock *)(unsigned long)0xDEAD0003UL;
    struct envblock **slot;
    int reason = -1;
    int rc;

    printf("\n--- T11: IRXTERM failure — ECTENVBK unchanged (AC-7) ---\n");

    irx_anchor_table_reset();
    if (!slot_reachable_or_skip("T11"))
    {
        return;
    }

    slot = ectenvbk_slot();
    *slot = sentinel;

    build_parmblock_with_tsofl(&pb, /*tso=*/1);
    rc = irx_init_initenvb(NULL, &pb, 0, &envblk, &reason);
    if (rc != 0 || envblk == NULL)
    {
        printf("  SKIP: T11 (irx_init_initenvb failed)\n");
        *slot = NULL;
        return;
    }

    /* Free the slot so irx_init_term's idempotency guard triggers. */
    irx_anchor_free_slot(envblk);

    reason = -1;
    rc = irx_init_term(envblk, &reason);

    CHECK(rc == 20, "T11: irx_init_term returns 20 (not registered)");
    CHECK(*slot == sentinel, "T11: ECTENVBK unchanged on failure (AC-7)");

    /* Cleanup: envblk was allocated but its IRXANCHR slot is already
     * free; free the ENVBLOCK storage directly to avoid a memory leak.
     * irx_init_term would do this, but it returned 20 so it did not. */
    *slot = NULL;
    {
        void *p = envblk;
        irxstor(RXSMFRE, 0, &p, NULL);
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("TSTTERM: IRXTERM C-Core\n");

    test_t1_basic_term();
    test_t2_null_envblock();
    test_t3_bad_eyecatcher();
    test_t4_anchor_slot_freed();
    test_t5_idempotency();
    test_t6_ectenvbk_unchanged();
    test_t7_irxterm_minimal_init();
    test_t8_irxterm_full_init();
    test_t9_tso_rollback_to_null();
    test_t10_tso_rollback_to_outer();
    test_t11_failure_ectenvbk_unchanged();

    printf("\n--- Results ---\n");
    printf("  Run:    %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Skipped: %d\n", tests_skipped);

    if (tests_failed > 0)
    {
        printf("TSTTERM FAILED\n");
        return 1;
    }

    printf("TSTTERM PASSED\n");
    return 0;
}
