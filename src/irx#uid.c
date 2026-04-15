/* ------------------------------------------------------------------ */
/*  irxuid.c - User ID Replaceable Routine                            */
/*                                                                    */
/*  Returns the current user ID.                                      */
/*  TSO: extracts from PSCB (Protected Step Control Block)            */
/*  Non-TSO: extracts from ACEE (if RACF) or JCT (job name)          */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 16 (User ID Routine)                   */
/*  Ref: Architecture Design v0.1.0, Section 5.6                     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxfunc.h"

int irxuid(char *userid, struct envblock *envblock)
{
    if (userid == NULL)
    {
        return 20;
    }

    /* Initialize to blanks */
    memset(userid, ' ', 8);

#ifdef __MVS__
    /* MVS 3.8j: Navigate PSA -> ASCB -> ASXB -> LWA -> PSCB
     * PSCB+X'00' = PSCBUSER (7 bytes, blank-padded)
     *
     * Fallback: ASCB -> ASXB -> ACEE -> ACEEUNAM (if RACF active)
     * Fallback: JCT jobname
     */

    /* PSA+X'224' -> current ASCB */
    void *ascb = *(void **)0x224;
    if (ascb != NULL)
    {
        /* ASCB+X'6C' -> ASXB */
        void *asxb = *(void **)((char *)ascb + 0x6C);
        if (asxb != NULL)
        {
            /* ASXB+X'14' -> LWA (Logon Work Area) */
            void *lwa = *(void **)((char *)asxb + 0x14);
            if (lwa != NULL)
            {
                /* LWA+X'18' -> PSCB */
                void *pscb = *(void **)((char *)lwa + 0x18);
                if (pscb != NULL)
                {
                    /* PSCB+X'00' = PSCBUSER (7 bytes) */
                    memcpy(userid, (char *)pscb, 7);
                    return 0;
                }
            }
        }
    }

    /* Fallback: return blanks (non-TSO batch) */
    /* TODO: try ACEE/JCT extraction */
#else
    /* Cross-compile: use getlogin() or environment */
    const char *user = "TESTUSER";
    int len = (int)strlen(user);
    if (len > 8)
    {
        len = 8;
    }
    memcpy(userid, user, len);
#endif

    return 0;
}
