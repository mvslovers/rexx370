/* ------------------------------------------------------------------ */
/*  irx#io.c — IRXINOUT Default I/O Replaceable Routine               */
/*                                                                    */
/*  MVS (primary): irxinout_mvs — writes SAY/TRACE/error output to   */
/*  the SYSTSPRT DD via fopen("DD:SYSTSPRT","w"). The FILE* is opened  */
/*  lazily on first write; crent370 CRT teardown closes it at exit.   */
/*                                                                    */
/*  Host (cross-compile tests): irxinout_host — writes to stdout so  */
/*  the test harness can capture SAY/TRACE output.                    */
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

/* Lazily opened handle for the SYSTSPRT DD.
 * One handle per load-module residence; crent370 CRT teardown closes it. */
static FILE *g_systsprt_fp = NULL;

int irxinout_mvs(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;

    switch (function)
    {
        case RXFWRITE:
        case RXFWRITERR:
        case RXFTWRITE:
            if (g_systsprt_fp == NULL)
            {
                g_systsprt_fp = fopen("DD:SYSTSPRT", "w");
                if (g_systsprt_fp == NULL)
                {
                    return 20;
                }
            }
            if (data != NULL && data->pstr != NULL && data->len > 0)
            {
                fwrite(data->pstr, 1, (size_t)data->len, g_systsprt_fp);
            }
            fputc('\n', g_systsprt_fp);
            fflush(g_systsprt_fp);
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
