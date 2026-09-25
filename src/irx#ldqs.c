/* irx#ldqs.c - IRXLOAD's member reader: stdio (QSAM through libc370)
**
** One of the two implementations of irx_ld_read_member() (irxldrd.h).
** This one is linked into IRXLOAD, the batch exec load routine. It
** reads "DD:ddname(MEMBER)" through fopen/fgets, so it needs a C
** runtime: fine under IRXJCL and any C host, not in a TSO address
** space driven from assembler -- that is what src/irx#ldbp.c is for.
**
** Ref: SC28-1883-0 Chapter 16 (Exec Load Routine); GitHub #230
**
** (c) 2026 mvslovers - REXX/370 Project
*/

#ifdef __MVS__

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxldrd.h"
#include "irxload.h"

enum
{
    CL8_LEN = 8,   /* any CL8 (blank-padded) IBM field               */
    DD_PREFIX = 3, /* "DD:"                                          */
    /* "DD:" + ddname + "(" + member + ")" + NUL                      */
    DD_SPEC_LEN = DD_PREFIX + CL8_LEN + 1 + CL8_LEN + 1 + 1,
    LINE_BUF = 256 /* one record plus '\n' and NUL; FB 80 needs 82  */
};

int irx_ld_read_member(const char *ddname, const unsigned char *member8,
                       struct irx_ld_acc *acc)
{
    char dd_spec[DD_SPEC_LEN];

    int mlen = CL8_LEN;
    while (mlen > 0 && member8[mlen - 1] == ' ')
    {
        --mlen;
    }

    char *p = dd_spec;
    memcpy(p, "DD:", DD_PREFIX);
    p += DD_PREFIX;
    for (int i = 0; ddname[i] && i < CL8_LEN; i++)
    {
        *p++ = ddname[i];
    }
    *p++ = '(';
    for (int i = 0; i < mlen; i++)
    {
        *p++ = (char)member8[i];
    }
    *p++ = ')';
    *p = '\0';

    FILE *f = fopen(dd_spec, "r");
    if (!f)
    {
        return IRXLOAD_NOTFOUND;
    }

    /* libc370's FILE carries the DCB's RECFM and LRECL (clibio.h). */
    int recfm = IRX_LD_RECFM_UNKNOWN;
    if ((f->recfm & _FILE_RECFM_TYPE) == _FILE_RECFM_F)
    {
        recfm = IRX_LD_RECFM_F;
    }
    else if ((f->recfm & _FILE_RECFM_TYPE) == _FILE_RECFM_V)
    {
        recfm = IRX_LD_RECFM_V;
    }
    irx_ld_begin_member(acc, recfm, (int)f->lrecl);

    char linebuf[LINE_BUF];
    int rc = 0;
    while (rc == 0 && fgets(linebuf, (int)sizeof(linebuf), f))
    {
        rc = irx_ld_add_line(acc, linebuf, (int)strlen(linebuf));
    }
    (void)fclose(f); /* input only: nothing to flush, nothing to lose */
    return rc;
}

#else
/* The host build reads files in irx#load.c itself. An empty
 * translation unit produces no ESD entries and breaks the NCAL linker;
 * mirror irx#env.c's dummy. */
static int _irx_ldqs_dummy = 0;
#endif
