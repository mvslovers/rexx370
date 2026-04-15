/* IRX#STOR.C - Storage Management Replaceable Routine
**
** All memory allocation/deallocation for REXX/370 goes through
** this routine. It is a replaceable routine — callers can install
** a custom implementation via the Module Name Table (MODNAMET).
**
** Default behaviour:
**   Subpool 0 (or no specific subpool) → calloc/free (crent370 heap)
**   Subpool > 0 (from PARMBLOCK)       → getmain/freemain (clibos.h)
**
** Ref: SC28-1883-0, Chapter 16 (Storage Management)
** Ref: Architecture Design v0.1.0, Section 5.5
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

    switch (function)
    {
        case RXSMGET:
            if (length <= 0 || addr_ptr == NULL)
            {
                return 20;
            }
#ifdef __MVS__
            if (subpool > 0)
            {
                /* Specific subpool: use GETMAIN R,LV=,SP= */
                void *ptr = getmain((unsigned)length, subpool);
                if (ptr == NULL)
                {
                    return 20;
                }
                memset(ptr, 0, (unsigned)length);
                *addr_ptr = ptr;
            }
            else
#endif
            {
                /* Default: crent370 heap (calloc zeros the memory) */
                void *ptr = calloc(1, (size_t)length);
                if (ptr == NULL)
                {
                    return 20;
                }
                *addr_ptr = ptr;
            }
            return 0;

        case RXSMFRE:
            if (addr_ptr == NULL || *addr_ptr == NULL)
            {
                return 20;
            }
#ifdef __MVS__
            if (subpool > 0)
            {
                freemain(*addr_ptr);
            }
            else
#endif
            {
                free(*addr_ptr);
            }
            *addr_ptr = NULL;
            return 0;

        default:
            return 20;
    }
}
