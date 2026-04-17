/* ------------------------------------------------------------------ */
/*  irxterm.c - IRXTERM: Terminate a Language Processor Environment    */
/*                                                                    */
/*  Reverses IRXINIT: frees all control blocks, removes the           */
/*  environment from the RAB chain, releases the RAB if it was        */
/*  the last environment.                                             */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 15 (IRXTERM)                           */
/*  Ref: Architecture Design v0.1.0, Section 6                       */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxbif.h"
#include "irxfunc.h"
#include "irxrab.h"
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
/*   3. Free internal Work Block (interpreter state)                  */
/*   4. Free SUBCOMTB entries + header                                */
/*   5. Free IRXEXTE                                                  */
/*   6. Free Work Block Extension                                     */
/*   7. Free PARMBLOCK                                                */
/*   8. Remove env_node from RAB chain                                */
/*   9. Free env_node                                                 */
/*  10. Free ENVBLOCK                                                 */
/*  11. Release RAB if last environment                               */
/*                                                                    */
/*  Returns: 0=OK, 20=error                                           */
/* ================================================================== */

int irxterm(struct envblock *envblk)
{
    struct irx_wkblk_int *wkbi;
    struct parmblock *pb;
    struct subcomtb_header *subcmd;
    struct irx_rab *rab;
    struct irx_env_node *node;

    /* 1. Validate */
    if (envblk == NULL ||
        memcmp(envblk->envblock_id, ENVBLOCK_ID, 8) != 0)
    {
        return 20;
    }

    /* 2. Term exit — deferred to Phase 6 */

    /* 3. Free internal Work Block */
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

    /* 4. Free SUBCOMTB */
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

    /* 5. Free IRXEXTE */
    stor_free((void **)&envblk->envblock_irxexte, envblk);

    /* 6. Free Work Block Extension */
    stor_free((void **)&envblk->envblock_workblok_ext, envblk);

    /* 7. Free PARMBLOCK */
    stor_free((void **)&envblk->envblock_parmblock, envblk);

    /* 8+9. Remove from RAB and free env_node */
    rab = NULL;
    /* Walk RAB chain to find our node */
    void **tcbuser_ptr;
    struct irx_env_node *cur;

#ifdef __MVS__
    void **psa_tcb = (void **)(*(int *)0x218);
    tcbuser_ptr = (void **)((char *)psa_tcb + 0x0A8);
#else
    extern void *_simulated_tcbuser;
    tcbuser_ptr = &_simulated_tcbuser;
#endif

    if (*tcbuser_ptr != NULL)
    {
        rab = (struct irx_rab *)(*tcbuser_ptr);
        if (memcmp(rab->rab_id, RAB_ID, 4) == 0)
        {
            /* Find node for this envblock */
            cur = (struct irx_env_node *)rab->rab_first;
            while (cur != NULL)
            {
                if (cur->node_envblock == envblk)
                {
                    node = cur;
                    irx_rab_remove_env(rab, node);
                    stor_free((void **)&node, envblk);
                    break;
                }
                cur = cur->node_next;
            }
        }
    }

    /* 10. Free ENVBLOCK itself (no envblock context for this free) */
    void *p = envblk;
    irxstor(RXSMFRE, 0, &p, NULL);

    /* 11. Release RAB if empty */
    if (rab != NULL && rab->rab_env_count == 0)
    {
        irx_rab_release(rab);
    }

    return 0;
}
