/* ------------------------------------------------------------------ */
/*  irxjcl.h — IRXJCL Batch Entry Point interface                     */
/*                                                                    */
/*  irx_jcl_dispatch() is the C core of IRXJCL, the module that MVS  */
/*  loads when a JCL step specifies EXEC PGM=IRXJCL.  The asm        */
/*  wrapper (asm/irxjcl.asm, entry IRXJCL) decodes the single-slot   */
/*  VLIST, saves the optional R0 ENVBLOCK hint, and delegates here.   */
/*                                                                    */
/*  z/OS-stage interface (WP-CPS-08 / TSK-220):                      */
/*    CALL IRXJCL,(PARMPTR),VL                                        */
/*    P1  A   PARM buffer: halfword(big-endian) length + data         */
/*    R0  opt ENVBLOCK pointer (eyecatcher-validated before use)      */
/*                                                                    */
/*  PARM layout:                                                      */
/*    +0  H   total data length (big-endian, may be 0 = no PARM)     */
/*    +2  CL* member-name [ space arg-string ]                        */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXJCL_H
#define IRXJCL_H

#include "irx.h"

/* asm() alias used by c2asm370 so the MVS symbol is IRXJDISP (≤8 chars). */
int irx_jcl_dispatch(void *parm_buffer,
                     struct envblock *envblock_r0) __asm__("IRXJDISP");

/* Return codes from irx_jcl_dispatch (→ R15 via asm wrapper). */
enum irxjcl_rc
{
    IRXJCL_OK = 0,       /* exec ran; check exit RC via EVALBLOCK     */
    IRXJCL_BADPARM = 24, /* PARM missing, zero-length, sequential-mode,
                          * or member name invalid (0 or > 8 chars)   */
    IRXJCL_NOENV = 28,   /* cannot locate/create a Language Env       */
    IRXJCL_ERROR = 20,   /* internal error (load failure, alloc, etc) */
};

#endif /* IRXJCL_H */
