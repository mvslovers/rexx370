/* ------------------------------------------------------------------ */
/*  irx#arith.c - REXX/370 Arithmetic Engine (WP-20)                 */
/*                                                                    */
/*  Arbitrary-precision BCD decimal arithmetic per SC28-1883-0 §9.   */
/*  All allocation goes through irxstor. No globals, fully reentrant.*/
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxarith.h"
#include "irxcond.h"
#include "irxfunc.h"
#include "irxlstr.h"
#include "irxpars.h"
#include "irxwkblk.h"
#include "lstralloc.h"
#include "lstring.h"

/* ------------------------------------------------------------------ */
/*  Internal BCD number                                               */
/*                                                                    */
/*  value = sign × coefficient × 10^exp                              */
/*  coefficient stored as digits[0..len-1], digits[0] = MSD          */
/*  Zero canonical form: digits=[0], len=1, sign=0, exp=0            */
/* ------------------------------------------------------------------ */

struct irx_number
{
    int sign;              /* 0 = non-negative, 1 = negative */
    int exp;               /* power: value = coeff × 10^exp  */
    int len;               /* significant digit count         */
    int cap;               /* capacity of digits[]            */
    unsigned char *digits; /* heap-allocated, 0-9 each      */
};

/* ------------------------------------------------------------------ */
/*  Storage helpers                                                    */
/* ------------------------------------------------------------------ */

static int num_alloc(struct envblock *env, int cap, struct irx_number *n)
{
    void *p = NULL;
    if (cap < 1)
    {
        cap = 1;
    }
    if (irxstor(RXSMGET, cap, &p, env) != 0)
    {
        return -1;
    }
    n->digits = (unsigned char *)p;
    n->cap = cap;
    n->len = 0;
    /* Do NOT reset sign/exp: callers set them explicitly after num_alloc,
     * and num_from_str sets sign before calling num_alloc. */
    return 0;
}

static void num_free(struct envblock *env, struct irx_number *n)
{
    if (n->digits != NULL)
    {
        void *p = n->digits;
        irxstor(RXSMFRE, n->cap, &p, env);
        n->digits = NULL;
    }
    n->cap = n->len = 0;
}

/* Allocate a work int-array of `count` ints via irxstor. */
static int *work_alloc(struct envblock *env, int count)
{
    void *p = NULL;
    int sz = count * (int)sizeof(int);
    if (sz <= 0 || irxstor(RXSMGET, sz, &p, env) != 0)
    {
        return NULL;
    }
    memset(p, 0, (size_t)sz);
    return (int *)p;
}

static void work_free(struct envblock *env, int *w, int count)
{
    void *p = w;
    irxstor(RXSMFRE, count * (int)sizeof(int), &p, env);
}

/* ------------------------------------------------------------------ */
/*  NUMERIC settings                                                  */
/* ------------------------------------------------------------------ */

static void get_numeric(struct envblock *env,
                        int *digits, int *fuzz, int *form)
{
    *digits = NUMERIC_DIGITS_DEFAULT;
    *fuzz = NUMERIC_FUZZ_DEFAULT;
    *form = NUMFORM_SCIENTIFIC;
    if (env != NULL && env->envblock_userfield != NULL)
    {
        struct irx_wkblk_int *wk =
            (struct irx_wkblk_int *)env->envblock_userfield;
        *digits = wk->wkbi_digits;
        *fuzz = wk->wkbi_fuzz;
        *form = wk->wkbi_form;
    }
}

/* ------------------------------------------------------------------ */
/*  Normalization helpers                                             */
/* ------------------------------------------------------------------ */

static void num_strip_trailing(struct irx_number *n)
{
    while (n->len > 1 && n->digits[n->len - 1] == 0)
    {
        n->exp++;
        n->len--;
    }
    if (n->len == 1 && n->digits[0] == 0)
    {
        n->sign = 0;
        n->exp = 0;
    }
}

static void num_strip_leading(struct irx_number *n)
{
    int lz = 0;
    while (lz < n->len - 1 && n->digits[lz] == 0)
    {
        lz++;
    }
    if (lz > 0)
    {
        memmove(n->digits, n->digits + lz, (size_t)(n->len - lz));
        n->len -= lz;
    }
}

/* Round n to at most `digits` significant figures (half-up). */
static void num_round(struct irx_number *n, int digits)
{
    int excess, carry, i;

    if (n->len <= digits)
    {
        goto done;
    }

    excess = n->len - digits;
    carry = (n->digits[digits] >= 5) ? 1 : 0;

    n->len = digits;
    n->exp += excess;

    if (carry)
    {
        for (i = digits - 1; i >= 0; i--)
        {
            n->digits[i]++;
            if (n->digits[i] < 10)
            {
                carry = 0;
                break;
            }
            n->digits[i] = 0;
        }
        if (carry)
        {
            /* Full overflow: 999...9 → 1000...0, one extra order of magnitude */
            n->digits[0] = 1;
            n->exp += n->len;
            n->len = 1;
        }
    }

done:
    num_strip_trailing(n);
}

/* ------------------------------------------------------------------ */
/*  Parse a REXX number string into struct irx_number.                */
/*  Returns 0 on success, -1 on failure.                             */
/* ------------------------------------------------------------------ */

static int num_from_str(struct envblock *env,
                        const unsigned char *s, int slen,
                        struct irx_number *n)
{
    int i = 0;
    int decimal_pos = -1;
    int nraw = 0;
    int e_val = 0;
    int e_sign = 1;
    int has_digits = 0;
    int alloc_cap = slen + 8;

    n->digits = NULL;
    n->cap = n->len = n->sign = n->exp = 0;

    while (i < slen && (s[i] == ' ' || s[i] == '\t'))
    {
        i++;
    }
    if (i >= slen)
    {
        return -1;
    }

    if (s[i] == '-')
    {
        n->sign = 1;
        i++;
    }
    else if (s[i] == '+')
    {
        i++;
    }

    if (num_alloc(env, alloc_cap, n) != 0)
    {
        return -1;
    }

    while (i < slen)
    {
        unsigned char c = s[i];
        if (c >= '0' && c <= '9')
        {
            if (nraw >= n->cap)
            {
                num_free(env, n);
                return -1;
            }
            n->digits[nraw++] = (unsigned char)(c - '0');
            has_digits = 1;
            i++;
        }
        else if (c == '.')
        {
            if (decimal_pos >= 0)
            {
                num_free(env, n);
                return -1;
            }
            decimal_pos = nraw;
            i++;
        }
        else if (c == 'E' || c == 'e')
        {
            i++;
            if (i < slen && s[i] == '+')
            {
                i++;
            }
            else if (i < slen && s[i] == '-')
            {
                e_sign = -1;
                i++;
            }
            while (i < slen && s[i] >= '0' && s[i] <= '9')
            {
                e_val = e_val * 10 + (int)(s[i] - '0');
                i++;
            }
            break;
        }
        else
        {
            break;
        }
    }

    /* Consume trailing whitespace; any remaining chars are an error. */
    while (i < slen && (s[i] == ' ' || s[i] == '\t'))
    {
        i++;
    }
    if (i < slen || !has_digits)
    {
        num_free(env, n);
        return -1;
    }

    n->exp = (decimal_pos >= 0)
                 ? e_sign * e_val - (nraw - decimal_pos)
                 : e_sign * e_val;
    n->len = nraw;

    num_strip_leading(n);
    num_strip_trailing(n);

    if (n->len == 0)
    {
        n->digits[0] = 0;
        n->len = 1;
        n->sign = 0;
        n->exp = 0;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Format struct irx_number to a char buffer.                        */
/*  Returns the number of characters written (not including NUL).    */
/*  buf must have at least (dig + 32) bytes where dig is the digit   */
/*  length of the input number. Callers are responsible for providing */
/*  a sufficient buffer.                                              */
/* ------------------------------------------------------------------ */

static int num_format(const struct irx_number *n, int dig, int form,
                      char *buf, int maxbuf)
{
    int adj_exp = n->exp + n->len - 1;
    int pos = 0;
    int i;
    char ebuf[12];
    int elen, etmp, edisplay;
    int dbefore;

    /* Canonical zero */
    if (n->len == 1 && n->digits[0] == 0)
    {
        if (pos < maxbuf - 1)
        {
            buf[pos++] = '0';
        }
        buf[pos] = '\0';
        return pos;
    }

    if (n->sign && pos < maxbuf - 1)
    {
        buf[pos++] = '-';
    }

    /* SC28-1883-0 §9.4: fixed-point when -5 <= adj_exp <= digits */
    if (adj_exp >= IRX_FIXED_POINT_MIN_EXP && adj_exp <= dig)
    {
        if (adj_exp >= 0)
        {
            /* Integer part: digits[0..adj_exp], zero-pad if needed */
            for (i = 0; i <= adj_exp; i++)
            {
                char d = (i < n->len) ? (char)('0' + n->digits[i]) : '0';
                if (pos < maxbuf - 1)
                {
                    buf[pos++] = d;
                }
            }
            /* Fractional part */
            if (adj_exp + 1 < n->len)
            {
                if (pos < maxbuf - 1)
                {
                    buf[pos++] = '.';
                }
                for (i = adj_exp + 1; i < n->len; i++)
                {
                    if (pos < maxbuf - 1)
                    {
                        buf[pos++] = (char)('0' + n->digits[i]);
                    }
                }
            }
        }
        else
        {
            /* Entirely fractional: 0.000...digits */
            if (pos < maxbuf - 1)
            {
                buf[pos++] = '0';
            }
            if (pos < maxbuf - 1)
            {
                buf[pos++] = '.';
            }
            for (i = 0; i < -(adj_exp + 1); i++)
            {
                if (pos < maxbuf - 1)
                {
                    buf[pos++] = '0';
                }
            }
            for (i = 0; i < n->len; i++)
            {
                if (pos < maxbuf - 1)
                {
                    buf[pos++] = (char)('0' + n->digits[i]);
                }
            }
        }
    }
    else
    {
        /* Exponential notation */
        dbefore = 1;
        edisplay = adj_exp;

        if (form == NUMFORM_ENGINEERING)
        {
            int rem = ((adj_exp % IRX_ENG_EXP_MULTIPLE) + IRX_ENG_EXP_MULTIPLE) % IRX_ENG_EXP_MULTIPLE;
            dbefore = rem + 1;
            edisplay = adj_exp - rem;
        }

        for (i = 0; i < dbefore; i++)
        {
            char d = (i < n->len) ? (char)('0' + n->digits[i]) : '0';
            if (pos < maxbuf - 1)
            {
                buf[pos++] = d;
            }
        }
        if (dbefore < n->len)
        {
            if (pos < maxbuf - 1)
            {
                buf[pos++] = '.';
            }
            for (i = dbefore; i < n->len; i++)
            {
                if (pos < maxbuf - 1)
                {
                    buf[pos++] = (char)('0' + n->digits[i]);
                }
            }
        }

        if (pos < maxbuf - 1)
        {
            buf[pos++] = 'E';
        }
        if (edisplay >= 0)
        {
            if (pos < maxbuf - 1)
            {
                buf[pos++] = '+';
            }
        }
        else
        {
            if (pos < maxbuf - 1)
            {
                buf[pos++] = '-';
            }
            edisplay = -edisplay;
        }

        /* Encode exponent digits */
        elen = 0;
        etmp = edisplay;
        if (etmp == 0)
        {
            ebuf[elen++] = '0';
        }
        else
        {
            while (etmp > 0 && elen < 10)
            {
                ebuf[elen++] = (char)('0' + etmp % 10);
                etmp /= 10;
            }
            /* Reverse */
            for (i = 0; i < elen / 2; i++)
            {
                char t = ebuf[i];
                ebuf[i] = ebuf[elen - 1 - i];
                ebuf[elen - 1 - i] = t;
            }
        }
        for (i = 0; i < elen; i++)
        {
            if (pos < maxbuf - 1)
            {
                buf[pos++] = ebuf[i];
            }
        }
    }

    buf[pos] = '\0';
    return pos;
}

/* ------------------------------------------------------------------ */
/*  Write a formatted number to a PLstr.                              */
/* ------------------------------------------------------------------ */

static int num_to_lstr(struct envblock *env, const struct irx_number *n,
                       int dig, int form, PLstr result)
{
    struct lstr_alloc *alloc = irx_lstr_init(env);
    int maxbuf = dig + 32;
    void *pbuf = NULL;
    char *buf;
    int slen;
    int rc;

    /* Fall back to the default malloc-backed allocator when env is NULL
     * (cross-compile tests that invoke the parser without a full envblock). */
    if (alloc == NULL)
    {
        alloc = lstr_default_alloc();
    }

    if (irxstor(RXSMGET, maxbuf, &pbuf, env) != 0)
    {
        return IRXPARS_NOMEM;
    }
    buf = (char *)pbuf;

    slen = num_format(n, dig, form, buf, maxbuf);

    rc = Lfx(alloc, result, (size_t)slen);
    if (rc == LSTR_OK)
    {
        if (slen > 0)
        {
            memcpy(result->pstr, buf, (size_t)slen);
        }
        result->len = (size_t)slen;
        result->type = LSTRING_TY;
    }

    irxstor(RXSMFRE, maxbuf, &pbuf, env);
    return (rc == LSTR_OK) ? IRXPARS_OK : IRXPARS_NOMEM;
}

/* ------------------------------------------------------------------ */
/*  Compare magnitudes: -1 if |a|<|b|, 0 if equal, +1 if |a|>|b|   */
/* ------------------------------------------------------------------ */

static int mag_compare(const struct irx_number *a,
                       const struct irx_number *b)
{
    int a_adj = a->exp + a->len - 1;
    int b_adj = b->exp + b->len - 1;
    int i;

    /* Both zero */
    if (a->len == 1 && a->digits[0] == 0 && b->len == 1 && b->digits[0] == 0)
    {
        return 0;
    }
    if (a->len == 1 && a->digits[0] == 0)
    {
        return -1;
    }
    if (b->len == 1 && b->digits[0] == 0)
    {
        return 1;
    }

    if (a_adj != b_adj)
    {
        return a_adj > b_adj ? 1 : -1;
    }

    for (i = 0; i < a->len && i < b->len; i++)
    {
        if (a->digits[i] != b->digits[i])
        {
            return a->digits[i] > b->digits[i] ? 1 : -1;
        }
    }
    for (; i < a->len; i++)
    {
        if (a->digits[i] != 0)
        {
            return 1;
        }
    }
    for (; i < b->len; i++)
    {
        if (b->digits[i] != 0)
        {
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Add magnitudes: r = |a| + |b|                                    */
/* ------------------------------------------------------------------ */

static int add_magnitudes(struct envblock *env,
                          const struct irx_number *a,
                          const struct irx_number *b,
                          struct irx_number *r)
{
    int lo = a->exp < b->exp ? a->exp : b->exp;
    int a_hi = a->exp + a->len - 1;
    int b_hi = b->exp + b->len - 1;
    int hi = a_hi > b_hi ? a_hi : b_hi;
    int width = hi - lo + 2; /* +1 carry slot */
    int *work;
    int i, k, msd, rlen;

    work = work_alloc(env, width);
    if (work == NULL)
    {
        return -1;
    }

    /* work[k] → decimal position (hi+1−k) */
    for (i = 0; i < a->len; i++)
    {
        k = hi + 1 - (a->exp + (a->len - 1 - i));
        work[k] += (int)a->digits[i];
    }
    for (i = 0; i < b->len; i++)
    {
        k = hi + 1 - (b->exp + (b->len - 1 - i));
        work[k] += (int)b->digits[i];
    }
    for (k = width - 1; k > 0; k--)
    {
        if (work[k] >= 10)
        {
            work[k - 1] += work[k] / 10;
            work[k] %= 10;
        }
    }

    msd = 0;
    while (msd < width - 1 && work[msd] == 0)
    {
        msd++;
    }
    rlen = width - msd;

    r->digits = NULL;
    r->cap = r->len = 0;
    if (num_alloc(env, rlen, r) != 0)
    {
        work_free(env, work, width);
        return -1;
    }
    for (i = 0; i < rlen; i++)
    {
        r->digits[i] = (unsigned char)work[msd + i];
    }
    r->len = rlen;
    r->exp = lo;
    r->sign = 0;

    work_free(env, work, width);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Subtract magnitudes: r = |a| − |b|, caller ensures |a| >= |b|   */
/* ------------------------------------------------------------------ */

static int sub_magnitudes(struct envblock *env,
                          const struct irx_number *a,
                          const struct irx_number *b,
                          struct irx_number *r)
{
    int lo = a->exp < b->exp ? a->exp : b->exp;
    int a_hi = a->exp + a->len - 1;
    int b_hi = b->exp + b->len - 1;
    int hi = a_hi > b_hi ? a_hi : b_hi;
    int width = hi - lo + 1;
    int *work;
    int i, k, msd, rlen;

    work = work_alloc(env, width);
    if (work == NULL)
    {
        return -1;
    }

    /* work[k] → decimal position (hi−k) */
    for (i = 0; i < a->len; i++)
    {
        k = hi - (a->exp + (a->len - 1 - i));
        work[k] += (int)a->digits[i];
    }
    for (i = 0; i < b->len; i++)
    {
        k = hi - (b->exp + (b->len - 1 - i));
        work[k] -= (int)b->digits[i];
    }
    for (k = width - 1; k > 0; k--)
    {
        if (work[k] < 0)
        {
            work[k] += 10;
            work[k - 1]--;
        }
    }

    msd = 0;
    while (msd < width - 1 && work[msd] == 0)
    {
        msd++;
    }
    rlen = width - msd;

    r->digits = NULL;
    r->cap = r->len = 0;
    if (num_alloc(env, rlen, r) != 0)
    {
        work_free(env, work, width);
        return -1;
    }
    for (i = 0; i < rlen; i++)
    {
        r->digits[i] = (unsigned char)work[msd + i];
    }
    r->len = rlen;
    r->exp = lo;
    r->sign = 0;

    work_free(env, work, width);
    num_strip_trailing(r);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Signed addition / subtraction                                     */
/* ------------------------------------------------------------------ */

static int num_addsub(struct envblock *env,
                      const struct irx_number *a,
                      const struct irx_number *b,
                      int subtract, int digits,
                      struct irx_number *r)
{
    int b_sign = subtract ? (b->sign ^ 1) : b->sign;
    int mc;
    int ret;

    r->digits = NULL;
    r->cap = r->len = 0;

    if (a->sign == b_sign)
    {
        ret = add_magnitudes(env, a, b, r);
        if (ret != 0)
        {
            return ret;
        }
        r->sign = a->sign;
    }
    else
    {
        mc = mag_compare(a, b);
        if (mc == 0)
        {
            if (num_alloc(env, 1, r) != 0)
            {
                return -1;
            }
            r->digits[0] = 0;
            r->len = 1;
            r->exp = 0;
            r->sign = 0;
        }
        else if (mc > 0)
        {
            ret = sub_magnitudes(env, a, b, r);
            if (ret != 0)
            {
                return ret;
            }
            r->sign = a->sign;
        }
        else
        {
            ret = sub_magnitudes(env, b, a, r);
            if (ret != 0)
            {
                return ret;
            }
            r->sign = b_sign;
        }
    }

    num_round(r, digits);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Multiplication: r = a × b                                        */
/* ------------------------------------------------------------------ */

static int num_mul(struct envblock *env,
                   const struct irx_number *a,
                   const struct irx_number *b,
                   int digits, struct irx_number *r)
{
    int rlen = a->len + b->len;
    int *work;
    int i, j, msd, n;

    r->digits = NULL;
    r->cap = r->len = 0;

    work = work_alloc(env, rlen);
    if (work == NULL)
    {
        return -1;
    }

    for (i = 0; i < a->len; i++)
    {
        for (j = 0; j < b->len; j++)
        {
            work[i + j + 1] += (int)a->digits[i] * (int)b->digits[j];
        }
    }
    for (i = rlen - 1; i > 0; i--)
    {
        if (work[i] >= 10)
        {
            work[i - 1] += work[i] / 10;
            work[i] %= 10;
        }
    }

    msd = 0;
    while (msd < rlen - 1 && work[msd] == 0)
    {
        msd++;
    }
    n = rlen - msd;

    if (num_alloc(env, n, r) != 0)
    {
        work_free(env, work, rlen);
        return -1;
    }
    for (i = 0; i < n; i++)
    {
        r->digits[i] = (unsigned char)work[msd + i];
    }
    r->len = n;
    r->exp = a->exp + b->exp;
    r->sign = a->sign ^ b->sign;

    work_free(env, work, rlen);
    num_round(r, digits);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Division: r = a / b (to `digits` sig figs)                       */
/*  If int_only, truncate to integer (for % and //).                 */
/* ------------------------------------------------------------------ */

static int num_div_impl(struct envblock *env,
                        const struct irx_number *a,
                        const struct irx_number *b,
                        int digits, int int_only,
                        struct irx_number *r)
{
    /* Scale a's coefficient up by `scale` extra zeros so that the
     * integer quotient has enough significant digits.
     * quotient_exp = a->exp - b->exp - scale */
    int scale = digits + 2 + b->len;
    int da_len = a->len + scale;
    int db_len = b->len;
    int dq_len = da_len - db_len + 1;
    int *da, *db, *dq, *partial, *prod;
    int i, jj, msd, rlen;

    r->digits = NULL;
    r->cap = r->len = 0;

    if (dq_len < 1)
    {
        dq_len = 1;
    }

    da = work_alloc(env, da_len);
    db = work_alloc(env, db_len);
    dq = work_alloc(env, dq_len);
    partial = work_alloc(env, db_len + 1);
    prod = work_alloc(env, db_len + 1);
    if (!da || !db || !dq || !partial || !prod)
    {
        if (da)
        {
            work_free(env, da, da_len);
        }
        if (db)
        {
            work_free(env, db, db_len);
        }
        if (dq)
        {
            work_free(env, dq, dq_len);
        }
        if (partial)
        {
            work_free(env, partial, db_len + 1);
        }
        if (prod)
        {
            work_free(env, prod, db_len + 1);
        }
        return -1;
    }

    /* Fill da: a's coefficient digits then zeros (the scale suffix). */
    for (i = 0; i < a->len; i++)
    {
        da[i] = (int)a->digits[i];
    }
    /* da[a->len..da_len-1] already 0 from work_alloc/memset. */

    for (i = 0; i < db_len; i++)
    {
        db[i] = (int)b->digits[i];
    }

    /* Initialize running partial dividend.
     * partial[0] is the overflow slot; partial[1..db_len] = da[0..db_len-1]. */
    partial[0] = 0;
    for (jj = 0; jj < db_len; jj++)
    {
        partial[jj + 1] = da[jj];
    }

    /* Shift-and-subtract long division. */
    for (i = 0; i < dq_len; i++)
    {
        int qd, carry, cmp;

        /* Estimate quotient digit from top two partial digits vs top db digit. */
        qd = (db[0] > 0) ? (partial[0] * 10 + partial[1]) / db[0] : 9;
        if (qd > 9)
        {
            qd = 9;
        }

        /* Compute prod[0..db_len] = qd * db as a (db_len+1)-digit array. */
        carry = 0;
        for (jj = db_len - 1; jj >= 0; jj--)
        {
            int v = qd * db[jj] + carry;
            prod[jj + 1] = v % 10;
            carry = v / 10;
        }
        prod[0] = carry;

        /* Reduce qd while prod > partial. */
        while (1)
        {
            cmp = 0;
            for (jj = 0; jj <= db_len; jj++)
            {
                if (prod[jj] > partial[jj])
                {
                    cmp = 1;
                    break;
                }
                if (prod[jj] < partial[jj])
                {
                    cmp = -1;
                    break;
                }
            }
            if (cmp <= 0)
            {
                break;
            }
            /* prod > partial: decrement qd and subtract db from prod. */
            qd--;
            {
                int borrow = 0;
                for (jj = db_len; jj >= 0; jj--)
                {
                    int d = (jj > 0) ? db[jj - 1] : 0;
                    int v = prod[jj] - d - borrow;
                    if (v < 0)
                    {
                        v += 10;
                        borrow = 1;
                    }
                    else
                    {
                        borrow = 0;
                    }
                    prod[jj] = v;
                }
            }
        }

        /* Subtract: partial -= prod. */
        {
            int borrow = 0;
            for (jj = db_len; jj >= 0; jj--)
            {
                int v = partial[jj] - prod[jj] - borrow;
                if (v < 0)
                {
                    v += 10;
                    borrow = 1;
                }
                else
                {
                    borrow = 0;
                }
                partial[jj] = v;
            }
        }

        dq[i] = qd;

        /* Shift partial left and bring in the next dividend digit. */
        for (jj = 0; jj < db_len; jj++)
        {
            partial[jj] = partial[jj + 1];
        }
        partial[db_len] = (i + db_len < da_len) ? da[i + db_len] : 0;
    }

    /* Find MSD of quotient */
    msd = 0;
    while (msd < dq_len - 1 && dq[msd] == 0)
    {
        msd++;
    }
    rlen = dq_len - msd;

    if (num_alloc(env, rlen, r) != 0)
    {
        work_free(env, da, da_len);
        work_free(env, db, db_len);
        work_free(env, dq, dq_len);
        work_free(env, partial, db_len + 1);
        work_free(env, prod, db_len + 1);
        return -1;
    }
    for (i = 0; i < rlen; i++)
    {
        r->digits[i] = (unsigned char)dq[msd + i];
    }
    r->len = rlen;
    r->exp = a->exp - b->exp - scale;
    r->sign = a->sign ^ b->sign;

    work_free(env, da, da_len);
    work_free(env, db, db_len);
    work_free(env, dq, dq_len);
    work_free(env, partial, db_len + 1);
    work_free(env, prod, db_len + 1);

    num_strip_leading(r);

    if (int_only)
    {
        /* Truncate fractional part toward zero */
        if (r->exp < 0)
        {
            int strip = -r->exp;
            if (strip >= r->len)
            {
                r->digits[0] = 0;
                r->len = 1;
                r->exp = 0;
                r->sign = 0;
            }
            else
            {
                r->len += r->exp; /* r->exp is negative */
                r->exp = 0;
                num_strip_trailing(r);
            }
        }
        /* Strip to digits if needed */
        if (r->len > digits)
        {
            num_round(r, digits);
        }
    }
    else
    {
        num_round(r, digits);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Power: r = a ** n (n must be a non-negative integer)              */
/* ------------------------------------------------------------------ */

static int num_power(struct envblock *env,
                     const struct irx_number *a,
                     long exp_val,
                     int digits,
                     struct irx_number *r)
{
    struct irx_number base;
    struct irx_number tmp;
    int ret;

    r->digits = NULL;
    r->cap = r->len = 0;

    if (exp_val == 0)
    {
        if (num_alloc(env, 1, r) != 0)
        {
            return -1;
        }
        r->digits[0] = 1;
        r->len = 1;
        r->exp = 0;
        r->sign = 0;
        return 0;
    }

    /* r = 1 */
    if (num_alloc(env, 1, r) != 0)
    {
        return -1;
    }
    r->digits[0] = 1;
    r->len = 1;
    r->exp = 0;
    r->sign = 0;

    /* base = a (copy) */
    base.digits = NULL;
    base.cap = base.len = 0;
    if (num_alloc(env, a->len, &base) != 0)
    {
        num_free(env, r);
        return -1;
    }
    memcpy(base.digits, a->digits, (size_t)a->len);
    base.len = a->len;
    base.exp = a->exp;
    base.sign = a->sign;

    while (exp_val > 0)
    {
        tmp.digits = NULL;
        tmp.cap = tmp.len = 0;
        ret = num_mul(env, r, &base, digits, &tmp);
        if (ret != 0)
        {
            num_free(env, r);
            num_free(env, &base);
            return ret;
        }
        num_free(env, r);
        *r = tmp;
        exp_val--;
    }

    num_free(env, &base);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Parse a PLstr as a REXX number, return C long for small ints.    */
/* ------------------------------------------------------------------ */

static int lstr_to_num(struct envblock *env, PLstr s,
                       struct irx_number *n)
{
    if (s == NULL || s->len == 0)
    {
        return -1;
    }
    return num_from_str(env, s->pstr, (int)s->len, n);
}

/* ------------------------------------------------------------------ */
/*  Public: irx_arith_op                                              */
/* ------------------------------------------------------------------ */

int irx_arith_op(struct envblock *env,
                 PLstr a, PLstr b,
                 enum irx_arith_opcode op,
                 PLstr result)
{
    struct irx_number na, nb, nr;
    int digits, fuzz, form;
    int ret;

    get_numeric(env, &digits, &fuzz, &form);

    na.digits = nb.digits = nr.digits = NULL;
    na.cap = na.len = nb.cap = nb.len = nr.cap = nr.len = 0;

    if (lstr_to_num(env, a, &na) != 0)
    {
        return IRXPARS_SYNTAX;
    }

    if (op != ARITH_NEG)
    {
        if (b == NULL || lstr_to_num(env, b, &nb) != 0)
        {
            num_free(env, &na);
            return IRXPARS_SYNTAX;
        }
    }

    switch (op)
    {
        case ARITH_ADD:
            ret = num_addsub(env, &na, &nb, 0, digits, &nr);
            break;
        case ARITH_SUB:
            ret = num_addsub(env, &na, &nb, 1, digits, &nr);
            break;
        case ARITH_MUL:
            ret = num_mul(env, &na, &nb, digits, &nr);
            break;
        case ARITH_DIV:
        {
            int is_zero = (nb.len == 1 && nb.digits[0] == 0);
            if (is_zero)
            {
                num_free(env, &na);
                num_free(env, &nb);
                return IRXPARS_DIVZERO;
            }
            ret = num_div_impl(env, &na, &nb, digits, 0, &nr);
            break;
        }
        case ARITH_INTDIV:
        {
            int is_zero = (nb.len == 1 && nb.digits[0] == 0);
            if (is_zero)
            {
                num_free(env, &na);
                num_free(env, &nb);
                return IRXPARS_DIVZERO;
            }
            ret = num_div_impl(env, &na, &nb, digits, 1, &nr);
            break;
        }
        case ARITH_MOD:
        {
            /* a // b = a - (a % b) * b  (integer division, then subtract) */
            struct irx_number nq, nmul;
            int is_zero = (nb.len == 1 && nb.digits[0] == 0);
            if (is_zero)
            {
                num_free(env, &na);
                num_free(env, &nb);
                return IRXPARS_DIVZERO;
            }
            nq.digits = nmul.digits = NULL;
            nq.cap = nq.len = nmul.cap = nmul.len = 0;

            ret = num_div_impl(env, &na, &nb, digits, 1, &nq);
            if (ret == 0)
            {
                ret = num_mul(env, &nq, &nb, digits, &nmul);
            }
            if (ret == 0)
            {
                ret = num_addsub(env, &na, &nmul, 1, digits, &nr);
            }

            num_free(env, &nq);
            num_free(env, &nmul);
            break;
        }
        case ARITH_POWER:
        {
            long exp_val = 0;
            int qi;
            int overflow = 0;

            if (nb.sign)
            {
                num_free(env, &na);
                num_free(env, &nb);
                return IRXPARS_SYNTAX;
            }
            if (nb.exp < 0)
            {
                int frac_start = nb.len + nb.exp;
                for (qi = frac_start; qi < nb.len; qi++)
                {
                    if (nb.digits[qi] != 0)
                    {
                        num_free(env, &na);
                        num_free(env, &nb);
                        return IRXPARS_SYNTAX;
                    }
                }
            }
            for (qi = 0; !overflow && qi < nb.len && (nb.exp < 0 ? qi < nb.len + nb.exp : 1); qi++)
            {
                if (exp_val > LONG_MAX / 10)
                {
                    overflow = 1;
                }
                else
                {
                    exp_val = exp_val * 10 + (long)nb.digits[qi];
                }
            }
            if (!overflow && nb.exp > 0)
            {
                int e;
                for (e = 0; !overflow && e < nb.exp; e++)
                {
                    if (exp_val > LONG_MAX / 10)
                    {
                        overflow = 1;
                    }
                    else
                    {
                        exp_val *= 10;
                    }
                }
            }

            /* SC28-1883-0 §9.5.4: |exponent| × DIGITS ≤ 9,999,999 */
            if (overflow || labs(exp_val) > 9999999 / digits)
            {
                irx_cond_raise(env, SYNTAX_OVERFLOW, 0,
                               "power exponent overflow");
                num_free(env, &na);
                num_free(env, &nb);
                return IRXPARS_SYNTAX;
            }

            ret = num_power(env, &na, exp_val, digits, &nr);
            break;
        }
        case ARITH_NEG:
        {
            /* Copy na into nr, flip sign */
            if (num_alloc(env, na.len, &nr) != 0)
            {
                num_free(env, &na);
                return IRXPARS_NOMEM;
            }
            memcpy(nr.digits, na.digits, (size_t)na.len);
            nr.len = na.len;
            nr.exp = na.exp;
            nr.sign = (na.len == 1 && na.digits[0] == 0) ? 0 : (na.sign ^ 1);
            ret = 0;
            break;
        }
        default:
            num_free(env, &na);
            num_free(env, &nb);
            return IRXPARS_SYNTAX;
    }

    num_free(env, &na);
    num_free(env, &nb);

    if (ret != 0)
    {
        num_free(env, &nr);
        return IRXPARS_NOMEM;
    }

    ret = num_to_lstr(env, &nr, digits, form, result);
    num_free(env, &nr);
    return ret;
}

/* ------------------------------------------------------------------ */
/*  Public: irx_arith_compare                                         */
/*                                                                    */
/*  Compare a and b with NUMERIC FUZZ applied.                       */
/*  FUZZ: if the difference is <= 10^(max_adj_exp - digits + fuzz),  */
/*  the numbers are considered equal.                                */
/* ------------------------------------------------------------------ */

int irx_arith_compare(struct envblock *env,
                      PLstr a, PLstr b,
                      int *cmp_out)
{
    struct irx_number na, nb, diff;
    int digits, fuzz, form;
    int ret, cmp;
    int adj_a, adj_b, adj_max;

    get_numeric(env, &digits, &fuzz, &form);

    na.digits = nb.digits = diff.digits = NULL;
    na.cap = na.len = nb.cap = nb.len = diff.cap = diff.len = 0;

    if (lstr_to_num(env, a, &na) != 0)
    {
        return IRXPARS_SYNTAX;
    }
    if (lstr_to_num(env, b, &nb) != 0)
    {
        num_free(env, &na);
        return IRXPARS_SYNTAX;
    }

    /* Quick sign comparison */
    if (na.sign != nb.sign)
    {
        int a_zero = (na.len == 1 && na.digits[0] == 0);
        int b_zero = (nb.len == 1 && nb.digits[0] == 0);
        if (a_zero && b_zero)
        {
            *cmp_out = 0;
        }
        else if (a_zero)
        {
            *cmp_out = nb.sign ? 1 : -1;
        }
        else if (b_zero)
        {
            *cmp_out = na.sign ? -1 : 1;
        }
        else
        {
            *cmp_out = na.sign ? -1 : 1;
        }
        num_free(env, &na);
        num_free(env, &nb);
        return IRXPARS_OK;
    }

    if (fuzz == 0)
    {
        /* No fuzz: straightforward magnitude compare with sign */
        cmp = mag_compare(&na, &nb);
        *cmp_out = na.sign ? -cmp : cmp;
        num_free(env, &na);
        num_free(env, &nb);
        return IRXPARS_OK;
    }

    /* With fuzz: compute |a - b| and compare against tolerance.
     * tolerance = 10^(max(adj_a, adj_b) - digits + fuzz)
     * If |a - b| < tolerance, they are equal. */
    adj_a = na.exp + na.len - 1;
    adj_b = nb.exp + nb.len - 1;
    adj_max = adj_a > adj_b ? adj_a : adj_b;

    ret = num_addsub(env, &na, &nb, 1, digits, &diff);
    if (ret != 0)
    {
        num_free(env, &na);
        num_free(env, &nb);
        return IRXPARS_NOMEM;
    }

    /* diff is |a - b| (sign already set by addsub) */
    diff.sign = 0; /* we just want the magnitude */

    /* Check if diff == 0 */
    if (diff.len == 1 && diff.digits[0] == 0)
    {
        *cmp_out = 0;
    }
    else
    {
        /* adj_exp of diff */
        int diff_adj = diff.exp + diff.len - 1;
        int tol_exp = adj_max - digits + fuzz;

        if (diff_adj < tol_exp)
        {
            *cmp_out = 0; /* within fuzz tolerance */
        }
        else
        {
            cmp = mag_compare(&na, &nb);
            *cmp_out = na.sign ? -cmp : cmp;
        }
    }

    num_free(env, &na);
    num_free(env, &nb);
    num_free(env, &diff);
    return IRXPARS_OK;
}
