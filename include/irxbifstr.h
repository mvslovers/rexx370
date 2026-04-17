/* ------------------------------------------------------------------ */
/*  irxbifstr.h - REXX/370 String Built-in Functions                 */
/*                                                                    */
/*  Register the SC28-1883-0 §4 string BIFs into the per-environment */
/*  BIF registry. Called once at the end of irxinit().                */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXBIFSTR_H
#define IRXBIFSTR_H

struct envblock;
struct irx_bif_registry;

/* Register every built-in defined in src/irx#bifs.c.
 * Returns IRX_BIF_OK on success or the first IRX_BIF_* error code. */
int irx_bifstr_register(struct envblock *env,
                        struct irx_bif_registry *reg) asm("IRXBSREG");

#endif /* IRXBIFSTR_H */
