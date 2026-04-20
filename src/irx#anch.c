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
