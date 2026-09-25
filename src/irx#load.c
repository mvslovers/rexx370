/* irx#load.c - IRXLOAD C-core: LOAD and FREE function codes
**
** Implements the IRXLOAD Programming Service per SC28-1883-0 §14.
**
** LOAD: locates a REXX exec in a PDS (MVS: through the reader linked with
**       this module, see irxldrd.h) or flat file (host),
**       reads all source lines into an INSTBLK, and returns a pointer.
** FREE: releases the INSTBLK and its source pool allocated by LOAD.
** INIT, TERM, CLOSEDD: nothing to do (no DD outlives a LOAD), RC 0.
** STATUS: nothing is cached, so RC 4 (not loaded) and INSTBLK 0.
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
** Ref: WP-CPS-07 / TSK-219 / GitHub mvslovers/rexx370#118
**
** (c) 2026 mvslovers - REXX/370 Project
*/

#include <stddef.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxinstb.h"
#include "irxldrd.h"
#include "irxload.h"

/* stdio on the host only: on MVS the reading is done behind
 * irx_ld_read_member() (irxldrd.h), and this file must stay free of
 * anything that needs a C runtime -- IRXLDTSO links it too. */
#ifndef __MVS__
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

/* Initial capacities for growable accumulation buffers.
 * Both tables double on exhaust; allocation failure returns IRXLOAD_NOMEM. */
#define IRXLOAD_INIT_LINES    256
#define IRXLOAD_INIT_SRCBYTES 8192

/* EBCDIC space character (used to strip trailing blanks on MVS). */
#define EBCDIC_SPACE ((unsigned char)0x40)

/* Length of any CL8 (8-character blank-padded) IBM field. */
#define CL8_LEN 8

/* Byte length of a CL8 function code (same as CL8_LEN, named for clarity). */
#define IRXLOAD_FC_LEN CL8_LEN

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
/*  irx_ld_begin_member - start a member                              */
/* ------------------------------------------------------------------ */
void irx_ld_begin_member(struct irx_ld_acc *acc, int recfm, int lrecl)
{
    acc->n = 0;
    acc->total = 0;
    acc->recfm = recfm;
    acc->lrecl = lrecl;
    acc->numbered = -1; /* the first record decides */
}

/* All of text[0..IRX_LD_SEQ_LEN) are digits. */
static int all_digits(const char *text)
{
    for (int i = 0; i < IRX_LD_SEQ_LEN; i++)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return 0;
        }
    }
    return 1;
}

/* SC28-1883-0 p. 358: a member is numbered if its FIRST record is --
 * fixed format and the last eight characters of the record numeric, or
 * variable format and the first eight numeric. For fixed records "the
 * last eight" means columns LRECL-7..LRECL, so the record must still
 * be LRECL long: a shorter line only ends in digits by chance. */
static int first_record_numbered(const struct irx_ld_acc *acc,
                                 const char *text, int len)
{
    switch (acc->recfm)
    {
        case IRX_LD_RECFM_F:
            return acc->lrecl >= IRX_LD_SEQ_LEN && len == acc->lrecl &&
                   all_digits(text + len - IRX_LD_SEQ_LEN);
        case IRX_LD_RECFM_V:
            return len >= IRX_LD_SEQ_LEN && all_digits(text);
        default:
            return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  irx_ld_add_line - append one record to a LOAD's accumulation      */
/*                                                                    */
/*  Shared by every reader (irxldrd.h), so a line means the same      */
/*  thing whichever one is linked: CR/LF off, sequence numbers off    */
/*  when the member is numbered, trailing blanks off, nothing else.   */
/* ------------------------------------------------------------------ */
int irx_ld_add_line(struct irx_ld_acc *acc, const char *text, int len)
{
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
    {
        --len;
    }

    if (acc->numbered < 0)
    {
        acc->numbered = first_record_numbered(acc, text, len);
    }
    if (acc->numbered)
    {
        if (acc->recfm == IRX_LD_RECFM_F)
        {
            int keep = acc->lrecl - IRX_LD_SEQ_LEN;
            if (len > keep)
            {
                len = keep;
            }
        }
        else
        {
            int drop = (len < IRX_LD_SEQ_LEN) ? len : IRX_LD_SEQ_LEN;
            text += drop;
            len -= drop;
        }
    }

    while (len > 0 && text[len - 1] == ' ')
    {
        --len;
    }
    if (acc->n >= acc->lt_cap / (int)sizeof(struct line_info))
    {
        if (grow_pool((void **)&acc->lt, &acc->lt_cap, acc->lt_cap * 2,
                      acc->env) != 0)
        {
            return IRXLOAD_NOMEM;
        }
    }
    if (len > 0 && acc->total + len > acc->tsrc_cap)
    {
        int new_cap = acc->tsrc_cap * 2;
        if (new_cap < acc->total + len)
        {
            new_cap = (acc->total + len) * 2;
        }
        if (grow_pool((void **)&acc->tsrc, &acc->tsrc_cap, new_cap,
                      acc->env) != 0)
        {
            return IRXLOAD_NOMEM;
        }
    }
    acc->lt[acc->n].offset = acc->total;
    acc->lt[acc->n].length = len;
    if (len > 0)
    {
        memcpy(acc->tsrc + acc->total, text, (size_t)len);
    }
    acc->total += len;
    acc->n++;
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
/*  set_found_dd — record which DD actually answered                  */
/*                                                                     */
/*  SC28-1883-0 has instblk_ddname name the DD the exec came from, and */
/*  REXX surfaces it through PARSE SOURCE.  With a blank exec_ddname    */
/*  only the search knows the answer, so every site that sets found=1  */
/*  records it here.  Blank-padded to CL8 like the EXECBLK field.      */
/* ------------------------------------------------------------------ */

static void set_found_dd(unsigned char *dst8, const char *dd)
{
    size_t i = 0;

    while (i < 8 && dd[i] != '\0')
    {
        dst8[i] = (unsigned char)dd[i];
        i++;
    }
    while (i < 8)
    {
        dst8[i++] = ' ';
    }
}

/* ------------------------------------------------------------------ */
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

    /* Initialise header to zero, then fill known fields.
     * On 64-bit hosts sizeof(struct instblk) > INSTBLK_HDRLEN (8-byte
     * pointers shift _filler4 past offset 128).  Entries must start
     * after the C struct's actual tail, not at the fixed MVS offset,
     * to avoid clobbering them with the _filler4 stash below. */
    memset(hdr, 0, (size_t)INSTBLK_HDRLEN);
    memcpy(hdr->instblk_acronym, INSTBLK_ID, sizeof(hdr->instblk_acronym));
    hdr->instblk_hdrlen = INSTBLK_HDRLEN;
    ents = (struct instblk_entry *)((char *)hdr +
                                    (sizeof(struct instblk) > INSTBLK_HDRLEN
                                         ? sizeof(struct instblk)
                                         : (size_t)INSTBLK_HDRLEN));
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
/*  irx_load_load — LOAD function code implementation                 */
/* ================================================================== */
static int irx_load_load(struct execblk *execblk,
                         struct instblk **instblk_p,
                         struct envblock *envblk)
{
    struct irx_ld_acc acc;
    int found = 0;
    /* The DD that actually answered; blank until something is found. */
    unsigned char found_dd[8] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    int rc = IRXLOAD_NOTFOUND;

    memset(&acc, 0, sizeof(acc));
    acc.env = envblk;

    /* Validate EXECBLK. */
    if (!execblk ||
        memcmp(execblk->exec_blk_acryn, EXECBLK_ID, sizeof(execblk->exec_blk_acryn)) != 0 ||
        execblk->exec_blk_length < EXECBLK_V1_LEN)
    {
        return IRXLOAD_ERROR;
    }

    /* Allocate temp accumulation buffers at initial capacity. */
    acc.lt_cap = IRXLOAD_INIT_LINES * (int)sizeof(struct line_info);
    acc.tsrc_cap = IRXLOAD_INIT_SRCBYTES;
    ALLOC(acc.lt, acc.lt_cap, envblk);
    ALLOC(acc.tsrc, acc.tsrc_cap, envblk);

    char dd_hint[CL8_BUFLEN];
    trim8(execblk->exec_ddname, dd_hint);

    /* DD search list: the caller's DD alone, or SYSEXEC then SYSPROC. */
    const char *try_dds[2];
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

    for (int di = 0; di < nd && !found; di++)
    {
#ifdef __MVS__
        int sr = irx_ld_read_member(try_dds[di], execblk->exec_member, &acc);
        if (sr == IRXLOAD_NOTFOUND)
        {
            continue;
        }
        if (sr != 0)
        {
            rc = sr;
            goto cleanup;
        }
#else
        /* Host: the DD name is an environment variable holding a
         * directory, the member a file <MEMBER>.rex in it. */
        char mname[CL8_BUFLEN];
        char fpath[512];
        trim8(execblk->exec_member, mname);
        for (int i = 0; mname[i]; i++)
        {
            mname[i] = (char)toupper((unsigned char)mname[i]);
        }
        const char *dir = getenv(try_dds[di]);
        if (!dir)
        {
            continue;
        }
        snprintf(fpath, sizeof(fpath), "%s/%s.rex", dir, mname);
        FILE *f = fopen(fpath, "r");
        if (!f)
        {
            continue;
        }
        /* A host file has no record format, so nothing is ever treated
         * as numbered here; the rule itself is host-tested through
         * irx_ld_begin_member() with an explicit format. */
        irx_ld_begin_member(&acc, IRX_LD_RECFM_UNKNOWN, 0);
        char linebuf[256];
        int sr = 0;
        while (sr == 0 && fgets(linebuf, (int)sizeof(linebuf), f))
        {
            sr = irx_ld_add_line(&acc, linebuf, (int)strlen(linebuf));
        }
        fclose(f);
        if (sr != 0)
        {
            rc = sr;
            goto cleanup;
        }
#endif
        found = 1;
        set_found_dd(found_dd, try_dds[di]);
    }

    if (!found)
    {
        rc = IRXLOAD_NOTFOUND;
        goto cleanup;
    }

    rc = build_instblk(envblk, instblk_p,
                       execblk->exec_member,
                       found_dd,
                       acc.lt, acc.n, acc.tsrc, acc.total);

cleanup:
    if (acc.tsrc)
    {
        void *p = acc.tsrc;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (acc.lt)
    {
        void *p = acc.lt;
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
    else if (memcmp(funccode, IRXLOAD_FC_INIT, IRXLOAD_FC_LEN) == 0 ||
             memcmp(funccode, IRXLOAD_FC_TERM, IRXLOAD_FC_LEN) == 0 ||
             memcmp(funccode, IRXLOAD_FC_CLOSEDD, IRXLOAD_FC_LEN) == 0)
    {
        /* Nothing to set up or tear down: every LOAD opens and closes
         * its DD itself, so no file outlives the call. */
        rc = IRXLOAD_OK;
    }
    else if (memcmp(funccode, IRXLOAD_FC_STATUS, IRXLOAD_FC_LEN) == 0)
    {
        /* Nothing is cached, so no exec is "currently loaded" in the
         * sense of SC28-1883-0 p. 361: INSTBLK 0 and RC 4. */
        *instblk_p = NULL;
        rc = IRXLOAD_NOTLOADED;
    }
    else
    {
        rc = IRXLOAD_ERROR;
    }

    *retval = rc;
    return rc;
}
