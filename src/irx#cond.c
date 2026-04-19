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

/* ------------------------------------------------------------------ */
/*  irx_cond_errortext - Primary-code descriptive text                */
/*                                                                    */
/*  Table is sourced verbatim from SC28-1883-0 Appendix A (Error      */
/*  Numbers and Messages). The edition defines codes 3..45, 48, 49;   */
/*  gaps (1, 2, 46, 47, 50..90) are intentional and return "".        */
/*                                                                    */
/*  Code 40 uses the TSO/E-verified text "Incorrect call to routine", */
/*  which matches the Appendix A entry byte-for-byte (no divergence). */
/* ------------------------------------------------------------------ */

struct errtext_entry
{
    int code;
    const char *text;
};

/* Sorted by code ascending. Entries are pulled verbatim from SC28-1883-0
 * Appendix A (Error Numbers and Messages); gaps are intentional and
 * mirror the primary codes the edition does not define. */
static const struct errtext_entry g_errortext[] = {
    {3, "Program is unreadable"},
    {4, "Program interrupted"},
    {5, "Machine storage exhausted"},
    {6, "Unmatched \"/*\" or quote"},
    {7, "WHEN or OTHERWISE expected"},
    {8, "Unexpected THEN or ELSE"},
    {9, "Unexpected WHEN or OTHERWISE"},
    {10, "Unexpected or unmatched END"},
    {11, "Control stack full"},
    {12, "Clause > 500 characters"},
    {13, "Invalid character in data"},
    {14, "Incomplete DO/SELECT/IF"},
    {15, "Invalid hex constant"},
    {16, "Label not found"},
    {17, "Unexpected PROCEDURE"},
    {18, "THEN expected"},
    {19, "String or symbol expected"},
    {20, "Symbol expected"},
    {21, "Invalid data on end of clause"},
    {22, "Invalid character string"},
    {23, "Invalid SBCS/DBCS mixed string"},
    {24, "Invalid TRACE request"},
    {25, "Invalid sub-keyword found"},
    {26, "Invalid whole number"},
    {27, "Invalid DO syntax"},
    {28, "Invalid LEAVE or ITERATE"},
    {29, "Environment name too long"},
    {30, "Name or string > 250 characters"},
    {31, "Name starts with numeric or \".\""},
    {32, "Invalid use of stem"},
    {33, "Invalid expression result"},
    {34, "Logical value not 0 or 1"},
    {35, "Invalid expression"},
    {36, "Unmatched \"(\" in expression"},
    {37, "Unexpected \",\" or \")\""},
    {38, "Invalid template or pattern"},
    {39, "Evaluation stack overflow"},
    {40, "Incorrect call to routine"},
    {41, "Bad arithmetic conversion"},
    {42, "Arithmetic overflow/underflow"},
    {43, "Routine not found"},
    {44, "Function did not return data"},
    {45, "No data specified on function RETURN"},
    {48, "Failure in system service"},
    {49, "Interpreter failure"},
};

#define ERRORTEXT_COUNT \
    ((int)(sizeof(g_errortext) / sizeof(g_errortext[0])))

const char *irx_cond_errortext(int code)
{
    if (code < ERRORTEXT_CODE_MIN || code > ERRORTEXT_CODE_MAX)
    {
        return NULL;
    }
    for (int i = 0; i < ERRORTEXT_COUNT; i++)
    {
        if (g_errortext[i].code == code)
        {
            return g_errortext[i].text;
        }
    }
    return "";
}
