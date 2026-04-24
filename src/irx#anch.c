/* ------------------------------------------------------------------ */
/*  irx#anch.c - ECTENVBK anchor library                              */
/*                                                                    */
/*  Implements the cold-path walk and push/pop discipline described   */
/*  in include/irxanchr.h. See CON-1 §6.1 for rationale.              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stddef.h>
#include <string.h>

#include "irx.h"
#include "irxanchr.h"

#ifdef __MVS__
#include "clibos.h"
#include "clibppa.h"

/* Low-core / control-block offsets per IBM macros — see header.
 * Named after the IBM fields so reviewers can cross-check the walk
 * against SYS1.AMODGEN (IHAPSA, IHAASCB, IHAASXB, IKJLWA, IKJECT). */
#define PSAAOLD  0x224 /* PSA  -> ASCB of current address space */
#define ASCBASXB 0x06C /* ASCB -> ASXB                          */
#define ASXBLWA  0x014 /* ASXB -> LWA (TSO link-pack work area) */
#define LWAPECT  0x020 /* LWA  -> current ECT                   */
#define ECTENVBK 0x030 /* ECT  -> anchored ENVBLOCK             */

/* Dereference a pointer stored at (base + offset). Returns NULL when
 * base itself is NULL so callers can chain defensively. */
static void *deref_at(void *base, int offset)
{
    if (base == NULL)
    {
        return NULL;
    }
    return *(void **)((char *)base + offset);
}

void *anch_walk(void)
{
    void *ascb = *(void **)PSAAOLD;
    void *asxb = deref_at(ascb, ASCBASXB);
    void *lwa = deref_at(asxb, ASXBLWA);
    void *ect = deref_at(lwa, LWAPECT);
    return ect;
}

int anch_tso(void)
{
    CLIBPPA *ppa = __ppaget();
    if (ppa == NULL)
    {
        return 0;
    }
    /* Both bits count as "TSO-capable": PPAFLAG_TSOFG is the TSO
     * foreground (ready-prompt / CALL); PPAFLAG_TSOBG is TSO
     * background (batch job driving IKJEFT01). Pure batch
     * (EXEC PGM=... directly) leaves both clear.
     *
     * Note: the sibling CLIBCRT.crtflag bits carry the same names
     * but are never written by crent370 startup — detection lives
     * in the process-level CLIBPPA, not the per-task CLIBCRT. */
    return (ppa->ppaflag & (PPAFLAG_TSOFG | PPAFLAG_TSOBG)) != 0;
}

/* Intentionally non-static: the test harness forward-declares this
 * helper (see test/mvs/tstanrm.c) so Case-b and Case-c on MVS can
 * seed the real ECTENVBK slot instead of the unused host-only
 * simulation variable. Production callers stay on anch_push /
 * anch_pop / anch_curr. */
struct envblock **ectenvbk_slot(void)
{
    void *ect = anch_walk();
    if (ect == NULL)
    {
        return NULL;
    }
    return (struct envblock **)((char *)ect + ECTENVBK);
}

#else /* !__MVS__ --- cross-compile harness ---------------------- */

/* On the host we simulate ECTENVBK with a single slot. Test programs
 * define this as a tentative definition (void* keeps the forward-decl
 * burden off the test harness — we cast here). */
extern void *_simulated_ectenvbk;

/* Non-static for symmetry with the MVS branch; the test harness never
 * calls this on host (it goes through _simulated_ectenvbk directly). */
struct envblock **ectenvbk_slot(void)
{
    return (struct envblock **)&_simulated_ectenvbk;
}

void *anch_walk(void)
{
    /* Nothing consumes the raw ECT pointer on host builds, but the
     * push/pop path expects non-NULL when a slot is available. We
     * return the slot's address as a sentinel. */
    return (void *)&_simulated_ectenvbk;
}

int anch_tso(void)
{
    return 0;
}

#endif /* __MVS__ */

struct envblock *anch_curr(void)
{
    struct envblock **slot = ectenvbk_slot();
    if (slot == NULL)
    {
        return NULL;
    }
    return *slot;
}

void anch_push(struct envblock *new_env)
{
    if (new_env == NULL)
    {
        return;
    }

    struct envblock **slot = ectenvbk_slot();
    if (slot == NULL)
    {
        /* Batch — no anchor reachable. Only populate the local
         * ENVBLOCK field so the symmetric cleanup path still works. */
        new_env->envblock_ectptr = NULL;
        return;
    }

#ifdef __MVS__
    new_env->envblock_ectptr = (char *)slot - ECTENVBK;
#else
    new_env->envblock_ectptr = NULL;
#endif

    /* Read-mostly anchor per CON-1 §6.1: only claim the ECTENVBK
     * slot when it is NULL. Any non-NULL value means another REXX
     * (on MVS 3.8j that is typically a parallel BREXX environment,
     * or an earlier rexx370 environment that is still live) already
     * owns the anchor — leave it alone. The caller manages the new
     * ENVBLOCK pointer explicitly via the IRXINIT return value, per
     * SC28-1883-0 §15 for reentrant environments. */
    if (*slot == NULL)
    {
        *slot = new_env;
    }
}

void anch_pop(struct envblock *env)
{
    if (env == NULL)
    {
        return;
    }

    struct envblock **slot = ectenvbk_slot();
    if (slot == NULL)
    {
        return;
    }

    /* Only clear when we are still the anchor holder. Any other
     * value means either we never wrote the slot (another REXX was
     * already there at push time) or something else has taken over
     * since — leave the slot unchanged in both cases. */
    if (*slot == env)
    {
        *slot = NULL;
    }
}

/* ================================================================== */
/*  Part 2: IRXANCHR Registry API (WP-I1a.3)                         */
/* ================================================================== */

#ifdef __MVS__

int irx_anchor_get_handle(irxanchr_header_t **out_anchor)
{
    unsigned size = 0;
    char ac = 0;
    void *ptr;
    irxanchr_header_t *hdr;

    if (out_anchor == NULL)
    {
        return IRX_ANCHOR_RC_LOAD_FAIL;
    }

    ptr = __load(NULL, "IRXANCHR", &size, &ac);
    if (ptr == NULL)
    {
        return IRX_ANCHOR_RC_LOAD_FAIL;
    }

    hdr = (irxanchr_header_t *)ptr;
    if (memcmp(hdr->id, IRXANCHR_EYECATCHER, 8) != 0)
    {
        return IRX_ANCHOR_RC_BAD_EYE;
    }

    *out_anchor = hdr;
    return IRX_ANCHOR_RC_OK;
}

/* Atomic exchange using the S/370 CS spin-loop from crent370 __cs().
 * Always stores new_val and returns the old value. On single-CPU MVS
 * 3.8j, the CS succeeds in one iteration absent an interrupt. */
static uint32_t anchor_swap(uint32_t *mem, uint32_t new_val)
{
    return (uint32_t)__cs((void *)mem, (unsigned)new_val);
}

/* Atomic post-increment via crent370 __uinc(). Returns value before
 * the increment; caller adds 1 for a 1-based token. */
static uint32_t anchor_fetch_inc(uint32_t *mem)
{
    return (uint32_t)__uinc((void *)mem);
}

#else /* !__MVS__ — cross-compile host simulation */

/* Static table mirroring asm/irxanchr.asm for cross-compile testing.
 * Initialised once by irx_anchor_get_handle(). Exposed via
 * _irxanchr_host_buf() so tests can inspect or corrupt the buffer. */
static uint8_t _host_irxanchr[sizeof(irxanchr_header_t) +
                              IRXANCHR_TOTAL_SLOTS * 40];
static int _host_irxanchr_ready = 0;

static void host_anchor_init(void)
{
    irxanchr_header_t *hdr = (irxanchr_header_t *)_host_irxanchr;
    irxanchr_entry_t *slots;

    memset(_host_irxanchr, 0, sizeof(_host_irxanchr));
    memcpy(hdr->id, IRXANCHR_EYECATCHER, 8);
    memcpy(hdr->version, IRXANCHR_VERSION, 4);
    hdr->total = IRXANCHR_TOTAL_SLOTS;
    hdr->used = 0;
    hdr->length = 40;

    slots = (irxanchr_entry_t *)((char *)hdr + sizeof(irxanchr_header_t));
    slots[0].envblock_ptr = IRXANCHR_SLOT_SENTINEL; /* permanent sentinel — Slot 0 */
    slots[2].envblock_ptr = IRXANCHR_SLOT_SENTINEL; /* permanent sentinel — Slot 2 */

    _host_irxanchr_ready = 1;
}

void *_irxanchr_host_buf(void)
{
    if (!_host_irxanchr_ready)
    {
        host_anchor_init();
    }
    return (void *)_host_irxanchr;
}

int irx_anchor_get_handle(irxanchr_header_t **out_anchor)
{
    if (out_anchor == NULL)
    {
        return IRX_ANCHOR_RC_LOAD_FAIL;
    }
    if (!_host_irxanchr_ready)
    {
        host_anchor_init();
    }
    if (memcmp(_host_irxanchr, IRXANCHR_EYECATCHER, 8) != 0)
    {
        return IRX_ANCHOR_RC_BAD_EYE;
    }
    *out_anchor = (irxanchr_header_t *)_host_irxanchr;
    return IRX_ANCHOR_RC_OK;
}

/* Non-atomic host helpers — tests are single-threaded. */
static uint32_t anchor_swap(uint32_t *mem, uint32_t new_val)
{
    uint32_t old = *mem;
    *mem = new_val;
    return old;
}

static uint32_t anchor_fetch_inc(uint32_t *mem)
{
    uint32_t old = *mem;
    *mem = old + 1;
    return old;
}

#endif /* __MVS__ */

/* ---- common registry functions ---- */

int irx_anchor_alloc_slot(void *envblock, void *tcb, uint32_t *out_token)
{
    irxanchr_header_t *hdr;
    irxanchr_entry_t *slots;
    uint32_t *counter;
    uint32_t ebptr;
    uint32_t i, start;
    int rc;

    /* NULL envblock is indistinguishable from SLOT_FREE (0x00000000). */
    if (envblock == NULL || out_token == NULL)
    {
        return IRX_ANCHOR_RC_LOAD_FAIL;
    }

    rc = irx_anchor_get_handle(&hdr);
    if (rc != IRX_ANCHOR_RC_OK)
    {
        return rc;
    }

    slots = (irxanchr_entry_t *)((char *)hdr + sizeof(irxanchr_header_t));
    counter = (uint32_t *)(void *)hdr->reserved; /* token counter @ header+0x18 */
    ebptr = (uint32_t)(uintptr_t)envblock;

    /* Append-only from USED — free slots below the high-watermark are
     * never recycled (IBM-observed behaviour, CON-4 deviation register). */
    start = hdr->used;
    if (start < 1)
    {
        start = 1;
    }

    for (i = start; i < hdr->total; i++)
    {
        uint32_t old;

        /* Skip permanent sentinels (0xFFFFFFFF) without touching them. */
        if (slots[i].envblock_ptr == IRXANCHR_SLOT_SENTINEL)
        {
            continue;
        }

        /* Atomic claim: exchange SLOT_FREE → ebptr. If the slot was
         * already taken, restore and advance. On single-CPU MVS 3.8j
         * there are no SVCs between the two swaps, so the restore is safe. */
        old = anchor_swap(&slots[i].envblock_ptr, ebptr);
        if (old != IRXANCHR_SLOT_FREE)
        {
            anchor_swap(&slots[i].envblock_ptr, old);
            continue;
        }

        /* Populate aux fields before bumping USED: a concurrent
         * find_by_tcb scans 0..USED and must see a complete entry. */
        slots[i].token = anchor_fetch_inc(counter) + 1U;
        slots[i].tcb_ptr = (uint32_t)(uintptr_t)tcb;
        slots[i].flags = IRXANCHR_FLAG_IN_USE;

        /* USED = max(USED, i+1). Plain store is safe: no SVCs exist
         * between the claim CS and here on single-CPU MVS 3.8j. */
        if (hdr->used < i + 1)
        {
            hdr->used = i + 1;
        }

        *out_token = slots[i].token;
        return IRX_ANCHOR_RC_OK;
    }

    return IRX_ANCHOR_RC_FULL;
}

int irx_anchor_free_slot(void *envblock)
{
    irxanchr_header_t *hdr;
    irxanchr_entry_t *slots;
    uint32_t ebptr;
    uint32_t i;

    if (envblock == NULL)
    {
        return IRX_ANCHOR_RC_NOT_FOUND;
    }

    if (irx_anchor_get_handle(&hdr) != IRX_ANCHOR_RC_OK)
    {
        return IRX_ANCHOR_RC_NOT_FOUND;
    }

    slots = (irxanchr_entry_t *)((char *)hdr + sizeof(irxanchr_header_t));
    ebptr = (uint32_t)(uintptr_t)envblock;

    for (i = 0; i <= hdr->used && i < hdr->total; i++)
    {
        if (slots[i].envblock_ptr == ebptr)
        {
            /* Single aligned store — atomic on S/370. Leave token,
             * tcb_ptr, and flags intact for post-mortem debugging. */
            slots[i].envblock_ptr = IRXANCHR_SLOT_FREE;
            return IRX_ANCHOR_RC_OK;
        }
    }

    return IRX_ANCHOR_RC_NOT_FOUND;
}

irxanchr_entry_t *irx_anchor_find_by_envblock(void *envblock)
{
    irxanchr_header_t *hdr;
    irxanchr_entry_t *slots;
    uint32_t ebptr;
    uint32_t i;

    /* Sentinel values must never be returned as a valid match. */
    if (envblock == NULL || (uint32_t)(uintptr_t)envblock == IRXANCHR_SLOT_SENTINEL)
    {
        return NULL;
    }

    if (irx_anchor_get_handle(&hdr) != IRX_ANCHOR_RC_OK)
    {
        return NULL;
    }

    slots = (irxanchr_entry_t *)((char *)hdr + sizeof(irxanchr_header_t));
    ebptr = (uint32_t)(uintptr_t)envblock;

    for (i = 0; i <= hdr->used && i < hdr->total; i++)
    {
        if (slots[i].envblock_ptr == ebptr)
        {
            return &slots[i];
        }
    }

    return NULL;
}

irxanchr_entry_t *irx_anchor_find_by_tcb(void *tcb)
{
    irxanchr_header_t *hdr;
    irxanchr_entry_t *slots;
    uint32_t tcbptr;
    uint32_t i;
    irxanchr_entry_t *best = NULL;

    if (tcb == NULL)
    {
        return NULL;
    }

    if (irx_anchor_get_handle(&hdr) != IRX_ANCHOR_RC_OK)
    {
        return NULL;
    }

    slots = (irxanchr_entry_t *)((char *)hdr + sizeof(irxanchr_header_t));
    tcbptr = (uint32_t)(uintptr_t)tcb;

    for (i = 0; i <= hdr->used && i < hdr->total; i++)
    {
        if (slots[i].envblock_ptr == IRXANCHR_SLOT_FREE ||
            slots[i].envblock_ptr == IRXANCHR_SLOT_SENTINEL)
        {
            continue;
        }
        if (slots[i].tcb_ptr == tcbptr)
        {
            if (best == NULL || slots[i].token > best->token)
            {
                best = &slots[i];
            }
        }
    }

    return best;
}

void irx_anchor_table_reset(void)
{
    irxanchr_header_t *hdr;
    irxanchr_entry_t *slots;
    uint32_t i;

#ifndef __MVS__
    _host_irxanchr_ready = 0;
    host_anchor_init();
    return;
#endif

    if (irx_anchor_get_handle(&hdr) != IRX_ANCHOR_RC_OK)
    {
        return;
    }

    slots = (irxanchr_entry_t *)((char *)hdr + sizeof(irxanchr_header_t));
    memset(hdr->reserved, 0, 8); /* clear token counter at header+0x18 */
    hdr->used = 0;

    for (i = 0; i < hdr->total; i++)
    {
        memset(&slots[i], 0, sizeof(irxanchr_entry_t));
    }

    slots[0].envblock_ptr = IRXANCHR_SLOT_SENTINEL; /* permanent sentinel */
    slots[2].envblock_ptr = IRXANCHR_SLOT_SENTINEL; /* permanent sentinel */
}
