/* ------------------------------------------------------------------ */
/*  tstphas1.c - Phase 1 Smoke Test                                */
/*                                                                    */
/*  Verifies the REXX/370 Phase 1 foundation:                        */
/*  - IRXINIT creates a valid environment                             */
/*  - All control blocks are properly linked                          */
/*  - IRXTERM cleans up everything                                    */
/*  - Multiple environments can coexist                               */
/*                                                                    */
/*  Build: gcc -I inc -o test_phase1 test/test_phase1.c               */
/*             src/irxinit.c src/irxterm.c src/irxstor.c              */
/*             src/irxrab.c src/irxuid.c src/irxmsgid.c               */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxanchr.h"
#include "irxfunc.h"
#include "irxwkblk.h"

/* Cross-compile: expose simulated ECTENVBK slot consumed by irx#anch.c */
#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

/* Forward-declare the ECTENVBK slot accessor implemented in
 * src/irx#anch.c. Used by CHECK_IF_REACHABLE below to decide whether
 * the read-mostly anchor assertions can hold on this run. */
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

/* Assertions that check anch_curr() against a non-NULL ENVBLOCK only
 * hold when the ECTENVBK slot is reachable so anch_push can actually
 * write it. ectenvbk_slot() returns non-NULL on host (the slot is the
 * `_simulated_ectenvbk` global) and on MVS under TSO (the
 * PSA→ASCB→ASXB→LWA→ECT walk completes); it returns NULL on pure
 * batch. Skip rather than fail when unreachable. Assertions that
 * check anch_curr() == NULL hold in both modes and run
 * unconditionally. */
#define CHECK_IF_REACHABLE(cond, msg)                            \
    do                                                           \
    {                                                            \
        if (ectenvbk_slot() != NULL)                             \
        {                                                        \
            CHECK(cond, msg);                                    \
        }                                                        \
        else                                                     \
        {                                                        \
            tests_skipped++;                                     \
            printf("  SKIP: %s (no ECT slot reachable)\n", msg); \
        }                                                        \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Test 1: Single environment lifecycle                              */
/* ------------------------------------------------------------------ */

static void test_single_env(void)
{
    struct envblock *envblk = NULL;
    struct parmblock *pb;
    struct irxexte *exte;
    struct irx_wkblk_int *wkbi;
    int rc;

    printf("\n--- Test 1: Single environment lifecycle ---\n");

    /* IRXINIT */
    rc = irxinit(NULL, &envblk);
    CHECK(rc == 0, "irxinit returns 0");
    CHECK(envblk != NULL, "envblock is not NULL");

    if (envblk == NULL)
    {
        return;
    }

    /* Validate ENVBLOCK */
    CHECK(memcmp(envblk->envblock_id, ENVBLOCK_ID, 8) == 0,
          "envblock eye-catcher is ENVBLOCK");
    CHECK(envblk->envblock_length == (int)sizeof(struct envblock),
          "envblock length is correct");

    /* Validate PARMBLOCK link */
    pb = (struct parmblock *)envblk->envblock_parmblock;
    CHECK(pb != NULL, "parmblock is linked");
    if (pb != NULL)
    {
        CHECK(memcmp(pb->parmblock_id, PARMBLOCK_ID, 8) == 0,
              "parmblock eye-catcher is IRXPARMS");
        CHECK(pb->parmblock_subcomtb != NULL,
              "subcomtb is linked in parmblock");
    }

    /* Validate IRXEXTE link */
    exte = (struct irxexte *)envblk->envblock_irxexte;
    CHECK(exte != NULL, "irxexte is linked");
    if (exte != NULL)
    {
        CHECK(exte->irxexte_entry_count == IRXEXTE_ENTRY_COUNT,
              "irxexte has correct entry count");
        CHECK(exte->irxuid != NULL,
              "irxuid is wired in irxexte");
        CHECK(exte->irxmsgid != NULL,
              "irxmsgid is wired in irxexte");
    }

    /* Validate internal Work Block */
    wkbi = (struct irx_wkblk_int *)envblk->envblock_userfield;
    CHECK(wkbi != NULL, "internal wkblk is linked via userfield");
    if (wkbi != NULL)
    {
        CHECK(memcmp(wkbi->wkbi_id, WKBLK_INT_ID, 4) == 0,
              "wkblk eye-catcher is WKBI");
        CHECK(wkbi->wkbi_digits == NUMERIC_DIGITS_DEFAULT,
              "NUMERIC DIGITS default is 9");
        CHECK(wkbi->wkbi_fuzz == NUMERIC_FUZZ_DEFAULT,
              "NUMERIC FUZZ default is 0");
        CHECK(wkbi->wkbi_form == NUMFORM_SCIENTIFIC,
              "NUMERIC FORM default is SCIENTIFIC");
        CHECK(wkbi->wkbi_envblock == envblk,
              "wkblk back-pointer to envblock is correct");
    }

    /* Validate ECTENVBK anchor (TSO only — anch_push is a no-op when
     * the slot is unreachable, so anch_curr stays NULL in batch). */
    CHECK_IF_REACHABLE(anch_curr() == envblk,
                       "anch_curr returns our envblock");

    /* IRXTERM */
    rc = irxterm(envblk);
    CHECK(rc == 0, "irxterm returns 0");

    /* anch_curr is NULL in both modes after term: TSO because
     * anch_pop cleared the slot, batch because the slot was never
     * written. Run unconditionally. */
    CHECK(anch_curr() == NULL, "anch_curr returns NULL after term");
}

/* ------------------------------------------------------------------ */
/*  Test 2: Multiple concurrent environments                          */
/*                                                                    */
/*  Under the read-mostly anchor (CON-1 §6.1) only the first IRXINIT  */
/*  claims ECTENVBK; later IRXINITs observe a non-NULL slot and       */
/*  leave it alone. IRXTERM is lenient and only clears when the slot */
/*  still equals the terminating env. So regardless of the order of   */
/*  the IRXTERMs, the slot stays pinned to env1 until env1 itself is  */
/*  terminated.                                                       */
/* ------------------------------------------------------------------ */

static void test_multiple_envs(void)
{
    struct envblock *env1 = NULL;
    struct envblock *env2 = NULL;
    struct envblock *env3 = NULL;
    int rc;

    printf("\n--- Test 2: Multiple concurrent environments ---\n");

    rc = irxinit(NULL, &env1);
    CHECK(rc == 0 && env1 != NULL, "env1 created");

    rc = irxinit(NULL, &env2);
    CHECK(rc == 0 && env2 != NULL, "env2 created");

    rc = irxinit(NULL, &env3);
    CHECK(rc == 0 && env3 != NULL, "env3 created");

    /* Read-mostly: env1 got the slot, env2/env3 never touched it.
     * The "still env1" checks below are TSO-only — see CHECK_IF_REACHABLE
     * comment near the top of the file. */
    CHECK_IF_REACHABLE(anch_curr() == env1,
                       "anch_curr holds the first claimant (env1)");

    CHECK(env1 != env2 && env2 != env3,
          "all envblocks are distinct");

    /* Terminate in reverse order. env3 and env2 were never in the
     * slot, so their terminates are no-ops at the anchor. */
    rc = irxterm(env3);
    CHECK(rc == 0, "env3 terminated");
    CHECK_IF_REACHABLE(anch_curr() == env1,
                       "after env3 term, anchor still env1");

    rc = irxterm(env2);
    CHECK(rc == 0, "env2 terminated");
    CHECK_IF_REACHABLE(anch_curr() == env1,
                       "after env2 term, anchor still env1");

    rc = irxterm(env1);
    CHECK(rc == 0, "env1 terminated");
    CHECK(anch_curr() == NULL,
          "after env1 term, no environments remain");
}

/* ------------------------------------------------------------------ */
/*  Test 3: User ID and Message ID routines                           */
/* ------------------------------------------------------------------ */

static void test_uid_msgid(void)
{
    struct envblock *envblk = NULL;
    char userid[8];
    char prefix[3];
    int rc;

    printf("\n--- Test 3: User ID and Message ID ---\n");

    rc = irxinit(NULL, &envblk);
    CHECK(rc == 0, "environment created for uid/msgid test");

    /* User ID */
    memset(userid, 0, 8);
    rc = irxuid(userid, envblk);
    CHECK(rc == 0, "irxuid returns 0");
    CHECK(userid[0] != 0, "userid is not empty");
    printf("         userid = '%.8s'\n", userid);

    /* Message ID - GET */
    memset(prefix, 0, 3);
    rc = irxmsgid(0, prefix, envblk);
    CHECK(rc == 0, "irxmsgid GET returns 0");
    CHECK(memcmp(prefix, "IRX", 3) == 0,
          "default prefix is IRX");

    /* Message ID - SET */
    rc = irxmsgid(1, "BRX", envblk);
    CHECK(rc == 0, "irxmsgid SET returns 0");
    rc = irxmsgid(0, prefix, envblk);
    CHECK(memcmp(prefix, "BRX", 3) == 0,
          "prefix changed to BRX");

    /* Restore */
    irxmsgid(1, "IRX", envblk);

    irxterm(envblk);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== REXX/370 Phase 1 Smoke Test ===\n");
    printf("    mode: %s\n", anch_tso() ? "TSO (ECT reachable)"
                                        : "batch (no ECT — anchor "
                                          "checks will skip)");

    test_single_env();
    test_multiple_envs();
    test_uid_msgid();

    printf("\n=== Results: passed=%d run=%d skipped=%d",
           tests_passed, tests_run, tests_skipped);
    if (tests_failed > 0)
    {
        printf(" FAILED=%d", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
