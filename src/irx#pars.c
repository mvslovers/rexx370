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

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "irx.h"
#include "irxwkblk.h"
#include "lstring.h"
#include "lstralloc.h"
#include "irxlstr.h"
#include "irxtokn.h"
#include "irxvpool.h"
#include "irxpars.h"

/* ------------------------------------------------------------------ */
/*  Lstr scratch helpers                                              */
/* ------------------------------------------------------------------ */

static int lstr_set_bytes(struct lstr_alloc *a, PLstr s,
                          const char *buf, size_t len)
{
    int rc = Lfx(a, s, len);
    if (rc != LSTR_OK) return rc;
    if (len > 0) memcpy(s->pstr, buf, len);
    s->len  = len;
    s->type = LSTRING_TY;   /* invalidate any cached numeric type */
    return LSTR_OK;
}

/* ------------------------------------------------------------------ */
/*  Error plumbing                                                    */
/* ------------------------------------------------------------------ */

static int fail(struct irx_parser *p, int code)
{
    if (p->error_code == IRXPARS_OK) {
        p->error_code = code;
        if (p->tok_pos < p->tok_count) {
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
    if (idx < 0 || idx >= p->tok_count) return NULL;
    return &p->tokens[idx];
}

static const struct irx_token *cur_tok(struct irx_parser *p)
{
    return peek_tok(p, 0);
}

static void advance_tok(struct irx_parser *p)
{
    if (p->tok_pos < p->tok_count) p->tok_pos++;
}

static int tok_is_op_char(const struct irx_token *t, unsigned char type,
                          char ch)
{
    if (t == NULL) return 0;
    if (t->tok_type != type) return 0;
    if (t->tok_length != 1) return 0;
    return t->tok_text[0] == ch;
}

static int tok_ends_clause(const struct irx_token *t)
{
    if (t == NULL) return 1;
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
    switch (t->tok_type) {
    case TOK_STRING:    return end + 2;
    case TOK_HEXSTRING: return end + 3;
    case TOK_BINSTRING: return end + 3;
    default:            return end;
    }
}

/* Two tokens are "adjacent in source" if there is no whitespace gap
 * between them (same line, no column gap). Used for both function
 * call detection (SYMBOL immediately followed by '(') and for the
 * abuttal vs. blank concat decision. */
static int toks_adjacent(const struct irx_token *a,
                         const struct irx_token *b)
{
    if (a == NULL || b == NULL) return 0;
    if (a->tok_line != b->tok_line) return 0;
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

    for (i = 0; i + 1 < s->len; i++) {
        if (s->pstr[i] == '\'' && s->pstr[i + 1] == '\'') saw_single = 1;
        if (s->pstr[i] == '"'  && s->pstr[i + 1] == '"')  saw_double = 1;
    }
    if (saw_double && !saw_single) pair = '"';

    for (i = 0; i < s->len; ) {
        if (i + 1 < s->len && s->pstr[i] == pair &&
            s->pstr[i + 1] == pair) {
            s->pstr[out++] = pair;
            i += 2;
        } else {
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
    for (i = 0; i < n; i++) {
        int c = p[i];
        if (islower(c)) p[i] = (unsigned char)toupper(c);
    }
}

/* Copy token text into dst as upper-case. */
static int set_upper_from_tok(struct lstr_alloc *a, PLstr dst,
                              const struct irx_token *t)
{
    int rc = lstr_set_bytes(a, dst, t->tok_text, t->tok_length);
    if (rc != LSTR_OK) return rc;
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

    if (_Lisnum(s) == LNUM_NOT_NUM) return 0;
    /* Strip leading/trailing blanks into a local null-terminated copy. */
    first = 0;
    while (first < s->len && (s->pstr[first] == ' ' ||
                              s->pstr[first] == '\t')) first++;
    last = s->len;
    while (last > first && (s->pstr[last - 1] == ' ' ||
                            s->pstr[last - 1] == '\t')) last--;
    if (last - first >= sizeof(buf)) return 0;
    for (i = 0; i < last - first; i++) buf[i] = (char)s->pstr[first + i];
    buf[last - first] = '\0';

    v = strtol(buf, &end, 10);
    if (*end != '\0') {
        /* Allow the REAL form produced by _Lisnum by reparsing with
         * strtod - but Phase 2 stores integers, so fall back to
         * integer part via double cast. */
        double d = strtod(buf, &end);
        if (*end != '\0') return 0;
        v = (long)d;
    }
    *out = v;
    return 1;
}

static int long_to_lstr(struct lstr_alloc *a, PLstr dst, long v)
{
    char buf[32];
    int n = sprintf(buf, "%ld", v);
    if (n < 0) return LSTR_ERR_NOMEM;
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
    if (n > 0) {
        cmp = memcmp(a->pstr, b->pstr, n);
        if (cmp != 0) return cmp < 0 ? -1 : 1;
    }
    if (a->len < b->len) return -1;
    if (a->len > b->len) return 1;
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
    if (anum && bnum) {
        if (la < lb) return -1;
        if (la > lb) return 1;
        return 0;
    }

    /* String comparison with blank-pad. Case-sensitive per
     * SC28-1883-0 Chapter 7. Trailing blanks on either side are
     * dropped first; any remaining length difference is then
     * padded with spaces on the right. */
    la_len = a->len;
    while (la_len > 0 && a->pstr[la_len - 1] == ' ') la_len--;
    lb_len = b->len;
    while (lb_len > 0 && b->pstr[lb_len - 1] == ' ') lb_len--;

    max_len = la_len > lb_len ? la_len : lb_len;
    for (i = 0; i < max_len; i++) {
        ca = (unsigned char)(i < la_len ? a->pstr[i] : ' ');
        cb = (unsigned char)(i < lb_len ? b->pstr[i] : ' ');
        if (ca < cb) return -1;
        if (ca > cb) return 1;
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

static const struct irx_bif g_bif_table[] = {
    { "LENGTH", 1, 1, bif_length }
};
static const int g_bif_count = (int)(sizeof(g_bif_table) /
                                     sizeof(g_bif_table[0]));

static const struct irx_bif *find_bif(const unsigned char *name, size_t len)
{
    int i;
    for (i = 0; i < g_bif_count; i++) {
        const char *bn = g_bif_table[i].bif_name;
        size_t bl = strlen(bn);
        if (bl == len && memcmp(bn, name, len) == 0) {
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

    if (tok_ends_clause(cur_tok(p))) {
        /* SAY with no expression -> output an empty line */
        rc = Lfx(p->alloc, &result, 0);
        if (rc != LSTR_OK) {
            return fail(p, IRXPARS_NOMEM);
        }
        result.len = 0;
    } else {
        rc = irx_pars_eval_expr(p, &result);
        if (rc != IRXPARS_OK) {
            Lfree(p->alloc, &result);
            return rc;
        }
    }

    if (p->envblock != NULL) {
        exte = (struct irxexte *)p->envblock->envblock_irxexte;
        if (exte != NULL && exte->io_routine != NULL) {
            io_fn = (int (*)(int, PLstr, struct envblock *))exte->io_routine;
            io_fn(RXFWRITE, &result, p->envblock);
        }
    }

    Lfree(p->alloc, &result);
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
    { "SAY", kw_say },
    { NULL,  NULL   }
};

static const struct irx_keyword *find_keyword(const unsigned char *name,
                                              size_t len)
{
    int i;
    for (i = 0; g_keyword_table[i].kw_name != NULL; i++) {
        const char *kn = g_keyword_table[i].kw_name;
        size_t kl = strlen(kn);
        if (kl == len && memcmp(kn, name, len) == 0) {
            return &g_keyword_table[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                              */
/* ------------------------------------------------------------------ */

static int parse_or        (struct irx_parser *p, PLstr out);
static int parse_and       (struct irx_parser *p, PLstr out);
static int parse_comparison(struct irx_parser *p, PLstr out);
static int parse_concat    (struct irx_parser *p, PLstr out);
static int parse_add       (struct irx_parser *p, PLstr out);
static int parse_mul       (struct irx_parser *p, PLstr out);
static int parse_power     (struct irx_parser *p, PLstr out);
static int parse_prefix    (struct irx_parser *p, PLstr out);
static int parse_primary   (struct irx_parser *p, PLstr out);

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

    for (i = 0; i < len; i++) {
        if (src[i] == '.') { first_dot = i; break; }
    }
    if (first_dot < 0) {
        /* Not compound after all. */
        return set_upper_from_tok(p->alloc, out, t);
    }

    /* Stem prefix includes the trailing dot. */
    if (lstr_set_bytes(p->alloc, out, src, (size_t)(first_dot + 1))
        != LSTR_OK) {
        return fail(p, IRXPARS_NOMEM);
    }
    upper_bytes(out->pstr, out->len);

    /* Walk each tail part. */
    start = first_dot + 1;
    while (start <= len) {
        int end = start;
        while (end < len && src[end] != '.') end++;

        /* Empty tail segment (e.g. "a..b"): treat as literal empty. */
        if (end > start) {
            Lstr key, value;
            int has_value = 0;
            int rc;
            Lzeroinit(&key);
            Lzeroinit(&value);

            rc = lstr_set_bytes(p->alloc, &key, src + start,
                                (size_t)(end - start));
            if (rc != LSTR_OK) {
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
                key.pstr[0] == '!') {
                rc = vpool_get(p->vpool, &key, &value);
                if (rc == VPOOL_OK) has_value = 1;
            }

            if (has_value) {
                if (Lstrcat(p->alloc, out, &value) != LSTR_OK) {
                    Lfree(p->alloc, &key);
                    Lfree(p->alloc, &value);
                    return fail(p, IRXPARS_NOMEM);
                }
            } else {
                /* Uninitialised tail symbol - use its literal name. */
                if (Lstrcat(p->alloc, out, &key) != LSTR_OK) {
                    Lfree(p->alloc, &key);
                    Lfree(p->alloc, &value);
                    return fail(p, IRXPARS_NOMEM);
                }
            }
            Lfree(p->alloc, &key);
            Lfree(p->alloc, &value);
        }

        if (end >= len) break;
        /* Consume the separator dot into the derived name. */
        if (Lcat(p->alloc, out, ".") != LSTR_OK) {
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

    if (t->tok_flags & TOKF_COMPOUND) {
        rc = resolve_compound_name(p, t, &name);
        if (rc != IRXPARS_OK) {
            Lfree(p->alloc, &name);
            return rc;
        }
    } else {
        if (set_upper_from_tok(p->alloc, &name, t) != LSTR_OK) {
            return fail(p, IRXPARS_NOMEM);
        }
    }

    rc = vpool_get(p->vpool, &name, out);
    if (rc != VPOOL_OK) {
        /* NOVALUE - use the derived upper-case name as the value. */
        if (Lstrcpy(p->alloc, out, &name) != LSTR_OK) {
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

#define IRX_MAX_ARGS 16

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

    for (i = 0; i < IRX_MAX_ARGS; i++) {
        Lzeroinit(&argvals[i]);
        argptrs[i] = &argvals[i];
    }
    Lzeroinit(&upname);

    /* Consume '(' */
    advance_tok(p);

    if (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_RPAREN) {
        advance_tok(p);
    } else {
        for (;;) {
            if (argc >= IRX_MAX_ARGS) {
                rc = fail(p, IRXPARS_SYNTAX);
                goto done;
            }
            rc = parse_or(p, &argvals[argc]);
            if (rc != IRXPARS_OK) goto done;
            argc++;

            if (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_COMMA) {
                advance_tok(p);
                continue;
            }
            if (cur_tok(p) != NULL &&
                cur_tok(p)->tok_type == TOK_RPAREN) {
                advance_tok(p);
                break;
            }
            rc = fail(p, IRXPARS_SYNTAX);
            goto done;
        }
    }

    if (set_upper_from_tok(p->alloc, &upname, name_tok) != LSTR_OK) {
        rc = fail(p, IRXPARS_NOMEM);
        goto done;
    }

    bif = find_bif(upname.pstr, upname.len);
    if (bif == NULL) {
        rc = fail(p, IRXPARS_BADFUNC);
        goto done;
    }
    if (argc < bif->bif_min_args || argc > bif->bif_max_args) {
        rc = fail(p, IRXPARS_SYNTAX);
        goto done;
    }

    rc = bif->bif_handler(p, argc, argptrs, out);

done:
    Lfree(p->alloc, &upname);
    for (i = 0; i < argc; i++) Lfree(p->alloc, &argvals[i]);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  parse_primary: literal, variable, function call, (expression)     */
/* ------------------------------------------------------------------ */

static int parse_primary(struct irx_parser *p, PLstr out)
{
    const struct irx_token *t = cur_tok(p);
    int rc;

    if (t == NULL || tok_ends_clause(t)) {
        return fail(p, IRXPARS_SYNTAX);
    }

    if (t->tok_type == TOK_LPAREN) {
        advance_tok(p);
        rc = parse_or(p, out);
        if (rc != IRXPARS_OK) return rc;
        if (cur_tok(p) == NULL ||
            cur_tok(p)->tok_type != TOK_RPAREN) {
            return fail(p, IRXPARS_SYNTAX);
        }
        advance_tok(p);
        return IRXPARS_OK;
    }

    if (t->tok_type == TOK_STRING ||
        t->tok_type == TOK_HEXSTRING ||
        t->tok_type == TOK_BINSTRING) {
        /* Hex/bin decoding is not required by WP-13 acceptance; we  */
        /* store the raw string body for now. A later WP will decode */
        /* to bytes via Lx2c / Lb2x.                                  */
        if (lstr_set_bytes(p->alloc, out, t->tok_text, t->tok_length)
            != LSTR_OK) return fail(p, IRXPARS_NOMEM);
        if (t->tok_type == TOK_STRING &&
            (t->tok_flags & TOKF_QUOTE_DBL) != 0) {
            dedouble_string(out);
        }
        advance_tok(p);
        return IRXPARS_OK;
    }

    if (t->tok_type == TOK_NUMBER) {
        if (lstr_set_bytes(p->alloc, out, t->tok_text, t->tok_length)
            != LSTR_OK) return fail(p, IRXPARS_NOMEM);
        advance_tok(p);
        return IRXPARS_OK;
    }

    if (t->tok_type == TOK_SYMBOL) {
        /* Constant symbol (starts with digit or dot) that is not a  */
        /* valid number becomes a literal string with its own text.  */
        if (t->tok_flags & TOKF_CONSTANT) {
            if (lstr_set_bytes(p->alloc, out, t->tok_text, t->tok_length)
                != LSTR_OK) return fail(p, IRXPARS_NOMEM);
            advance_tok(p);
            return IRXPARS_OK;
        }

        /* Function call: SYMBOL immediately followed by '('. */
        {
            const struct irx_token *nxt = peek_tok(p, 1);
            if (nxt != NULL && nxt->tok_type == TOK_LPAREN &&
                toks_adjacent(t, nxt)) {
                const struct irx_token *name_tok = t;
                advance_tok(p);   /* consume SYMBOL */
                return parse_function_call(p, name_tok, out);
            }
        }

        /* Plain variable reference. */
        rc = lookup_variable(p, t, out);
        if (rc != IRXPARS_OK) return rc;
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

    while (t != NULL) {
        if (t->tok_type == TOK_NOT) {
            logical_not = !logical_not;
            advance_tok(p);
        } else if (tok_is_op_char(t, TOK_OPERATOR, '+')) {
            advance_tok(p);
        } else if (tok_is_op_char(t, TOK_OPERATOR, '-')) {
            negate = !negate;
            advance_tok(p);
        } else {
            break;
        }
        t = cur_tok(p);
    }

    {
        int rc = parse_primary(p, out);
        if (rc != IRXPARS_OK) return rc;

        if (negate) {
            long v;
            if (!lstr_to_long(out, &v)) return fail(p, IRXPARS_SYNTAX);
            if (long_to_lstr(p->alloc, out, -v) != LSTR_OK) {
                return fail(p, IRXPARS_NOMEM);
            }
        }
        if (logical_not) {
            long v;
            if (!lstr_to_long(out, &v)) return fail(p, IRXPARS_SYNTAX);
            if (v != 0 && v != 1) return fail(p, IRXPARS_SYNTAX);
            if (long_to_lstr(p->alloc, out, v ? 0 : 1) != LSTR_OK) {
                return fail(p, IRXPARS_NOMEM);
            }
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
    if (rc != IRXPARS_OK) return rc;

    if (next_is_power(p)) {
        Lstr rhs;
        long base, exp, result;
        Lzeroinit(&rhs);

        advance_tok(p);   /* first  * */
        advance_tok(p);   /* second * */

        rc = parse_power(p, &rhs);   /* recurse = right-associative */
        if (rc != IRXPARS_OK) { Lfree(p->alloc, &rhs); return rc; }

        if (!lstr_to_long(out, &base) || !lstr_to_long(&rhs, &exp)) {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_SYNTAX);
        }
        Lfree(p->alloc, &rhs);

        if (exp < 0) return fail(p, IRXPARS_SYNTAX);
        result = 1;
        while (exp-- > 0) result *= base;

        if (long_to_lstr(p->alloc, out, result) != LSTR_OK) {
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
    if (rc != IRXPARS_OK) return rc;

    for (;;) {
        const struct irx_token *t0 = cur_tok(p);
        const struct irx_token *t1 = peek_tok(p, 1);
        char op;
        Lstr rhs;
        long a, b, r;

        /* `**` is handled one level up (parse_power recurses back),  */
        /* so we stop here if we see it, to avoid eating the `*`.    */
        if (next_is_power(p)) break;

        if (tok_is_op_char(t0, TOK_OPERATOR, '*')) op = '*';
        else if (tok_is_op_char(t0, TOK_OPERATOR, '/')) {
            if (tok_is_op_char(t1, TOK_OPERATOR, '/')) {
                op = 'm';   /* // integer remainder */
            } else {
                op = '/';
            }
        }
        else if (tok_is_op_char(t0, TOK_OPERATOR, '%')) op = '%';
        else break;

        advance_tok(p);
        if (op == 'm') advance_tok(p);

        Lzeroinit(&rhs);
        rc = parse_power(p, &rhs);
        if (rc != IRXPARS_OK) { Lfree(p->alloc, &rhs); return rc; }

        if (!lstr_to_long(out, &a) || !lstr_to_long(&rhs, &b)) {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_SYNTAX);
        }
        Lfree(p->alloc, &rhs);

        switch (op) {
        case '*': r = a * b; break;
        case '/':
            if (b == 0) return fail(p, IRXPARS_DIVZERO);
            r = a / b;
            break;
        case 'm':
            if (b == 0) return fail(p, IRXPARS_DIVZERO);
            r = a - (a / b) * b;
            break;
        case '%':
            if (b == 0) return fail(p, IRXPARS_DIVZERO);
            r = a / b;
            break;
        default:
            return fail(p, IRXPARS_SYNTAX);
        }

        if (long_to_lstr(p->alloc, out, r) != LSTR_OK) {
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
    if (rc != IRXPARS_OK) return rc;

    for (;;) {
        const struct irx_token *t = cur_tok(p);
        char op;
        Lstr rhs;
        long a, b, r;

        if (tok_is_op_char(t, TOK_OPERATOR, '+')) op = '+';
        else if (tok_is_op_char(t, TOK_OPERATOR, '-')) op = '-';
        else break;

        advance_tok(p);

        Lzeroinit(&rhs);
        rc = parse_mul(p, &rhs);
        if (rc != IRXPARS_OK) { Lfree(p->alloc, &rhs); return rc; }

        if (!lstr_to_long(out, &a) || !lstr_to_long(&rhs, &b)) {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_SYNTAX);
        }
        Lfree(p->alloc, &rhs);

        r = (op == '+') ? (a + b) : (a - b);
        if (long_to_lstr(p->alloc, out, r) != LSTR_OK) {
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
    if (t == NULL) return 0;
    switch (t->tok_type) {
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
    if (rc != IRXPARS_OK) return rc;

    for (;;) {
        const struct irx_token *t0 = cur_tok(p);
        const struct irx_token *t1 = peek_tok(p, 1);
        const struct irx_token *prev;
        int explicit_concat = 0;
        int blank_concat = 0;
        Lstr rhs;

        if (tok_is_op_char(t0, TOK_LOGICAL, '|') &&
            tok_is_op_char(t1, TOK_LOGICAL, '|')) {
            explicit_concat = 1;
        } else if (tok_starts_term(t0)) {
            /* Implicit concat. The left operand's last token lives
             * at tok_pos - 1. If there's a column gap (same line,
             * whitespace between) it's blank concat; otherwise it's
             * an abuttal. */
            prev = peek_tok(p, -1);
            if (prev == NULL || prev->tok_line != t0->tok_line) break;
            if (toks_adjacent(prev, t0)) {
                blank_concat = 0;
            } else {
                blank_concat = 1;
            }
        } else {
            break;
        }

        if (explicit_concat) {
            advance_tok(p);
            advance_tok(p);
        }

        Lzeroinit(&rhs);
        rc = parse_add(p, &rhs);
        if (rc != IRXPARS_OK) { Lfree(p->alloc, &rhs); return rc; }

        if (blank_concat) {
            if (Lcat(p->alloc, out, " ") != LSTR_OK) {
                Lfree(p->alloc, &rhs);
                return fail(p, IRXPARS_NOMEM);
            }
        }
        if (Lstrcat(p->alloc, out, &rhs) != LSTR_OK) {
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
#define CMP_EQ     1   /* =   */
#define CMP_NE     2   /* \=  */
#define CMP_EQS    3   /* ==  */
#define CMP_NES    4   /* \== */
#define CMP_GT     5   /* >   */
#define CMP_LT     6   /* <   */
#define CMP_GE     7   /* >=  */
#define CMP_LE     8   /* <=  */

static int match_comparison(struct irx_parser *p, int *out_op)
{
    const struct irx_token *t0 = peek_tok(p, 0);
    const struct irx_token *t1 = peek_tok(p, 1);
    const struct irx_token *t2 = peek_tok(p, 2);

    if (t0 == NULL) return 0;

    if (t0->tok_type == TOK_NOT) {
        if (tok_is_op_char(t1, TOK_COMPARISON, '=')) {
            if (tok_is_op_char(t2, TOK_COMPARISON, '=')) {
                *out_op = CMP_NES;
                advance_tok(p); advance_tok(p); advance_tok(p);
                return 1;
            }
            *out_op = CMP_NE;
            advance_tok(p); advance_tok(p);
            return 1;
        }
        return 0;
    }

    if (tok_is_op_char(t0, TOK_COMPARISON, '=')) {
        if (tok_is_op_char(t1, TOK_COMPARISON, '=')) {
            *out_op = CMP_EQS;
            advance_tok(p); advance_tok(p);
            return 1;
        }
        *out_op = CMP_EQ;
        advance_tok(p);
        return 1;
    }

    if (tok_is_op_char(t0, TOK_COMPARISON, '>')) {
        if (tok_is_op_char(t1, TOK_COMPARISON, '=')) {
            *out_op = CMP_GE;
            advance_tok(p); advance_tok(p);
            return 1;
        }
        *out_op = CMP_GT;
        advance_tok(p);
        return 1;
    }

    if (tok_is_op_char(t0, TOK_COMPARISON, '<')) {
        if (tok_is_op_char(t1, TOK_COMPARISON, '=')) {
            *out_op = CMP_LE;
            advance_tok(p); advance_tok(p);
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
    if (rc != IRXPARS_OK) return rc;

    {
        int op;
        Lstr rhs;
        int c, result;

        if (!match_comparison(p, &op)) return IRXPARS_OK;

        Lzeroinit(&rhs);
        rc = parse_concat(p, &rhs);
        if (rc != IRXPARS_OK) { Lfree(p->alloc, &rhs); return rc; }

        if (op == CMP_EQS || op == CMP_NES) {
            c = compare_strict(out, &rhs);
        } else {
            c = compare_normal(out, &rhs);
        }
        Lfree(p->alloc, &rhs);

        switch (op) {
        case CMP_EQ:  result = (c == 0); break;
        case CMP_NE:  result = (c != 0); break;
        case CMP_EQS: result = (c == 0); break;
        case CMP_NES: result = (c != 0); break;
        case CMP_GT:  result = (c >  0); break;
        case CMP_LT:  result = (c <  0); break;
        case CMP_GE:  result = (c >= 0); break;
        case CMP_LE:  result = (c <= 0); break;
        default:      result = 0; break;
        }

        if (long_to_lstr(p->alloc, out, (long)result) != LSTR_OK) {
            return fail(p, IRXPARS_NOMEM);
        }
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
    if (!lstr_to_long(s, &v)) return 0;
    if (v != 0 && v != 1) return 0;
    *out = (int)v;
    return 1;
}

static int parse_and(struct irx_parser *p, PLstr out)
{
    int rc = parse_comparison(p, out);
    if (rc != IRXPARS_OK) return rc;

    for (;;) {
        const struct irx_token *t0 = peek_tok(p, 0);
        const struct irx_token *t1 = peek_tok(p, 1);
        Lstr rhs;
        int a, b;

        /* `&&` is XOR at the parse_or level, so we require the next
         * token NOT to be another `&`. */
        if (!tok_is_op_char(t0, TOK_LOGICAL, '&')) break;
        if (tok_is_op_char(t1, TOK_LOGICAL, '&')) break;

        advance_tok(p);

        Lzeroinit(&rhs);
        rc = parse_comparison(p, &rhs);
        if (rc != IRXPARS_OK) { Lfree(p->alloc, &rhs); return rc; }

        if (!as_bool(out, &a) || !as_bool(&rhs, &b)) {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_SYNTAX);
        }
        Lfree(p->alloc, &rhs);

        if (long_to_lstr(p->alloc, out, (long)(a && b)) != LSTR_OK) {
            return fail(p, IRXPARS_NOMEM);
        }
    }
    return IRXPARS_OK;
}

static int parse_or(struct irx_parser *p, PLstr out)
{
    int rc = parse_and(p, out);
    if (rc != IRXPARS_OK) return rc;

    for (;;) {
        const struct irx_token *t0 = peek_tok(p, 0);
        const struct irx_token *t1 = peek_tok(p, 1);
        int is_or = 0;
        int is_xor = 0;
        Lstr rhs;
        int a, b, r;

        if (tok_is_op_char(t0, TOK_LOGICAL, '|') &&
            !tok_is_op_char(t1, TOK_LOGICAL, '|')) {
            is_or = 1;
        } else if (tok_is_op_char(t0, TOK_LOGICAL, '&') &&
                   tok_is_op_char(t1, TOK_LOGICAL, '&')) {
            is_xor = 1;
        } else {
            break;
        }

        if (is_xor) {
            advance_tok(p);
            advance_tok(p);
        } else {
            advance_tok(p);
        }
        (void)is_or;

        Lzeroinit(&rhs);
        rc = parse_and(p, &rhs);
        if (rc != IRXPARS_OK) { Lfree(p->alloc, &rhs); return rc; }

        if (!as_bool(out, &a) || !as_bool(&rhs, &b)) {
            Lfree(p->alloc, &rhs);
            return fail(p, IRXPARS_SYNTAX);
        }
        Lfree(p->alloc, &rhs);

        r = is_xor ? (a ^ b) : (a | b);
        if (long_to_lstr(p->alloc, out, (long)r) != LSTR_OK) {
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

    if (name_tok->tok_flags & TOKF_COMPOUND) {
        rc = resolve_compound_name(p, name_tok, &name);
        if (rc != IRXPARS_OK) goto done;
    } else {
        if (set_upper_from_tok(p->alloc, &name, name_tok) != LSTR_OK) {
            rc = fail(p, IRXPARS_NOMEM);
            goto done;
        }
    }

    advance_tok(p);   /* SYMBOL */
    advance_tok(p);   /* =      */

    rc = irx_pars_eval_expr(p, &value);
    if (rc != IRXPARS_OK) goto done;

    if (vpool_set(p->vpool, &name, &value) != VPOOL_OK) {
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

static int is_assignment_here(struct irx_parser *p)
{
    const struct irx_token *t0 = peek_tok(p, 0);
    const struct irx_token *t1 = peek_tok(p, 1);
    const struct irx_token *t2 = peek_tok(p, 2);

    if (t0 == NULL || t0->tok_type != TOK_SYMBOL) return 0;
    if (t0->tok_flags & TOKF_CONSTANT) return 0;
    if (!tok_is_op_char(t1, TOK_COMPARISON, '=')) return 0;
    /* Exclude `==` which is strict comparison, not assignment. */
    if (tok_is_op_char(t2, TOK_COMPARISON, '=')) return 0;
    return 1;
}

static int exec_clause(struct irx_parser *p)
{
    const struct irx_token *t0;

    /* Skip empty clauses. */
    while (cur_tok(p) != NULL && cur_tok(p)->tok_type == TOK_EOC) {
        advance_tok(p);
    }

    t0 = cur_tok(p);
    if (t0 == NULL || t0->tok_type == TOK_EOF) return IRXPARS_OK;

    /* Null clause (just a semicolon) also does nothing. */
    if (t0->tok_type == TOK_SEMICOLON) {
        advance_tok(p);
        return IRXPARS_OK;
    }

    /* Rule 1: assignment. */
    if (is_assignment_here(p)) {
        return exec_assignment(p);
    }

    /* Rule 2: label - SYMBOL followed by SEMICOLON (the colon). */
    if (t0->tok_type == TOK_SYMBOL) {
        const struct irx_token *t1 = peek_tok(p, 1);
        if (t1 != NULL && t1->tok_type == TOK_SEMICOLON &&
            t1->tok_length == 1 && t1->tok_text[0] == ':') {
            /* Label declaration - WP-15 will record it. For now, just
             * consume the two tokens. */
            advance_tok(p);
            advance_tok(p);
            return IRXPARS_OK;
        }

        /* Rule 3: keyword instruction. */
        {
            Lstr upname;
            const struct irx_keyword *kw;
            Lzeroinit(&upname);
            if (set_upper_from_tok(p->alloc, &upname, t0) != LSTR_OK) {
                return fail(p, IRXPARS_NOMEM);
            }
            kw = find_keyword(upname.pstr, upname.len);
            Lfree(p->alloc, &upname);
            if (kw != NULL) {
                advance_tok(p);
                return kw->kw_handler(p);
            }
        }
    }

    /* Rule 4: command. */
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
    if (p == NULL || tokens == NULL || vpool == NULL || alloc == NULL) {
        return IRXPARS_BADARG;
    }
    memset(p, 0, sizeof(*p));
    p->tokens    = tokens;
    p->tok_count = tok_count;
    p->tok_pos   = 0;
    p->vpool     = vpool;
    p->alloc     = alloc;
    p->envblock  = envblock;
    Lzeroinit(&p->result);
    p->error_code = IRXPARS_OK;
    p->error_line = 0;
    return IRXPARS_OK;
}

void irx_pars_cleanup(struct irx_parser *p)
{
    if (p == NULL) return;
    Lfree(p->alloc, &p->result);
}

int irx_pars_eval_expr(struct irx_parser *p, PLstr out)
{
    if (p == NULL || out == NULL) return IRXPARS_BADARG;
    return parse_or(p, out);
}

int irx_pars_run(struct irx_parser *p)
{
    if (p == NULL) return IRXPARS_BADARG;

    while (p->tok_pos < p->tok_count) {
        const struct irx_token *t = cur_tok(p);
        int rc;
        if (t == NULL || t->tok_type == TOK_EOF) break;

        rc = exec_clause(p);
        if (rc != IRXPARS_OK) return rc;

        /* Consume trailing clause terminators. */
        while (cur_tok(p) != NULL &&
               (cur_tok(p)->tok_type == TOK_EOC ||
                cur_tok(p)->tok_type == TOK_SEMICOLON)) {
            advance_tok(p);
        }
    }
    return IRXPARS_OK;
}
