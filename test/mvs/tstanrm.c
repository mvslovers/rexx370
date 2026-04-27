/* ------------------------------------------------------------------ */
/*  tstanrm.c - ECTENVBK TSOFL-conditional anchor tests  */
/*                                                                    */
/*  IRXPROBE Phase α (TSK-192, CON-14) verified IBM's actual          */
/*  IRXINIT/IRXTERM contract for the ECTENVBK slot:                    */
/*                                                                    */
/*    - TSOFL=1 IRXINIT  → ECTENVBK is overwritten unconditionally     */
/*      with the new ENVBLOCK, regardless of the slot's prior value.   */
/*    - TSOFL=0 IRXINIT  → ECTENVBK is left untouched.                 */
/*    - IRXTERM (any TSOFL) → ECTENVBK is never modified (CON-3).      */
/*                                                                    */
/*  The cases below pin down all four observables in one place so a   */
/*  reviewer can point at this file as the executable contract:        */
/*                                                                    */
/*    (a) Empty-slot baseline — TSOFL=1 IRXINIT writes the slot;       */
/*        IRXTERM does not clear it (caller-managed lifetime).        */
/*                                                                    */
/*    (b) Foreign-owned slot — a sentinel pre-seeded into ECTENVBK     */
/*        is clobbered by TSOFL=1 IRXINIT (matches IBM behaviour).    */
/*        IRXTERM leaves the new value in place.                      */
/*                                                                    */
/*    (c) Own-env stacking — a TSOFL=1 IRXINIT for `outer` claims     */
/*        the slot; a second TSOFL=1 IRXINIT for `inner` overwrites   */
/*        it. IRXTERM on either env is a CON-3 no-op at the anchor.   */
/*                                                                    */
/*    (d) Non-TSO no-op — TSOFL=0 IRXINIT must not touch the slot,    */
/*        even when one is reachable. The pre-seeded sentinel must   */
/*        survive the IRXINIT call.                                   */
/*                                                                    */
/*  An old read-mostly / claim-if-NULL implementation would fail      */
/*  cases (b) and (c); the IBM-compatible TSOFL-conditional contract   */
/*  passes all four. See TSK-195 for the change rationale.            */
/*                                                                    */
/*  Platform-aware seed helper                                        */
/*  --------------------------                                        */
/*  On MVS, production code reads and writes the real ECTENVBK slot   */
/*  via ectenvbk_slot() (which walks PSA→ASCB→ASXB→LWA→ECT). The      */
/*  host-only `_simulated_ectenvbk` global is unused on MVS. Cases   */
/*  that need to simulate a pre-existing slot state use               */
/*  _test_set_anchor() below, which targets whichever storage the     */
/*  production code actually reads, per platform. Do not re-introduce */
/*  direct `_simulated_ectenvbk = ...` writes in cases (b) / (c) /    */
/*  (d) — they silently no-op on MVS and make the test look like it   */
/*  passes on host while failing on MVS. See CON-1 §6.1.              */
/*                                                                    */
/*  Cases that require a specific anchor state only run when           */
/*  ectenvbk_slot() returns non-NULL. In pure MVS batch (EXEC         */
/*  PGM=... direct) the walk returns NULL, so those assertions are    */
/*  skipped (tests_skipped) rather than failed.                       */
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
#include <string.h>

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

/* Build a parmblock with the requested TSOFL value, mirroring the
 * helper in tstinit.c. Bitfield writes lay down the right bit on both
 * MVS (MSB-first) and host (LSB-first) int bitfield encodings. */
static void build_parmblock(struct parmblock *pb, int tso)
{
    memset(pb, 0, sizeof(*pb));
    memcpy(pb->parmblock_id, PARMBLOCK_ID, 8);
    memcpy(pb->parmblock_version, PARMBLOCK_VERSION_0042, 4);
    pb->tsofl_mask = -1;
    pb->tsofl = tso ? -1 : 0;
    memset(pb->parmblock_addrspn, ' ', 8);
    memset(pb->parmblock_ffff, 0xFF, 8);
}

/* ------------------------------------------------------------------ */
/*  Case (a) — empty-slot baseline (TSOFL=1)                          */
/* ------------------------------------------------------------------ */

static void case_a_empty_slot_baseline(void)
{
    struct envblock *env = NULL;
    struct parmblock pb;
    int rc;

    printf("\n--- Case (a): empty-slot baseline (TSOFL=1) ---\n");

    _test_set_anchor(NULL);
    CHECK(anch_curr() == NULL, "precondition: anch_curr() == NULL");

    build_parmblock(&pb, /*tso=*/1);
    rc = irxinit(&pb, &env);
    CHECK(rc == 0, "irxinit returns 0");
    CHECK(env != NULL, "irxinit produced an ENVBLOCK");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == env,
              "TSOFL=1: slot written (anch_curr() == env)"),
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
/*  Case (b) — foreign-owned slot, TSOFL=1 clobbers it                */
/* ------------------------------------------------------------------ */

static void case_b_foreign_slot_clobbered(void)
{
    struct envblock *env = NULL;
    struct parmblock pb;
    int rc;

    printf("\n--- Case (b): foreign-owned slot clobbered (TSOFL=1) ---\n");

    /* Seed the real slot (TSO) or the host simulation variable with a
     * sentinel that simulates a different REXX (e.g. parallel BREXX)
     * already owning the anchor. On batch MVS the slot is unreachable
     * and the seed is a no-op — the dependent assertions below are
     * gated through CHECK_IF_REACHABLE. */
    _test_set_anchor((struct envblock *)SENTINEL);
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == SENTINEL,
              "pre-seed: anch_curr() == SENTINEL"),
        "pre-seed: anch_curr() == SENTINEL");

    build_parmblock(&pb, /*tso=*/1);
    rc = irxinit(&pb, &env);
    CHECK(rc == 0, "irxinit returns 0");
    CHECK(env != NULL, "irxinit produced an ENVBLOCK");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == env,
              "TSOFL=1: foreign slot is overwritten with new env"),
        "slot overwritten after TSOFL=1 irxinit");
    CHECK(env != (struct envblock *)SENTINEL,
          "returned env is distinct from the pre-existing anchor");

    struct envblock *slot_before = anch_curr(); /* save before irxterm */
    rc = irxterm(env);
    CHECK(rc == 0, "irxterm returns 0");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == slot_before,
              "slot unchanged by irxterm (CON-3 no-op)"),
        "slot unchanged after irxterm");

    /* Caller-side cleanup so Case (c) starts with a clean precondition. */
    _test_set_anchor(NULL);
    CHECK(anch_curr() == NULL, "post-cleanup: anch_curr() == NULL");
}

/* ------------------------------------------------------------------ */
/*  Case (c) — own-env stacking (TSOFL=1)                             */
/* ------------------------------------------------------------------ */

static void case_c_own_env_stacking(void)
{
    struct envblock *outer = NULL;
    struct envblock *inner = NULL;
    struct parmblock pb;
    int rc;

    printf("\n--- Case (c): own-env stacking (TSOFL=1) ---\n");

    _test_set_anchor(NULL);
    CHECK(anch_curr() == NULL, "precondition: anch_curr() == NULL");

    build_parmblock(&pb, /*tso=*/1);

    rc = irxinit(&pb, &outer);
    CHECK(rc == 0 && outer != NULL, "outer irxinit returned a valid env");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == outer,
              "TSOFL=1: outer claimed the slot"),
        "outer claimed the slot");

    rc = irxinit(&pb, &inner);
    CHECK(rc == 0 && inner != NULL, "inner irxinit returned a valid env");
    CHECK(inner != outer, "inner env is distinct from outer");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == inner,
              "TSOFL=1 stacking: inner overwrote outer in the slot"),
        "slot now inner after stacked TSOFL=1 irxinit");

    rc = irxterm(inner);
    CHECK(rc == 0, "inner irxterm returns 0");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == inner,
              "slot still inner after inner irxterm (CON-3 no-op)"),
        "slot still inner after inner irxterm");

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
/*  Case (d) — non-TSO env (TSOFL=0) leaves the slot untouched        */
/* ------------------------------------------------------------------ */

static void case_d_non_tso_noop(void)
{
    struct envblock *env = NULL;
    struct parmblock pb;
    int rc;

    printf("\n--- Case (d): non-TSO env, TSOFL=0 no-op ---\n");

    _test_set_anchor((struct envblock *)SENTINEL);
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == SENTINEL,
              "pre-seed: anch_curr() == SENTINEL"),
        "pre-seed: anch_curr() == SENTINEL");

    build_parmblock(&pb, /*tso=*/0);
    rc = irxinit(&pb, &env);
    CHECK(rc == 0, "irxinit returns 0");
    CHECK(env != NULL, "irxinit produced an ENVBLOCK");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == SENTINEL,
              "TSOFL=0: slot remains the sentinel (no anchor write)"),
        "slot unchanged after TSOFL=0 irxinit");

    rc = irxterm(env);
    CHECK(rc == 0, "irxterm returns 0");
    CHECK_IF_REACHABLE(
        CHECK(anch_curr() == SENTINEL,
              "TSOFL=0: slot still sentinel after irxterm"),
        "slot still SENTINEL after irxterm");

    _test_set_anchor(NULL);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== ECTENVBK TSOFL-conditional anchor tests (TSK-195) ===\n");
    printf("    mode: %s\n", anch_tso() ? "TSO (ECT reachable)"
                                        : "batch (no ECT — slot-state "
                                          "assertions will skip)");

    /* Silence unused-function warning on host where _test_get_anchor
     * is not exercised by any case today. The helper is kept for
     * symmetry with _test_set_anchor and future tests. */
    (void)_test_get_anchor;

    case_a_empty_slot_baseline();
    case_b_foreign_slot_clobbered();
    case_c_own_env_stacking();
    case_d_non_tso_noop();

    printf("\n=== Results: passed=%d run=%d skipped=%d",
           tests_passed, tests_run, tests_skipped);
    if (tests_failed > 0)
    {
        printf(" FAILED=%d", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
