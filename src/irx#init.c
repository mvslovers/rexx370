/* ------------------------------------------------------------------ */
/*  irxinit.c - IRXINIT: Initialize a Language Processor Environment  */
/*                                                                    */
/*  Creates and initializes all control blocks for a REXX             */
/*  environment: ENVBLOCK, PARMBLOCK, IRXEXTE, Work Block Extension,  */
/*  internal Work Block, SUBCOMTB, and hooks everything together.     */
/*  Registers the environment in the RAB chain.                       */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 15 (IRXINIT)                           */
/*  Ref: Architecture Design v0.1.0, Section 6.3                     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxbif.h"
#include "irxbifstr.h"
#include "irxfunc.h"
#include "irxio.h"
#include "irxpars.h"
#include "irxrab.h"
#include "irxwkblk.h"

/* Default host command environments */
#define DEFAULT_HOSTENV_TSO  "TSO     "
#define DEFAULT_HOSTENV_MVS  "MVS     "
#define DEFAULT_HOSTENV_LINK "LINK    "
#define DEFAULT_HANDLER_NAME "IRXSTAM "

/* Default number of subcommand table entries */
#define DEFAULT_SUBCOMTB_ENTRIES 8

/* Internal helper: allocate via irxstor with error propagation */
#define ALLOC(ptr, size, envblk)                                  \
    do                                                            \
    {                                                             \
        void *_tmp = NULL;                                        \
        int _rc = irxstor(RXSMGET, (int)(size), &_tmp, (envblk)); \
        if (_rc != 0)                                             \
            goto cleanup;                                         \
        (ptr) = _tmp;                                             \
    } while (0)

/* ------------------------------------------------------------------ */
/*  init_parmblock - Create and populate the PARMBLOCK                */
/* ------------------------------------------------------------------ */

static int init_parmblock(struct parmblock **pb_out,
                          void *user_parms,
                          struct envblock *envblk)
{
    struct parmblock *pb = NULL;

    ALLOC(pb, sizeof(struct parmblock), envblk);

    memcpy(pb->parmblock_id, PARMBLOCK_ID, 8);
    memcpy(pb->parmblock_version, PARMBLOCK_VERSION_0200, 4);
    memcpy(pb->parmblock_language, "ENU", 3);

    /* Default flags: nothing special */
    memset(pb->parmblock_flags, 0, 4);
    memset(pb->parmblock_masks, 0, 4);

    pb->parmblock_subpool = 0;
    memset(pb->parmblock_addrspn, ' ', 8);
    memset(pb->parmblock_ffff, 0xFF, 8);

    /* TODO: If user_parms provided, merge flags/masks from it */

    *pb_out = pb;
    return 0;

cleanup:
    return 20;
}

/* ------------------------------------------------------------------ */
/*  init_subcomtb - Create and populate default host cmd environments  */
/* ------------------------------------------------------------------ */

static int init_subcomtb(struct subcomtb_header **hdr_out,
                         struct parmblock *pb,
                         struct envblock *envblk)
{
    struct subcomtb_header *hdr = NULL;
    struct subcomtb_entry *entries = NULL;
    int used = 0;

    ALLOC(hdr, sizeof(struct subcomtb_header), envblk);
    ALLOC(entries, DEFAULT_SUBCOMTB_ENTRIES * sizeof(struct subcomtb_entry),
          envblk);

    /* MVS environment (always present) */
    memcpy(entries[used].subcomtb_name, DEFAULT_HOSTENV_MVS, 8);
    memcpy(entries[used].subcomtb_routine, DEFAULT_HANDLER_NAME, 8);
    memset(entries[used].subcomtb_token, ' ', 16);
    used++;

    /* TSO environment (if TSO flag set or running under TSO) */
    memcpy(entries[used].subcomtb_name, DEFAULT_HOSTENV_TSO, 8);
    memcpy(entries[used].subcomtb_routine, DEFAULT_HANDLER_NAME, 8);
    memset(entries[used].subcomtb_token, ' ', 16);
    used++;

    /* LINK environment */
    memcpy(entries[used].subcomtb_name, DEFAULT_HOSTENV_LINK, 8);
    memcpy(entries[used].subcomtb_routine, DEFAULT_HANDLER_NAME, 8);
    memset(entries[used].subcomtb_token, ' ', 16);
    used++;

    /* Populate header */
    hdr->subcomtb_first = entries;
    hdr->subcomtb_total = DEFAULT_SUBCOMTB_ENTRIES;
    hdr->subcomtb_used = used;
    hdr->subcomtb_length = SUBCOMTB_ENTRY_LEN;
    memcpy(hdr->subcomtb_initial, DEFAULT_HOSTENV_MVS, 8);
    memset(hdr->_filler1, 0, 8);
    memset(hdr->subcomtb_ffff, 0xFF, 8);

    /* Link into PARMBLOCK */
    pb->parmblock_subcomtb = hdr;

    *hdr_out = hdr;
    return 0;

cleanup:
    /* Partial cleanup */
    if (entries != NULL)
    {
        void *p = entries;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (hdr != NULL)
    {
        void *p = hdr;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    return 20;
}

/* ------------------------------------------------------------------ */
/*  init_irxexte - Create the REXX Vector of External Entry Points    */
/* ------------------------------------------------------------------ */

static int init_irxexte(struct irxexte **exte_out,
                        struct envblock *envblk)
{
    struct irxexte *exte = NULL;

    ALLOC(exte, sizeof(struct irxexte), envblk);

    exte->irxexte_entry_count = IRXEXTE_ENTRY_COUNT;

    /* Install default replaceable routines.
     * For Phase 1, most are NULL stubs. As modules are implemented
     * they will be wired in here.
     *
     * The pattern is: each slot has a "active routine" pointer and
     * a "default routine" pointer. Active = what gets called.
     * Default = our built-in. Active can be overridden via MODNAMET.
     */

    /* Phase 1: Storage, User ID, Message ID */
    exte->irxinit = NULL; /* Will be set after init completes */
    exte->irxterm = NULL; /* Same */
    exte->irxuid = (void *)irxuid;
    exte->userid_routine = (void *)irxuid;
    exte->irxmsgid = (void *)irxmsgid;
    exte->msgid_routine = (void *)irxmsgid;

    /* Phase 2+: stubs */
    exte->irxexec = NULL;
    exte->irxexcom = NULL;
    exte->irxjcl = NULL;
    exte->irxrlt = NULL;
    exte->irxsubcm = NULL;
    exte->irxic = NULL;
    exte->irxterma = NULL;
    exte->load_routine = NULL;
    exte->irxload = NULL;

    /* Phase 2 (WP-14): Default and active I/O routine */
    exte->io_routine = (void *)irxinout;
    exte->irxinout = (void *)irxinout;
    exte->stack_routine = NULL;
    exte->irxstk = NULL;
    exte->irxsay = NULL;
    exte->irxers = NULL;
    exte->irxhst = NULL;
    exte->irxhlt = NULL;
    exte->irxtxt = NULL;
    exte->irxlin = NULL;
    exte->irxrte = NULL;

    *exte_out = exte;
    return 0;

cleanup:
    return 20;
}

/* ------------------------------------------------------------------ */
/*  init_wkblk_int - Create internal work block (interpreter state)   */
/* ------------------------------------------------------------------ */

static int init_wkblk_int(struct irx_wkblk_int **wk_out,
                          struct envblock *envblk)
{
    struct irx_wkblk_int *wk = NULL;

    ALLOC(wk, sizeof(struct irx_wkblk_int), envblk);

    memcpy(wk->wkbi_id, WKBLK_INT_ID, 4);
    wk->wkbi_length = (int)sizeof(struct irx_wkblk_int);
    wk->wkbi_envblock = envblk;

    /* NUMERIC defaults */
    wk->wkbi_digits = NUMERIC_DIGITS_DEFAULT;
    wk->wkbi_fuzz = NUMERIC_FUZZ_DEFAULT;
    wk->wkbi_form = NUMFORM_SCIENTIFIC;

    /* TRACE default */
    wk->wkbi_trace = TRACE_NORMAL;

    /* Special variables */
    wk->wkbi_sigl = 0;
    wk->wkbi_rc = 0;

    /* All pointer fields are already NULL from irxstor zero-fill */

    *wk_out = wk;
    return 0;

cleanup:
    return 20;
}

/* ================================================================== */
/*  irxinit - Initialize a Language Processor Environment             */
/*                                                                    */
/*  Sequence (per Architecture Design v0.1.0, Section 6.3):           */
/*   1. Obtain/create RAB for current task                            */
/*   2. Allocate ENVBLOCK                                             */
/*   3. Allocate and populate PARMBLOCK                               */
/*   4. Allocate Work Block Extension (IBM standard)                  */
/*   5. Build IRXEXTE (resolve replaceable routines)                  */
/*   6. Initialize SUBCOMTB (host command environments)               */
/*   7. Initialize internal Work Block (interpreter state)            */
/*   8. Create irx_env_node and register in RAB chain                 */
/*   9. Call initialization exit (if defined) — Phase 6               */
/*  10. Return ENVBLOCK pointer to caller                             */
/*                                                                    */
/*  Returns: 0=OK, 20=init error, 28=storage error                   */
/* ================================================================== */

int irxinit(void *parms, struct envblock **envblock_ptr)
{
    int rc = 0;

    struct irx_rab *rab = NULL;
    struct envblock *envblk = NULL;
    struct parmblock *pb = NULL;
    struct workblok_ext *wkext = NULL;
    struct irxexte *exte = NULL;
    struct subcomtb_header *subcmd = NULL;
    struct irx_wkblk_int *wkbi = NULL;
    struct irx_env_node *node = NULL;

    if (envblock_ptr == NULL)
    {
        return 20;
    }

    /* 1. Obtain RAB */
    rc = irx_rab_obtain(&rab);
    if (rc != 0)
    {
        return 28;
    }

    /* 2. Allocate ENVBLOCK */
    {
        void *storage = NULL;
        rc = irxstor(RXSMGET, (int)sizeof(struct envblock),
                     &storage, NULL);
        if (rc != 0)
        {
            goto cleanup;
        }
        envblk = (struct envblock *)storage;
    }

    /* Populate ENVBLOCK eye-catcher and version */
    memcpy(envblk->envblock_id, ENVBLOCK_ID, 8);
    memcpy(envblk->envblock_version, ENVBLOCK_VERSION_0100, 4);
    envblk->envblock_length = (int)sizeof(struct envblock);

    /* 3. PARMBLOCK */
    rc = init_parmblock(&pb, parms, envblk);
    if (rc != 0)
    {
        goto cleanup;
    }
    envblk->envblock_parmblock = pb;

    /* 4. Work Block Extension (IBM standard) */
    {
        void *storage = NULL;
        rc = irxstor(RXSMGET, (int)sizeof(struct workblok_ext),
                     &storage, envblk);
        if (rc != 0)
        {
            goto cleanup;
        }
        wkext = (struct workblok_ext *)storage;
    }
    envblk->envblock_workblok_ext = wkext;

    /* 5. IRXEXTE */
    rc = init_irxexte(&exte, envblk);
    if (rc != 0)
    {
        goto cleanup;
    }
    envblk->envblock_irxexte = exte;

    /* 6. SUBCOMTB (host command environments) */
    rc = init_subcomtb(&subcmd, pb, envblk);
    if (rc != 0)
    {
        goto cleanup;
    }

    /* 7. Internal Work Block (interpreter state) */
    rc = init_wkblk_int(&wkbi, envblk);
    if (rc != 0)
    {
        goto cleanup;
    }

    /* Link internal work block via envblock_userfield */
    envblk->envblock_userfield = wkbi;

    /* 7a. BIF registry and core registrations */
    {
        struct irx_bif_registry *reg = NULL;
        rc = irx_bif_create(envblk, &reg);
        if (rc != 0)
        {
            goto cleanup;
        }
        wkbi->wkbi_bif_registry = reg;

        rc = irx_pars_register_core_bifs(envblk, reg);
        if (rc != 0)
        {
            goto cleanup;
        }
        rc = irx_bifstr_register(envblk, reg);
        if (rc != 0)
        {
            goto cleanup;
        }
    }

    /* 8. Create env_node and register in RAB */
    {
        void *storage = NULL;
        rc = irxstor(RXSMGET, (int)sizeof(struct irx_env_node),
                     &storage, envblk);
        if (rc != 0)
        {
            goto cleanup;
        }
        node = (struct irx_env_node *)storage;
    }

    memcpy(node->node_id, ENVNODE_ID, 4);
    node->node_length = (int)sizeof(struct irx_env_node);
    node->node_envblock = envblk;
    node->node_flags = ENVNODE_ACTIVE;

    rc = irx_rab_add_env(rab, node);
    if (rc != 0)
    {
        goto cleanup;
    }

    /* 9. Init exit — deferred to Phase 6 */

    /* 10. Success */
    *envblock_ptr = envblk;
    return 0;

cleanup:
    /* Reverse-order cleanup of whatever was allocated */
    /* Note: irxstor handles NULL gracefully */
    if (node != NULL)
    {
        void *p = node;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (wkbi != NULL)
    {
        void *p = wkbi;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    /* subcmd entries freed inside init_subcomtb on its own failure */
    if (exte != NULL)
    {
        void *p = exte;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (wkext != NULL)
    {
        void *p = wkext;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (pb != NULL)
    {
        void *p = pb;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (envblk != NULL)
    {
        void *p = envblk;
        irxstor(RXSMFRE, 0, &p, NULL);
    }

    return (rc != 0) ? rc : 20;
}
