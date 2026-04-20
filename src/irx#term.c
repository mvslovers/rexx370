/* ------------------------------------------------------------------ */
/*  irxterm.c - IRXTERM: Terminate a Language Processor Environment    */
/*                                                                    */
/*  Reverses IRXINIT: pops the environment off ECTENVBK, frees all    */
/*  control blocks.                                                   */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 15 (IRXTERM)                            */
/*  Ref: CON-1 §6.4 (IRXTERM flow)                                    */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxanchor.h"
#include "irxbif.h"
#include "irxfunc.h"
#include "irxwkblk.h"

/* Internal helper: free via irxstor, NULL-safe */
static void stor_free(void **ptr, struct envblock *envblk)
{
    if (ptr != NULL && *ptr != NULL)
    {
        irxstor(RXSMFRE, 0, ptr, envblk);
    }
}

/* ================================================================== */
/*  irxterm - Terminate a Language Processor Environment               */
/*                                                                    */
/*  Sequence:                                                         */
/*   1. Validate ENVBLOCK                                             */
/*   2. Call termination exit (Phase 6)                               */
/*   3. Unpublish from ECTENVBK (lenient if not current)              */
/*   4. Free internal Work Block (interpreter state)                  */
/*   5. Free SUBCOMTB entries + header                                */
/*   6. Free IRXEXTE                                                  */
/*   7. Free Work Block Extension                                     */
/*   8. Free PARMBLOCK                                                */
/*   9. Free ENVBLOCK                                                 */
/*                                                                    */
/*  Returns: 0=OK, 20=error                                           */
/* ================================================================== */

int irxterm(struct envblock *envblk)
{
    struct irx_wkblk_int *wkbi;
    struct parmblock *pb;
    struct subcomtb_header *subcmd;

    /* 1. Validate */
    if (envblk == NULL ||
        memcmp(envblk->envblock_id, ENVBLOCK_ID, 8) != 0)
    {
        return 20;
    }

    /* 2. Term exit — deferred to Phase 6 */

    /* 3. Unpublish from ECTENVBK. Lenient: if someone pushed another
     * environment on top out of LIFO order, we leave the anchor alone
     * and merely free our local storage. */
    anchor_pop(envblk);

    /* 4. Free internal Work Block */
    wkbi = (struct irx_wkblk_int *)envblk->envblock_userfield;
    if (wkbi != NULL)
    {
        /* TODO Phase 2+: Free variable pool, data stack,
         * exec stack, token stream, label table, cache
         * that hang off wkbi before freeing wkbi itself */

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

    /* 5. Free SUBCOMTB */
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

    /* 6. Free IRXEXTE */
    stor_free((void **)&envblk->envblock_irxexte, envblk);

    /* 7. Free Work Block Extension */
    stor_free((void **)&envblk->envblock_workblok_ext, envblk);

    /* 8. Free PARMBLOCK */
    stor_free((void **)&envblk->envblock_parmblock, envblk);

    /* 9. Free ENVBLOCK itself (no envblock context for this free) */
    void *p = envblk;
    irxstor(RXSMFRE, 0, &p, NULL);

    return 0;
}
