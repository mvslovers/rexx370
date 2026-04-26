/* ------------------------------------------------------------------ */
/*  tstanrm.c - Read-mostly ECTENVBK protection tests  */
/*                                                                    */
/*  CON-1 §6.1 defines three observable states the ECTENVBK slot can  */
/*  be in, and a distinct read-mostly response to each. The tests     */
/*  below pin down all three as a single self-contained artefact so   */
/*  future reviewers can point at one file for the anchor contract:   */
/*                                                                    */
/*    (a) Empty-slot baseline — IRXINIT claims the slot; IRXTERM      */
/*        leaves ECTENVBK unchanged (CON-3: caller responsibility).   */
/*                                                                    */
/*    (b) BREXX-simulated non-NULL slot — another REXX owns the       */
/*        anchor. IRXINIT must NOT overwrite it; IRXTERM leaves the   */
/*        slot alone because it never pointed at our env.             */
/*                                                                    */
/*    (c) Own-env stacking — a first IRXINIT claimed the slot; a      */
/*        second IRXINIT on top must not disturb it. IRXTERM on any   */
/*        env never modifies ECTENVBK (CON-3) — the caller manages    */
/*        ECTENVBK lifetime.                                          */
/*                                                                    */
/*  An old push/pop implementation would fail cases (b) and (c);      */
/*  read-mostly passes all three.                                     */
/*                                                                    */
/*  Platform-aware seed helper                                        */
/*  --------------------------                                        */
/*  On MVS, production anch_push / anch_pop / anch_curr read and      */
/*  write the real ECTENVBK slot via ectenvbk_slot() (which walks     */
/*  PSA→ASCB→ASXB→LWA→ECT). The host-only `_simulated_ectenvbk`       */
/*  global is unused on MVS. Cases (b) and (c) need to simulate a     */
/*  non-NULL initial slot state; _test_set_anchor() below targets     */
/*  whichever storage the production code actually reads, per         */
/*  platform. Do not re-introduce direct `_simulated_ectenvbk = ...`  */
/*  writes in Case (b) / Case (c) — they silently no-op on MVS and    */
/*  make the test look like it passes on host while failing on MVS.   */
/*  See CON-1 §6.1.                                                   */
/*                                                                    */
/*  Cases that require a specific anchor state (seeding a sentinel,   */
/*  asserting that irxinit claimed the slot, asserting lenient pop)   */
/*  only run when anch_tso() is true — i.e. the TSO ECT chain is      */
/*  reachable. In pure batch (EXEC PGM=... direct) the walk returns   */
/*  NULL and anch_push / anch_pop reduce to local field updates, so   */
/*  those assertions are skipped (tests_skipped) rather than failed.  */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 \                                  */
/*        -o /tmp/tstanrm test/mvs/tstanrm.c \                        */
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
#include "irxanchr.h"
#include "irxfunc.h"

#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

/* Forward-declare the ECTENVBK slot accessor implemented in
 * src/irx#anch.c. Production code reaches it through anch_push /
 * anch_pop / anch_curr; the test harness needs direct access so it
 * can seed the real slot on MVS to simulate a foreign REXX owning
 * the anchor (Case b) or a prior own-env claim (Case c). */
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

/* Run `stmt` only when the ECTENVBK slot is reachable; otherwise
 * count it as skipped with a diagnostic line. `stmt` is expected
 * to contain exactly one CHECK.
 *
 * "Reachable" means ectenvbk_slot() returns non-NULL: true on host
 * (the slot is the simulation global, always present) and on MVS
 * under TSO (the PSA→ASCB→ASXB→LWA→ECT walk completes); false only
 * on pure MVS batch where LWA is NULL. Gating on slot reachability
 * — not on anch_tso() — keeps the host cross-compile counts
 * unchanged while still skipping the four/seven assertions that
 * cannot hold under batch. */
#define CHECK_IF_REACHABLE(stmt, msg)                      \
    do                                                     \
    {                                                      \
        if (ectenvbk_slot() != NULL)                       \
        {                                                  \
            stmt;                                          \
        }                                                  \
        else                                               \
        {                                                  \
            tests_skipped++;                               \
            printf("  SKIP: %s (no ECT slot reachable)\n", \
                   msg);                                   \
        }                                                  \
    } while (0)

/* Sentinel for Case (b) — any value that cannot collide with a real
 * ENVBLOCK address returned by the host allocator. */
static void *const SENTINEL = (void *)(unsigned long)0xDEAD0001UL;

/* Platform-aware slot seed / read. Tests never touch
 * _simulated_ectenvbk directly — use these so MVS and host builds
 * exercise the same code path that production anch_* functions
 * observe. */
static void _test_set_anchor(struct envblock *value)
{
#ifdef __MVS__
    struct envblock **slot = ectenvbk_slot();
    if (slot != NULL)
    {
        *slot = value;
    }
#else
    _simulated_ectenvbk = value;
#endif
}

static struct envblock *_test_get_anchor(void)
{
#ifdef __MVS__
    struct envblock **slot = ectenvbk_slot();
    return (slot != NULL) ? *slot : NULL;
#else
    return (struct envblock *)_simulated_ectenvbk;
#endif
}

/* ------------------------------------------------------------------ */
/*  Case (a) — empty-slot baseline                                    */
/* ------------------------------------------------------------------ */

static void case_a_empty_slot_baseline(void)
{
    struct envblock *env = NULL;
    int rc;

    printf("\n--- Case (a): empty-slot baseline ---\n");

    _test_set_anchor(NULL);
    CHECK(anch_curr() == NULL, "precondition: anch_curr() == NULL");

    rc = irxinit(NULL, &env);
    CHECK(rc == 0, "irxinit returns 0");
    CHECK(env != NULL, "irxinit produced an ENVBLOCK");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == env,
              "slot claimed: anch_curr() == env (was NULL at push)"),
        "slot claimed: anch_curr() == env");

    struct envblock *slot_before = anch_curr(); /* save before irxterm */
    rc = irxterm(env);
    CHECK(rc == 0, "irxterm returns 0");
    /* CON-3: IRXTERM does not touch ECTENVBK. The slot retains its
     * pre-term value regardless of whether we were the holder. */
    CHECK(anch_curr() == slot_before,
          "ECTENVBK unchanged after irxterm (CON-3)");
    /* Caller-side cleanup. */
    _test_set_anchor(NULL);
}

/* ------------------------------------------------------------------ */
/*  Case (b) — BREXX-simulated non-NULL slot                          */
/* ------------------------------------------------------------------ */

static void case_b_simulated_brexx_owns_slot(void)
{
    struct envblock *env = NULL;
    int rc;

    printf("\n--- Case (b): BREXX-simulated non-NULL slot ---\n");

    /* Seed the real slot (TSO) or the host simulation variable. On
     * batch MVS the slot is unreachable and the seed is a no-op —
     * the dependent assertions below are gated behind anch_tso(). */
    _test_set_anchor((struct envblock *)SENTINEL);
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == SENTINEL,
              "pre-seed: anch_curr() == SENTINEL"),
        "pre-seed: anch_curr() == SENTINEL");

    rc = irxinit(NULL, &env);
    CHECK(rc == 0, "irxinit returns 0");
    CHECK(env != NULL, "irxinit produced an ENVBLOCK");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == SENTINEL,
              "slot NOT overwritten (read-mostly guard holds)"),
        "slot NOT overwritten after irxinit");
    CHECK(env != (struct envblock *)SENTINEL,
          "returned env is distinct from the pre-existing anchor");

    rc = irxterm(env);
    CHECK(rc == 0, "irxterm returns 0");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == SENTINEL,
              "slot still SENTINEL after irxterm (lenient pop)"),
        "slot still SENTINEL after irxterm");

    /* Always run the post-cleanup restore: on MVS batch the slot was
     * never touched, on host the simulation variable was, and the
     * helper handles both. Keeps Case (c)'s precondition clean. */
    _test_set_anchor(NULL);
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

    _test_set_anchor(NULL);
    CHECK(anch_curr() == NULL, "precondition: anch_curr() == NULL");

    rc = irxinit(NULL, &outer);
    CHECK(rc == 0 && outer != NULL, "outer irxinit returned a valid env");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == outer,
              "outer claimed the slot (was NULL at push)"),
        "outer claimed the slot");

    rc = irxinit(NULL, &inner);
    CHECK(rc == 0 && inner != NULL, "inner irxinit returned a valid env");
    CHECK(inner != outer, "inner env is distinct from outer");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == outer,
              "slot still outer (inner read-mostly-skipped the write)"),
        "slot still outer after inner irxinit");

    rc = irxterm(inner);
    CHECK(rc == 0, "inner irxterm returns 0");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == outer,
              "slot still outer (inner was not the holder; CON-3 no-op)"),
        "slot still outer after inner irxterm");

    struct envblock *slot_before = anch_curr(); /* save before outer irxterm */
    rc = irxterm(outer);
    CHECK(rc == 0, "outer irxterm returns 0");
    /* CON-3: IRXTERM does not touch ECTENVBK. */
    CHECK(anch_curr() == slot_before,
          "ECTENVBK unchanged after outer irxterm (CON-3)");
    /* Caller-side cleanup. */
    _test_set_anchor(NULL);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== Read-mostly ECTENVBK protection tests (CON-1 §6.1) ===\n");
    printf("    mode: %s\n", anch_tso() ? "TSO (ECT reachable)"
                                        : "batch (no ECT — TSO-only "
                                          "assertions will skip)");

    /* Silence unused-function warning on host where _test_get_anchor
     * is not exercised by any case today. The helper is kept for
     * symmetry with _test_set_anchor and future tests. */
    (void)_test_get_anchor;

    case_a_empty_slot_baseline();
    case_b_simulated_brexx_owns_slot();
    case_c_own_env_stacking();

    printf("\n=== Results: passed=%d run=%d skipped=%d",
           tests_passed, tests_run, tests_skipped);
    if (tests_failed > 0)
    {
        printf(" FAILED=%d", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
