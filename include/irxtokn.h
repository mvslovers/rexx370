/* ------------------------------------------------------------------ */
/*  irxtokn.h - REXX/370 Tokenizer (IRXTOKN)                          */
/*                                                                    */
/*  Converts REXX source text into a token stream. Single-pass,       */
/*  reentrant. All state is in a private context on the caller's      */
/*  stack; this header exposes only the run/free entry points and     */
/*  the resulting token record layout.                                 */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 2 (Structure and Syntax)                */
/*  Ref: Architecture Design v0.1.0, Section 7.2                      */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXTOKN_H
#define IRXTOKN_H

#include "irx.h"

/* ================================================================== */
/*  Token types                                                       */
/* ================================================================== */

#define TOK_SYMBOL      0x01  /* FRED, A.B.C, X12, 'stem.' prefix    */
#define TOK_STRING      0x02  /* 'hello' or "world"                  */
#define TOK_NUMBER      0x03  /* 42, 3.14, 1E5                       */
#define TOK_HEXSTRING   0x04  /* 'FF'x                               */
#define TOK_BINSTRING   0x05  /* '1010'b                             */
/* Operator characters are emitted one per token; the parser forms   */
/* composite operators (||, **, //, ==, >=, <=, &&, \=, etc.) from   */
/* adjacent operator tokens. See docs/tokenizer-notes.md section 3.  */
#define TOK_OPERATOR    0x06  /* + - * / %                           */
#define TOK_COMPARISON  0x07  /* = > <                               */
#define TOK_LOGICAL     0x08  /* & |                                 */
#define TOK_NOT         0x09  /* \ or EBCDIC NOT sign                */
#define TOK_CONCAT      0x0A  /* reserved (parser may synthesize)    */
#define TOK_LPAREN      0x0B  /* (                                   */
#define TOK_RPAREN      0x0C  /* )                                   */
#define TOK_COMMA       0x0D  /* ,                                   */
#define TOK_SEMICOLON   0x0E  /* ;                                   */
#define TOK_EOC         0x10  /* End of clause (logical newline)     */
#define TOK_EOF         0x11  /* End of source                       */

/* ================================================================== */
/*  Token flags                                                       */
/* ================================================================== */

#define TOKF_NONE        0x00
#define TOKF_QUOTE_DBL   0x01  /* STRING contained doubled quotes     */
#define TOKF_COMPOUND    0x02  /* SYMBOL is a compound (has a dot)    */
#define TOKF_CONSTANT    0x04  /* SYMBOL is a constant (starts digit  */
                               /* or '.')                             */

/* ================================================================== */
/*  Token record (contiguous array, cache-friendly)                   */
/* ================================================================== */

struct irx_token {
    unsigned char   tok_type;      /* TOK_*                          */
    unsigned char   tok_flags;     /* TOKF_*                         */
    short           tok_col;       /* 1-based column of first byte   */
    int             tok_line;      /* 1-based line of first byte     */
    const char     *tok_text;      /* pointer into source buffer     */
    unsigned short  tok_length;    /* length of token text in bytes  */
    unsigned short  tok_reserved;  /* pad / future use               */
};

/* ================================================================== */
/*  Error reporting                                                   */
/* ================================================================== */

#define TOKERR_NONE               0
#define TOKERR_STORAGE           20  /* irxstor failed                */
#define TOKERR_UNTERMINATED_STR  30  /* string literal not closed     */
#define TOKERR_UNTERMINATED_CMT  31  /* comment not closed            */
#define TOKERR_INVALID_HEX       32  /* bad hex digit in 'xx'x        */
#define TOKERR_INVALID_BIN       33  /* bad bit digit in 'xx'b        */
#define TOKERR_ODD_HEX_GROUP     34  /* hex group has odd count       */
#define TOKERR_BAD_BIN_GROUP     35  /* binary group size not 4/8     */
#define TOKERR_BAD_CHAR          36  /* unrecognized character        */

struct irx_tokn_error {
    int  error_code;     /* TOKERR_*                                  */
    int  error_line;     /* 1-based, where error was detected         */
    int  error_col;      /* 1-based                                   */
};

/* ================================================================== */
/*  Public entry points                                               */
/* ================================================================== */

/* Tokenize source text.
 *
 *   envblock  - owning ENVBLOCK (used to route irxstor calls).
 *               May be NULL during bootstrap / unit tests.
 *   src       - pointer to source text. Not copied; token records
 *               hold pointers into this buffer, so src must outlive
 *               the returned token array.
 *   src_len   - length of src in bytes.
 *   out_tokens- on success, receives a pointer to a newly allocated
 *               array of struct irx_token ending with TOK_EOF.
 *               Caller must release with irx_tokn_free.
 *   out_count - on success, receives the number of tokens including
 *               the trailing TOK_EOF entry.
 *   out_error - optional (may be NULL). On non-zero return, receives
 *               error code and source position.
 *
 *   Returns 0 on success, non-zero on error (see TOKERR_*).
 */
int  irx_tokn_run(struct envblock *envblock,
                  const char *src, int src_len,
                  struct irx_token **out_tokens, int *out_count,
                  struct irx_tokn_error *out_error) asm("IRXTOKNR");

/* Release a token array returned by irx_tokn_run.
 *
 *   envblock  - owning ENVBLOCK (same as passed to irx_tokn_run).
 *   tokens    - pointer returned in out_tokens.
 *   count     - value returned in out_count.
 */
void irx_tokn_free(struct envblock *envblock,
                   struct irx_token *tokens, int count) asm("IRXTOKNF");

#endif /* IRXTOKN_H */
