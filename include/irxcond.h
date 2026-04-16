/* ------------------------------------------------------------------ */
/*  irxcond.h - REXX/370 Condition Information Block                  */
/*                                                                    */
/*  Tracks the last raised condition within a REXX environment.       */
/*  Allocated lazily by the arithmetic engine on first error; freed   */
/*  by irxterm().  Pointed to by irx_wkblk_int.wkbi_last_condition.  */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 7 (SIGNAL ON / CALL ON), Appendix E    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXCOND_H
#define IRXCOND_H

/* SYNTAX error subcodes (SC28-1883-0 Appendix E) */
#define SYNTAX_BAD_OPERAND    24 /* arithmetic operand not a number  */
#define SYNTAX_OVERFLOW       26 /* overflow / underflow             */
#define SYNTAX_DIVZERO        42 /* divide by zero                   */
#define SYNTAX_EXP_OVERFLOW   41 /* result exponent too large        */

struct irx_condition_info
{
    int  valid;      /* non-zero if this block contains a raised condition */
    int  code;       /* primary REXX error number (e.g. 24)                */
    int  subcode;    /* secondary error code (0 if none)                   */
    char desc[80];   /* human-readable description (null-terminated)       */
};

#endif /* IRXCOND_H */
