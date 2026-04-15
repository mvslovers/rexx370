/* ------------------------------------------------------------------ */
/*  irxrab.c - REXX Anchor Block (RAB) Management                     */
/*                                                                    */
/*  Manages the TCB -> TCBUSER -> RAB -> ENVBLOCK chain.              */
/*  The RAB is created on first IRXINIT call for a task and           */
/*  destroyed when the last environment is terminated.                */
/*                                                                    */
/*  On MVS 3.8j, TCBUSER is at offset +X'A8' in the TCB.             */
/*  We chain our RAB by saving the previous TCBUSER value and         */
/*  restoring it on RAB release.                                      */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxrab.h"
#include "irxwkblk.h"

#ifdef __MVS__
/* MVS 3.8j: access TCB via PSA -> current TCB */
/* PSA+X'218' -> current TCB address */
/* TCB+X'A8'  -> TCBUSER field       */
#define PSA_CURTCB_OFFSET 0x218
#define TCB_USER_OFFSET   0x0A8

static void **get_tcbuser_ptr(void)
{
    void **psa_tcb = (void **)(*(int *)PSA_CURTCB_OFFSET);
    return (void **)((char *)psa_tcb + TCB_USER_OFFSET);
}
#else
/* Cross-compile: simulate TCBUSER with a global variable.
 * Defined in the test harness (test_phase1.c) or main program. */
extern void *_simulated_tcbuser;

static void **get_tcbuser_ptr(void)
{
    return &_simulated_tcbuser;
}
#endif

/* ------------------------------------------------------------------ */
/*  irx_rab_obtain - Get or create the RAB for the current task       */
/*                                                                    */
/*  If a RAB already exists (TCBUSER points to one with valid         */
/*  eye-catcher), return it. Otherwise allocate a new one.            */
/*                                                                    */
/*  Returns: 0=OK, 20=storage error                                   */
/* ------------------------------------------------------------------ */

int irx_rab_obtain(struct irx_rab **rab_ptr)
{
    void **tcbuser_ptr;
    struct irx_rab *rab;

    if (rab_ptr == NULL)
    {
        return 20;
    }

    tcbuser_ptr = get_tcbuser_ptr();

    /* Check if RAB already exists */
    if (*tcbuser_ptr != NULL)
    {
        rab = (struct irx_rab *)(*tcbuser_ptr);
        if (memcmp(rab->rab_id, RAB_ID, 4) == 0 &&
            (rab->rab_flags & RAB_ACTIVE))
        {
            *rab_ptr = rab;
            return 0;
        }
    }

    /* Allocate new RAB */
    void *storage = NULL;
    int rc = irxstor(RXSMGET, (int)sizeof(struct irx_rab),
                     &storage, NULL);
    if (rc != 0)
    {
        return 20;
    }
    rab = (struct irx_rab *)storage;

    /* Initialize RAB */
    memcpy(rab->rab_id, RAB_ID, 4);
    rab->rab_length = (int)sizeof(struct irx_rab);
    rab->rab_version = RAB_VERSION;
    rab->rab_env_count = 0;
    rab->rab_first = NULL;
    rab->rab_last = NULL;
    rab->rab_flags = RAB_ACTIVE;

    /* Save previous TCBUSER and install RAB */
    rab->rab_prev_tcbuser = *tcbuser_ptr;
    *tcbuser_ptr = rab;

    *rab_ptr = rab;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  irx_rab_release - Release the RAB for the current task            */
/*                                                                    */
/*  Only succeeds if no environments remain (rab_env_count == 0).     */
/*  Restores the previous TCBUSER value.                              */
/*                                                                    */
/*  Returns: 0=OK, 4=environments still active, 20=error              */
/* ------------------------------------------------------------------ */

int irx_rab_release(struct irx_rab *rab)
{
    void **tcbuser_ptr;

    if (rab == NULL ||
        memcmp(rab->rab_id, RAB_ID, 4) != 0)
    {
        return 20;
    }

    if (rab->rab_env_count > 0)
    {
        return 4;
    }

    /* Restore previous TCBUSER */
    tcbuser_ptr = get_tcbuser_ptr();
    *tcbuser_ptr = rab->rab_prev_tcbuser;

    /* Free RAB storage */
    void *ptr = rab;
    irxstor(RXSMFRE, (int)sizeof(struct irx_rab), &ptr, NULL);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  irx_rab_add_env - Add an environment node to the RAB chain        */
/* ------------------------------------------------------------------ */

int irx_rab_add_env(struct irx_rab *rab, struct irx_env_node *node)
{
    if (rab == NULL || node == NULL)
    {
        return 20;
    }

    node->node_rab = rab;
    node->node_next = NULL;
    node->node_prev = (struct irx_env_node *)rab->rab_last;

    if (rab->rab_last != NULL)
    {
        ((struct irx_env_node *)rab->rab_last)->node_next = node;
    }
    else
    {
        rab->rab_first = node;
    }

    rab->rab_last = node;
    rab->rab_env_count++;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  irx_rab_remove_env - Remove an environment node from the chain    */
/* ------------------------------------------------------------------ */

int irx_rab_remove_env(struct irx_rab *rab, struct irx_env_node *node)
{
    if (rab == NULL || node == NULL)
    {
        return 20;
    }

    if (node->node_prev != NULL)
    {
        node->node_prev->node_next = node->node_next;
    }
    else
    {
        rab->rab_first = node->node_next;
    }

    if (node->node_next != NULL)
    {
        node->node_next->node_prev = node->node_prev;
    }
    else
    {
        rab->rab_last = node->node_prev;
    }

    node->node_next = NULL;
    node->node_prev = NULL;
    node->node_rab = NULL;
    rab->rab_env_count--;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  irx_find_env - Find the most recent (current) environment         */
/*                                                                    */
/*  Returns: ENVBLOCK pointer, or NULL if none                        */
/* ------------------------------------------------------------------ */

struct envblock *irx_find_env(void)
{
    void **tcbuser_ptr;
    struct irx_rab *rab;
    struct irx_env_node *node;

    tcbuser_ptr = get_tcbuser_ptr();
    if (*tcbuser_ptr == NULL)
    {
        return NULL;
    }

    rab = (struct irx_rab *)(*tcbuser_ptr);
    if (memcmp(rab->rab_id, RAB_ID, 4) != 0)
    {
        return NULL;
    }

    /* Most recent = last in chain */
    node = (struct irx_env_node *)rab->rab_last;
    if (node == NULL)
    {
        return NULL;
    }

    return node->node_envblock;
}
