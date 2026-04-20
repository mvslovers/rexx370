/* ------------------------------------------------------------------ */
/*  irx#anch.c - ECTENVBK anchor library                              */
/*                                                                    */
/*  Implements the cold-path walk and push/pop discipline described   */
/*  in include/irxanchor.h. See CON-1 §6.1 for rationale.             */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stddef.h>

#include "irx.h"
#include "irxanchor.h"

#ifdef __MVS__
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

static struct envblock **ectenvbk_slot(void)
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

static struct envblock **ectenvbk_slot(void)
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
        /* Batch (no ECT reachable) — we still record the absence in
         * the ENVBLOCK so the cleanup path can be symmetric. */
        new_env->rexx370_prev = NULL;
        new_env->envblock_ectptr = NULL;
        return;
    }

    new_env->rexx370_prev = *slot;
#ifdef __MVS__
    new_env->envblock_ectptr = (char *)slot - ECTENVBK;
#else
    new_env->envblock_ectptr = NULL;
#endif
    *slot = new_env;
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
        /* Batch — nothing was installed; nothing to restore. */
        return;
    }

    /* Lenient pop: only unwind when we're still on top. A caller
     * that terminates environments out of LIFO order loses their
     * rexx370_prev link by design; the spec doesn't define nested
     * non-LIFO termination and IBM's IRXTERM is likewise lenient. */
    if (*slot == env)
    {
        *slot = env->rexx370_prev;
    }
}
