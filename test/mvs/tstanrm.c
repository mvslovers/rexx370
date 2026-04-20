/* ------------------------------------------------------------------ */
/*  tstanrm.c - Read-mostly ECTENVBK protection tests  */
/*                                                                    */
/*  CON-1 §6.1 defines three observable states the ECTENVBK slot can  */
/*  be in, and a distinct read-mostly response to each. The tests     */
/*  below pin down all three as a single self-contained artefact so   */
/*  future reviewers can point at one file for the anchor contract:   */
/*                                                                    */
/*    (a) Empty-slot baseline — the "fresh login" case. IRXINIT       */
/*        claims the slot; IRXTERM clears it back to NULL.            */
/*                                                                    */
/*    (b) BREXX-simulated non-NULL slot — another REXX owns the       */
/*        anchor. IRXINIT must NOT overwrite it; IRXTERM must be      */
/*        lenient because the slot never pointed at our env.          */
/*                                                                    */
/*    (c) Own-env stacking — a first IRXINIT claimed the slot; a      */
/*        second IRXINIT on top must not disturb it. IRXTERM on the   */
/*        inner env is a no-op at the anchor; only the terminate of   */
/*        the first claimant actually clears the slot.                */
/*                                                                    */
/*  An old push/pop implementation would fail cases (b) and (c);      */
/*  read-mostly passes all three.                                     */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 \                                  */
/*        -o test/test_anchor_readmostly \                            */
/*        test/test_anchor_readmostly.c \                             */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \       */
/*        'src/irx#anch.c' 'src/irx#uid.c'  'src/irx#msid.c' \       */
/*        'src/irx#cond.c' 'src/irx#bif.c'  'src/irx#bifs.c' \       */
/*        'src/irx#io.c'   'src/irx#lstr.c' 'src/irx#tokn.c' \       */
/*        'src/irx#vpol.c' 'src/irx#pars.c' 'src/irx#ctrl.c' \       */
/*        'src/irx#arith.c' \                                         */
/*        ../lstring370/src/lstr#*.c                                  */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>

#include "irx.h"
#include "irxanchor.h"
#include "irxfunc.h"

void *_simulated_ectenvbk = NULL;

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

/* Sentinel for Case (b) — any value that cannot collide with a real
 * ENVBLOCK address returned by the host allocator. */
static void *const SENTINEL = (void *)(unsigned long)0xDEAD0001UL;

/* ------------------------------------------------------------------ */
/*  Case (a) — empty-slot baseline                                    */
/* ------------------------------------------------------------------ */

static void case_a_empty_slot_baseline(void)
{
    struct envblock *env = NULL;
    int rc;

    printf("\n--- Case (a): empty-slot baseline ---\n");

    _simulated_ectenvbk = NULL;
    CHECK(anch_curr() == NULL, "precondition: anch_curr() == NULL");

    rc = irxinit(NULL, &env);
    CHECK(rc == 0, "irxinit returns 0");
    CHECK(env != NULL, "irxinit produced an ENVBLOCK");
    CHECK(anch_curr() == env,
          "slot claimed: anch_curr() == env (was NULL at push)");

    rc = irxterm(env);
    CHECK(rc == 0, "irxterm returns 0");
    CHECK(anch_curr() == NULL,
          "slot cleared: anch_curr() == NULL (env was still holder)");
}

/* ------------------------------------------------------------------ */
/*  Case (b) — BREXX-simulated non-NULL slot                          */
/* ------------------------------------------------------------------ */

static void case_b_simulated_brexx_owns_slot(void)
{
    struct envblock *env = NULL;
    int rc;

    printf("\n--- Case (b): BREXX-simulated non-NULL slot ---\n");

    _simulated_ectenvbk = SENTINEL;
    CHECK(anch_curr() == SENTINEL, "pre-seed: anch_curr() == SENTINEL");

    rc = irxinit(NULL, &env);
    CHECK(rc == 0, "irxinit returns 0");
    CHECK(env != NULL, "irxinit produced an ENVBLOCK");
    CHECK(anch_curr() == SENTINEL,
          "slot NOT overwritten (read-mostly guard holds)");
    CHECK(env != (struct envblock *)SENTINEL,
          "returned env is distinct from the pre-existing anchor");

    rc = irxterm(env);
    CHECK(rc == 0, "irxterm returns 0");
    CHECK(anch_curr() == SENTINEL,
          "slot still SENTINEL after irxterm (lenient pop)");

    _simulated_ectenvbk = NULL;
    CHECK(anch_curr() == NULL, "post-cleanup: anch_curr() == NULL");
}

/* ------------------------------------------------------------------ */
/*  Case (c) — own-env stacking                                       */
/* ------------------------------------------------------------------ */

static void case_c_own_env_stacking(void)
{
    struct envblock *outer = NULL;
    struct envblock *inner = NULL;
    int rc;

    printf("\n--- Case (c): own-env stacking ---\n");

    _simulated_ectenvbk = NULL;
    CHECK(anch_curr() == NULL, "precondition: anch_curr() == NULL");

    rc = irxinit(NULL, &outer);
    CHECK(rc == 0 && outer != NULL, "outer irxinit returned a valid env");
    CHECK(anch_curr() == outer,
          "outer claimed the slot (was NULL at push)");

    rc = irxinit(NULL, &inner);
    CHECK(rc == 0 && inner != NULL, "inner irxinit returned a valid env");
    CHECK(inner != outer, "inner env is distinct from outer");
    CHECK(anch_curr() == outer,
          "slot still outer (inner read-mostly-skipped the write)");

    rc = irxterm(inner);
    CHECK(rc == 0, "inner irxterm returns 0");
    CHECK(anch_curr() == outer,
          "slot still outer (inner was not the holder; lenient pop)");

    rc = irxterm(outer);
    CHECK(rc == 0, "outer irxterm returns 0");
    CHECK(anch_curr() == NULL,
          "slot cleared (outer was the holder)");
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== Read-mostly ECTENVBK protection tests (CON-1 §6.1) ===\n");

    case_a_empty_slot_baseline();
    case_b_simulated_brexx_owns_slot();
    case_c_own_env_stacking();

    printf("\n=== Results: %d/%d passed",
           tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
