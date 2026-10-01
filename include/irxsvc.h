/* ------------------------------------------------------------------ */
/*  irxsvc.h — MVS system services without libc370 (#298)             */
/*                                                                    */
/*  LOAD and DELETE by inline assembler.  libc370's __load() reports  */
/*  with wtof()/wtodumpf() and searches with BLDL first; both pull    */
/*  code into every module that loads, and BLDL does not see modules  */
/*  that are only in the LPA.                                         */
/*                                                                    */
/*  name: NUL-terminated, 1-8 characters, already upper case (all     */
/*  callers pass literals or MODNAMET slots).  MVS only.              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#ifndef IRXSVC_H
#define IRXSVC_H

#ifdef __MVS__
/* LOAD EP=name: entry point address, or NULL if the module cannot be
 * loaded (no abend: ERRET). */
void *irx_svc_load(const char *name) asm("IRXSVLD");

/* DELETE EP=name: MVS return code (0 = deleted). */
int irx_svc_delete(const char *name) asm("IRXSVDL");
#endif

#endif /* IRXSVC_H */
