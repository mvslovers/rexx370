/* ------------------------------------------------------------------ */
/*  irxbifs.h - REXX/370 Built-in Function Registration                */
/*                                                                    */
/*  Single entry point that registers every built-in function into    */
/*  the per-environment BIF registry. Called once from irxinit()      */
/*  after the registry has been allocated.                            */
/*                                                                    */
/*  The handlers themselves live in src/irx#bifs.c (string BIFs) and  */
/*  src/irx#pars.c (parser-internal BIFs such as ARG).                */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXBIFS_H
#define IRXBIFS_H

#include "lstring.h"

struct envblock;
struct irx_bif_registry;
struct irx_parser;

/* Register every built-in that ships with the core interpreter.
 * Returns IRX_BIF_OK on success or the first IRX_BIF_* error code. */
int irx_bif_register_all(struct envblock *env,
                         struct irx_bif_registry *reg) asm("IRXBIFAL");

/* Parse a TRACE option string (shared by bif_trace and kw_trace).
 * Sets *letter_out (one of NAILRCFEO, upper-cased) and *toggle_out (0/1).
 * "?L" form: toggle=1, letter=L. Plain "L..." form: toggle=0, letter=L.
 * toggle is always written — plain "L" clears any prior toggle.
 * Returns 0 on success; -1 on error (SYNTAX 40.23 already raised).
 *
 * asm() alias required: cross-module entry point, name > 8 chars. */
int parse_trace_option(struct irx_parser *p, PLstr opt,
                       char *letter_out, int *toggle_out) asm("IRXPTOPT");

#endif /* IRXBIFS_H */
