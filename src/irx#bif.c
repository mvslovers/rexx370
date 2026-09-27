/* ------------------------------------------------------------------ */
/*  irx#bif.c - REXX/370 BIF argument-validation helpers              */
/*                                                                    */
/*  Shared helpers the BIF handlers use to validate their arguments   */
/*  and raise SYNTAX 40.x. BIF lookup is irx_bif_find_local() in      */
/*  irx#bifs.c, next to the static table it searches; the former      */
/*  per-environment registry is gone (#254).                          */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <limits.h>
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
/*  Constants                                                         */
/* ------------------------------------------------------------------ */

/* Scratch buffer size for assembling "BIFNAME: <detail>" messages.   */
#define BIF_DESC_BUF 96

/* Error return propagated to the parser when a validation helper      */
/* raises a condition. Matches IRXPARS_SYNTAX.                         */
#define BIF_FAIL 20

/* Decimal radix for whole-number parsing.                             */
#define BIF_RADIX 10


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
        long digit = (long)(c - '0');
        /* Reject values that would overflow LONG_MAX during the
         * multiply-add. Without this check, c2asm370's 32-bit long
         * silently wraps a 10-digit input like 4294967301 to 5,
         * which then looks like a valid small line number to
         * callers like SOURCELINE. Check uses only safe arithmetic
         * (LONG_MAX - digit can't underflow because digit ≤ 9 and
         * LONG_MAX ≥ 2^31-1 on every supported target). */
        if (v > (LONG_MAX - digit) / BIF_RADIX)
        {
            return -1;
        }
        v = v * BIF_RADIX + digit;
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
