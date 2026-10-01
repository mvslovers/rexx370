/* ------------------------------------------------------------------ */
/*  irx#io.c — IRXINOUT Default I/O Replaceable Routine               */
/*                                                                    */
/*  MVS: irxinout, the load module IRXINOUT (#255).  Writes SAY,      */
/*  TRACE and error output to the OUTDD named in the environment's    */
/*  MODNAMET (SYSTSPRT when blank) through QSAM, asm/irxqsam.asm, the */
/*  way SC28-1883-0 describes the system-supplied routine.  No stdio  */
/*  and no C runtime (#302): before, stdio made this module 42 KB and */
/*  IRXJCL carried a second copy of it.                               */
/*                                                                    */
/*  The DCB stays open for the life of the environment; its area and */
/*  the record buffer hang off the work block (wkbi_io_state).        */
/*  IRXTERM calls RXFTERM, which CLOSEs and so writes the last block. */
/*  Without a work block that has the field (envblock NULL, or an     */
/*  environment from an older IRXINIT) every write opens, puts and    */
/*  closes on its own.                                                */
/*                                                                    */
/*  Records: RECFM and LRECL come from the DD or the data set, else   */
/*  VB/132 (the OPEN exit in irxqsam.asm), as libc370 wrote them.  A  */
/*  line longer than a record continues in the next record, as        */
/*  libc370 did; an empty line is an empty record.  RECFM A gets a    */
/*  blank carriage-control byte.                                      */
/*                                                                    */
/*  A TSO environment does not use this routine: IRXTSPRM names       */
/*  IRXIOTSO in its MODNAMET (src/irx#tsio.c, PUTLINE).               */
/*                                                                    */
/*  Host (cross-compile tests): irxinout_host writes to stdout.       */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 16 (Replaceable Routines)               */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxio.h"
#include "irxwkblk.h"
#include "lstring.h"

#ifdef __MVS__

#include "irxqsam.h"

enum
{
    IO_DDNAME_LEN = 8,
    IO_RDW_LEN = 4,
};

struct io_state
{
    struct qsam_parm qp;
    int data_max;       /* data bytes per record */
    unsigned char *rec; /* record buffer, LRECL bytes */
};

/* OUTDD from the MODNAMET copy, SYSTSPRT when blank or absent. */
static void io_outdd(const struct envblock *env, char out[IO_DDNAME_LEN])
{
    const struct parmblock *pb =
        env != NULL ? (const struct parmblock *)env->envblock_parmblock
                    : NULL;
    const struct modnamet *mn =
        pb != NULL ? (const struct modnamet *)pb->parmblock_modnamet : NULL;
    static const char dflt[IO_DDNAME_LEN] = {'S', 'Y', 'S', 'T',
                                             'S', 'P', 'R', 'T'};
    const unsigned char *src = (const unsigned char *)dflt;

    if (mn != NULL)
    {
        for (int i = 0; i < IO_DDNAME_LEN; i++)
        {
            if (mn->modnamet_outdd[i] != ' ')
            {
                src = mn->modnamet_outdd;
                break;
            }
        }
    }
    for (int i = 0; i < IO_DDNAME_LEN; i++)
    {
        out[i] = (char)src[i];
    }
}

/* Where the open state lives, or NULL when the environment has no
 * place for it. */
static void **io_state_slot(struct envblock *env)
{
    struct irx_wkblk_int *wk =
        env != NULL ? (struct irx_wkblk_int *)env->envblock_workblok_ext
                    : NULL;
    if (wk == NULL || memcmp(wk->wkbi_id, WKBLK_INT_ID, 4) != 0 ||
        !WKBI_HAS(wk, wkbi_io_state))
    {
        return NULL;
    }
    return &wk->wkbi_io_state;
}

static void io_release(struct io_state *st, struct envblock *env)
{
    if (st->rec != NULL)
    {
        void *p = st->rec;
        irxstor(RXSMFRE, 0, &p, env);
    }
    void *p = st;
    irxstor(RXSMFRE, 0, &p, env);
}

static struct io_state *io_open(struct envblock *env)
{
    void *mem = NULL;
    if (irxstor(RXSMGET, (int)sizeof(struct io_state), &mem, env) != 0)
    {
        return NULL;
    }
    struct io_state *st = (struct io_state *)mem;
    st->qp.func = QSAM_OPEN;
    st->qp.area_len = QSAM_AREA_LEN;
    io_outdd(env, st->qp.name);
    if (qsam_call(&st->qp) != QSAM_OK)
    {
        io_release(st, env);
        return NULL;
    }

    int lrecl = st->qp.lrecl;
    int data_max = lrecl;
    if (st->qp.recfm & QSAM_RECFM_V)
    {
        data_max -= IO_RDW_LEN;
    }
    if (st->qp.recfm & QSAM_RECFM_A)
    {
        data_max -= 1;
    }
    void *rec = NULL;
    if (data_max < 1 || irxstor(RXSMGET, lrecl, &rec, env) != 0)
    {
        st->qp.func = QSAM_CLOSE;
        (void)qsam_call(&st->qp);
        io_release(st, env);
        return NULL;
    }
    st->rec = (unsigned char *)rec;
    st->data_max = data_max;
    return st;
}

static void io_close(struct io_state *st, struct envblock *env)
{
    st->qp.func = QSAM_CLOSE;
    (void)qsam_call(&st->qp);
    io_release(st, env);
}

/* One record from n bytes at p (n <= data_max). */
static int io_put(struct io_state *st, const char *p, int n)
{
    unsigned char *r = st->rec;
    int off = (st->qp.recfm & QSAM_RECFM_V) ? IO_RDW_LEN : 0;

    if (st->qp.recfm & QSAM_RECFM_A)
    {
        r[off++] = ' ';
    }
    for (int i = 0; i < n; i++)
    {
        r[off++] = (unsigned char)p[i];
    }
    if (st->qp.recfm & QSAM_RECFM_V)
    {
        r[0] = (unsigned char)(off >> 8);
        r[1] = (unsigned char)off;
        r[2] = 0;
        r[3] = 0;
    }
    else
    {
        while (off < st->qp.lrecl)
        {
            r[off++] = ' ';
        }
    }
    st->qp.func = QSAM_PUT;
    st->qp.rec = r;
    return qsam_call(&st->qp) == QSAM_OK ? 0 : 20;
}

/* A line longer than a record continues in the next one. */
static int io_write(struct io_state *st, PLstr data)
{
    const char *p = (data != NULL && data->pstr != NULL)
                        ? (const char *)data->pstr
                        : "";
    int len = (data != NULL && data->pstr != NULL) ? (int)data->len : 0;
    int pos = 0;

    do
    {
        int n = len - pos;
        if (n > st->data_max)
        {
            n = st->data_max;
        }
        if (io_put(st, p + pos, n) != 0)
        {
            return 20;
        }
        pos += n;
    } while (pos < len);
    return 0;
}

int irxinout(int function, PLstr data, struct envblock *envblock)
{
    void **slot = io_state_slot(envblock);

    switch (function)
    {
        case RXFWRITE:
        case RXFWRITERR:
        case RXFTWRITE:
        {
            struct io_state *st =
                slot != NULL ? (struct io_state *)*slot : NULL;
            int keep = slot != NULL;
            if (st == NULL)
            {
                st = io_open(envblock);
                if (st == NULL)
                {
                    return 20;
                }
                if (keep)
                {
                    *slot = st;
                }
            }
            int rc = io_write(st, data);
            if (!keep)
            {
                io_close(st, envblock);
            }
            return rc;
        }
        case RXFTERM:
            if (slot != NULL && *slot != NULL)
            {
                io_close((struct io_state *)*slot, envblock);
                *slot = NULL;
            }
            return 0;
        case RXFREAD:
        case RXFREADP:
            /* TODO WP-33b: implement PULL / LINEIN */
            return 20;
        default:
            return 20;
    }
}

#else /* !__MVS__ */

int irxinout_host(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;

    switch (function)
    {
        case RXFWRITE:
        case RXFWRITERR:
        case RXFTWRITE:
            if (data != NULL && data->pstr != NULL && data->len > 0)
            {
                fwrite(data->pstr, 1, (size_t)data->len, stdout);
            }
            fputc('\n', stdout);
            return 0;

        case RXFREAD:
        case RXFREADP:
            /* TODO WP-33b: implement PULL / LINEIN */
            return 20;

        default:
            return 20;
    }
}

#endif /* __MVS__ */
