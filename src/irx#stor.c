/* IRX#STOR.C - Storage Management Replaceable Routine
**
** All memory allocation/deallocation for REXX/370 goes through
** this routine. It is a replaceable routine — callers can install
** a custom implementation via the Module Name Table (MODNAMET).
**
** Behaviour:
**   MVS  → getmain/freemain (clibos.h) for ALL subpools, including 0.
**          crent370 getmain() embeds an 8-byte prefix (subpool +
**          length) so freemain() recovers them — no caller-side
**          length tracking required. Bypassing the calloc/free path
**          keeps irxstor independent of CLIBCRT, so the entry-point
**          wrappers (asm/irxinit.asm, asm/irxterm.asm) can dispatch
**          into the C-core without going through @@CRT0 first.
**          Caveat: getmain() calls wtof() on its failure path, which
**          IS CLIBCRT-dependent — see #85.
**   Host → calloc/free (cross-compile / unit tests).
**
** Ref: SC28-1883-0, Chapter 16 (Storage Management)
** Ref: Architecture Design v0.1.0, Section 5.5
** Ref: GitHub mvslovers/rexx370#85
*/

#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxwkblk.h"

#ifdef __MVS__
#include <clibos.h> /* getmain(), freemain() — crent370 */
#endif

/* ------------------------------------------------------------------ */
/*  irxstor - Acquire or release storage                              */
/*                                                                    */
/*  function  - RXSMGET (0) = acquire, RXSMFRE (1) = release          */
/*  length    - RXSMGET: requested size. RXSMFRE: size to free.       */
/*  addr_ptr  - RXSMGET: output addr. RXSMFRE: input addr.            */
/*  envblock  - Owning ENVBLOCK (may be NULL during bootstrap)        */
/*                                                                    */
/*  Returns: 0=OK, 20=storage not available or invalid address        */
/* ------------------------------------------------------------------ */

int irxstor(int function, int length, void **addr_ptr,
            struct envblock *envblock)
{
#ifdef __MVS__
    int subpool = 0;

    /* Determine subpool from PARMBLOCK if available */
    if (envblock != NULL && envblock->envblock_parmblock != NULL)
    {
        struct parmblock *pb = (struct parmblock *)envblock->envblock_parmblock;
        if (pb->parmblock_subpool > 0)
        {
            subpool = pb->parmblock_subpool;
        }
    }
#else
    (void)envblock;
#endif

    switch (function)
    {
        case RXSMGET:
            if (length <= 0 || addr_ptr == NULL)
            {
                return 20;
            }
#ifdef __MVS__
            {
                /* getmain() zero-fills internally and embeds the
                 * subpool + length in an 8-byte prefix that
                 * freemain() recovers, so no caller-side length
                 * tracking is needed. */
                void *ptr = getmain((unsigned)length, (unsigned)subpool);
                if (ptr == NULL)
                {
                    return 20;
                }
                *addr_ptr = ptr;
            }
#else
            {
                void *ptr = calloc(1, (size_t)length);
                if (ptr == NULL)
                {
                    return 20;
                }
                *addr_ptr = ptr;
            }
#endif
            return 0;

        case RXSMFRE:
            if (addr_ptr == NULL || *addr_ptr == NULL)
            {
                return 20;
            }
#ifdef __MVS__
            freemain(*addr_ptr);
#else
            free(*addr_ptr);
#endif
            *addr_ptr = NULL;
            return 0;

        default:
            return 20;
    }
}
