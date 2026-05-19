/* ------------------------------------------------------------------ */
/*  irxlstr.c - REXX/370 <-> lstring370 adapter                       */
/*                                                                    */
/*  Implements the allocator bridge that routes lstring370 storage    */
/*  operations through irxstor, plus the REXX-specific extensions    */
/*  (_Lisnum, irx_datatype) that live on top of the generic library. */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxlstr.h"
#include "irxtokn.h"
#include "irxwkblk.h"
#include "lstring.h"

/* ------------------------------------------------------------------ */
/*  Allocator bridge                                                  */
/*                                                                    */
/*  The ctx field in the lstr_alloc struct holds the envblock pointer */
/*  so each alloc/dealloc routes through the correct environment's    */
/*  storage management replaceable routine. No statics - everything   */
/*  travels with the struct.                                          */
/*                                                                    */
/*  rexx_lstr_alloc_raw / rexx_lstr_dealloc_raw are the direct        */
/*  irxstor wrappers. rexx_lstr_alloc / rexx_lstr_dealloc are the     */
/*  pool-aware entry points installed in the lstr_alloc callbacks.   */
/* ------------------------------------------------------------------ */

static void *rexx_lstr_alloc_raw(size_t size, void *ctx)
{
    struct envblock *env = (struct envblock *)ctx;
    void *ptr = NULL;
    int rc;

    rc = irxstor(RXSMGET, (int)size, &ptr, env);
    if (rc != 0)
    {
        return NULL;
    }
    return ptr;
}

static void rexx_lstr_dealloc_raw(void *ptr, size_t size, void *ctx)
{
    struct envblock *env = (struct envblock *)ctx;
    void *p = ptr;

    irxstor(RXSMFRE, (int)size, &p, env);
}

/* ------------------------------------------------------------------ */
/*  Allocator pool                                                    */
/*                                                                    */
/*  Bucket capacities match lstring370's round_capacity() sequence:  */
/*    bucket 0 →  16 bytes                                            */
/*    bucket 1 →  32 bytes                                            */
/*    bucket 2 →  64 bytes                                            */
/*    bucket 3 → 128 bytes                                            */
/*                                                                    */
/*  Lfx always calls the allocator with a size that is already        */
/*  rounded to one of these values, so pool_bucket_for is exact.      */
/*  Sizes outside these four values bypass the pool entirely.         */
/*                                                                    */
/*  Subpool safety: all buffers in a per-env pool were GETMAIN'd with  */
/*  the same subpool (the env's parmblock_subpool). On MVS, freemain()*/
/*  reads subpool from the 8-byte prefix embedded by crent370's       */
/*  getmain(), not from parmblock — so teardown is safe even after    */
/*  PARMBLOCK has been released.                                       */
/* ------------------------------------------------------------------ */

/* Bucket capacities — must match lstring370's round_capacity() sequence. */
enum lstr_bucket_cap
{
    LSTR_CAP_16 = 16,
    LSTR_CAP_32 = 32,
    LSTR_CAP_64 = 64,
    LSTR_CAP_128 = 128
};

static const int lstr_pool_caps[LSTR_POOL_BUCKET_COUNT] = {
    LSTR_CAP_16, LSTR_CAP_32, LSTR_CAP_64, LSTR_CAP_128};

/* Return the bucket index for an exact-match size, or -1 if not pooled.
 * always_inline eliminates per-call overhead on the 66 M hot call sites.  */
static __inline__ __attribute__((always_inline)) int pool_bucket_for(int size)
{
    switch (size)
    {
        case LSTR_CAP_16:
            return 0;
        case LSTR_CAP_32:
            return 1;
        case LSTR_CAP_64:
            return 2;
        case LSTR_CAP_128:
            return LSTR_POOL_BUCKET_COUNT - 1;
        default:
            return -1;
    }
}

static void *rexx_lstr_alloc(size_t size, void *ctx)
{
    struct envblock *env = (struct envblock *)ctx;
    struct irx_wkblk_int *wkbi;
    int bkt;

    wkbi = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wkbi != NULL)
    {
        bkt = pool_bucket_for((int)size);
        if (bkt >= 0 && wkbi->wkbi_lstr_pool.buckets[bkt].count > 0)
        {
            return wkbi->wkbi_lstr_pool.buckets[bkt]
                .items[--wkbi->wkbi_lstr_pool.buckets[bkt].count];
        }
    }
    return rexx_lstr_alloc_raw(size, ctx);
}

static void rexx_lstr_dealloc(void *ptr, size_t size, void *ctx)
{
    struct envblock *env = (struct envblock *)ctx;
    struct irx_wkblk_int *wkbi;
    int bkt;

    wkbi = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wkbi != NULL)
    {
        bkt = pool_bucket_for((int)size);
        if (bkt >= 0 &&
            wkbi->wkbi_lstr_pool.buckets[bkt].count < LSTR_POOL_MAX_PER_BUCKET)
        {
            wkbi->wkbi_lstr_pool.buckets[bkt]
                .items[wkbi->wkbi_lstr_pool.buckets[bkt].count++] = ptr;
            return;
        }
    }
    rexx_lstr_dealloc_raw(ptr, size, ctx);
}

void irx_lstr_pool_teardown(struct envblock *envblock)
{
    struct irx_wkblk_int *wkbi;
    struct lstr_pool *pool;
    int bkt;
    int i;

    if (envblock == NULL)
    {
        return;
    }
    wkbi = (struct irx_wkblk_int *)envblock->envblock_userfield;
    if (wkbi == NULL)
    {
        return;
    }

    pool = &wkbi->wkbi_lstr_pool;
    for (bkt = 0; bkt < LSTR_POOL_BUCKET_COUNT; bkt++)
    {
        for (i = 0; i < pool->buckets[bkt].count; i++)
        {
            rexx_lstr_dealloc_raw(pool->buckets[bkt].items[i],
                                  (size_t)lstr_pool_caps[bkt], envblock);
        }
        pool->buckets[bkt].count = 0;
    }
}

struct lstr_alloc *irx_lstr_init(struct envblock *envblock)
{
    struct irx_wkblk_int *wkbi;
    struct lstr_alloc *alloc;
    void *mem = NULL;
    int rc;

    if (envblock == NULL)
    {
        return NULL;
    }

    wkbi = (struct irx_wkblk_int *)envblock->envblock_userfield;
    if (wkbi == NULL)
    {
        return NULL;
    }

    if (wkbi->wkbi_lstr_alloc != NULL)
    {
        return (struct lstr_alloc *)wkbi->wkbi_lstr_alloc;
    }

    rc = irxstor(RXSMGET, (int)sizeof(struct lstr_alloc), &mem, envblock);
    if (rc != 0)
    {
        return NULL;
    }

    alloc = (struct lstr_alloc *)mem;
    alloc->alloc = rexx_lstr_alloc;
    alloc->dealloc = rexx_lstr_dealloc;
    alloc->ctx = envblock;

    wkbi->wkbi_lstr_alloc = alloc;
    return alloc;
}

/* ------------------------------------------------------------------ */
/*  REXX number detection                                             */
/*                                                                    */
/*  Grammar per SC28-1883-0 Chapter 6, slightly relaxed:              */
/*                                                                    */
/*    number    ::= spaces? sign? mantissa exponent? spaces?          */
/*    sign      ::= '+' | '-'                                         */
/*    mantissa  ::= digits ('.' digits?)? | '.' digits                */
/*    exponent  ::= ('E' | 'e') sign? digits                          */
/*                                                                    */
/*  Spaces are ASCII/EBCDIC isspace() whitespace. The result is       */
/*  LNUM_INTEGER if no dot and no exponent were seen, LNUM_REAL       */
/*  otherwise, LNUM_NOT_NUM if the input doesn't match the grammar.   */
/* ------------------------------------------------------------------ */

static int classify_number(const unsigned char *p, size_t n)
{
    size_t i = 0;
    int saw_dot = 0;
    int saw_exp = 0;
    int saw_mantissa = 0;

    /* Leading whitespace */
    while (i < n && isspace(p[i]))
    {
        i++;
    }
    if (i >= n)
    {
        return LNUM_NOT_NUM;
    }

    /* Optional sign */
    if (p[i] == '+' || p[i] == '-')
    {
        i++;
    }

    /* Mantissa: digits, optional dot, optional digits; or . digits */
    while (i < n && isdigit(p[i]))
    {
        i++;
        saw_mantissa = 1;
    }
    if (i < n && p[i] == '.')
    {
        saw_dot = 1;
        i++;
        while (i < n && isdigit(p[i]))
        {
            i++;
            saw_mantissa = 1;
        }
    }
    if (!saw_mantissa)
    {
        return LNUM_NOT_NUM;
    }

    /* Optional exponent */
    if (i < n && (p[i] == 'E' || p[i] == 'e'))
    {
        int saw_exp_digit = 0;
        saw_exp = 1;
        i++;
        if (i < n && (p[i] == '+' || p[i] == '-'))
        {
            i++;
        }
        while (i < n && isdigit(p[i]))
        {
            i++;
            saw_exp_digit = 1;
        }
        if (!saw_exp_digit)
        {
            return LNUM_NOT_NUM;
        }
    }

    /* Trailing whitespace */
    while (i < n && isspace(p[i]))
    {
        i++;
    }
    if (i != n)
    {
        return LNUM_NOT_NUM;
    }

    return (saw_dot || saw_exp) ? LNUM_REAL : LNUM_INTEGER;
}

int _Lisnum(PLstr s)
{
    int class;

    if (s == NULL)
    {
        return LNUM_NOT_NUM;
    }
    if (s->type == LINTEGER_TY)
    {
        return LNUM_INTEGER;
    }
    if (s->type == LREAL_TY)
    {
        return LNUM_REAL;
    }
    if (s->pstr == NULL || s->len == 0)
    {
        return LNUM_NOT_NUM;
    }

    class = classify_number(s->pstr, s->len);
    if (class == LNUM_INTEGER)
    {
        s->type = LINTEGER_TY;
    }
    else if (class == LNUM_REAL)
    {
        s->type = LREAL_TY;
    }
    return class;
}

/* ------------------------------------------------------------------ */
/*  DATATYPE() classifiers                                            */
/*                                                                    */
/*  Each helper walks the buffer byte-by-byte; REXX DATATYPE is       */
/*  defined only for non-empty strings - the caller in irx_datatype   */
/*  handles the empty-string short-circuit.                            */
/* ------------------------------------------------------------------ */

static int all_match(const unsigned char *p, size_t n, int (*pred)(int))
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        if (!pred(p[i]))
        {
            return 0;
        }
    }
    return 1;
}

static int is_binary_char(int c)
{
    return c == '0' || c == '1';
}

static int is_alnum(int c)
{
    return isalnum(c);
}

static int is_lower(int c)
{
    return islower(c);
}

static int is_upper(int c)
{
    return isupper(c);
}

static int is_alpha(int c)
{
    return isalpha(c);
}

/* Hex with embedded blanks allowed between byte boundaries. */
static int check_hex(const unsigned char *p, size_t n)
{
    size_t i;
    size_t digits = 0;
    for (i = 0; i < n; i++)
    {
        if (p[i] == ' ' || p[i] == '\t')
        {
            continue;
        }
        if (!isxdigit(p[i]))
        {
            return 0;
        }
        digits++;
    }
    return digits > 0;
}

/* Whole number: sign?, digits, no dot, no exponent (or zero-valued
 * exponent - we accept any integer REXX number via classify_number
 * and then ensure it's LNUM_INTEGER). */
static int check_whole(const unsigned char *p, size_t n)
{
    return classify_number(p, n) == LNUM_INTEGER;
}

int irx_datatype(PLstr s, char option)
{
    if (s == NULL || s->pstr == NULL || s->len == 0)
    {
        return 0;
    }

    switch (option)
    {
        case '\0':
        case 'N':
        case 'n':
            return classify_number(s->pstr, s->len) != LNUM_NOT_NUM;

        case 'A':
        case 'a':
            return all_match(s->pstr, s->len, is_alnum);

        case 'B':
        case 'b':
            return all_match(s->pstr, s->len, is_binary_char);

        case 'L':
        case 'l':
            return all_match(s->pstr, s->len, is_lower);

        case 'M':
        case 'm':
            return all_match(s->pstr, s->len, is_alpha);

        case 'S':
        case 's':
            /* Shares the tokenizer's rule: non-empty, first char a
             * symbol starter (letter / special), rest symbol chars.
             * This rejects leading digits and '.' — those introduce
             * constant symbols / numbers, not variable names. */
            return is_rexx_symbol(s->pstr, s->len);

        case 'U':
        case 'u':
            return all_match(s->pstr, s->len, is_upper);

        case 'W':
        case 'w':
            return check_whole(s->pstr, s->len);

        case 'X':
        case 'x':
            return check_hex(s->pstr, s->len);

        default:
            return 0;
    }
}
