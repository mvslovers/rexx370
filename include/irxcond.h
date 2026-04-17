/* ------------------------------------------------------------------ */
/*  irxcond.h - REXX/370 Condition Information Block                  */
/*                                                                    */
/*  Tracks the last raised condition within a REXX environment.       */
/*  Allocated lazily on first error; freed by irxterm().              */
/*  Pointed to by irx_wkblk_int.wkbi_last_condition.                  */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 7 (SIGNAL ON / CALL ON), Appendix E    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXCOND_H
#define IRXCOND_H

struct envblock; /* forward decl to avoid circular include */

/* ================================================================== */
/*  SYNTAX error primary codes (SC28-1883-0 Appendix E)               */
/* ================================================================== */

#define SYNTAX_BAD_OPERAND  24 /* arithmetic operand not a number  */
#define SYNTAX_OVERFLOW     26 /* overflow / underflow             */
#define SYNTAX_EXP_OVERFLOW 41 /* result exponent too large        */
#define SYNTAX_DIVZERO      42 /* divide by zero                   */
#define SYNTAX_BAD_CALL     40 /* incorrect call to routine        */

/* ================================================================== */
/*  SYNTAX 40.x subcodes — incorrect call to routine                  */
/*  Raised by built-in functions when argument validation fails.      */
/* ================================================================== */

/* Subcodes listed in SC28-1883-0 Appendix E. Not every integer from  */
/* 1..29 is defined by the spec; we expose only the documented ones.  */
#define ERR40_TOO_FEW_ARGS    1  /* too few arguments                 */
#define ERR40_TOO_MANY_ARGS   2  /* too many arguments                */
#define ERR40_STRING_REQUIRED 3  /* argument N must be a string       */
#define ERR40_ARG_LENGTH      4  /* argument N length out of range    */
#define ERR40_WHOLE_NUMBER    5  /* argument N must be a whole number */
#define ERR40_NONNEG_WHOLE    11 /* argument N must be ≥ 0 whole num  */
#define ERR40_POSITIVE_WHOLE  12 /* argument N must be > 0 whole num  */
#define ERR40_SINGLE_CHAR     14 /* argument N must be single char    */
#define ERR40_NUMBER_REQUIRED 21 /* argument N must be a number       */
#define ERR40_OPTION_INVALID  23 /* argument N option not in allowed  */
#define ERR40_PAIRED_LENGTH   29 /* translate table lengths mismatch  */

/* ================================================================== */
/*  Condition Information block                                       */
/* ================================================================== */

#define IRX_COND_DESC_LEN 80

struct irx_condition_info
{
    int valid;                    /* non-zero if this block contains a raised condition */
    int code;                     /* primary REXX error number (e.g. 24)                */
    int subcode;                  /* secondary error code (0 if none)                   */
    char desc[IRX_COND_DESC_LEN]; /* human-readable description (null-terminated) */
};

/* ================================================================== */
/*  Public condition-raising helper                                   */
/*                                                                    */
/*  Populates wkbi_last_condition on the given environment. Lazily    */
/*  allocates the condition block on first use. Safe to call when     */
/*  env == NULL or the work block is missing (then it silently        */
/*  returns without error).                                           */
/* ================================================================== */

void irx_cond_raise(struct envblock *env, int code, int subcode,
                    const char *desc) asm("IRXCRAIS");

#endif /* IRXCOND_H */
