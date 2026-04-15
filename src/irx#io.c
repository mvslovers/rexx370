/* ------------------------------------------------------------------ */
/*  irxio.c - IRXINOUT: Default I/O Replaceable Routine               */
/*                                                                    */
/*  Default implementation of the I/O replaceable routine. Handles   */
/*  output (RXFWRITE, RXFWRITERR, RXFTWRITE) by writing the string   */
/*  followed by a newline. Read operations are stubbed pending        */
/*  WP-33 (PULL / LINEIN).                                            */
/*                                                                    */
/*  On MVS (WP-33): RXFWRITE will write to the SYSPRINT DDNAME via   */
/*  QSAM PUT; RXFREAD / RXFREADP will read from the console or data  */
/*  stack. For now the non-__MVS__ path (printf) is used to drive    */
/*  host-side unit tests.                                             */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 16, Page 366                           */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>

#include "irx.h"
#include "irxwkblk.h"
#include "lstring.h"
#include "irxio.h"

int irxinout(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;     /* reserved for WP-33 dataset / stack I/O */

    switch (function) {

    case RXFWRITE:      /* SAY output                             */
    case RXFWRITERR:    /* Error message output                   */
    case RXFTWRITE:     /* Trace output                           */
        if (data != NULL && data->pstr != NULL && data->len > 0) {
            fwrite(data->pstr, 1, (size_t)data->len, stdout);
        }
        fputc('\n', stdout);
        return 0;

    case RXFREAD:       /* PULL from terminal (WP-33)             */
    case RXFREADP:      /* PULL from stack then terminal (WP-33)  */
        /* TODO WP-33: implement PULL / LINEIN */
        return 20;

    default:
        return 20;
    }
}
