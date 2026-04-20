/* ------------------------------------------------------------------ */
/*  test_anchor_readmostly.c - Case (b) verification for CON-1 §6.1   */
/*                                                                    */
/*  The read-mostly ECTENVBK anchor has three distinguishable         */
/*  states. TSTANCH exercises state (a) (slot is NULL at entry) and   */
/*  state (c) (slot ends up holding our env) — but in the common     */
/*  "fresh login" case those two paths are byte-identical to what     */
/*  an old-style push/pop implementation would do. The test below    */
/*  pins down state (b) explicitly: another REXX already owns the    */
/*  anchor when IRXINIT runs.                                         */
/*                                                                    */
/*  Under read-mostly we must observe:                                */
/*    - IRXINIT succeeds and returns a valid ENVBLOCK                 */
/*    - ECTENVBK stays at the pre-existing value — not overwritten   */
/*    - IRXTERM is lenient: because ECTENVBK never equalled our env, */
/*      the terminate path does not clear the slot                    */
/*                                                                    */
/*  A push/pop implementation would fail both of the slot checks.    */
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

/* Pin ECTENVBK to a sentinel that cannot collide with any real
 * ENVBLOCK address returned by the host malloc. */
static void *const SENTINEL = (void *)(unsigned long)0xDEAD0001UL;

int main(void)
{
    struct envblock *env = NULL;
    int rc;

    printf("=== Read-mostly ECTENVBK — Case (b): slot already owned ===\n");

    /* Seed the fake anchor. On the host this is the storage the
     * anchor API reads/writes directly; on MVS the same test would
     * need a different injection point, which is out of scope for
     * this cross-compile unit. */
    _simulated_ectenvbk = SENTINEL;
    CHECK(anch_curr() == SENTINEL, "pre-seed: anch_curr() == SENTINEL");

    rc = irxinit(NULL, &env);
    CHECK(rc == 0, "irxinit returns 0");
    CHECK(env != NULL, "irxinit produced an ENVBLOCK");

    CHECK(anch_curr() == SENTINEL,
          "ECTENVBK NOT overwritten (read-mostly guard holds)");
    CHECK(env != (struct envblock *)SENTINEL,
          "returned env is distinct from the pre-existing anchor");

    rc = irxterm(env);
    CHECK(rc == 0, "irxterm returns 0");

    CHECK(anch_curr() == SENTINEL,
          "ECTENVBK still untouched after irxterm (lenient pop)");

    /* Restore the slot so any follow-up harness starts clean. */
    _simulated_ectenvbk = NULL;
    CHECK(anch_curr() == NULL, "post-cleanup: anch_curr() == NULL");

    printf("\n=== Results: %d/%d passed",
           tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
