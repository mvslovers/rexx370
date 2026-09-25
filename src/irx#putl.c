/* ------------------------------------------------------------------ */
/*  irx#putl.c - host stand-in for asm/putlin.asm                     */
/*                                                                    */
/*  On MVS tso_put_line() in irx#tsio.c builds an IOPL and calls     */
/*  PUTLINE through the CSECT PUTLCALL. The host build swaps the .asm */
/*  for this file via [host].replace in project.toml -- the same      */
/*  arrangement asm/istso.asm has with src/irx#env.c -- and this file */
/*  provides tso_put_line() itself.                                   */
/*                                                                    */
/*  The call is captured rather than printed, so a host test can      */
/*  assert what irxinout_tso() would have written -- how many lines,  */
/*  with which length -- without an MVS deploy.                       */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#ifndef __MVS__

#include <string.h>

#include "irx.h"
#include "irxio.h"

char _simulated_putl_buf[TSO_LINE_MAX];
int _simulated_putl_len = 0;
int _simulated_putl_calls = 0;

int tso_put_line(const char *buf, int len)
{
    _simulated_putl_calls++;
    if (len > TSO_LINE_MAX)
    {
        len = TSO_LINE_MAX; /* cannot happen: irxinout_tso splits */
    }
    if (buf != NULL && len > 0)
    {
        memcpy(_simulated_putl_buf, buf, (size_t)len);
    }
    _simulated_putl_len = len;
    return 0;
}

#else
/* An empty translation unit produces no ESD entries and breaks the
 * NCAL linker; mirror irx#env.c's dummy. */
static int _irx_putl_dummy = 0;
#endif
