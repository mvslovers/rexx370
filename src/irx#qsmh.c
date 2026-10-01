/* ------------------------------------------------------------------ */
/*  irx#qsmh.c — host stand-in for asm/irxqsam.asm (#302)             */
/*                                                                    */
/*  [host] replace links this file instead of the assembler module.   */
/*  Nothing calls IRXQSAM on the host: irx#io.c's host path,          */
/*  irxinout_host, writes to stdout.                                  */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

typedef int irx_qsam_host_unused;
