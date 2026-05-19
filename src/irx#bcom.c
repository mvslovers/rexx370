/* ------------------------------------------------------------------ */
/*  irx#bcom.c - REXX/370 Bytecode Compiler (WP-BC-02)               */
/*                                                                    */
/*  irx_bc_compile() — Entry point.                                  */
/*                                                                    */
/*  Phase 2 scope:                                                    */
/*    - Assignment statements:  symbol = expr                        */
/*    - Expressions: literals, variables, arithmetic, comparison,    */
/*      logical, string concatenation, parenthesised groups          */
/*    - EXIT statement (no expression)                               */
/*    - Any other construct returns IRXBC_ERR_UNSUP                  */
/*                                                                    */
/*  Memory: caller must free the returned irx_bc_execblk with        */
/*    irxstor(RXSMFRE, 0, &p, envblock)                              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxbops.h"
#include "irxbvm.h"
#include "irxexbl.h"
#include "irxfunc.h"
#include "irxtokn.h"
#include "irxwkblk.h"

/* ================================================================== */
/*  Compiler limits                                                   */
/* ================================================================== */

#define BCOM_MAX_CODE   1024
#define BCOM_MAX_CONSTS 64
#define BCOM_MAX_SYMS   64

/* ================================================================== */
/*  Compiler context (heap-allocated to avoid large stack frames)     */
/* ================================================================== */

struct bcom_ctx
{
    struct envblock *env;
    const struct irx_token *tokens;
    int tok_count;
    int pos;

    unsigned char code[BCOM_MAX_CODE];
    int code_len;

    /* Each table entry: byte[0]=length, byte[1..63]=data */
    char consts[BCOM_MAX_CONSTS][IRXBC_ENTRY_SIZE];
    int const_count;

    char syms[BCOM_MAX_SYMS][IRXBC_ENTRY_SIZE];
    int sym_count;

    int rc;
    int hit_exit;
};

/* ================================================================== */
/*  Forward declarations                                              */
/* ================================================================== */

static void bc_exp0(struct bcom_ctx *ctx);

/* ================================================================== */
/*  Helpers                                                           */
/* ================================================================== */

static int emit_byte(struct bcom_ctx *ctx, unsigned char op)
{
    if (ctx->code_len >= BCOM_MAX_CODE)
    {
        ctx->rc = IRXBC_ERR_STOR;
        return IRXBC_ERR_STOR;
    }
    ctx->code[ctx->code_len++] = op;
    return IRXBC_OK;
}

static int emit_u16(struct bcom_ctx *ctx, int idx)
{
    if (ctx->code_len + 2 > BCOM_MAX_CODE)
    {
        ctx->rc = IRXBC_ERR_STOR;
        return IRXBC_ERR_STOR;
    }
    ctx->code[ctx->code_len++] = (unsigned char)(idx & 0xFF);
    ctx->code[ctx->code_len++] = (unsigned char)((idx >> 8) & 0xFF);
    return IRXBC_OK;
}

/* Return token at pos+offset, or NULL if out of range. */
static const struct irx_token *tok_at(const struct bcom_ctx *ctx, int offset)
{
    int i = ctx->pos + offset;
    if (i < 0 || i >= ctx->tok_count)
    {
        return NULL;
    }
    return &ctx->tokens[i];
}

/* Return 1 if token at pos+offset has the given type. */
static int tok_type_at(const struct bcom_ctx *ctx, int offset,
                       unsigned char type)
{
    const struct irx_token *t = tok_at(ctx, offset);
    return t != NULL && t->tok_type == type;
}

/* Return first character of tok_text for the token at pos+offset, or 0. */
static char tok_ch(const struct bcom_ctx *ctx, int offset)
{
    const struct irx_token *t = tok_at(ctx, offset);
    if (t == NULL || t->tok_length == 0 || t->tok_text == NULL)
    {
        return 0;
    }
    return t->tok_text[0];
}

/* Add a constant string (text, len) to the const table; return index.
 * Returns -1 on overflow. Duplicates are not merged (acceptable for now). */
static int add_const(struct bcom_ctx *ctx, const char *text, int len)
{
    int i;
    if (len > IRXBC_STR_MAX)
    {
        len = IRXBC_STR_MAX;
    }
    /* check for duplicate */
    for (i = 0; i < ctx->const_count; i++)
    {
        if ((unsigned char)ctx->consts[i][0] == (unsigned char)len &&
            memcmp(ctx->consts[i] + 1, text, (size_t)len) == 0)
        {
            return i;
        }
    }
    if (ctx->const_count >= BCOM_MAX_CONSTS)
    {
        return -1;
    }
    i = ctx->const_count++;
    ctx->consts[i][0] = (char)(unsigned char)len;
    if (len > 0)
    {
        memcpy(ctx->consts[i] + 1, text, (size_t)len);
    }
    return i;
}

/* Add a symbol name (upper-case, NUL-terminated) to the sym table.
 * Returns index.  Deduplicates by name. Returns -1 on overflow. */
static int add_sym(struct bcom_ctx *ctx, const char *name)
{
    int i;
    int len = (int)strlen(name);
    if (len > IRXBC_STR_MAX)
    {
        len = IRXBC_STR_MAX;
    }
    for (i = 0; i < ctx->sym_count; i++)
    {
        if ((unsigned char)ctx->syms[i][0] == (unsigned char)len &&
            memcmp(ctx->syms[i] + 1, name, (size_t)len) == 0)
        {
            return i;
        }
    }
    if (ctx->sym_count >= BCOM_MAX_SYMS)
    {
        return -1;
    }
    i = ctx->sym_count++;
    ctx->syms[i][0] = (char)(unsigned char)len;
    memcpy(ctx->syms[i] + 1, name, (size_t)len);
    return i;
}

/* ================================================================== */
/*  Expression cascade — bc_exp0 is the entry point (lowest prec.)   */
/*                                                                    */
/*  Precedence (lowest first):                                        */
/*   0 — | (OR), && (XOR)                                            */
/*   1 — & (AND)                                                      */
/*   2 — comparison operators                                         */
/*   3 — || (concat), abuttal-blank                                   */
/*   4 — + - (add/subtract)                                          */
/*   5 — * / % // (multiply/divide)                                  */
/*   6 — ** (power)                                                   */
/*   7 — unary + - \                                                  */
/*   8 — atoms: literals, variables, ( expr )                        */
/* ================================================================== */

/* Level 8 — atoms */
static void bc_exp8(struct bcom_ctx *ctx)
{
    const struct irx_token *t = tok_at(ctx, 0);

    if (t == NULL || ctx->rc != IRXBC_OK)
    {
        return;
    }

    /* Parenthesised expression */
    if (t->tok_type == TOK_LPAREN)
    {
        ctx->pos++;
        bc_exp0(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        t = tok_at(ctx, 0);
        if (t == NULL || t->tok_type != TOK_RPAREN)
        {
            ctx->rc = IRXBC_ERR_UNSUP;
            return;
        }
        ctx->pos++;
        return;
    }

    /* String literal */
    if (t->tok_type == TOK_STRING)
    {
        int ci = add_const(ctx, t->tok_text, (int)t->tok_length);
        if (ci < 0)
        {
            ctx->rc = IRXBC_ERR_STOR;
            return;
        }
        ctx->pos++;
        emit_byte(ctx, OP_PUSH_LIT);
        emit_u16(ctx, ci);
        return;
    }

    /* Number token */
    if (t->tok_type == TOK_NUMBER)
    {
        int ci = add_const(ctx, t->tok_text, (int)t->tok_length);
        if (ci < 0)
        {
            ctx->rc = IRXBC_ERR_STOR;
            return;
        }
        ctx->pos++;
        emit_byte(ctx, OP_PUSH_LIT);
        emit_u16(ctx, ci);
        return;
    }

    /* Symbol — constant symbol (starts with digit or '.') or variable */
    if (t->tok_type == TOK_SYMBOL)
    {
        if (t->tok_flags & TOKF_CONSTANT)
        {
            /* constant symbol: treat as literal */
            int ci = add_const(ctx, t->tok_text, (int)t->tok_length);
            if (ci < 0)
            {
                ctx->rc = IRXBC_ERR_STOR;
                return;
            }
            ctx->pos++;
            emit_byte(ctx, OP_PUSH_LIT);
            emit_u16(ctx, ci);
        }
        else
        {
            /* variable: load from vpool (NOVALUE = uppercase name) */
            const char *name = (t->tok_upper != NULL) ? t->tok_upper
                                                      : t->tok_text;
            int si = add_sym(ctx, name);
            if (si < 0)
            {
                ctx->rc = IRXBC_ERR_STOR;
                return;
            }
            ctx->pos++;
            emit_byte(ctx, OP_LOAD);
            emit_u16(ctx, si);
        }
        return;
    }

    ctx->rc = IRXBC_ERR_UNSUP;
}

/* Level 7 — unary prefix: + - \ */
static void bc_exp7(struct bcom_ctx *ctx)
{
    const struct irx_token *t;

    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    t = tok_at(ctx, 0);
    if (t == NULL)
    {
        return;
    }

    if (t->tok_type == TOK_OPERATOR && tok_ch(ctx, 0) == '-')
    {
        ctx->pos++;
        bc_exp7(ctx); /* right-associative unary */
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_NEG);
        return;
    }

    if (t->tok_type == TOK_OPERATOR && tok_ch(ctx, 0) == '+')
    {
        /* Unary + is a no-op in REXX (forces numeric context). */
        ctx->pos++;
        bc_exp7(ctx);
        return;
    }

    if (t->tok_type == TOK_NOT)
    {
        ctx->pos++;
        bc_exp7(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_NOT);
        return;
    }

    bc_exp8(ctx);
}

/* Level 6 — power: ** (right-associative) */
static void bc_exp6(struct bcom_ctx *ctx)
{
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    bc_exp7(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    if (tok_type_at(ctx, 0, TOK_OPERATOR) && tok_ch(ctx, 0) == '*' &&
        tok_type_at(ctx, 1, TOK_OPERATOR) && tok_ch(ctx, 1) == '*')
    {
        ctx->pos += 2;
        bc_exp6(ctx); /* right-associative */
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_POW);
    }
}

/* Level 5 — multiplicative: * / // % */
static void bc_exp5(struct bcom_ctx *ctx)
{
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    bc_exp6(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    for (;;)
    {
        if (tok_type_at(ctx, 0, TOK_OPERATOR) && tok_ch(ctx, 0) == '/' &&
            tok_type_at(ctx, 1, TOK_OPERATOR) && tok_ch(ctx, 1) == '/')
        {
            /* // remainder */
            ctx->pos += 2;
            bc_exp6(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_MOD);
        }
        else if (tok_type_at(ctx, 0, TOK_OPERATOR) && tok_ch(ctx, 0) == '*' &&
                 !(tok_type_at(ctx, 1, TOK_OPERATOR) && tok_ch(ctx, 1) == '*'))
        {
            ctx->pos++;
            bc_exp6(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_MUL);
        }
        else if (tok_type_at(ctx, 0, TOK_OPERATOR) && tok_ch(ctx, 0) == '/' &&
                 !(tok_type_at(ctx, 1, TOK_OPERATOR) && tok_ch(ctx, 1) == '/'))
        {
            ctx->pos++;
            bc_exp6(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_DIV);
        }
        else if (tok_type_at(ctx, 0, TOK_OPERATOR) && tok_ch(ctx, 0) == '%')
        {
            ctx->pos++;
            bc_exp6(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_IDIV);
        }
        else
        {
            break;
        }
    }
}

/* Level 4 — additive: + - */
static void bc_exp4(struct bcom_ctx *ctx)
{
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    bc_exp5(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    for (;;)
    {
        if (tok_type_at(ctx, 0, TOK_OPERATOR) && tok_ch(ctx, 0) == '+')
        {
            ctx->pos++;
            bc_exp5(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_ADD);
        }
        else if (tok_type_at(ctx, 0, TOK_OPERATOR) && tok_ch(ctx, 0) == '-')
        {
            ctx->pos++;
            bc_exp5(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_SUB);
        }
        else
        {
            break;
        }
    }
}

/* Level 3 — concatenation: || and abuttal-blank.
 *
 * Abuttal: two adjacent value-producing tokens (symbol, string,
 * number, or closing paren) without an operator between them are
 * concatenated with one blank (SC28-1883-0 §2.3.6).
 *
 * Detection: after parsing the left operand, if the next token is a
 * value-starter (symbol, string, number, '(') or a prefix unary
 * operator that can start an expression, we have abuttal.
 */
static int is_value_starter(const struct bcom_ctx *ctx, int offset)
{
    const struct irx_token *t = tok_at(ctx, offset);
    if (t == NULL)
    {
        return 0;
    }
    /* Only pure value-producing tokens can start an abuttal: symbols,
     * string literals, numbers, and parenthesised expressions.
     * Operator tokens (including unary - and \) are NOT included:
     * they would be mis-parsed as abuttal when the intent is a binary
     * operator (e.g. '3 \= 4' would be parsed as concat, not NE). */
    return t->tok_type == TOK_SYMBOL || t->tok_type == TOK_STRING ||
           t->tok_type == TOK_NUMBER || t->tok_type == TOK_LPAREN;
}

static void bc_exp3(struct bcom_ctx *ctx)
{
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    bc_exp4(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    for (;;)
    {
        /* || explicit concatenation */
        if (tok_type_at(ctx, 0, TOK_LOGICAL) && tok_ch(ctx, 0) == '|' &&
            tok_type_at(ctx, 1, TOK_LOGICAL) && tok_ch(ctx, 1) == '|')
        {
            ctx->pos += 2;
            bc_exp4(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_CONCAT);
        }
        /* abuttal-blank */
        else if (is_value_starter(ctx, 0))
        {
            bc_exp4(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_BCONCAT);
        }
        else
        {
            break;
        }
    }
}

/* Level 2 — comparison operators */
static void bc_exp2(struct bcom_ctx *ctx)
{
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    bc_exp3(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    /* Comparison is non-associative: parse at most one operator. */
    {
        unsigned char op = 0;

        /* 3-character strict: >>= <<= \== (handled as two below via nesting) */

        /* 2-character composites checked first (longer match wins) */
        if (tok_type_at(ctx, 0, TOK_COMPARISON) && tok_ch(ctx, 0) == '=' &&
            tok_type_at(ctx, 1, TOK_COMPARISON) && tok_ch(ctx, 1) == '=')
        {
            /* ==  but not \== (handled via \) */
            op = OP_DEQ;
            ctx->pos += 2;
        }
        else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
                 tok_ch(ctx, 0) == '>' &&
                 tok_type_at(ctx, 1, TOK_COMPARISON) &&
                 tok_ch(ctx, 1) == '>' &&
                 tok_type_at(ctx, 2, TOK_COMPARISON) &&
                 tok_ch(ctx, 2) == '=')
        {
            /* >>= */
            op = OP_DGE;
            ctx->pos += 3;
        }
        else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
                 tok_ch(ctx, 0) == '<' &&
                 tok_type_at(ctx, 1, TOK_COMPARISON) &&
                 tok_ch(ctx, 1) == '<' &&
                 tok_type_at(ctx, 2, TOK_COMPARISON) &&
                 tok_ch(ctx, 2) == '=')
        {
            /* <<= */
            op = OP_DLE;
            ctx->pos += 3;
        }
        else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
                 tok_ch(ctx, 0) == '>' &&
                 tok_type_at(ctx, 1, TOK_COMPARISON) &&
                 tok_ch(ctx, 1) == '>')
        {
            /* >> */
            op = OP_DGT;
            ctx->pos += 2;
        }
        else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
                 tok_ch(ctx, 0) == '<' &&
                 tok_type_at(ctx, 1, TOK_COMPARISON) &&
                 tok_ch(ctx, 1) == '<')
        {
            /* << */
            op = OP_DLT;
            ctx->pos += 2;
        }
        else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
                 tok_ch(ctx, 0) == '>' &&
                 tok_type_at(ctx, 1, TOK_COMPARISON) &&
                 tok_ch(ctx, 1) == '=')
        {
            /* >= */
            op = OP_GE;
            ctx->pos += 2;
        }
        else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
                 tok_ch(ctx, 0) == '<' &&
                 tok_type_at(ctx, 1, TOK_COMPARISON) &&
                 tok_ch(ctx, 1) == '=')
        {
            /* <= */
            op = OP_LE;
            ctx->pos += 2;
        }
        else if (tok_type_at(ctx, 0, TOK_NOT) &&
                 tok_type_at(ctx, 1, TOK_COMPARISON) &&
                 tok_ch(ctx, 1) == '=' &&
                 tok_type_at(ctx, 2, TOK_COMPARISON) &&
                 tok_ch(ctx, 2) == '=')
        {
            /* \== */
            op = OP_DNE;
            ctx->pos += 3;
        }
        else if (tok_type_at(ctx, 0, TOK_NOT) &&
                 tok_type_at(ctx, 1, TOK_COMPARISON) &&
                 tok_ch(ctx, 1) == '=')
        {
            /* \= */
            op = OP_NE;
            ctx->pos += 2;
        }
        else if (tok_type_at(ctx, 0, TOK_NOT) &&
                 tok_type_at(ctx, 1, TOK_COMPARISON) &&
                 tok_ch(ctx, 1) == '>')
        {
            /* \> same as <= */
            op = OP_LE;
            ctx->pos += 2;
        }
        else if (tok_type_at(ctx, 0, TOK_NOT) &&
                 tok_type_at(ctx, 1, TOK_COMPARISON) &&
                 tok_ch(ctx, 1) == '<')
        {
            /* \< same as >= */
            op = OP_GE;
            ctx->pos += 2;
        }
        else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
                 tok_ch(ctx, 0) == '=')
        {
            /* = */
            op = OP_EQ;
            ctx->pos++;
        }
        else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
                 tok_ch(ctx, 0) == '>')
        {
            /* > */
            op = OP_GT;
            ctx->pos++;
        }
        else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
                 tok_ch(ctx, 0) == '<')
        {
            /* < */
            op = OP_LT;
            ctx->pos++;
        }

        if (op != 0)
        {
            bc_exp3(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, op);
        }
    }
}

/* Level 1 — AND: & */
static void bc_exp1(struct bcom_ctx *ctx)
{
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    bc_exp2(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    while (tok_type_at(ctx, 0, TOK_LOGICAL) && tok_ch(ctx, 0) == '&' &&
           !(tok_type_at(ctx, 1, TOK_LOGICAL) && tok_ch(ctx, 1) == '&'))
    {
        ctx->pos++;
        bc_exp2(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_AND);
    }
}

/* Level 0 — OR / XOR: | && */
static void bc_exp0(struct bcom_ctx *ctx)
{
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    bc_exp1(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    for (;;)
    {
        if (tok_type_at(ctx, 0, TOK_LOGICAL) && tok_ch(ctx, 0) == '&' &&
            tok_type_at(ctx, 1, TOK_LOGICAL) && tok_ch(ctx, 1) == '&')
        {
            ctx->pos += 2;
            bc_exp1(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_XOR);
        }
        else if (tok_type_at(ctx, 0, TOK_LOGICAL) &&
                 tok_ch(ctx, 0) == '|' &&
                 !(tok_type_at(ctx, 1, TOK_LOGICAL) && tok_ch(ctx, 1) == '|'))
        {
            ctx->pos++;
            bc_exp1(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_OR);
        }
        else
        {
            break;
        }
    }
}

/* ================================================================== */
/*  Statement compiler                                                */
/* ================================================================== */

static void bc_stmt(struct bcom_ctx *ctx)
{
    const struct irx_token *t0 = tok_at(ctx, 0);
    const struct irx_token *t1 = tok_at(ctx, 1);

    if (t0 == NULL || t0->tok_type == TOK_EOC || t0->tok_type == TOK_EOF)
    {
        return;
    }

    emit_byte(ctx, OP_NEWCLAUSE);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    /* EXIT [expr] statement */
    if (t0->tok_type == TOK_SYMBOL && t0->tok_upper != NULL &&
        strcmp(t0->tok_upper, "EXIT") == 0)
    {
        const struct irx_token *t_next;
        ctx->pos++;
        t_next = tok_at(ctx, 0);
        if (t_next == NULL || t_next->tok_type == TOK_EOC ||
            t_next->tok_type == TOK_EOF)
        {
            /* EXIT without expression */
            emit_byte(ctx, OP_EXIT);
        }
        else
        {
            /* EXIT with expression: evaluate, pop as int, exit */
            bc_exp0(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_EXIT_RC);
        }
        ctx->hit_exit = 1;
        return;
    }

    /* Assignment: simple-symbol = expr  (= must be single, not ==) */
    if (t0->tok_type == TOK_SYMBOL && !(t0->tok_flags & TOKF_CONSTANT) &&
        t1 != NULL && t1->tok_type == TOK_COMPARISON &&
        t1->tok_length > 0 && t1->tok_text[0] == '=' &&
        !(tok_type_at(ctx, 2, TOK_COMPARISON) && tok_ch(ctx, 2) == '='))
    {
        const char *name =
            (t0->tok_upper != NULL) ? t0->tok_upper : t0->tok_text;
        int si = add_sym(ctx, name);
        if (si < 0)
        {
            ctx->rc = IRXBC_ERR_STOR;
            return;
        }
        ctx->pos += 2; /* consume symbol and = */
        bc_exp0(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_STORE);
        emit_u16(ctx, si);
        return;
    }

    ctx->rc = IRXBC_ERR_UNSUP;
}

/* ================================================================== */
/*  Program compiler                                                  */
/* ================================================================== */

static void bc_program(struct bcom_ctx *ctx)
{
    for (;;)
    {
        const struct irx_token *t = tok_at(ctx, 0);

        if (t == NULL || t->tok_type == TOK_EOF)
        {
            break;
        }
        if (t->tok_type == TOK_EOC)
        {
            ctx->pos++;
            continue;
        }

        bc_stmt(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        if (ctx->hit_exit)
        {
            return;
        }

        /* Consume trailing EOC */
        t = tok_at(ctx, 0);
        if (t != NULL && t->tok_type == TOK_EOC)
        {
            ctx->pos++;
        }
    }

    /* Implicit program exit */
    emit_byte(ctx, OP_EXIT);
}

/* ================================================================== */
/*  irx_bc_compile                                                    */
/* ================================================================== */

int irx_bc_compile(struct envblock *envblock,
                   const char *source, int source_len,
                   struct irx_bc_execblk **bc_out)
{
    struct irx_token *tokens = NULL;
    int tok_count = 0;
    struct irx_tokn_error tok_err;
    struct bcom_ctx *ctx = NULL;
    void *ctx_mem = NULL;
    struct irx_bc_execblk *bc = NULL;
    void *bc_mem = NULL;
    int total;
    char *dst;
    int i;
    int rc = IRXBC_OK;

    memset(&tok_err, 0, sizeof(tok_err));

    if (bc_out == NULL)
    {
        return IRXBC_ERR_UNSUP;
    }
    *bc_out = NULL;

    /* 1. Tokenize */
    rc = irx_tokn_run(envblock, source, source_len,
                      &tokens, &tok_count, &tok_err);
    if (rc != 0)
    {
        return IRXBC_ERR_TOKN;
    }

    /* 2. Allocate compiler context */
    if (irxstor(RXSMGET, (int)sizeof(struct bcom_ctx),
                &ctx_mem, envblock) != 0)
    {
        rc = IRXBC_ERR_STOR;
        goto cleanup;
    }
    ctx = (struct bcom_ctx *)ctx_mem;
    memset(ctx, 0, sizeof(struct bcom_ctx));
    ctx->env = envblock;
    ctx->tokens = tokens;
    ctx->tok_count = tok_count;
    ctx->rc = IRXBC_OK;

    /* 3. Compile */
    bc_program(ctx);
    rc = ctx->rc;
    if (rc != IRXBC_OK)
    {
        goto cleanup;
    }

    /* 4. Allocate EXECBLK */
    total = (int)sizeof(struct irx_bc_execblk) + ctx->const_count * IRXBC_ENTRY_SIZE + ctx->sym_count * IRXBC_ENTRY_SIZE + ctx->code_len;

    if (irxstor(RXSMGET, total, &bc_mem, envblock) != 0)
    {
        rc = IRXBC_ERR_STOR;
        goto cleanup;
    }

    /* 5. Populate header */
    bc = (struct irx_bc_execblk *)bc_mem;
    memset(bc, 0, (size_t)total);
    memcpy(bc->magic, IRXBC_MAGIC, sizeof(bc->magic));
    bc->version = IRXBC_VERSION;
    bc->flags = 0;
    bc->const_count = (uint32_t)ctx->const_count;
    bc->symbol_count = (uint32_t)ctx->sym_count;
    bc->code_length = (uint32_t)ctx->code_len;
    bc->entry_offset = 0;
    bc->trace_map_offset = 0;

    /* 6. Copy tables and bytecode */
    dst = IRXBC_CONST_TBL(bc);
    for (i = 0; i < ctx->const_count; i++)
    {
        memcpy(dst, ctx->consts[i], IRXBC_ENTRY_SIZE);
        dst += IRXBC_ENTRY_SIZE;
    }
    dst = IRXBC_SYM_TBL(bc);
    for (i = 0; i < ctx->sym_count; i++)
    {
        memcpy(dst, ctx->syms[i], IRXBC_ENTRY_SIZE);
        dst += IRXBC_ENTRY_SIZE;
    }
    memcpy(IRXBC_CODE(bc), ctx->code, (size_t)ctx->code_len);

    *bc_out = bc;
    bc_mem = NULL; /* ownership transferred */

cleanup:
    if (tokens != NULL)
    {
        irx_tokn_free(envblock, tokens, tok_count);
    }
    if (ctx_mem != NULL)
    {
        void *p = ctx_mem;
        irxstor(RXSMFRE, 0, &p, envblock);
    }
    if (bc_mem != NULL)
    {
        void *p = bc_mem;
        irxstor(RXSMFRE, 0, &p, envblock);
    }
    return rc;
}
