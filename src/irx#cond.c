/* ------------------------------------------------------------------ */
/*  irx#cond.c - REXX/370 Condition helper                           */
/*                                                                    */
/*  Raises a SYNTAX / ERROR condition by populating                  */
/*  wkbi_last_condition on the given environment. Called by the      */
/*  arithmetic engine, the BIF registry, and other modules that      */
/*  need to surface a diagnostic to the REXX program.                 */
/*                                                                    */
/*  No globals; state lives exclusively on the ENVBLOCK.              */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 7 / Appendix E                         */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxcond.h"
#include "irxfunc.h"
#include "irxwkblk.h"

void irx_cond_raise(struct envblock *env, int code, int subcode,
                    const char *desc)
{
    if (env == NULL || env->envblock_userfield == NULL)
    {
        return;
    }

    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)env->envblock_userfield;
    struct irx_condition_info *ci = wk->wkbi_last_condition;

    if (ci == NULL)
    {
        void *p = NULL;
        if (irxstor(RXSMGET, (int)sizeof(struct irx_condition_info),
                    &p, env) != 0)
        {
            return;
        }
        ci = (struct irx_condition_info *)p;
        memset(ci, 0, sizeof(*ci));
        wk->wkbi_last_condition = ci;
    }

    ci->valid = 1;
    ci->code = code;
    ci->subcode = subcode;

    if (desc != NULL)
    {
        size_t dlen = strlen(desc);
        if (dlen >= sizeof(ci->desc))
        {
            dlen = sizeof(ci->desc) - 1;
        }
        memcpy(ci->desc, desc, dlen);
        ci->desc[dlen] = '\0';
    }
    else
    {
        ci->desc[0] = '\0';
    }
}
