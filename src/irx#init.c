/* ------------------------------------------------------------------ */
/*  irxinit.c - IRXINIT: Initialize a Language Processor Environment  */
/*                                                                    */
/*  Provides irx_init_initenvb(), the 9-step C-core that implements   */
/*  the INITENVB function code: previous-env lookup, PARMBLOCK        */
/*  inheritance, ENVBLOCK allocation, IRXANCHR slot claim, and        */
/*  ECTENVBK update for TSO environments.                              */
/*                                                                    */
/*  Also provides irxinit(), the IBM-compatible compat wrapper that   */
/*  calls irx_init_initenvb() and then installs the full IRXEXTE,     */
/*  SUBCOMTB, and interpreter Work Block.                             */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 15 (IRXINIT)                            */
/*  Ref: CON-1 §3.1 (ENVBLOCK), §3.2 (PARMBLOCK inheritance),         */
/*       §3.8 (IRXEXTE), §6.2 (env-type detection),                   */
/*       §6.3 (INITENVB algorithm)                                     */
/*  Ref: CON-3 (ECTENVBK semantics — greenfield-verified,             */
/*       non-greenfield behavior TBD pending IRXPROBE)                */
/*  Ref: CON-4 (VERSION='0042', SLOT_FREE=0x00)                       */
/*  Ref: WP-I1c.1                                                     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "irx.h"
#include "irx_init.h"
#include "irxanchr.h"
#include "irxbif.h"
#include "irxbifs.h"
#include "irxfunc.h"
#include "irxio.h"
#include "irxpars.h"
#include "irxwkblk.h"

/* Lock the CON-1 §3.1 ENVBLOCK size on MVS — the IBM-reserved tail
 * at +304..+319 must stay intact so the physical layout remains
 * byte-exact against SC28-1883-0/-4. Only meaningful on the real
 * target; host builds use 8-byte pointers and have a different layout
 * which is irrelevant since host tests never exchange binaries with MVS.
 * _Static_assert is C11; c2asm370 is gnu99 — use typedef-array idiom. */
#ifdef __MVS__
typedef char envblock_size_is_320_[(sizeof(struct envblock) == 320) ? 1 : -1];
#endif

/* ECT ENVBK slot offset inside the ECT control block (IBM SYS1.AMODGEN). */
#define ECT_ENVBK_OFF 0x030

/* Default host command environment names (8 bytes each, EBCDIC-safe). */
#define DEFAULT_HOSTENV_TSO  "TSO     "
#define DEFAULT_HOSTENV_MVS  "MVS     "
#define DEFAULT_HOSTENV_LINK "LINK    "
#define DEFAULT_HANDLER_NAME "IRXSTAM "

#define DEFAULT_SUBCOMTB_ENTRIES 8

/* Allocate via irxstor; jump to cleanup: on failure. */
#define ALLOC(ptr, size, envblk)                                  \
    do                                                            \
    {                                                             \
        void *_tmp = NULL;                                        \
        int _rc = irxstor(RXSMGET, (int)(size), &_tmp, (envblk)); \
        if (_rc != 0)                                             \
            goto cleanup;                                         \
        (ptr) = _tmp;                                             \
    } while (0)

/* ================================================================== */
/*  Shared helper: init_subcomtb                                      */
/* ================================================================== */

static int init_subcomtb(struct subcomtb_header **hdr_out,
                         struct parmblock *pb,
                         struct envblock *envblk)
{
    struct subcomtb_header *hdr = NULL;
    struct subcomtb_entry *entries = NULL;
    int used = 0;

    ALLOC(hdr, sizeof(struct subcomtb_header), envblk);
    ALLOC(entries,
          DEFAULT_SUBCOMTB_ENTRIES * sizeof(struct subcomtb_entry),
          envblk);

    memcpy(entries[used].subcomtb_name, DEFAULT_HOSTENV_MVS, 8);
    memcpy(entries[used].subcomtb_routine, DEFAULT_HANDLER_NAME, 8);
    memset(entries[used].subcomtb_token, ' ', 16);
    used++;

    memcpy(entries[used].subcomtb_name, DEFAULT_HOSTENV_TSO, 8);
    memcpy(entries[used].subcomtb_routine, DEFAULT_HANDLER_NAME, 8);
    memset(entries[used].subcomtb_token, ' ', 16);
    used++;

    memcpy(entries[used].subcomtb_name, DEFAULT_HOSTENV_LINK, 8);
    memcpy(entries[used].subcomtb_routine, DEFAULT_HANDLER_NAME, 8);
    memset(entries[used].subcomtb_token, ' ', 16);
    used++;

    hdr->subcomtb_first = entries;
    hdr->subcomtb_total = DEFAULT_SUBCOMTB_ENTRIES;
    hdr->subcomtb_used = used;
    hdr->subcomtb_length = SUBCOMTB_ENTRY_LEN;
    memcpy(hdr->subcomtb_initial, DEFAULT_HOSTENV_MVS, 8);
    memset(hdr->_filler1, 0, 8);
    memset(hdr->subcomtb_ffff, 0xFF, 8);

    pb->parmblock_subcomtb = hdr;

    *hdr_out = hdr;
    return 0;

cleanup:
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

/* ================================================================== */
/*  Shared helper: init_wkblk_int                                     */
/* ================================================================== */

static int init_wkblk_int(struct irx_wkblk_int **wk_out,
                          struct envblock *envblk)
{
    struct irx_wkblk_int *wk = NULL;

    ALLOC(wk, sizeof(struct irx_wkblk_int), envblk);

    memcpy(wk->wkbi_id, WKBLK_INT_ID, 4);
    wk->wkbi_length = (int)sizeof(struct irx_wkblk_int);
    wk->wkbi_envblock = envblk;

    wk->wkbi_digits = NUMERIC_DIGITS_DEFAULT;
    wk->wkbi_fuzz = NUMERIC_FUZZ_DEFAULT;
    wk->wkbi_form = NUMFORM_SCIENTIFIC;

    wk->wkbi_trace = TRACE_NORMAL;
    wk->wkbi_sigl = 0;
    wk->wkbi_rc = 0;

    *wk_out = wk;
    return 0;

cleanup:
    return 20;
}

/* ================================================================== */
/*  irx_init_initenvb — INITENVB 9-step C-core (WP-I1c.1)            */
/*                                                                    */
/*  Steps:                                                            */
/*   1. Previous-env lookup (caller hint → TCB anchor find; stub for  */
/*      parent-TCB walk and module fallback — WP-I1c.2)              */
/*   2. PARMBLOCK build with flags/mask inheritance (CON-1 §3.2)      */
/*   3. Env-type detection: TSOFL from parmblock or anch_tso()        */
/*   4. ENVBLOCK allocation (VERSION='0042', 320 bytes on MVS)        */
/*   5. PARMBLOCK copy allocation and link                            */
/*   6. IRXEXTE placeholder (zeroed; COUNT=IRXEXTE_ENTRY_COUNT)       */
/*   7. IRXANCHR slot allocation                                      */
/*   8. ECTENVBK claim-if-null (TSO only, MVS only; CON-3 TBD)        */
/*   9. Return *out_envblock, reason_code=0                           */
/*                                                                    */
/*  Returns: 0=OK, 20=error (out_reason_code set)                    */
/* ================================================================== */

int irx_init_initenvb(struct envblock *prev_envblock,
                      struct parmblock *caller_parmblock,
                      uint32_t user_field,
                      struct envblock **out_envblock,
                      int *out_reason_code)
{
    struct envblock *envblk = NULL;
    struct parmblock *pb_copy = NULL;
    struct irxexte *exte = NULL;
    int reason = 0;
    int is_tso = 0; /* avoid name clash with tsofl macro in irx.h */

    /* Effective parmblock fields, computed in step 2. */
    unsigned char eff_flags[4];
    unsigned char eff_masks[4];
    unsigned char eff_language[3];
    int eff_subpool = 0;

    if (out_envblock == NULL || out_reason_code == NULL)
    {
        return 20;
    }

    /* ----------------------------------------------------------------
     * Step 1: Previous-env lookup.
     *
     * Priority order (CON-1 §6.3):
     *   a) Caller-supplied prev_envblock hint (if eye-catcher valid)
     *   b) Non-reentrant: find by current TCB in IRXANCHR table
     *   c) Parent-TCB walk (TSO only)    — stubbed, WP-I1c.2
     *   d) Module fallback (LOAD/BLDL)   — stubbed, WP-I1c.2
     * ---------------------------------------------------------------- */
    {
        struct envblock *prev = NULL;

        /* (a) Caller-supplied hint */
        if (prev_envblock != NULL &&
            memcmp(prev_envblock->envblock_id, ENVBLOCK_ID, 8) == 0)
        {
            prev = prev_envblock;
        }

        /* (b) TCB-based lookup via IRXANCHR */
        if (prev == NULL)
        {
            void *tcb = NULL;
#ifdef __MVS__
            tcb = *(void **)0x21C; /* PSATOLD */
#endif
            if (tcb != NULL)
            {
                irxanchr_entry_t *slot = irx_anchor_find_by_tcb(tcb);
                if (slot != NULL)
                {
                    prev = (struct envblock *)(unsigned long)slot->envblock_ptr;
                }
            }
        }

        /* Resolve the effective parmblock from prev (if any). */
        if (prev != NULL && prev->envblock_parmblock != NULL)
        {
            struct parmblock *prev_pb =
                (struct parmblock *)prev->envblock_parmblock;
            memcpy(eff_flags, prev_pb->parmblock_flags, 4);
            memcpy(eff_masks, prev_pb->parmblock_masks, 4);
            memcpy(eff_language, prev_pb->parmblock_language, 3);
            eff_subpool = prev_pb->parmblock_subpool;
        }
        else
        {
            /* Defaults */
            memset(eff_flags, 0, 4);
            memset(eff_masks, 0, 4);
            memcpy(eff_language, "ENU", 3);
            eff_subpool = 0;
        }
    }

    /* ----------------------------------------------------------------
     * Step 2: PARMBLOCK build with flags/mask inheritance.
     *
     * CON-1 §3.2: for each flag byte i:
     *   new_flags[i] = (prev_flags[i] & ~caller_masks[i])
     *                | (caller_flags[i] & caller_masks[i])
     *
     * Where caller_masks[i] == 0 means "inherit from prev" and
     * caller_masks[i] == 0xFF means "use caller value".
     * ---------------------------------------------------------------- */
    if (caller_parmblock != NULL)
    {
        int i;
        for (i = 0; i < 4; i++)
        {
            unsigned char cmask = caller_parmblock->parmblock_masks[i];
            eff_flags[i] =
                (eff_flags[i] & (unsigned char)(~cmask)) |
                (caller_parmblock->parmblock_flags[i] & cmask);
        }
        if (caller_parmblock->parmblock_subpool != 0)
        {
            eff_subpool = caller_parmblock->parmblock_subpool;
        }
        memcpy(eff_language, caller_parmblock->parmblock_language, 3);
    }

    /* ----------------------------------------------------------------
     * Step 3: Env-type detection (CON-1 §6.2).
     *
     * TSOFL=1 → TSO environment. Detection hierarchy:
     *   - If the caller's parmblock had tsofl_mask set, respect it.
     *   - Otherwise auto-detect via anch_tso().
     * ---------------------------------------------------------------- */
    {
        int explicit_tso = 0;
        if (caller_parmblock != NULL && caller_parmblock->tsofl_mask)
        {
            explicit_tso = 1;
            /* tsofl bit-field: MSB of flags byte 0. */
            is_tso = (caller_parmblock->tsofl != 0) ? 1 : 0;
        }
        if (!explicit_tso)
        {
            is_tso = anch_tso();
            /* Reflect auto-detected TSOFL back into the effective flags. */
            if (is_tso)
            {
                /* Set MSB of flags byte 0 (tsofl is the MSB). */
                eff_flags[0] |= 0x80;
            }
            else
            {
                eff_flags[0] &= (unsigned char)~0x80;
            }
        }
    }

    /* ----------------------------------------------------------------
     * Step 4: ENVBLOCK allocation (GETMAIN, subpool eff_subpool,
     * 320 bytes on MVS, eye-catcher 'ENVBLOCK', version '0042').
     * ---------------------------------------------------------------- */
    {
        void *storage = NULL;
        int rc = irxstor(RXSMGET, (int)sizeof(struct envblock),
                         &storage, NULL);
        if (rc != 0)
        {
            reason = 1;
            goto cleanup;
        }
        envblk = (struct envblock *)storage;
    }

    memcpy(envblk->envblock_id, ENVBLOCK_ID, 8);
    memcpy(envblk->envblock_version, ENVBLOCK_VERSION_0042, 4);
    envblk->envblock_length = (int)sizeof(struct envblock);
    envblk->envblock_userfield = (void *)(unsigned long)user_field;

    /* ----------------------------------------------------------------
     * Step 5: PARMBLOCK copy allocation.
     * ---------------------------------------------------------------- */
    {
        void *storage = NULL;
        int rc = irxstor(RXSMGET, (int)sizeof(struct parmblock),
                         &storage, envblk);
        if (rc != 0)
        {
            reason = 2;
            goto cleanup;
        }
        pb_copy = (struct parmblock *)storage;
    }

    memcpy(pb_copy->parmblock_id, PARMBLOCK_ID, 8);
    memcpy(pb_copy->parmblock_version, PARMBLOCK_VERSION_0042, 4);
    memcpy(pb_copy->parmblock_language, eff_language, 3);
    pb_copy->parmblock_subpool = eff_subpool;
    memcpy(pb_copy->parmblock_flags, eff_flags, 4);
    memset(pb_copy->parmblock_masks, 0, 4);
    memset(pb_copy->parmblock_addrspn, ' ', 8);
    memset(pb_copy->parmblock_ffff, 0xFF, 8);

    envblk->envblock_parmblock = pb_copy;

    /* ----------------------------------------------------------------
     * Step 6: IRXEXTE placeholder.
     * Allocate sizeof(struct irxexte) bytes, zero-filled. Set entry
     * count; all routine slots remain NULL (filled by compat wrapper
     * or WP-I1c.4).
     * ---------------------------------------------------------------- */
    {
        void *storage = NULL;
        int rc = irxstor(RXSMGET, (int)sizeof(struct irxexte),
                         &storage, envblk);
        if (rc != 0)
        {
            reason = 3;
            goto cleanup;
        }
        exte = (struct irxexte *)storage;
    }

    exte->irxexte_entry_count = IRXEXTE_ENTRY_COUNT;
    envblk->envblock_irxexte = exte;

    /* ----------------------------------------------------------------
     * Step 7: IRXANCHR slot allocation.
     *
     * The table is append-only (no recycling) and holds 62 usable
     * slots. On MVS each step starts fresh; on the cross-compile host
     * a long-running test process may exhaust the table. A full table
     * is not fatal: the environment is fully usable without a
     * registered slot — it just won't appear in irx_anchor_find_*
     * queries until a slot is freed by irxterm() and the next MVS step
     * starts with a clean table.
     * ---------------------------------------------------------------- */
    {
        void *tcb = NULL;
#ifdef __MVS__
        tcb = *(void **)0x21C; /* PSATOLD */
#endif
        uint32_t slot_token = 0;
        /* Ignore IRX_ANCHOR_RC_FULL — non-fatal, env remains usable. */
        (void)irx_anchor_alloc_slot(envblk, tcb, &slot_token);
    }

    /* ----------------------------------------------------------------
     * Step 8: ECTENVBK claim-if-null (TSO + MVS only).
     *
     * Conservative semantics pending IRXPROBE verification. CON-3
     * greenfield observation showed IBM writes ECTENVBK on init, but
     * the non-greenfield case (slot already set) is TBD. We claim
     * the slot only when NULL to avoid trampling other environments.
     * If IRXPROBE shows IBM does unconditional overwrite we can flip.
     *
     * Not executed on host builds — anch_tso() always returns 0 on
     * host, so is_tso is 0 anyway; the #ifdef also avoids the
     * (ect + ECT_ENVBK_OFF) pointer math on a host-simulated address.
     * ---------------------------------------------------------------- */
#ifdef __MVS__
    if (is_tso)
    {
        void *ect = anch_walk();
        if (ect != NULL)
        {
            struct envblock **slot =
                (struct envblock **)((char *)ect + ECT_ENVBK_OFF);
            if (*slot == NULL)
            {
                *slot = envblk;
                envblk->envblock_ectptr = ect;
            }
            else
            {
                /* Slot occupied — record ECT for tracing but don't
                 * overwrite the anchor. */
                envblk->envblock_ectptr = ect;
            }
        }
    }
#endif

    /* ----------------------------------------------------------------
     * Step 9: Return the new environment.
     * ---------------------------------------------------------------- */
    *out_envblock = envblk;
    *out_reason_code = 0;
    return 0;

cleanup:
    if (exte != NULL)
    {
        void *p = exte;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (pb_copy != NULL)
    {
        void *p = pb_copy;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (envblk != NULL)
    {
        void *p = envblk;
        irxstor(RXSMFRE, 0, &p, NULL);
    }

    *out_reason_code = reason;
    return 20;
}

/* ================================================================== */
/*  irx_findenvb — FINDENVB C-core (WP-I1c.2)                   */
/*                                                                    */
/*  Returns the most recently allocated (highest token) active,       */
/*  non-reentrant IRXANCHR slot whose TCB matches PSATOLD.            */
/*                                                                    */
/*  We iterate the table directly (rather than calling                */
/*  irx_anchor_find_by_tcb) for two reasons:                          */
/*    1. irx_anchor_find_by_tcb returns the highest-token entry       */
/*       regardless of the RENTRANT flag; we need to skip reentrant   */
/*       environments.                                                */
/*    2. irx_anchor_find_by_tcb early-exits on NULL tcb, but on the  */
/*       cross-compile host all slots record tcb_ptr=0 (PSATOLD is   */
/*       unavailable), so the NULL guard would prevent host testing.  */
/*                                                                    */
/*  Returns: 0=found, 4=no non-reentrant env on this TCB.            */
/* ================================================================== */

int irx_findenvb(struct envblock **out_envblock, int *out_reason_code)
{
    irxanchr_header_t *hdr;
    irxanchr_entry_t *slots;
    irxanchr_entry_t *best = NULL;
    uint32_t tcbptr;
    uint32_t i;

    if (out_envblock == NULL || out_reason_code == NULL)
    {
        if (out_reason_code != NULL)
        {
            *out_reason_code = 4;
        }
        return 4;
    }

    *out_envblock = NULL;
    *out_reason_code = 0;

    {
        void *tcb = NULL;
#ifdef __MVS__
        tcb = *(void **)0x21C; /* PSATOLD */
#endif
        tcbptr = (uint32_t)(unsigned long)tcb;
    }

    if (irx_anchor_get_handle(&hdr) != IRX_ANCHOR_RC_OK)
    {
        *out_reason_code = 4;
        return 4;
    }

    slots = (irxanchr_entry_t *)((char *)hdr + sizeof(irxanchr_header_t));

    for (i = 0; i < hdr->used; i++)
    {
        struct envblock *eb;
        struct parmblock *pb;

        if (slots[i].envblock_ptr == IRXANCHR_SLOT_FREE ||
            slots[i].envblock_ptr == IRXANCHR_SLOT_SENTINEL)
        {
            continue;
        }
        if (!(slots[i].flags & IRXANCHR_FLAG_IN_USE))
        {
            continue;
        }
        if (slots[i].tcb_ptr != tcbptr)
        {
            continue;
        }

        /* On MVS (24-bit), the stored uint32_t holds the full address.
         * On the 64-bit cross-compile host, alloc_slot stashed the full
         * pointer in rsvd1 to avoid truncation (see irx#anch.c). */
#ifdef __MVS__
        eb = (struct envblock *)(unsigned long)slots[i].envblock_ptr;
#else
        {
            void *full_ptr = NULL;
            memcpy(&full_ptr, slots[i].rsvd1, sizeof(void *));
            eb = (struct envblock *)full_ptr;
        }
#endif
        if (eb == NULL || memcmp(eb->envblock_id, ENVBLOCK_ID, 8) != 0)
        {
            continue;
        }

        /* FINDENVB only returns non-reentrant environments (SC28-1883-0 §14). */
        pb = (struct parmblock *)eb->envblock_parmblock;
        if (pb != NULL && pb->rentrant)
        {
            continue;
        }

        if (best == NULL || slots[i].token > best->token)
        {
            best = &slots[i];
        }
    }

    if (best == NULL)
    {
        *out_reason_code = 4;
        return 4;
    }

    /* Return the envblock address using the same full-pointer recovery
     * as used in the loop above to avoid 32-bit truncation on host. */
#ifdef __MVS__
    *out_envblock = (struct envblock *)(unsigned long)best->envblock_ptr;
#else
    {
        void *full_ptr = NULL;
        memcpy(&full_ptr, best->rsvd1, sizeof(void *));
        *out_envblock = (struct envblock *)full_ptr;
    }
#endif
    return 0;
}

/* ================================================================== */
/*  irx_chekenvb — CHEKENVB C-core (WP-I1c.2)                   */
/*                                                                    */
/*  Validates a caller-supplied ENVBLOCK address by checking          */
/*  (1) the 'ENVBLOCK' eye-catcher at offset +0 and                  */
/*  (2) that the address appears in an active IRXANCHR slot.          */
/*                                                                    */
/*  Returns: 0=valid, 20=invalid (out_reason_code set).              */
/* ================================================================== */

int irx_chekenvb(struct envblock *envblock, int *out_reason_code)
{
    irxanchr_entry_t *slot;

    if (out_reason_code == NULL)
    {
        return 20;
    }

    *out_reason_code = 0;

    /* NULL or missing eye-catcher — cannot be a valid ENVBLOCK. */
    if (envblock == NULL ||
        memcmp(envblock->envblock_id, ENVBLOCK_ID, 8) != 0)
    {
        *out_reason_code = 4;
        return 20;
    }

    /* Eye-catcher is present; confirm it is registered in IRXANCHR. */
    slot = irx_anchor_find_by_envblock(envblock);
    if (slot == NULL)
    {
        *out_reason_code = 8;
        return 20;
    }

    return 0;
}

/* ================================================================== */
/*  irx_dispatch — central IRXINIT dispatcher (WP-I1c.2)             */
/*                                                                    */
/*  Routes a CL8 function code to the appropriate C-core.            */
/*  Designed so WP-I1c.5 (HLASM entry-point wrapper) can call a      */
/*  single C entry point after parsing the caller VLIST.             */
/*                                                                    */
/*  Unknown function code: RC=20, RSN=12.                            */
/* ================================================================== */

int irx_dispatch(const char funccode[IRXINIT_FUNCCODE_LEN],
                 struct envblock *prev_envblock,
                 struct parmblock *caller_parmblock,
                 uint32_t user_field,
                 struct envblock **envblock_inout,
                 int *out_reason_code)
{
    if (funccode == NULL || envblock_inout == NULL || out_reason_code == NULL)
    {
        if (out_reason_code != NULL)
        {
            *out_reason_code = 12;
        }
        return 20;
    }

    if (memcmp(funccode, "INITENVB", 8) == 0)
    {
        return irx_init_initenvb(prev_envblock, caller_parmblock,
                                 user_field, envblock_inout, out_reason_code);
    }

    if (memcmp(funccode, "FINDENVB", 8) == 0)
    {
        (void)prev_envblock;
        (void)caller_parmblock;
        (void)user_field;
        return irx_findenvb(envblock_inout, out_reason_code);
    }

    if (memcmp(funccode, "CHEKENVB", 8) == 0)
    {
        (void)prev_envblock;
        (void)caller_parmblock;
        (void)user_field;
        return irx_chekenvb(*envblock_inout, out_reason_code);
    }

    *out_reason_code = 12;
    return 20;
}

/* ================================================================== */
/*  irxinit — IBM-compatible IRXINIT wrapper                         */
/*                                                                    */
/*  Calls irx_init_initenvb() for the core 9 steps, then installs    */
/*  the full IRXEXTE (real function pointers), SUBCOMTB, internal     */
/*  Work Block, and BIF registry required by the interpreter.         */
/*                                                                    */
/*  On host (non-MVS) builds, writes the simulated ECTENVBK slot if  */
/*  it is currently NULL, preserving the read-mostly test semantics  */
/*  expected by tstphas1 and tstanrm (anch_tso() returns 0 on host,  */
/*  so irx_init_initenvb() step 8 is skipped — the host write here   */
/*  stands in for it without calling the deprecated anch_push()).     */
/*                                                                    */
/*  Returns: 0=OK, 20=init error                                      */
/* ================================================================== */

int irxinit(void *parms, struct envblock **envblock_ptr)
{
    struct envblock *envblk = NULL;
    struct workblok_ext *wkext = NULL;
    struct irxexte *exte = NULL;
    struct subcomtb_header *subcmd = NULL;
    struct irx_wkblk_int *wkbi = NULL;
    int reason = 0;
    int rc;

    if (envblock_ptr == NULL)
    {
        return 20;
    }

    /* Call the 9-step C-core. Pass caller's parmblock directly;
     * TSOFL auto-detection happens inside irx_init_initenvb(). */
    rc = irx_init_initenvb(NULL,
                           (struct parmblock *)parms,
                           0,
                           &envblk,
                           &reason);
    if (rc != 0)
    {
        return 20;
    }

    /* Allocate the Work Block Extension (IBM standard control block). */
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

    /* Fill in real IRXEXTE function pointers (replacing the placeholder
     * installed by irx_init_initenvb() step 6 which left all slots NULL).
     * Phase 2+: stubs remain NULL until the relevant module is implemented. */
    exte = (struct irxexte *)envblk->envblock_irxexte;

    exte->irxinit = NULL; /* Set to self after init completes — Phase 6 */
    exte->irxterm = NULL; /* Same */
    exte->irxuid = (void *)irxuid;
    exte->userid_routine = (void *)irxuid;
    exte->irxmsgid = (void *)irxmsgid;
    exte->msgid_routine = (void *)irxmsgid;

    exte->irxexec = NULL;
    exte->irxexcom = NULL;
    exte->irxjcl = NULL;
    exte->irxrlt = NULL;
    exte->irxsubcm = NULL;
    exte->irxic = NULL;
    exte->irxterma = NULL;
    exte->load_routine = NULL;
    exte->irxload = NULL;

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

    /* SUBCOMTB (host command environments). */
    {
        struct parmblock *pb = (struct parmblock *)envblk->envblock_parmblock;
        rc = init_subcomtb(&subcmd, pb, envblk);
        if (rc != 0)
        {
            goto cleanup;
        }
    }

    /* Internal Work Block (interpreter state). */
    rc = init_wkblk_int(&wkbi, envblk);
    if (rc != 0)
    {
        goto cleanup;
    }
    envblk->envblock_userfield = wkbi;

    /* BIF registry and core registrations (WP-21a). */
    {
        struct irx_bif_registry *reg = NULL;
        rc = irx_bif_create(envblk, &reg);
        if (rc != 0)
        {
            goto cleanup;
        }
        wkbi->wkbi_bif_registry = reg;

        rc = irx_bif_register_all(envblk, reg);
        if (rc != 0)
        {
            goto cleanup;
        }
    }

    /* Host-only: simulate ECTENVBK slot write for cross-compile tests.
     * On MVS, irx_init_initenvb() step 8 handles ECTENVBK for TSO
     * environments. On host, anch_tso() returns 0 so step 8 is skipped;
     * this block writes the simulation slot with read-mostly semantics
     * (claim-if-null) so tstphas1 and tstanrm continue to pass. */
#ifndef __MVS__
    {
        /* ectenvbk_slot() lives in irx#anch.c; test programs define the
         * _simulated_ectenvbk global that the host build reads. */
        extern struct envblock **ectenvbk_slot(void);
        struct envblock **slot = ectenvbk_slot();
        if (slot != NULL && *slot == NULL)
        {
            *slot = envblk;
        }
    }
#endif

    *envblock_ptr = envblk;
    return 0;

cleanup:
    /* irxterm handles full teardown including IRXANCHR slot (WP-I1c.3). */
    if (envblk != NULL)
    {
        irxterm(envblk);
    }
    return 20;
}
