/* ------------------------------------------------------------------ */
/*  irx#envt.c — REXX370_BYTECODE / REXX370_BCDEBUG for a C host      */
/*                                                                    */
/*  Moved out of IRXINIT (#298): reading them there needed getenv()   */
/*  plus a check that the caller has a C runtime, and the two pulled  */
/*  libc370's lock and printf code into IRXINIT.  Only programs with  */
/*  a runtime of their own link this file: IRXJCL and the C tests.    */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <stdlib.h>

#include "irx.h"
#include "irxenvt.h"
#include "irxwkblk.h"

/* Case-insensitive string equality. */
static int env_eq_ci(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
        {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static int env_is_on(const char *e)
{
    return e[0] == '1' || env_eq_ci(e, "true") || env_eq_ci(e, "yes") ||
           env_eq_ci(e, "on");
}

static int env_is_off(const char *e)
{
    return e[0] == '0' || env_eq_ci(e, "false") || env_eq_ci(e, "no") ||
           env_eq_ci(e, "off");
}

void irx_env_toggles(struct envblock *envblock)
{
    if (envblock == NULL || envblock->envblock_workblok_ext == NULL)
    {
        return;
    }
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)envblock->envblock_workblok_ext;

    const char *e = getenv("REXX370_BYTECODE");
    if (e != NULL)
    {
        if (env_is_off(e))
        {
            wk->wkbi_use_bytecode = 0;
        }
        else if (env_is_on(e))
        {
            wk->wkbi_use_bytecode = 1;
        }
    }

    e = getenv("REXX370_BCDEBUG");
    if (e != NULL && env_is_on(e))
    {
        wk->wkbi_bc_debug = 1;
    }
}
