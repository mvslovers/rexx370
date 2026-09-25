/* irx#ldbp.c - IRXLDTSO's member reader: BPAM, no C runtime
**
** One of the two implementations of irx_ld_read_member() (irxldrd.h).
** This one is linked into IRXLDTSO, the exec load routine IRXTSPRM
** names in MODNAMET EXROUT. The I/O is asm/irxbpam.asm -- OPEN, FIND,
** READ/CHECK, CLOSE -- and this file only deblocks, so nothing here
** needs a C runtime: storage comes from irxstor (getmain on MVS), and
** memcpy/memset are compiled inline.
**
** Line semantics are the ones irx#ldqs.c has through fgets: one line
** per logical record, handed to irx_ld_add_line() with the RECFM and
** LRECL from the DCB, which removes sequence numbers when the member is
** numbered (#231) and strips trailing blanks.
**
** Ref: SC28-1883-0 Chapter 16 (Exec Load Routine); GitHub #230
**
** (c) 2026 mvslovers - REXX/370 Project
*/

#ifdef __MVS__

#include <stddef.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxldrd.h"
#include "irxload.h"

enum
{
    CL8_LEN = 8,

    /* IRXBPAM function codes (BPFUNC). */
    BP_OPEN = 1,
    BP_FIND = 2,
    BP_READ = 3,
    BP_CLOSE = 4,

    /* FIND's "member not found". */
    BP_FIND_NOTFOUND = 4,

    /* Bytes of BPAREA handed to IRXBPAM: save area, DCB, DECB and OPEN
     * list. IRXBPAM refuses a shorter area (BALEN in the .asm). */
    BP_AREA = 256,

    /* DCBRECFM: the top two bits are the format. */
    RECFM_FORMAT = 0xC0,
    RECFM_F = 0x80,
    RECFM_V = 0x40,
    RECFM_U = 0xC0,

    /* A V block starts with a 4-byte BDW, each record with a 4-byte
     * RDW; the first halfword of either is the length including it. */
    VB_PREFIX = 4
};

/* Mirrors the BPARM DSECT in asm/irxbpam.asm, offset for offset. */
struct bpam_parm
{
    int func;                    /* +0  */
    int rc;                      /* +4  */
    unsigned char name[CL8_LEN]; /* +8  DDNAME / member, blank padded */
    void *buf;                   /* +16 READ buffer                    */
    int len;                     /* +20 READ: bytes read, 0 = end      */
    unsigned short blksize;      /* +24 OPEN: DCBBLKSI                 */
    unsigned short lrecl;        /* +26 OPEN: DCBLRECL                 */
    unsigned char recfm;         /* +28 OPEN: DCBRECFM                 */
    unsigned char flags;         /* +29 IRXBPAM's own                  */
    unsigned short alen;         /* +30 length of area                 */
    unsigned char area[BP_AREA]; /* +32 IRXBPAM's DCB, DECB, ...       */
};

typedef char bpam_area_ofs_ok_[(offsetof(struct bpam_parm, area) == 32) ? 1 : -1];

int bpam_call(struct bpam_parm *p) asm("IRXBPAM");

static unsigned int halfword(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | p[1];
}

/* Split one block into logical records and append each as a line. */
static int deblock(const unsigned char *blk, int len,
                   const struct bpam_parm *p, struct irx_ld_acc *acc)
{
    switch (p->recfm & RECFM_FORMAT)
    {
        case RECFM_F:
        {
            int lrecl = (int)p->lrecl;
            if (lrecl <= 0 || len % lrecl != 0)
            {
                return IRXLOAD_ERROR; /* a block of partial records */
            }
            for (int off = 0; off < len; off += lrecl)
            {
                int rc = irx_ld_add_line(acc, (const char *)blk + off, lrecl);
                if (rc != 0)
                {
                    return rc;
                }
            }
            return 0;
        }

        case RECFM_V:
        {
            if (len < VB_PREFIX)
            {
                return IRXLOAD_ERROR;
            }
            int limit = (int)halfword(blk);
            if (limit > len)
            {
                limit = len;
            }
            int off = VB_PREFIX;
            while (off + VB_PREFIX <= limit)
            {
                int rl = (int)halfword(blk + off);
                if (rl < VB_PREFIX || off + rl > limit)
                {
                    return IRXLOAD_ERROR; /* RDW runs past the block */
                }
                int rc = irx_ld_add_line(acc, (const char *)blk + off + VB_PREFIX,
                                         rl - VB_PREFIX);
                if (rc != 0)
                {
                    return rc;
                }
                off += rl;
            }
            return 0;
        }

        case RECFM_U:
            return irx_ld_add_line(acc, (const char *)blk, len);

        default:
            return IRXLOAD_ERROR;
    }
}

static void bp_free(void **pp, struct envblock *env)
{
    if (*pp != NULL)
    {
        irxstor(RXSMFRE, 0, pp, env);
    }
}

int irx_ld_read_member(const char *ddname, const unsigned char *member8,
                       struct irx_ld_acc *acc)
{
    void *pv = NULL;
    void *buf = NULL;
    if (irxstor(RXSMGET, (int)sizeof(struct bpam_parm), &pv, acc->env) != 0)
    {
        return IRXLOAD_NOMEM;
    }
    struct bpam_parm *p = (struct bpam_parm *)pv;
    p->alen = (unsigned short)BP_AREA;

    memset(p->name, ' ', CL8_LEN);
    for (int i = 0; i < CL8_LEN && ddname[i] != '\0'; i++)
    {
        p->name[i] = (unsigned char)ddname[i];
    }
    p->func = BP_OPEN;
    if (bpam_call(p) != 0)
    {
        /* No DD, or it would not open: this DD has no such exec. */
        bp_free(&pv, acc->env);
        return IRXLOAD_NOTFOUND;
    }

    switch (p->recfm & RECFM_FORMAT)
    {
        case RECFM_F:
            irx_ld_begin_member(acc, IRX_LD_RECFM_F, (int)p->lrecl);
            break;
        case RECFM_V:
            irx_ld_begin_member(acc, IRX_LD_RECFM_V, (int)p->lrecl);
            break;
        default:
            irx_ld_begin_member(acc, IRX_LD_RECFM_UNKNOWN, 0);
            break;
    }

    int rc = 0;
    memcpy(p->name, member8, CL8_LEN);
    p->func = BP_FIND;
    int fr = bpam_call(p);
    if (fr == BP_FIND_NOTFOUND)
    {
        rc = IRXLOAD_NOTFOUND;
    }
    else if (fr != 0)
    {
        rc = IRXLOAD_ERROR;
    }
    else if (p->blksize == 0 ||
             irxstor(RXSMGET, (int)p->blksize, &buf, acc->env) != 0)
    {
        rc = (p->blksize == 0) ? IRXLOAD_ERROR : IRXLOAD_NOMEM;
    }

    while (rc == 0)
    {
        p->func = BP_READ;
        p->buf = buf;
        if (bpam_call(p) != 0)
        {
            rc = IRXLOAD_ERROR; /* SYNAD: I/O error */
            break;
        }
        if (p->len == 0)
        {
            break; /* end of member */
        }
        rc = deblock((const unsigned char *)buf, p->len, p, acc);
    }

    p->func = BP_CLOSE;
    (void)bpam_call(p); /* input only: nothing to lose on CLOSE */
    bp_free(&buf, acc->env);
    bp_free(&pv, acc->env);
    return rc;
}

#else
/* The host build reads files in irx#load.c itself. An empty
 * translation unit produces no ESD entries and breaks the NCAL linker;
 * mirror irx#env.c's dummy. */
static int _irx_ldbp_dummy = 0;
#endif
