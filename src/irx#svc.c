/* ------------------------------------------------------------------ */
/*  irx#svc.c — LOAD and DELETE by inline assembler (#298)            */
/*                                                                    */
/*  Replaces libc370's __load()/__delete().  LOAD uses EPLOC with     */
/*  ERRET, so a missing module returns NULL instead of abending 806,  */
/*  and the search is the system's own (job pack, LPA, task library,  */
/*  link list); libc370 searched with BLDL first, which misses a      */
/*  module that is only in the LPA.                                   */
/*                                                                    */
/*  The labels in the LOAD below are file-scope in the assembler      */
/*  output, so this file holds exactly one LOAD.                      */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include "irxsvc.h"

#ifdef __MVS__

enum
{
    SVC_NAME_LEN = 8
};

/* CL8, blank padded. */
static void svc_name8(char out[SVC_NAME_LEN], const char *name)
{
    int i = 0;
    for (; i < SVC_NAME_LEN && name[i] != '\0'; i++)
    {
        out[i] = name[i];
    }
    for (; i < SVC_NAME_LEN; i++)
    {
        out[i] = ' ';
    }
}

void *irx_svc_load(const char *name)
{
    char name8[SVC_NAME_LEN];
    void *epa;

    if (name == 0 || name[0] == '\0')
    {
        return 0;
    }
    svc_name8(name8, name);
    __asm__ volatile("LOAD  EPLOC=(%1),ERRET=IRXSVLDE\n\t"
                     "LR\t%0,0\n\t"
                     "B\tIRXSVLDX\n"
                     "IRXSVLDE DS\t0H\n\t"
                     "SLR\t%0,%0\n"
                     "IRXSVLDX DS\t0H"
                     : "=r"(epa)
                     : "r"(name8)
                     : "0", "1", "14", "15", "memory");
    return epa;
}

int irx_svc_delete(const char *name)
{
    char name8[SVC_NAME_LEN];
    int rc;

    if (name == 0 || name[0] == '\0')
    {
        return 4;
    }
    svc_name8(name8, name);
    __asm__ volatile("DELETE EPLOC=(%1)\n\t"
                     "LR\t%0,15"
                     : "=r"(rc)
                     : "r"(name8)
                     : "0", "1", "14", "15", "memory");
    return rc;
}

#else
/* The host has no LOAD; keep the translation unit non-empty. */
typedef int irx_svc_host_unused;
#endif
