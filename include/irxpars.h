/* ------------------------------------------------------------------ */
/*  irxpars.h - REXX/370 Parser + Expression Evaluator (IRXPARS)      */
/*                                                                    */
/*  Direct-interpretation parser for the REXX token stream produced   */
/*  by WP-10 (irx_tokn_run). Classifies clauses, performs             */
/*  assignments, evaluates expressions with full REXX operator        */
/*  precedence, resolves simple and compound variables, and           */
/*  dispatches built-in function calls.                               */
/*                                                                    */
/*  All parser state lives in struct irx_parser on the caller's       */
/*  stack. No globals, no statics - multiple parsers may coexist in   */
/*  the same address space. Keyword and BIF tables are static const  */
/*  data with no mutable state.                                       */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 2 (Structure/Syntax), Chapter 6         */
/*       (Numbers), Chapter 7 (Expressions)                           */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef __IRXPARS_H__
#define __IRXPARS_H__

#include "irx.h"
#include "irxtokn.h"
#include "irxvpool.h"
#include "lstring.h"
#include "lstralloc.h"

/* ================================================================== */
/*  Return codes                                                      */
/* ================================================================== */

#define IRXPARS_OK          0
#define IRXPARS_SYNTAX     20  /* generic SYNTAX error               */
#define IRXPARS_NOMEM      21  /* allocator failure                  */
#define IRXPARS_NOVALUE    22  /* uninitialised variable (NOVALUE)   */
#define IRXPARS_DIVZERO    23  /* division by zero                   */
#define IRXPARS_BADFUNC    24  /* unknown function in call           */
#define IRXPARS_BADARG     25  /* bad argument to parser entry       */

/* ================================================================== */
/*  Parser context                                                    */
/* ================================================================== */

struct irx_parser {
    struct irx_token    *tokens;       /* token stream                */
    int                  tok_count;    /* number of tokens            */
    int                  tok_pos;      /* cursor                      */

    struct irx_vpool    *vpool;        /* variable scope              */
    struct lstr_alloc   *alloc;        /* allocator for Lstr          */
    struct envblock     *envblock;     /* owning environment          */

    Lstr                 result;       /* last expression result      */

    int                  error_code;   /* IRXPARS_*                   */
    int                  error_line;   /* source line of the error    */

    void                *label_table;  /* label table (WP-15)         */
    void                *exec_stack;   /* execution stack (WP-15)     */

    int                  exit_requested; /* set by EXIT / RETURN      */
    int                  exit_rc;        /* RC passed to EXIT          */
};

/* ================================================================== */
/*  Keyword dispatch table                                            */
/*                                                                    */
/*  Each WP that adds a new keyword instruction registers a handler   */
/*  in irx_keyword_table[]. The parser core walks the table with a   */
/*  case-insensitive comparison after the clause has been classified  */
/*  as "keyword instruction" (rule 3 in clause classification).       */
/* ================================================================== */

typedef int (*irx_keyword_fn)(struct irx_parser *p);

struct irx_keyword {
    const char     *kw_name;   /* upper-case ASCII name              */
    irx_keyword_fn  kw_handler;
};

/* ================================================================== */
/*  Built-in function table                                           */
/* ================================================================== */

typedef int (*irx_bif_fn)(struct irx_parser *p,
                          int argc, PLstr *argv, PLstr result);

struct irx_bif {
    const char *bif_name;      /* upper-case ASCII name              */
    int         bif_min_args;
    int         bif_max_args;
    irx_bif_fn  bif_handler;
};

/* ================================================================== */
/*  Public entry points                                               */
/* ================================================================== */

/* Initialise a parser context. Does NOT take ownership of tokens or
 * vpool; the caller is responsible for their lifetime. The result
 * Lstr inside the context is zero-initialised and grown on demand. */
int  irx_pars_init(struct irx_parser *p,
                   struct irx_token *tokens, int tok_count,
                   struct irx_vpool *vpool,
                   struct lstr_alloc *alloc,
                   struct envblock *envblock);

/* Release allocator-backed memory owned by the context (the result
 * Lstr). Safe to call on a zero-initialised context. */
void irx_pars_cleanup(struct irx_parser *p);

/* Top-level clause loop. Processes every clause in the token stream
 * until TOK_EOF or an error. Returns IRXPARS_OK or an IRXPARS_* code. */
int  irx_pars_run(struct irx_parser *p);

/* Evaluate the expression starting at the current token position
 * into out. Stops at a clause terminator or a right parenthesis at
 * depth 0. Used both internally and by higher-level instructions
 * (SAY, IF, WHILE, etc.) in later work packages. */
int  irx_pars_eval_expr(struct irx_parser *p, PLstr out);

#endif /* __IRXPARS_H__ */
