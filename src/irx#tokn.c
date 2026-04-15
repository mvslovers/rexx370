/* ------------------------------------------------------------------ */
/*  irxtokn.c - REXX/370 Tokenizer                                    */
/*                                                                    */
/*  Single-pass, reentrant tokenizer. All state lives in a context    */
/*  struct allocated on the caller's stack (see struct tok_ctx).      */
/*                                                                    */
/*  The tokenizer is a pure source-to-tokens transformer. It does     */
/*  not touch the internal Work Block; the caller (IRXEXEC) is        */
/*  responsible for installing the resulting token stream into        */
/*  wkbi_tokens when it is ready to execute.                          */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 2 (Structure and Syntax)                */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxtokn.h"
#include "irxwkblk.h"

/* ------------------------------------------------------------------ */
/*  Tokenizer context - private, stack-allocated per call             */
/* ------------------------------------------------------------------ */

struct tok_ctx
{
    const char *src; /* source buffer                */
    int src_len;     /* total length in bytes        */
    int pos;         /* byte offset into src         */
    int line;        /* 1-based, current position    */
    int col;         /* 1-based, current position    */

    struct irx_token *tokens; /* grown array                  */
    int tok_count;            /* live entries                 */
    int tok_capacity;         /* allocated slots              */

    struct envblock *envblock; /* for irxstor                  */

    int err_code; /* TOKERR_*                     */
    int err_line;
    int err_col;
};

/* Initial/grown token array capacity. 64 handles small execs in one  */
/* allocation; grows by doubling thereafter.                          */
#define TOK_INITIAL_CAPACITY 64

/* ------------------------------------------------------------------ */
/*  Character classification                                          */
/*                                                                    */
/*  We use <ctype.h> which crent370 provides in EBCDIC-correct form   */
/*  on MVS and the platform libc provides for cross-compile.          */
/*  REXX-specific classes are built on top of the standard macros.    */
/* ------------------------------------------------------------------ */

/* Characters allowed as the first character of a symbol (letter or
 * one of the REXX special starters). */
static int is_symbol_start(int c)
{
    if (isalpha(c))
    {
        return 1;
    }
    switch (c)
    {
        case '_':
        case '@':
        case '#':
        case '$':
        case '?':
        case '!':
            return 1;
        default:
            return 0;
    }
}

/* Characters allowed within a symbol after the first char. Dot is
 * included; dots make a symbol compound. */
static int is_symbol_char(int c)
{
    if (isalnum(c))
    {
        return 1;
    }
    switch (c)
    {
        case '_':
        case '@':
        case '#':
        case '$':
        case '?':
        case '!':
        case '.':
            return 1;
        default:
            return 0;
    }
}

/* The REXX "NOT" sign has two representations: backslash, and the
 * logical-not sign. The logical-not is 0x5F in EBCDIC (CP037/CP1047)
 * per SC28-1883-0, and is not required in cross-compile mode. */
static int is_not_sign(int c)
{
    if (c == '\\')
    {
        return 1;
    }
#ifdef __MVS__
    if (c == 0x5F)
    {
        return 1; /* EBCDIC logical-not */
    }
#endif
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Position advance helper                                           */
/* ------------------------------------------------------------------ */

static void advance(struct tok_ctx *ctx, int n)
{
    int i;
    for (i = 0; i < n && ctx->pos < ctx->src_len; i++)
    {
        if (ctx->src[ctx->pos] == '\n')
        {
            ctx->line++;
            ctx->col = 1;
        }
        else
        {
            ctx->col++;
        }
        ctx->pos++;
    }
}

static int peek(struct tok_ctx *ctx, int off)
{
    int p = ctx->pos + off;
    if (p < 0 || p >= ctx->src_len)
    {
        return -1;
    }
    return (unsigned char)ctx->src[p];
}

/* ------------------------------------------------------------------ */
/*  Token array growth                                                */
/* ------------------------------------------------------------------ */

static int grow_tokens(struct tok_ctx *ctx)
{
    int new_cap = ctx->tok_capacity ? ctx->tok_capacity * 2
                                    : TOK_INITIAL_CAPACITY;
    void *new_ptr = NULL;
    int rc = irxstor(RXSMGET,
                     (int)(new_cap * sizeof(struct irx_token)),
                     &new_ptr, ctx->envblock);
    if (rc != 0)
    {
        ctx->err_code = TOKERR_STORAGE;
        return 20;
    }
    if (ctx->tokens != NULL)
    {
        memcpy(new_ptr, ctx->tokens,
               ctx->tok_count * sizeof(struct irx_token));
        void *p = ctx->tokens;
        irxstor(RXSMFRE,
                (int)(ctx->tok_capacity * sizeof(struct irx_token)),
                &p, ctx->envblock);
    }
    ctx->tokens = (struct irx_token *)new_ptr;
    ctx->tok_capacity = new_cap;
    return 0;
}

/* Append a token record. Stores text pointer and length, classifies
 * by type/flags. Returns 0 on success, non-zero on storage error. */
static int emit(struct tok_ctx *ctx,
                unsigned char type, unsigned char flags,
                const char *text, int length,
                int start_line, int start_col)
{
    struct irx_token *t;

    if (ctx->tok_count >= ctx->tok_capacity)
    {
        if (grow_tokens(ctx) != 0)
        {
            return 20;
        }
    }
    t = &ctx->tokens[ctx->tok_count++];
    t->tok_type = type;
    t->tok_flags = flags;
    t->tok_col = (short)start_col;
    t->tok_line = start_line;
    t->tok_text = text;
    t->tok_length = (unsigned short)length;
    t->tok_reserved = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Comment handling                                                  */
/*                                                                    */
/*  REXX block comments use slash-star and star-slash and may nest.   */
/*  Line numbers inside a comment are still counted so SOURCELINE /   */
/*  PARSE SOURCE / TRACE / error messages report the correct line.    */
/* ------------------------------------------------------------------ */

static int skip_comment(struct tok_ctx *ctx)
{
    int depth = 1;
    /* We are positioned just past the opening '/' '*'. */
    advance(ctx, 2);
    while (ctx->pos < ctx->src_len)
    {
        int c0 = peek(ctx, 0);
        int c1 = peek(ctx, 1);
        if (c0 == '/' && c1 == '*')
        {
            depth++;
            advance(ctx, 2);
        }
        else if (c0 == '*' && c1 == '/')
        {
            depth--;
            advance(ctx, 2);
            if (depth == 0)
            {
                return 0;
            }
        }
        else
        {
            advance(ctx, 1);
        }
    }
    ctx->err_code = TOKERR_UNTERMINATED_CMT;
    ctx->err_line = ctx->line;
    ctx->err_col = ctx->col;
    return 20;
}

/* Skip whitespace (excluding newline) and comments. Does not consume
 * newlines - the caller decides whether a newline ends a clause. */
static int skip_inline_ws(struct tok_ctx *ctx)
{
    while (ctx->pos < ctx->src_len)
    {
        int c0 = peek(ctx, 0);
        int c1 = peek(ctx, 1);
        if (c0 == ' ' || c0 == '\t' || c0 == '\r')
        {
            advance(ctx, 1);
        }
        else if (c0 == '/' && c1 == '*')
        {
            if (skip_comment(ctx) != 0)
            {
                return 20;
            }
        }
        else
        {
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Clause terminator emission with continuation support              */
/*                                                                    */
/*  Per SC28-1883-0: a trailing comma before a line end is a          */
/*  continuation; the comma is dropped and the newline is not a       */
/*  clause terminator. We detect this after-the-fact: if the last     */
/*  emitted token is TOK_COMMA and the current newline follows only   */
/*  whitespace and comments, retract the comma and skip the EOC.      */
/* ------------------------------------------------------------------ */

static int emit_eoc(struct tok_ctx *ctx, int at_line, int at_col)
{
    /* Suppress EOC at the very start of the stream (leading blank
     * lines / leading comments produce no clause). */
    if (ctx->tok_count == 0)
    {
        return 0;
    }
    if (ctx->tokens[ctx->tok_count - 1].tok_type == TOK_COMMA)
    {
        /* Continuation: keep the comma (it is also the argument
         * separator in CALL / function-call contexts) and suppress
         * the EOC only. Per SC28-1883-0 Chapter 2: "A clause is
         * ended by the end of a line that is not immediately
         * preceded by a comma." */
        return 0;
    }
    /* Collapse multiple consecutive EOCs. */
    if (ctx->tokens[ctx->tok_count - 1].tok_type == TOK_EOC)
    {
        return 0;
    }
    return emit(ctx, TOK_EOC, TOKF_NONE, NULL, 0, at_line, at_col);
}

/* ------------------------------------------------------------------ */
/*  Scanner: string literal (and hex/bin suffixes)                    */
/* ------------------------------------------------------------------ */

static int scan_string(struct tok_ctx *ctx)
{
    int start_line = ctx->line;
    int start_col = ctx->col;
    int text_start;
    int quote = peek(ctx, 0);
    int doubled = 0;
    int suffix;
    unsigned char type = TOK_STRING;
    unsigned char flags = TOKF_NONE;

    advance(ctx, 1); /* consume opening quote */
    text_start = ctx->pos;

    while (ctx->pos < ctx->src_len)
    {
        int c = peek(ctx, 0);
        if (c == quote)
        {
            if (peek(ctx, 1) == quote)
            {
                /* Doubled quote - literal. */
                doubled = 1;
                advance(ctx, 2);
                continue;
            }
            /* End of string. */
            break;
        }
        /* REXX strings do not span lines; an embedded newline is an
         * unterminated-string error. */
        if (c == '\n')
        {
            ctx->err_code = TOKERR_UNTERMINATED_STR;
            ctx->err_line = start_line;
            ctx->err_col = start_col;
            return 20;
        }
        advance(ctx, 1);
    }

    if (ctx->pos >= ctx->src_len)
    {
        ctx->err_code = TOKERR_UNTERMINATED_STR;
        ctx->err_line = start_line;
        ctx->err_col = start_col;
        return 20;
    }

    /* ctx->pos is at the closing quote. */
    int body_len = ctx->pos - text_start;
    const char *body = ctx->src + text_start;

    advance(ctx, 1); /* consume closing quote */

    /* Check for x/X or b/B suffix indicating hex/bin string. */
    suffix = peek(ctx, 0);
    if (suffix == 'x' || suffix == 'X')
    {
        int hex_digits = 0;
        int i;
        for (i = 0; i < body_len; i++)
        {
            int bc = (unsigned char)body[i];
            if (bc == ' ' || bc == '\t')
            {
                continue;
            }
            if (!isxdigit(bc))
            {
                ctx->err_code = TOKERR_INVALID_HEX;
                ctx->err_line = start_line;
                ctx->err_col = start_col;
                return 20;
            }
            hex_digits++;
        }
        if ((hex_digits & 1) != 0)
        {
            ctx->err_code = TOKERR_ODD_HEX_GROUP;
            ctx->err_line = start_line;
            ctx->err_col = start_col;
            return 20;
        }
        type = TOK_HEXSTRING;
        advance(ctx, 1);
    }
    else if (suffix == 'b' || suffix == 'B')
    {
        int bits = 0;
        int i;
        for (i = 0; i < body_len; i++)
        {
            int bc = (unsigned char)body[i];
            if (bc == ' ' || bc == '\t')
            {
                continue;
            }
            if (bc != '0' && bc != '1')
            {
                ctx->err_code = TOKERR_INVALID_BIN;
                ctx->err_line = start_line;
                ctx->err_col = start_col;
                return 20;
            }
            bits++;
        }
        if ((bits & 3) != 0)
        {
            ctx->err_code = TOKERR_BAD_BIN_GROUP;
            ctx->err_line = start_line;
            ctx->err_col = start_col;
            return 20;
        }
        type = TOK_BINSTRING;
        advance(ctx, 1);
    }

    if (doubled)
    {
        flags |= TOKF_QUOTE_DBL;
    }
    return emit(ctx, type, flags, body, body_len,
                start_line, start_col);
}

/* ------------------------------------------------------------------ */
/*  Scanner: symbol / number                                          */
/*                                                                    */
/*  A run of symbol characters starting with a letter/special becomes */
/*  a SYMBOL. A run starting with a digit or '.' is classified as     */
/*  NUMBER if it matches REXX number syntax, otherwise as a constant  */
/*  SYMBOL. Both are consumed by the same greedy loop; classification */
/*  is done afterwards on the captured run.                           */
/* ------------------------------------------------------------------ */

static int looks_like_number(const char *s, int len)
{
    int i = 0;
    int saw_digit = 0;
    int saw_dot = 0;

    if (len == 0)
    {
        return 0;
    }

    /* Optional leading '.' handled by the digit loop. */
    while (i < len)
    {
        int c = (unsigned char)s[i];
        if (isdigit(c))
        {
            saw_digit = 1;
            i++;
        }
        else if (c == '.' && !saw_dot)
        {
            saw_dot = 1;
            i++;
        }
        else
        {
            break;
        }
    }
    if (!saw_digit)
    {
        return 0;
    }
    if (i == len)
    {
        return 1;
    }

    /* Exponent: E[+-]digits */
    if (s[i] == 'E' || s[i] == 'e')
    {
        i++;
        if (i < len && (s[i] == '+' || s[i] == '-'))
        {
            i++;
        }
        if (i == len)
        {
            return 0;
        }
        while (i < len)
        {
            if (!isdigit((unsigned char)s[i]))
            {
                return 0;
            }
            i++;
        }
        return 1;
    }
    return 0;
}

static int scan_symbol_or_number(struct tok_ctx *ctx)
{
    int start_line = ctx->line;
    int start_col = ctx->col;
    int start_pos = ctx->pos;
    int saw_dot = 0;
    int first = peek(ctx, 0);
    unsigned char flags = TOKF_NONE;
    unsigned char type;
    const char *text;
    int length;

    while (ctx->pos < ctx->src_len)
    {
        int c = peek(ctx, 0);
        if (!is_symbol_char(c))
        {
            break;
        }
        if (c == '.')
        {
            saw_dot = 1;
        }
        advance(ctx, 1);
    }

    /* For digit/dot-started runs, extend with a signed exponent
     * (E+nn / E-nn). The unsigned exponent (E5) is already part of
     * the symbol_char run above. */
    if ((isdigit(first) || first == '.') &&
        ctx->pos > start_pos &&
        (peek(ctx, 0) == '+' || peek(ctx, 0) == '-'))
    {
        char prev = ctx->src[ctx->pos - 1];
        if (prev == 'E' || prev == 'e')
        {
            advance(ctx, 1);
            while (ctx->pos < ctx->src_len &&
                   isdigit(peek(ctx, 0)))
            {
                advance(ctx, 1);
            }
        }
    }

    length = ctx->pos - start_pos;
    text = ctx->src + start_pos;

    if (saw_dot)
    {
        flags |= TOKF_COMPOUND;
    }

    /* A constant symbol starts with a digit or a dot. */
    if (isdigit(first) || first == '.')
    {
        flags |= TOKF_CONSTANT;
        if (looks_like_number(text, length))
        {
            type = TOK_NUMBER;
        }
        else
        {
            type = TOK_SYMBOL;
        }
    }
    else
    {
        type = TOK_SYMBOL;
    }

    return emit(ctx, type, flags, text, length, start_line, start_col);
}

/* ------------------------------------------------------------------ */
/*  Scanner: operators, punctuation                                   */
/*                                                                    */
/*  Per design notes section 3, the tokenizer emits one token per     */
/*  operator character. Composite operators (||, **, //, ==, >=, <=,  */
/*  &&, \=, \==, >>, <<, >>=, <<=, \>>, \<<) are formed by the parser */
/*  from adjacent operator tokens. This handles the awkward edge      */
/*  case of comments or whitespace appearing inside what looks like   */
/*  a multi-character operator (a | comment | b is still concat).     */
/* ------------------------------------------------------------------ */

static int scan_operator(struct tok_ctx *ctx)
{
    int start_line = ctx->line;
    int start_col = ctx->col;
    int start_pos = ctx->pos;
    int c0 = peek(ctx, 0);
    unsigned char type;

    /* NOT character (backslash, plus EBCDIC logical-not on MVS). */
    if (is_not_sign(c0))
    {
        type = TOK_NOT;
    }
    else
    {
        switch (c0)
        {
            case '(':
                advance(ctx, 1);
                return emit(ctx, TOK_LPAREN, TOKF_NONE,
                            ctx->src + start_pos, 1, start_line, start_col);
            case ')':
                advance(ctx, 1);
                return emit(ctx, TOK_RPAREN, TOKF_NONE,
                            ctx->src + start_pos, 1, start_line, start_col);
            case ',':
                advance(ctx, 1);
                return emit(ctx, TOK_COMMA, TOKF_NONE,
                            ctx->src + start_pos, 1, start_line, start_col);
            case ';':
            case ':':
                /* Both terminate a clause. The colon after a label
                 * symbol is recognised by the parser from context. */
                advance(ctx, 1);
                return emit(ctx, TOK_SEMICOLON, TOKF_NONE,
                            ctx->src + start_pos, 1, start_line, start_col);

            case '+':
            case '-':
            case '*':
            case '/':
            case '%':
                type = TOK_OPERATOR;
                break;

            case '=':
            case '>':
            case '<':
                type = TOK_COMPARISON;
                break;

            case '&':
            case '|':
                type = TOK_LOGICAL;
                break;

            default:
                ctx->err_code = TOKERR_BAD_CHAR;
                ctx->err_line = start_line;
                ctx->err_col = start_col;
                return 20;
        }
    }

    advance(ctx, 1);
    return emit(ctx, type, TOKF_NONE,
                ctx->src + start_pos, 1, start_line, start_col);
}

/* ------------------------------------------------------------------ */
/*  Main loop                                                         */
/* ------------------------------------------------------------------ */

static int tokenize(struct tok_ctx *ctx)
{
    while (ctx->pos < ctx->src_len)
    {
        int c;

        if (skip_inline_ws(ctx) != 0)
        {
            return 20;
        }
        if (ctx->pos >= ctx->src_len)
        {
            break;
        }

        c = peek(ctx, 0);

        if (c == '\n')
        {
            int l = ctx->line;
            int k = ctx->col;
            advance(ctx, 1);
            if (emit_eoc(ctx, l, k) != 0)
            {
                return 20;
            }
            continue;
        }

        if (c == '\'' || c == '"')
        {
            if (scan_string(ctx) != 0)
            {
                return 20;
            }
            continue;
        }

        if (is_symbol_start(c) || isdigit(c) ||
            (c == '.' && isdigit(peek(ctx, 1))))
        {
            if (scan_symbol_or_number(ctx) != 0)
            {
                return 20;
            }
            continue;
        }

        if (scan_operator(ctx) != 0)
        {
            return 20;
        }
    }

    /* Ensure clause terminates. */
    if (ctx->tok_count > 0 &&
        ctx->tokens[ctx->tok_count - 1].tok_type != TOK_EOC)
    {
        if (emit(ctx, TOK_EOC, TOKF_NONE, NULL, 0,
                 ctx->line, ctx->col) != 0)
        {
            return 20;
        }
    }

    return emit(ctx, TOK_EOF, TOKF_NONE, NULL, 0,
                ctx->line, ctx->col);
}

/* ------------------------------------------------------------------ */
/*  Public entry points                                               */
/* ------------------------------------------------------------------ */

int irx_tokn_run(struct envblock *envblock,
                 const char *src, int src_len,
                 struct irx_token **out_tokens, int *out_count,
                 struct irx_tokn_error *out_error)
{
    struct tok_ctx ctx;
    int rc;

    if (out_tokens == NULL || out_count == NULL ||
        src == NULL || src_len < 0)
    {
        if (out_error != NULL)
        {
            out_error->error_code = TOKERR_BAD_CHAR;
            out_error->error_line = 0;
            out_error->error_col = 0;
        }
        return 20;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.src = src;
    ctx.src_len = src_len;
    ctx.line = 1;
    ctx.col = 1;
    ctx.envblock = envblock;

    rc = tokenize(&ctx);

    if (rc != 0)
    {
        if (ctx.tokens != NULL)
        {
            void *p = ctx.tokens;
            irxstor(RXSMFRE,
                    (int)(ctx.tok_capacity * sizeof(struct irx_token)),
                    &p, envblock);
        }
        if (out_error != NULL)
        {
            out_error->error_code = ctx.err_code;
            out_error->error_line = ctx.err_line;
            out_error->error_col = ctx.err_col;
        }
        *out_tokens = NULL;
        *out_count = 0;
        return rc;
    }

    if (out_error != NULL)
    {
        out_error->error_code = TOKERR_NONE;
        out_error->error_line = 0;
        out_error->error_col = 0;
    }
    *out_tokens = ctx.tokens;
    *out_count = ctx.tok_count;
    return 0;
}

void irx_tokn_free(struct envblock *envblock,
                   struct irx_token *tokens, int count)
{
    /* count is in records; the capacity-vs-count distinction is lost
     * at this point, so we free the count-sized allocation. That is
     * safe because irx_tokn_run shrinks the bookkeeping only, never
     * the allocation, and irxstor ignores the length argument when
     * backed by the crent370 heap (see irxstor implementation). */
    void *p;
    if (tokens == NULL)
    {
        return;
    }
    p = tokens;
    irxstor(RXSMFRE, (int)(count * sizeof(struct irx_token)),
            &p, envblock);
}
