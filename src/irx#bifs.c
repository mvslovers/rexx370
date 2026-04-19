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
#include "irxfunc.h"
#include "irxlstr.h"
#include "irxpars.h"
#include "irxtokn.h"
#include "irxvpool.h"
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

/* Compose "BIFNAME<suffix>" into a fixed buffer with runtime clamp and
 * raise the condition. A clamp keeps callers safe even if a future BIF
 * has a long name that overruns the descriptor. Shared by
 * raise_nonnumeric and bif_max_min's "argument omitted" path. */
static void raise_bif_cond(struct irx_parser *p, int code, int subcode,
                           const char *bif_name, const char *suffix)
{
    char desc[80];
    size_t nlen = strlen(bif_name);
    size_t slen = strlen(suffix);
    if (nlen + slen >= sizeof(desc))
    {
        nlen = sizeof(desc) - slen - 1;
    }
    memcpy(desc, bif_name, nlen);
    memcpy(desc + nlen, suffix, slen);
    desc[nlen + slen] = '\0';
    irx_cond_raise(p->envblock, code, subcode, desc);
}

static void raise_nonnumeric(struct irx_parser *p, const char *bif_name)
{
    raise_bif_cond(p, SYNTAX_BAD_ARITH, ERR41_NONNUMERIC, bif_name,
                   ": argument is not a valid number");
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
    /* The dispatcher enforces min_args=1 via g_bifstr_table so argc>=1
     * is an invariant here; no runtime guard needed. */

    /* Every positional argument must be present; REXX forbids omitted
     * operands to MAX/MIN. */
    int i;
    for (i = 0; i < argc; i++)
    {
        if (argv[i] == NULL || argv[i]->len == 0)
        {
            raise_bif_cond(p, SYNTAX_BAD_CALL, ERR40_TOO_FEW_ARGS,
                           bif_name, ": argument omitted");
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
     * returned value also obeys NUMERIC DIGITS / FORM.
     *
     * Performance note: this ARITH_ADD(0) is a second BCD pass after
     * the compare loop already validated every operand. MAX/MIN is not
     * a hot path in typical REXX workloads, and the normalization pass
     * is what makes the result honour the active NUMERIC DIGITS / FORM
     * settings — irx_arith_compare writes only a -1/0/+1 verdict, never
     * a formatted Lstr. If profiling ever flags this doubled work as a
     * real bottleneck, add a public irx_arith_normalize() helper shaped
     * like irx_arith_trunc and call that here instead. No follow-up
     * issue was filed: without a measured trigger it would just age in
     * the backlog. */
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
    sprintf(desc, "%s: %s exceeds NUMERIC DIGITS max (%d)", bif_name,
            arg_name, NUMERIC_DIGITS_MAX);
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
/*                                                                    */
/*  Output bit width: two LCG steps yield 30 bits of entropy by       */
/*  concatenating the 15 high-order bits of each step. The low-order  */
/*  bits of an LCG have very short periods (bit 1 repeats every 4    */
/*  steps) so we intentionally discard them and use only the upper   */
/*  half of each state. 30 bits cover the full RANDOM_MAX_RANGE of   */
/*  100000 without modulus collapse. A single-step 15-bit output     */
/*  would silently truncate any range above 32767.                   */
/* ------------------------------------------------------------------ */

#define RANDOM_DEFAULT_MIN   0L
#define RANDOM_DEFAULT_MAX   999L
#define RANDOM_MAX_RANGE     100000L
#define RANDOM_LCG_MULT      1103515245U
#define RANDOM_LCG_INC       12345U
#define RANDOM_LCG_HI_SHIFT  16      /* drop short-period low bits      */
#define RANDOM_LCG_HI_MASK   0x7FFFU /* 15 good bits per step          */
#define RANDOM_LCG_HI_BITS   15
#define RANDOM_LCG_OUT_BITS  30 /* two steps -> 30-bit output     */
#define RANDOM_LCG_OUT_SCALE (1UL << RANDOM_LCG_OUT_BITS)

static unsigned int lcg_next(unsigned int state)
{
    return state * RANDOM_LCG_MULT + RANDOM_LCG_INC;
}

/* Two-step LCG combiner: returns a 30-bit value in [0, 2^30) and
 * updates *state in place. Callers persist *state back into the work
 * block so the next RANDOM() call continues the sequence. */
static unsigned long lcg_next_30(unsigned int *state)
{
    *state = lcg_next(*state);
    unsigned int hi = (*state >> RANDOM_LCG_HI_SHIFT) & RANDOM_LCG_HI_MASK;
    *state = lcg_next(*state);
    unsigned int lo = (*state >> RANDOM_LCG_HI_SHIFT) & RANDOM_LCG_HI_MASK;
    return ((unsigned long)hi << RANDOM_LCG_HI_BITS) | (unsigned long)lo;
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
    unsigned long raw30 = lcg_next_30(&state);
    if (wk != NULL)
    {
        wk->wkbi_random_seed = state;
    }

    long range = max_val - min_val + 1;
    long value = min_val + (long)(raw30 % (unsigned long)range);
    return translate_lstr_rc(long_to_lstr(p->alloc, result, value));
}

/* ================================================================== */
/*  Phase H — Conversion BIFs (WP-21b Phase D)                        */
/*                                                                    */
/*  C2X / X2C / B2X / X2B are pure byte-level conversions; they       */
/*  neither inspect nor produce REXX numbers. C2D / X2D / D2C / D2X   */
/*  bridge bytes and decimal numbers. Small inputs go through direct  */
/*  long arithmetic; larger values route through                      */
/*  irx_arith_from_digits / irx_arith_to_digits (WP-21b Phase B).     */
/*                                                                    */
/*  Charset note: the conversion BIFs operate on raw byte values, not */
/*  on characters as symbols. On EBCDIC MVS, D2C(193) produces the    */
/*  byte 0xC1 which happens to render as 'A'; on ASCII Linux,         */
/*  D2C(65) produces 0x41 which also renders as 'A'. The interpreter  */
/*  is byte-identical across platforms — only the rendering differs.  */
/* ================================================================== */

/* Hex-digit lookup. Returns 0..15 for the three legal digit ranges,  */
/* -1 for any other character. The contiguous-range checks hold on    */
/* both ASCII and EBCDIC because '0'..'9', 'A'..'F', and 'a'..'f' are */
/* each a single contiguous code-point block in both encodings.       */
static int hex_val(unsigned char c)
{
    if (c >= (unsigned char)'0' && c <= (unsigned char)'9')
    {
        return (int)(c - (unsigned char)'0');
    }
    if (c >= (unsigned char)'A' && c <= (unsigned char)'F')
    {
        return (int)(c - (unsigned char)'A') + 10;
    }
    if (c >= (unsigned char)'a' && c <= (unsigned char)'f')
    {
        return (int)(c - (unsigned char)'a') + 10;
    }
    return -1;
}

/* Produce an upper-case hex digit for nibble value 0..15. */
static unsigned char hex_char(int v)
{
    if (v < 10)
    {
        return (unsigned char)((int)'0' + v);
    }
    return (unsigned char)((int)'A' + (v - 10));
}

/* SC28-1883-0 treats only blank as whitespace inside hex/binary       */
/* literals. Tab, newline, etc. are not skipped.                       */
static int is_blank(unsigned char c)
{
    return c == (unsigned char)' ';
}

static void raise_bad_hex(struct irx_parser *p, const char *bif_name)
{
    raise_bif_cond(p, SYNTAX_BAD_CALL, ERR40_BAD_HEX, bif_name,
                   ": argument 1 must be a hexadecimal string");
}

static void raise_bad_binary(struct irx_parser *p, const char *bif_name)
{
    raise_bif_cond(p, SYNTAX_BAD_CALL, ERR40_BAD_BINARY, bif_name,
                   ": argument 1 must be a binary string");
}

/* ------------------------------------------------------------------ */
/*  Byte-based conversions (no IRXARITH involvement)                  */
/* ------------------------------------------------------------------ */

static int bif_c2x(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    (void)argc;
    PLstr in = argv[0];
    size_t in_len = in->len;
    size_t out_len = in_len * 2;
    int rc = Lfx(p->alloc, result, out_len);
    if (rc != LSTR_OK)
    {
        return translate_lstr_rc(rc);
    }
    size_t i;
    for (i = 0; i < in_len; i++)
    {
        unsigned char b = in->pstr[i];
        result->pstr[2 * i] = hex_char((b >> 4) & 0x0F);
        result->pstr[2 * i + 1] = hex_char(b & 0x0F);
    }
    result->len = out_len;
    result->type = LSTRING_TY;
    return IRXPARS_OK;
}

static int bif_x2c(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    (void)argc;
    PLstr in = argv[0];
    size_t count = 0;
    size_t i;
    for (i = 0; i < in->len; i++)
    {
        unsigned char c = in->pstr[i];
        if (is_blank(c))
        {
            continue;
        }
        if (hex_val(c) < 0)
        {
            raise_bad_hex(p, "X2C");
            return IRXPARS_SYNTAX;
        }
        count++;
    }

    size_t out_len = (count + 1) / 2;
    int rc = Lfx(p->alloc, result, out_len);
    if (rc != LSTR_OK)
    {
        return translate_lstr_rc(rc);
    }
    /* Odd total count pads a leading '0' nibble; the very first input  */
    /* digit becomes the LOW nibble of byte[0].                         */
    int nibble_count = (int)(count & 1);
    int cur = 0;
    size_t out_idx = 0;
    for (i = 0; i < in->len; i++)
    {
        unsigned char c = in->pstr[i];
        if (is_blank(c))
        {
            continue;
        }
        cur = (cur << 4) | hex_val(c);
        nibble_count++;
        if (nibble_count == 2)
        {
            result->pstr[out_idx++] = (unsigned char)cur;
            cur = 0;
            nibble_count = 0;
        }
    }
    result->len = out_len;
    result->type = LSTRING_TY;
    return IRXPARS_OK;
}

static int bif_b2x(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    (void)argc;
    PLstr in = argv[0];
    size_t count = 0;
    size_t i;
    for (i = 0; i < in->len; i++)
    {
        unsigned char c = in->pstr[i];
        if (is_blank(c))
        {
            continue;
        }
        if (c != (unsigned char)'0' && c != (unsigned char)'1')
        {
            raise_bad_binary(p, "B2X");
            return IRXPARS_SYNTAX;
        }
        count++;
    }

    size_t out_len = (count + 3) / 4;
    int rc = Lfx(p->alloc, result, out_len);
    if (rc != LSTR_OK)
    {
        return translate_lstr_rc(rc);
    }
    /* Left-pad zero bits so 4-bit groups align from the right. */
    int pad = (int)((4 - (count & 3)) & 3);
    int bit_count = pad;
    int cur = 0;
    size_t out_idx = 0;
    for (i = 0; i < in->len; i++)
    {
        unsigned char c = in->pstr[i];
        if (is_blank(c))
        {
            continue;
        }
        cur = (cur << 1) | (int)(c - (unsigned char)'0');
        bit_count++;
        if (bit_count == 4)
        {
            result->pstr[out_idx++] = hex_char(cur);
            cur = 0;
            bit_count = 0;
        }
    }
    result->len = out_len;
    result->type = LSTRING_TY;
    return IRXPARS_OK;
}

static int bif_x2b(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    (void)argc;
    PLstr in = argv[0];
    size_t count = 0;
    size_t i;
    for (i = 0; i < in->len; i++)
    {
        unsigned char c = in->pstr[i];
        if (is_blank(c))
        {
            continue;
        }
        if (hex_val(c) < 0)
        {
            raise_bad_hex(p, "X2B");
            return IRXPARS_SYNTAX;
        }
        count++;
    }

    size_t out_len = count * 4;
    int rc = Lfx(p->alloc, result, out_len);
    if (rc != LSTR_OK)
    {
        return translate_lstr_rc(rc);
    }
    size_t out_idx = 0;
    for (i = 0; i < in->len; i++)
    {
        unsigned char c = in->pstr[i];
        if (is_blank(c))
        {
            continue;
        }
        int v = hex_val(c);
        int b;
        for (b = 3; b >= 0; b--)
        {
            result->pstr[out_idx++] =
                (unsigned char)((int)'0' + ((v >> b) & 1));
        }
    }
    result->len = out_len;
    result->type = LSTRING_TY;
    return IRXPARS_OK;
}

/* ------------------------------------------------------------------ */
/*  BCD helpers for the large-value paths                             */
/* ------------------------------------------------------------------ */

/* Multiply the BCD digit array (MSB first) by 256 and add `byte_val`. */
/* Returns the new length or -1 if the buffer is too small to hold the */
/* result. At most three digits can be prepended per call (carry ≤ 255 */
/* times residual ≤ ~2304, which rolls up to at most three decimals).  */
/*                                                                      */
/* Performance note: the prepend loop uses memmove per new digit, so a  */
/* k-byte input costs O(k²) bytes of movement worst case (~7.5 MB for   */
/* k=1000). A "prepend counter + single final shift" variant would      */
/* reduce this to O(k). Left as-is until profiling against a real       */
/* hotpath justifies the change — C2D inputs of that magnitude are      */
/* pathological in practice.                                            */
static int bcd_mul256_add(char *digits, int len, int cap, int byte_val)
{
    int i;
    int carry = byte_val;
    for (i = len - 1; i >= 0; i--)
    {
        int v = (int)(unsigned char)digits[i] * 256 + carry;
        digits[i] = (char)(v % 10);
        carry = v / 10;
    }
    int new_len = len;
    while (carry > 0)
    {
        if (new_len >= cap)
        {
            return -1;
        }
        memmove(digits + 1, digits, (size_t)new_len);
        digits[0] = (char)(carry % 10);
        carry /= 10;
        new_len++;
    }
    return new_len;
}

/* Given a BCD digit array (MSB first) representing |value|, extract   */
/* bytes LSB-first via repeated division by 256. Up to `max_bytes` are */
/* written into `bytes_out`. The digit array is consumed. Returns the  */
/* count written.                                                      */
/*                                                                     */
/* Loop invariant for the inner division: each `cur = rem*10 + d[i]`   */
/* is < 256*10 + 9 = 2569, so `cur/256 ∈ 0..9` — every iteration emits */
/* a legitimate single decimal digit, no normalization needed.         */
static int bcd_to_bytes_lsb(char *digits, int digits_len,
                            unsigned char *bytes_out, int max_bytes)
{
    int count = 0;
    int len = digits_len;
    while (count < max_bytes && len > 0)
    {
        int all_zero = 1;
        int i;
        for (i = 0; i < len; i++)
        {
            if (digits[i] != 0)
            {
                all_zero = 0;
                break;
            }
        }
        if (all_zero)
        {
            break;
        }
        int rem = 0;
        int first_nz = -1;
        for (i = 0; i < len; i++)
        {
            int cur = rem * 10 + (int)(unsigned char)digits[i];
            digits[i] = (char)(cur / 256);
            rem = cur % 256;
            if (first_nz < 0 && digits[i] != 0)
            {
                first_nz = i;
            }
        }
        bytes_out[count++] = (unsigned char)rem;
        if (first_nz < 0)
        {
            len = 0;
        }
        else if (first_nz > 0)
        {
            memmove(digits, digits + first_nz,
                    (size_t)(len - first_nz));
            len -= first_nz;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/*  C2D / X2D core                                                    */
/* ------------------------------------------------------------------ */

/* Emit a signed decimal REXX number from a magnitude byte array (MSB   */
/* first, already the absolute value — no two's-complement logic here). */
/* `sign` is 0 for non-negative, 1 for negative.                        */
/*                                                                       */
/* Small path: byte_len ≤ 3 always fits signed long — uval ≤ 2^24-1,    */
/* sval ∈ [-2^24+1, 2^24-1]. Four-byte and larger magnitudes route       */
/* through the BCD path; keeping the small-path threshold at 3 sidesteps */
/* platform-specific `long` widths (32 bit on MVS, 64 on the Linux       */
/* cross-compile host).                                                  */
static int compute_c2d(struct irx_parser *p,
                       const unsigned char *mag, size_t byte_len,
                       int sign, PLstr result)
{
    /* Strip leading zero bytes so the small-path check sees the true    */
    /* magnitude width. */
    while (byte_len > 0 && mag[0] == 0)
    {
        mag++;
        byte_len--;
    }

    if (byte_len == 0)
    {
        return translate_lstr_rc(long_to_lstr(p->alloc, result, 0L));
    }

    if (byte_len <= 3)
    {
        unsigned long uval = 0;
        size_t i;
        for (i = 0; i < byte_len; i++)
        {
            uval = (uval << 8) | mag[i];
        }
        long sval = sign ? -(long)uval : (long)uval;
        return translate_lstr_rc(long_to_lstr(p->alloc, result, sval));
    }

    /* BCD path. Scratch: digits upper bound is byte_len * 3 (each byte  */
    /* contributes ≤ 2.41 decimal digits; round up to 3 for slack).      */
    /* The (int) cast is safe in practice: byte_len is bounded by the    */
    /* input Lstr length which is well under INT_MAX/3 on 24-bit MVS.    */
    int digits_cap = (int)byte_len * 3 + 4;
    void *tmp = NULL;
    int arc = irxstor(RXSMGET, digits_cap, &tmp, p->envblock);
    if (arc != 0)
    {
        return IRXPARS_NOMEM;
    }
    char *digits = (char *)tmp;

    int digits_len = 0;
    size_t i;
    for (i = 0; i < byte_len; i++)
    {
        digits_len = bcd_mul256_add(digits, digits_len, digits_cap,
                                    (int)mag[i]);
        if (digits_len < 0)
        {
            void *p1 = digits;
            irxstor(RXSMFRE, 0, &p1, p->envblock);
            return IRXPARS_NOMEM;
        }
    }

    int rc = irx_arith_from_digits(p->envblock, digits, digits_len,
                                   sign ? 1 : 0, 0L, result);

    void *p1 = digits;
    irxstor(RXSMFRE, 0, &p1, p->envblock);
    return rc;
}

/* Negate a bit-width-bounded byte array in place (two's complement)     */
/* and return the resulting magnitude (low `byte_len` bytes). When       */
/* `pad_odd_nibble` is non-zero, byte[0]'s high nibble is masked before  */
/* and after the flip so the eff_hex-nibble-wide semantics are honored.  */
static void twos_complement_bytes(unsigned char *bytes, size_t byte_len,
                                  int pad_odd_nibble)
{
    if (byte_len == 0)
    {
        return;
    }
    if (pad_odd_nibble)
    {
        bytes[0] &= 0x0F;
    }
    size_t i;
    for (i = 0; i < byte_len; i++)
    {
        bytes[i] = (unsigned char)(~bytes[i]);
    }
    int carry = 1;
    size_t j;
    for (j = byte_len; j > 0 && carry != 0;)
    {
        j--;
        int s = (int)bytes[j] + carry;
        bytes[j] = (unsigned char)(s & 0xFF);
        carry = (s >> 8) & 1;
    }
    if (pad_odd_nibble)
    {
        bytes[0] &= 0x0F;
    }
}

static int bif_c2d(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    PLstr in = argv[0];
    long n = 0;
    int n_given = 0;

    if (argc >= 2 && argv[1] != NULL && argv[1]->len > 0)
    {
        int rc = irx_bif_whole_nonneg(p, argv, 1, "C2D", &n);
        if (rc != 0)
        {
            return rc;
        }
        n_given = 1;
    }

    const unsigned char *bytes = in->pstr;
    size_t byte_len = in->len;
    unsigned char *work = NULL;

    if (n_given)
    {
        if (n == 0)
        {
            return translate_lstr_rc(long_to_lstr(p->alloc, result, 0L));
        }
        if (byte_len > (size_t)n)
        {
            /* Keep the rightmost n bytes, drop the rest. */
            bytes += byte_len - (size_t)n;
            byte_len = (size_t)n;
        }
        else if (byte_len < (size_t)n)
        {
            /* Left-pad with 0x00 to exactly n bytes. */
            void *tmp = NULL;
            int arc = irxstor(RXSMGET, (int)n, &tmp, p->envblock);
            if (arc != 0)
            {
                return IRXPARS_NOMEM;
            }
            work = (unsigned char *)tmp;
            size_t pad = (size_t)n - byte_len;
            memset(work, 0, pad);
            if (byte_len > 0)
            {
                memcpy(work + pad, bytes, byte_len);
            }
            bytes = work;
            byte_len = (size_t)n;
        }
    }

    /* Determine sign. For n_given with MSB of byte[0] set, compute the  */
    /* magnitude (two's complement) in a scratch buffer and hand that to */
    /* compute_c2d with sign=1. Otherwise treat as positive.             */
    int sign = 0;
    if (n_given && byte_len > 0 && (bytes[0] & 0x80U) != 0)
    {
        sign = 1;
        if (work == NULL)
        {
            /* Copy into a mutable scratch buffer — we mustn't modify the */
            /* original argv[0] payload. */
            void *tmp = NULL;
            int arc = irxstor(RXSMGET, (int)byte_len, &tmp, p->envblock);
            if (arc != 0)
            {
                return IRXPARS_NOMEM;
            }
            work = (unsigned char *)tmp;
            memcpy(work, bytes, byte_len);
            bytes = work;
        }
        twos_complement_bytes(work, byte_len, 0);
    }

    int rc = compute_c2d(p, bytes, byte_len, sign, result);

    if (work != NULL)
    {
        void *p1 = work;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
    }
    return rc;
}

static int bif_x2d(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    PLstr in = argv[0];
    long n_arg = 0;
    int n_given = 0;

    if (argc >= 2 && argv[1] != NULL && argv[1]->len > 0)
    {
        int rc = irx_bif_whole_nonneg(p, argv, 1, "X2D", &n_arg);
        if (rc != 0)
        {
            return rc;
        }
        n_given = 1;
    }

    size_t hex_count = 0;
    size_t i;
    for (i = 0; i < in->len; i++)
    {
        unsigned char c = in->pstr[i];
        if (is_blank(c))
        {
            continue;
        }
        if (hex_val(c) < 0)
        {
            raise_bad_hex(p, "X2D");
            return IRXPARS_SYNTAX;
        }
        hex_count++;
    }

    size_t eff_hex = n_given ? (size_t)n_arg : hex_count;
    if (eff_hex == 0)
    {
        return translate_lstr_rc(long_to_lstr(p->alloc, result, 0L));
    }

    size_t byte_len = (eff_hex + 1) / 2;
    int pad_odd = (int)(eff_hex & 1);

    void *tmp = NULL;
    int arc = irxstor(RXSMGET, (int)byte_len, &tmp, p->envblock);
    if (arc != 0)
    {
        return IRXPARS_NOMEM;
    }
    unsigned char *bytes = (unsigned char *)tmp;
    memset(bytes, 0, byte_len);

    /* Pack up to min(eff_hex, hex_count) nibbles into the rightmost     */
    /* slots. When the input has fewer than eff_hex digits, the leading  */
    /* slots stay zero — that's the implicit left-pad.                   */
    size_t need = (eff_hex < hex_count) ? eff_hex : hex_count;
    size_t total_slots = byte_len * 2;
    size_t slot = total_slots;
    size_t taken = 0;
    size_t j;
    for (j = in->len; j > 0 && taken < need;)
    {
        j--;
        unsigned char c = in->pstr[j];
        if (is_blank(c))
        {
            continue;
        }
        int v = hex_val(c);
        slot--;
        size_t byte_idx = slot / 2;
        int is_high = ((slot & 1) == 0);
        if (is_high)
        {
            bytes[byte_idx] |= (unsigned char)((v & 0x0F) << 4);
        }
        else
        {
            bytes[byte_idx] |= (unsigned char)(v & 0x0F);
        }
        taken++;
    }

    /* Two's-complement sign = bit 3 of the leftmost real hex digit.     */
    /* For odd eff_hex, slot 0 is a pad '0' nibble and the first real    */
    /* digit lives at slot 1. Mask byte[0]'s high nibble for odd eff_hex */
    /* so the magnitude arithmetic stays within the effective bit width. */
    if (pad_odd && byte_len > 0)
    {
        bytes[0] &= 0x0F;
    }

    int sign = 0;
    if (n_given && byte_len > 0)
    {
        unsigned char msn = pad_odd
                                ? (unsigned char)(bytes[0] & 0x0F)
                                : (unsigned char)((bytes[0] >> 4) & 0x0F);
        if ((msn & 0x08U) != 0)
        {
            sign = 1;
            twos_complement_bytes(bytes, byte_len, pad_odd);
        }
    }

    int rc = compute_c2d(p, bytes, byte_len, sign, result);

    {
        void *p1 = bytes;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/*  D2C / D2X core                                                    */
/* ------------------------------------------------------------------ */

/* Parse argv[0] as a REXX whole number via irx_arith_to_digits.       */
/* Expands the digit array by any positive exponent (trailing zeros).  */
/* Sets condition and returns IRXPARS_SYNTAX on non-numeric or         */
/* fractional inputs.                                                  */
static int d2_parse_whole(struct irx_parser *p, PLstr in,
                          const char *bif_name,
                          char *digits, int digits_cap,
                          int *digits_len_out, int *sign_out)
{
    int digits_len = 0;
    int sign = 0;
    long exponent = 0;
    int rc = irx_arith_to_digits(p->envblock, in, digits, digits_cap,
                                 &digits_len, &sign, &exponent);
    if (rc == IRXPARS_SYNTAX)
    {
        raise_nonnumeric(p, bif_name);
        return rc;
    }
    if (rc != IRXPARS_OK)
    {
        return rc;
    }
    if (exponent < 0)
    {
        raise_bif_cond(p, SYNTAX_BAD_CALL, ERR40_WHOLE_NUMBER, bif_name,
                       ": argument 1 must be a whole number");
        return IRXPARS_SYNTAX;
    }
    if (exponent > 0)
    {
        if ((long)digits_len + exponent > (long)digits_cap)
        {
            raise_bif_cond(p, SYNTAX_BAD_CALL, ERR40_ARG_LENGTH, bif_name,
                           ": argument 1 magnitude too large");
            return IRXPARS_SYNTAX;
        }
        int e;
        for (e = 0; e < (int)exponent; e++)
        {
            digits[digits_len + e] = 0;
        }
        digits_len += (int)exponent;
    }
    *digits_len_out = digits_len;
    *sign_out = sign;
    return IRXPARS_OK;
}

/* Core for D2C and the byte-backing of D2X. Writes `out_bytes_len`    */
/* bytes MSB-first into `out_bytes`. Handles sign, length padding,     */
/* truncation, and two's-complement negation. `*all_zero_out` reflects */
/* the PRE-NEGATION magnitude (1 iff |n| == 0 after length truncation) */
/* — callers only use it in the sign==0 / length-omitted path where    */
/* the distinction doesn't matter. If a future refactor consults       */
/* `all_zero` on a post-negation buffer, re-check this semantics.      */
/*                                                                     */
/* Performance note: two scratch buffers are allocated per call        */
/* (`digits`, DIGITS_CAP bytes ≈ 2000; `lsb`, lsb_cap bytes ≤ ~505).    */
/* Consolidating into one allocation with offset pointers would save   */
/* ~2.5 KB on the MVS heap per invocation — deferred until a concrete  */
/* hotpath motivates it.                                               */
static int d2_core_write_bytes(struct irx_parser *p, PLstr in,
                               const char *bif_name,
                               int length_given,
                               int out_bytes_len,
                               unsigned char *out_bytes,
                               int *sign_out, int *all_zero_out,
                               int twos_mask_high_nibble)
{
    /* digits_cap sized to hold NUMERIC DIGITS max plus a generous       */
    /* exponent expansion. 2 * NUMERIC_DIGITS_MAX covers every number    */
    /* that fits the engine — if that overflows, d2_parse_whole raises  */
    /* a condition.                                                     */
    enum
    {
        DIGITS_CAP = NUMERIC_DIGITS_MAX * 2
    };

    char *digits = NULL;
    int rc = IRXPARS_OK;
    int digits_len = 0;
    int sign = 0;

    {
        void *tmp = NULL;
        int arc = irxstor(RXSMGET, DIGITS_CAP, &tmp, p->envblock);
        if (arc != 0)
        {
            return IRXPARS_NOMEM;
        }
        digits = (char *)tmp;
    }

    rc = d2_parse_whole(p, in, bif_name, digits, DIGITS_CAP,
                        &digits_len, &sign);
    if (rc != IRXPARS_OK)
    {
        goto cleanup;
    }

    *sign_out = sign;

    /* Negative without length → SYNTAX 40.11. Must come before byte    */
    /* extraction so the error wins over an empty output.               */
    if (sign == 1 && !length_given)
    {
        raise_bif_cond(p, SYNTAX_BAD_CALL, ERR40_NONNEG_WHOLE, bif_name,
                       ": argument 1 must be non-negative when length omitted");
        rc = IRXPARS_SYNTAX;
        goto cleanup;
    }

    /* Extract low bytes into a temporary LSB-first buffer. We need at  */
    /* most out_bytes_len + 1 bytes to detect overflow for the length-  */
    /* omitted case (the extra byte lets us see a non-zero after our    */
    /* window ends). For D2C/D2X with length given, out_bytes_len is    */
    /* enough and overflow truncates silently per spec.                 */
    int lsb_cap = out_bytes_len + 1;
    unsigned char *lsb = NULL;
    {
        void *tmp = NULL;
        int arc = irxstor(RXSMGET, lsb_cap, &tmp, p->envblock);
        if (arc != 0)
        {
            rc = IRXPARS_NOMEM;
            goto cleanup;
        }
        lsb = (unsigned char *)tmp;
    }
    memset(lsb, 0, (size_t)lsb_cap);

    (void)bcd_to_bytes_lsb(digits, digits_len, lsb, lsb_cap);

    /* Reverse LSB-first into MSB-first in out_bytes, right-aligned. */
    memset(out_bytes, 0, (size_t)out_bytes_len);
    int k;
    for (k = 0; k < out_bytes_len; k++)
    {
        out_bytes[out_bytes_len - 1 - k] = lsb[k];
    }

    *all_zero_out = 1;
    {
        int m;
        for (m = 0; m < out_bytes_len; m++)
        {
            if (out_bytes[m] != 0)
            {
                *all_zero_out = 0;
                break;
            }
        }
    }

    /* For D2X with odd hex-digit length, the caller sets                */
    /* twos_mask_high_nibble = 1 to mask out the stray high nibble of    */
    /* byte[0] before negation (and after, to restore the invariant).   */
    if (twos_mask_high_nibble && out_bytes_len > 0)
    {
        out_bytes[0] &= 0x0F;
    }

    if (sign == 1 && length_given && out_bytes_len > 0)
    {
        /* Flip all bits, add 1 with carry. */
        int m;
        for (m = 0; m < out_bytes_len; m++)
        {
            out_bytes[m] = (unsigned char)(~out_bytes[m]);
        }
        int carry = 1;
        for (m = out_bytes_len; m > 0 && carry != 0;)
        {
            m--;
            int s = (int)out_bytes[m] + carry;
            out_bytes[m] = (unsigned char)(s & 0xFF);
            carry = (s >> 8) & 1;
        }
        if (twos_mask_high_nibble)
        {
            out_bytes[0] &= 0x0F;
        }
    }

    {
        void *p1 = lsb;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
    }
    rc = IRXPARS_OK;

cleanup:
{
    void *p1 = digits;
    irxstor(RXSMFRE, 0, &p1, p->envblock);
}
    return rc;
}

static int bif_d2c(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    long length = 0;
    int length_given = 0;

    if (argc >= 2 && argv[1] != NULL && argv[1]->len > 0)
    {
        int rc = irx_bif_whole_nonneg(p, argv, 1, "D2C", &length);
        if (rc != 0)
        {
            return rc;
        }
        length_given = 1;
    }

    /* length == 0 → empty result regardless of value. */
    if (length_given && length == 0)
    {
        int lrc = Lfx(p->alloc, result, 0);
        if (lrc == LSTR_OK)
        {
            result->len = 0;
            result->type = LSTRING_TY;
        }
        return translate_lstr_rc(lrc);
    }

    int out_len;
    if (length_given)
    {
        out_len = (int)length;
    }
    else
    {
        /* Upper bound for the unsigned case. Each decimal digit needs   */
        /* ≤ log2(10)/8 ≈ 0.415 bytes, so digits/2 + 2 is safe. We       */
        /* actually ask d2_core_write_bytes for this many slots; if the  */
        /* magnitude is smaller we trim the leading zero bytes afterward.*/
        out_len = NUMERIC_DIGITS_MAX / 2 + 4;
    }

    void *tmp = NULL;
    int arc = irxstor(RXSMGET, out_len, &tmp, p->envblock);
    if (arc != 0)
    {
        return IRXPARS_NOMEM;
    }
    unsigned char *buf = (unsigned char *)tmp;

    int sign = 0;
    int all_zero = 0;
    int rc = d2_core_write_bytes(p, argv[0], "D2C", length_given, out_len,
                                 buf, &sign, &all_zero, 0);
    if (rc != IRXPARS_OK)
    {
        void *p1 = buf;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
        return rc;
    }

    int emit_len = out_len;
    int emit_start = 0;
    if (!length_given)
    {
        /* Strip leading zero bytes; zero value → empty. */
        if (all_zero)
        {
            emit_len = 0;
        }
        else
        {
            while (emit_start < out_len && buf[emit_start] == 0)
            {
                emit_start++;
            }
            emit_len = out_len - emit_start;
        }
    }

    int lrc = Lfx(p->alloc, result, (size_t)emit_len);
    if (lrc != LSTR_OK)
    {
        void *p1 = buf;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
        return translate_lstr_rc(lrc);
    }
    if (emit_len > 0)
    {
        memcpy(result->pstr, buf + emit_start, (size_t)emit_len);
    }
    result->len = (size_t)emit_len;
    result->type = LSTRING_TY;

    {
        void *p1 = buf;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
    }
    return IRXPARS_OK;
}

static int bif_d2x(struct irx_parser *p, int argc, PLstr *argv,
                   PLstr result)
{
    long length = 0;
    int length_given = 0;

    if (argc >= 2 && argv[1] != NULL && argv[1]->len > 0)
    {
        int rc = irx_bif_whole_nonneg(p, argv, 1, "D2X", &length);
        if (rc != 0)
        {
            return rc;
        }
        length_given = 1;
    }

    if (length_given && length == 0)
    {
        int lrc = Lfx(p->alloc, result, 0);
        if (lrc == LSTR_OK)
        {
            result->len = 0;
            result->type = LSTRING_TY;
        }
        return translate_lstr_rc(lrc);
    }

    int byte_len;
    int pad_odd = 0;
    if (length_given)
    {
        byte_len = (int)((length + 1) / 2);
        pad_odd = (int)(length & 1);
    }
    else
    {
        byte_len = NUMERIC_DIGITS_MAX / 2 + 4;
    }

    void *tmp = NULL;
    int arc = irxstor(RXSMGET, byte_len, &tmp, p->envblock);
    if (arc != 0)
    {
        return IRXPARS_NOMEM;
    }
    unsigned char *buf = (unsigned char *)tmp;

    int sign = 0;
    int all_zero = 0;
    int rc = d2_core_write_bytes(p, argv[0], "D2X", length_given, byte_len,
                                 buf, &sign, &all_zero, pad_odd);
    if (rc != IRXPARS_OK)
    {
        void *p1 = buf;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
        return rc;
    }

    /* Emit hex. For length_given: exactly `length` hex digits,          */
    /* starting at nibble position (pad_odd): if pad_odd, the high       */
    /* nibble of byte[0] is the pad and is skipped. For length omitted:  */
    /* emit all nibbles of the un-trimmed buffer, then strip leading '0' */
    /* characters (leaving a single '0' if the value is zero).           */
    int out_hex;
    if (length_given)
    {
        out_hex = (int)length;
    }
    else
    {
        out_hex = byte_len * 2;
    }

    /* Worst case scratch = byte_len * 2. */
    void *tmp2 = NULL;
    int arc2 = irxstor(RXSMGET, byte_len * 2, &tmp2, p->envblock);
    if (arc2 != 0)
    {
        void *p1 = buf;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
        return IRXPARS_NOMEM;
    }
    unsigned char *hex_buf = (unsigned char *)tmp2;
    int k;
    for (k = 0; k < byte_len; k++)
    {
        hex_buf[2 * k] = hex_char((buf[k] >> 4) & 0x0F);
        hex_buf[2 * k + 1] = hex_char(buf[k] & 0x0F);
    }

    int emit_start;
    int emit_len;
    if (length_given)
    {
        emit_start = pad_odd ? 1 : 0;
        emit_len = out_hex;
    }
    else
    {
        if (all_zero)
        {
            /* D2X(0) → "0". */
            emit_start = byte_len * 2 - 1;
            emit_len = 1;
        }
        else
        {
            emit_start = 0;
            while (emit_start < byte_len * 2 - 1 && hex_buf[emit_start] == (unsigned char)'0')
            {
                emit_start++;
            }
            emit_len = byte_len * 2 - emit_start;
        }
    }

    int lrc = Lfx(p->alloc, result, (size_t)emit_len);
    if (lrc == LSTR_OK)
    {
        if (emit_len > 0)
        {
            memcpy(result->pstr, hex_buf + emit_start, (size_t)emit_len);
        }
        result->len = (size_t)emit_len;
        result->type = LSTRING_TY;
    }

    {
        void *p1 = hex_buf;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
    }
    {
        void *p1 = buf;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
    }
    return translate_lstr_rc(lrc);
}

/* ================================================================== */
/*  Phase I — Reflection BIFs (WP-21b Phase E)                        */
/*                                                                    */
/*  DATATYPE / SYMBOL / DIGITS / FUZZ / FORM expose interpreter       */
/*  state and classification rules to REXX code. They are thin        */
/*  dispatch wrappers: _Lisnum() and irx_datatype() carry the number  */
/*  and character-class logic; is_rexx_symbol() is the tokenizer's    */
/*  own predicate, reused verbatim.                                   */
/* ================================================================== */

/* Copy a short literal result string into `result`. Thin wrapper so
 * the handlers below can return 'VAR'/'LIT'/'BAD' etc. in a single
 * expression and keep every error path on translate_lstr_rc(). */
static int lit_to_lstr(struct lstr_alloc *a, PLstr dst, const char *s)
{
    size_t n = strlen(s);
    int rc = Lfx(a, dst, n);
    if (rc != LSTR_OK)
    {
        return rc;
    }
    if (n > 0)
    {
        memcpy(dst->pstr, s, n);
    }
    dst->len = n;
    dst->type = LSTRING_TY;
    return LSTR_OK;
}

static int bif_datatype(struct irx_parser *p, int argc, PLstr *argv,
                        PLstr result)
{
    PLstr str = argv[0];

    /* No-option form: 'NUM' if str is a valid REXX number, else 'CHAR'.
     * Empty string is not a number — it falls through to 'CHAR'. */
    if (argc < 2 || argv[1] == NULL || argv[1]->len == 0)
    {
        int is_num = (str->len > 0) && (_Lisnum(str) != LNUM_NOT_NUM);
        return translate_lstr_rc(
            lit_to_lstr(p->alloc, result, is_num ? "NUM" : "CHAR"));
    }

    char opt = 0;
    int rc = irx_bif_opt_option(p, argc, argv, 1, "DATATYPE",
                                "NWSABLMUX", 'N', &opt);
    if (rc != 0)
    {
        return rc;
    }

    int match = 0;
    if (str->len > 0)
    {
        switch (opt)
        {
            case 'N':
                match = (_Lisnum(str) != LNUM_NOT_NUM);
                break;
            case 'W':
                /* Whole number: classify must come back INTEGER (no
                 * fractional part, no exponent). _Lisnum() caches the
                 * classification, keeping the check in sync with the
                 * parser and arithmetic engine. */
                match = (_Lisnum(str) == LNUM_INTEGER);
                break;
            case 'S':
                match = is_rexx_symbol(str->pstr, str->len);
                break;
            case 'A':
            case 'B':
            case 'L':
            case 'M':
            case 'U':
            case 'X':
                match = irx_datatype(str, opt);
                break;
            default:
                /* irx_bif_opt_option already rejected anything outside
                 * the allowed set, so this branch is unreachable. */
                match = 0;
                break;
        }
    }

    return translate_lstr_rc(
        lit_to_lstr(p->alloc, result, match ? "1" : "0"));
}

static int bif_symbol(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    (void)argc;
    PLstr name = argv[0];

    if (name->len == 0 || !is_rexx_symbol(name->pstr, name->len))
    {
        return translate_lstr_rc(lit_to_lstr(p->alloc, result, "BAD"));
    }

    /* vpool stores variable names as-is (parser produces the canonical
     * upper-case form). SYMBOL() is called directly by user code with
     * whatever casing the caller typed, so uppercase here before the
     * lookup to match the parser's convention. */
    unsigned char tmp_buf[64];
    unsigned char *up = NULL;
    int heap_up = 0;

    if (name->len <= sizeof(tmp_buf))
    {
        up = tmp_buf;
    }
    else
    {
        void *mem = NULL;
        if (irxstor(RXSMGET, (int)name->len, &mem, p->envblock) != 0)
        {
            return IRXPARS_NOMEM;
        }
        up = (unsigned char *)mem;
        heap_up = 1;
    }

    size_t i;
    for (i = 0; i < name->len; i++)
    {
        unsigned char c = name->pstr[i];
        up[i] = (unsigned char)(islower(c) ? toupper(c) : c);
    }

    Lstr key;
    key.pstr = up;
    key.len = name->len;
    key.maxlen = name->len;
    key.type = LSTRING_TY;

    /* vpool_exists returns 1 if the name is a set variable, 0
     * otherwise (missing or tombstoned). It does NOT use the
     * VPOOL_* return-code namespace. */
    const char *text =
        (vpool_exists(p->vpool, &key) != 0) ? "VAR" : "LIT";

    int rc = lit_to_lstr(p->alloc, result, text);

    if (heap_up)
    {
        void *p1 = up;
        irxstor(RXSMFRE, 0, &p1, p->envblock);
    }

    return translate_lstr_rc(rc);
}

/* DIGITS / FUZZ / FORM read per-environment NUMERIC state from the
 * work block. When no work block is attached (bootstrap / some tests)
 * the documented defaults (9 / 0 / SCIENTIFIC) apply. */
static struct irx_wkblk_int *wkbi_from_parser(struct irx_parser *p)
{
    if (p->envblock == NULL || p->envblock->envblock_userfield == NULL)
    {
        return NULL;
    }
    return (struct irx_wkblk_int *)p->envblock->envblock_userfield;
}

static int bif_digits(struct irx_parser *p, int argc, PLstr *argv,
                      PLstr result)
{
    (void)argc;
    (void)argv;
    struct irx_wkblk_int *wk = wkbi_from_parser(p);
    long v = (wk != NULL) ? (long)wk->wkbi_digits
                          : (long)NUMERIC_DIGITS_DEFAULT;
    return translate_lstr_rc(long_to_lstr(p->alloc, result, v));
}

static int bif_fuzz(struct irx_parser *p, int argc, PLstr *argv,
                    PLstr result)
{
    (void)argc;
    (void)argv;
    struct irx_wkblk_int *wk = wkbi_from_parser(p);
    long v = (wk != NULL) ? (long)wk->wkbi_fuzz
                          : (long)NUMERIC_FUZZ_DEFAULT;
    return translate_lstr_rc(long_to_lstr(p->alloc, result, v));
}

static int bif_form(struct irx_parser *p, int argc, PLstr *argv,
                    PLstr result)
{
    (void)argc;
    (void)argv;
    struct irx_wkblk_int *wk = wkbi_from_parser(p);
    int form = (wk != NULL) ? wk->wkbi_form : NUMFORM_SCIENTIFIC;
    const char *text =
        (form == NUMFORM_ENGINEERING) ? "ENGINEERING" : "SCIENTIFIC";
    return translate_lstr_rc(lit_to_lstr(p->alloc, result, text));
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
    /* Phase H — Conversion BIFs (WP-21b Phase D) */
    {"C2X", 1, 1, bif_c2x},
    {"X2C", 1, 1, bif_x2c},
    {"B2X", 1, 1, bif_b2x},
    {"X2B", 1, 1, bif_x2b},
    {"C2D", 1, 2, bif_c2d},
    {"X2D", 1, 2, bif_x2d},
    {"D2C", 1, 2, bif_d2c},
    {"D2X", 1, 2, bif_d2x},
    /* Phase I — Reflection BIFs (WP-21b Phase E) */
    {"DATATYPE", 1, 2, bif_datatype},
    {"SYMBOL", 1, 1, bif_symbol},
    {"DIGITS", 0, 0, bif_digits},
    {"FUZZ", 0, 0, bif_fuzz},
    {"FORM", 0, 0, bif_form},
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
