/* ------------------------------------------------------------------ */
/*  irx#bifs.c - REXX/370 String Built-in Functions (WP-21a)         */
/*                                                                    */
/*  Implements the ~29 SC28-1883-0 §4 string BIFs as thin wrappers    */
/*  around lstring370 primitives. Argument parsing and validation     */
/*  goes through the helpers in irx#bif.c, which surface SYNTAX 40.x  */
/*  conditions on failure.                                            */
/*                                                                    */
/*  All state is per-environment; no globals, no statics holding      */
/*  mutable data. Registration is one-shot via irx_bifstr_register(). */
/*                                                                    */
/*  Note on the filename: SC28-1883-0 naming wants IRXBIFSTR but the  */
/*  MVS PDS member limit is 8 characters; mbt truncates upper-cased   */
/*  basenames, so this file is named irx#bifs.c → IRX#BIFS.           */
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
#include "irxbifstr.h"
#include "irxcond.h"
#include "irxlstr.h"
#include "irxpars.h"
#include "irxwkblk.h"
#include "lstralloc.h"
#include "lstring.h"

/* ================================================================== */
/*  Scratch / conversion helpers                                      */
/* ================================================================== */

#define LTOA_BUF 32

static int long_to_lstr(struct lstr_alloc *a, PLstr dst, long v)
{
    char buf[LTOA_BUF];
    int n = sprintf(buf, "%ld", v);
    if (n < 0)
    {
        return LSTR_ERR_NOMEM;
    }
    int rc = Lfx(a, dst, (size_t)n);
    if (rc != LSTR_OK)
    {
        return rc;
    }
    if (n > 0)
    {
        memcpy(dst->pstr, buf, (size_t)n);
    }
    dst->len = (size_t)n;
    dst->type = LSTRING_TY;
    return LSTR_OK;
}

static int translate_lstr_rc(int rc)
{
    if (rc == LSTR_OK)
    {
        return IRXPARS_OK;
    }
    if (rc == LSTR_ERR_NOMEM)
    {
        return IRXPARS_NOMEM;
    }
    return IRXPARS_SYNTAX;
}

static int set_empty(struct lstr_alloc *a, PLstr s)
{
    int rc = Lfx(a, s, 0);
    if (rc != LSTR_OK)
    {
        return rc;
    }
    s->len = 0;
    s->type = LSTRING_TY;
    return LSTR_OK;
}

static int set_one(struct lstr_alloc *a, PLstr s, unsigned char c)
{
    int rc = Lfx(a, s, 1);
    if (rc != LSTR_OK)
    {
        return rc;
    }
    s->pstr[0] = c;
    s->len = 1;
    s->type = LSTRING_TY;
    return LSTR_OK;
}

/* ================================================================== */
/*  Phase B — Substring & position                                    */
/* ================================================================== */

static int bif_length(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    (void)argc;
    return translate_lstr_rc(
        long_to_lstr(p->alloc, result, (long)argv[0]->len));
}

static int bif_left(struct irx_parser *p, int argc, PLstr *argv,
                    PLstr result)
{
    long n = 0;
    char pad = ' ';
    int rc = irx_bif_whole_nonneg(p, argv, 1, "LEFT", &n);
    if (rc != 0)
    {
        return rc;
    }
    rc = irx_bif_opt_char(p, argc, argv, 2, "LEFT", ' ', &pad);
    if (rc != 0)
    {
        return rc;
    }
    return translate_lstr_rc(
        Lleft(p->alloc, result, argv[0], (size_t)n, pad));
}

static int bif_right(struct irx_parser *p, int argc, PLstr *argv,
                     PLstr result)
{
    long n = 0;
    char pad = ' ';
    int rc = irx_bif_whole_nonneg(p, argv, 1, "RIGHT", &n);
    if (rc != 0)
    {
        return rc;
    }
    rc = irx_bif_opt_char(p, argc, argv, 2, "RIGHT", ' ', &pad);
    if (rc != 0)
    {
        return rc;
    }
    return translate_lstr_rc(
        Lright(p->alloc, result, argv[0], (size_t)n, pad));
}

static int bif_substr(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    long start = 0;
    long len = 0;
    char pad = ' ';
    int rc = irx_bif_whole_positive(p, argv, 1, "SUBSTR", &start);
    if (rc != 0)
    {
        return rc;
    }

    size_t used_len = LSTR_REST;
    if (argc >= 3 && argv[2] != NULL && argv[2]->len > 0)
    {
        rc = irx_bif_whole_nonneg(p, argv, 2, "SUBSTR", &len);
        if (rc != 0)
        {
            return rc;
        }
        used_len = (size_t)len;
    }

    rc = irx_bif_opt_char(p, argc, argv, 3, "SUBSTR", ' ', &pad);
    if (rc != 0)
    {
        return rc;
    }

    return translate_lstr_rc(
        Lsubstr(p->alloc, result, argv[0], (size_t)start, used_len, pad));
}

static int bif_pos(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    long start = 1;
    int rc = irx_bif_opt_whole(p, argc, argv, 2, "POS", 1, &start);
    if (rc != 0)
    {
        return rc;
    }
    size_t pos = Lpos(argv[0], argv[1], (size_t)start);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, (long)pos));
}

static int bif_index(struct irx_parser *p, int argc, PLstr *argv,
                     PLstr result)
{
    /* INDEX(haystack, needle [,start]) — haystack/needle reversed vs POS. */
    long start = 1;
    int rc = irx_bif_opt_whole(p, argc, argv, 2, "INDEX", 1, &start);
    if (rc != 0)
    {
        return rc;
    }
    size_t pos = Lindex(argv[1], argv[0], (size_t)start);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, (long)pos));
}

static int bif_lastpos(struct irx_parser *p, int argc, PLstr *argv,
                       PLstr result)
{
    long start = 0;
    int rc = irx_bif_opt_whole(p, argc, argv, 2, "LASTPOS", 0, &start);
    if (rc != 0)
    {
        return rc;
    }
    size_t pos = Llastpos(argv[0], argv[1], (size_t)start);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, (long)pos));
}

/* ================================================================== */
/*  Phase C — Word operations                                         */
/* ================================================================== */

static int bif_words(struct irx_parser *p, int argc, PLstr *argv,
                     PLstr result)
{
    (void)argc;
    size_t n = Lwords(argv[0]);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, (long)n));
}

static int bif_word(struct irx_parser *p, int argc, PLstr *argv,
                    PLstr result)
{
    (void)argc;
    long n = 0;
    int rc = irx_bif_whole_positive(p, argv, 1, "WORD", &n);
    if (rc != 0)
    {
        return rc;
    }
    return translate_lstr_rc(Lword(p->alloc, result, argv[0], (size_t)n));
}

static int bif_wordindex(struct irx_parser *p, int argc, PLstr *argv,
                         PLstr result)
{
    (void)argc;
    long n = 0;
    int rc = irx_bif_whole_positive(p, argv, 1, "WORDINDEX", &n);
    if (rc != 0)
    {
        return rc;
    }
    size_t idx = Lwordindex(argv[0], (size_t)n);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, (long)idx));
}

static int bif_wordlength(struct irx_parser *p, int argc, PLstr *argv,
                          PLstr result)
{
    (void)argc;
    long n = 0;
    int rc = irx_bif_whole_positive(p, argv, 1, "WORDLENGTH", &n);
    if (rc != 0)
    {
        return rc;
    }
    size_t len = Lwordlength(argv[0], (size_t)n);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, (long)len));
}

static int bif_subword(struct irx_parser *p, int argc, PLstr *argv,
                       PLstr result)
{
    long n = 0;
    long count = 0;
    int rc = irx_bif_whole_positive(p, argv, 1, "SUBWORD", &n);
    if (rc != 0)
    {
        return rc;
    }

    size_t used_count = LSTR_REST;
    if (argc >= 3 && argv[2] != NULL && argv[2]->len > 0)
    {
        rc = irx_bif_whole_nonneg(p, argv, 2, "SUBWORD", &count);
        if (rc != 0)
        {
            return rc;
        }
        used_count = (size_t)count;
    }

    return translate_lstr_rc(
        Lsubword(p->alloc, result, argv[0], (size_t)n, used_count));
}

static int bif_wordpos(struct irx_parser *p, int argc, PLstr *argv,
                       PLstr result)
{
    long start = 1;
    int rc = irx_bif_opt_whole(p, argc, argv, 2, "WORDPOS", 1, &start);
    if (rc != 0)
    {
        return rc;
    }
    size_t pos = Lwordpos(argv[0], argv[1], (size_t)start);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, (long)pos));
}

/* ================================================================== */
/*  Phase D — Padding, stripping, formatting                          */
/* ================================================================== */

static int bif_center(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    long n = 0;
    char pad = ' ';
    int rc = irx_bif_whole_nonneg(p, argv, 1, "CENTER", &n);
    if (rc != 0)
    {
        return rc;
    }
    rc = irx_bif_opt_char(p, argc, argv, 2, "CENTER", ' ', &pad);
    if (rc != 0)
    {
        return rc;
    }
    return translate_lstr_rc(
        Lcenter(p->alloc, result, argv[0], (size_t)n, pad));
}

static int bif_strip(struct irx_parser *p, int argc, PLstr *argv,
                     PLstr result)
{
    char opt = LSTRIP_BOTH;
    char sc = ' ';
    int rc = irx_bif_opt_option(p, argc, argv, 1, "STRIP",
                                "BLT", LSTRIP_BOTH, &opt);
    if (rc != 0)
    {
        return rc;
    }
    rc = irx_bif_opt_char(p, argc, argv, 2, "STRIP", ' ', &sc);
    if (rc != 0)
    {
        return rc;
    }
    return translate_lstr_rc(
        Lstrip(p->alloc, result, argv[0], opt, sc));
}

static int bif_space(struct irx_parser *p, int argc, PLstr *argv,
                     PLstr result)
{
    long n = 1;
    char pad = ' ';
    int rc = irx_bif_opt_whole(p, argc, argv, 1, "SPACE", 1, &n);
    if (rc != 0)
    {
        return rc;
    }
    rc = irx_bif_opt_char(p, argc, argv, 2, "SPACE", ' ', &pad);
    if (rc != 0)
    {
        return rc;
    }
    return translate_lstr_rc(
        Lspace(p->alloc, result, argv[0], (size_t)n, pad));
}

static int bif_copies(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    (void)argc;
    long n = 0;
    int rc = irx_bif_whole_nonneg(p, argv, 1, "COPIES", &n);
    if (rc != 0)
    {
        return rc;
    }
    return translate_lstr_rc(
        Lcopies(p->alloc, result, argv[0], (size_t)n));
}

static int bif_reverse(struct irx_parser *p, int argc, PLstr *argv,
                       PLstr result)
{
    (void)argc;
    return translate_lstr_rc(Lreverse(p->alloc, result, argv[0]));
}

/* ------------------------------------------------------------------ */
/*  JUSTIFY(str, n [,pad])                                            */
/*                                                                    */
/*  Normalise whitespace in str (SPACE style), then distribute extra  */
/*  pad characters evenly between word gaps to fill to exactly n.     */
/*  Extra pad characters are spread right-to-left so leftmost gaps    */
/*  get padded first.                                                 */
/* ------------------------------------------------------------------ */

static int bif_justify(struct irx_parser *p, int argc, PLstr *argv,
                       PLstr result)
{
    long n = 0;
    char pad = ' ';
    int rc = irx_bif_whole_nonneg(p, argv, 1, "JUSTIFY", &n);
    if (rc != 0)
    {
        return rc;
    }
    rc = irx_bif_opt_char(p, argc, argv, 2, "JUSTIFY", ' ', &pad);
    if (rc != 0)
    {
        return rc;
    }

    Lstr spaced;
    Lzeroinit(&spaced);
    rc = Lspace(p->alloc, &spaced, argv[0], 1, pad);
    if (rc != LSTR_OK)
    {
        Lfree(p->alloc, &spaced);
        return translate_lstr_rc(rc);
    }

    size_t width = (size_t)n;

    /* Empty source or zero width → just pad-or-truncate to width. */
    if (spaced.len == 0 || width == 0)
    {
        int rc2 = Lfx(p->alloc, result, width);
        if (rc2 != LSTR_OK)
        {
            Lfree(p->alloc, &spaced);
            return translate_lstr_rc(rc2);
        }
        if (width > 0)
        {
            memset(result->pstr, (unsigned char)pad, width);
        }
        result->len = width;
        result->type = LSTRING_TY;
        Lfree(p->alloc, &spaced);
        return IRXPARS_OK;
    }

    if (width <= spaced.len)
    {
        int rc2 = Lfx(p->alloc, result, width);
        if (rc2 != LSTR_OK)
        {
            Lfree(p->alloc, &spaced);
            return translate_lstr_rc(rc2);
        }
        memcpy(result->pstr, spaced.pstr, width);
        result->len = width;
        result->type = LSTRING_TY;
        Lfree(p->alloc, &spaced);
        return IRXPARS_OK;
    }

    /* Count word gaps (single pad chars between words). */
    size_t gaps = 0;
    size_t i;
    for (i = 0; i < spaced.len; i++)
    {
        if ((char)spaced.pstr[i] == pad)
        {
            gaps++;
        }
    }

    size_t extra = width - spaced.len;

    int rc2 = Lfx(p->alloc, result, width);
    if (rc2 != LSTR_OK)
    {
        Lfree(p->alloc, &spaced);
        return translate_lstr_rc(rc2);
    }

    if (gaps == 0)
    {
        /* Single word — pad on the right. */
        memcpy(result->pstr, spaced.pstr, spaced.len);
        memset(result->pstr + spaced.len, (unsigned char)pad, extra);
        result->len = width;
        result->type = LSTRING_TY;
        Lfree(p->alloc, &spaced);
        return IRXPARS_OK;
    }

    size_t base = extra / gaps;
    size_t rem = extra % gaps;

    size_t src_idx = 0;
    size_t dst_idx = 0;
    for (src_idx = 0; src_idx < spaced.len; src_idx++)
    {
        unsigned char c = spaced.pstr[src_idx];
        result->pstr[dst_idx++] = c;
        if ((char)c == pad)
        {
            size_t extra_here = base + (rem > 0 ? 1 : 0);
            if (rem > 0)
            {
                rem--;
            }
            size_t k;
            for (k = 0; k < extra_here; k++)
            {
                result->pstr[dst_idx++] = (unsigned char)pad;
            }
        }
    }

    result->len = width;
    result->type = LSTRING_TY;
    Lfree(p->alloc, &spaced);
    return IRXPARS_OK;
}

/* ================================================================== */
/*  Phase E — Insert, delete, overlay                                 */
/* ================================================================== */

static int bif_insert(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    long pos = 0;
    long len = 0;
    char pad = ' ';
    int rc = irx_bif_opt_whole(p, argc, argv, 2, "INSERT", 0, &pos);
    if (rc != 0)
    {
        return rc;
    }

    size_t used_len = LSTR_REST;
    if (argc >= 4 && argv[3] != NULL && argv[3]->len > 0)
    {
        rc = irx_bif_whole_nonneg(p, argv, 3, "INSERT", &len);
        if (rc != 0)
        {
            return rc;
        }
        used_len = (size_t)len;
    }

    rc = irx_bif_opt_char(p, argc, argv, 4, "INSERT", ' ', &pad);
    if (rc != 0)
    {
        return rc;
    }

    /* Linsert may not accept LSTR_REST as length; fall back to ins->len. */
    Lstr ins_trim;
    Lzeroinit(&ins_trim);
    const PLstr ins = argv[0];
    size_t actual_len = (used_len == LSTR_REST) ? ins->len : used_len;

    /* If actual_len > ins->len, pad ins on the right with `pad`. */
    if (actual_len > ins->len)
    {
        rc = Lfx(p->alloc, &ins_trim, actual_len);
        if (rc != LSTR_OK)
        {
            Lfree(p->alloc, &ins_trim);
            return translate_lstr_rc(rc);
        }
        memcpy(ins_trim.pstr, ins->pstr, ins->len);
        memset(ins_trim.pstr + ins->len, (unsigned char)pad,
               actual_len - ins->len);
        ins_trim.len = actual_len;
        ins_trim.type = LSTRING_TY;
    }
    else if (actual_len < ins->len)
    {
        rc = Lfx(p->alloc, &ins_trim, actual_len);
        if (rc != LSTR_OK)
        {
            Lfree(p->alloc, &ins_trim);
            return translate_lstr_rc(rc);
        }
        memcpy(ins_trim.pstr, ins->pstr, actual_len);
        ins_trim.len = actual_len;
        ins_trim.type = LSTRING_TY;
    }
    else
    {
        rc = Lstrcpy(p->alloc, &ins_trim, ins);
        if (rc != LSTR_OK)
        {
            Lfree(p->alloc, &ins_trim);
            return translate_lstr_rc(rc);
        }
    }

    rc = Linsert(p->alloc, result, &ins_trim, argv[1],
                 (size_t)pos, pad);
    Lfree(p->alloc, &ins_trim);
    return translate_lstr_rc(rc);
}

static int bif_overlay(struct irx_parser *p, int argc, PLstr *argv,
                       PLstr result)
{
    long pos = 1;
    long len = 0;
    char pad = ' ';
    int rc = irx_bif_opt_whole(p, argc, argv, 2, "OVERLAY", 1, &pos);
    if (rc != 0)
    {
        return rc;
    }
    if (pos < 1)
    {
        pos = 1;
    }

    size_t used_len = LSTR_REST;
    if (argc >= 4 && argv[3] != NULL && argv[3]->len > 0)
    {
        rc = irx_bif_whole_nonneg(p, argv, 3, "OVERLAY", &len);
        if (rc != 0)
        {
            return rc;
        }
        used_len = (size_t)len;
    }

    rc = irx_bif_opt_char(p, argc, argv, 4, "OVERLAY", ' ', &pad);
    if (rc != 0)
    {
        return rc;
    }

    /* Same trim/pad dance as INSERT. */
    Lstr ins_trim;
    Lzeroinit(&ins_trim);
    const PLstr ins = argv[0];
    size_t actual_len = (used_len == LSTR_REST) ? ins->len : used_len;

    if (actual_len != ins->len)
    {
        rc = Lfx(p->alloc, &ins_trim, actual_len);
        if (rc != LSTR_OK)
        {
            Lfree(p->alloc, &ins_trim);
            return translate_lstr_rc(rc);
        }
        size_t copy = ins->len < actual_len ? ins->len : actual_len;
        memcpy(ins_trim.pstr, ins->pstr, copy);
        if (copy < actual_len)
        {
            memset(ins_trim.pstr + copy, (unsigned char)pad,
                   actual_len - copy);
        }
        ins_trim.len = actual_len;
        ins_trim.type = LSTRING_TY;
    }
    else
    {
        rc = Lstrcpy(p->alloc, &ins_trim, ins);
        if (rc != LSTR_OK)
        {
            Lfree(p->alloc, &ins_trim);
            return translate_lstr_rc(rc);
        }
    }

    rc = Loverlay(p->alloc, result, &ins_trim, argv[1],
                  (size_t)pos, pad);
    Lfree(p->alloc, &ins_trim);
    return translate_lstr_rc(rc);
}

static int bif_delstr(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    long start = 0;
    long len = 0;
    int rc = irx_bif_whole_positive(p, argv, 1, "DELSTR", &start);
    if (rc != 0)
    {
        return rc;
    }

    size_t used_len = LSTR_REST;
    if (argc >= 3 && argv[2] != NULL && argv[2]->len > 0)
    {
        rc = irx_bif_whole_nonneg(p, argv, 2, "DELSTR", &len);
        if (rc != 0)
        {
            return rc;
        }
        used_len = (size_t)len;
    }

    return translate_lstr_rc(
        Ldelstr(p->alloc, result, argv[0], (size_t)start, used_len));
}

static int bif_delword(struct irx_parser *p, int argc, PLstr *argv,
                       PLstr result)
{
    long start = 0;
    long count = 0;
    int rc = irx_bif_whole_positive(p, argv, 1, "DELWORD", &start);
    if (rc != 0)
    {
        return rc;
    }

    size_t used_count = LSTR_REST;
    if (argc >= 3 && argv[2] != NULL && argv[2]->len > 0)
    {
        rc = irx_bif_whole_nonneg(p, argv, 2, "DELWORD", &count);
        if (rc != 0)
        {
            return rc;
        }
        used_count = (size_t)count;
    }

    return translate_lstr_rc(
        Ldelword(p->alloc, result, argv[0], (size_t)start, used_count));
}

/* ================================================================== */
/*  Phase F — Translation & verification                              */
/* ================================================================== */

static int bif_translate(struct irx_parser *p, int argc, PLstr *argv,
                         PLstr result)
{
    char pad = ' ';
    int rc = irx_bif_opt_char(p, argc, argv, 3, "TRANSLATE", ' ', &pad);
    if (rc != 0)
    {
        return rc;
    }

    PLstr tableo = NULL;
    PLstr tablei = NULL;
    if (argc >= 2 && argv[1] != NULL && argv[1]->len > 0)
    {
        tableo = argv[1];
    }
    if (argc >= 3 && argv[2] != NULL && argv[2]->len > 0)
    {
        tablei = argv[2];
    }

    return translate_lstr_rc(
        Ltranslate(p->alloc, result, argv[0], tableo, tablei, pad));
}

static int bif_verify(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    char opt = 'N';
    int mode = LVERIFY_NOMATCH;
    long start = 1;
    int rc = irx_bif_opt_option(p, argc, argv, 2, "VERIFY",
                                "MN", 'N', &opt);
    if (rc != 0)
    {
        return rc;
    }
    mode = (opt == 'M') ? LVERIFY_MATCH : LVERIFY_NOMATCH;

    rc = irx_bif_opt_whole(p, argc, argv, 3, "VERIFY", 1, &start);
    if (rc != 0)
    {
        return rc;
    }

    size_t pos = Lverify(argv[0], argv[1], mode, (size_t)start);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, (long)pos));
}

static int bif_compare(struct irx_parser *p, int argc, PLstr *argv,
                       PLstr result)
{
    char pad = ' ';
    int rc = irx_bif_opt_char(p, argc, argv, 2, "COMPARE", ' ', &pad);
    if (rc != 0)
    {
        return rc;
    }
    size_t pos = Lcompare(argv[0], argv[1], pad);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, (long)pos));
}

static int bif_abbrev(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    long min = 0;
    int rc = irx_bif_opt_whole(p, argc, argv, 2, "ABBREV", 0, &min);
    if (rc != 0)
    {
        return rc;
    }
    int ok = Labbrev(argv[0], argv[1], (size_t)min);
    return translate_lstr_rc(set_one(p->alloc, result,
                                     ok ? (unsigned char)'1'
                                        : (unsigned char)'0'));
}

/* ------------------------------------------------------------------ */
/*  XRANGE([start] [,end])                                            */
/*                                                                    */
/*  Returns the characters with byte values start..end inclusive.     */
/*  Defaults: start=0x00, end=0xFF. If end < start, the range wraps   */
/*  through 0xFF/0x00 (i.e. XRANGE('FE'x,'01'x) yields 'FEFF0001'x).  */
/* ------------------------------------------------------------------ */

static int bif_xrange(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    unsigned char lo = 0x00;
    unsigned char hi = 0xFF;
    char desc[96];

    if (argc >= 1 && argv[0] != NULL && argv[0]->len > 0)
    {
        if (argv[0]->len != 1)
        {
            memcpy(desc, "XRANGE: argument must be a single character",
                   sizeof("XRANGE: argument must be a single character"));
            irx_cond_raise(p->envblock, SYNTAX_BAD_CALL,
                           ERR40_SINGLE_CHAR, desc);
            return IRXPARS_SYNTAX;
        }
        lo = argv[0]->pstr[0];
    }
    if (argc >= 2 && argv[1] != NULL && argv[1]->len > 0)
    {
        if (argv[1]->len != 1)
        {
            memcpy(desc, "XRANGE: argument must be a single character",
                   sizeof("XRANGE: argument must be a single character"));
            irx_cond_raise(p->envblock, SYNTAX_BAD_CALL,
                           ERR40_SINGLE_CHAR, desc);
            return IRXPARS_SYNTAX;
        }
        hi = argv[1]->pstr[0];
    }

    size_t n;
    if (hi >= lo)
    {
        n = (size_t)(hi - lo) + 1;
    }
    else
    {
        n = (size_t)(0x100 - (int)lo + (int)hi + 1);
    }

    int rc = Lfx(p->alloc, result, n);
    if (rc != LSTR_OK)
    {
        return translate_lstr_rc(rc);
    }
    size_t i;
    unsigned int c = lo;
    for (i = 0; i < n; i++)
    {
        result->pstr[i] = (unsigned char)(c & 0xFF);
        c++;
    }
    result->len = n;
    result->type = LSTRING_TY;
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  FIND(str, phrase)                                                 */
/*                                                                    */
/*  Returns the word position in str at which phrase appears as a     */
/*  contiguous word sequence, or 0 if not found. Whitespace in phrase */
/*  matches any whitespace run in str. Both arguments are compared    */
/*  word-by-word; leading/trailing whitespace is irrelevant.          */
/* ------------------------------------------------------------------ */

static size_t next_word_span(const PLstr s, size_t *cursor,
                             size_t *word_start, size_t *word_len)
{
    while (*cursor < s->len && isspace(s->pstr[*cursor]))
    {
        (*cursor)++;
    }
    if (*cursor >= s->len)
    {
        return 0;
    }
    *word_start = *cursor;
    while (*cursor < s->len && !isspace(s->pstr[*cursor]))
    {
        (*cursor)++;
    }
    *word_len = *cursor - *word_start;
    return 1;
}

static int bif_find(struct irx_parser *p, int argc, PLstr *argv,
                    PLstr result)
{
    (void)argc;
    const PLstr str = argv[0];
    const PLstr phrase = argv[1];

    /* Count words in phrase and stash (offset,len) pairs in a small
     * temporary buffer. REXX FIND has no stated cap on phrase words;
     * 64 words is well beyond typical usage. */
#define FIND_MAX_WORDS 64
    size_t ph_off[FIND_MAX_WORDS];
    size_t ph_len[FIND_MAX_WORDS];
    size_t ph_count = 0;
    size_t cursor = 0;
    size_t wstart = 0;
    size_t wlen = 0;
    while (next_word_span(phrase, &cursor, &wstart, &wlen))
    {
        if (ph_count >= FIND_MAX_WORDS)
        {
            return translate_lstr_rc(long_to_lstr(p->alloc, result, 0));
        }
        ph_off[ph_count] = wstart;
        ph_len[ph_count] = wlen;
        ph_count++;
    }

    if (ph_count == 0)
    {
        return translate_lstr_rc(long_to_lstr(p->alloc, result, 0));
    }

    /* Scan str word-by-word. */
    size_t pos = 0; /* 1-based running word number in str  */
    size_t s_cursor = 0;
    while (s_cursor < str->len)
    {
        /* Advance to start of next word in str. */
        size_t save_cursor = s_cursor;
        size_t s_start = 0;
        size_t s_len = 0;
        if (!next_word_span(str, &s_cursor, &s_start, &s_len))
        {
            break;
        }
        pos++;

        /* Compare against the phrase: first word must match at the
         * current position; remaining ph_count-1 words must follow. */
        if (s_len == ph_len[0] &&
            memcmp(str->pstr + s_start, phrase->pstr + ph_off[0],
                   s_len) == 0)
        {
            size_t look_cursor = s_cursor;
            size_t all_match = 1;
            size_t i;
            for (i = 1; i < ph_count; i++)
            {
                size_t w_start = 0;
                size_t w_len = 0;
                if (!next_word_span(str, &look_cursor, &w_start, &w_len))
                {
                    all_match = 0;
                    break;
                }
                if (w_len != ph_len[i] ||
                    memcmp(str->pstr + w_start,
                           phrase->pstr + ph_off[i], w_len) != 0)
                {
                    all_match = 0;
                    break;
                }
            }
            if (all_match)
            {
                return translate_lstr_rc(
                    long_to_lstr(p->alloc, result, (long)pos));
            }
        }

        (void)save_cursor; /* silence unused-var in release builds */
    }

    return translate_lstr_rc(long_to_lstr(p->alloc, result, 0));
#undef FIND_MAX_WORDS
}

/* ================================================================== */
/*  Registration                                                      */
/* ================================================================== */

static const struct irx_bif_entry g_bifstr_table[] = {
    /* Phase B */
    {"LENGTH", 1, 1, bif_length},
    {"LEFT", 2, 3, bif_left},
    {"RIGHT", 2, 3, bif_right},
    {"SUBSTR", 2, 4, bif_substr},
    {"POS", 2, 3, bif_pos},
    {"INDEX", 2, 3, bif_index},
    {"LASTPOS", 2, 3, bif_lastpos},
    /* Phase C */
    {"WORDS", 1, 1, bif_words},
    {"WORD", 2, 2, bif_word},
    {"WORDINDEX", 2, 2, bif_wordindex},
    {"WORDLENGTH", 2, 2, bif_wordlength},
    {"SUBWORD", 2, 3, bif_subword},
    {"WORDPOS", 2, 3, bif_wordpos},
    /* Phase D */
    {"CENTER", 2, 3, bif_center},
    {"CENTRE", 2, 3, bif_center},
    {"STRIP", 1, 3, bif_strip},
    {"SPACE", 1, 3, bif_space},
    {"JUSTIFY", 2, 3, bif_justify},
    {"COPIES", 2, 2, bif_copies},
    {"REVERSE", 1, 1, bif_reverse},
    /* Phase E */
    {"INSERT", 2, 5, bif_insert},
    {"OVERLAY", 2, 5, bif_overlay},
    {"DELSTR", 2, 3, bif_delstr},
    {"DELWORD", 2, 3, bif_delword},
    /* Phase F */
    {"TRANSLATE", 1, 4, bif_translate},
    {"VERIFY", 2, 4, bif_verify},
    {"COMPARE", 2, 3, bif_compare},
    {"ABBREV", 2, 3, bif_abbrev},
    {"XRANGE", 0, 2, bif_xrange},
    {"FIND", 2, 2, bif_find},
    /* Sentinel */
    {"", 0, 0, NULL}};

#define BIFSTR_COUNT \
    ((int)(sizeof(g_bifstr_table) / sizeof(g_bifstr_table[0])))

int irx_bifstr_register(struct envblock *env, struct irx_bif_registry *reg)
{
    (void)set_empty; /* kept for future use */
    return irx_bif_register_table(env, reg, g_bifstr_table,
                                  BIFSTR_COUNT);
}
