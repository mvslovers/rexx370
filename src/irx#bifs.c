/* ------------------------------------------------------------------ */
/*  irx#bifs.c - REXX/370 String Built-in Functions (WP-21a)         */
/*                                                                    */
/*  Implements the ~29 SC28-1883-0 §4 string BIFs as thin wrappers    */
/*  around lstring370 primitives. Argument parsing and validation     */
/*  goes through the helpers in irx#bif.c, which surface SYNTAX 40.x  */
/*  conditions on failure.                                            */
/*                                                                    */
/*  All state is per-environment; no globals, no statics holding      */
/*  mutable data. Registration is one-shot via irx_bif_register_all() */
/*  which wires up every built-in — string BIFs from this module plus */
/*  parser-internal BIFs (ARG) defined in src/irx#pars.c.             */
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
#include "irxarith.h"
#include "irxbif.h"
#include "irxbifs.h"
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

/* Implementation cap on the phrase argument. SC28-1883-0 §4 does not
 * define a limit, but real REXX programs never approach this value —
 * 1024 words would be 6+ KB of prose. Exceeding the cap raises
 * SYNTAX 40.ERR40_ARG_LENGTH so the caller learns immediately that
 * the argument is out of range, rather than silently getting 0. */
#define FIND_MAX_WORDS 1024

static int bif_find(struct irx_parser *p, int argc, PLstr *argv,
                    PLstr result)
{
    (void)argc;
    const PLstr str = argv[0];
    const PLstr phrase = argv[1];

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
            char desc[64];
            sprintf(desc, "FIND: phrase exceeds %d words", FIND_MAX_WORDS);
            irx_cond_raise(p->envblock, SYNTAX_BAD_CALL,
                           ERR40_ARG_LENGTH, desc);
            return IRXPARS_SYNTAX;
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
}

/* ================================================================== */
/*  Phase G — Numeric BIFs (WP-21b Phase C)                           */
/*                                                                    */
/*  All seven BIFs delegate to IRXARITH primitives (irx_arith_op,     */
/*  irx_arith_compare, irx_arith_trunc, irx_arith_format,             */
/*  irx_arith_to_digits). Non-numeric operands raise SYNTAX 41.1      */
/*  (bad arithmetic conversion); argument-shape errors raise 40.x     */
/*  via the irx#bif.c helpers.                                        */
/* ================================================================== */

static void raise_nonnumeric(struct irx_parser *p, const char *bif_name)
{
    char desc[64];
    size_t nlen = strlen(bif_name);
    const char *suffix = ": argument is not a valid number";
    size_t slen = strlen(suffix);
    if (nlen + slen >= sizeof(desc))
    {
        nlen = sizeof(desc) - slen - 1;
    }
    memcpy(desc, bif_name, nlen);
    memcpy(desc + nlen, suffix, slen);
    desc[nlen + slen] = '\0';
    irx_cond_raise(p->envblock, SYNTAX_BAD_ARITH, ERR41_NONNUMERIC, desc);
}

/* Return normalized argv[i] (sign preserved) in out. Non-numeric
 * operand → SYNTAX 41.1. Uses ADD with 0 to invoke the standard
 * number formatter without changing the value. */
static int normalize_preserve(struct irx_parser *p, PLstr in, PLstr out,
                              const char *bif_name)
{
    unsigned char zero_byte = (unsigned char)'0';
    Lstr zero;
    zero.pstr = &zero_byte;
    zero.len = 1;
    zero.maxlen = 1;
    zero.type = LSTRING_TY;

    int rc = irx_arith_op(p->envblock, in, &zero, ARITH_ADD, out);
    if (rc == IRXPARS_SYNTAX)
    {
        raise_nonnumeric(p, bif_name);
    }
    return rc;
}

static int bif_max_min(struct irx_parser *p, int argc, PLstr *argv,
                       PLstr result, int want_max, const char *bif_name)
{
    if (argc < 1)
    {
        char desc[32];
        memcpy(desc, bif_name, strlen(bif_name) + 1);
        irx_cond_raise(p->envblock, SYNTAX_BAD_CALL, ERR40_TOO_FEW_ARGS,
                       desc);
        return IRXPARS_SYNTAX;
    }

    /* Every positional argument must be present; REXX forbids omitted
     * operands to MAX/MIN. */
    int i;
    for (i = 0; i < argc; i++)
    {
        if (argv[i] == NULL || argv[i]->len == 0)
        {
            char desc[64];
            size_t nlen = strlen(bif_name);
            memcpy(desc, bif_name, nlen);
            memcpy(desc + nlen, ": argument omitted",
                   sizeof(": argument omitted"));
            irx_cond_raise(p->envblock, SYNTAX_BAD_CALL,
                           ERR40_TOO_FEW_ARGS, desc);
            return IRXPARS_SYNTAX;
        }
    }

    int winner = 0;
    for (i = 1; i < argc; i++)
    {
        int cmp = 0;
        int rc = irx_arith_compare(p->envblock, argv[winner], argv[i],
                                   &cmp);
        if (rc == IRXPARS_SYNTAX)
        {
            raise_nonnumeric(p, bif_name);
            return rc;
        }
        if (rc != IRXPARS_OK)
        {
            return rc;
        }
        if ((want_max && cmp < 0) || (!want_max && cmp > 0))
        {
            winner = i;
        }
    }

    /* Single-arg fast path needs its own validation: the compare loop
     * above never runs when argc == 1. Use normalize_preserve so the
     * returned value also obeys NUMERIC DIGITS / FORM. */
    return normalize_preserve(p, argv[winner], result, bif_name);
}

static int bif_max(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    return bif_max_min(p, argc, argv, result, 1, "MAX");
}

static int bif_min(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    return bif_max_min(p, argc, argv, result, 0, "MIN");
}

static int bif_abs(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    (void)argc;
    int rc = irx_arith_op(p->envblock, argv[0], NULL, ARITH_ABS, result);
    if (rc == IRXPARS_SYNTAX)
    {
        raise_nonnumeric(p, "ABS");
    }
    return rc;
}

static int bif_sign(struct irx_parser *p, int argc, PLstr *argv,
                    PLstr result)
{
    (void)argc;
    unsigned char zero_byte = (unsigned char)'0';
    Lstr zero;
    zero.pstr = &zero_byte;
    zero.len = 1;
    zero.maxlen = 1;
    zero.type = LSTRING_TY;

    int cmp = 0;
    int rc = irx_arith_compare(p->envblock, argv[0], &zero, &cmp);
    if (rc == IRXPARS_SYNTAX)
    {
        raise_nonnumeric(p, "SIGN");
        return rc;
    }
    if (rc != IRXPARS_OK)
    {
        return rc;
    }

    long v = (cmp < 0) ? -1L : (cmp > 0 ? 1L : 0L);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, v));
}

/* Reject integer arguments exceeding NUMERIC DIGITS_MAX before the
 * arithmetic layer collapses them into a generic SYNTAX. Surfaces the
 * failure as 40.4 (ERR40_ARG_LENGTH) per SC28-1883-0 §18 error-code
 * hygiene. */
static int bif_require_digits_range(struct irx_parser *p, long v,
                                    const char *bif_name,
                                    const char *arg_name)
{
    char desc[96];
    if (v <= NUMERIC_DIGITS_MAX)
    {
        return IRXPARS_OK;
    }
    int n = sprintf(desc, "%s: %s exceeds NUMERIC DIGITS max (%d)",
                    bif_name, arg_name, NUMERIC_DIGITS_MAX);
    (void)n;
    irx_cond_raise(p->envblock, SYNTAX_BAD_CALL, ERR40_ARG_LENGTH, desc);
    return IRXPARS_SYNTAX;
}

static int bif_trunc(struct irx_parser *p, int argc, PLstr *argv,
                     PLstr result)
{
    long decimals = 0;
    int rc = irx_bif_opt_whole(p, argc, argv, 1, "TRUNC", 0, &decimals);
    if (rc != 0)
    {
        return rc;
    }
    rc = bif_require_digits_range(p, decimals, "TRUNC", "decimals");
    if (rc != IRXPARS_OK)
    {
        return rc;
    }
    rc = irx_arith_trunc(p->envblock, argv[0], decimals, result);
    if (rc == IRXPARS_SYNTAX)
    {
        /* Out-of-range decimals was rejected above, so the remaining
         * failure path is a non-numeric input operand. */
        raise_nonnumeric(p, "TRUNC");
    }
    return rc;
}

static int bif_format(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    long before = IRX_FORMAT_OMIT;
    long after = IRX_FORMAT_OMIT;
    long expp = IRX_FORMAT_OMIT;
    long expt = IRX_FORMAT_OMIT;

    /* opt_whole rejects negative user input via whole_nonneg and leaves
     * `out` at the default (IRX_FORMAT_OMIT) when the argument is
     * absent or empty, so the OMIT sentinel can never escape from a
     * negative caller value. */
    int rc = irx_bif_opt_whole(p, argc, argv, 1, "FORMAT",
                               IRX_FORMAT_OMIT, &before);
    if (rc != 0)
    {
        return rc;
    }
    rc = irx_bif_opt_whole(p, argc, argv, 2, "FORMAT",
                           IRX_FORMAT_OMIT, &after);
    if (rc != 0)
    {
        return rc;
    }
    rc = irx_bif_opt_whole(p, argc, argv, 3, "FORMAT",
                           IRX_FORMAT_OMIT, &expp);
    if (rc != 0)
    {
        return rc;
    }
    rc = irx_bif_opt_whole(p, argc, argv, 4, "FORMAT",
                           IRX_FORMAT_OMIT, &expt);
    if (rc != 0)
    {
        return rc;
    }

    /* Guard each integer arg so out-of-range values surface as 40.4
     * rather than a generic 41.1 when the arithmetic layer refuses
     * them. IRX_FORMAT_OMIT (-1) is below the threshold and passes. */
    if ((rc = bif_require_digits_range(p, before, "FORMAT", "before")) !=
            IRXPARS_OK ||
        (rc = bif_require_digits_range(p, after, "FORMAT", "after")) !=
            IRXPARS_OK ||
        (rc = bif_require_digits_range(p, expp, "FORMAT", "expp")) !=
            IRXPARS_OK ||
        (rc = bif_require_digits_range(p, expt, "FORMAT", "expt")) !=
            IRXPARS_OK)
    {
        return rc;
    }

    rc = irx_arith_format(p->envblock, argv[0], before, after, expp, expt,
                          result);
    if (rc == IRXPARS_SYNTAX)
    {
        raise_nonnumeric(p, "FORMAT");
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/*  RANDOM([min][,[max][,[seed]]])                                    */
/*                                                                    */
/*  Simple 32-bit linear congruential generator seeded in the work    */
/*  block (wkbi_random_seed). Constants taken from the Numerical      */
/*  Recipes / glibc LCG (multiplier 1103515245, increment 12345).     */
/* ------------------------------------------------------------------ */

#define RANDOM_DEFAULT_MIN  0L
#define RANDOM_DEFAULT_MAX  999L
#define RANDOM_MAX_RANGE    100000L
#define RANDOM_LCG_MULT     1103515245U
#define RANDOM_LCG_INC      12345U
#define RANDOM_LCG_OUT_SHFT 16
#define RANDOM_LCG_OUT_MASK 0x7FFFU

static unsigned int lcg_next(unsigned int state)
{
    return state * RANDOM_LCG_MULT + RANDOM_LCG_INC;
}

static int bif_random(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    long min_val = RANDOM_DEFAULT_MIN;
    long max_val = RANDOM_DEFAULT_MAX;
    int have_seed = 0;
    long seed_val = 0;

    if (argc >= 1 && argv[0] != NULL && argv[0]->len > 0)
    {
        int rc = irx_bif_whole_nonneg(p, argv, 0, "RANDOM", &min_val);
        if (rc != 0)
        {
            return rc;
        }
    }
    if (argc >= 2 && argv[1] != NULL && argv[1]->len > 0)
    {
        int rc = irx_bif_whole_nonneg(p, argv, 1, "RANDOM", &max_val);
        if (rc != 0)
        {
            return rc;
        }
    }
    if (argc >= 3 && argv[2] != NULL && argv[2]->len > 0)
    {
        int rc = irx_bif_whole_nonneg(p, argv, 2, "RANDOM", &seed_val);
        if (rc != 0)
        {
            return rc;
        }
        have_seed = 1;
    }

    if (max_val < min_val)
    {
        char desc[] = "RANDOM: max must be >= min";
        irx_cond_raise(p->envblock, SYNTAX_BAD_CALL, ERR40_NONNEG_WHOLE,
                       desc);
        return IRXPARS_SYNTAX;
    }
    if (max_val - min_val > RANDOM_MAX_RANGE)
    {
        char desc[] = "RANDOM: range exceeds 100000";
        irx_cond_raise(p->envblock, SYNTAX_BAD_CALL, ERR40_ARG_LENGTH,
                       desc);
        return IRXPARS_SYNTAX;
    }

    struct irx_wkblk_int *wk = NULL;
    if (p->envblock != NULL && p->envblock->envblock_userfield != NULL)
    {
        wk = (struct irx_wkblk_int *)p->envblock->envblock_userfield;
    }

    if (have_seed)
    {
        if (wk != NULL)
        {
            wk->wkbi_random_seed = (unsigned int)seed_val;
        }
        /* SC28-1883-0: seeding returns '0' without consuming a value. */
        return translate_lstr_rc(long_to_lstr(p->alloc, result, 0L));
    }

    unsigned int state = (wk != NULL) ? wk->wkbi_random_seed : 0U;
    state = lcg_next(state);
    if (wk != NULL)
    {
        wk->wkbi_random_seed = state;
    }

    long range = max_val - min_val + 1;
    unsigned int raw = (state >> RANDOM_LCG_OUT_SHFT) & RANDOM_LCG_OUT_MASK;
    long value = min_val + (long)(raw % (unsigned long)range);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, value));
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
    /* Phase G — Numeric BIFs (WP-21b Phase C) */
    {"MAX", 1, IRX_MAX_ARGS, bif_max},
    {"MIN", 1, IRX_MAX_ARGS, bif_min},
    {"ABS", 1, 1, bif_abs},
    {"SIGN", 1, 1, bif_sign},
    {"TRUNC", 1, 2, bif_trunc},
    {"FORMAT", 1, 5, bif_format},
    {"RANDOM", 0, 3, bif_random},
    /* Sentinel */
    {"", 0, 0, NULL}};

#define BIFSTR_COUNT \
    ((int)(sizeof(g_bifstr_table) / sizeof(g_bifstr_table[0])))

int irx_bif_register_all(struct envblock *env, struct irx_bif_registry *reg)
{
    int rc = irx_bif_register_table(env, reg, g_bifstr_table,
                                    BIFSTR_COUNT);
    if (rc != IRX_BIF_OK)
    {
        return rc;
    }

    /* ARG() is implemented in src/irx#pars.c because it reads the
     * parser-private call_args / call_argc fields. Its registration
     * is consolidated here so there is a single entry point for all
     * built-ins. */
    return irx_bif_register(env, reg, "ARG", 0, 2, irx_pars_bif_arg);
}
