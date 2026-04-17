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

struct envblock;
struct irx_bif_registry;

/* Register every built-in that ships with the core interpreter.
 * Returns IRX_BIF_OK on success or the first IRX_BIF_* error code. */
int irx_bif_register_all(struct envblock *env,
                         struct irx_bif_registry *reg) asm("IRXBIFAL");

#endif /* IRXBIFS_H */
