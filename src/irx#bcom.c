/* ------------------------------------------------------------------ */
/*  irx#bcom.c - REXX/370 Bytecode Compiler (WP-BC-03/04)            */
/*                                                                    */
/*  irx_bc_compile() — Entry point.                                  */
/*                                                                    */
/*  Scope (WP-BC-04 extends WP-BC-03):                               */
/*    - CALL name [arg,...] statement  (OP_CALL)                      */
/*    - RETURN [expr]                  (OP_RETURN / OP_RETURNV)       */
/*    - Function call in expression:   name(arg,...) (OP_CALL_BIF)   */
/*    - Internal labels               (OP_LABEL)                     */
/*                                                                    */
/*  Scope (WP-BC-03 extends WP-BC-02):                               */
/*    - SAY statement                                                 */
/*    - IF/THEN/ELSE with forward-jump patching                       */
/*    - SELECT/WHEN/OTHERWISE                                         */
/*    - DO FOREVER / DO WHILE cond / DO UNTIL cond                   */
/*    - DO count                                                      */
/*    - DO var = start TO limit [BY step] [FOR count]                */
/*    - ITERATE [label] / LEAVE [label]                              */
/*    - EXIT [expr] / assignment / expressions (from WP-BC-02)       */
/*                                                                    */
/*  Jump offsets are signed i16 little-endian, relative to the byte  */
/*  immediately after the full instruction.                           */
/*                                                                    */
/*  Memory: caller must free the returned irx_bc_execblk with        */
/*    irxstor(RXSMFRE, 0, &p, envblock)                              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <string.h>

#include "irx.h"
#include "irxbops.h"
#include "irxbvm.h"
#include "irxexbl.h"
#include "irxfunc.h"
#include "irxpars.h"
#include "irxtokn.h"
#include "irxwkblk.h"

/* ================================================================== */
/*  Compiler limits                                                   */
/* ================================================================== */

#define BCOM_MAX_CODE   16384
#define BCOM_MAX_CONSTS 512
#define BCOM_MAX_SYMS   512
#define BCOM_MAX_LOOP   16
#define BCOM_MAX_LPATCH 48
#define BCOM_MAX_LABEL  33

/* ================================================================== */
/*  DO loop type codes                                                */
/* ================================================================== */

#define BCTL_DO_FOREVER 0
#define BCTL_DO_COUNT   1
#define BCTL_DO_TO      2
#define BCTL_DO_WHILE   3
#define BCTL_DO_UNTIL   4
#define BCTL_SELECT     5
#define BCTL_DO_BLOCK   6 /* bare DO (simple group, execute once) */

/* ================================================================== */
/*  Loop / SELECT context frame (compile-time)                        */
/*                                                                    */
/*  iterate_known: 1 = iterate_target is final at push time           */
/*                 (FOREVER, WHILE) — ITERATE emits backward jump.    */
/*                 0 = iterate_target set later (COUNT, TO, UNTIL)    */
/*                     — ITERATE forwards are recorded in             */
/*                       iterate_patches[] and resolved when target   */
/*                       is set by loop_set_iterate.                  */
/* ================================================================== */

struct bc_loop_ctx
{
    int type;
    int loop_top;
    int iterate_target;
    int iterate_known;
    char label[BCOM_MAX_LABEL];
    int leave_patches[BCOM_MAX_LPATCH];
    int leave_count;
    int iterate_patches[BCOM_MAX_LPATCH];
    int iterate_count;
};

/* ================================================================== */
/*  Compiler context                                                   */
/* ================================================================== */

struct bcom_ctx
{
    struct envblock *env;
    const struct irx_token *tokens;
    int tok_count;
    int pos;

    unsigned char code[BCOM_MAX_CODE];
    int code_len;

    char consts[BCOM_MAX_CONSTS][IRXBC_ENTRY_SIZE];
    int const_count;

    char syms[BCOM_MAX_SYMS][IRXBC_ENTRY_SIZE];
    int sym_count;

    struct bc_loop_ctx loops[BCOM_MAX_LOOP];
    int loop_depth;

    int rc;
    int hit_exit;

    /* WP-BC-DIAG: set together at every UNSUP site via BC_FAIL_UNSUP.
     * unsup_reason holds a bc_unsup_reason code (see below); unsup_line
     * is the 1-based source line of the offending token, captured by
     * bc_cur_line() at the moment the compiler gives up.  Both stay at
     * their zero-initialised defaults (BC_UNSUP_NONE / 0) unless an
     * UNSUP is hit. */
    int unsup_reason;
    int unsup_line;
};

/* ================================================================== */
/*  UNSUP reason codes (WP-BC-DIAG)                                    */
/*                                                                    */
/*  Each of the IRXBC_ERR_UNSUP sites in this file records one of     */
/*  these codes so the REXX370_BCDEBUG diagnostic can report WHICH    */
/*  construct forced the token-walk fallback (and on which line).     */
/*  Codes are MVS-memory-friendly: the call sites carry only the      */
/*  small integer; the human-readable strings live once, centrally,   */
/*  in bc_unsup_text[] (see irx_bc_unsup_text below).                 */
/*                                                                    */
/*  Some sites are defensive syntax guards and legitimately share a   */
/*  coarse code; others mark real feature gaps and get a specific     */
/*  one.  BC_UNSUP_COUNT is the table-sizing sentinel — keep it last. */
/* ================================================================== */

enum bc_unsup_reason
{
    BC_UNSUP_NONE = 0,             /* no UNSUP recorded                 */
    BC_UNSUP_INTERNAL,             /* API misuse / internal guard       */
    BC_UNSUP_STATEMENT,            /* unrecognised statement keyword    */
    BC_UNSUP_EXPR_OPERAND,         /* unsupported expression operand    */
    BC_UNSUP_EXPR_PAREN,           /* ')' expected in (sub)expression   */
    BC_UNSUP_TOO_MANY_ARGS,        /* argument count exceeds limit      */
    BC_UNSUP_FUNC_ARG_SEP,         /* malformed function-call arg list  */
    BC_UNSUP_CALL_TARGET,          /* CALL target is not a plain label  */
    BC_UNSUP_CALL_ARG_SEP,         /* malformed CALL argument list      */
    BC_UNSUP_PARSE_INDIRECT,       /* PARSE indirect pattern (var)      */
    BC_UNSUP_PARSE_RELPOS,         /* PARSE relative position (+n/-n)   */
    BC_UNSUP_PARSE_ABSPOS,         /* PARSE absolute position (=n)      */
    BC_UNSUP_PARSE_TOO_MANY_TAILS, /* template tail count limit       */
    BC_UNSUP_PARSE_TEMPLATE,       /* unsupported PARSE template item   */
    BC_UNSUP_PARSE_VAR,            /* PARSE VAR target not a symbol     */
    BC_UNSUP_PARSE_VALUE_WITH,     /* PARSE VALUE without WITH          */
    BC_UNSUP_PARSE_SOURCE,         /* unsupported PARSE source keyword  */
    BC_UNSUP_EXPOSE_INDIRECT,      /* PROCEDURE EXPOSE indirect (var)   */
    BC_UNSUP_EXPOSE_LIMIT,         /* PROCEDURE EXPOSE count limit      */
    BC_UNSUP_COMPOUND_TAIL_LIMIT,  /* compound variable tail limit     */
    BC_UNSUP_IF_THEN,              /* IF condition not followed by THEN */
    BC_UNSUP_WHEN_THEN,            /* WHEN condition not followed by THEN*/
    BC_UNSUP_SELECT_BODY,          /* unexpected token in SELECT body   */
    BC_UNSUP_DO_CONTROL,           /* unsupported controlled-DO clause  */
    BC_UNSUP_ITERATE_TARGET,       /* ITERATE has no matching loop      */
    BC_UNSUP_LEAVE_TARGET,         /* LEAVE has no matching loop/SELECT */
    BC_UNSUP_TRACE_VALUE,          /* TRACE VALUE with empty expression */
    BC_UNSUP_TRACE_SETTING,        /* empty/invalid TRACE setting word  */
    BC_UNSUP_TRACE_FORM,           /* unsupported TRACE form            */
    BC_UNSUP_ADDRESS_VALUE,        /* ADDRESS VALUE with empty expr     */
    BC_UNSUP_ADDRESS_FORM,         /* unsupported ADDRESS form          */
    BC_UNSUP_SIGNAL_CONDITION,     /* SIGNAL ON/OFF unknown condition   */
    BC_UNSUP_SIGNAL_NAME,          /* SIGNAL ON ... NAME not a symbol   */
    BC_UNSUP_SIGNAL_TARGET,        /* SIGNAL label is not a symbol      */
    BC_UNSUP_DROP_TARGET,          /* DROP target is not a symbol       */
    BC_UNSUP_COUNT                 /* sentinel — table size; keep last  */
};

/* ================================================================== */
/*  Forward declarations                                              */
/* ================================================================== */

static void bc_exp0(struct bcom_ctx *ctx);
static void bc_expr(struct bcom_ctx *ctx);
static void bc_stmt(struct bcom_ctx *ctx);
static void bc_stmts_until(struct bcom_ctx *ctx, const char *stop1,
                           const char *stop2, const char *stop3);

/* ================================================================== */
/*  Emit helpers                                                      */
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

static int emit_i16(struct bcom_ctx *ctx, int offset)
{
    unsigned int v = (unsigned int)(short)(offset);
    if (ctx->code_len + 2 > BCOM_MAX_CODE)
    {
        ctx->rc = IRXBC_ERR_STOR;
        return IRXBC_ERR_STOR;
    }
    ctx->code[ctx->code_len++] = (unsigned char)(v & 0xFF);
    ctx->code[ctx->code_len++] = (unsigned char)((v >> 8) & 0xFF);
    return IRXBC_OK;
}

/* Emit op + i16 placeholder; return position of the op byte or -1. */
static int emit_jmp_op(struct bcom_ctx *ctx, unsigned char op)
{
    int pos = ctx->code_len;
    if (emit_byte(ctx, op) != IRXBC_OK || emit_i16(ctx, 0) != IRXBC_OK)
    {
        return -1;
    }
    return pos;
}

/* Patch i16 at code[patch_pos+1..+2] to jump to ctx->code_len. */
static void patch_jmp_to_here(struct bcom_ctx *ctx, int patch_pos)
{
    unsigned int v;
    int off;
    if (patch_pos < 0 || ctx->rc != IRXBC_OK)
    {
        return;
    }
    off = ctx->code_len - (patch_pos + 3);
    v = (unsigned int)(short)(off);
    ctx->code[patch_pos + 1] = (unsigned char)(v & 0xFF);
    ctx->code[patch_pos + 2] = (unsigned char)((v >> 8) & 0xFF);
}

/* Patch a JMP to a specific target position. */
static void patch_jmp_to(struct bcom_ctx *ctx, int patch_pos, int target)
{
    unsigned int v;
    int off;
    if (patch_pos < 0 || ctx->rc != IRXBC_OK)
    {
        return;
    }
    off = target - (patch_pos + 3);
    v = (unsigned int)(short)(off);
    ctx->code[patch_pos + 1] = (unsigned char)(v & 0xFF);
    ctx->code[patch_pos + 2] = (unsigned char)((v >> 8) & 0xFF);
}

/* Emit a backward jump to a known target. */
static void emit_jmp_back(struct bcom_ctx *ctx, unsigned char op, int target)
{
    int patch_pos = emit_jmp_op(ctx, op);
    if (patch_pos < 0)
    {
        return;
    }
    patch_jmp_to(ctx, patch_pos, target);
}

/* ================================================================== */
/*  Token helpers                                                     */
/* ================================================================== */

static const struct irx_token *tok_at(const struct bcom_ctx *ctx, int offset)
{
    int i = ctx->pos + offset;
    if (i < 0 || i >= ctx->tok_count)
    {
        return NULL;
    }
    return &ctx->tokens[i];
}

static int tok_type_at(const struct bcom_ctx *ctx, int offset,
                       unsigned char type)
{
    const struct irx_token *t = tok_at(ctx, offset);
    return t != NULL && t->tok_type == type;
}

static char tok_ch(const struct bcom_ctx *ctx, int offset)
{
    const struct irx_token *t = tok_at(ctx, offset);
    if (t == NULL || t->tok_length == 0 || t->tok_text == NULL)
    {
        return 0;
    }
    return t->tok_text[0];
}

static int tok_kw(const struct bcom_ctx *ctx, int offset, const char *kw)
{
    const struct irx_token *t = tok_at(ctx, offset);
    if (t == NULL || t->tok_type != TOK_SYMBOL || t->tok_upper == NULL)
    {
        return 0;
    }
    return strcmp(t->tok_upper, kw) == 0;
}

/* True if t is an explicit ';' clause separator.
 *
 * The tokenizer maps BOTH ';' and ':' to TOK_SEMICOLON, distinguished
 * only by the source character: a ';' terminates a clause exactly like
 * a logical newline (TOK_EOC), whereas a ':' marks a label and must
 * NOT be treated as a clause end (see the label special-case in
 * bc_program).  This helper returns true for ';' only, never ':'. */
static int tok_is_semi(const struct irx_token *t)
{
    return t != NULL && t->tok_type == TOK_SEMICOLON &&
           t->tok_length > 0 && t->tok_text != NULL && t->tok_text[0] == ';';
}

static int tok_ends_clause(const struct bcom_ctx *ctx)
{
    const struct irx_token *t = tok_at(ctx, 0);
    return t == NULL || t->tok_type == TOK_EOC || t->tok_type == TOK_EOF ||
           tok_is_semi(t);
}

/* True if the token at offset is a continuation-comma: a TOK_COMMA the
 * tokenizer kept (suppressing the line-ending EOC) and flagged
 * TOKF_CONTINUATION because it ended a physical line (SC28-1883-0 §3.2,
 * src/irx#tokn.c).  In a general expression context this is a blank
 * concatenation, NOT an argument separator (WP-BC-CONTCOMMA). */
static int tok_is_cont_comma(const struct bcom_ctx *ctx, int offset)
{
    const struct irx_token *t = tok_at(ctx, offset);
    return t != NULL && t->tok_type == TOK_COMMA &&
           (t->tok_flags & TOKF_CONTINUATION) != 0;
}

static void skip_eoc(struct bcom_ctx *ctx)
{
    const struct irx_token *t;
    while ((t = tok_at(ctx, 0)) != NULL &&
           (t->tok_type == TOK_EOC || tok_is_semi(t)))
    {
        ctx->pos++;
    }
}

static void consume_eoc(struct bcom_ctx *ctx)
{
    const struct irx_token *t = tok_at(ctx, 0);
    if (t != NULL && (t->tok_type == TOK_EOC || tok_is_semi(t)))
    {
        ctx->pos++;
    }
}

/* Skip a single logical-newline (TOK_EOC) so a THEN/ELSE/WHEN body that
 * begins on the next physical line is found.  A ';' is deliberately NOT
 * skipped: a ';' immediately after THEN/ELSE is a null clause (an empty
 * conditional body), matching the token-walk interpreter.  The ';' is
 * left in place for the caller's clause-separator consume_eoc, and the
 * empty body is produced by bc_stmt's tok_is_semi early-return.  Using
 * consume_eoc here instead would swallow the ';' and wrongly absorb the
 * following clause as the body. */
static void consume_newline(struct bcom_ctx *ctx)
{
    if (tok_type_at(ctx, 0, TOK_EOC))
    {
        ctx->pos++;
    }
}

/* ================================================================== */
/*  UNSUP diagnostic helpers (WP-BC-DIAG)                             */
/* ================================================================== */

/* Source line of the token the compiler is currently looking at.
 * Clamps pos into range and steps back over the TOK_EOF sentinel
 * (whose tok_line is upbuf_cap, not a real line) so an UNSUP raised
 * at end-of-source still reports the last meaningful line. */
static int bc_cur_line(const struct bcom_ctx *ctx)
{
    int p = ctx->pos;

    if (ctx->tokens == NULL || ctx->tok_count <= 0)
    {
        return 0;
    }
    if (p < 0)
    {
        p = 0;
    }
    if (p >= ctx->tok_count)
    {
        p = ctx->tok_count - 1;
    }
    while (p > 0 && ctx->tokens[p].tok_type == TOK_EOF)
    {
        p--;
    }
    return ctx->tokens[p].tok_line;
}

/* Record an unsupported construct: set rc plus the diagnostic
 * reason/line in one place.  Control flow (return / break / return -1)
 * stays explicit at each call site because it differs per site. */
#define BC_FAIL_UNSUP(ctx, reason)            \
    do                                        \
    {                                         \
        (ctx)->rc = IRXBC_ERR_UNSUP;          \
        (ctx)->unsup_reason = (reason);       \
        (ctx)->unsup_line = bc_cur_line(ctx); \
    } while (0)

/* ================================================================== */
/*  Symbol / constant table                                           */
/* ================================================================== */

static int add_const(struct bcom_ctx *ctx, const char *text, int len)
{
    int i;
    if (len > IRXBC_STR_MAX)
    {
        ctx->rc = IRXBC_ERR_STRTOOLONG;
        return -1;
    }
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
        ctx->rc = IRXBC_ERR_STOR;
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

static int add_sym(struct bcom_ctx *ctx, const char *name)
{
    int i;
    int len = (int)strlen(name);
    if (len > IRXBC_STR_MAX)
    {
        ctx->rc = IRXBC_ERR_STRTOOLONG;
        return -1;
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
        ctx->rc = IRXBC_ERR_STOR;
        return -1;
    }
    i = ctx->sym_count++;
    ctx->syms[i][0] = (char)(unsigned char)len;
    memcpy(ctx->syms[i] + 1, name, (size_t)len);
    return i;
}

/* Emit PUSH_LIT for a small integer string (1-2 chars, "0".."99"). */
static void emit_push_int(struct bcom_ctx *ctx, const char *s)
{
    int ci = add_const(ctx, s, (int)strlen(s));
    if (ci < 0)
    {
        return;
    }
    emit_byte(ctx, OP_PUSH_LIT);
    emit_u16(ctx, ci);
}

/* Emit STORE sym_idx */
static void emit_store(struct bcom_ctx *ctx, int si)
{
    emit_byte(ctx, OP_STORE);
    emit_u16(ctx, si);
}

/* Emit LOAD sym_idx */
static void emit_load(struct bcom_ctx *ctx, int si)
{
    emit_byte(ctx, OP_LOAD);
    emit_u16(ctx, si);
}

/* Build a compiler-generated DO loop symbol: __DO<depth>_<suffix> */
static void make_do_sym(char *buf, int depth, const char *suffix)
{
    int i = 0;
    buf[i++] = '_';
    buf[i++] = '_';
    buf[i++] = 'D';
    buf[i++] = 'O';
    buf[i++] = (char)('0' + (depth % 10));
    buf[i++] = '_';
    while (*suffix)
    {
        buf[i++] = *suffix++;
    }
    buf[i] = '\0';
}

/* ================================================================== */
/*  Loop context helpers                                              */
/* ================================================================== */

static struct bc_loop_ctx *loop_push(struct bcom_ctx *ctx, int type)
{
    struct bc_loop_ctx *f;
    if (ctx->loop_depth >= BCOM_MAX_LOOP)
    {
        ctx->rc = IRXBC_ERR_LOOP;
        return NULL;
    }
    f = &ctx->loops[ctx->loop_depth++];
    memset(f, 0, sizeof(struct bc_loop_ctx));
    f->type = type;
    return f;
}

static void loop_pop(struct bcom_ctx *ctx)
{
    if (ctx->loop_depth > 0)
    {
        ctx->loop_depth--;
    }
}

static void loop_patch_leaves(struct bcom_ctx *ctx, struct bc_loop_ctx *f)
{
    int i;
    for (i = 0; i < f->leave_count; i++)
    {
        patch_jmp_to_here(ctx, f->leave_patches[i]);
    }
}

/* Set iterate_target and resolve any pending iterate patches. */
static void loop_set_iterate(struct bcom_ctx *ctx, struct bc_loop_ctx *f,
                             int target)
{
    int i;
    f->iterate_target = target;
    f->iterate_known = 1;
    for (i = 0; i < f->iterate_count; i++)
    {
        patch_jmp_to(ctx, f->iterate_patches[i], target);
    }
    f->iterate_count = 0;
}

/* Find the innermost true loop frame (not SELECT), optionally matching label. */
static struct bc_loop_ctx *loop_find(struct bcom_ctx *ctx, const char *label)
{
    int i;
    if (label == NULL || label[0] == '\0')
    {
        for (i = ctx->loop_depth - 1; i >= 0; i--)
        {
            if (ctx->loops[i].type != BCTL_SELECT)
            {
                return &ctx->loops[i];
            }
        }
        return NULL;
    }
    for (i = ctx->loop_depth - 1; i >= 0; i--)
    {
        if (ctx->loops[i].type != BCTL_SELECT &&
            strcmp(ctx->loops[i].label, label) == 0)
        {
            return &ctx->loops[i];
        }
    }
    return NULL;
}

/* Find the innermost SELECT frame. */
static struct bc_loop_ctx *select_frame(struct bcom_ctx *ctx)
{
    int i;
    for (i = ctx->loop_depth - 1; i >= 0; i--)
    {
        if (ctx->loops[i].type == BCTL_SELECT)
        {
            return &ctx->loops[i];
        }
    }
    return NULL;
}

static void loop_add_leave_patch(struct bcom_ctx *ctx, struct bc_loop_ctx *f,
                                 int patch_pos)
{
    if (f->leave_count >= BCOM_MAX_LPATCH)
    {
        ctx->rc = IRXBC_ERR_PATCH;
        return;
    }
    f->leave_patches[f->leave_count++] = patch_pos;
}

static void loop_add_iterate_patch(struct bcom_ctx *ctx, struct bc_loop_ctx *f,
                                   int patch_pos)
{
    if (f->iterate_count >= BCOM_MAX_LPATCH)
    {
        ctx->rc = IRXBC_ERR_PATCH;
        return;
    }
    f->iterate_patches[f->iterate_count++] = patch_pos;
}

/* ================================================================== */
/*  Adjacency check (WP-BC-04)                                        */
/* ================================================================== */

/* True when b immediately follows a in source with no whitespace gap.
 *
 * The source-end column of a string-like token is its start column plus
 * its body length plus the surrounding delimiters that tok_length does
 * not count: 2 for a plain quoted string, 3 for a hex / bin string (which
 * also carries a trailing x / b suffix).  This mirrors tok_source_end_col
 * in the token-walk (src/irx#pars.c) so the bytecode compiler computes
 * adjacency identically.  The hex/bin arm is currently unreachable from
 * the concatenation site (bc_exp8 rejects hex/bin operands and forces a
 * whole-program token-walk fallback); it is kept faithful to the
 * reference so the helper stays correct if that ever changes. */
static int toks_adjacent_bc(const struct irx_token *a,
                            const struct irx_token *b)
{
    int end;
    if (a == NULL || b == NULL)
    {
        return 0;
    }
    if (a->tok_line != b->tok_line)
    {
        return 0;
    }
    end = (int)a->tok_col + (int)a->tok_length;
    if (a->tok_type == TOK_STRING)
    {
        end += 2;
    }
    else if (a->tok_type == TOK_HEXSTRING || a->tok_type == TOK_BINSTRING)
    {
        end += 3;
    }
    return end == (int)b->tok_col;
}

/* ================================================================== */
/*  Function call expression — name(arg, ...) (WP-BC-04)             */
/* ================================================================== */

/* Called with ctx->pos pointing PAST the symbol and '(' already
 * consumed.  Collects comma-separated arg expressions up to ')'.
 * Emits OP_CALL_BIF sym_idx nargs.  sym_idx is for the uppercased
 * function name. */
static void bc_funcall(struct bcom_ctx *ctx, int sym_idx)
{
    int nargs = 0;

    /* Empty arg list */
    if (tok_type_at(ctx, 0, TOK_RPAREN))
    {
        ctx->pos++;
        emit_byte(ctx, OP_CALL_BIF);
        emit_u16(ctx, sym_idx);
        emit_byte(ctx, (unsigned char)nargs);
        return;
    }

    for (;;)
    {
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        if (nargs >= IRX_MAX_ARGS)
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_TOO_MANY_ARGS);
            return;
        }
        /* An omitted argument is an empty slot — a ',' or ')' where an
         * expression is expected (f(,b), f(a,,c), f(a,)).  Emit the
         * omitted marker instead of compiling an expression; the marker
         * still occupies one argument slot.  Mirrors the token-walk
         * parse_function_call() (WP-BC-ARGOMIT). */
        if (tok_type_at(ctx, 0, TOK_COMMA) ||
            tok_type_at(ctx, 0, TOK_RPAREN))
        {
            emit_byte(ctx, OP_PUSH_OMITTED);
        }
        else
        {
            bc_exp0(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
        }
        nargs++;

        if (tok_type_at(ctx, 0, TOK_RPAREN))
        {
            ctx->pos++;
            break;
        }
        if (tok_type_at(ctx, 0, TOK_COMMA))
        {
            ctx->pos++;
            continue;
        }
        BC_FAIL_UNSUP(ctx, BC_UNSUP_FUNC_ARG_SEP);
        return;
    }

    emit_byte(ctx, OP_CALL_BIF);
    emit_u16(ctx, sym_idx);
    emit_byte(ctx, (unsigned char)nargs);
}

/* ================================================================== */
/*  CALL statement (WP-BC-04)                                         */
/* ================================================================== */

/* Collect comma-separated args until EOC/EOF.  Each arg is an expr.
 * Emits OP_CALL sym_idx nargs. */
static void bc_call_stmt(struct bcom_ctx *ctx)
{
    const struct irx_token *t;
    const char *name;
    int sym_idx;
    int nargs = 0;

    ctx->pos++; /* consume CALL */

    t = tok_at(ctx, 0);
    if (t == NULL || t->tok_type != TOK_SYMBOL ||
        (t->tok_flags & TOKF_CONSTANT))
    {
        BC_FAIL_UNSUP(ctx, BC_UNSUP_CALL_TARGET);
        return;
    }

    name = (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;
    sym_idx = add_sym(ctx, name);
    if (sym_idx < 0)
    {
        return;
    }
    ctx->pos++; /* consume target name */

    /* Optional arg list: CALL NAME arg1, arg2 ... */
    if (!tok_ends_clause(ctx))
    {
        int after_comma = 0;
        for (;;)
        {
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            if (nargs >= IRX_MAX_ARGS)
            {
                BC_FAIL_UNSUP(ctx, BC_UNSUP_TOO_MANY_ARGS);
                return;
            }
            /* Omitted argument at this position — a leading or
             * between-commas empty slot (CALL f ,x  /  CALL f a,,c).
             * Mirrors the token-walk kw_call (WP-BC-ARGOMIT). */
            if (tok_type_at(ctx, 0, TOK_COMMA))
            {
                emit_byte(ctx, OP_PUSH_OMITTED);
                nargs++;
                ctx->pos++;
                after_comma = 1;
                continue;
            }
            /* A trailing comma (CALL f a,) contributes one final
             * omitted argument before the clause ends. */
            if (tok_ends_clause(ctx))
            {
                if (after_comma)
                {
                    emit_byte(ctx, OP_PUSH_OMITTED);
                    nargs++;
                }
                break;
            }
            bc_exp0(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            nargs++;
            after_comma = 0;
            if (tok_type_at(ctx, 0, TOK_COMMA))
            {
                ctx->pos++;
                after_comma = 1;
                continue;
            }
            if (tok_ends_clause(ctx))
            {
                break;
            }
            BC_FAIL_UNSUP(ctx, BC_UNSUP_CALL_ARG_SEP);
            return;
        }
    }

    emit_byte(ctx, OP_CALL);
    emit_u16(ctx, sym_idx);
    emit_byte(ctx, (unsigned char)nargs);
}

/* ================================================================== */
/*  RETURN statement (WP-BC-04)                                       */
/* ================================================================== */

static void bc_return_stmt(struct bcom_ctx *ctx)
{
    ctx->pos++; /* consume RETURN */

    if (tok_ends_clause(ctx))
    {
        emit_byte(ctx, OP_RETURN);
        return;
    }

    bc_expr(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }
    emit_byte(ctx, OP_RETURNV);
}

/* ================================================================== */
/*  PARSE statement compiler (WP-BC-05 PR A)                          */
/* ================================================================== */

/* Version string returned by PARSE VERSION (must match irx#pars.c). */
#define BCOM_PARSE_VERSION "REXX370 0.1.0 16 Apr 2026"

/*
 * One item in a pending template item list.
 * Items accumulate until a trigger fires; when flushed:
 *   items[0..n-2] get OP_TR_SPACE (one word each)
 *   items[n-1]    gets the actual trigger
 *
 * NOTE: The one-pvar-at-a-time model diverges from the token-walk
 * when N≥2 items precede an absolute/relative position trigger that
 * splits mid-word.  This is a known limitation — the pattern is rare
 * and intentionally absent from the test suite.
 */
#define BPSE_MAX_ITEMS 16
#define BPSE_MAX_TAILS 8

struct bpse_tail
{
    int is_const; /* 1 = constant (push lit), 0 = variable (load sym) */
    int idx;      /* const_table or sym_table index */
};

struct bpse_item
{
    int is_dot;
    int sym_idx;
    int is_compound; /* 1 if compound variable target */
    int stem_idx;    /* sym index for stem (e.g. "A.") */
    int tail_count;
    struct bpse_tail tails[BPSE_MAX_TAILS];
};

/* Emit the bytecode for a single parse-template item (no trigger). */
static void bpse_emit_item(struct bcom_ctx *ctx, const struct bpse_item *it)
{
    if (it->is_dot)
    {
        emit_byte(ctx, OP_PDOT);
    }
    else if (it->is_compound)
    {
        int j;
        for (j = 0; j < it->tail_count; j++)
        {
            if (it->tails[j].is_const)
            {
                emit_byte(ctx, OP_PUSH_LIT);
                emit_u16(ctx, it->tails[j].idx);
            }
            else
            {
                emit_byte(ctx, OP_LOAD);
                emit_u16(ctx, it->tails[j].idx);
            }
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
        }
        emit_byte(ctx, OP_PVAR_STEM);
        emit_u16(ctx, it->stem_idx);
        emit_byte(ctx, (unsigned char)it->tail_count);
    }
    else
    {
        emit_byte(ctx, OP_PVAR);
        emit_u16(ctx, it->sym_idx);
    }
}

static void bpse_flush(struct bcom_ctx *ctx,
                       const struct bpse_item *items, int n_items,
                       unsigned char last_trigger, int targ)
{
    int i;
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    for (i = 0; i < n_items - 1; i++)
    {
        bpse_emit_item(ctx, &items[i]);
        emit_byte(ctx, OP_TR_SPACE);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
    }

    if (n_items == 0)
    {
        emit_byte(ctx, OP_PDOT);
    }
    else
    {
        bpse_emit_item(ctx, &items[n_items - 1]);
    }
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    switch (last_trigger)
    {
        case OP_TR_SPACE:
        {
            emit_byte(ctx, OP_TR_SPACE);
            break;
        }
        case OP_TR_LIT:
        {
            emit_byte(ctx, OP_TR_LIT);
            emit_u16(ctx, targ);
            break;
        }
        case OP_TR_ABS:
        {
            emit_byte(ctx, OP_TR_ABS);
            emit_u16(ctx, targ);
            break;
        }
        case OP_TR_REL:
        {
            emit_byte(ctx, OP_TR_REL);
            emit_i16(ctx, targ);
            break;
        }
        case OP_TR_END:
        {
            emit_byte(ctx, OP_TR_END);
            break;
        }
        case OP_TR_VAR:
        {
            emit_byte(ctx, OP_TR_VAR);
            emit_u16(ctx, targ); /* targ = sym_idx */
            break;
        }
        default:
        {
            break;
        }
    }
}

/* Collapse doubled quote pairs in a string literal token text. */
static int bpse_dedouble(const char *raw, int raw_len,
                         char *out, int out_max)
{
    unsigned char pair = '\'';
    int k, saw_single = 0, saw_double = 0, out_len = 0;

    for (k = 0; k < raw_len - 1; k++)
    {
        if ((unsigned char)raw[k] == '\'' && (unsigned char)raw[k + 1] == '\'')
        {
            saw_single = 1;
        }
        if ((unsigned char)raw[k] == '"' && (unsigned char)raw[k + 1] == '"')
        {
            saw_double = 1;
        }
    }
    if (saw_double && !saw_single)
    {
        pair = '"';
    }

    for (k = 0; k < raw_len;)
    {
        if (out_len >= out_max)
        {
            return -1;
        }
        if (k + 1 < raw_len && (unsigned char)raw[k] == pair &&
            (unsigned char)raw[k + 1] == pair)
        {
            out[out_len++] = raw[k];
            k += 2;
        }
        else
        {
            out[out_len++] = raw[k++];
        }
    }
    return out_len;
}

static void bc_parse_template(struct bcom_ctx *ctx)
{
    struct bpse_item items[BPSE_MAX_ITEMS];
    int n_items = 0;

    while (ctx->rc == IRXBC_OK && !tok_ends_clause(ctx))
    {
        const struct irx_token *t = tok_at(ctx, 0);
        if (t == NULL)
        {
            break;
        }

        if (t->tok_type == TOK_COMMA)
        {
            break;
        }

        /* Indirect pattern (var) — delimiter value taken from variable
         * at runtime.  Only simple, non-compound variable names are
         * accepted inside the parens; anything else is UNSUP.
         * Compound variables (TOKF_COMPOUND) are rejected conservatively:
         * variable-tail compounds (a.x) cannot be resolved at compile
         * time and would silently look up the literal name "A.X" in the
         * vpool, giving wrong results.  Unlock in a later WP if needed. */
        if (t->tok_type == TOK_LPAREN)
        {
            const struct irx_token *t1 = tok_at(ctx, 1);
            const struct irx_token *t2 = tok_at(ctx, 2);
            if (t1 != NULL && t1->tok_type == TOK_SYMBOL &&
                !(t1->tok_flags & TOKF_CONSTANT) &&
                !(t1->tok_flags & TOKF_COMPOUND) &&
                t2 != NULL && t2->tok_type == TOK_RPAREN)
            {
                const char *vname =
                    (t1->tok_upper != NULL) ? t1->tok_upper : t1->tok_text;
                int si = add_sym(ctx, vname);
                if (si < 0)
                {
                    return;
                }
                ctx->pos += 3; /* consume '(' sym ')' */
                bpse_flush(ctx, items, n_items, OP_TR_VAR, si);
                n_items = 0;
                continue;
            }
            BC_FAIL_UNSUP(ctx, BC_UNSUP_PARSE_INDIRECT);
            return;
        }

        /* Relative position: +n or -n */
        if (t->tok_type == TOK_OPERATOR && t->tok_length == 1 &&
            (t->tok_text[0] == '+' || t->tok_text[0] == '-'))
        {
            const struct irx_token *t1 = tok_at(ctx, 1);
            if (t1 != NULL && t1->tok_type == TOK_NUMBER)
            {
                int n = 0, k;
                for (k = 0; k < (int)t1->tok_length; k++)
                {
                    if (t1->tok_text[k] >= '0' && t1->tok_text[k] <= '9')
                    {
                        n = n * 10 + (t1->tok_text[k] - '0');
                    }
                }
                if (t->tok_text[0] == '-')
                {
                    n = -n;
                }
                ctx->pos += 2;
                bpse_flush(ctx, items, n_items, OP_TR_REL, n);
                n_items = 0;
                continue;
            }
            BC_FAIL_UNSUP(ctx, BC_UNSUP_PARSE_RELPOS);
            return;
        }

        /* Absolute position: bare integer */
        if (t->tok_type == TOK_NUMBER)
        {
            int col = 0, k;
            for (k = 0; k < (int)t->tok_length; k++)
            {
                if (t->tok_text[k] >= '0' && t->tok_text[k] <= '9')
                {
                    col = col * 10 + (t->tok_text[k] - '0');
                }
            }
            ctx->pos++;
            bpse_flush(ctx, items, n_items, OP_TR_ABS, col);
            n_items = 0;
            continue;
        }

        /* Absolute position: =n */
        if (t->tok_type == TOK_COMPARISON && t->tok_length == 1 &&
            t->tok_text[0] == '=')
        {
            const struct irx_token *t1 = tok_at(ctx, 1);
            if (t1 != NULL && t1->tok_type == TOK_NUMBER)
            {
                int col = 0, k;
                for (k = 0; k < (int)t1->tok_length; k++)
                {
                    if (t1->tok_text[k] >= '0' && t1->tok_text[k] <= '9')
                    {
                        col = col * 10 + (t1->tok_text[k] - '0');
                    }
                }
                ctx->pos += 2;
                bpse_flush(ctx, items, n_items, OP_TR_ABS, col);
                n_items = 0;
                continue;
            }
            BC_FAIL_UNSUP(ctx, BC_UNSUP_PARSE_ABSPOS);
            return;
        }

        /* String literal: search trigger */
        if (t->tok_type == TOK_STRING)
        {
            char tmp[IRXBC_STR_MAX + 1];
            int ci;
            if (t->tok_flags & TOKF_QUOTE_DBL)
            {
                int tlen = bpse_dedouble(t->tok_text, (int)t->tok_length,
                                         tmp, IRXBC_STR_MAX);
                if (tlen < 0)
                {
                    ctx->rc = IRXBC_ERR_STRTOOLONG;
                    return;
                }
                ci = add_const(ctx, tmp, tlen);
            }
            else
            {
                ci = add_const(ctx, t->tok_text, (int)t->tok_length);
            }
            if (ci < 0)
            {
                return;
            }
            ctx->pos++;
            bpse_flush(ctx, items, n_items, OP_TR_LIT, ci);
            n_items = 0;
            continue;
        }

        /* Symbol: variable name, dot placeholder, or compound variable */
        if (t->tok_type == TOK_SYMBOL)
        {
            const char *txt = t->tok_text;
            int tlen = (int)t->tok_length;
            int dot_pos = -1;
            int j;

            /* Standalone dot */
            if (tlen == 1 && txt[0] == '.')
            {
                if (n_items < BPSE_MAX_ITEMS)
                {
                    items[n_items].is_dot = 1;
                    items[n_items].sym_idx = -1;
                    items[n_items].is_compound = 0;
                    n_items++;
                }
                ctx->pos++;
                continue;
            }

            /* Find first dot to detect compound variable */
            for (j = 0; j < tlen; j++)
            {
                if (txt[j] == '.')
                {
                    dot_pos = j;
                    break;
                }
            }

            if (dot_pos >= 0)
            {
                /* Compound variable target (e.g. a.1, a.i, a.i.j) */
                const char *up = (t->tok_upper != NULL) ? t->tok_upper : txt;
                char stem_buf[IRXBC_STR_MAX + 2];
                int stem_len = dot_pos + 1; /* includes trailing dot */
                int stem_si;
                int k;

                if (stem_len > IRXBC_STR_MAX)
                {
                    ctx->rc = IRXBC_ERR_STRTOOLONG;
                    return;
                }
                memcpy(stem_buf, up, (size_t)stem_len);
                stem_buf[stem_len] = '\0';
                stem_si = add_sym(ctx, stem_buf);
                if (stem_si < 0)
                {
                    return;
                }
                if (n_items < BPSE_MAX_ITEMS)
                {
                    struct bpse_item *it = &items[n_items];
                    it->is_dot = 0;
                    it->sym_idx = -1;
                    it->is_compound = 1;
                    it->stem_idx = stem_si;
                    it->tail_count = 0;

                    k = stem_len;
                    while (k < tlen)
                    {
                        int seg_start = k;
                        int seg_len;
                        int is_const_tail;

                        while (k < tlen && up[k] != '.')
                        {
                            k++;
                        }
                        seg_len = k - seg_start;
                        if (k < tlen)
                        {
                            k++;
                        }
                        if (it->tail_count >= BPSE_MAX_TAILS)
                        {
                            BC_FAIL_UNSUP(ctx, BC_UNSUP_PARSE_TOO_MANY_TAILS);
                            return;
                        }
                        is_const_tail = (seg_len == 0 ||
                                         isdigit((unsigned char)up[seg_start]));
                        if (is_const_tail)
                        {
                            int ci = add_const(ctx, up + seg_start, seg_len);
                            if (ci < 0)
                            {
                                return;
                            }
                            it->tails[it->tail_count].is_const = 1;
                            it->tails[it->tail_count].idx = ci;
                        }
                        else
                        {
                            char seg_buf[IRXBC_STR_MAX + 1];
                            int si2;
                            if (seg_len > IRXBC_STR_MAX)
                            {
                                ctx->rc = IRXBC_ERR_STRTOOLONG;
                                return;
                            }
                            memcpy(seg_buf, up + seg_start, (size_t)seg_len);
                            seg_buf[seg_len] = '\0';
                            si2 = add_sym(ctx, seg_buf);
                            if (si2 < 0)
                            {
                                return;
                            }
                            it->tails[it->tail_count].is_const = 0;
                            it->tails[it->tail_count].idx = si2;
                        }
                        it->tail_count++;
                    }
                    n_items++;
                }
                ctx->pos++;
                continue;
            }

            /* Regular variable */
            if (!(t->tok_flags & TOKF_CONSTANT))
            {
                const char *name =
                    (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;
                int si = add_sym(ctx, name);
                if (si < 0)
                {
                    return;
                }
                if (n_items < BPSE_MAX_ITEMS)
                {
                    items[n_items].is_dot = 0;
                    items[n_items].sym_idx = si;
                    items[n_items].is_compound = 0;
                    n_items++;
                }
            }
            ctx->pos++;
            continue;
        }

        BC_FAIL_UNSUP(ctx, BC_UNSUP_PARSE_TEMPLATE);
        return;
    }

    /* Flush remaining items with TR_END */
    if (n_items > 0)
    {
        bpse_flush(ctx, items, n_items, OP_TR_END, 0);
    }
}

/* Emit code to push ARG(n) onto the eval stack. */
static void bc_push_arg_n(struct bcom_ctx *ctx, int n)
{
    char nbuf[12];
    char tmp[12];
    int nlen = 0, i = 0, ci, si, nn = n;

    if (nn <= 0)
    {
        nbuf[nlen++] = '0';
    }
    else
    {
        while (nn > 0)
        {
            tmp[i++] = (char)('0' + (nn % 10));
            nn /= 10;
        }
        while (i > 0)
        {
            nbuf[nlen++] = tmp[--i];
        }
    }
    ci = add_const(ctx, nbuf, nlen);
    if (ci < 0)
    {
        return;
    }
    emit_byte(ctx, OP_PUSH_LIT);
    emit_u16(ctx, ci);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }
    si = add_sym(ctx, "ARG");
    if (si < 0)
    {
        return;
    }
    emit_byte(ctx, OP_CALL_BIF);
    emit_u16(ctx, si);
    emit_byte(ctx, 1);
}

/* Count comma-separated templates from the current position. */
static int bc_count_parse_templates(const struct bcom_ctx *ctx)
{
    int count = 1, i;
    for (i = ctx->pos; i < ctx->tok_count; i++)
    {
        const struct irx_token *t = &ctx->tokens[i];
        if (t->tok_type == TOK_EOC || t->tok_type == TOK_EOF ||
            tok_is_semi(t))
        {
            break;
        }
        if (t->tok_type == TOK_COMMA)
        {
            count++;
        }
    }
    return count;
}

static void bc_parse_stmt(struct bcom_ctx *ctx)
{
    int upper = 0, is_arg = 0, arg_idx = 1;
    unsigned char flags;

    ctx->pos++; /* consume PARSE */

    if (tok_kw(ctx, 0, "UPPER"))
    {
        upper = 1;
        ctx->pos++;
    }
    flags = upper ? 0x01 : 0x00;

    if (tok_kw(ctx, 0, "ARG"))
    {
        is_arg = 1;
        ctx->pos++;
    }
    else if (tok_kw(ctx, 0, "VAR"))
    {
        const struct irx_token *vt;
        const char *vname;
        int si, n, i;

        ctx->pos++;
        vt = tok_at(ctx, 0);
        if (vt == NULL || vt->tok_type != TOK_SYMBOL ||
            (vt->tok_flags & TOKF_CONSTANT))
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_PARSE_VAR);
            return;
        }
        vname = (vt->tok_upper != NULL) ? vt->tok_upper : vt->tok_text;
        si = add_sym(ctx, vname);
        if (si < 0)
        {
            return;
        }
        ctx->pos++;
        n = bc_count_parse_templates(ctx);
        emit_byte(ctx, OP_LOAD);
        emit_u16(ctx, si);
        for (i = 1; i < n; i++)
        {
            emit_byte(ctx, OP_DUP);
        }
    }
    else if (tok_kw(ctx, 0, "VALUE"))
    {
        int with_pos = -1, saved_count, n, i, depth = 0, j;

        ctx->pos++;
        for (j = ctx->pos; j < ctx->tok_count; j++)
        {
            const struct irx_token *t = &ctx->tokens[j];
            if (t->tok_type == TOK_EOC || t->tok_type == TOK_EOF ||
                tok_is_semi(t))
            {
                break;
            }
            if (t->tok_type == TOK_LPAREN)
            {
                depth++;
                continue;
            }
            if (t->tok_type == TOK_RPAREN && depth > 0)
            {
                depth--;
                continue;
            }
            if (depth == 0 && t->tok_type == TOK_SYMBOL &&
                t->tok_upper != NULL &&
                strcmp(t->tok_upper, "WITH") == 0)
            {
                with_pos = j;
                break;
            }
        }
        if (with_pos < 0)
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_PARSE_VALUE_WITH);
            return;
        }
        saved_count = ctx->tok_count;
        ctx->tok_count = with_pos;
        bc_exp0(ctx);
        ctx->tok_count = saved_count;
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        ctx->pos = with_pos + 1;
        n = bc_count_parse_templates(ctx);
        for (i = 1; i < n; i++)
        {
            emit_byte(ctx, OP_DUP);
        }
    }
    else if (tok_kw(ctx, 0, "SOURCE"))
    {
        int n, i;
        ctx->pos++;
        n = bc_count_parse_templates(ctx);
        emit_byte(ctx, OP_PUSH_SOURCE);
        for (i = 1; i < n; i++)
        {
            emit_byte(ctx, OP_DUP);
        }
    }
    else if (tok_kw(ctx, 0, "VERSION"))
    {
        const char *vs = BCOM_PARSE_VERSION;
        int n, i, ci;
        ctx->pos++;
        n = bc_count_parse_templates(ctx);
        ci = add_const(ctx, vs, (int)strlen(vs));
        if (ci < 0)
        {
            return;
        }
        emit_byte(ctx, OP_PUSH_LIT);
        emit_u16(ctx, ci);
        for (i = 1; i < n; i++)
        {
            emit_byte(ctx, OP_DUP);
        }
    }
    else if (tok_kw(ctx, 0, "NUMERIC"))
    {
        int n, i;
        ctx->pos++;
        n = bc_count_parse_templates(ctx);
        emit_byte(ctx, OP_PUSH_NUMERIC);
        for (i = 1; i < n; i++)
        {
            emit_byte(ctx, OP_DUP);
        }
    }
    else if (tok_kw(ctx, 0, "PULL"))
    {
        /* WP-33b: external data queue — compiled but unsupported at runtime. */
        int n, i;
        ctx->pos++;
        n = bc_count_parse_templates(ctx);
        emit_byte(ctx, OP_PULL_FROM_QUEUE);
        for (i = 1; i < n; i++)
        {
            emit_byte(ctx, OP_DUP);
        }
    }
    else
    {
        BC_FAIL_UNSUP(ctx, BC_UNSUP_PARSE_SOURCE);
        return;
    }

    for (;;)
    {
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        if (is_arg)
        {
            bc_push_arg_n(ctx, arg_idx++);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
        }
        emit_byte(ctx, OP_PARSE_BEGIN);
        emit_byte(ctx, flags);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        bc_parse_template(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_PARSE_END);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        if (tok_type_at(ctx, 0, TOK_COMMA))
        {
            ctx->pos++;
        }
        else
        {
            break;
        }
    }
}

/* ================================================================== */
/*  PROCEDURE [EXPOSE ...] statement compiler (WP-BC-05 PR B)         */
/* ================================================================== */

static void bc_procedure_stmt(struct bcom_ctx *ctx)
{
    int expose_patch;
    int nexposed = 0;

    ctx->pos++; /* consume PROCEDURE */

    emit_byte(ctx, OP_PROC);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }
    expose_patch = ctx->code_len;
    emit_byte(ctx, 0); /* nexposed placeholder — filled in below */
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    if (!tok_kw(ctx, 0, "EXPOSE"))
    {
        return; /* no EXPOSE clause — isolated scope, nexposed stays 0 */
    }
    ctx->pos++; /* consume EXPOSE */

    while (!tok_ends_clause(ctx))
    {
        const struct irx_token *t = tok_at(ctx, 0);
        if (t == NULL)
        {
            break;
        }

        if (t->tok_type == TOK_LPAREN)
        {
            /* (varname) — indirect expose */
            const char *iname;
            int si;
            ctx->pos++; /* consume ( */
            t = tok_at(ctx, 0);
            if (t == NULL || t->tok_type != TOK_SYMBOL ||
                (t->tok_flags & TOKF_CONSTANT))
            {
                BC_FAIL_UNSUP(ctx, BC_UNSUP_EXPOSE_INDIRECT);
                return;
            }
            iname = (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;
            si = add_sym(ctx, iname);
            if (si < 0)
            {
                return;
            }
            ctx->pos++; /* consume varname */
            t = tok_at(ctx, 0);
            if (t == NULL || t->tok_type != TOK_RPAREN)
            {
                BC_FAIL_UNSUP(ctx, BC_UNSUP_EXPOSE_INDIRECT);
                return;
            }
            ctx->pos++; /* consume ) */
            emit_byte(ctx, OP_EXPOSE_INDIRECT);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_u16(ctx, si);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            if (++nexposed > 255)
            {
                BC_FAIL_UNSUP(ctx, BC_UNSUP_EXPOSE_LIMIT);
                return;
            }
        }
        else if (t->tok_type == TOK_SYMBOL && !(t->tok_flags & TOKF_CONSTANT))
        {
            /* plain name or stem. */
            const char *name =
                (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;
            int si = add_sym(ctx, name);
            if (si < 0)
            {
                return;
            }
            emit_byte(ctx, OP_EXPOSE);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_u16(ctx, si);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            if (++nexposed > 255)
            {
                BC_FAIL_UNSUP(ctx, BC_UNSUP_EXPOSE_LIMIT);
                return;
            }
            ctx->pos++;
        }
        else
        {
            break;
        }
    }

    ctx->code[expose_patch] = (unsigned char)nexposed;
}

/* ================================================================== */
/*  Compound variable helpers (WP-BC-05 PR C)                         */
/* ================================================================== */

/* Emit bytecode to push each tail segment of a compound variable onto
 * the eval stack and register the stem in the symbol table.
 *
 * txt       — full uppercased compound name, e.g. "A.B.C"
 * tlen      — length of txt
 * dot_pos   — offset of the first '.' in txt
 * stem_si_out — receives the sym-table index for the stem (e.g. "A.")
 *
 * Returns the tail count (>= 0), or -1 if an error was recorded in ctx.
 * A bare stem like "A." (dot is the last char) returns 0 tails; the
 * caller then emits OP_LOAD_STEM / OP_STORE_STEM / OP_DROP_STEM with
 * tail_count = 0. */
static int bc_compound_tails(struct bcom_ctx *ctx,
                             const char *txt, int tlen, int dot_pos,
                             int *stem_si_out)
{
    char stem_buf[IRXBC_STR_MAX + 2];
    int stem_len = dot_pos + 1; /* includes trailing dot */
    int si;
    int i;
    int tail_count = 0;

    if (stem_len > IRXBC_STR_MAX)
    {
        ctx->rc = IRXBC_ERR_STRTOOLONG;
        return -1;
    }
    memcpy(stem_buf, txt, (size_t)stem_len);
    stem_buf[stem_len] = '\0';
    si = add_sym(ctx, stem_buf);
    if (si < 0)
    {
        return -1;
    }
    *stem_si_out = si;

    /* Emit each tail segment after the first dot. */
    i = stem_len;
    while (i < tlen)
    {
        int seg_start = i;
        int seg_len;
        char seg_buf[IRXBC_STR_MAX + 1];

        while (i < tlen && txt[i] != '.')
        {
            i++;
        }
        seg_len = i - seg_start;
        if (i < tlen) /* skip the dot separator */
        {
            i++;
        }

        if (tail_count == 255)
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_COMPOUND_TAIL_LIMIT);
            return -1;
        }

        if (seg_len == 0 || isdigit((unsigned char)txt[seg_start]))
        {
            /* Constant tail: push as a literal string. */
            int ci = add_const(ctx, txt + seg_start, seg_len);
            if (ci < 0)
            {
                return -1;
            }
            emit_byte(ctx, OP_PUSH_LIT);
            emit_u16(ctx, ci);
        }
        else
        {
            /* Variable tail: load the variable's value. */
            if (seg_len > IRXBC_STR_MAX)
            {
                ctx->rc = IRXBC_ERR_STRTOOLONG;
                return -1;
            }
            memcpy(seg_buf, txt + seg_start, (size_t)seg_len);
            seg_buf[seg_len] = '\0';
            si = add_sym(ctx, seg_buf);
            if (si < 0)
            {
                return -1;
            }
            emit_byte(ctx, OP_LOAD);
            emit_u16(ctx, si);
        }
        if (ctx->rc != IRXBC_OK)
        {
            return -1;
        }
        tail_count++;
    }
    return tail_count;
}

/* ================================================================== */
/*  Expression compiler (bc_exp0 .. bc_exp8)                          */
/* ================================================================== */

static void bc_exp8(struct bcom_ctx *ctx)
{
    const struct irx_token *t = tok_at(ctx, 0);

    if (t == NULL || ctx->rc != IRXBC_OK)
    {
        return;
    }

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
            BC_FAIL_UNSUP(ctx, BC_UNSUP_EXPR_PAREN);
            return;
        }
        ctx->pos++;
        return;
    }

    if (t->tok_type == TOK_STRING)
    {
        int ci = add_const(ctx, t->tok_text, (int)t->tok_length);
        if (ci < 0)
        {
            return;
        }
        ctx->pos++;
        emit_byte(ctx, OP_PUSH_LIT);
        emit_u16(ctx, ci);
        return;
    }

    if (t->tok_type == TOK_NUMBER)
    {
        int ci = add_const(ctx, t->tok_text, (int)t->tok_length);
        if (ci < 0)
        {
            return;
        }
        ctx->pos++;
        emit_byte(ctx, OP_PUSH_LIT);
        emit_u16(ctx, ci);
        return;
    }

    if (t->tok_type == TOK_SYMBOL)
    {
        if (t->tok_flags & TOKF_CONSTANT)
        {
            int ci = add_const(ctx, t->tok_text, (int)t->tok_length);
            if (ci < 0)
            {
                return;
            }
            ctx->pos++;
            emit_byte(ctx, OP_PUSH_LIT);
            emit_u16(ctx, ci);
        }
        else if (t->tok_flags & TOKF_COMPOUND)
        {
            /* Compound variable: A.B, A.B.C, A.1, A. etc. */
            const char *txt =
                (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;
            int tlen = (int)t->tok_length;
            int dot_pos;
            int stem_si;
            int tail_count;

            for (dot_pos = 0; dot_pos < tlen; dot_pos++)
            {
                if (txt[dot_pos] == '.')
                {
                    break;
                }
            }
            ctx->pos++;
            tail_count = bc_compound_tails(ctx, txt, tlen, dot_pos, &stem_si);
            if (tail_count < 0 || ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_LOAD_STEM);
            emit_u16(ctx, stem_si);
            emit_byte(ctx, (unsigned char)tail_count);
        }
        else
        {
            const struct irx_token *next = tok_at(ctx, 1);
            const char *name =
                (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;

            /* Function call: SYMBOL immediately followed by '(' */
            if (next != NULL && next->tok_type == TOK_LPAREN &&
                toks_adjacent_bc(t, next))
            {
                int si = add_sym(ctx, name);
                if (si < 0)
                {
                    return;
                }
                ctx->pos += 2; /* consume symbol + '(' */
                bc_funcall(ctx, si);
            }
            else
            {
                int si = add_sym(ctx, name);
                if (si < 0)
                {
                    return;
                }
                ctx->pos++;
                emit_byte(ctx, OP_LOAD);
                emit_u16(ctx, si);
            }
        }
        return;
    }

    BC_FAIL_UNSUP(ctx, BC_UNSUP_EXPR_OPERAND);
}

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
        bc_exp7(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_NEG);
        return;
    }

    if (t->tok_type == TOK_OPERATOR && tok_ch(ctx, 0) == '+')
    {
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
        bc_exp6(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_POW);
    }
}

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
            ctx->pos += 2;
            bc_exp6(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_MOD);
        }
        else if (tok_type_at(ctx, 0, TOK_OPERATOR) &&
                 tok_ch(ctx, 0) == '*' &&
                 !(tok_type_at(ctx, 1, TOK_OPERATOR) &&
                   tok_ch(ctx, 1) == '*'))
        {
            ctx->pos++;
            bc_exp6(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_MUL);
        }
        else if (tok_type_at(ctx, 0, TOK_OPERATOR) &&
                 tok_ch(ctx, 0) == '/' &&
                 !(tok_type_at(ctx, 1, TOK_OPERATOR) &&
                   tok_ch(ctx, 1) == '/'))
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

/* Instruction-level keywords that partition the grammar and must not
 * be consumed as values in blank-concatenation context. */
static const char *const bc_kw_barriers[] = {
    "THEN", "ELSE", "END", "WHEN", "OTHERWISE",
    "TO", "BY", "FOR", "WHILE", "UNTIL", "FOREVER",
    "IF", "DO", "SAY", "SELECT", "EXIT",
    "ITERATE", "LEAVE", "NOP",
    "CALL", "RETURN", "PROCEDURE", "PARSE", "DROP", "SIGNAL",
    "TRACE", "ADDRESS",
    NULL};

static int is_kw_barrier(const struct bcom_ctx *ctx, int offset)
{
    const struct irx_token *t = tok_at(ctx, offset);
    int i;
    if (t == NULL || t->tok_type != TOK_SYMBOL || t->tok_upper == NULL)
    {
        return 0;
    }
    for (i = 0; bc_kw_barriers[i] != NULL; i++)
    {
        if (strcmp(t->tok_upper, bc_kw_barriers[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int is_value_starter(const struct bcom_ctx *ctx, int offset)
{
    const struct irx_token *t = tok_at(ctx, offset);
    if (t == NULL)
    {
        return 0;
    }
    if (t->tok_type == TOK_SYMBOL && is_kw_barrier(ctx, offset))
    {
        return 0;
    }
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
        else if (is_value_starter(ctx, 0))
        {
            /* Implicit concatenation. Decide abuttal vs. blank by source
             * adjacency BEFORE bc_exp4 advances past the right operand,
             * mirroring the token-walk parse_concat (src/irx#pars.c): the
             * left operand's last token sits at pos-1 and the right
             * operand's first token at pos.  Adjacent in source (no
             * whitespace gap) is an abuttal -> OP_CONCAT (no blank); a gap
             * is a blank concatenation -> OP_BCONCAT.  Terms on different
             * source lines (reachable only across a line-spanning comment)
             * are not adjacent and keep the prior blank behavior. */
            const struct irx_token *lhs_last = tok_at(ctx, -1);
            const struct irx_token *rhs_first = tok_at(ctx, 0);
            int abuttal = toks_adjacent_bc(lhs_last, rhs_first);
            bc_exp4(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, abuttal ? OP_CONCAT : OP_BCONCAT);
        }
        else
        {
            break;
        }
    }
}

static void bc_exp2(struct bcom_ctx *ctx)
{
    unsigned char op = 0;

    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    bc_exp3(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    if (tok_type_at(ctx, 0, TOK_COMPARISON) && tok_ch(ctx, 0) == '=' &&
        tok_type_at(ctx, 1, TOK_COMPARISON) && tok_ch(ctx, 1) == '=')
    {
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
        op = OP_DLE;
        ctx->pos += 3;
    }
    else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
             tok_ch(ctx, 0) == '>' &&
             tok_type_at(ctx, 1, TOK_COMPARISON) &&
             tok_ch(ctx, 1) == '>')
    {
        op = OP_DGT;
        ctx->pos += 2;
    }
    else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
             tok_ch(ctx, 0) == '<' &&
             tok_type_at(ctx, 1, TOK_COMPARISON) &&
             tok_ch(ctx, 1) == '<')
    {
        op = OP_DLT;
        ctx->pos += 2;
    }
    else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
             tok_ch(ctx, 0) == '>' &&
             tok_type_at(ctx, 1, TOK_COMPARISON) &&
             tok_ch(ctx, 1) == '=')
    {
        op = OP_GE;
        ctx->pos += 2;
    }
    else if (tok_type_at(ctx, 0, TOK_COMPARISON) &&
             tok_ch(ctx, 0) == '<' &&
             tok_type_at(ctx, 1, TOK_COMPARISON) &&
             tok_ch(ctx, 1) == '=')
    {
        op = OP_LE;
        ctx->pos += 2;
    }
    else if (tok_type_at(ctx, 0, TOK_NOT) &&
             tok_type_at(ctx, 1, TOK_COMPARISON) &&
             tok_ch(ctx, 1) == '=' &&
             tok_type_at(ctx, 2, TOK_COMPARISON) &&
             tok_ch(ctx, 2) == '=')
    {
        op = OP_DNE;
        ctx->pos += 3;
    }
    else if (tok_type_at(ctx, 0, TOK_NOT) &&
             tok_type_at(ctx, 1, TOK_COMPARISON) &&
             tok_ch(ctx, 1) == '=')
    {
        op = OP_NE;
        ctx->pos += 2;
    }
    else if (tok_type_at(ctx, 0, TOK_NOT) &&
             tok_type_at(ctx, 1, TOK_COMPARISON) &&
             tok_ch(ctx, 1) == '>')
    {
        op = OP_LE;
        ctx->pos += 2;
    }
    else if (tok_type_at(ctx, 0, TOK_NOT) &&
             tok_type_at(ctx, 1, TOK_COMPARISON) &&
             tok_ch(ctx, 1) == '<')
    {
        op = OP_GE;
        ctx->pos += 2;
    }
    else if (tok_type_at(ctx, 0, TOK_COMPARISON) && tok_ch(ctx, 0) == '=')
    {
        op = OP_EQ;
        ctx->pos++;
    }
    else if (tok_type_at(ctx, 0, TOK_COMPARISON) && tok_ch(ctx, 0) == '>')
    {
        op = OP_GT;
        ctx->pos++;
    }
    else if (tok_type_at(ctx, 0, TOK_COMPARISON) && tok_ch(ctx, 0) == '<')
    {
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
                 !(tok_type_at(ctx, 1, TOK_LOGICAL) &&
                   tok_ch(ctx, 1) == '|'))
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

/* Compile a full expression in a general (non-argument) context.
 * Compiles the expression with bc_exp0, then folds any trailing
 * continuation-comma (a physical-line join, SC28-1883-0 §3.2) into a
 * blank concatenation with the following sub-expression — mirroring the
 * token-walk irx_pars_eval_expr (src/irx#pars.c).  A continuation-comma
 * immediately before a clause end or an instruction keyword is a bare
 * line join with nothing to concatenate: it is consumed and the fold
 * stops (matching the token-walk break conditions).
 *
 * Argument lists (CALL / function calls) deliberately do NOT use this:
 * there a comma — even a continuation-comma — stays an argument
 * separator, so bc_funcall / bc_call_stmt call bc_exp0 directly.  This
 * is the same split as the token-walk's parse_or vs irx_pars_eval_expr
 * (WP-BC-CONTCOMMA). */
static void bc_expr(struct bcom_ctx *ctx)
{
    bc_exp0(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    while (tok_is_cont_comma(ctx, 0))
    {
        ctx->pos++; /* consume the continuation comma */
        if (tok_ends_clause(ctx) || is_kw_barrier(ctx, 0))
        {
            break;
        }
        bc_exp0(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_BCONCAT);
    }
}

/* ================================================================== */
/*  SAY                                                               */
/* ================================================================== */

static void C_say_bc(struct bcom_ctx *ctx)
{
    ctx->pos++; /* consume SAY */
    if (tok_ends_clause(ctx))
    {
        emit_push_int(ctx, "");
    }
    else
    {
        bc_expr(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
    }
    emit_byte(ctx, OP_SAY);
}

/* ================================================================== */
/*  IF/THEN/ELSE                                                      */
/* ================================================================== */

static void C_if_bc(struct bcom_ctx *ctx)
{
    int jf_patch;
    int jmp_patch = -1;

    ctx->pos++; /* consume IF */

    bc_expr(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    if (!tok_kw(ctx, 0, "THEN"))
    {
        BC_FAIL_UNSUP(ctx, BC_UNSUP_IF_THEN);
        return;
    }
    ctx->pos++; /* consume THEN */
    consume_newline(ctx);

    jf_patch = emit_jmp_op(ctx, OP_JF);
    if (jf_patch < 0)
    {
        return;
    }

    bc_stmt(ctx);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }
    consume_eoc(ctx);

    if (tok_kw(ctx, 0, "ELSE"))
    {
        jmp_patch = emit_jmp_op(ctx, OP_JMP);
        if (jmp_patch < 0)
        {
            return;
        }
        patch_jmp_to_here(ctx, jf_patch);

        ctx->pos++; /* consume ELSE */
        consume_newline(ctx);

        bc_stmt(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        consume_eoc(ctx);

        patch_jmp_to_here(ctx, jmp_patch);
    }
    else
    {
        patch_jmp_to_here(ctx, jf_patch);
    }
}

/* ================================================================== */
/*  SELECT/WHEN/OTHERWISE                                             */
/* ================================================================== */

static void C_select_bc(struct bcom_ctx *ctx)
{
    struct bc_loop_ctx *sf;
    int jf_patch;

    ctx->pos++; /* consume SELECT */
    consume_eoc(ctx);
    skip_eoc(ctx);

    sf = loop_push(ctx, BCTL_SELECT);
    if (sf == NULL)
    {
        return;
    }

    for (;;)
    {
        if (ctx->rc != IRXBC_OK)
        {
            break;
        }
        skip_eoc(ctx);

        if (tok_kw(ctx, 0, "WHEN"))
        {
            int jmp;
            ctx->pos++; /* consume WHEN */

            bc_expr(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                break;
            }

            if (!tok_kw(ctx, 0, "THEN"))
            {
                BC_FAIL_UNSUP(ctx, BC_UNSUP_WHEN_THEN);
                break;
            }
            ctx->pos++; /* consume THEN */
            consume_newline(ctx);

            jf_patch = emit_jmp_op(ctx, OP_JF);
            if (jf_patch < 0)
            {
                break;
            }

            bc_stmt(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                break;
            }
            consume_eoc(ctx);

            jmp = emit_jmp_op(ctx, OP_JMP);
            if (jmp < 0)
            {
                break;
            }
            loop_add_leave_patch(ctx, sf, jmp);

            patch_jmp_to_here(ctx, jf_patch);
        }
        else if (tok_kw(ctx, 0, "OTHERWISE"))
        {
            ctx->pos++; /* consume OTHERWISE */
            consume_eoc(ctx);
            bc_stmts_until(ctx, "END", NULL, NULL);
            break;
        }
        else if (tok_kw(ctx, 0, "END"))
        {
            break;
        }
        else
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_SELECT_BODY);
            break;
        }
    }

    if (ctx->rc != IRXBC_OK)
    {
        loop_pop(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "END"))
    {
        ctx->pos++;
        if (tok_type_at(ctx, 0, TOK_SYMBOL) && !tok_ends_clause(ctx))
        {
            ctx->pos++;
        }
    }

    loop_patch_leaves(ctx, sf);
    loop_pop(ctx);
}

/* ================================================================== */
/*  DO loops                                                          */
/* ================================================================== */

/* Emit the DO TO condition check:
 *   (step > 0 AND var <= limit) OR (step <= 0 AND var >= limit)
 * Returns the JF patch position for the failing case. */
static int emit_do_to_cond(struct bcom_ctx *ctx, int si_var, int si_lim,
                           int si_stp)
{
    /* step > 0 AND var <= limit */
    emit_load(ctx, si_stp);
    emit_push_int(ctx, "0");
    emit_byte(ctx, OP_GT);
    emit_load(ctx, si_var);
    emit_load(ctx, si_lim);
    emit_byte(ctx, OP_LE);
    emit_byte(ctx, OP_AND);

    /* step <= 0 AND var >= limit  (covers step < 0 and step = 0) */
    emit_load(ctx, si_stp);
    emit_push_int(ctx, "0");
    emit_byte(ctx, OP_GT);
    emit_byte(ctx, OP_NOT);
    emit_load(ctx, si_var);
    emit_load(ctx, si_lim);
    emit_byte(ctx, OP_GE);
    emit_byte(ctx, OP_AND);

    emit_byte(ctx, OP_OR);
    return emit_jmp_op(ctx, OP_JF); /* jump to loop_end if false */
}

static void C_do_bc(struct bcom_ctx *ctx)
{
    struct bc_loop_ctx *lf;
    int loop_top;
    int loop_type = BCTL_DO_FOREVER;
    int cond_jf = -1;

    int si_var = -1;
    int si_lim = -1;
    int si_stp = -1;
    int si_for = -1;

    char sym_lim[24];
    char sym_stp[24];
    char sym_for[24];

    /* Token position of the UNTIL condition for DO UNTIL */
    int until_cond_tok = -1;

    int depth = ctx->loop_depth;

    make_do_sym(sym_lim, depth, "LIM");
    make_do_sym(sym_stp, depth, "STP");
    make_do_sym(sym_for, depth, "FOR");

    ctx->pos++; /* consume DO */

    /* ---- Parse DO header ------------------------------------------ */

    if (tok_kw(ctx, 0, "FOREVER"))
    {
        ctx->pos++; /* consume FOREVER */
        loop_type = BCTL_DO_FOREVER;
    }
    else if (tok_ends_clause(ctx))
    {
        loop_type = BCTL_DO_BLOCK; /* bare DO — simple group, execute once */
    }
    else if (tok_kw(ctx, 0, "WHILE"))
    {
        loop_type = BCTL_DO_WHILE;
        /* WHILE keyword consumed in the loop entry section below */
    }
    else if (tok_kw(ctx, 0, "UNTIL"))
    {
        loop_type = BCTL_DO_UNTIL;
        ctx->pos++;                /* consume UNTIL */
        until_cond_tok = ctx->pos; /* save condition start */
        /* Skip past condition tokens to the EOC */
        while (!tok_ends_clause(ctx))
        {
            ctx->pos++;
        }
        /* consume_eoc happens below */
    }
    else if (tok_type_at(ctx, 0, TOK_SYMBOL) &&
             !(tok_at(ctx, 0)->tok_flags & TOKF_CONSTANT) &&
             tok_type_at(ctx, 1, TOK_COMPARISON) &&
             tok_ch(ctx, 1) == '=' &&
             !tok_type_at(ctx, 2, TOK_COMPARISON))
    {
        /* DO var = start TO limit [BY step] [FOR count] */
        const struct irx_token *t = tok_at(ctx, 0);
        const char *vname =
            (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;

        loop_type = BCTL_DO_TO;

        si_var = add_sym(ctx, vname);
        si_lim = add_sym(ctx, sym_lim);
        si_stp = add_sym(ctx, sym_stp);
        if (si_var < 0 || si_lim < 0 || si_stp < 0)
        {
            return;
        }

        ctx->pos += 2; /* consume var and = */

        /* Compile start → store in var */
        bc_exp0(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_store(ctx, si_var);

        /* Expect TO */
        if (!tok_kw(ctx, 0, "TO"))
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_DO_CONTROL);
            return;
        }
        ctx->pos++; /* consume TO */

        /* Compile limit → store */
        bc_exp0(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_store(ctx, si_lim);

        /* Optional BY */
        if (tok_kw(ctx, 0, "BY"))
        {
            ctx->pos++;
            bc_exp0(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_store(ctx, si_stp);
        }
        else
        {
            emit_push_int(ctx, "1");
            emit_store(ctx, si_stp);
        }

        /* Optional FOR */
        if (tok_kw(ctx, 0, "FOR"))
        {
            ctx->pos++;
            si_for = add_sym(ctx, sym_for);
            if (si_for < 0)
            {
                return;
            }
            bc_exp0(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_store(ctx, si_for);
        }
    }
    else
    {
        /* DO count_expr — count stays on eval stack; FORINIT consumes it */
        loop_type = BCTL_DO_COUNT;
        bc_exp0(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
    }

    consume_eoc(ctx);

    /* ---- Push loop context ---------------------------------------- */
    lf = loop_push(ctx, loop_type);
    if (lf == NULL)
    {
        return;
    }

    /* ---- DO COUNT: FORINIT + entry guard (once, before loop_top) -- */
    if (loop_type == BCTL_DO_COUNT)
    {
        emit_byte(ctx, OP_FORINIT);
        emit_byte(ctx, (unsigned char)depth);
        cond_jf = emit_jmp_op(ctx, OP_JF);
        if (cond_jf < 0 || ctx->rc != IRXBC_OK)
        {
            loop_pop(ctx);
            return;
        }
    }

    /* ---- loop_top ------------------------------------------------- */
    loop_top = ctx->code_len;
    lf->loop_top = loop_top;
    emit_byte(ctx, OP_NEWCLAUSE);

    /* For WHILE and FOREVER, iterate = loop_top (known now). */
    if (loop_type == BCTL_DO_WHILE || loop_type == BCTL_DO_FOREVER)
    {
        lf->iterate_target = loop_top;
        lf->iterate_known = 1;
    }
    /* DO BLOCK has no loop — iterate and leave both go to loop_end. */
    /* iterate_known=0 until loop_set_iterate is called after body. */

    /* ---- Entry condition ----------------------------------------- */

    if (loop_type == BCTL_DO_WHILE)
    {
        ctx->pos++; /* consume WHILE */
        bc_exp0(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            loop_pop(ctx);
            return;
        }
        consume_eoc(ctx);
        cond_jf = emit_jmp_op(ctx, OP_JF);
        if (cond_jf < 0)
        {
            loop_pop(ctx);
            return;
        }
    }
    else if (loop_type == BCTL_DO_TO)
    {
        cond_jf = emit_do_to_cond(ctx, si_var, si_lim, si_stp);
        if (cond_jf < 0)
        {
            loop_pop(ctx);
            return;
        }
        /* FOR count: also check si_for > 0 */
        if (si_for >= 0)
        {
            /* Back-patch: the existing cond_jf is now wrong because we
             * need to AND with the FOR check.  Since this is rare and
             * complex to AND into an already-emitted JF, we re-structure:
             * if cond fails (already emitted), fall through to for check
             * → skip.  Simpler: after the current JF patch, test FOR. */
            /* Easier: just add FOR check inline before the cond. */
            /* TODO: wrap both checks.  For now, emit FOR check after:
             * cond_jf patches to loop_end.  We also add:
             *   if FOR > 0 is false → also exit. */
            int for_jf;
            emit_load(ctx, si_for);
            emit_push_int(ctx, "0");
            emit_byte(ctx, OP_GT);
            for_jf = emit_jmp_op(ctx, OP_JF);
            if (for_jf >= 0)
            {
                loop_add_leave_patch(ctx, lf, for_jf);
            }
        }
    }

    /* ---- Compile loop body ---------------------------------------- */
    bc_stmts_until(ctx, "END", NULL, NULL);
    if (ctx->rc != IRXBC_OK)
    {
        loop_pop(ctx);
        return;
    }

    /* ---- Iterate section ----------------------------------------- */

    if (loop_type == BCTL_DO_COUNT)
    {
        int decfor_pos;
        loop_set_iterate(ctx, lf, ctx->code_len);
        /* DECFOR: dec frame[depth]; jump to loop_end when exhausted */
        decfor_pos = ctx->code_len;
        emit_byte(ctx, OP_DECFOR);
        emit_byte(ctx, (unsigned char)depth);
        emit_i16(ctx, 0); /* placeholder — patched after backward JMP */
        emit_jmp_back(ctx, OP_JMP, loop_top);
        /* ctx->code_len is now loop_end; patch DECFOR i16 */
        if (ctx->rc == IRXBC_OK)
        {
            int off = ctx->code_len - (decfor_pos + 4);
            unsigned int uv = (unsigned int)(short)(off);
            ctx->code[decfor_pos + 2] = (unsigned char)(uv & 0xFF);
            ctx->code[decfor_pos + 3] = (unsigned char)((uv >> 8) & 0xFF);
        }
    }
    else if (loop_type == BCTL_DO_TO)
    {
        loop_set_iterate(ctx, lf, ctx->code_len);
        /* var = var + step */
        emit_load(ctx, si_var);
        emit_load(ctx, si_stp);
        emit_byte(ctx, OP_ADD);
        emit_store(ctx, si_var);
        /* FOR count decrement */
        if (si_for >= 0)
        {
            emit_load(ctx, si_for);
            emit_push_int(ctx, "1");
            emit_byte(ctx, OP_SUB);
            emit_store(ctx, si_for);
        }
    }
    else if (loop_type == BCTL_DO_UNTIL)
    {
        /* iterate_target = here (before condition re-evaluation) */
        loop_set_iterate(ctx, lf, ctx->code_len);

        /* Re-compile condition using saved token position */
        {
            int saved_pos = ctx->pos;
            ctx->pos = until_cond_tok;
            bc_exp0(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                loop_pop(ctx);
                return;
            }
            ctx->pos = saved_pos;
        }

        /* JT to loop_end: exit if condition is now true */
        {
            int jt = emit_jmp_op(ctx, OP_JT);
            if (jt >= 0)
            {
                loop_add_leave_patch(ctx, lf, jt);
            }
        }
    }

    /* ---- JMP back to loop_top (omitted for DO BLOCK and DO COUNT) -- */
    if (loop_type == BCTL_DO_BLOCK)
    {
        /* Simple group: no backward jump.  ITERATE and LEAVE both
         * resolve to loop_end (the fall-through point). */
        loop_set_iterate(ctx, lf, ctx->code_len);
    }
    else if (loop_type != BCTL_DO_COUNT)
    {
        /* DO COUNT already emitted DECFOR + JMP back in the iterate section */
        emit_jmp_back(ctx, OP_JMP, loop_top);
    }

    /* ---- loop_end ------------------------------------------------ */

    if (cond_jf >= 0)
    {
        patch_jmp_to_here(ctx, cond_jf);
    }
    loop_patch_leaves(ctx, lf);
    loop_pop(ctx);

    /* Consume END keyword (and optional loop-var name after END) */
    if (tok_kw(ctx, 0, "END"))
    {
        ctx->pos++;
        if (tok_type_at(ctx, 0, TOK_SYMBOL) && !tok_ends_clause(ctx))
        {
            ctx->pos++;
        }
    }
}

/* ================================================================== */
/*  ITERATE / LEAVE                                                   */
/* ================================================================== */

static void C_iterate_bc(struct bcom_ctx *ctx)
{
    char label[BCOM_MAX_LABEL];
    struct bc_loop_ctx *lf;

    ctx->pos++; /* consume ITERATE */

    label[0] = '\0';
    if (tok_type_at(ctx, 0, TOK_SYMBOL) && !tok_ends_clause(ctx))
    {
        const struct irx_token *t = tok_at(ctx, 0);
        const char *up = (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;
        int n = (int)strlen(up);
        if (n >= BCOM_MAX_LABEL)
        {
            n = BCOM_MAX_LABEL - 1;
        }
        memcpy(label, up, (size_t)n);
        label[n] = '\0';
        ctx->pos++;
    }

    lf = loop_find(ctx, label[0] ? label : NULL);
    if (lf == NULL)
    {
        BC_FAIL_UNSUP(ctx, BC_UNSUP_ITERATE_TARGET);
        return;
    }

    if (lf->iterate_known)
    {
        /* Backward jump to known iterate_target */
        emit_jmp_back(ctx, OP_ITERATE, lf->iterate_target);
    }
    else
    {
        /* Forward jump; will be patched when iterate_target is set */
        int p = emit_jmp_op(ctx, OP_ITERATE);
        if (p >= 0)
        {
            loop_add_iterate_patch(ctx, lf, p);
        }
    }
}

static void C_leave_bc(struct bcom_ctx *ctx)
{
    char label[BCOM_MAX_LABEL];
    struct bc_loop_ctx *lf;
    int jmp;

    ctx->pos++; /* consume LEAVE */

    label[0] = '\0';
    if (tok_type_at(ctx, 0, TOK_SYMBOL) && !tok_ends_clause(ctx))
    {
        const struct irx_token *t = tok_at(ctx, 0);
        const char *up = (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;
        int n = (int)strlen(up);
        if (n >= BCOM_MAX_LABEL)
        {
            n = BCOM_MAX_LABEL - 1;
        }
        memcpy(label, up, (size_t)n);
        label[n] = '\0';
        ctx->pos++;
    }

    lf = loop_find(ctx, label[0] ? label : NULL);
    if (lf == NULL)
    {
        /* Fall back to innermost SELECT */
        lf = select_frame(ctx);
        if (lf == NULL)
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_LEAVE_TARGET);
            return;
        }
    }

    jmp = emit_jmp_op(ctx, OP_LEAVE);
    if (jmp >= 0)
    {
        loop_add_leave_patch(ctx, lf, jmp);
    }
}

/* ================================================================== */
/*  SIGNAL statement (WP-BC-07 PR A)                                  */
/* ================================================================== */

/* ================================================================== */
/*  bc_trace_stmt — TRACE statement compiler (WP-BC-08)               */
/*                                                                    */
/*  Forms:                                                            */
/*    TRACE             → OP_TRACE_TOGGLE                             */
/*    TRACE VALUE expr  → bc_exp0 + OP_TRACE_VALUE                   */
/*    TRACE number      → consume, no-op (skip-form)                  */
/*    TRACE option      → OP_TRACE_SET mode:u8                        */
/* ================================================================== */

static void bc_trace_stmt(struct bcom_ctx *ctx)
{
    const struct irx_token *t;
    static const char allowed[] = "NAILRCFEO";

    ctx->pos++; /* consume TRACE */

    /* Bare TRACE — toggle wkbi_interactive, keep wkbi_trace letter. */
    if (tok_ends_clause(ctx))
    {
        emit_byte(ctx, OP_TRACE_TOGGLE);
        return;
    }

    /* TRACE VALUE expr */
    if (tok_kw(ctx, 0, "VALUE"))
    {
        ctx->pos++; /* consume VALUE */
        if (tok_ends_clause(ctx))
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_TRACE_VALUE);
            return;
        }
        bc_expr(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_TRACE_VALUE);
        return;
    }

    t = tok_at(ctx, 0);

    /* TRACE number — skip-form: consume, no-op. */
    if (t != NULL && t->tok_type == TOK_NUMBER)
    {
        ctx->pos++;
        return;
    }
    if (t != NULL && t->tok_type == TOK_OPERATOR &&
        (tok_ch(ctx, 0) == '+' || tok_ch(ctx, 0) == '-') &&
        tok_type_at(ctx, 1, TOK_NUMBER))
    {
        ctx->pos += 2; /* consume sign + number */
        return;
    }

    /* TRACE option — symbol or string literal: constant mode. */
    if (t != NULL && (t->tok_type == TOK_SYMBOL || t->tok_type == TOK_STRING))
    {
        const char *text = (t->tok_upper != NULL) ? t->tok_upper
                                                  : (const char *)t->tok_text;
        int n = (int)t->tok_length;
        int idx = 0;
        int toggle = 0;
        char c;
        unsigned char mode;

        if (idx < n && text[idx] == '?')
        {
            toggle = 1;
            idx++;
        }
        if (idx >= n)
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_TRACE_SETTING);
            return;
        }
        c = (char)toupper((unsigned char)text[idx]);
        {
            const char *p = strchr(allowed, c);
            if (p == NULL)
            {
                BC_FAIL_UNSUP(ctx, BC_UNSUP_TRACE_SETTING);
                return;
            }
            /* Encode as letter-index (0-8) in bits 0-3, interactive in bit 4.
             * Storing the raw character value is wrong on EBCDIC: trace letters
             * have bit 7 set (e.g. 'O'=0xD6), which collides with any flag in
             * that bit.  An index into "NAILRCFEO" is platform-neutral. */
            mode = (unsigned char)((int)(p - allowed) | (toggle ? 0x10 : 0x00));
        }
        ctx->pos++;
        emit_byte(ctx, OP_TRACE_SET);
        emit_byte(ctx, mode);
        return;
    }

    BC_FAIL_UNSUP(ctx, BC_UNSUP_TRACE_FORM);
}

/* ================================================================== */
/*  bc_address_stmt — ADDRESS statement compiler (WP-BC-08)           */
/*                                                                    */
/*  Forms:                                                            */
/*    ADDRESS             → OP_ADDRESS_TOGGLE                         */
/*    ADDRESS VALUE expr  → bc_exp0 + OP_ADDRESS_VALUE                */
/*    ADDRESS env         → OP_ADDRESS_SET sym_idx:u16                */
/*    ADDRESS env cmd     → consume clause, no-op (WP-33 stub)        */
/* ================================================================== */

static void bc_address_stmt(struct bcom_ctx *ctx)
{
    const struct irx_token *t;

    ctx->pos++; /* consume ADDRESS */

    /* Bare ADDRESS — toggle between current and previous environment. */
    if (tok_ends_clause(ctx))
    {
        emit_byte(ctx, OP_ADDRESS_TOGGLE);
        return;
    }

    /* ADDRESS VALUE expr */
    if (tok_kw(ctx, 0, "VALUE"))
    {
        ctx->pos++; /* consume VALUE */
        if (tok_ends_clause(ctx))
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_ADDRESS_VALUE);
            return;
        }
        bc_expr(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_ADDRESS_VALUE);
        return;
    }

    t = tok_at(ctx, 0);

    /* ADDRESS env — symbol or string token. */
    if (t != NULL && (t->tok_type == TOK_SYMBOL || t->tok_type == TOK_STRING))
    {
        const struct irx_token *tnext = tok_at(ctx, 1);

        /* One-shot form: ADDRESS env command — consume clause, no write.
         * TODO(WP-33): route command to host environment.  A bare ';'
         * after the env name ends the clause, so it is the set form. */
        if (tnext != NULL && tnext->tok_type != TOK_EOC &&
            tnext->tok_type != TOK_EOF && !tok_is_semi(tnext))
        {
            while (!tok_ends_clause(ctx))
            {
                ctx->pos++;
            }
            return;
        }

        /* String form: write env-name to wkbi_address.
         *
         * For symbols, tok_upper is null-terminated → safe for add_sym.
         * For string literals, tok_text is a pointer into the source
         * buffer (not null-terminated).  Copy to a local buffer first. */
        {
            const char *name;
            char name_buf[IRXBC_STR_MAX + 1];
            int si;

            if (t->tok_type == TOK_SYMBOL && t->tok_upper != NULL)
            {
                name = t->tok_upper;
            }
            else
            {
                int tlen = (int)t->tok_length;
                if (tlen > IRXBC_STR_MAX)
                {
                    tlen = IRXBC_STR_MAX;
                }
                memcpy(name_buf, t->tok_text, (size_t)tlen);
                name_buf[tlen] = '\0';
                name = name_buf;
            }

            si = add_sym(ctx, name);
            if (si < 0)
            {
                return;
            }
            ctx->pos++;
            emit_byte(ctx, OP_ADDRESS_SET);
            emit_u16(ctx, si);
        }
        return;
    }

    BC_FAIL_UNSUP(ctx, BC_UNSUP_ADDRESS_FORM);
}

static void bc_signal_stmt(struct bcom_ctx *ctx)
{
    const struct irx_token *t;

    ctx->pos++; /* consume SIGNAL */

    /* SIGNAL ON condition [NAME label] (WP-BC-07 PR B) */
    if (tok_kw(ctx, 0, "ON"))
    {
        unsigned char cond;
        const char *cond_name;
        int lsi;

        ctx->pos++; /* consume ON */

        if (tok_kw(ctx, 0, "ERROR"))
        {
            cond = COND_ERROR;
            cond_name = "ERROR";
        }
        else if (tok_kw(ctx, 0, "FAILURE"))
        {
            cond = COND_FAILURE;
            cond_name = "FAILURE";
        }
        else if (tok_kw(ctx, 0, "HALT"))
        {
            cond = COND_HALT;
            cond_name = "HALT";
        }
        else if (tok_kw(ctx, 0, "NOVALUE"))
        {
            cond = COND_NOVALUE;
            cond_name = "NOVALUE";
        }
        else if (tok_kw(ctx, 0, "NOTREADY"))
        {
            cond = COND_NOTREADY;
            cond_name = "NOTREADY";
        }
        else if (tok_kw(ctx, 0, "SYNTAX"))
        {
            cond = COND_SYNTAX;
            cond_name = "SYNTAX";
        }
        else
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_SIGNAL_CONDITION);
            return;
        }
        ctx->pos++; /* consume condition name */

        if (tok_kw(ctx, 0, "NAME"))
        {
            const struct irx_token *nt;
            const char *lname;
            ctx->pos++; /* consume NAME */
            nt = tok_at(ctx, 0);
            if (nt == NULL || nt->tok_type != TOK_SYMBOL)
            {
                BC_FAIL_UNSUP(ctx, BC_UNSUP_SIGNAL_NAME);
                return;
            }
            lname = (nt->tok_upper != NULL) ? nt->tok_upper : nt->tok_text;
            lsi = add_sym(ctx, lname);
            if (lsi < 0)
            {
                return;
            }
            ctx->pos++; /* consume label name */
        }
        else
        {
            /* Default: handler label = condition name (uppercase) */
            lsi = add_sym(ctx, cond_name);
            if (lsi < 0)
            {
                return;
            }
        }

        emit_byte(ctx, OP_SIGNAL_ON);
        emit_byte(ctx, cond);
        emit_u16(ctx, lsi);
        return;
    }

    /* SIGNAL OFF condition (WP-BC-07 PR B) */
    if (tok_kw(ctx, 0, "OFF"))
    {
        unsigned char cond;

        ctx->pos++; /* consume OFF */

        if (tok_kw(ctx, 0, "ERROR"))
        {
            cond = COND_ERROR;
        }
        else if (tok_kw(ctx, 0, "FAILURE"))
        {
            cond = COND_FAILURE;
        }
        else if (tok_kw(ctx, 0, "HALT"))
        {
            cond = COND_HALT;
        }
        else if (tok_kw(ctx, 0, "NOVALUE"))
        {
            cond = COND_NOVALUE;
        }
        else if (tok_kw(ctx, 0, "NOTREADY"))
        {
            cond = COND_NOTREADY;
        }
        else if (tok_kw(ctx, 0, "SYNTAX"))
        {
            cond = COND_SYNTAX;
        }
        else
        {
            BC_FAIL_UNSUP(ctx, BC_UNSUP_SIGNAL_CONDITION);
            return;
        }
        ctx->pos++; /* consume condition name */

        emit_byte(ctx, OP_SIGNAL_OFF);
        emit_byte(ctx, cond);
        return;
    }

    /* SIGNAL VALUE expr — evaluate expression, jump to named label */
    if (tok_kw(ctx, 0, "VALUE"))
    {
        ctx->pos++; /* consume VALUE */
        bc_expr(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        emit_byte(ctx, OP_SIGNAL_VALUE);
        return;
    }

    /* SIGNAL label — compile-time-known target */
    t = tok_at(ctx, 0);
    if (t == NULL || t->tok_type != TOK_SYMBOL)
    {
        BC_FAIL_UNSUP(ctx, BC_UNSUP_SIGNAL_TARGET);
        return;
    }
    {
        const char *name = (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;
        int si = add_sym(ctx, name);
        if (si < 0)
        {
            return;
        }
        ctx->pos++;
        emit_byte(ctx, OP_SIGNAL);
        emit_u16(ctx, si);
    }
}

/* ================================================================== */
/*  bc_stmts_until                                                    */
/* ================================================================== */

static void bc_stmts_until(struct bcom_ctx *ctx, const char *stop1,
                           const char *stop2, const char *stop3)
{
    for (;;)
    {
        const struct irx_token *t;

        if (ctx->rc != IRXBC_OK)
        {
            return;
        }

        skip_eoc(ctx);

        t = tok_at(ctx, 0);
        if (t == NULL || t->tok_type == TOK_EOF)
        {
            return;
        }

        if (stop1 != NULL && tok_kw(ctx, 0, stop1))
        {
            return;
        }
        if (stop2 != NULL && tok_kw(ctx, 0, stop2))
        {
            return;
        }
        if (stop3 != NULL && tok_kw(ctx, 0, stop3))
        {
            return;
        }

        bc_stmt(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }
        consume_eoc(ctx);
    }
}

/* ================================================================== */
/*  bc_stmt                                                           */
/* ================================================================== */

static void bc_stmt(struct bcom_ctx *ctx)
{
    const struct irx_token *t0 = tok_at(ctx, 0);
    const struct irx_token *t1 = tok_at(ctx, 1);

    if (t0 == NULL || t0->tok_type == TOK_EOC || t0->tok_type == TOK_EOF ||
        tok_is_semi(t0))
    {
        return;
    }

    emit_byte(ctx, OP_NEWCLAUSE);
    if (ctx->rc != IRXBC_OK)
    {
        return;
    }

    if (tok_kw(ctx, 0, "EXIT"))
    {
        const struct irx_token *tn;
        ctx->pos++;
        tn = tok_at(ctx, 0);
        if (tn == NULL || tn->tok_type == TOK_EOC || tn->tok_type == TOK_EOF ||
            tok_is_semi(tn))
        {
            emit_byte(ctx, OP_EXIT);
        }
        else
        {
            bc_expr(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_EXIT_RC);
        }
        ctx->hit_exit = 1;
        return;
    }

    if (tok_kw(ctx, 0, "SAY"))
    {
        C_say_bc(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "IF"))
    {
        C_if_bc(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "SELECT"))
    {
        C_select_bc(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "DO"))
    {
        C_do_bc(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "ITERATE"))
    {
        C_iterate_bc(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "LEAVE"))
    {
        C_leave_bc(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "NOP"))
    {
        ctx->pos++;
        return;
    }

    if (tok_kw(ctx, 0, "CALL"))
    {
        bc_call_stmt(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "RETURN"))
    {
        bc_return_stmt(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "PARSE"))
    {
        bc_parse_stmt(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "ARG"))
    {
        /* ARG template [, template ...] ≡ PARSE UPPER ARG template [...] */
        int arg_idx = 1;
        ctx->pos++; /* consume ARG */
        for (;;)
        {
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            bc_push_arg_n(ctx, arg_idx++);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_PARSE_BEGIN);
            emit_byte(ctx, 0x01); /* UPPER flag */
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            bc_parse_template(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_PARSE_END);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            if (tok_type_at(ctx, 0, TOK_COMMA))
            {
                ctx->pos++;
            }
            else
            {
                break;
            }
        }
        return;
    }

    if (tok_kw(ctx, 0, "PROCEDURE"))
    {
        bc_procedure_stmt(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "SIGNAL"))
    {
        bc_signal_stmt(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "TRACE"))
    {
        bc_trace_stmt(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "ADDRESS"))
    {
        bc_address_stmt(ctx);
        return;
    }

    if (tok_kw(ctx, 0, "DROP"))
    {
        ctx->pos++; /* consume DROP */
        for (;;)
        {
            const struct irx_token *td = tok_at(ctx, 0);
            if (td == NULL || td->tok_type == TOK_EOC ||
                td->tok_type == TOK_EOF || tok_is_semi(td))
            {
                break;
            }
            if (td->tok_type != TOK_SYMBOL ||
                (td->tok_flags & TOKF_CONSTANT))
            {
                BC_FAIL_UNSUP(ctx, BC_UNSUP_DROP_TARGET);
                return;
            }
            if (td->tok_flags & TOKF_COMPOUND)
            {
                const char *txt =
                    (td->tok_upper != NULL) ? td->tok_upper : td->tok_text;
                int tlen = (int)td->tok_length;
                int dot_pos;
                int stem_si;
                int tail_count;

                for (dot_pos = 0; dot_pos < tlen; dot_pos++)
                {
                    if (txt[dot_pos] == '.')
                    {
                        break;
                    }
                }
                tail_count =
                    bc_compound_tails(ctx, txt, tlen, dot_pos, &stem_si);
                if (tail_count < 0 || ctx->rc != IRXBC_OK)
                {
                    return;
                }
                ctx->pos++;
                emit_byte(ctx, OP_DROP_STEM);
                emit_u16(ctx, stem_si);
                emit_byte(ctx, (unsigned char)tail_count);
            }
            else
            {
                const char *name =
                    (td->tok_upper != NULL) ? td->tok_upper : td->tok_text;
                int si = add_sym(ctx, name);
                if (si < 0)
                {
                    return;
                }
                ctx->pos++;
                emit_byte(ctx, OP_DROP);
                emit_u16(ctx, si);
            }
        }
        return;
    }

    /* Assignment: symbol = expr (simple or compound LHS) */
    if (t0->tok_type == TOK_SYMBOL && !(t0->tok_flags & TOKF_CONSTANT) &&
        t1 != NULL && t1->tok_type == TOK_COMPARISON &&
        t1->tok_length > 0 && t1->tok_text[0] == '=' &&
        !(tok_type_at(ctx, 2, TOK_COMPARISON) && tok_ch(ctx, 2) == '='))
    {
        if (t0->tok_flags & TOKF_COMPOUND)
        {
            const char *txt =
                (t0->tok_upper != NULL) ? t0->tok_upper : t0->tok_text;
            int tlen = (int)t0->tok_length;
            int dot_pos;
            int stem_si;
            int tail_count;

            for (dot_pos = 0; dot_pos < tlen; dot_pos++)
            {
                if (txt[dot_pos] == '.')
                {
                    break;
                }
            }
            /* Push tails BEFORE consuming '=' so positions are correct. */
            tail_count =
                bc_compound_tails(ctx, txt, tlen, dot_pos, &stem_si);
            if (tail_count < 0 || ctx->rc != IRXBC_OK)
            {
                return;
            }
            ctx->pos += 2; /* consume compound-symbol + '=' */
            bc_expr(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_byte(ctx, OP_STORE_STEM);
            emit_u16(ctx, stem_si);
            emit_byte(ctx, (unsigned char)tail_count);
        }
        else
        {
            const char *name =
                (t0->tok_upper != NULL) ? t0->tok_upper : t0->tok_text;
            int si = add_sym(ctx, name);
            if (si < 0)
            {
                return;
            }
            ctx->pos += 2;
            bc_expr(ctx);
            if (ctx->rc != IRXBC_OK)
            {
                return;
            }
            emit_store(ctx, si);
        }
        return;
    }

    BC_FAIL_UNSUP(ctx, BC_UNSUP_STATEMENT);
}

/* ================================================================== */
/*  bc_program                                                        */
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
        if (t->tok_type == TOK_EOC || tok_is_semi(t))
        {
            ctx->pos++;
            continue;
        }

        /* Detect label: SYMBOL followed by SEMICOLON (tokenizer maps ':').
         * tok_is_semi() above matched only ';', so a ':' still reaches
         * here and the label form is recognised unchanged. */
        if (t->tok_type == TOK_SYMBOL &&
            tok_type_at(ctx, 1, TOK_SEMICOLON) &&
            tok_ch(ctx, 1) == ':')
        {
            const char *lname =
                (t->tok_upper != NULL) ? t->tok_upper : t->tok_text;
            int lsi = add_sym(ctx, lname);
            if (lsi < 0)
            {
                return;
            }
            emit_byte(ctx, OP_LABEL);
            emit_u16(ctx, lsi);
            ctx->pos += 2;
            continue;
        }

        bc_stmt(ctx);
        if (ctx->rc != IRXBC_OK)
        {
            return;
        }

        t = tok_at(ctx, 0);
        if (t != NULL && (t->tok_type == TOK_EOC || tok_is_semi(t)))
        {
            ctx->pos++;
        }
    }

    emit_byte(ctx, OP_EXIT);
}

/* ================================================================== */
/*  UNSUP reason → text (WP-BC-DIAG)                                  */
/*                                                                    */
/*  Central, compact lookup used only by the REXX370_BCDEBUG          */
/*  diagnostic.  Designated initialisers key each string to its enum  */
/*  value, so the mapping cannot drift if the enum is reordered; any  */
/*  gap falls through to "unknown" in irx_bc_unsup_text().            */
/* ================================================================== */

static const char *const bc_unsup_text[BC_UNSUP_COUNT] = {
    [BC_UNSUP_NONE] = "(none)",
    [BC_UNSUP_INTERNAL] = "internal/API guard",
    [BC_UNSUP_STATEMENT] = "unsupported statement",
    [BC_UNSUP_EXPR_OPERAND] = "unsupported expression operand",
    [BC_UNSUP_EXPR_PAREN] = "')' expected in expression",
    [BC_UNSUP_TOO_MANY_ARGS] = "too many call arguments",
    [BC_UNSUP_FUNC_ARG_SEP] = "malformed function argument list",
    [BC_UNSUP_CALL_TARGET] = "CALL target is not a label",
    [BC_UNSUP_CALL_ARG_SEP] = "malformed CALL argument list",
    [BC_UNSUP_PARSE_INDIRECT] = "PARSE indirect pattern (var)",
    [BC_UNSUP_PARSE_RELPOS] = "PARSE relative position (+n/-n)",
    [BC_UNSUP_PARSE_ABSPOS] = "PARSE absolute position (=n)",
    [BC_UNSUP_PARSE_TOO_MANY_TAILS] = "PARSE template tail limit",
    [BC_UNSUP_PARSE_TEMPLATE] = "unsupported PARSE template item",
    [BC_UNSUP_PARSE_VAR] = "PARSE VAR target is not a symbol",
    [BC_UNSUP_PARSE_VALUE_WITH] = "PARSE VALUE without WITH",
    [BC_UNSUP_PARSE_SOURCE] = "unsupported PARSE source",
    [BC_UNSUP_EXPOSE_INDIRECT] = "PROCEDURE EXPOSE indirect (var)",
    [BC_UNSUP_EXPOSE_LIMIT] = "PROCEDURE EXPOSE count limit",
    [BC_UNSUP_COMPOUND_TAIL_LIMIT] = "compound variable tail limit",
    [BC_UNSUP_IF_THEN] = "IF without THEN",
    [BC_UNSUP_WHEN_THEN] = "WHEN without THEN",
    [BC_UNSUP_SELECT_BODY] = "unexpected token in SELECT",
    [BC_UNSUP_DO_CONTROL] = "unsupported controlled DO",
    [BC_UNSUP_ITERATE_TARGET] = "ITERATE has no matching loop",
    [BC_UNSUP_LEAVE_TARGET] = "LEAVE has no matching loop/SELECT",
    [BC_UNSUP_TRACE_VALUE] = "TRACE VALUE with empty expression",
    [BC_UNSUP_TRACE_SETTING] = "invalid TRACE setting",
    [BC_UNSUP_TRACE_FORM] = "unsupported TRACE form",
    [BC_UNSUP_ADDRESS_VALUE] = "ADDRESS VALUE with empty expression",
    [BC_UNSUP_ADDRESS_FORM] = "unsupported ADDRESS form",
    [BC_UNSUP_SIGNAL_CONDITION] = "unsupported SIGNAL condition",
    [BC_UNSUP_SIGNAL_NAME] = "SIGNAL ... NAME is not a symbol",
    [BC_UNSUP_SIGNAL_TARGET] = "SIGNAL label is not a symbol",
    [BC_UNSUP_DROP_TARGET] = "DROP target is not a symbol",
};

const char *irx_bc_unsup_text(int reason)
{
    if (reason < 0 || reason >= BC_UNSUP_COUNT ||
        bc_unsup_text[reason] == NULL)
    {
        return "unknown";
    }
    return bc_unsup_text[reason];
}

/* ================================================================== */
/*  irx_bc_compile                                                    */
/* ================================================================== */

int irx_bc_compile(struct envblock *envblock,
                   const char *source, int source_len,
                   struct irx_bc_execblk **bc_out,
                   int *unsup_reason_out,
                   int *unsup_line_out)
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

    /* Default the diagnostic out-params; only an UNSUP overwrites them. */
    if (unsup_reason_out != NULL)
    {
        *unsup_reason_out = BC_UNSUP_NONE;
    }
    if (unsup_line_out != NULL)
    {
        *unsup_line_out = 0;
    }

    if (bc_out == NULL)
    {
        if (unsup_reason_out != NULL)
        {
            *unsup_reason_out = BC_UNSUP_INTERNAL;
        }
        return IRXBC_ERR_UNSUP;
    }
    *bc_out = NULL;

    rc = irx_tokn_run(envblock, source, source_len,
                      &tokens, &tok_count, &tok_err);
    if (rc != 0)
    {
        return IRXBC_ERR_TOKN;
    }

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

    bc_program(ctx);
    rc = ctx->rc;
    if (rc == IRXBC_ERR_UNSUP)
    {
        /* Capture the reason/line before cleanup frees ctx. */
        if (unsup_reason_out != NULL)
        {
            *unsup_reason_out = ctx->unsup_reason;
        }
        if (unsup_line_out != NULL)
        {
            *unsup_line_out = ctx->unsup_line;
        }
    }
    if (rc != IRXBC_OK)
    {
        goto cleanup;
    }

    total = (int)sizeof(struct irx_bc_execblk) +
            ctx->const_count * IRXBC_ENTRY_SIZE +
            ctx->sym_count * IRXBC_ENTRY_SIZE +
            ctx->code_len;

    if (irxstor(RXSMGET, total, &bc_mem, envblock) != 0)
    {
        rc = IRXBC_ERR_STOR;
        goto cleanup;
    }

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
    bc_mem = NULL;

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
