/* ------------------------------------------------------------------ */
/*  irxenvt.h — environment-variable switches for a C host            */
/*                                                                    */
/*  irx_env_toggles() applies REXX370_BYTECODE and REXX370_BCDEBUG to */
/*  an environment that IRXINIT has just built.  IRXINIT itself reads */
/*  no environment variables (#298): getenv() needs the caller's C    */
/*  runtime.  Call it only from a program that has one: IRXJCL and    */
/*  the C test hosts.  On MVS the variables come from DD:SYSENV       */
/*  (libc370 @@START), on the host from the process environment.      */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#ifndef IRXENVT_H
#define IRXENVT_H

#include "irx.h"

/*
 *   REXX370_BYTECODE  0/false/no/off: token-walk path
 *                     1/true/yes/on:  bytecode path (the default)
 *   REXX370_BCDEBUG   1/true/yes/on:  IRXJCL reports "[bc] exec=N
 *                     fallback=M" at the end of the run
 *
 * Case-insensitive; other values leave the setting unchanged.
 */
void irx_env_toggles(struct envblock *envblock) asm("IRXENVTG");

#endif /* IRXENVT_H */
