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

/* irxinout_tso - TSO I/O routine (WP-33-TSO)
 *
 * Writes through PUTLINE (IKJPUTL). In the TSO foreground that reaches
 * the terminal; in the background the TMP has STACKed SYSTSIN/SYSTSPRT
 * and PUTLINE writes to SYSTSPRT. TPUT is not used: on MVS 3.8j SVC 93
 * returns without doing anything in an address space with no TSB,
 * which is the batch TMP (mvs38src IKT0009C).
 *
 * IRXINIT LOADs it by name when the environment's MODNAMET names
 * IRXIOTSO, which IRXTSPRM does; IRXTERM DELETEs it.
 *
 * Available on both platforms: on the host src/irx#putl.c captures
 * each line instead of calling PUTLINE, so the routine can be tested
 * without an MVS deploy.
 *
 * Reads return IRXIO_TSO_UNSUPPORTED until WP-33b implements PULL.
 *
 * Returns: 0=OK, otherwise the worst PUTLINE return code, or one of
 * the IRXIO_TSO_* codes below.
 */
/* The MVS external name is set explicitly: names are truncated to 8
 * characters there, and "irxinout_tso" would collide with "irxinout" --
 * both become IRXINOUT, and the linker would quietly resolve the TSO
 * routine to the batch one. The alias is what stops it. */
#ifdef __MVS__
int irxinout_tso(int function, PLstr data,
                 struct envblock *envblock) asm("IRXIOTSO");
#else
int irxinout_tso(int function, PLstr data, struct envblock *envblock);
#endif

enum
{
    /* Longest piece one PUTLINE call carries. A longer SAY goes out
     * as several lines; the work area for one call stays a fixed size
     * on the stack. */
    TSO_LINE_MAX = 256,

    /* Header of a PUTLINE data line: LL (halfword, counts itself) and
     * a halfword offset, then the text. */
    TSO_LINE_HDR = 4,

    /* No TMP in this address space: no LWA, ECT or UPT to build an
     * IOPL from. */
    IRXIO_TSO_NO_TMP = 16,

    /* Function code this routine does not implement (yet). */
    IRXIO_TSO_UNSUPPORTED = 20
};

#endif /* IRXIO_H */
