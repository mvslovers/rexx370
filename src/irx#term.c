/* ------------------------------------------------------------------ */
/*  irx#term.c - IRXTERM: Terminate a Language Processor Environment  */
/*                                                                    */
/*  irx_init_term() — 6-step C-core:                                  */
/*    1. Validate eye-catcher                                         */
/*    2. IRXANCHR slot lookup (idempotency guard); save slot ptr      */
/*    3. Free IRXEXTE + PARMBLOCK (reverse of INITENVB steps 6, 5)   */
/*    4a. Release IRXANCHR slot                                       */
/*    4b. ECTENVBK rollback to predecessor (TSO-attached envs only;   */
/*        only when we currently own the slot — CON-14, CON-1 §6.1)  */
/*    5. Free ENVBLOCK itself                                         */
/*                                                                    */
/*  irxterm() — compat wrapper that frees wkbi / SUBCOMTB /          */
/*  workblok_ext (Phase 2+ state) before delegating to irx_init_term. */
/*                                                                    */
/*  Ref: SC28-1883-0 §15 (IRXTERM)                                   */
/*  Ref: CON-1 §6.4 (IRXTERM flow), §6.1 (coexistence guard)        */
/*  Ref: CON-14 / IRXPROBE Phase alpha (ECTENVBK predecessor rollback)*/
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irx_init.h"
#include "irxanchr.h"
#include "irxbif.h"
#include "irxfunc.h"
#include "irxwkblk.h"

/* Forward-declare the ECTENVBK slot accessor from irx#anch.c. */
struct envblock **ectenvbk_slot(void);

/* Internal helper: free via irxstor, NULL-safe. */
static void stor_free(void **ptr, struct envblock *envblk)
{
    if (ptr != NULL && *ptr != NULL)
    {
        irxstor(RXSMFRE, 0, ptr, envblk);
    }
}

/* ================================================================== */
/*  irx_init_term — 5-step IRXTERM C-core (WP-I1c.3)                 */
/* ================================================================== */

int irx_init_term(struct envblock *envblock, int *out_reason_code)
{
    irxanchr_entry_t *my_slot;
    int my_is_tso;
    void *p;

    /* Step 1: Validate eye-catcher. */
    if (envblock == NULL ||
        memcmp(envblock->envblock_id, ENVBLOCK_ID, 8) != 0)
    {
        if (out_reason_code != NULL)
        {
            *out_reason_code = 4;
        }
        return 20;
    }

    /* Step 2: IRXANCHR slot lookup — idempotency guard.
     * Save the slot pointer; step 4b uses it after slot free. */
    my_slot = irx_anchor_find_by_envblock(envblock);
    if (my_slot == NULL)
    {
        if (out_reason_code != NULL)
        {
            *out_reason_code = 4;
        }
        return 20;
    }
    my_is_tso = (my_slot->flags & IRXANCHR_FLAG_TSO_ATTACHED) != 0;

    /* Step 3: Free IRXEXTE, then PARMBLOCK — reverse of INITENVB
     * steps 6 and 5.  IRXEXTE freed first so irxstor can still read
     * the subpool from envblock_parmblock. */
    stor_free((void **)&envblock->envblock_irxexte, envblock);
    stor_free((void **)&envblock->envblock_parmblock, envblock);

    /* Step 4a: Release IRXANCHR slot (envblock_ptr set to SLOT_FREE).
     * my_slot still points into the table — the pointer itself is valid
     * for find_previous_used even though envblock_ptr is now zeroed. */
    irx_anchor_free_slot(envblock);

    /* Step 4b: ECTENVBK predecessor rollback (TSO-attached envs only).
     * Guard: only write the slot when it currently points at us — this
     * prevents clobbering a foreign anchor in BREXX/370 coexistence or
     * out-of-order TERM scenarios (CON-1 §6.1, CON-14). */
    if (my_is_tso)
    {
        struct envblock **ect_slot = ectenvbk_slot();
        if (ect_slot != NULL && *ect_slot == envblock)
        {
            irxanchr_entry_t *prev = irx_anchor_find_previous_used(my_slot);
            if (prev != NULL)
            {
#ifndef __MVS__
                void *full_ptr = NULL;
                memcpy(&full_ptr, prev->rsvd1, sizeof(void *));
                *ect_slot = (struct envblock *)full_ptr;
#else
                *ect_slot = (struct envblock *)(unsigned long)prev->envblock_ptr;
#endif
            }
            else
            {
                *ect_slot = NULL;
            }
        }
    }

    /* Step 5: Free ENVBLOCK itself (no envblock context for this free). */
    p = envblock;
    irxstor(RXSMFRE, 0, &p, NULL);

    if (out_reason_code != NULL)
    {
        *out_reason_code = 0;
    }
    return 0;
}

/* ================================================================== */
/*  irxterm — compat wrapper                                          */
/*                                                                    */
/*  Frees Phase 2+ state (wkbi, SUBCOMTB, workblok_ext) that lives   */
/*  outside irx_init_term's scope, then delegates the remaining       */
/*  IRXEXTE / PARMBLOCK / IRXANCHR / ENVBLOCK teardown to the C-core. */
/* ================================================================== */

int irxterm(struct envblock *envblk)
{
    struct irx_wkblk_int *wkbi;
    struct parmblock *pb;
    struct subcomtb_header *subcmd;
    int reason = 0;

    /* 1. Validate — must precede any envblk dereference. */
    if (envblk == NULL ||
        memcmp(envblk->envblock_id, ENVBLOCK_ID, 8) != 0)
    {
        return 20;
    }

    /* 2. Term exit — deferred to Phase 6. */

    /* 3. Free internal Work Block. */
    wkbi = (struct irx_wkblk_int *)envblk->envblock_userfield;
    if (wkbi != NULL)
    {
        /* TODO Phase 2+: Free variable pool, data stack,
         * exec stack, token stream, label table, cache
         * that hang off wkbi before freeing wkbi itself. */

        /* Free the lstring370 allocator bridge if it was installed. */
        if (wkbi->wkbi_lstr_alloc != NULL)
        {
            stor_free(&wkbi->wkbi_lstr_alloc, envblk);
        }

        /* Free the BIF registry (WP-21a). */
        if (wkbi->wkbi_bif_registry != NULL)
        {
            irx_bif_destroy(
                envblk,
                (struct irx_bif_registry *)wkbi->wkbi_bif_registry);
            wkbi->wkbi_bif_registry = NULL;
        }

        envblk->envblock_userfield = NULL;
        stor_free((void **)&wkbi, envblk);
    }

    /* 4. Free SUBCOMTB — must happen before PARMBLOCK is released. */
    pb = (struct parmblock *)envblk->envblock_parmblock;
    if (pb != NULL)
    {
        subcmd = (struct subcomtb_header *)pb->parmblock_subcomtb;
        if (subcmd != NULL)
        {
            stor_free((void **)&subcmd->subcomtb_first, envblk);
            pb->parmblock_subcomtb = NULL;
            stor_free((void **)&subcmd, envblk);
        }
    }

    /* 5. Free Work Block Extension — allocated by IRXEXEC, freed here. */
    stor_free((void **)&envblk->envblock_workblok_ext, envblk);

    /* 6. Delegate IRXEXTE + PARMBLOCK + IRXANCHR + ENVBLOCK to C-core.
     * After this call envblk is freed and must not be dereferenced. */
    return irx_init_term(envblk, &reason);
}
