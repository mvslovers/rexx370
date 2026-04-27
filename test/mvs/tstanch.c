/* ------------------------------------------------------------------ */
/*  tstanch.c - TSTANCH: ENVBLOCK anchor smoketest pseudomodule      */
/*                                                                    */
/*  Runs both in TSO (CALL 'TSTANCH') and in Batch (EXEC PGM=TSTANCH).*/
/*  Cross-compile-friendly — links on Linux/gcc against the rexx370   */
/*  phase-1 object set and prints to stdout; on MVS it emits the same */
/*  content through printf (crent370 maps to PUTLINE/WTO depending on */
/*  how the module is invoked).                                       */
/*                                                                    */
/*  Verifies the TSOFL-conditional ECTENVBK contract (CON-14):        */
/*   - TSOFL=1 IRXINIT overwrites ECTENVBK unconditionally.           */
/*     IRXTERM rolls it back to predecessor (NULL for single env).    */
/*   - TSOFL=0 IRXINIT leaves ECTENVBK untouched.                    */
/*     IRXTERM does not touch ECTENVBK.                               */
/*   - In batch (ECT not reachable) IRXINIT and IRXTERM succeed with  */
/*     local-field-only semantics; ECTENVBK is never written.         */
/*                                                                    */
/*  Exit codes:                                                       */
/*     0 — anchor behavior observed correct                           */
/*     8 — anchor semantics violation                                 */
/*    16 — IRXINIT or IRXTERM failed                                  */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>

#include "irx.h"
#include "irxanchr.h"
#include "irxfunc.h"

#ifdef __MVS__
#include "clibppa.h"
#endif

#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

enum
{
    RC_OK = 0,
    RC_ANCHOR_BROKEN = 8,
    RC_IRX_FAILED = 16,
};

/* Dump the crent370 process-level runtime flag byte. crent370's
 * @@CRT0 startup populates PPAFLAG_TSOFG / PPAFLAG_TSOBG via the MVS
 * EXTRACT SVC (FIELDS=TSO/PSB); the similarly-named bits in
 * CLIBCRT.crtflag are never written and were misleading us earlier.
 * This helper prints the raw byte plus each decoded bit so we can
 * confirm the right detection strategy against real Hercules runs. */
static void dump_ppaflag(void)
{
#ifdef __MVS__
    CLIBPPA *ppa = __ppaget();
    if (ppa == NULL)
    {
        printf("  ppaflag        = <no CLIBPPA>\n");
        return;
    }
    unsigned char f = (unsigned char)ppa->ppaflag;
    printf("  ppaflag        = 0x%02X  [TSOFG=%d TSOBG=%d TIN=%d TOUT=%d TERR=%d]\n",
           f,
           (f & PPAFLAG_TSOFG) ? 1 : 0,
           (f & PPAFLAG_TSOBG) ? 1 : 0,
           (f & PPAFLAG_TIN) ? 1 : 0,
           (f & PPAFLAG_TOUT) ? 1 : 0,
           (f & PPAFLAG_TERR) ? 1 : 0);
#else
    printf("  ppaflag        = N/A (host build)\n");
#endif
}

static void dump_state(const char *label, struct envblock *env)
{
    printf("  [%s]\n", label);
    printf("    ECTENVBK slot  = %p\n", (void *)anch_curr());
    printf("    is_tso         = %d\n", anch_tso());
    printf("    walk_to_ect    = %p\n", anch_walk());
    if (env != NULL)
    {
        printf("    envblock addr  = %p\n", (void *)env);
        printf("    envblock_ectptr= %p\n", env->envblock_ectptr);
    }
}

int main(void)
{
    struct envblock *env = NULL;
    struct envblock *initial_anchor;
    int rc;

    printf("=== TSTANCH — REXX/370 anchor smoketest ===\n");

    dump_ppaflag();

    initial_anchor = anch_curr();
    printf("  initial ECTENVBK = %p\n", (void *)initial_anchor);

    rc = irxinit(NULL, &env);
    printf("  irxinit rc = %d, env = %p\n", rc, (void *)env);
    if (rc != 0 || env == NULL)
    {
        printf("FAIL: irxinit did not return an ENVBLOCK\n");
        return RC_IRX_FAILED;
    }

    dump_state("after irxinit", env);

    int exit_rc = 0;

    if (anch_walk() != NULL)
    {
        if (anch_tso())
        {
            /* TSO: IRXINIT unconditionally overwrites ECTENVBK (CON-14). */
            if (anch_curr() != env)
            {
                printf("FAIL: TSO IRXINIT did not install env in ECTENVBK\n");
                exit_rc = RC_ANCHOR_BROKEN;
            }
        }
        else
        {
            /* Non-TSO: IRXINIT must not touch ECTENVBK (CON-14). */
            if (anch_curr() != initial_anchor)
            {
                printf("FAIL: non-TSO IRXINIT modified ECTENVBK\n");
                exit_rc = RC_ANCHOR_BROKEN;
            }
        }
    }
    else
    {
        /* Batch path: no ECT reachable; local-only semantics. */
        if (env->envblock_ectptr != NULL)
        {
            printf("FAIL: batch envblock_ectptr is non-NULL\n");
            exit_rc = RC_ANCHOR_BROKEN;
        }
        if (anch_curr() != NULL)
        {
            printf("FAIL: batch anch_curr returned non-NULL\n");
            exit_rc = RC_ANCHOR_BROKEN;
        }
    }

    rc = irxterm(env);
    printf("  irxterm rc = %d\n", rc);
    if (rc != 0)
    {
        printf("FAIL: irxterm returned non-zero\n");
        return RC_IRX_FAILED;
    }

    dump_state("after irxterm", NULL);

    /* After IRXTERM the slot must be at the expected post-term value.
     *
     * TSO path: IRXINIT wrote the slot; IRXTERM rolls back to the
     * predecessor TSO-attached env in IRXANCHR (NULL for this single-env
     * test since we started from an empty table).
     *
     * Non-TSO / batch: neither IRXINIT nor IRXTERM touched the slot;
     * it remains at initial_anchor. */
    {
        struct envblock *expected = anch_tso() ? NULL : initial_anchor;
        if (anch_curr() != expected)
        {
            printf("FAIL: ECTENVBK not at expected value after IRXTERM\n");
            exit_rc = RC_ANCHOR_BROKEN;
        }
    }

    if (exit_rc == 0)
    {
        printf("PASS: anchor behavior correct; exiting rc=0\n");
    }
    return exit_rc;
}
