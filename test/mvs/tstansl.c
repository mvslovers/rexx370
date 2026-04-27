/* ------------------------------------------------------------------ */
/*  tstansl.c - IRXANCHR Slot-Management-API tests (WP-I1a.3)         */
/*                                                                    */
/*  Eight test cases pinning down the IBM-observed slot-allocation    */
/*  behaviour:                                                        */
/*    1. Alloc/find/free round-trip; USED stays at high-watermark      */
/*    2. High-watermark never shrinks; next alloc uses N+1, not freed  */
/*    3. Sentinel integrity (Slot 0, Slot 2) across alloc/free cycles  */
/*    4. Token counter is strictly monotonic (free + re-alloc > old)   */
/*    5. Table-full: 62nd alloc succeeds, 63rd returns RC=FULL         */
/*    6. find_by_tcb returns the highest-token entry for a TCB         */
/*    7. get_handle returns RC=BAD_EYE on corrupt eye-catcher (host)   */
/*    8. is_tso flag steers FLAGS field; lookup is flag-agnostic       */
/*                                                                    */
/*  Cross-compile build:                                              */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 \                                  */
/*        -o /tmp/tstansl test/mvs/tstansl.c \                        */
/*        'src/irx#anch.c'                                            */
/*                                                                    */
/*  MVS build: mbt build --target TSTANSL                             */
/*             CALL 'hlq.LOAD(TSTANSL)'                               */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "irxanchr.h"

/* Required by irx#anch.c host simulation (ectenvbk_slot). */
void *_simulated_ectenvbk = NULL;

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                                */
/* ------------------------------------------------------------------ */

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

/* Fake envblock and TCB addresses — small integers cast to pointers.
 * Functions compare these numerically (uint32_t) and never dereference. */
#define ENV(n) ((void *)(unsigned long)(0x1000U * (unsigned)(n)))
#define TCB(n) ((void *)(unsigned long)(0x100U * (unsigned)(n)))

/* Convenience: get slot array from the live IRXANCHR handle. */
static irxanchr_entry_t *get_slots(void)
{
    irxanchr_header_t *hdr;
    if (irx_anchor_get_handle(&hdr) != IRX_ANCHOR_RC_OK)
    {
        return NULL;
    }
    return (irxanchr_entry_t *)((char *)hdr + sizeof(irxanchr_header_t));
}

static uint32_t get_used(void)
{
    irxanchr_header_t *hdr;
    if (irx_anchor_get_handle(&hdr) != IRX_ANCHOR_RC_OK)
    {
        return IRXANCHR_SLOT_SENTINEL; /* distinct sentinel for caller */
    }
    return hdr->used;
}

/* ------------------------------------------------------------------ */
/*  Test 1: alloc / find / free round-trip                             */
/* ------------------------------------------------------------------ */

static void test_1_round_trip(void)
{
    uint32_t tok = 0;
    int rc;
    irxanchr_entry_t *e;

    printf("--- Test 1: alloc/find/free round-trip\n");
    irx_anchor_table_reset();

    rc = irx_anchor_alloc_slot(ENV(1), TCB(1), /*is_tso=*/1, &tok);
    CHECK(rc == IRX_ANCHOR_RC_OK, "T1: alloc slot returns OK");
    CHECK(tok == 1, "T1: first token is 1");
    CHECK(get_used() == 2, "T1: USED = 2 after first alloc");

    e = irx_anchor_find_by_envblock(ENV(1));
    CHECK(e != NULL, "T1: find_by_envblock returns non-NULL");
    CHECK(e != NULL && e->token == 1, "T1: found slot has token 1");
    CHECK(e != NULL &&
              e->tcb_ptr == (uint32_t)(unsigned long)TCB(1),
          "T1: found slot has correct tcb_ptr");
    CHECK(e != NULL && (e->flags & IRXANCHR_FLAG_TSO_ATTACHED),
          "T1: TSO-attached slot has 0x40000000 flag");

    rc = irx_anchor_free_slot(ENV(1));
    CHECK(rc == IRX_ANCHOR_RC_OK, "T1: free_slot returns OK");
    CHECK(get_used() == 2, "T1: USED stays at 2 after free");

    e = irx_anchor_find_by_envblock(ENV(1));
    CHECK(e == NULL, "T1: find_by_envblock returns NULL after free");
}

/* ------------------------------------------------------------------ */
/*  Test 2: high-watermark does not shrink; no recycling below it      */
/* ------------------------------------------------------------------ */

static void test_2_hwm_no_recycle(void)
{
    uint32_t tok_a, tok_b, tok_c, tok_d;
    irxanchr_entry_t *slots;

    printf("--- Test 2: high-watermark / no recycling\n");
    irx_anchor_table_reset();
    slots = get_slots();

    irx_anchor_alloc_slot(ENV(2), TCB(1), /*is_tso=*/1, &tok_a);
    CHECK(slots != NULL &&
              slots[1].envblock_ptr == (uint32_t)(unsigned long)ENV(2),
          "T2: env_A lands in slot 1");
    CHECK(get_used() == 2, "T2: USED = 2 after env_A");

    /* Slot 2 is a permanent sentinel — env_B must skip it → slot 3. */
    irx_anchor_alloc_slot(ENV(3), TCB(1), /*is_tso=*/1, &tok_b);
    CHECK(slots != NULL &&
              slots[3].envblock_ptr == (uint32_t)(unsigned long)ENV(3),
          "T2: env_B lands in slot 3 (slot 2 is sentinel)");
    CHECK(get_used() == 4, "T2: USED = 4 after env_B");

    irx_anchor_alloc_slot(ENV(4), TCB(1), /*is_tso=*/1, &tok_c);
    CHECK(slots != NULL &&
              slots[4].envblock_ptr == (uint32_t)(unsigned long)ENV(4),
          "T2: env_C lands in slot 4");
    CHECK(get_used() == 5, "T2: USED = 5 after env_C");

    irx_anchor_free_slot(ENV(3));
    CHECK(get_used() == 5, "T2: USED stays 5 after freeing env_B");

    /* Append-only: next alloc starts at USED=5, not recycled slot 3. */
    irx_anchor_alloc_slot(ENV(5), TCB(1), /*is_tso=*/1, &tok_d);
    CHECK(slots != NULL &&
              slots[5].envblock_ptr == (uint32_t)(unsigned long)ENV(5),
          "T2: env_D lands in slot 5 (no recycling of freed slot 3)");
    CHECK(get_used() == 6, "T2: USED = 6 after env_D");

    (void)tok_a;
    (void)tok_b;
    (void)tok_c;
    (void)tok_d;
}

/* ------------------------------------------------------------------ */
/*  Test 3: permanent sentinels survive multiple alloc/free cycles     */
/* ------------------------------------------------------------------ */

static void test_3_sentinel_integrity(void)
{
    uint32_t tok;
    irxanchr_entry_t *slots;

    printf("--- Test 3: sentinel integrity\n");
    irx_anchor_table_reset();
    slots = get_slots();

    /* Drive several alloc/free cycles so the scan passes both
     * sentinel positions at least once. */
    irx_anchor_alloc_slot(ENV(10), TCB(1), /*is_tso=*/1, &tok);
    irx_anchor_alloc_slot(ENV(11), TCB(1), /*is_tso=*/1, &tok);
    irx_anchor_free_slot(ENV(10));
    irx_anchor_alloc_slot(ENV(12), TCB(1), /*is_tso=*/1, &tok);

    CHECK(slots != NULL &&
              slots[0].envblock_ptr == IRXANCHR_SLOT_SENTINEL,
          "T3: slot 0 remains sentinel after alloc/free cycles");
    CHECK(slots != NULL &&
              slots[2].envblock_ptr == IRXANCHR_SLOT_SENTINEL,
          "T3: slot 2 remains sentinel after alloc/free cycles");

    /* free_slot with a sentinel-value pointer must return NOT_FOUND
     * and leave both sentinel slots intact. */
    {
        int rc3 = irx_anchor_free_slot((void *)(unsigned long)IRXANCHR_SLOT_SENTINEL);
        CHECK(rc3 == IRX_ANCHOR_RC_NOT_FOUND,
              "T3: free_slot(SENTINEL) returns NOT_FOUND");
        CHECK(slots != NULL &&
                  slots[0].envblock_ptr == IRXANCHR_SLOT_SENTINEL,
              "T3: slot 0 still sentinel after free_slot(SENTINEL)");
        CHECK(slots != NULL &&
                  slots[2].envblock_ptr == IRXANCHR_SLOT_SENTINEL,
              "T3: slot 2 still sentinel after free_slot(SENTINEL)");
    }

    (void)tok;
}

/* ------------------------------------------------------------------ */
/*  Test 4: token counter is strictly monotonic                        */
/* ------------------------------------------------------------------ */

static void test_4_token_monotonic(void)
{
    uint32_t tok1 = 0;
    uint32_t tok2 = 0;

    printf("--- Test 4: token monotonicity\n");
    irx_anchor_table_reset();

    irx_anchor_alloc_slot(ENV(20), TCB(1), /*is_tso=*/1, &tok1);
    irx_anchor_free_slot(ENV(20));
    irx_anchor_alloc_slot(ENV(20), TCB(1), /*is_tso=*/1, &tok2);

    CHECK(tok2 > tok1,
          "T4: re-allocated slot gets strictly higher token");
}

/* ------------------------------------------------------------------ */
/*  Test 5: table-full — 62nd alloc succeeds, 63rd returns RC=FULL    */
/* ------------------------------------------------------------------ */

static void test_5_table_full(void)
{
    int i;
    int rc;
    uint32_t tok;
    int filled = 0;

    printf("--- Test 5: table full (63rd alloc returns RC=FULL)\n");
    irx_anchor_table_reset();

    /* Allocatable slots: 1, 3-63 = 62 slots. */
    for (i = 0; i < 62; i++)
    {
        rc = irx_anchor_alloc_slot(ENV(100 + i), TCB(1), /*is_tso=*/1, &tok);
        if (rc == IRX_ANCHOR_RC_OK)
        {
            filled++;
        }
    }
    CHECK(filled == 62, "T5: 62 allocations succeed (all non-sentinel slots)");

    rc = irx_anchor_alloc_slot(ENV(200), TCB(1), /*is_tso=*/1, &tok);
    CHECK(rc == IRX_ANCHOR_RC_FULL,
          "T5: 63rd allocation returns RC=FULL");

    (void)tok;
}

/* ------------------------------------------------------------------ */
/*  Test 6: find_by_tcb returns highest-token entry for a TCB          */
/* ------------------------------------------------------------------ */

static void test_6_find_by_tcb(void)
{
    uint32_t tok1;
    uint32_t tok2;
    uint32_t tok3;
    irxanchr_entry_t *found;

    printf("--- Test 6: find_by_tcb returns highest-token entry\n");
    irx_anchor_table_reset();

    irx_anchor_alloc_slot(ENV(30), TCB(2), /*is_tso=*/1, &tok1);
    irx_anchor_alloc_slot(ENV(31), TCB(2), /*is_tso=*/1, &tok2);
    irx_anchor_alloc_slot(ENV(32), TCB(3), /*is_tso=*/1, &tok3);

    found = irx_anchor_find_by_tcb(TCB(2));
    CHECK(found != NULL, "T6: find_by_tcb(tcb2) returns non-NULL");
    /* env_B was allocated second for tcb2 so it has the higher token. */
    CHECK(found != NULL &&
              found->envblock_ptr == (uint32_t)(unsigned long)ENV(31),
          "T6: find_by_tcb(tcb2) returns highest-token entry (env_B)");

    found = irx_anchor_find_by_tcb(TCB(3));
    CHECK(found != NULL &&
              found->envblock_ptr == (uint32_t)(unsigned long)ENV(32),
          "T6: find_by_tcb(tcb3) returns env_C");

    found = irx_anchor_find_by_tcb(TCB(4));
    CHECK(found == NULL, "T6: find_by_tcb(unknown tcb) returns NULL");

    (void)tok1;
    (void)tok2;
    (void)tok3;
}

/* ------------------------------------------------------------------ */
/*  Test 7: get_handle rejects corrupt eye-catcher (host only)         */
/* ------------------------------------------------------------------ */

static void test_7_eyecatcher(void)
{
    int rc;
    irxanchr_header_t *hdr;

    printf("--- Test 7: eye-catcher validation\n");

#ifndef __MVS__
    {
        uint8_t *buf = (uint8_t *)_irxanchr_host_buf();
        uint8_t saved = buf[0];

        rc = irx_anchor_get_handle(&hdr);
        CHECK(rc == IRX_ANCHOR_RC_OK,
              "T7: get_handle OK on valid eye-catcher");

        buf[0] = 0x00; /* corrupt first byte */
        rc = irx_anchor_get_handle(&hdr);
        CHECK(rc == IRX_ANCHOR_RC_BAD_EYE,
              "T7: get_handle returns BAD_EYE on corrupt eye-catcher");

        buf[0] = saved; /* restore */
        rc = irx_anchor_get_handle(&hdr);
        CHECK(rc == IRX_ANCHOR_RC_OK,
              "T7: get_handle OK after restoring eye-catcher");
    }
#else
    /* On MVS, IRXANCHR lives in Step-TCB JPQ private storage (loaded by
     * IRXTMPW at logon). Corruption would technically succeed but would
     * break the live module for the rest of the TSO session, so we skip
     * the destructive path and test only the success path. */
    rc = irx_anchor_get_handle(&hdr);
    CHECK(rc == IRX_ANCHOR_RC_OK,
          "T7: get_handle returns OK on MVS");
    tests_skipped++;
    printf("  SKIP: T7 eye-catcher corruption requires host build\n");
    (void)hdr;
#endif
}

/* ------------------------------------------------------------------ */
/*  Test 8: is_tso parameter steers FLAGS; lookup is flag-agnostic     */
/*                                                                    */
/*  Per IRXPROBE Phase α (CON-14, Case A1 vs A3): the top bit of      */
/*  the FLAGS field is set only for TSO-attached envs (TSOFL=1) and   */
/*  cleared for non-TSO envs (TSOFL=0). Slot occupancy is determined  */
/*  by envblock_ptr alone — find_by_envblock and find_by_tcb must     */
/*  return both flag values.                                          */
/* ------------------------------------------------------------------ */

static void test_8_is_tso_flag(void)
{
    uint32_t tok = 0;
    irxanchr_entry_t *e;

    printf("--- Test 8: is_tso flag steers FLAGS field\n");
    irx_anchor_table_reset();

    /* TSO-attached env → flag set. */
    irx_anchor_alloc_slot(ENV(40), TCB(1), /*is_tso=*/1, &tok);
    e = irx_anchor_find_by_envblock(ENV(40));
    CHECK(e != NULL && e->flags == IRXANCHR_FLAG_TSO_ATTACHED,
          "T8: is_tso=1 → flags == 0x40000000");

    /* Non-TSO env → flag clear. */
    irx_anchor_alloc_slot(ENV(41), TCB(1), /*is_tso=*/0, &tok);
    e = irx_anchor_find_by_envblock(ENV(41));
    CHECK(e != NULL && e->flags == 0U,
          "T8: is_tso=0 → flags == 0x00000000");

    /* Both env kinds findable via find_by_envblock. */
    CHECK(irx_anchor_find_by_envblock(ENV(40)) != NULL,
          "T8: find_by_envblock returns TSO-attached slot");
    CHECK(irx_anchor_find_by_envblock(ENV(41)) != NULL,
          "T8: find_by_envblock returns non-TSO slot");

    /* find_by_tcb returns the highest-token entry regardless of flag —
     * here the non-TSO env was allocated second, so it wins. */
    e = irx_anchor_find_by_tcb(TCB(1));
    CHECK(e != NULL && e->envblock_ptr == (uint32_t)(unsigned long)ENV(41),
          "T8: find_by_tcb returns highest-token entry across flag values");
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== IRXANCHR Slot-Management-API tests (WP-I1a.3) ===\n");

    test_1_round_trip();
    test_2_hwm_no_recycle();
    test_3_sentinel_integrity();
    test_4_token_monotonic();
    test_5_table_full();
    test_6_find_by_tcb();
    test_7_eyecatcher();
    test_8_is_tso_flag();

    printf("\n=== Results: passed=%d run=%d skipped=%d",
           tests_passed, tests_run, tests_skipped);
    if (tests_failed > 0)
    {
        printf(" FAILED=%d", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
