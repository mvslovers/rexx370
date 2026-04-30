/* ------------------------------------------------------------------ */
/*  irxwkblk.h - Internal Work Block Extension                        */
/*                                                                    */
/*  Per-environment interpreter runtime state for REXX/370.           */
/*  Pointed to by envblock_userfield in the IBM-compatible ENVBLOCK.  */
/*                                                                    */
/*  This block holds all mutable interpreter state that would be      */
/*  global variables in a non-reentrant interpreter. Every field      */
/*  here is per-environment, enabling multiple concurrent REXX        */
/*  executions in the same address space.                             */
/*                                                                    */
/*  Ref: Architecture Design v0.1.0, Section 3.7                     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXWKBLK_H
#define IRXWKBLK_H

#include "irx.h"
#include "irxcond.h"

/* ================================================================== */
/*  NUMERIC FORM values                                               */
/* ================================================================== */

#define NUMFORM_SCIENTIFIC  0
#define NUMFORM_ENGINEERING 1

/* ================================================================== */
/*  TRACE setting values                                              */
/* ================================================================== */

#define TRACE_OFF           'O'
#define TRACE_NORMAL        'N'
#define TRACE_ALL           'A'
#define TRACE_COMMANDS      'C'
#define TRACE_ERROR         'E'
#define TRACE_FAILURE       'F'
#define TRACE_INTERMEDIATES 'I'
#define TRACE_LABELS        'L'
#define TRACE_RESULTS       'R'
#define TRACE_SCAN          'S'

/* ================================================================== */
/*  I/O Routine Function Codes (for Replaceable I/O Routine)          */
/*  Ref: SC28-1883-0, Chapter 16, Page 366                           */
/* ================================================================== */

#define RXFWRITE    0 /* Write a line (SAY)                      */
#define RXFREAD     1 /* Read a line (PULL from terminal)        */
#define RXFREADP    2 /* Read (PULL from stack, then terminal)   */
#define RXFTWRITE   3 /* Write trace output                      */
#define RXFWRITERR  4 /* Write error message                     */
#define RXFOPEN     5 /* Open a dataset                          */
#define RXFCLOSE    6 /* Close a dataset                         */
#define RXFREAD_DS  7 /* Read from dataset (EXECIO)              */
#define RXFWRITE_DS 8 /* Write to dataset (EXECIO)               */

/* ================================================================== */
/*  Storage Management Function Codes                                 */
/*  Ref: SC28-1883-0, Chapter 16                                     */
/* ================================================================== */

#define RXSMGET 0 /* Acquire storage (GETMAIN equivalent)    */
#define RXSMFRE 1 /* Release storage (FREEMAIN equivalent)   */

/* ================================================================== */
/*  Signal Condition Flags                                            */
/*  Ref: SC28-1883-0, Chapter 7                                      */
/* ================================================================== */

#define COND_ERROR    0x01
#define COND_HALT     0x02
#define COND_NOVALUE  0x04
#define COND_NOTREADY 0x08
#define COND_SYNTAX   0x10
#define COND_FAILURE  0x20

/* ================================================================== */
/*  Internal Work Block Extension (irx_wkblk_int)                     */
/*  Per-environment interpreter runtime state                         */
/* ================================================================== */

struct irx_wkblk_int
{
    unsigned char wkbi_id[4];       /* Eye-catcher: 'WKBI'            */
    int wkbi_length;                /* Length of this block            */
    struct envblock *wkbi_envblock; /* -> owning ENVBLOCK             */

    /* --- NUMERIC settings --- */
    int wkbi_digits; /* NUMERIC DIGITS (default 9)     */
    int wkbi_fuzz;   /* NUMERIC FUZZ (default 0)       */
    int wkbi_form;   /* NUMERIC FORM (0=SCI, 1=ENG)   */

    /* --- TRACE settings --- */
    int wkbi_trace;       /* Current TRACE setting          */
    int wkbi_interactive; /* Interactive trace active (0/1) */

    /* --- Special variables --- */
    int wkbi_sigl; /* Current SIGL value             */
    int wkbi_rc;   /* Current RC value               */

    /* --- Variable Pool --- */
    void *wkbi_varpool; /* -> variable pool root          */

    /* --- Data Stack --- */
    void *wkbi_datastack; /* -> data stack anchor           */

    /* --- Execution Stack --- */
    void *wkbi_execstack; /* -> internal exec stack         */

    /* --- Condition Information --- */
    int wkbi_condflags;  /* Active SIGNAL ON conditions    */
    void *wkbi_condinfo; /* -> condition information block */

    /* --- Time BIF stamps (WP-CPS-01) --------------------------------- */
    /* Zero-initialized by irxstor(RXSMGET); zero sec == uninitialised.
     * Stored as (sec, usec) pairs to avoid 64-bit division on MVS.    */
    unsigned int wkbi_init_stamp_sec;   /* epoch sec at first TIME() call */
    unsigned int wkbi_init_stamp_usec;  /* usec part at first TIME() call */
    unsigned int wkbi_reset_stamp_sec;  /* TIME('R') reference stamp, sec */
    unsigned int wkbi_reset_stamp_usec; /* TIME('R') reference stamp, usec */

    /* --- Error Recovery --- */
    int wkbi_error_number;   /* Last REXX error number         */
    void *wkbi_error_source; /* -> source line causing error   */
    int wkbi_error_line;     /* Line number of error           */

    /* --- Source Management --- */
    void *wkbi_source;   /* -> loaded source image         */
    int wkbi_source_len; /* Length of source image         */
    void *wkbi_tokens;   /* -> token stream (after parse)  */

    /* --- Labels --- */
    void *wkbi_labels; /* -> label table                 */

    /* --- Exec Cache (per environment) --- */
    void *wkbi_cache; /* -> exec LRU cache              */

    /* --- Interpreter State --- */
    int wkbi_depth; /* Current call/procedure depth   */
    int wkbi_flags; /* Internal state flags           */

    /* --- lstring370 allocator bridge (WP-11b) ----------------------- */
    /* Opaque pointer to a `struct lstr_alloc` allocated via irxstor.
     * Set lazily by irx_lstr_init(); freed by irxterm(). Consumers
     * that need the concrete type must include <lstring.h>.          */
    void *wkbi_lstr_alloc;

    /* --- Condition tracking (WP-20) --- */
    struct irx_condition_info *wkbi_last_condition; /* last raised condition */

    /* --- BIF registry (WP-21a) ------------------------------------- */
    /* Opaque pointer to struct irx_bif_registry, allocated by irxinit
     * and released by irxterm. Populated with all core built-ins at
     * environment creation. See <irxbif.h>.                          */
    void *wkbi_bif_registry;

    /* --- RANDOM seed (WP-21b Phase C) ------------------------------ */
    /* Per-environment 32-bit LCG state used by the RANDOM() BIF.
     * Zero-initialized on env creation; RANDOM(,,seed) sets it      */
    /* explicitly. Consumed the last reserved word-slot documented    */
    /* alongside wkbi_bif_registry above; add a new _reserved[] array */
    /* when a future WP needs another word.                           */
    unsigned int wkbi_random_seed;

    /* --- ADDRESS state (WP-CPS-03) --------------------------------- */
    /* 8-byte space-padded host command environment name, consistent
     * with SUBCOMTB and PARMBLOCK conventions.  Seeded at env
     * creation from pb->tsofl ("TSO     " or "MVS     "); write path
     * follows in WP-CPS-05 (ADDRESS keyword).                        */
    char wkbi_address[8];
};

#define WKBLK_INT_ID "WKBI"

/* wkbi_flags values */
#define WKBI_RUNNING   0x80000000 /* Exec is currently executing       */
#define WKBI_HALTED    0x40000000 /* HALT condition is pending         */
#define WKBI_HT_ACTIVE 0x20000000 /* HT (halt typing) is active       */

/* Default NUMERIC settings */
#define NUMERIC_DIGITS_DEFAULT 9
#define NUMERIC_FUZZ_DEFAULT   0
#define NUMERIC_DIGITS_MAX     1000 /* SC28-1883-0 §9 max; WP-20        */

#endif /* IRXWKBLK_H */
