/* ------------------------------------------------------------------ */
/*  irx#bif.c - REXX/370 BIF registry and argument-validation helpers */
/*                                                                    */
/*  Linear list of registered built-in functions. One list per        */
/*  environment, anchored at wkbi_bif_registry. Lookup is case-       */
/*  sensitive on the registered (upper-case) name.                    */
/*                                                                    */
/*  Allocation goes through irxstor; no malloc/free direct calls.     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxbif.h"
#include "irxcond.h"
#include "irxfunc.h"
#include "irxpars.h"
#include "irxwkblk.h"
#include "lstring.h"

/* ------------------------------------------------------------------ */
/*  Registry layout                                                   */
/* ------------------------------------------------------------------ */

#define BIF_EYECATCHER     "IRBR"
#define BIF_EYECATCHER_LEN 4

/* Scratch buffer size for assembling "BIFNAME: <detail>" messages.   */
#define BIF_DESC_BUF 96

/* Error return propagated to the parser when a validation helper      */
/* raises a condition. Matches IRXPARS_SYNTAX.                         */
#define BIF_FAIL 20

/* Decimal radix for whole-number parsing.                             */
#define BIF_RADIX 10

struct bif_node
{
    struct bif_node *next;
    struct irx_bif_entry entry;
};

struct irx_bif_registry
{
    unsigned char id[BIF_EYECATCHER_LEN]; /* eye-catcher "IRBR"        */
    struct bif_node *head;
    int count;
};

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

static int name_len(const char *name)
{
    int n = 0;
    while (n < IRX_BIF_NAME_MAX && name[n] != '\0')
    {
        n++;
    }
    return n;
}

static int ebcdic_eq(const char *a, const unsigned char *b, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
    {
        if ((unsigned char)a[i] != b[i])
        {
            return 0;
        }
    }
    return a[len] == '\0';
}

/* ------------------------------------------------------------------ */
/*  Public registry API                                               */
/* ------------------------------------------------------------------ */

int irx_bif_create(struct envblock *env, struct irx_bif_registry **out)
{
    if (out == NULL)
    {
        return IRX_BIF_BADARG;
    }
    *out = NULL;

    void *p = NULL;
    if (irxstor(RXSMGET, (int)sizeof(struct irx_bif_registry),
                &p, env) != 0)
    {
        return IRX_BIF_NOMEM;
    }

    struct irx_bif_registry *reg = (struct irx_bif_registry *)p;
    memcpy(reg->id, BIF_EYECATCHER, BIF_EYECATCHER_LEN);
    reg->head = NULL;
    reg->count = 0;

    *out = reg;
    return IRX_BIF_OK;
}

void irx_bif_destroy(struct envblock *env, struct irx_bif_registry *reg)
{
    if (reg == NULL)
    {
        return;
    }

    struct bif_node *node = reg->head;
    while (node != NULL)
    {
        struct bif_node *next = node->next;
        void *p = node;
        irxstor(RXSMFRE, 0, &p, env);
        node = next;
    }

    void *p = reg;
    irxstor(RXSMFRE, 0, &p, env);
}

int irx_bif_register(struct envblock *env, struct irx_bif_registry *reg,
                     const char *name, int min_args, int max_args,
                     irx_bif_handler_t handler)
{
    if (reg == NULL || name == NULL || handler == NULL ||
        min_args < 0 || max_args < min_args)
    {
        return IRX_BIF_BADARG;
    }

    int nlen = name_len(name);
    if (nlen == 0 || nlen >= IRX_BIF_NAME_MAX)
    {
        return IRX_BIF_BADARG;
    }

    /* Reject duplicates. */
    struct bif_node *cursor = reg->head;
    while (cursor != NULL)
    {
        if (strcmp(cursor->entry.name, name) == 0)
        {
            return IRX_BIF_DUPLICATE;
        }
        cursor = cursor->next;
    }

    void *p = NULL;
    if (irxstor(RXSMGET, (int)sizeof(struct bif_node), &p, env) != 0)
    {
        return IRX_BIF_NOMEM;
    }
    struct bif_node *node = (struct bif_node *)p;

    memset(node->entry.name, 0, sizeof(node->entry.name));
    memcpy(node->entry.name, name, (size_t)nlen);
    node->entry.min_args = min_args;
    node->entry.max_args = max_args;
    node->entry.handler = handler;

    node->next = reg->head;
    reg->head = node;
    reg->count++;
    return IRX_BIF_OK;
}

int irx_bif_register_table(struct envblock *env,
                           struct irx_bif_registry *reg,
                           const struct irx_bif_entry *table, int count)
{
    if (reg == NULL || table == NULL || count < 0)
    {
        return IRX_BIF_BADARG;
    }

    int i;
    for (i = 0; i < count; i++)
    {
        const struct irx_bif_entry *e = &table[i];
        if (e->name[0] == '\0')
        {
            break;
        }
        int rc = irx_bif_register(env, reg, e->name,
                                  e->min_args, e->max_args, e->handler);
        if (rc != IRX_BIF_OK)
        {
            return rc;
        }
    }
    return IRX_BIF_OK;
}

const struct irx_bif_entry *
irx_bif_find(const struct irx_bif_registry *reg,
             const unsigned char *name, size_t len)
{
    if (reg == NULL || name == NULL || len == 0 ||
        len >= IRX_BIF_NAME_MAX)
    {
        return NULL;
    }

    struct bif_node *node = reg->head;
    while (node != NULL)
    {
        if (ebcdic_eq(node->entry.name, name, len))
        {
            return &node->entry;
        }
        node = node->next;
    }
    return NULL;
}

/* ================================================================== */
/*  Argument-validation helpers                                       */
/* ================================================================== */

/* Build "BIFNAME: detail" into desc_buf. Returns desc_buf for          */
/* convenience.                                                         */
static const char *mkdesc(char *buf, size_t cap, const char *bif_name,
                          const char *detail)
{
    size_t nlen = strlen(bif_name);
    size_t dlen = strlen(detail);
    size_t off = 0;

    if (nlen + 2 + dlen >= cap)
    {
        if (nlen >= cap)
        {
            nlen = cap - 1;
        }
        memcpy(buf, bif_name, nlen);
        buf[nlen] = '\0';
        return buf;
    }

    memcpy(buf, bif_name, nlen);
    off += nlen;
    buf[off++] = ':';
    buf[off++] = ' ';
    memcpy(buf + off, detail, dlen);
    off += dlen;
    buf[off] = '\0';
    return buf;
}

int irx_bif_require_arg(struct irx_parser *p, int argc, PLstr *argv,
                        int idx, const char *bif_name)
{
    char desc[BIF_DESC_BUF];
    if (idx < argc && argv != NULL && argv[idx] != NULL)
    {
        return 0;
    }
    irx_cond_raise(p->envblock, SYNTAX_BAD_CALL, ERR40_TOO_FEW_ARGS,
                   mkdesc(desc, sizeof(desc), bif_name,
                          "required argument missing"));
    return BIF_FAIL;
}

static int parse_whole(PLstr s, long *out)
{
    if (s == NULL || s->len == 0)
    {
        return -1;
    }
    size_t i = 0;
    int neg = 0;
    if (i < s->len && (s->pstr[i] == '+' || s->pstr[i] == '-'))
    {
        neg = (s->pstr[i] == '-');
        i++;
    }
    if (i >= s->len)
    {
        return -1;
    }
    long v = 0;
    for (; i < s->len; i++)
    {
        unsigned char c = s->pstr[i];
        if (c < '0' || c > '9')
        {
            return -1;
        }
        v = v * BIF_RADIX + (long)(c - '0');
    }
    *out = neg ? -v : v;
    return 0;
}

int irx_bif_whole_nonneg(struct irx_parser *p, PLstr *argv,
                         int idx, const char *bif_name, long *out)
{
    char desc[BIF_DESC_BUF];
    long v = 0;
    if (parse_whole(argv[idx], &v) != 0 || v < 0)
    {
        irx_cond_raise(p->envblock, SYNTAX_BAD_CALL, ERR40_NONNEG_WHOLE,
                       mkdesc(desc, sizeof(desc), bif_name,
                              "argument must be a non-negative whole number"));
        return BIF_FAIL;
    }
    *out = v;
    return 0;
}

int irx_bif_whole_positive(struct irx_parser *p, PLstr *argv,
                           int idx, const char *bif_name, long *out)
{
    char desc[BIF_DESC_BUF];
    long v = 0;
    if (parse_whole(argv[idx], &v) != 0 || v <= 0)
    {
        irx_cond_raise(p->envblock, SYNTAX_BAD_CALL, ERR40_POSITIVE_WHOLE,
                       mkdesc(desc, sizeof(desc), bif_name,
                              "argument must be a positive whole number"));
        return BIF_FAIL;
    }
    *out = v;
    return 0;
}

int irx_bif_opt_whole(struct irx_parser *p, int argc, PLstr *argv,
                      int idx, const char *bif_name,
                      long default_val, long *out)
{
    if (idx >= argc || argv[idx] == NULL || argv[idx]->len == 0)
    {
        *out = default_val;
        return 0;
    }
    return irx_bif_whole_nonneg(p, argv, idx, bif_name, out);
}

int irx_bif_opt_char(struct irx_parser *p, int argc, PLstr *argv,
                     int idx, const char *bif_name,
                     char default_char, char *out)
{
    char desc[BIF_DESC_BUF];
    if (idx >= argc || argv[idx] == NULL || argv[idx]->len == 0)
    {
        *out = default_char;
        return 0;
    }
    if (argv[idx]->len != 1)
    {
        irx_cond_raise(p->envblock, SYNTAX_BAD_CALL, ERR40_SINGLE_CHAR,
                       mkdesc(desc, sizeof(desc), bif_name,
                              "argument must be a single character"));
        return BIF_FAIL;
    }
    *out = (char)argv[idx]->pstr[0];
    return 0;
}

int irx_bif_opt_option(struct irx_parser *p, int argc, PLstr *argv,
                       int idx, const char *bif_name,
                       const char *allowed, char default_opt, char *out)
{
    char desc[BIF_DESC_BUF];
    if (idx >= argc || argv[idx] == NULL || argv[idx]->len == 0)
    {
        *out = default_opt;
        return 0;
    }
    if (argv[idx]->len < 1)
    {
        irx_cond_raise(p->envblock, SYNTAX_BAD_CALL, ERR40_OPTION_INVALID,
                       mkdesc(desc, sizeof(desc), bif_name,
                              "option argument empty"));
        return BIF_FAIL;
    }

    unsigned char c = argv[idx]->pstr[0];
    if (islower(c))
    {
        c = (unsigned char)toupper(c);
    }
    const char *s;
    for (s = allowed; *s != '\0'; s++)
    {
        if ((unsigned char)*s == c)
        {
            *out = (char)c;
            return 0;
        }
    }
    irx_cond_raise(p->envblock, SYNTAX_BAD_CALL, ERR40_OPTION_INVALID,
                   mkdesc(desc, sizeof(desc), bif_name,
                          "option value not recognised"));
    return BIF_FAIL;
}
