/* irx#load.c - IRXLOAD C-core: LOAD and FREE function codes
**
** Implements the IRXLOAD Programming Service per SC28-1883-0 §14.
**
** LOAD: locates a REXX exec in a PDS (MVS: BSAM) or flat file (host),
**       reads all source lines into an INSTBLK, and returns a pointer.
** FREE: releases the INSTBLK and its source pool allocated by LOAD.
**
** DD search order (per ticket WP-CPS-07):
**   1. EXECBLK_DDNAME if non-blank
**   2. SYSEXEC
**   3. SYSPROC
**   SYSUEXEC is out of scope (TSO-specific; future separate ticket).
**
** Source accumulation uses a single-pass approach with growable buffers:
**   - Temp buffers (line_info table + source pool) start at
**     IRXLOAD_INIT_LINES entries / IRXLOAD_INIT_SRCBYTES bytes and
**     double on exhaust via grow_pool().
**   - Execs that cannot be accommodated due to allocation failure
**     return RC=4 (IRXLOAD_NOMEM).
**
** LOAD/FREE bookkeeping:
**   The source-pool pointer is stashed in instblk._filler4[0..] using
**   memcpy so FREE can recover it without any external table.
**   _filler4 is IBM-reserved and private to this LOAD/FREE pair; no
**   other module may read or write these bytes in an IRXLOAD-owned block.
**
** Ref: SC28-1883-0 §14 (IRXLOAD Programming Service)
** Ref: CON-1 §3.4 (INSTBLK byte-exact layout)
** Ref: WP-CPS-07 / TSK-219 / GitHub mvslovers/rexx370#116
**
** (c) 2026 mvslovers - REXX/370 Project
*/

#include <stddef.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxinstb.h"
#include "irxload.h"

#ifdef __MVS__
#include <clibos.h>
#include <osdcb.h>
#include <osio.h>
#else
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#endif

/* On MVS, sizeof(struct instblk) must equal INSTBLK_HDRLEN (128).
 * On 64-bit hosts the layout differs (8-byte pointers); assertion is
 * MVS-only for the same reason as envblock_size_is_320_ in irx#init.c. */
#ifdef __MVS__
typedef char instblk_hdrlen_ok_[(sizeof(struct instblk) == INSTBLK_HDRLEN) ? 1 : -1];
#endif

/* Source-pool pointer stored in _filler4[0..1] by LOAD; recovered by FREE.
 * Assert pointer fits in the 8-byte _filler4 field (int[2] on MVS = 8 B,
 * same on 64-bit host). */
typedef char instblk_filler4_fits_[(sizeof(void *) <= sizeof(((struct instblk *)0)->_filler4)) ? 1 : -1];

/* Per-line accumulation entry during single-pass source read. */
struct line_info
{
    int offset; /* byte offset of this line's start in temp source pool */
    int length; /* line byte count (trailing spaces stripped) */
};

/* Initial capacities for growable accumulation buffers.
 * Both tables double on exhaust; allocation failure returns IRXLOAD_NOMEM. */
#define IRXLOAD_INIT_LINES    256
#define IRXLOAD_INIT_SRCBYTES 8192

/* EBCDIC space character (used to strip trailing blanks on MVS). */
#define EBCDIC_SPACE ((unsigned char)0x40)

/* Byte count of a BDW or RDW field in a VB block. */
#define VB_HDR_SIZE 4

/* Length of any CL8 (8-character blank-padded) IBM field. */
#define CL8_LEN 8

/* Byte length of a CL8 function code (same as CL8_LEN, named for clarity). */
#define IRXLOAD_FC_LEN CL8_LEN

/* Length of the TTR (Track-Track-Record) field in a BLDL entry (3 bytes). */
#define TTR_LEN 3

/* Bit shift to extract the high byte of a 16-bit big-endian integer. */
#define BYTE_SHIFT 8

/* Size of a NUL-terminated copy of a CL8 field (8 data bytes + NUL). */
#define CL8_BUFLEN (CL8_LEN + 1)

/* Allocate via irxstor; on failure jump to cleanup: in the enclosing
 * function.  Mirrors the ALLOC macro in irx#init.c. */
#define ALLOC(ptr, size, env)                               \
    do                                                      \
    {                                                       \
        void *_t = NULL;                                    \
        if (irxstor(RXSMGET, (int)(size), &_t, (env)) != 0) \
            goto cleanup;                                   \
        (ptr) = _t;                                         \
    } while (0)

/* Grow a pool that was allocated via irxstor.
 * Allocates a new block of new_cap bytes, copies *cur_cap bytes from *buf,
 * frees the old block, and updates *buf and *cur_cap.
 * Returns 0 on success, -1 if the new allocation fails. */
static int grow_pool(void **buf, int *cur_cap, int new_cap,
                     struct envblock *envblk)
{
    void *nb = NULL;
    if (irxstor(RXSMGET, new_cap, &nb, envblk) != 0)
    {
        return -1;
    }
    memcpy(nb, *buf, (size_t)*cur_cap);
    irxstor(RXSMFRE, 0, buf, envblk);
    *buf = nb;
    *cur_cap = new_cap;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  trim8 — strip trailing spaces from an 8-char padded field         */
/*  Writes a NUL-terminated copy to out (caller supplies >= 9 bytes). */
/* ------------------------------------------------------------------ */
static void trim8(const unsigned char *src, char *out)
{
    int len = CL8_LEN;
    /* EBCDIC space = 0x40; ASCII space = 0x20; both handled. */
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == EBCDIC_SPACE))
    {
        --len;
    }
    memcpy(out, src, (size_t)len);
    out[len] = '\0';
}

/* ================================================================== */
/*  build_instblk — assemble the final INSTBLK from accumulated data  */
/*                                                                    */
/*  Allocates two blocks via irxstor:                                 */
/*    1. header + entry table (INSTBLK_HDRLEN + n * 8 bytes)         */
/*    2. final source pool (total source bytes, min 1)               */
/*                                                                    */
/*  Fills the header, copies source text, and builds entry pointers.  */
/*  Stashes the source-pool pointer in instblk._filler4 for FREE.    */
/*  Returns IRXLOAD_OK on success; frees both blocks and returns      */
/*  IRXLOAD_NOMEM on allocation failure.                              */
/* ================================================================== */
static int build_instblk(struct envblock *envblk,
                         struct instblk **out,
                         const unsigned char *member8,
                         const unsigned char *ddname8,
                         const struct line_info *lt, int n,
                         const char *tsrc, int total)
{
    struct instblk *hdr = NULL;
    char *fsrc = NULL;
    struct instblk_entry *ents;
    int block_size;
    void *sp_copy;
    int i;

    /* Allocate at least sizeof(struct instblk) bytes for the header.
     * On 64-bit hosts the struct is wider than INSTBLK_HDRLEN (8-byte
     * pointers push _filler4 past offset 128); the entry array follows. */
    block_size = (int)(sizeof(struct instblk) > INSTBLK_HDRLEN
                           ? sizeof(struct instblk)
                           : INSTBLK_HDRLEN) +
                 n * (int)sizeof(struct instblk_entry);
    ALLOC(hdr, block_size, envblk);
    ALLOC(fsrc, (total > 0 ? total : 1), envblk);

    if (total > 0)
    {
        memcpy(fsrc, tsrc, (size_t)total);
    }

    /* Initialise header to zero, then fill known fields. */
    memset(hdr, 0, (size_t)INSTBLK_HDRLEN);
    memcpy(hdr->instblk_acronym, INSTBLK_ID, sizeof(hdr->instblk_acronym));
    hdr->instblk_hdrlen = INSTBLK_HDRLEN;
    ents = (struct instblk_entry *)((char *)hdr + INSTBLK_HDRLEN);
    hdr->instblk_address = ents;
    hdr->instblk_usedlen = n * (int)sizeof(struct instblk_entry);
    memcpy(hdr->instblk_member, member8, sizeof(hdr->instblk_member));
    if (ddname8)
    {
        memcpy(hdr->instblk_ddname, ddname8, sizeof(hdr->instblk_ddname));
    }
    /* instblk_subcom left blank (initial subcommand = default) */

    /* Build entry pointers: each entry points into the final source pool. */
    for (i = 0; i < n; i++)
    {
        ents[i].instblk_stmt_ = fsrc + lt[i].offset;
        ents[i].instblk_stmtlen = lt[i].length;
    }

    /* Stash source-pool pointer in _filler4 for FREE to recover. */
    sp_copy = fsrc;
    memcpy(hdr->_filler4, (const void *)&sp_copy, sizeof(void *));

    *out = hdr;
    return IRXLOAD_OK;

cleanup:
    if (fsrc)
    {
        void *p = fsrc;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (hdr)
    {
        void *p = hdr;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    return IRXLOAD_NOMEM;
}

/* ================================================================== */
/*  MVS BSAM path                                                     */
/* ================================================================== */
#ifdef __MVS__

/* scan_member — read all source lines of a PDS member via BSAM.
 *
 * On entry *lt_p/*lt_cap and *tsrc_p/*tsrc_cap describe caller-supplied
 * growable buffers.  On success the buffers (and caps) may have grown.
 * Returns 0 on success, IRXLOAD_NOTFOUND if the DD or member is absent,
 * or IRXLOAD_NOMEM if a reallocation or read-buffer allocation fails.
 */
static int scan_member(const char *ddname,
                       const unsigned char *member8,
                       struct envblock *envblk,
                       struct line_info **lt_p, int *lt_cap,
                       char **tsrc_p, int *tsrc_cap,
                       int *n_out, int *total_out)
{
    BLDL bldl_buf;
    DECB decb;
    DCB *dcb;
    void *rbuf_v = NULL;
    char *rbuf;
    int blksize;
    int lrecl;
    int recfm;
    struct line_info *lt;
    char *tsrc;
    int n = 0;
    int total = 0;
    int rc = IRXLOAD_NOTFOUND;

    memset(&bldl_buf, 0, sizeof(bldl_buf));
    bldl_buf.ff = 1;
    bldl_buf.ll = (short)sizeof(DE14);
    memcpy(bldl_buf.de14[0].name, member8, sizeof(bldl_buf.de14[0].name));

    dcb = osbdcb(ddname, NULL);
    if (!dcb)
    {
        return IRXLOAD_NOTFOUND;
    }

    if (osbopen(dcb, 0, "r") != 0)
    {
        osbclose(dcb, NULL, 1, 0);
        return IRXLOAD_NOTFOUND;
    }

    if (__bldl(&bldl_buf, dcb) != 0)
    {
        osbclose(dcb, NULL, 1, 0);
        return IRXLOAD_NOTFOUND;
    }

    /* Position DCB at member start: TTR from BLDL, R=0. */
    memcpy(dcb->ttrn, bldl_buf.de14[0].ttr, TTR_LEN);
    dcb->ttrn[TTR_LEN] = 0; /* R byte = 0 → start from beginning */

    blksize = (int)dcb->dcbblksi;
    lrecl = (int)dcb->dcblrecl;
    recfm = (int)(dcb->dcbrecfm & (DCBRECF | DCBRECV));

    if (irxstor(RXSMGET, blksize, &rbuf_v, envblk) != 0)
    {
        osbclose(dcb, NULL, 1, 0);
        return IRXLOAD_NOMEM;
    }
    rbuf = (char *)rbuf_v;
    lt = *lt_p;
    tsrc = *tsrc_p;
    rc = 0;

    for (;;)
    {
        int chk;
        osbread(&decb, dcb, rbuf, blksize);
        chk = oscheck(&decb);
        if (chk != 0)
        {
            break; /* end of member */
        }

        if (recfm == DCBRECF)
        {
            /* FB: fixed-length records of LRECL bytes each. */
            int nr = (lrecl > 0) ? blksize / lrecl : 0;
            int i;
            for (i = 0; i < nr; i++)
            {
                char *rec = rbuf + (ptrdiff_t)i * lrecl;
                int len = lrecl;
                /* Trim trailing EBCDIC spaces. */
                while (len > 0 && (unsigned char)rec[len - 1] == EBCDIC_SPACE)
                {
                    --len;
                }
                if (n >= *lt_cap / (int)sizeof(struct line_info))
                {
                    if (grow_pool((void **)lt_p, lt_cap, *lt_cap * 2, envblk) != 0)
                    {
                        rc = IRXLOAD_NOMEM;
                        goto done;
                    }
                    lt = *lt_p;
                }
                if (len > 0 && total + len > *tsrc_cap)
                {
                    int nc = *tsrc_cap * 2;
                    if (nc < total + len)
                    {
                        nc = (total + len) * 2;
                    }
                    if (grow_pool((void **)tsrc_p, tsrc_cap, nc, envblk) != 0)
                    {
                        rc = IRXLOAD_NOMEM;
                        goto done;
                    }
                    tsrc = *tsrc_p;
                }
                lt[n].offset = total;
                lt[n].length = len;
                if (len > 0)
                {
                    memcpy(tsrc + total, rec, (size_t)len);
                }
                total += len;
                n++;
            }
        }
        else if (recfm == DCBRECV)
        {
            /* VB: BDW (4-byte block descriptor) + records with RDW prefix. */
            int blk_len = ((unsigned char)rbuf[0] << BYTE_SHIFT) |
                          (unsigned char)rbuf[1];
            int pos = VB_HDR_SIZE; /* skip BDW */
            while (pos + VB_HDR_SIZE <= blk_len)
            {
                int rdw = ((unsigned char)rbuf[pos] << BYTE_SHIFT) |
                          (unsigned char)rbuf[pos + 1];
                int dlen = rdw - VB_HDR_SIZE;
                if (dlen < 0)
                {
                    break; /* corrupt RDW */
                }
                if (dlen > 0)
                {
                    if (n >= *lt_cap / (int)sizeof(struct line_info))
                    {
                        if (grow_pool((void **)lt_p, lt_cap, *lt_cap * 2, envblk) != 0)
                        {
                            rc = IRXLOAD_NOMEM;
                            goto done;
                        }
                        lt = *lt_p;
                    }
                    if (total + dlen > *tsrc_cap)
                    {
                        int nc = *tsrc_cap * 2;
                        if (nc < total + dlen)
                        {
                            nc = (total + dlen) * 2;
                        }
                        if (grow_pool((void **)tsrc_p, tsrc_cap, nc, envblk) != 0)
                        {
                            rc = IRXLOAD_NOMEM;
                            goto done;
                        }
                        tsrc = *tsrc_p;
                    }
                    lt[n].offset = total;
                    lt[n].length = dlen;
                    memcpy(tsrc + total, rbuf + pos + VB_HDR_SIZE, (size_t)dlen);
                    total += dlen;
                    n++;
                }
                pos += rdw;
            }
        }
        /* DCBRECU (undefined RECFM) is not supported for PDS source. */
    }

done:
    irxstor(RXSMFRE, 0, &rbuf_v, envblk);
    osbclose(dcb, NULL, 1, 0);
    if (rc == 0)
    {
        *n_out = n;
        *total_out = total;
    }
    return rc;
}

#endif /* __MVS__ */

/* ================================================================== */
/*  irx_load_load — LOAD function code implementation                 */
/* ================================================================== */
static int irx_load_load(struct execblk *execblk,
                         struct instblk **instblk_p,
                         struct envblock *envblk)
{
    struct line_info *lt = NULL;
    char *tsrc = NULL;
    int lt_cap = 0;
    int tsrc_cap = 0;
    int n = 0;
    int total = 0;
    int found = 0;
    int rc = IRXLOAD_NOTFOUND;

    /* Validate EXECBLK. */
    if (!execblk ||
        memcmp(execblk->exec_blk_acryn, EXECBLK_ID, sizeof(execblk->exec_blk_acryn)) != 0 ||
        execblk->exec_blk_length < EXECBLK_V1_LEN)
    {
        return IRXLOAD_ERROR;
    }

    /* Allocate temp accumulation buffers at initial capacity. */
    lt_cap = IRXLOAD_INIT_LINES * (int)sizeof(struct line_info);
    tsrc_cap = IRXLOAD_INIT_SRCBYTES;
    ALLOC(lt, lt_cap, envblk);
    ALLOC(tsrc, tsrc_cap, envblk);

#ifdef __MVS__
    {
        char dd_hint[CL8_BUFLEN];
        trim8(execblk->exec_ddname, dd_hint);

        if (dd_hint[0] != '\0')
        {
            /* Caller supplied a specific DD — use only that. */
            int sr = scan_member(dd_hint, execblk->exec_member, envblk,
                                 &lt, &lt_cap, &tsrc, &tsrc_cap,
                                 &n, &total);
            if (sr == 0)
            {
                found = 1;
            }
            else if (sr == IRXLOAD_NOMEM)
            {
                rc = IRXLOAD_NOMEM;
                goto cleanup;
            }
        }
        else
        {
            /* Standard search order: SYSEXEC first, then SYSPROC. */
            static const char *dds[] = {"SYSEXEC", "SYSPROC"};
            int di;
            for (di = 0; di < 2 && !found; di++)
            {
                int sr = scan_member(dds[di], execblk->exec_member, envblk,
                                     &lt, &lt_cap, &tsrc, &tsrc_cap,
                                     &n, &total);
                if (sr == 0)
                {
                    found = 1;
                }
                else if (sr == IRXLOAD_NOMEM)
                {
                    rc = IRXLOAD_NOMEM;
                    goto cleanup;
                }
            }
        }
    }
#else
    {
        char dd_hint[CL8_BUFLEN];
        char mname[CL8_BUFLEN];
        char fpath[512];

        trim8(execblk->exec_ddname, dd_hint);
        trim8(execblk->exec_member, mname);

        /* Uppercase member name for filesystem lookup. */
        {
            int i;
            for (i = 0; mname[i]; i++)
            {
                mname[i] = (char)toupper((unsigned char)mname[i]);
            }
        }

        /* Build DD search list.
         * On host, getenv("SYSEXEC") etc. return directory paths set by the
         * test harness.  If a DD-name env var is not set, skip that DD. */
        {
            const char *try_dds[3];
            int nd = 0;

            if (dd_hint[0] != '\0')
            {
                try_dds[nd++] = dd_hint;
            }
            else
            {
                try_dds[nd++] = "SYSEXEC";
                try_dds[nd++] = "SYSPROC";
            }

            {
                int di;
                for (di = 0; di < nd && !found; di++)
                {
                    const char *dir = getenv(try_dds[di]);
                    FILE *f;
                    if (!dir)
                    {
                        continue;
                    }
                    snprintf(fpath, sizeof(fpath), "%s/%s.rex", dir, mname);
                    f = fopen(fpath, "r");
                    if (!f)
                    {
                        continue;
                    }

                    /* Single-pass: fgets each line into growable buffers. */
                    {
                        char linebuf[256];
                        int read_ok = 1;
                        while (fgets(linebuf, (int)sizeof(linebuf), f))
                        {
                            int len = (int)strlen(linebuf);
                            /* Strip newline and trailing spaces. */
                            while (len > 0 &&
                                   (linebuf[len - 1] == '\n' ||
                                    linebuf[len - 1] == '\r' ||
                                    linebuf[len - 1] == ' '))
                            {
                                --len;
                            }
                            if (n >= lt_cap / (int)sizeof(struct line_info))
                            {
                                if (grow_pool((void **)&lt, &lt_cap,
                                              lt_cap * 2, envblk) != 0)
                                {
                                    rc = IRXLOAD_NOMEM;
                                    read_ok = 0;
                                    break;
                                }
                            }
                            if (len > 0 && total + len > tsrc_cap)
                            {
                                int new_cap = tsrc_cap * 2;
                                if (new_cap < total + len)
                                {
                                    new_cap = (total + len) * 2;
                                }
                                if (grow_pool((void **)&tsrc, &tsrc_cap,
                                              new_cap, envblk) != 0)
                                {
                                    rc = IRXLOAD_NOMEM;
                                    read_ok = 0;
                                    break;
                                }
                            }
                            lt[n].offset = total;
                            lt[n].length = len;
                            if (len > 0)
                            {
                                memcpy(tsrc + total, linebuf, (size_t)len);
                            }
                            total += len;
                            n++;
                        }
                        fclose(f);
                        if (!read_ok)
                        {
                            goto cleanup;
                        }
                        found = 1;
                    }
                }
            }
        }
    }
#endif /* __MVS__ */

    if (!found)
    {
        rc = IRXLOAD_NOTFOUND;
        goto cleanup;
    }

    rc = build_instblk(envblk, instblk_p,
                       execblk->exec_member,
                       execblk->exec_ddname,
                       lt, n, tsrc, total);

cleanup:
    if (tsrc)
    {
        void *p = tsrc;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (lt)
    {
        void *p = lt;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    return rc;
}

/* ================================================================== */
/*  irx_load_free — FREE function code implementation                 */
/* ================================================================== */
static int irx_load_free(struct instblk **instblk_pp,
                         struct envblock *envblk)
{
    struct instblk *hdr;
    void *sp = NULL;

    if (!instblk_pp || !*instblk_pp)
    {
        return IRXLOAD_ERROR;
    }

    hdr = *instblk_pp;
    if (memcmp(hdr->instblk_acronym, INSTBLK_ID, sizeof(hdr->instblk_acronym)) != 0)
    {
        return IRXLOAD_ERROR;
    }

    /* Recover and free source pool stashed by LOAD. */
    memcpy((void *)&sp, hdr->_filler4, sizeof(void *));
    if (sp)
    {
        irxstor(RXSMFRE, 0, &sp, envblk);
    }

    /* Clear caller's pointer before freeing so any use-after-free is
     * immediately visible as a NULL dereference. */
    *instblk_pp = NULL;
    {
        void *p = hdr;
        irxstor(RXSMFRE, 0, &p, envblk);
    }

    return IRXLOAD_OK;
}

/* ================================================================== */
/*  irx_load_dispatch — central dispatcher (asm() alias: IRXLDISP)   */
/* ================================================================== */
int irx_load_dispatch(const char *funccode,
                      struct execblk *execblk,
                      struct instblk **instblk_p,
                      struct envblock *envblk,
                      int *retval)
{
    int rc;

    if (!funccode || !instblk_p || !retval)
    {
        rc = IRXLOAD_ERROR;
        if (retval)
        {
            *retval = rc;
        }
        return rc;
    }

    if (memcmp(funccode, IRXLOAD_FC_LOAD, IRXLOAD_FC_LEN) == 0)
    {
        rc = irx_load_load(execblk, instblk_p, envblk);
    }
    else if (memcmp(funccode, IRXLOAD_FC_FREE, IRXLOAD_FC_LEN) == 0)
    {
        rc = irx_load_free(instblk_p, envblk);
    }
    else
    {
        rc = IRXLOAD_ERROR;
    }

    *retval = rc;
    return rc;
}
