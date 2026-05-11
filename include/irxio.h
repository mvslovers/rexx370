/* ------------------------------------------------------------------ */
/*  irxio.h - IRXINOUT Default I/O Replaceable Routine                */
/*                                                                    */
/*  The default implementation of the I/O Replaceable Routine for    */
/*  REXX/370. Wired into IRXEXTE.io_routine and IRXEXTE.irxinout     */
/*  during IRXINIT; can be overridden by the caller via MODNAMET.    */
/*                                                                    */
/*  Platform variants (both in src/irx#io.c):                        */
/*    irxinout_mvs  — MVS (primary): writes to SYSTSPRT DD via        */
/*                    fopen("DD:SYSTSPRT","w")                        */
/*    irxinout_host — host/Linux: writes to stdout for test capture   */
/*                                                                    */
/*  IRXINIT step 6 selects the appropriate variant via #ifdef __MVS__.*/
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

#ifdef __MVS__

/* irxinout - MVS default I/O routine (primary)
 *
 * Writes to stdout + fflush. irx_jcl_dispatch_main redirects stdout
 * to DD:SYSTSPRT before exec invocation so SAY output appears in the
 * JES2 spool SYSTSPRT dataset.
 *
 * Parameters:
 *   function - I/O function code (RXFWRITE, RXFREAD, etc.)
 *   data     - For write functions: string to write.
 *              For read functions: output buffer for read line.
 *   envblock - The owning ENVBLOCK (NULL acceptable)
 *
 * Returns: 0=OK, 20=error
 */
int irxinout(int function, PLstr data, struct envblock *envblock);

#else /* !__MVS__ */

/* irxinout_host - host (Linux/gcc) I/O routine
 *
 * Writes to stdout. Used by cross-compile unit tests where
 * SAY/TRACE/error output is captured by the test harness.
 *
 * Parameters:
 *   function - I/O function code (RXFWRITE, RXFREAD, etc.)
 *   data     - For write functions: string to write.
 *              For read functions: output buffer for read line.
 *   envblock - The owning ENVBLOCK (NULL acceptable in test contexts)
 *
 * Returns: 0=OK, 20=error
 */
int irxinout_host(int function, PLstr data, struct envblock *envblock);

#endif /* __MVS__ */

#endif /* IRXIO_H */
