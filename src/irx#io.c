/* ------------------------------------------------------------------ */
/*  irx#io.c — IRXINOUT Default I/O Replaceable Routine               */
/*                                                                    */
/*  MVS (primary): irxinout — writes SAY/TRACE/error output to        */
/*  stdout + fflush. irx_jcl_dispatch_main redirects stdout to        */
/*  DD:SYSTSPRT before executing any exec, so SAY output lands in     */
/*  the JES2 spool SYSTSPRT dataset.                                  */
/*                                                                    */
/*  Host (cross-compile tests): irxinout_host — same logic, stdout    */
/*  is the terminal / test harness capture stream.                    */
/*                                                                    */
/*  IRXINIT step 6 wires the appropriate variant into                 */
/*  exte->io_routine and exte->irxinout via #ifdef __MVS__.           */
/*                                                                    */
/*  Future: WP-33-TSO adds irxinout_tso (TPUT, TSO foreground)       */
/*  with tso_flag-based dispatch at IRXINIT step 6.                   */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 16 (Replaceable Routines)               */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>

#include "irx.h"
#include "irxio.h"
#include "irxwkblk.h"
#include "lstring.h"

#ifdef __MVS__

int irxinout(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;

    switch (function)
    {
        case RXFWRITE:
        case RXFWRITERR:
        case RXFTWRITE:
            if (data != NULL && data->pstr != NULL && data->len > 0)
            {
                fwrite(data->pstr, 1, (size_t)data->len, stdout);
            }
            fputc('\n', stdout);
            fflush(stdout);
            return 0;

        case RXFREAD:
        case RXFREADP:
            /* TODO WP-33b: implement PULL / LINEIN */
            return 20;

        default:
            return 20;
    }
}

#else /* !__MVS__ */

int irxinout_host(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;

    switch (function)
    {
        case RXFWRITE:
        case RXFWRITERR:
        case RXFTWRITE:
            if (data != NULL && data->pstr != NULL && data->len > 0)
            {
                fwrite(data->pstr, 1, (size_t)data->len, stdout);
            }
            fputc('\n', stdout);
            return 0;

        case RXFREAD:
        case RXFREADP:
            /* TODO WP-33b: implement PULL / LINEIN */
            return 20;

        default:
            return 20;
    }
}

#endif /* __MVS__ */
