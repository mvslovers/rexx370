/* ------------------------------------------------------------------ */
/*  irxio.h - IRXINOUT Default I/O Replaceable Routine                */
/*                                                                    */
/*  The default implementation of the I/O Replaceable Routine for    */
/*  REXX/370. Wired into IRXEXTE.io_routine and IRXEXTE.irxinout     */
/*  during IRXINIT; can be overridden by the caller via MODNAMET.    */
/*                                                                    */
/*  I/O function codes are defined in irxwkblk.h (RXFWRITE etc.).    */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 16 (Replaceable Routines)              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXIO_H
#define IRXIO_H

#include "irx.h"
#include "lstring.h"

/* IRXINOUT - Default I/O Replaceable Routine
 *
 * Called via IRXEXTE.io_routine for all I/O operations (SAY, PULL,
 * trace output, error messages, etc.).
 *
 * Parameters:
 *   function - I/O function code (RXFWRITE, RXFREAD, etc.)
 *              See irxwkblk.h for RXFWRITE, RXFREAD, RXFWRITERR, ...
 *   data     - For RXFWRITE/RXFWRITERR/RXFTWRITE: string to write.
 *              For RXFREAD/RXFREADP: output buffer for the read line.
 *   envblock - The owning ENVBLOCK (NULL acceptable in test contexts)
 *
 * Returns: 0=OK, 20=error
 */
int irxinout(int function, PLstr data, struct envblock *envblock);

#endif /* IRXIO_H */
