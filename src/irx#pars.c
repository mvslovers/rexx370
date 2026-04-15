/* ------------------------------------------------------------------ */
/*  irxpars.c - REXX/370 Parser + Expression Evaluator                */
/*                                                                    */
/*  Direct-interpretation parser. Walks the token stream from WP-10   */
/*  clause by clause, dispatches assignments / keyword instructions / */
/*  labels / commands, and evaluates expressions using recursive      */
/*  descent with one function per REXX precedence level.              */
/*                                                                    */
/*  All state in struct irx_parser on the caller's stack. Keyword    */
/*  and BIF tables are const. No statics with state, no globals.      */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 7 (Expressions)                         */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxctrl.h"
#include "irxlstr.h"
#include "irxpars.h"
#include "irxtokn.h"
#include "irxvpool.h"
#include "irxwkblk.h"
#include "lstralloc.h"
#include "lstring.h"

/* ------------------------------------------------------------------ */
/*  Lstr scratch helpers                                              */
/* ------------------------------------------------------------------ */

static int lstr_set_bytes(struct lstr_alloc *a, PLstr s,
                          const char *buf, size_t len)
{
    int rc = Lfx(a, s, len);
    if (rc != LSTR_OK)
    {
        return rc;
    }
    if (len > 0)
    {
        memcpy(s->pstr, buf, len);
    }
    s->len = len;
    s->type = LSTRING_TY; /* invalidate any cached numeric type */
    return LSTR_OK;
}

/* ------------------------------------------------------------------ */
/*  Error plumbing                                                    */
/* ------------------------------------------------------------------ */

static int fail(struct irx_parser *p, int code)
{
    if (p->error_code == IRXPARS_OK)
    {
        p->error_code = code;
        if (p->tok_pos < p->tok_count)
        {
            p->error_line = p->tokens[p->tok_pos].tok_line;
        }
    }
    return code;
}

/* ------------------------------------------------------------------ */
/*  Token inspection                                                  */
/* ------------------------------------------------------------------ */

static const struct irx_token *peek_tok(struct irx_parser *p, int off)
{
    int idx = p->tok_pos + off;
    if (idx < 0 || idx >= p->tok_count)
    {
        return NULL;
    }
    return &p->tokens[idx];
}

static const struct irx_token *cur_tok(struct irx_parser *p)
{
    return peek_tok(p, 0);
}

static void advance_tok(struct irx_parser *p)
{
    if (p->tok_pos < p->tok_count)
    {
        p->tok_pos++;
    }
}

static int tok_is_op_char(const struct irx_token *t, unsigned char type,
                          char ch)
{
    if (t == NULL)
    {
        return 0;
    }
    if (t->tok_type != type)
    {
        return 0;
    }
    if (t->tok_length != 1)
    {
        return 0;
    }
    return t->tok_text[0] == ch;
}

static int tok_ends_clause(const struct irx_token *t)
{
    if (t == NULL)
    {
        return 1;
    }
    return t->tok_type == TOK_EOC ||
           t->tok_type == TOK_EOF ||
           t->tok_type == TOK_SEMICOLON;
}

/* Source-end column of a token. For string-like tokens, tok_length
 * counts only the body bytes, so we must add the surrounding quotes
 * (2 for plain strings, 3 for hex / bin strings which carry a
 * trailing x / b suffix). */
static int tok_source_end_col(const struct irx_token *t)
{
    int end = (int)t->tok_col + (int)t->tok_length;
    switch (t->tok_type)
    {
        case TOK_STRING:
            return end + 2;
        case TOK_HEXSTRING:
            return end + 3;
        case TOK_BINSTRING:
            return end + 3;
        default:
            return end;
    }
}

/* Two tokens are "adjacent in source" if there is no whitespace gap
 * between them (same line, no column gap). Used for both function
 * call detection (SYMBOL immediately followed by '(') and for the
 * abuttal vs. blank concat decision. */
static int toks_adjacent(const struct irx_token *a,
                         const struct irx_token *b)
{
    if (a == NULL || b == NULL)
    {
        return 0;
    }
    if (a->tok_line != b->tok_line)
    {
        return 0;
    }
    return tok_source_end_col(a) == (int)b->tok_col;
}

/* Collapse doubled quotes in a string body. The tokenizer flags any
 * string that contained doubled quotes via TOKF_QUOTE_DBL and leaves
 * the raw source bytes in tok_text. We do not know the original
 * delimiter, so we try single quotes first (the common case in REXX
 * source) and fall back to double quotes. The parser only calls this
 * helper for plain TOK_STRING tokens; hex and bin strings never
 * carry TOKF_QUOTE_DBL. */
static void dedouble_string(PLstr s)
{
    unsigned char pair = '\'';
    size_t i;
    size_t out = 0;
    int saw_single = 0;
    int saw_double = 0;

    for (i = 0; i + 1 < s->len; i++)
    {
        if (s->pstr[i] == '\'' && s->pstr[i + 1] == '\'')
        {
            saw_single = 1;
        }
        if (s->pstr[i] == '"' && s->pstr[i + 1] == '"')
        {
            saw_double = 1;
        }
    }
    if (saw_double && !saw_single)
    {
        pair = '"';
    }

    for (i = 0; i < s->len;)
    {
        if (i + 1 < s->len && s->pstr[i] == pair &&
            s->pstr[i + 1] == pair)
        {
            s->pstr[out++] = pair;
            i += 2;
        }
        else
        {
            s->pstr[out++] = s->pstr[i++];
        }
    }
    s->len = out;
}

/* ------------------------------------------------------------------ */
/*  Uppercase helpers                                                 */
/* ------------------------------------------------------------------ */

static void upper_bytes(unsigned char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        int c = p[i];
        if (islower(c))
        {
            p[i] = (unsigned char)toupper(c);
        }
    }
}

/* Copy token text into dst as upper-case. */
static int set_upper_from_tok(struct lstr_alloc *a, PLstr dst,
                              const struct irx_token *t)
{
    int rc = lstr_set_bytes(a, dst, t->tok_text, t->tok_length);
    if (rc != LSTR_OK)
    {
        return rc;
    }
    upper_bytes(dst->pstr, dst->len);
    return LSTR_OK;
}

/* ------------------------------------------------------------------ */
/*  Numeric coercion (Phase 2 uses C long)                            */
/* ------------------------------------------------------------------ */

static int lstr_to_long(PLstr s, long *out)
{
    char buf[48];
    char *end;
    long v;
    size_t i, first, last;

    if (_Lisnum(s) == LNUM_NOT_NUM)
    {
        return 0;
    }
    /* Strip leading/trailing blanks into a local null-terminated copy. */
    first = 0;
    while (first < s->len && (s->pstr[first] == ' ' ||
                              s->pstr[first] == '\t'))
    {
        first++;
    }
    last = s->len;
    while (last > first && (s->pstr[last - 1] == ' ' ||
                            s->pstr[last - 1] == '\t'))
    {
        last--;
    }
    if (last - first >= sizeof(buf))
    {
        return 0;
    }
    for (i = 0; i < last - first; i++)
    {
        buf[i] = (char)s->pstr[first + i];
    }
    buf[last - first] = '\0';

    v = strtol(buf, &end, 10);
    if (*end != '\0')
    {
        /* Allow the REAL form produced by _Lisnum by reparsing with
         * strtod - but Phase 2 stores integers, so fall back to
         * integer part via double cast. */
        double d = strtod(buf, &end);
        if (*end != '\0')
        {
            return 0;
        }
        v = (long)d;
    }
    *out = v;
    return 1;
}

static int long_to_lstr(struct lstr_alloc *a, PLstr dst, long v)
{
    char buf[32];
    int n = sprintf(buf, "%ld", v);
    if (n < 0)
    {
        return LSTR_ERR_NOMEM;
    }
    return lstr_set_bytes(a, dst, buf, (size_t)n);
}

/* ------------------------------------------------------------------ */
/*  Comparison helpers                                                */
/*                                                                    */
/*  REXX `=` : if both numeric, compare numerically; else blank-pad   */
/*             the shorter operand with spaces and compare            */
/*             byte-for-byte. Case-sensitive.                         */
/*  REXX `==`: exact byte-for-byte comparison, no padding.            */
/*                                                                    */
/*  Ref: SC28-1883-0 Chapter 7; ANSI X3J18 Section 7.4.7.             */
/*  Case folding applies to variable NAMES (done by the parser        */
/*  before pool access), never to VALUES in a comparison.             */
/* ------------------------------------------------------------------ */

static int compare_strict(PLstr a, PLstr b)
{
    size_t n = a->len < b->len ? a->len : b->len;
    int cmp;
    if (n > 0)
    {
        cmp = memcmp(a->pstr, b->pstr, n);
        if (cmp != 0)
        {
            return cmp < 0 ? -1 : 1;
        }
    }
    if (a->len < b->len)
    {
        return -1;
    }
    if (a->len > b->len)
    {
        return 1;
    }
    return 0;
}

static int compare_normal(PLstr a, PLstr b)
{
    long la, lb;
    size_t la_len, lb_len, max_len, i;
    int anum, bnum;
    unsigned char ca, cb;

    anum = lstr_to_long(a, &la);
    bnum = lstr_to_long(b, &lb);
    if (anum && bnum)
    {
        if (la < lb)
        {
            return -1;
        }
        if (la > lb)
        {
            return 1;
        }
        return 0;
    }

    /* String comparison with blank-pad. Case-sensitive per
     * SC28-1883-0 Chapter 7. Trailing blanks on either side are
     * dropped first; any remaining length difference is then
     * padded with spaces on the right. */
    la_len = a->len;
    while (la_len > 0 && a->pstr[la_len - 1] == ' ')
    {
        la_len--;
    }
    lb_len = b->len;
    while (lb_len > 0 && b->pstr[lb_len - 1] == ' ')
    {
        lb_len--;
    }

    max_len = la_len > lb_len ? la_len : lb_len;
    for (i = 0; i < max_len; i++)
    {
        ca = (unsigned char)(i < la_len ? a->pstr[i] : ' ');
        cb = (unsigned char)(i < lb_len ? b->pstr[i] : ' ');
        if (ca < cb)
        {
            return -1;
        }
        if (ca > cb)
        {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Built-in functions                                                */
/* ------------------------------------------------------------------ */

static int bif_length(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    (void)argc;
    return long_to_lstr(p->alloc, result, (long)argv[0]->len);
}

/* ------------------------------------------------------------------ */
/*  ARG([n[,option]]) - argument count / value / existence (WP-17)   */
/* ------------------------------------------------------------------ */

static int bif_arg(struct irx_parser *p, int argc, PLstr *argv, PLstr result)
{
    int n;
    char nbuf[16];
    int nlen;

    /* ARG() -> number of arguments. */
    if (argc == 0)
    {
        return long_to_lstr(p->alloc, result, (long)p->call_argc);
    }

    /* Empty index string is a syntax error. */
    if (argv[0]->len == 0)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    /* Get positional index n (1-based). */
    nlen = (argv[0]->len < (int)sizeof(nbuf) - 1)
               ? (int)argv[0]->len
               : (int)(sizeof(nbuf) - 1);
    memcpy(nbuf, argv[0]->pstr, (size_t)nlen);
    nbuf[nlen] = '\0';
    n = atoi(nbuf);
    if (n < 1)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    if (argc == 1)
    {
        /* ARG(n) -> value of argument n or "". */
        if (n <= p->call_argc && p->call_arg_exists != NULL &&
            p->call_arg_exists[n - 1] &&
            p->call_args != NULL)
        {
            int rc = Lstrcpy(p->alloc, result, &p->call_args[n - 1]);
            return (rc == LSTR_OK) ? IRXPARS_OK : fail(p, IRXPARS_NOMEM);
        }
        return (lstr_set_bytes(p->alloc, result, "", 0) == LSTR_OK)
                   ? IRXPARS_OK
                   : fail(p, IRXPARS_NOMEM);
    }

    /* argc == 2: 'E' (exists) or 'O' (omitted) option. */
    if (argv[1]->len != 1)
    {
        return fail(p, IRXPARS_SYNTAX);
    }
    int flag;
    int exists;
    unsigned char opt = argv[1]->pstr[0];
    char answer;

    if (islower(opt))
    {
        opt = (unsigned char)toupper(opt);
    }
    exists = (n <= p->call_argc && p->call_arg_exists != NULL &&
              p->call_arg_exists[n - 1])
                 ? 1
                 : 0;

    if (opt == 'E')
    {
        flag = exists;
    }
    else if (opt == 'O')
    {
        flag = !exists;
    }
    else
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    answer = flag ? '1' : '0';
    return (lstr_set_bytes(p->alloc, result, &answer, 1) == LSTR_OK)
               ? IRXPARS_OK
               : fail(p, IRXPARS_NOMEM);
}

static const struct irx_bif g_bif_table[] = {
    {"LENGTH", 1, 1, bif_length},
    {"ARG", 0, 2, bif_arg}};
static const int g_bif_count = (int)(sizeof(g_bif_table) /
                                     sizeof(g_bif_table[0]));

static const struct irx_bif *find_bif(const unsigned char *name, size_t len)
{
    int i;
    for (i = 0; i < g_bif_count; i++)
    {
        const char *bn = g_bif_table[i].bif_name;
        size_t bl = strlen(bn);
        if (bl == len && memcmp(bn, name, len) == 0)
        {
            return &g_bif_table[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  SAY keyword handler (WP-14)                                       */
/*                                                                    */
/*  SAY [expr]                                                        */
/*  Evaluates the expression (or produces an empty string if none)   */
/*  and writes it followed by a newline via the IRXEXTE io_routine.  */
/* ------------------------------------------------------------------ */

static int kw_say(struct irx_parser *p)
{
    Lstr result;
    struct irxexte *exte;
    int (*io_fn)(int, PLstr, struct envblock *);
    int rc;

    Lzeroinit(&result);

    if (tok_ends_clause(cur_tok(p)))
    {
        /* SAY with no expression -> output an empty line */
        rc = Lfx(p->alloc, &result, 0);
        if (rc != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
        result.len = 0;
    }
    else
    {
        rc = irx_pars_eval_expr(p, &result);
        if (rc != IRXPARS_OK)
        {
            Lfree(p->alloc, &result);
            return rc;
        }
    }

    if (p->envblock != NULL)
    {
        exte = (struct irxexte *)p->envblock->envblock_irxexte;
        if (exte != NULL && exte->io_routine != NULL)
        {
            io_fn = (int (*)(int, PLstr, struct envblock *))exte->io_routine;
            io_fn(RXFWRITE, &result, p->envblock);
        }
    }

    Lfree(p->alloc, &result);
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  WP-15 forward declarations (parse_* defined later in file)        */
/* ------------------------------------------------------------------ */

static int parse_add(struct irx_parser *p, PLstr out);
static int parse_or(struct irx_parser *p, PLstr out);

/* ------------------------------------------------------------------ */
/*  WP-15 helper: case-insensitive symbol comparison                  */
/* ------------------------------------------------------------------ */

static int sym_matches(const struct irx_token *t, const char *name)
{
    size_t n = strlen(name);
    size_t i;
    if (t == NULL || t->tok_type != TOK_SYMBOL)
    {
        return 0;
    }
    if ((size_t)t->tok_length != n)
    {
        return 0;
    }
    for (i = 0; i < n; i++)
    {
        int c = (unsigned char)t->tok_text[i];
        if (islower(c))
        {
            c = toupper(c);
        }
        if (c != (unsigned char)name[i])
        {
            return 0;
        }
    }
    return 1;
}

/* Upper-case a symbol token's text into a fixed char buffer.
 * Returns the number of bytes written (may truncate to dst_max-1). */
static int sym_to_upper(const struct irx_token *t, char *dst, int dst_max)
{
    int n = (int)t->tok_length;
    int i;
    if (n >= dst_max)
    {
        n = dst_max - 1;
    }
    for (i = 0; i < n; i++)
    {
        int c = (unsigned char)t->tok_text[i];
        dst[i] = (char)(islower(c) ? toupper(c) : c);
    }
    dst[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------ */
/*  WP-15 helper: find position after the matching END                */
/*                                                                    */
/*  Scans forward from `from`, counting DO/SELECT vs END at clause    */
/*  starts. Returns tok_pos of first token after the matching END,    */
/*  or -1 if not found.                                               */
/* ------------------------------------------------------------------ */

/* Returns 1 if the token at `pos` is the keyword `kw` (not an
 * assignment target), 0 otherwise. */
static int tok_is_kw(struct irx_parser *p, int pos, const char *kw)
{
    const struct irx_token *t;
    const struct irx_token *tnext;
    const struct irx_token *tnext2;

    if (pos < 0 || pos >= p->tok_count)
    {
        return 0;
    }
    t = &p->tokens[pos];
    if (!sym_matches(t, kw))
    {
        return 0;
    }
    /* Exclude assignment: SYMBOL = (but not SYMBOL ==) */
    tnext = (pos + 1 < p->tok_count) ? &p->tokens[pos + 1] : NULL;
    tnext2 = (pos + 2 < p->tok_count) ? &p->tokens[pos + 2] : NULL;
    if (tok_is_op_char(tnext, TOK_COMPARISON, '=') &&
        !tok_is_op_char(tnext2, TOK_COMPARISON, '='))
    {
        return 0; /* it's an assignment */
    }
    return 1;
}

static int find_end_after(struct irx_parser *p, int from)
{
    int depth = 1;
    int pos = from;

    while (pos < p->tok_count)
    {
        const struct irx_token *t = &p->tokens[pos];
        if (t->tok_type == TOK_EOF)
        {
            break;
        }
        if (tok_is_kw(p, pos, "DO") || tok_is_kw(p, pos, "SELECT"))
        {
            depth++;
        }
        else if (tok_is_kw(p, pos, "END"))
        {
            depth--;
            if (depth == 0)
            {
                return pos + 1;
            }
        }
        pos++;
    }
    return -1;
}

/* Skip forward to the end of the current clause (stops at EOC/EOF/;).
 * Handles nested parentheses so that function arguments are not
 * mistaken for clause boundaries. */
static void skip_to_clause_end(struct irx_parser *p)
{
    int depth = 0;
    while (p->tok_pos < p->tok_count)
    {
        const struct irx_token *t = cur_tok(p);
        if (t == NULL)
        {
            break;
        }
        if (t->tok_type == TOK_LPAREN)
        {
            depth++;
            advance_tok(p);
            continue;
        }
        if (t->tok_type == TOK_RPAREN)
        {
            if (depth > 0)
            {
                depth--;
            }
            advance_tok(p);
            continue;
        }
        if (depth == 0 && tok_ends_clause(t))
        {
            break;
        }
        advance_tok(p);
    }
}

/* Skip one complete instruction (simple clause or DO/SELECT block).
 * On entry, tok_pos should point at the first token of the instruction
 * (leading EOC tokens are consumed first).
 * On return, tok_pos points to the first terminator after the
 * instruction (i.e., the caller should then consume trailing EOC). */
static int skip_instruction(struct irx_parser *p)
{
    const struct irx_token *t;

    /* Eat leading EOC */
    while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
    {
        advance_tok(p);
    }

    t = cur_tok(p);
    if (t == NULL || t->tok_type == TOK_EOF)
    {
        return IRXPARS_OK;
    }

    /* DO or SELECT block: jump to token after matching END */
    if (tok_is_kw(p, p->tok_pos, "DO") ||
        tok_is_kw(p, p->tok_pos, "SELECT"))
    {
        int end_pos = find_end_after(p, p->tok_pos + 1);
        if (end_pos < 0)
        {
            return fail(p, IRXPARS_SYNTAX);
        }
        p->tok_pos = end_pos;
        return IRXPARS_OK;
    }

    skip_to_clause_end(p);
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  WP-15 helper: set a variable by name (char * + length)            */
/* ------------------------------------------------------------------ */

static int set_var_str(struct irx_parser *p,
                       const char *name, int name_len,
                       const char *val, int val_len)
{
    Lstr k, v;
    int rc;

    Lzeroinit(&k);
    Lzeroinit(&v);

    rc = lstr_set_bytes(p->alloc, &k, name, (size_t)name_len);
    if (rc != LSTR_OK)
    {
        return fail(p, IRXPARS_NOMEM);
    }

    rc = lstr_set_bytes(p->alloc, &v, val, (size_t)val_len);
    if (rc != LSTR_OK)
    {
        Lfree(p->alloc, &k);
        return fail(p, IRXPARS_NOMEM);
    }

    rc = vpool_set(p->vpool, &k, &v);
    Lfree(p->alloc, &k);
    Lfree(p->alloc, &v);
    return (rc == VPOOL_OK) ? IRXPARS_OK : fail(p, IRXPARS_NOMEM);
}

static int set_var_long(struct irx_parser *p,
                        const char *name, int name_len, long val)
{
    char buf[32];
    int n = sprintf(buf, "%ld", val);
    if (n < 0)
    {
        return fail(p, IRXPARS_NOMEM);
    }
    return set_var_str(p, name, name_len, buf, n);
}

/* ------------------------------------------------------------------ */
/*  WP-15 helper: evaluate a condition at a saved token position      */
/*                                                                    */
/*  Temporarily sets p->tok_pos to cond_pos, evaluates one           */
/*  expression, then restores tok_pos to saved. Result is put in out. */
/* ------------------------------------------------------------------ */

static int eval_at(struct irx_parser *p, int cond_pos, PLstr out)
{
    int saved = p->tok_pos;
    int rc;
    p->tok_pos = cond_pos;
    rc = irx_pars_eval_expr(p, out);
    p->tok_pos = saved;
    return rc;
}

/* ------------------------------------------------------------------ */
/*  WP-15 helper: DO frame iteration check                            */
/*                                                                    */
/*  Called by kw_end to decide whether to iterate or exit the loop.  */
/*  Returns 1 if the loop body should execute again, 0 to exit.      */
/*  On iteration, updates the control variable and re-sets loop_start  */
/*  as the target. On exit, leaves p->tok_pos past the END token.     */
/* ------------------------------------------------------------------ */

static int do_should_iterate(struct irx_parser *p,
                             struct irx_exec_frame *f)
{
    Lstr cond;
    long cond_val;
    int iterate;

    switch (f->do_type)
    {
        case DO_SIMPLE:
            return 0; /* body executes once */

        case DO_FOREVER:
            return 1;

        case DO_COUNT:
            if (f->ctrl_count <= 0)
            {
                return 0;
            }
            f->ctrl_count--;
            return (f->ctrl_count >= 0) ? 1 : 0;

        case DO_CTRL:
        {
            /* Increment control variable, then check bounds. */
            f->ctrl_val += f->ctrl_by;
            if (f->ctrl_by >= 0)
            {
                iterate = (f->ctrl_val <= f->ctrl_to);
            }
            else
            {
                iterate = (f->ctrl_val >= f->ctrl_to);
            }
            if (iterate && f->ctrl_name_len > 0)
            {
                set_var_long(p, f->ctrl_name, f->ctrl_name_len,
                             f->ctrl_val);
            }
            return iterate;
        }

        case DO_WHILE:
            Lzeroinit(&cond);
            if (eval_at(p, f->cond_tok_pos, &cond) != IRXPARS_OK)
            {
                Lfree(p->alloc, &cond);
                return 0;
            }
            iterate = lstr_to_long(&cond, &cond_val) && (cond_val != 0);
            Lfree(p->alloc, &cond);
            return iterate;

        case DO_UNTIL:
            /* Body always ran at least once before END is reached. */
            Lzeroinit(&cond);
            if (eval_at(p, f->cond_tok_pos, &cond) != IRXPARS_OK)
            {
                Lfree(p->alloc, &cond);
                return 0;
            }
            /* UNTIL: stop when cond is true */
            iterate = !(lstr_to_long(&cond, &cond_val) && (cond_val != 0));
            Lfree(p->alloc, &cond);
            return iterate;

        default:
            return 0;
    }
}

/* ================================================================== */
/*  WP-15 keyword handlers                                            */
/* ================================================================== */

/* Forward declaration for mutual recursion (kw_if uses exec_clause). */
static int exec_clause(struct irx_parser *p);

/* ------------------------------------------------------------------ */
/*  NOP                                                               */
/* ------------------------------------------------------------------ */

static int kw_nop(struct irx_parser *p)
{
    (void)p;
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  THEN / ELSE — syntax barriers only, never standalone instructions  */
/* ------------------------------------------------------------------ */

static int kw_then(struct irx_parser *p)
{
    return fail(p, IRXPARS_SYNTAX);
}

static int kw_else(struct irx_parser *p)
{
    return fail(p, IRXPARS_SYNTAX);
}

/* ------------------------------------------------------------------ */
/*  EXIT [expr]                                                       */
/* ------------------------------------------------------------------ */

static int kw_exit(struct irx_parser *p)
{
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    long rc_val = 0;

    if (!tok_ends_clause(cur_tok(p)))
    {
        Lstr result;
        Lzeroinit(&result);
        if (irx_pars_eval_expr(p, &result) == IRXPARS_OK)
        {
            lstr_to_long(&result, &rc_val);
        }
        Lfree(p->alloc, &result);
    }

    p->exit_requested = 1;
    p->exit_rc = (int)rc_val;
    if (es != NULL)
    {
        es->exit_requested = 1;
        es->exit_rc = (int)rc_val;
    }
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  DO [variant] ... END                                              */
/* ------------------------------------------------------------------ */

static int kw_do(struct irx_parser *p)
{
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    struct irx_exec_frame *f;
    const struct irx_token *t;
    int rc = IRXPARS_OK;
    Lstr tmp;

    Lzeroinit(&tmp);

    f = irx_ctrl_frame_push(p, FRAME_DO);
    if (f == NULL)
    {
        return fail(p, IRXPARS_NOMEM);
    }

    /* Inherit label from exec_stack->last_label if set. */
    if (es != NULL && es->last_label_len > 0)
    {
        int n = es->last_label_len;
        if (n >= CTRL_NAME_MAX)
        {
            n = CTRL_NAME_MAX - 1;
        }
        memcpy(f->do_label, es->last_label, (size_t)n);
        f->do_label[n] = '\0';
        f->do_label_len = n;
        es->last_label_len = 0;
        es->last_label[0] = '\0';
    }

    t = cur_tok(p);

    /* ---- DO FOREVER ---- */
    if (sym_matches(t, "FOREVER"))
    {
        advance_tok(p);
        f->do_type = DO_FOREVER;
        goto body;
    }

    /* ---- DO WHILE cond ---- */
    if (sym_matches(t, "WHILE"))
    {
        advance_tok(p);
        f->do_type = DO_WHILE;
        f->cond_tok_pos = p->tok_pos;
        /* Evaluate once to (a) validate syntax and (b) check first time */
        rc = irx_pars_eval_expr(p, &tmp);
        if (rc != IRXPARS_OK)
        {
            goto done;
        }
        long cv = 0;
        if (!lstr_to_long(&tmp, &cv) || cv == 0)
        {
            /* Condition false on entry: find END and jump past */
            int end_pos = find_end_after(p, p->tok_pos);
            if (end_pos < 0)
            {
                rc = fail(p, IRXPARS_SYNTAX);
                goto done;
            }
            irx_ctrl_frame_pop(p);
            p->tok_pos = end_pos;
            goto done;
        }
        goto body;
    }

    /* ---- DO UNTIL cond ---- */
    if (sym_matches(t, "UNTIL"))
    {
        advance_tok(p);
        f->do_type = DO_UNTIL;
        f->cond_tok_pos = p->tok_pos;
        /* Skip past the UNTIL expression so loop_start lands on body. */
        rc = irx_pars_eval_expr(p, &tmp);
        if (rc != IRXPARS_OK)
        {
            goto done;
        }
        /* UNTIL always executes body at least once. */
        goto body;
    }

    /* ---- DO SYMBOL = start TO end [BY step] ---- */
    if (t != NULL && t->tok_type == TOK_SYMBOL &&
        !(t->tok_flags & TOKF_CONSTANT))
    {
        const struct irx_token *tnext = peek_tok(p, 1);
        if (tok_is_op_char(tnext, TOK_COMPARISON, '='))
        {
            /* Check it's not == */
            const struct irx_token *tnext2 = peek_tok(p, 2);
            if (!tok_is_op_char(tnext2, TOK_COMPARISON, '='))
            {
                /* DO ctrl_var = start TO end [BY step] */
                f->do_type = DO_CTRL;
                f->ctrl_by = 1; /* default step */
                f->ctrl_name_len = sym_to_upper(t, f->ctrl_name,
                                                CTRL_NAME_MAX);
                advance_tok(p); /* SYMBOL */
                advance_tok(p); /* =      */

                /* start expression: use parse_add to stop at TO/BY/WHILE */
                rc = parse_add(p, &tmp);
                if (rc != IRXPARS_OK)
                {
                    goto done;
                }
                if (!lstr_to_long(&tmp, &f->ctrl_val))
                {
                    rc = fail(p, IRXPARS_SYNTAX);
                    goto done;
                }
                Lfree(p->alloc, &tmp);
                Lzeroinit(&tmp);

                /* TO */
                if (!sym_matches(cur_tok(p), "TO"))
                {
                    rc = fail(p, IRXPARS_SYNTAX);
                    goto done;
                }
                advance_tok(p);

                /* end expression */
                rc = parse_add(p, &tmp);
                if (rc != IRXPARS_OK)
                {
                    goto done;
                }
                if (!lstr_to_long(&tmp, &f->ctrl_to))
                {
                    rc = fail(p, IRXPARS_SYNTAX);
                    goto done;
                }
                Lfree(p->alloc, &tmp);
                Lzeroinit(&tmp);

                /* Optional BY */
                if (sym_matches(cur_tok(p), "BY"))
                {
                    advance_tok(p);
                    rc = parse_add(p, &tmp);
                    if (rc != IRXPARS_OK)
                    {
                        goto done;
                    }
                    if (!lstr_to_long(&tmp, &f->ctrl_by))
                    {
                        rc = fail(p, IRXPARS_SYNTAX);
                        goto done;
                    }
                    Lfree(p->alloc, &tmp);
                    Lzeroinit(&tmp);
                }

                /* Optional WHILE/UNTIL: skip expression (future WP). */
                if (sym_matches(cur_tok(p), "WHILE") ||
                    sym_matches(cur_tok(p), "UNTIL"))
                {
                    advance_tok(p);
                    rc = irx_pars_eval_expr(p, &tmp);
                    if (rc != IRXPARS_OK)
                    {
                        goto done;
                    }
                    Lfree(p->alloc, &tmp);
                    Lzeroinit(&tmp);
                }

                /* Set initial ctrl var value. */
                rc = set_var_long(p, f->ctrl_name, f->ctrl_name_len,
                                  f->ctrl_val);
                if (rc != IRXPARS_OK)
                {
                    goto done;
                }

                /* Check initial condition: if start > end (BY>0) skip. */
                int skip = 0;
                if (f->ctrl_by >= 0 && f->ctrl_val > f->ctrl_to)
                {
                    skip = 1;
                }
                if (f->ctrl_by < 0 && f->ctrl_val < f->ctrl_to)
                {
                    skip = 1;
                }
                if (skip)
                {
                    int end_pos = find_end_after(p, p->tok_pos);
                    if (end_pos < 0)
                    {
                        rc = fail(p, IRXPARS_SYNTAX);
                        goto done;
                    }
                    irx_ctrl_frame_pop(p);
                    p->tok_pos = end_pos;
                    goto done;
                }
                goto body;
            }
        }
    }

    /* ---- DO n (repetitive count) ---- */
    if (t != NULL && !tok_ends_clause(t))
    {
        /* Could be DO n where n is an expression. */
        f->do_type = DO_COUNT;
        rc = parse_add(p, &tmp);
        if (rc != IRXPARS_OK)
        {
            goto done;
        }
        if (!lstr_to_long(&tmp, &f->ctrl_count))
        {
            rc = fail(p, IRXPARS_SYNTAX);
            goto done;
        }
        Lfree(p->alloc, &tmp);
        Lzeroinit(&tmp);
        if (f->ctrl_count <= 0)
        {
            /* Zero or negative: skip entire body. */
            int end_pos = find_end_after(p, p->tok_pos);
            if (end_pos < 0)
            {
                rc = fail(p, IRXPARS_SYNTAX);
                goto done;
            }
            irx_ctrl_frame_pop(p);
            p->tok_pos = end_pos;
            goto done;
        }
        f->ctrl_count--; /* already executing first iteration */
        goto body;
    }

    /* ---- DO; ... END (simple group) ---- */
    f->do_type = DO_SIMPLE;

body:
    /* Record loop_start = first token of body (after header). */
    /* Eat any trailing EOC before the body so loop_start lands
     * on the first actual token. */
    {
        int end_pos;
        while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
        {
            advance_tok(p);
        }
        f->loop_start = p->tok_pos;
        end_pos = find_end_after(p, p->tok_pos);
        if (end_pos < 0)
        {
            rc = fail(p, IRXPARS_SYNTAX);
            goto done;
        }
        f->loop_end = end_pos;
    }

done:
    Lfree(p->alloc, &tmp);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  END [name]                                                        */
/* ------------------------------------------------------------------ */

static int kw_end(struct irx_parser *p)
{
    struct irx_exec_frame *f = irx_ctrl_frame_top(p);

    /* Consume optional name after END (REXX allows END loopname). */
    if (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_SYMBOL &&
        !tok_ends_clause(cur_tok(p)))
    {
        advance_tok(p);
    }

    if (f == NULL)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    if (f->frame_type == FRAME_SELECT)
    {
        /* End of SELECT block. */
        if (!f->select_matched)
        {
            /* No WHEN matched and no OTHERWISE was present. */
            return fail(p, IRXPARS_SYNTAX);
        }
        irx_ctrl_frame_pop(p);
        return IRXPARS_OK;
    }

    if (f->frame_type != FRAME_DO)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    if (do_should_iterate(p, f))
    {
        /* Go back to loop body. */
        p->tok_pos = f->loop_start;
    }
    else
    {
        /* Exit loop: tok_pos is already past END (set to loop_end). */
        p->tok_pos = f->loop_end;
        irx_ctrl_frame_pop(p);
    }
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  ITERATE [name]                                                    */
/* ------------------------------------------------------------------ */

static int kw_iterate(struct irx_parser *p)
{
    char label[CTRL_NAME_MAX];
    int label_len = 0;
    int frame_idx;
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    struct irx_exec_frame *f;

    /* Optional label */
    if (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_SYMBOL &&
        !tok_ends_clause(cur_tok(p)))
    {
        label_len = sym_to_upper(cur_tok(p), label, CTRL_NAME_MAX);
        advance_tok(p);
    }

    frame_idx = irx_ctrl_find_do(p,
                                 label_len > 0 ? label : NULL,
                                 label_len);
    if (frame_idx < 0)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    f = &es->frames[frame_idx];

    /* Pop all frames above the target DO frame. */
    es->top = frame_idx + 1;

    /* Advance the loop: for DO_CTRL increment ctrl var before jumping. */
    if (f->do_type == DO_CTRL)
    {
        f->ctrl_val += f->ctrl_by;
        /* Check bounds */
        int in_range;
        if (f->ctrl_by >= 0)
        {
            in_range = (f->ctrl_val <= f->ctrl_to);
        }
        else
        {
            in_range = (f->ctrl_val >= f->ctrl_to);
        }
        if (!in_range)
        {
            p->tok_pos = f->loop_end;
            irx_ctrl_frame_pop(p);
            return IRXPARS_OK;
        }
        set_var_long(p, f->ctrl_name, f->ctrl_name_len, f->ctrl_val);
    }
    else if (f->do_type == DO_WHILE)
    {
        /* Re-evaluate WHILE condition. */
        Lstr cond;
        long cv = 0;
        Lzeroinit(&cond);
        eval_at(p, f->cond_tok_pos, &cond);
        lstr_to_long(&cond, &cv);
        Lfree(p->alloc, &cond);
        if (!cv)
        {
            p->tok_pos = f->loop_end;
            irx_ctrl_frame_pop(p);
            return IRXPARS_OK;
        }
    }

    p->tok_pos = f->loop_start;
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  LEAVE [name]                                                      */
/* ------------------------------------------------------------------ */

static int kw_leave(struct irx_parser *p)
{
    char label[CTRL_NAME_MAX];
    int label_len = 0;
    int frame_idx;
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    struct irx_exec_frame *f;

    /* Optional label */
    if (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_SYMBOL &&
        !tok_ends_clause(cur_tok(p)))
    {
        label_len = sym_to_upper(cur_tok(p), label, CTRL_NAME_MAX);
        advance_tok(p);
    }

    frame_idx = irx_ctrl_find_do(p,
                                 label_len > 0 ? label : NULL,
                                 label_len);
    if (frame_idx < 0)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    f = &es->frames[frame_idx];
    p->tok_pos = f->loop_end;

    /* Pop the target frame and all frames above it. */
    es->top = frame_idx;
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  IF expr THEN clause_or_block [ELSE clause_or_block]               */
/* ------------------------------------------------------------------ */

static int kw_if(struct irx_parser *p)
{
    Lstr cond;
    long cond_val = 0;
    int cond_true;
    int rc;

    Lzeroinit(&cond);
    rc = irx_pars_eval_expr(p, &cond);
    if (rc != IRXPARS_OK)
    {
        Lfree(p->alloc, &cond);
        return rc;
    }
    lstr_to_long(&cond, &cond_val);
    cond_true = (cond_val != 0);
    Lfree(p->alloc, &cond);

    /* Consume THEN (required). */
    /* Skip any EOC between condition and THEN. */
    while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
    {
        advance_tok(p);
    }
    if (!sym_matches(cur_tok(p), "THEN"))
    {
        return fail(p, IRXPARS_SYNTAX);
    }
    advance_tok(p); /* consume THEN */

    /* Skip any EOC/whitespace between THEN and its clause. */
    while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
    {
        advance_tok(p);
    }

    if (cond_true)
    {
        struct irx_exec_stack *es =
            (struct irx_exec_stack *)p->exec_stack;
        int depth_before = es ? es->top : 0;

        /* Execute THEN branch. */
        rc = exec_clause(p);
        if (rc != IRXPARS_OK)
        {
            return rc;
        }

        /* If THEN was DO/SELECT, run the block until that frame exits. */
        while (rc == IRXPARS_OK && !p->exit_requested &&
               es != NULL && es->top > depth_before)
        {
            const struct irx_token *ct = cur_tok(p);
            if (ct == NULL || ct->tok_type == TOK_EOF)
            {
                break;
            }
            rc = exec_clause(p);
        }
        if (rc != IRXPARS_OK)
        {
            return rc;
        }
        if (p->exit_requested)
        {
            return IRXPARS_OK;
        }

        /* Consume trailing terminators of the THEN clause. */
        while (cur_tok(p) != NULL &&
               (cur_tok(p)->tok_type == TOK_EOC ||
                cur_tok(p)->tok_type == TOK_SEMICOLON))
        {
            advance_tok(p);
        }

        /* Check for ELSE and skip it. */
        while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
        {
            advance_tok(p);
        }
        if (sym_matches(cur_tok(p), "ELSE"))
        {
            advance_tok(p);
            while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
            {
                advance_tok(p);
            }
            rc = skip_instruction(p);
        }
    }
    else
    {
        /* Skip THEN branch. */
        rc = skip_instruction(p);
        if (rc != IRXPARS_OK)
        {
            return rc;
        }
        while (cur_tok(p) != NULL &&
               (cur_tok(p)->tok_type == TOK_EOC ||
                cur_tok(p)->tok_type == TOK_SEMICOLON))
        {
            advance_tok(p);
        }

        /* Check for ELSE and execute it. */
        while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
        {
            advance_tok(p);
        }
        if (sym_matches(cur_tok(p), "ELSE"))
        {
            advance_tok(p);
            while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
            {
                advance_tok(p);
            }
            rc = exec_clause(p);
        }
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/*  SELECT; WHEN cond THEN clause; ... [OTHERWISE clause;] END        */
/* ------------------------------------------------------------------ */

static int kw_select(struct irx_parser *p)
{
    struct irx_exec_frame *f;

    f = irx_ctrl_frame_push(p, FRAME_SELECT);
    if (f == NULL)
    {
        return fail(p, IRXPARS_NOMEM);
    }

    f->select_matched = 0;

    /* Find the matching END so WHEN can jump past it. */
    f->select_end = find_end_after(p, p->tok_pos);
    if (f->select_end < 0)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    return IRXPARS_OK;
}

static int kw_when(struct irx_parser *p)
{
    struct irx_exec_frame *f = irx_ctrl_frame_top(p);
    Lstr cond;
    long cond_val = 0;
    int cond_true;
    int rc;

    if (f == NULL || f->frame_type != FRAME_SELECT)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    Lzeroinit(&cond);

    if (f->select_matched)
    {
        /* A prior WHEN already matched: skip this WHEN entirely
         * (condition + THEN + clause) and jump to the next WHEN,
         * OTHERWISE, or END. cond was never allocated — no Lfree. */
        /* Skip condition */
        rc = skip_instruction(p);
        if (rc != IRXPARS_OK)
        {
            return rc;
        }
        /* Consume THEN */
        while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
        {
            advance_tok(p);
        }
        if (sym_matches(cur_tok(p), "THEN"))
        {
            advance_tok(p);
        }
        /* Skip THEN clause */
        while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
        {
            advance_tok(p);
        }
        rc = skip_instruction(p);
        return rc;
    }

    rc = irx_pars_eval_expr(p, &cond);
    if (rc != IRXPARS_OK)
    {
        Lfree(p->alloc, &cond);
        return rc;
    }
    lstr_to_long(&cond, &cond_val);
    cond_true = (cond_val != 0);
    Lfree(p->alloc, &cond);

    /* Consume THEN */
    while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
    {
        advance_tok(p);
    }
    if (!sym_matches(cur_tok(p), "THEN"))
    {
        return fail(p, IRXPARS_SYNTAX);
    }
    advance_tok(p);

    while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
    {
        advance_tok(p);
    }

    if (cond_true)
    {
        f->select_matched = 1;
        rc = exec_clause(p);
        if (rc != IRXPARS_OK)
        {
            return rc;
        }
        /* Jump to after SELECT END — kw_end will pop the frame. */
        /* Leave tok_pos at select_end so END/OTHERWISE are skipped. */
        /* Don't jump yet; let the normal clause loop encounter END. */
    }
    else
    {
        rc = skip_instruction(p);
    }
    return rc;
}

static int kw_otherwise(struct irx_parser *p)
{
    struct irx_exec_frame *f = irx_ctrl_frame_top(p);

    if (f == NULL || f->frame_type != FRAME_SELECT)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    if (f->select_matched)
    {
        /* Already matched: skip the OTHERWISE body. */
        return skip_instruction(p);
    }

    /* No WHEN matched: mark as matched (prevents "no match" error
     * in kw_end) and execute body. */
    f->select_matched = 1;
    return exec_clause(p);
}

/* ------------------------------------------------------------------ */
/*  CALL label [args]                                                 */
/* ------------------------------------------------------------------ */

static int kw_call(struct irx_parser *p)
{
    char label[CTRL_NAME_MAX];
    int label_len;
    int label_pos;
    int return_pos;
    int call_line;
    struct irx_exec_frame *f;
    char linebuf[32];
    int n;
    Lstr *new_args = NULL;
    int *new_exists = NULL;
    int argc = 0;
    int after_comma = 0;
    int rc;
    int i;

    /* CALL must be followed by a label name (symbol). */
    if (cur_tok(p) == NULL || cur_tok(p)->tok_type != TOK_SYMBOL)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    label_len = sym_to_upper(cur_tok(p), label, CTRL_NAME_MAX);
    call_line = cur_tok(p)->tok_line;
    advance_tok(p);

    /* Allocate argument arrays. */
    new_args = (Lstr *)p->alloc->alloc(
        (size_t)IRX_MAX_ARGS * sizeof(Lstr), p->alloc->ctx);
    new_exists = (int *)p->alloc->alloc(
        (size_t)IRX_MAX_ARGS * sizeof(int), p->alloc->ctx);
    if (new_args == NULL || new_exists == NULL)
    {
        rc = fail(p, IRXPARS_NOMEM);
        goto err;
    }
    memset(new_args, 0, (size_t)IRX_MAX_ARGS * sizeof(Lstr));
    memset(new_exists, 0, (size_t)IRX_MAX_ARGS * sizeof(int));

    /* Parse comma-separated arguments until clause end.
     * An omitted argument is an empty slot between commas, or
     * before the first comma, or after the last comma. */
    if (!tok_ends_clause(cur_tok(p)))
    {
        for (;;)
        {
            if (argc >= IRX_MAX_ARGS)
            {
                rc = fail(p, IRXPARS_SYNTAX);
                goto err;
            }
            if (cur_tok(p) != NULL &&
                cur_tok(p)->tok_type == TOK_COMMA)
            {
                /* Omitted argument at this position. */
                Lzeroinit(&new_args[argc]);
                new_exists[argc] = 0;
                argc++;
                advance_tok(p);
                after_comma = 1;
                continue;
            }
            if (tok_ends_clause(cur_tok(p)))
            {
                /* Trailing comma means one more omitted argument. */
                if (after_comma)
                {
                    if (argc >= IRX_MAX_ARGS)
                    {
                        rc = fail(p, IRXPARS_SYNTAX);
                        goto err;
                    }
                    Lzeroinit(&new_args[argc]);
                    new_exists[argc] = 0;
                    argc++;
                }
                break;
            }
            /* Evaluate the expression for this argument position. */
            Lzeroinit(&new_args[argc]);
            rc = irx_pars_eval_expr(p, &new_args[argc]);
            if (rc != IRXPARS_OK)
            {
                /* eval may have partially allocated into new_args[argc]. */
                Lfree(p->alloc, &new_args[argc]);
                goto err;
            }
            new_exists[argc] = 1;
            argc++;
            after_comma = 0;
            if (cur_tok(p) != NULL &&
                cur_tok(p)->tok_type == TOK_COMMA)
            {
                advance_tok(p);
                after_comma = 1;
                continue;
            }
            break;
        }
    }
    skip_to_clause_end(p);

    /* Where to return to (first token of next clause). */
    return_pos = p->tok_pos;

    /* Look up the label. */
    label_pos = irx_ctrl_label_find(p, label, label_len);
    if (label_pos < 0)
    {
        rc = fail(p, IRXPARS_BADFUNC);
        goto err;
    }

    /* Push CALL frame, saving the current argument context. */
    f = irx_ctrl_frame_push(p, FRAME_CALL);
    if (f == NULL)
    {
        rc = fail(p, IRXPARS_NOMEM);
        goto err;
    }
    f->call_return_pos = return_pos;
    f->call_line = call_line;
    f->saved_vpool = NULL;
    f->saved_args = p->call_args;
    f->saved_arg_exists = p->call_arg_exists;
    f->saved_argc = p->call_argc;
    f->procedure_allowed = 1;
    f->has_procedure = 0;

    /* Install the new argument context. */
    p->call_args = new_args;
    p->call_arg_exists = new_exists;
    p->call_argc = argc;

    /* Set SIGL special variable. */
    n = sprintf(linebuf, "%d", call_line);
    set_var_str(p, "SIGL", 4, linebuf, n);

    /* Jump to label (past SYMBOL ':' tokens). */
    p->tok_pos = label_pos + 2;
    return IRXPARS_OK;

err:
    if (new_args != NULL)
    {
        for (i = 0; i < argc; i++)
        {
            Lfree(p->alloc, &new_args[i]);
        }
        p->alloc->dealloc(new_args,
                          (size_t)IRX_MAX_ARGS * sizeof(Lstr),
                          p->alloc->ctx);
    }
    if (new_exists != NULL)
    {
        p->alloc->dealloc(new_exists,
                          (size_t)IRX_MAX_ARGS * sizeof(int),
                          p->alloc->ctx);
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/*  RETURN [expr]                                                     */
/* ------------------------------------------------------------------ */

static int kw_return(struct irx_parser *p)
{
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    struct irx_exec_frame *f;
    Lstr result;
    int have_result = 0;
    int i, j;

    Lzeroinit(&result);

    if (!tok_ends_clause(cur_tok(p)))
    {
        int rc = irx_pars_eval_expr(p, &result);
        if (rc != IRXPARS_OK)
        {
            Lfree(p->alloc, &result);
            return rc;
        }
        have_result = 1;
    }

    /* Locate the nearest enclosing CALL frame. */
    if (es == NULL)
    {
        goto no_call_frame;
    }

    for (i = es->top - 1; i >= 0; i--)
    {
        if (es->frames[i].frame_type == FRAME_CALL)
        {
            break;
        }
    }
    if (i < 0)
    {
        goto no_call_frame;
    }

    f = &es->frames[i];

    /* Restore the caller's variable pool if PROCEDURE was executed. */
    if (f->has_procedure)
    {
        vpool_destroy(p->vpool);   /* destroy child pool */
        p->vpool = f->saved_vpool; /* restore caller's pool */
    }

    /* Set RESULT in the caller's variable pool (now restored). */
    if (have_result)
    {
        Lstr key;
        Lzeroinit(&key);
        if (lstr_set_bytes(p->alloc, &key, "RESULT", 6) == LSTR_OK)
        {
            vpool_set(p->vpool, &key, &result);
            Lfree(p->alloc, &key);
        }
    }
    Lfree(p->alloc, &result);

    /* Restore the caller's argument context. */
    if (p->call_args != NULL)
    {
        for (j = 0; j < p->call_argc; j++)
        {
            Lfree(p->alloc, &p->call_args[j]);
        }
        p->alloc->dealloc(p->call_args,
                          (size_t)IRX_MAX_ARGS * sizeof(Lstr),
                          p->alloc->ctx);
    }
    if (p->call_arg_exists != NULL)
    {
        p->alloc->dealloc(p->call_arg_exists,
                          (size_t)IRX_MAX_ARGS * sizeof(int),
                          p->alloc->ctx);
    }
    p->call_args = f->saved_args;
    p->call_arg_exists = f->saved_arg_exists;
    p->call_argc = f->saved_argc;

    p->tok_pos = f->call_return_pos;
    es->top = i; /* pop everything up to and including CALL frame */
    return IRXPARS_OK;

no_call_frame:
    /* No CALL frame on stack: set RESULT at top level, treat as EXIT 0. */
    if (have_result)
    {
        Lstr key;
        Lzeroinit(&key);
        if (lstr_set_bytes(p->alloc, &key, "RESULT", 6) == LSTR_OK)
        {
            vpool_set(p->vpool, &key, &result);
            Lfree(p->alloc, &key);
        }
    }
    Lfree(p->alloc, &result);
    p->exit_requested = 1;
    p->exit_rc = 0;
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  PROCEDURE [EXPOSE var ...]  (WP-17)                               */
/* ------------------------------------------------------------------ */

static int kw_procedure(struct irx_parser *p)
{
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    struct irx_exec_frame *f;
    struct irx_vpool *child;
    const struct irx_token *t;
    char kw[8];
    int kw_len;
    int i;

    /* Find the nearest enclosing CALL frame. */
    if (es == NULL)
    {
        return fail(p, IRXPARS_SYNTAX);
    }
    for (i = es->top - 1; i >= 0; i--)
    {
        if (es->frames[i].frame_type == FRAME_CALL)
        {
            break;
        }
    }
    if (i < 0)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    f = &es->frames[i];
    if (!f->procedure_allowed)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    /* Mark PROCEDURE as executed for this CALL frame. */
    f->procedure_allowed = 0;
    f->has_procedure = 1;

    /* Create an isolated child variable pool. */
    child = vpool_create(p->alloc, p->vpool);
    if (child == NULL)
    {
        return fail(p, IRXPARS_NOMEM);
    }

    /* Save caller's pool and switch to child pool. */
    f->saved_vpool = p->vpool;
    p->vpool = child;

    /* Parse optional EXPOSE keyword and variable list. */
    t = cur_tok(p);
    if (t != NULL && !tok_ends_clause(t) && t->tok_type == TOK_SYMBOL &&
        t->tok_length == 6)
    {
        kw_len = sym_to_upper(t, kw, (int)sizeof(kw));
        if (kw_len == 6 && memcmp(kw, "EXPOSE", 6) == 0)
        {
            advance_tok(p); /* consume EXPOSE */

            /* Parse each name in the expose list. */
            while (!tok_ends_clause(cur_tok(p)))
            {
                Lstr ename;
                int rc;
                size_t el;

                t = cur_tok(p);
                if (t == NULL)
                {
                    break;
                }

                /* Indirect expose: (varname) — look up varname in the
                 * caller's pool, split its value by whitespace, expose
                 * each resulting name (or stem if it ends with '.'). */
                if (t->tok_type == TOK_LPAREN)
                {
                    Lstr iname;
                    Lstr ival;
                    size_t ipos;
                    size_t ilen;

                    advance_tok(p); /* consume ( */
                    t = cur_tok(p);
                    if (t == NULL || t->tok_type != TOK_SYMBOL)
                    {
                        return fail(p, IRXPARS_SYNTAX);
                    }

                    Lzeroinit(&iname);
                    if (set_upper_from_tok(p->alloc, &iname, t) != LSTR_OK)
                    {
                        Lfree(p->alloc, &iname);
                        return fail(p, IRXPARS_NOMEM);
                    }
                    advance_tok(p); /* consume varname */

                    t = cur_tok(p);
                    if (t == NULL || t->tok_type != TOK_RPAREN)
                    {
                        Lfree(p->alloc, &iname);
                        return fail(p, IRXPARS_SYNTAX);
                    }
                    advance_tok(p); /* consume ) */

                    /* Look up in the caller's pool (f->saved_vpool). */
                    Lzeroinit(&ival);
                    vpool_get(f->saved_vpool, &iname, &ival);
                    Lfree(p->alloc, &iname);

                    /* Split ival by whitespace and expose each word. */
                    ilen = ival.len;
                    ipos = 0;
                    while (ipos < ilen)
                    {
                        size_t wstart;
                        size_t wend;

                        while (ipos < ilen &&
                               isspace((unsigned char)ival.pstr[ipos]))
                        {
                            ipos++;
                        }
                        wstart = ipos;
                        while (ipos < ilen &&
                               !isspace((unsigned char)ival.pstr[ipos]))
                        {
                            ipos++;
                        }
                        wend = ipos;
                        if (wend == wstart)
                        {
                            break;
                        }

                        Lzeroinit(&ename);
                        if (Lfx(p->alloc, &ename,
                                wend - wstart) != LSTR_OK)
                        {
                            Lfree(p->alloc, &ival);
                            return fail(p, IRXPARS_NOMEM);
                        }
                        memcpy(ename.pstr, ival.pstr + wstart,
                               wend - wstart);
                        ename.len = wend - wstart;
                        ename.type = LSTRING_TY;
                        upper_bytes(ename.pstr, ename.len);

                        el = ename.len;
                        if (el > 0 && ename.pstr[el - 1] == '.')
                        {
                            vpool_expose_stem(child, &ename);
                        }
                        else
                        {
                            vpool_expose_var(child, &ename);
                        }
                        Lfree(p->alloc, &ename);
                    }
                    Lfree(p->alloc, &ival);
                    continue;
                }

                if (t->tok_type != TOK_SYMBOL)
                {
                    break;
                }

                Lzeroinit(&ename);
                rc = set_upper_from_tok(p->alloc, &ename, t);
                if (rc != LSTR_OK)
                {
                    Lfree(p->alloc, &ename);
                    return fail(p, IRXPARS_NOMEM);
                }
                advance_tok(p);

                /* Names ending with '.' are stem names. */
                el = ename.len;
                if (el > 0 && ename.pstr[el - 1] == '.')
                {
                    vpool_expose_stem(child, &ename);
                }
                else
                {
                    vpool_expose_var(child, &ename);
                }
                Lfree(p->alloc, &ename);
            }
        }
    }

    skip_to_clause_end(p);
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  ARG [var ...]  (WP-17)                                            */
/*                                                                    */
/*  Simplified PARSE UPPER ARG: splits call_args[0] by words and     */
/*  assigns (uppercased) to the listed variables. The last variable   */
/*  receives the remaining words joined with single spaces.           */
/* ------------------------------------------------------------------ */

/* Assign words from src[0..srclen-1] (starting at *pos_inout) to
 * vars[0..nvar-1], uppercased. The last variable gets the remaining text
 * trailing-stripped. Updates *pos_inout to reflect consumed input. */
/* Get pointer to the i-th variable name slot in the flat heap buffer. */
#define ARG_VAR(vars, i) ((vars) + (i) * CTRL_NAME_MAX)

static int arg_assign_words(struct irx_parser *p,
                            const unsigned char *src, size_t srclen,
                            size_t *pos_inout,
                            char *vars, int *var_lens,
                            int nvar)
{
    int i;
    size_t pos = *pos_inout;

    for (i = 0; i < nvar; i++)
    {
        Lstr val;
        Lstr key;
        size_t wstart;
        size_t wend;
        int rc;

        Lzeroinit(&val);
        Lzeroinit(&key);

        /* Skip leading whitespace. */
        while (pos < srclen && isspace((unsigned char)src[pos]))
        {
            pos++;
        }
        wstart = pos;

        if (i == nvar - 1)
        {
            /* Last var: rest of string, trailing-stripped. */
            wend = srclen;
            while (wend > wstart && isspace((unsigned char)src[wend - 1]))
            {
                wend--;
            }
        }
        else
        {
            /* Non-last var: next non-space run. */
            while (pos < srclen && !isspace((unsigned char)src[pos]))
            {
                pos++;
            }
            wend = pos;
        }

        if (wend > wstart)
        {
            size_t vlen = wend - wstart;
            if (Lfx(p->alloc, &val, vlen) != LSTR_OK)
            {
                return fail(p, IRXPARS_NOMEM);
            }
            memcpy(val.pstr, src + wstart, vlen);
            val.len = vlen;
            val.type = LSTRING_TY;
            upper_bytes(val.pstr, vlen);
        }

        rc = lstr_set_bytes(p->alloc, &key, ARG_VAR(vars, i),
                            (size_t)var_lens[i]);
        if (rc != LSTR_OK)
        {
            Lfree(p->alloc, &val);
            return fail(p, IRXPARS_NOMEM);
        }
        vpool_set(p->vpool, &key, &val);
        Lfree(p->alloc, &key);
        Lfree(p->alloc, &val);
    }

    *pos_inout = pos;
    return IRXPARS_OK;
}

static int kw_arg(struct irx_parser *p)
{
    /* ARG [template [, template ...]]
     * Each comma advances to the next argument position.
     * Each template assigns words from that argument to variables.
     *
     * Variable name storage is heap-allocated (IRX_MAX_ARGS *
     * CTRL_NAME_MAX bytes) to keep the parser stack frame small
     * on MVS 24-bit targets. */
    char *vars;
    int *var_lens;
    int nvar;
    int arg_idx = 0;
    int hit_comma;
    int rc = IRXPARS_OK;
    size_t pos;
    const unsigned char *src;
    size_t srclen;
    size_t vars_size = (size_t)IRX_MAX_ARGS * CTRL_NAME_MAX;
    size_t var_lens_size = (size_t)IRX_MAX_ARGS * sizeof(int);

    vars = (char *)p->alloc->alloc(vars_size, p->alloc->ctx);
    var_lens = (int *)p->alloc->alloc(var_lens_size, p->alloc->ctx);
    if (vars == NULL || var_lens == NULL)
    {
        if (vars != NULL)
        {
            p->alloc->dealloc(vars, vars_size, p->alloc->ctx);
        }
        if (var_lens != NULL)
        {
            p->alloc->dealloc(var_lens, var_lens_size, p->alloc->ctx);
        }
        return fail(p, IRXPARS_NOMEM);
    }

    while (!tok_ends_clause(cur_tok(p)))
    {
        const struct irx_token *t;

        /* Collect variable names until comma or clause end. */
        nvar = 0;
        hit_comma = 0;
        while (!tok_ends_clause(cur_tok(p)))
        {
            t = cur_tok(p);
            if (t == NULL)
            {
                break;
            }
            if (t->tok_type == TOK_COMMA)
            {
                advance_tok(p);
                hit_comma = 1;
                break;
            }
            if (t->tok_type != TOK_SYMBOL)
            {
                skip_to_clause_end(p);
                goto done;
            }
            if (nvar < IRX_MAX_ARGS)
            {
                var_lens[nvar] = sym_to_upper(t, ARG_VAR(vars, nvar),
                                              CTRL_NAME_MAX);
                nvar++;
            }
            advance_tok(p);
        }

        /* Assign words from the current argument to the collected vars. */
        src = NULL;
        srclen = 0;
        pos = 0;
        if (arg_idx < p->call_argc && p->call_arg_exists != NULL &&
            p->call_arg_exists[arg_idx] && p->call_args != NULL &&
            p->call_args[arg_idx].pstr != NULL)
        {
            src = p->call_args[arg_idx].pstr;
            srclen = p->call_args[arg_idx].len;
        }

        if (nvar > 0)
        {
            rc = arg_assign_words(p, src, srclen, &pos,
                                  vars, var_lens, nvar);
            if (rc != IRXPARS_OK)
            {
                goto done;
            }
        }

        arg_idx++;

        if (!hit_comma)
        {
            break; /* no comma: done */
        }
    }

done:
    p->alloc->dealloc(vars, vars_size, p->alloc->ctx);
    p->alloc->dealloc(var_lens, var_lens_size, p->alloc->ctx);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  SIGNAL label                                                      */
/* ------------------------------------------------------------------ */

static int kw_signal(struct irx_parser *p)
{
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    char label[CTRL_NAME_MAX];
    int label_len;
    int label_pos;

    if (cur_tok(p) == NULL || cur_tok(p)->tok_type != TOK_SYMBOL)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    label_len = sym_to_upper(cur_tok(p), label, CTRL_NAME_MAX);
    advance_tok(p);

    label_pos = irx_ctrl_label_find(p, label, label_len);
    if (label_pos < 0)
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    /* Clear the entire execution stack (SIGNAL discards all frames). */
    if (es != NULL)
    {
        es->top = 0;
    }

    /* Jump to label (past SYMBOL ':' tokens). */
    p->tok_pos = label_pos + 2;
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  Keyword table (WP-13 registers no handlers)                       */
/*                                                                    */
/*  WP-14 adds SAY. WP-15 adds DO/IF/SELECT/CALL/RETURN/EXIT/SIGNAL. */
/*  WP-16 adds PARSE. Each subsequent WP extends this table without   */
/*  modifying the parser core.                                        */
/* ------------------------------------------------------------------ */

static const struct irx_keyword g_keyword_table[] = {
    {"SAY", kw_say},
    {"NOP", kw_nop},
    {"EXIT", kw_exit},
    {"DO", kw_do},
    {"END", kw_end},
    {"ITERATE", kw_iterate},
    {"LEAVE", kw_leave},
    {"IF", kw_if},
    {"THEN", kw_then}, /* barrier only — IF consumes THEN   */
    {"ELSE", kw_else}, /* barrier only — IF consumes ELSE   */
    {"SELECT", kw_select},
    {"WHEN", kw_when},
    {"OTHERWISE", kw_otherwise},
    {"CALL", kw_call},
    {"RETURN", kw_return},
    {"SIGNAL", kw_signal},
    {"PROCEDURE", kw_procedure},
    {"ARG", kw_arg},
    {NULL, NULL}};

static const struct irx_keyword *find_keyword(const unsigned char *name,
                                              size_t len)
{
    int i;
    for (i = 0; g_keyword_table[i].kw_name != NULL; i++)
    {
        const char *kn = g_keyword_table[i].kw_name;
        size_t kl = strlen(kn);
        if (kl == len && memcmp(kn, name, len) == 0)
        {
            return &g_keyword_table[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                              */
/* ------------------------------------------------------------------ */

static int parse_or(struct irx_parser *p, PLstr out);
static int parse_and(struct irx_parser *p, PLstr out);
static int parse_comparison(struct irx_parser *p, PLstr out);
static int parse_concat(struct irx_parser *p, PLstr out);
static int parse_add(struct irx_parser *p, PLstr out);
static int parse_mul(struct irx_parser *p, PLstr out);
static int parse_power(struct irx_parser *p, PLstr out);
static int parse_prefix(struct irx_parser *p, PLstr out);
static int parse_primary(struct irx_parser *p, PLstr out);

/* ------------------------------------------------------------------ */
/*  Compound-tail resolution                                          */
/*                                                                    */
/*  stem.i.j where i=FOO, j=3 -> STEM.FOO.3. The pool is expected     */
/*  to have the stem-default fallback built in.                       */
/* ------------------------------------------------------------------ */

static int resolve_compound_name(struct irx_parser *p,
                                 const struct irx_token *t,
                                 PLstr out)
{
    Lstr part;
    const char *src = t->tok_text;
    int len = (int)t->tok_length;
    int start, i;
    int first_dot = -1;

    Lzeroinit(&part);

    for (i = 0; i < len; i++)
    {
        if (src[i] == '.')
        {
            first_dot = i;
            break;
        }
    }
    if (first_dot < 0)
    {
        /* Not compound after all. */
        return set_upper_from_tok(p->alloc, out, t);
    }

    /* Stem prefix includes the trailing dot. */
    if (lstr_set_bytes(p->alloc, out, src, (size_t)(first_dot + 1)) != LSTR_OK)
    {
        return fail(p, IRXPARS_NOMEM);
    }
    upper_bytes(out->pstr, out->len);

    /* Walk each tail part. */
    start = first_dot + 1;
    while (start <= len)
    {
        int end = start;
        while (end < len && src[end] != '.')
        {
            end++;
        }

        /* Empty tail segment (e.g. "a..b"): treat as literal empty. */
        if (end > start)
        {
            Lstr key, value;
            int has_value = 0;
            int rc;
            Lzeroinit(&key);
            Lzeroinit(&value);

            rc = lstr_set_bytes(p->alloc, &key, src + start,
                                (size_t)(end - start));
            if (rc != LSTR_OK)
            {
                Lfree(p->alloc, &key);
                return fail(p, IRXPARS_NOMEM);
            }
            upper_bytes(key.pstr, key.len);

            /* Numeric tail parts are used as-is (per SC28-1883-0). A
             * tail that is itself a valid symbol gets resolved via
             * the variable pool. */
            if (isalpha(key.pstr[0]) || key.pstr[0] == '_' ||
                key.pstr[0] == '@' || key.pstr[0] == '#' ||
                key.pstr[0] == '$' || key.pstr[0] == '?' ||
                key.pstr[0] == '!')
            {
                rc = vpool_get(p->vpool, &key, &value);
                if (rc == VPOOL_OK)
                {
                    has_value = 1;
                }
            }

            if (has_value)
            {
                if (Lstrcat(p->alloc, out, &value) != LSTR_OK)
                {
                    Lfree(p->alloc, &key);
                    Lfree(p->alloc, &value);
                    return fail(p, IRXPARS_NOMEM);
                }
            }
            else
            {
                /* Uninitialised tail symbol - use its literal name. */
                if (Lstrcat(p->alloc, out, &key) != LSTR_OK)
                {
                    Lfree(p->alloc, &key);
                    Lfree(p->alloc, &value);
                    return fail(p, IRXPARS_NOMEM);
                }
            }
            Lfree(p->alloc, &key);
            Lfree(p->alloc, &value);
        }

        if (end >= len)
        {
            break;
        }
        /* Consume the separator dot into the derived name. */
        if (Lcat(p->alloc, out, ".") != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
        start = end + 1;
    }

    (void)part;
    return IRXPARS_OK;
}

/* Look up a variable (simple or compound). If undefined, returns the
 * upper-case name as per REXX NOVALUE semantics (not an error here -
 * TRAP handling belongs to a later WP). */
static int lookup_variable(struct irx_parser *p,
                           const struct irx_token *t, PLstr out)
{
    Lstr name;
    int rc;

    Lzeroinit(&name);

    if (t->tok_flags & TOKF_COMPOUND)
    {
        rc = resolve_compound_name(p, t, &name);
        if (rc != IRXPARS_OK)
        {
            Lfree(p->alloc, &name);
            return rc;
        }
    }
    else
    {
        if (set_upper_from_tok(p->alloc, &name, t) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
    }

    rc = vpool_get(p->vpool, &name, out);
    if (rc != VPOOL_OK)
    {
        /* NOVALUE - use the derived upper-case name as the value. */
        if (Lstrcpy(p->alloc, out, &name) != LSTR_OK)
        {
            Lfree(p->alloc, &name);
            return fail(p, IRXPARS_NOMEM);
        }
    }

    Lfree(p->alloc, &name);
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  Function call                                                     */
/*                                                                    */
/*  SYMBOL immediately followed by '(' is a function call. The        */
/*  parser has already consumed the SYMBOL; it is the caller's job    */
/*  to detect the pattern. Arguments are comma-separated expressions. */
/* ------------------------------------------------------------------ */

static int parse_function_call(struct irx_parser *p,
                               const struct irx_token *name_tok,
                               PLstr out)
{
    Lstr argvals[IRX_MAX_ARGS];
    PLstr argptrs[IRX_MAX_ARGS];
    int argc = 0;
    int i;
    int rc;
    const struct irx_bif *bif;
    Lstr upname;

    for (i = 0; i < IRX_MAX_ARGS; i++)
    {
        Lzeroinit(&argvals[i]);
        argptrs[i] = &argvals[i];
    }
    Lzeroinit(&upname);

    /* Consume '(' */
    advance_tok(p);

    if (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_RPAREN)
    {
        advance_tok(p);
    }
    else
    {
        for (;;)
        {
            if (argc >= IRX_MAX_ARGS)
            {
                rc = fail(p, IRXPARS_SYNTAX);
                goto done;
            }
            rc = parse_or(p, &argvals[argc]);
            if (rc != IRXPARS_OK)
            {
                goto done;
            }
            argc++;

            if (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_COMMA)
            {
                advance_tok(p);
                continue;
            }
            if (cur_tok(p) != NULL &&
                cur_tok(p)->tok_type == TOK_RPAREN)
            {
                advance_tok(p);
                break;
            }
            rc = fail(p, IRXPARS_SYNTAX);
            goto done;
        }
    }

    if (set_upper_from_tok(p->alloc, &upname, name_tok) != LSTR_OK)
    {
        rc = fail(p, IRXPARS_NOMEM);
        goto done;
    }

    bif = find_bif(upname.pstr, upname.len);
    if (bif == NULL)
    {
        rc = fail(p, IRXPARS_BADFUNC);
        goto done;
    }
    if (argc < bif->bif_min_args || argc > bif->bif_max_args)
    {
        rc = fail(p, IRXPARS_SYNTAX);
        goto done;
    }

    rc = bif->bif_handler(p, argc, argptrs, out);

done:
    Lfree(p->alloc, &upname);
    for (i = 0; i < argc; i++)
    {
        Lfree(p->alloc, &argvals[i]);
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/*  parse_primary: literal, variable, function call, (expression)     */
/* ------------------------------------------------------------------ */

static int parse_primary(struct irx_parser *p, PLstr out)
{
    const struct irx_token *t = cur_tok(p);
    int rc;

    if (t == NULL || tok_ends_clause(t))
    {
        return fail(p, IRXPARS_SYNTAX);
    }

    if (t->tok_type == TOK_LPAREN)
    {
        advance_tok(p);
        rc = parse_or(p, out);
        if (rc != IRXPARS_OK)
        {
            return rc;
        }
        if (cur_tok(p) == NULL ||
            cur_tok(p)->tok_type != TOK_RPAREN)
        {
            return fail(p, IRXPARS_SYNTAX);
        }
        advance_tok(p);
        return IRXPARS_OK;
    }

    if (t->tok_type == TOK_STRING ||
        t->tok_type == TOK_HEXSTRING ||
        t->tok_type == TOK_BINSTRING)
    {
        /* Hex/bin decoding is not required by WP-13 acceptance; we  */
        /* store the raw string body for now. A later WP will decode */
        /* to bytes via Lx2c / Lb2x.                                  */
        if (lstr_set_bytes(p->alloc, out, t->tok_text, t->tok_length) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
        if (t->tok_type == TOK_STRING &&
            (t->tok_flags & TOKF_QUOTE_DBL) != 0)
        {
            dedouble_string(out);
        }
        advance_tok(p);
        return IRXPARS_OK;
    }

    if (t->tok_type == TOK_NUMBER)
    {
        if (lstr_set_bytes(p->alloc, out, t->tok_text, t->tok_length) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
        advance_tok(p);
        return IRXPARS_OK;
    }

    if (t->tok_type == TOK_SYMBOL)
    {
        /* Constant symbol (starts with digit or dot) that is not a  */
        /* valid number becomes a literal string with its own text.  */
        if (t->tok_flags & TOKF_CONSTANT)
        {
            if (lstr_set_bytes(p->alloc, out, t->tok_text, t->tok_length) != LSTR_OK)
            {
                return fail(p, IRXPARS_NOMEM);
            }
            advance_tok(p);
            return IRXPARS_OK;
        }

        /* Function call: SYMBOL immediately followed by '('. */
        const struct irx_token *nxt = peek_tok(p, 1);
        if (nxt != NULL && nxt->tok_type == TOK_LPAREN &&
            toks_adjacent(t, nxt))
        {
            const struct irx_token *name_tok = t;
            advance_tok(p); /* consume SYMBOL */
            return parse_function_call(p, name_tok, out);
        }

        /* Plain variable reference. */
        rc = lookup_variable(p, t, out);
        if (rc != IRXPARS_OK)
        {
            return rc;
        }
        advance_tok(p);
        return IRXPARS_OK;
    }

    return fail(p, IRXPARS_SYNTAX);
}

/* ------------------------------------------------------------------ */
/*  parse_prefix: unary \ + -                                         */
/* ------------------------------------------------------------------ */

static int parse_prefix(struct irx_parser *p, PLstr out)
{
    const struct irx_token *t = cur_tok(p);
    int negate = 0;
    int logical_not = 0;

    while (t != NULL)
    {
        if (t->tok_type == TOK_NOT)
        {
            logical_not = !logical_not;
            advance_tok(p);
        }
        else if (tok_is_op_char(t, TOK_OPERATOR, '+'))
        {
            advance_tok(p);
        }
        else if (tok_is_op_char(t, TOK_OPERATOR, '-'))
        {
            negate = !negate;
            advance_tok(p);
        }
        else
        {
            break;
        }
        t = cur_tok(p);
    }

    int rc = parse_primary(p, out);
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    if (negate)
    {
        long v;
        if (!lstr_to_long(out, &v))
        {
            return fail(p, IRXPARS_SYNTAX);
        }
        if (long_to_lstr(p->alloc, out, -v) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
    }
    if (logical_not)
    {
        long v;
        if (!lstr_to_long(out, &v))
        {
            return fail(p, IRXPARS_SYNTAX);
        }
        if (v != 0 && v != 1)
        {
            return fail(p, IRXPARS_SYNTAX);
        }
        if (long_to_lstr(p->alloc, out, v ? 0 : 1) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
    }
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  parse_power: right-associative **                                 */
/* ------------------------------------------------------------------ */

static int next_is_power(struct irx_parser *p)
{
    const struct irx_token *t0 = peek_tok(p, 0);
    const struct irx_token *t1 = peek_tok(p, 1);
    return tok_is_op_char(t0, TOK_OPERATOR, '*') &&
           tok_is_op_char(t1, TOK_OPERATOR, '*');
}

static int parse_power(struct irx_parser *p, PLstr out)
{
    int rc = parse_prefix(p, out);
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    if (next_is_power(p))
    {
        Lstr rhs;
        long base, exp, result;
        Lzeroinit(&rhs);

        advance_tok(p); /* first  * */
        advance_tok(p); /* second * */

        rc = parse_power(p, &rhs); /* recurse = right-associative */
        if (rc != IRXPARS_OK)
        {
            Lfree(p->alloc, &rhs);
            return rc;
        }

        if (!lstr_to_long(out, &base) || !lstr_to_long(&rhs, &exp))
        {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_SYNTAX);
        }
        Lfree(p->alloc, &rhs);

        if (exp < 0)
        {
            return fail(p, IRXPARS_SYNTAX);
        }
        result = 1;
        while (exp-- > 0)
        {
            result *= base;
        }

        if (long_to_lstr(p->alloc, out, result) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
    }
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  parse_mul: * / // %                                               */
/* ------------------------------------------------------------------ */

static int parse_mul(struct irx_parser *p, PLstr out)
{
    int rc = parse_power(p, out);
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    for (;;)
    {
        const struct irx_token *t0 = cur_tok(p);
        const struct irx_token *t1 = peek_tok(p, 1);
        char op;
        Lstr rhs;
        long a, b, r;

        /* `**` is handled one level up (parse_power recurses back),  */
        /* so we stop here if we see it, to avoid eating the `*`.    */
        if (next_is_power(p))
        {
            break;
        }

        if (tok_is_op_char(t0, TOK_OPERATOR, '*'))
        {
            op = '*';
        }
        else if (tok_is_op_char(t0, TOK_OPERATOR, '/'))
        {
            if (tok_is_op_char(t1, TOK_OPERATOR, '/'))
            {
                op = 'm'; /* // integer remainder */
            }
            else
            {
                op = '/';
            }
        }
        else if (tok_is_op_char(t0, TOK_OPERATOR, '%'))
        {
            op = '%';
        }
        else
        {
            break;
        }

        advance_tok(p);
        if (op == 'm')
        {
            advance_tok(p);
        }

        Lzeroinit(&rhs);
        rc = parse_power(p, &rhs);
        if (rc != IRXPARS_OK)
        {
            Lfree(p->alloc, &rhs);
            return rc;
        }

        if (!lstr_to_long(out, &a) || !lstr_to_long(&rhs, &b))
        {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_SYNTAX);
        }
        Lfree(p->alloc, &rhs);

        switch (op)
        {
            case '*':
                r = a * b;
                break;
            case '/':
                if (b == 0)
                {
                    return fail(p, IRXPARS_DIVZERO);
                }
                r = a / b;
                break;
            case 'm':
                if (b == 0)
                {
                    return fail(p, IRXPARS_DIVZERO);
                }
                r = a - (a / b) * b;
                break;
            case '%':
                if (b == 0)
                {
                    return fail(p, IRXPARS_DIVZERO);
                }
                r = a / b;
                break;
            default:
                return fail(p, IRXPARS_SYNTAX);
        }

        if (long_to_lstr(p->alloc, out, r) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
    }
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  parse_add: + -                                                    */
/* ------------------------------------------------------------------ */

static int parse_add(struct irx_parser *p, PLstr out)
{
    int rc = parse_mul(p, out);
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    for (;;)
    {
        const struct irx_token *t = cur_tok(p);
        char op;
        Lstr rhs;
        long a, b, r;

        if (tok_is_op_char(t, TOK_OPERATOR, '+'))
        {
            op = '+';
        }
        else if (tok_is_op_char(t, TOK_OPERATOR, '-'))
        {
            op = '-';
        }
        else
        {
            break;
        }

        advance_tok(p);

        Lzeroinit(&rhs);
        rc = parse_mul(p, &rhs);
        if (rc != IRXPARS_OK)
        {
            Lfree(p->alloc, &rhs);
            return rc;
        }

        if (!lstr_to_long(out, &a) || !lstr_to_long(&rhs, &b))
        {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_SYNTAX);
        }
        Lfree(p->alloc, &rhs);

        r = (op == '+') ? (a + b) : (a - b);
        if (long_to_lstr(p->alloc, out, r) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
    }
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  parse_concat: || blank abuttal                                    */
/* ------------------------------------------------------------------ */

static int tok_starts_term(const struct irx_token *t)
{
    if (t == NULL)
    {
        return 0;
    }
    switch (t->tok_type)
    {
        case TOK_SYMBOL:
        case TOK_STRING:
        case TOK_NUMBER:
        case TOK_HEXSTRING:
        case TOK_BINSTRING:
        case TOK_LPAREN:
            return 1;
        default:
            return 0;
    }
}

static int parse_concat(struct irx_parser *p, PLstr out)
{
    int rc = parse_add(p, out);
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    for (;;)
    {
        const struct irx_token *t0 = cur_tok(p);
        const struct irx_token *t1 = peek_tok(p, 1);
        const struct irx_token *prev;
        int explicit_concat = 0;
        int blank_concat = 0;
        Lstr rhs;

        if (tok_is_op_char(t0, TOK_LOGICAL, '|') &&
            tok_is_op_char(t1, TOK_LOGICAL, '|'))
        {
            explicit_concat = 1;
        }
        else if (tok_starts_term(t0))
        {
            /* Implicit concat. The left operand's last token lives
             * at tok_pos - 1. If there's a column gap (same line,
             * whitespace between) it's blank concat; otherwise it's
             * an abuttal.
             *
             * Keyword check: a symbol that is a known keyword (THEN,
             * ELSE, WHEN, DO, END, etc.) cannot start a concatenated
             * term — it is a clause-level delimiter, not an operand. */
            if (t0->tok_type == TOK_SYMBOL)
            {
                /* If the symbol is immediately followed by '(' it is a
                 * function call, not a keyword instruction — do not stop. */
                const struct irx_token *tnxt = peek_tok(p, 1);
                int is_func = (tnxt != NULL &&
                               tnxt->tok_type == TOK_LPAREN &&
                               toks_adjacent(t0, tnxt));
                if (!is_func)
                {
                    char kbuf[32];
                    int kn = (int)t0->tok_length;
                    if (kn < (int)sizeof(kbuf))
                    {
                        memcpy(kbuf, t0->tok_text, (size_t)kn);
                        kbuf[kn] = '\0';
                        upper_bytes((unsigned char *)kbuf, (size_t)kn);
                        if (find_keyword((unsigned char *)kbuf,
                                         (size_t)kn) != NULL)
                        {
                            break; /* keyword: stop concatenation */
                        }
                    }
                }
            }
            prev = peek_tok(p, -1);
            if (prev == NULL || prev->tok_line != t0->tok_line)
            {
                break;
            }
            if (toks_adjacent(prev, t0))
            {
                blank_concat = 0;
            }
            else
            {
                blank_concat = 1;
            }
        }
        else
        {
            break;
        }

        if (explicit_concat)
        {
            advance_tok(p);
            advance_tok(p);
        }

        Lzeroinit(&rhs);
        rc = parse_add(p, &rhs);
        if (rc != IRXPARS_OK)
        {
            Lfree(p->alloc, &rhs);
            return rc;
        }

        if (blank_concat)
        {
            if (Lcat(p->alloc, out, " ") != LSTR_OK)
            {
                Lfree(p->alloc, &rhs);
                return fail(p, IRXPARS_NOMEM);
            }
        }
        if (Lstrcat(p->alloc, out, &rhs) != LSTR_OK)
        {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_NOMEM);
        }
        Lfree(p->alloc, &rhs);
    }
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  parse_comparison: = == \= > < >= <= \> \< etc.                    */
/* ------------------------------------------------------------------ */

/* Operator codes for the comparison layer. */
#define CMP_EQ  1 /* =   */
#define CMP_NE  2 /* \=  */
#define CMP_EQS 3 /* ==  */
#define CMP_NES 4 /* \== */
#define CMP_GT  5 /* >   */
#define CMP_LT  6 /* <   */
#define CMP_GE  7 /* >=  */
#define CMP_LE  8 /* <=  */

static int match_comparison(struct irx_parser *p, int *out_op)
{
    const struct irx_token *t0 = peek_tok(p, 0);
    const struct irx_token *t1 = peek_tok(p, 1);
    const struct irx_token *t2 = peek_tok(p, 2);

    if (t0 == NULL)
    {
        return 0;
    }

    if (t0->tok_type == TOK_NOT)
    {
        if (tok_is_op_char(t1, TOK_COMPARISON, '='))
        {
            if (tok_is_op_char(t2, TOK_COMPARISON, '='))
            {
                *out_op = CMP_NES;
                advance_tok(p);
                advance_tok(p);
                advance_tok(p);
                return 1;
            }
            *out_op = CMP_NE;
            advance_tok(p);
            advance_tok(p);
            return 1;
        }
        return 0;
    }

    if (tok_is_op_char(t0, TOK_COMPARISON, '='))
    {
        if (tok_is_op_char(t1, TOK_COMPARISON, '='))
        {
            *out_op = CMP_EQS;
            advance_tok(p);
            advance_tok(p);
            return 1;
        }
        *out_op = CMP_EQ;
        advance_tok(p);
        return 1;
    }

    if (tok_is_op_char(t0, TOK_COMPARISON, '>'))
    {
        if (tok_is_op_char(t1, TOK_COMPARISON, '='))
        {
            *out_op = CMP_GE;
            advance_tok(p);
            advance_tok(p);
            return 1;
        }
        *out_op = CMP_GT;
        advance_tok(p);
        return 1;
    }

    if (tok_is_op_char(t0, TOK_COMPARISON, '<'))
    {
        if (tok_is_op_char(t1, TOK_COMPARISON, '='))
        {
            *out_op = CMP_LE;
            advance_tok(p);
            advance_tok(p);
            return 1;
        }
        *out_op = CMP_LT;
        advance_tok(p);
        return 1;
    }

    return 0;
}

static int parse_comparison(struct irx_parser *p, PLstr out)
{
    int rc = parse_concat(p, out);
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    int op;
    Lstr rhs;
    int c, result;

    if (!match_comparison(p, &op))
    {
        return IRXPARS_OK;
    }

    Lzeroinit(&rhs);
    rc = parse_concat(p, &rhs);
    if (rc != IRXPARS_OK)
    {
        Lfree(p->alloc, &rhs);
        return rc;
    }

    if (op == CMP_EQS || op == CMP_NES)
    {
        c = compare_strict(out, &rhs);
    }
    else
    {
        c = compare_normal(out, &rhs);
    }
    Lfree(p->alloc, &rhs);

    switch (op)
    {
        case CMP_EQ:
            result = (c == 0);
            break;
        case CMP_NE:
            result = (c != 0);
            break;
        case CMP_EQS:
            result = (c == 0);
            break;
        case CMP_NES:
            result = (c != 0);
            break;
        case CMP_GT:
            result = (c > 0);
            break;
        case CMP_LT:
            result = (c < 0);
            break;
        case CMP_GE:
            result = (c >= 0);
            break;
        case CMP_LE:
            result = (c <= 0);
            break;
        default:
            result = 0;
            break;
    }

    if (long_to_lstr(p->alloc, out, (long)result) != LSTR_OK)
    {
        return fail(p, IRXPARS_NOMEM);
    }
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  parse_and: &                                                      */
/*  parse_or : | &&                                                   */
/* ------------------------------------------------------------------ */

static int as_bool(PLstr s, int *out)
{
    long v;
    if (!lstr_to_long(s, &v))
    {
        return 0;
    }
    if (v != 0 && v != 1)
    {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int parse_and(struct irx_parser *p, PLstr out)
{
    int rc = parse_comparison(p, out);
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    for (;;)
    {
        const struct irx_token *t0 = peek_tok(p, 0);
        const struct irx_token *t1 = peek_tok(p, 1);
        Lstr rhs;
        int a, b;

        /* `&&` is XOR at the parse_or level, so we require the next
         * token NOT to be another `&`. */
        if (!tok_is_op_char(t0, TOK_LOGICAL, '&'))
        {
            break;
        }
        if (tok_is_op_char(t1, TOK_LOGICAL, '&'))
        {
            break;
        }

        advance_tok(p);

        Lzeroinit(&rhs);
        rc = parse_comparison(p, &rhs);
        if (rc != IRXPARS_OK)
        {
            Lfree(p->alloc, &rhs);
            return rc;
        }

        if (!as_bool(out, &a) || !as_bool(&rhs, &b))
        {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_SYNTAX);
        }
        Lfree(p->alloc, &rhs);

        if (long_to_lstr(p->alloc, out, (long)(a && b)) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
    }
    return IRXPARS_OK;
}

static int parse_or(struct irx_parser *p, PLstr out)
{
    int rc = parse_and(p, out);
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    for (;;)
    {
        const struct irx_token *t0 = peek_tok(p, 0);
        const struct irx_token *t1 = peek_tok(p, 1);
        int is_or = 0;
        int is_xor = 0;
        Lstr rhs;
        int a, b, r;

        if (tok_is_op_char(t0, TOK_LOGICAL, '|') &&
            !tok_is_op_char(t1, TOK_LOGICAL, '|'))
        {
            is_or = 1;
        }
        else if (tok_is_op_char(t0, TOK_LOGICAL, '&') &&
                 tok_is_op_char(t1, TOK_LOGICAL, '&'))
        {
            is_xor = 1;
        }
        else
        {
            break;
        }

        if (is_xor)
        {
            advance_tok(p);
            advance_tok(p);
        }
        else
        {
            advance_tok(p);
        }
        (void)is_or;

        Lzeroinit(&rhs);
        rc = parse_and(p, &rhs);
        if (rc != IRXPARS_OK)
        {
            Lfree(p->alloc, &rhs);
            return rc;
        }

        if (!as_bool(out, &a) || !as_bool(&rhs, &b))
        {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_SYNTAX);
        }
        Lfree(p->alloc, &rhs);

        r = is_xor ? (a ^ b) : (a | b);
        if (long_to_lstr(p->alloc, out, (long)r) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
    }
    return IRXPARS_OK;
}

/* ================================================================== */
/*  Clause handlers                                                   */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/*  Assignment: SYMBOL = expr                                         */
/* ------------------------------------------------------------------ */

static int exec_assignment(struct irx_parser *p)
{
    const struct irx_token *name_tok = cur_tok(p);
    Lstr name, value;
    int rc;

    Lzeroinit(&name);
    Lzeroinit(&value);

    if (name_tok->tok_flags & TOKF_COMPOUND)
    {
        rc = resolve_compound_name(p, name_tok, &name);
        if (rc != IRXPARS_OK)
        {
            goto done;
        }
    }
    else
    {
        if (set_upper_from_tok(p->alloc, &name, name_tok) != LSTR_OK)
        {
            rc = fail(p, IRXPARS_NOMEM);
            goto done;
        }
    }

    advance_tok(p); /* SYMBOL */
    advance_tok(p); /* =      */

    rc = irx_pars_eval_expr(p, &value);
    if (rc != IRXPARS_OK)
    {
        goto done;
    }

    if (vpool_set(p->vpool, &name, &value) != VPOOL_OK)
    {
        rc = fail(p, IRXPARS_NOMEM);
        goto done;
    }

    rc = IRXPARS_OK;

done:
    Lfree(p->alloc, &name);
    Lfree(p->alloc, &value);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Command clause: evaluate the expression and discard the result.   */
/*  (A later WP will actually forward it to the host environment.)    */
/* ------------------------------------------------------------------ */

static int exec_command(struct irx_parser *p)
{
    Lstr tmp;
    int rc;

    Lzeroinit(&tmp);
    rc = irx_pars_eval_expr(p, &tmp);
    Lfree(p->alloc, &tmp);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Clause classification                                             */
/* ------------------------------------------------------------------ */

/* Clear the procedure_allowed flag on the topmost CALL frame.
 * Called before any executable clause other than PROCEDURE. */
static void proc_allowed_clear(struct irx_parser *p)
{
    struct irx_exec_frame *f = irx_ctrl_frame_top(p);
    if (f != NULL && f->frame_type == FRAME_CALL)
    {
        f->procedure_allowed = 0;
    }
}

static int is_assignment_here(struct irx_parser *p)
{
    const struct irx_token *t0 = peek_tok(p, 0);
    const struct irx_token *t1 = peek_tok(p, 1);
    const struct irx_token *t2 = peek_tok(p, 2);

    if (t0 == NULL || t0->tok_type != TOK_SYMBOL)
    {
        return 0;
    }
    if (t0->tok_flags & TOKF_CONSTANT)
    {
        return 0;
    }
    if (!tok_is_op_char(t1, TOK_COMPARISON, '='))
    {
        return 0;
    }
    /* Exclude `==` which is strict comparison, not assignment. */
    if (tok_is_op_char(t2, TOK_COMPARISON, '='))
    {
        return 0;
    }
    return 1;
}

static int exec_clause(struct irx_parser *p)
{
    const struct irx_token *t0;

    /* Skip empty clauses. */
    while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC)
    {
        advance_tok(p);
    }

    t0 = cur_tok(p);
    if (t0 == NULL || t0->tok_type == TOK_EOF)
    {
        return IRXPARS_OK;
    }

    /* Null clause (just a semicolon) also does nothing. */
    if (t0->tok_type == TOK_SEMICOLON)
    {
        advance_tok(p);
        return IRXPARS_OK;
    }

    /* Rule 1: assignment. */
    if (is_assignment_here(p))
    {
        proc_allowed_clear(p);
        return exec_assignment(p);
    }

    /* Rule 2: label - SYMBOL followed by SEMICOLON (the colon). */
    if (t0->tok_type == TOK_SYMBOL)
    {
        const struct irx_token *t1 = peek_tok(p, 1);
        if (t1 != NULL && t1->tok_type == TOK_SEMICOLON &&
            t1->tok_length == 1 && t1->tok_text[0] == ':')
        {
            /* Label declaration: record in exec_stack->last_label so
             * the immediately following DO can associate it.
             * Labels do NOT clear procedure_allowed. */
            struct irx_exec_stack *es =
                (struct irx_exec_stack *)p->exec_stack;
            if (es != NULL)
            {
                es->last_label_len = sym_to_upper(
                    t0, es->last_label, CTRL_NAME_MAX);
            }
            advance_tok(p);
            advance_tok(p);
            return IRXPARS_OK;
        }

        /* Rule 3: keyword instruction. */
        Lstr upname;
        const struct irx_keyword *kw;
        Lzeroinit(&upname);
        if (set_upper_from_tok(p->alloc, &upname, t0) != LSTR_OK)
        {
            return fail(p, IRXPARS_NOMEM);
        }
        kw = find_keyword(upname.pstr, upname.len);
        Lfree(p->alloc, &upname);
        if (kw != NULL)
        {
            /* PROCEDURE is allowed only as first executable clause
             * in a subroutine; it checks procedure_allowed itself.
             * All other keywords clear the flag before executing. */
            if (kw->kw_handler != kw_procedure)
            {
                proc_allowed_clear(p);
            }
            advance_tok(p);
            return kw->kw_handler(p);
        }
    }

    /* Rule 4: command. */
    proc_allowed_clear(p);
    return exec_command(p);
}

/* ================================================================== */
/*  Public entry points                                               */
/* ================================================================== */

int irx_pars_init(struct irx_parser *p,
                  struct irx_token *tokens, int tok_count,
                  struct irx_vpool *vpool,
                  struct lstr_alloc *alloc,
                  struct envblock *envblock)
{
    int rc;
    if (p == NULL || tokens == NULL || vpool == NULL || alloc == NULL)
    {
        return IRXPARS_BADARG;
    }
    memset(p, 0, sizeof(*p));
    p->tokens = tokens;
    p->tok_count = tok_count;
    p->tok_pos = 0;
    p->vpool = vpool;
    p->alloc = alloc;
    p->envblock = envblock;
    Lzeroinit(&p->result);
    p->error_code = IRXPARS_OK;
    p->error_line = 0;

    rc = irx_ctrl_init(p);
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    return IRXPARS_OK;
}

void irx_pars_cleanup(struct irx_parser *p)
{
    if (p == NULL)
    {
        return;
    }
    irx_ctrl_cleanup(p);
    Lfree(p->alloc, &p->result);

    /* Free call_args if any remain (WP-17). */
    if (p->alloc != NULL && p->call_args != NULL)
    {
        int i;
        for (i = 0; i < p->call_argc; i++)
        {
            Lfree(p->alloc, &p->call_args[i]);
        }
        p->alloc->dealloc(p->call_args,
                          (size_t)IRX_MAX_ARGS * sizeof(Lstr),
                          p->alloc->ctx);
        p->call_args = NULL;
    }
    if (p->alloc != NULL && p->call_arg_exists != NULL)
    {
        p->alloc->dealloc(p->call_arg_exists,
                          (size_t)IRX_MAX_ARGS * sizeof(int),
                          p->alloc->ctx);
        p->call_arg_exists = NULL;
    }
}

int irx_pars_eval_expr(struct irx_parser *p, PLstr out)
{
    if (p == NULL || out == NULL)
    {
        return IRXPARS_BADARG;
    }
    return parse_or(p, out);
}

int irx_pars_run(struct irx_parser *p)
{
    int rc;
    if (p == NULL)
    {
        return IRXPARS_BADARG;
    }

    /* First pass: build the label table for CALL/SIGNAL. */
    rc = irx_ctrl_label_scan(p);
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    while (p->tok_pos < p->tok_count)
    {
        const struct irx_token *t = cur_tok(p);
        if (t == NULL || t->tok_type == TOK_EOF)
        {
            break;
        }

        rc = exec_clause(p);
        if (rc != IRXPARS_OK)
        {
            return rc;
        }

        if (p->exit_requested)
        {
            break;
        }

        /* Consume trailing clause terminators. */
        while (cur_tok(p) != NULL &&
               (cur_tok(p)->tok_type == TOK_EOC ||
                cur_tok(p)->tok_type == TOK_SEMICOLON))
        {
            advance_tok(p);
        }
    }
    return IRXPARS_OK;
}
