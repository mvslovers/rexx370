/* ------------------------------------------------------------------ */
/*  irx#tsio.c - IRXIOTSO, the TSO I/O replaceable routine           */
/*                                                                    */
/*  Its own load module on purpose.  SC28-1883-0 Chapter 16 puts the  */
/*  replaceable routines in MODNAMET by NAME, and IRXINIT LOADs the   */
/*  one the environment asks for: IRXTSPRM names IRXIOTSO, IRXPARMS   */
/*  leaves the slot blank and keeps the stdio routine in irx#io.c.    */
/*  Nothing links both, so no load module carries a routine it will   */
/*  never call -- which on a 16 MB machine is the point.              */
/*                                                                    */
/*  Writes through PUTLINE (IKJPUTL, via asm/putlin.asm).  PUTLINE    */
/*  reaches the terminal in the foreground and SYSTSPRT in the        */
/*  background, where the TMP STACKs SYSTSIN/SYSTSPRT.  TPUT does     */
/*  not: SVC 93 returns without doing anything in an address space    */
/*  with no TSB, which is exactly the batch TMP.  PUTLINE touches no  */
/*  libc state either, so this routine works where no C runtime       */
/*  exists -- the TSO address space, where IKJEFT01 and IKJCT430 are  */
/*  assembler.                                                        */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxio.h"
#include "irxwkblk.h"
#include "lstring.h"

#ifdef __MVS__

/* Control-block offsets, named after the IBM fields. Cross-checked
 * against the IBM sources (PSCB+52 and LWA+24/+32 as EQUs). The walk
 * to the LWA is the one irx#anch.c does for the ECT; it is repeated
 * here so IRXIOTSO does not have to link the anchor registry. */
enum
{
    PSAAOLD = 0x224,  /* PSA  -> ASCB of the current address space */
    ASCBASXB = 0x06C, /* ASCB -> ASXB                             */
    ASXBLWA = 0x014,  /* ASXB -> LWA                              */
    LWAPSCB = 24,     /* LWA  -> PSCB                             */
    LWAPECT = 32,     /* LWA  -> ECT                              */
    PSCBUPT = 52      /* PSCB -> UPT                              */
};

/* PTPB control bytes for OUTPUT=(line,TERM,SINGLE,DATA), as the
 * PUTLINE macro sets them: byte 0 X'30' = DATA + SINGLE, byte 1 X'00'
 * = TERM (not FORMAT). */
enum
{
    PTPB_CTL0_DATA_SINGLE = 0x30,
    PTPB_CTL1_TERM = 0x00
};

enum
{
    IOPL_WORDS = 4,  /* UPT, ECT, ECB, PTPB                          */
    PTPB_BYTES = 12, /* control word, PTPBOPUT, PTPBFLN              */
    PTPB_OPUT = 4,   /* offset of PTPBOPUT                           */
    SAVE_WORDS = 18  /* save area IKJPUTL stores our registers into */
};

/* Everything one PUTLINE call needs, in one block on the stack. */
struct putl_work
{
    void *iopl[IOPL_WORDS];
    unsigned char ptpb[PTPB_BYTES];
    unsigned int ecb;              /* IOPL ECB, posted by nobody    */
    unsigned int save[SAVE_WORDS]; /* save area for IKJPUTL         */
    /* Data line: LL (halfword, includes these 4 bytes), 0, text. */
    unsigned char line[TSO_LINE_HDR + TSO_LINE_MAX];
};

int putl_call(void *iopl, void *savearea) asm("PUTLCALL");

static void *cb_at(void *base, int offset)
{
    if (base == NULL)
    {
        return NULL;
    }
    return *(void **)((char *)base + offset);
}

/* One line, at most TSO_LINE_MAX bytes, through PUTLINE. */
static int tso_put_line(const char *buf, int len)
{
    void *ascb = *(void **)PSAAOLD;
    void *lwa = cb_at(cb_at(ascb, ASCBASXB), ASXBLWA);
    void *ect = cb_at(lwa, LWAPECT);
    void *upt = cb_at(cb_at(lwa, LWAPSCB), PSCBUPT);
    if (ect == NULL || upt == NULL)
    {
        /* No TMP in this address space: nothing PUTLINE could use. */
        return IRXIO_TSO_NO_TMP;
    }

    struct putl_work w;
    memset(&w, 0, sizeof(w));

    int ll = len + TSO_LINE_HDR;
    w.line[0] = (unsigned char)((ll >> 8) & 0xFF);
    w.line[1] = (unsigned char)(ll & 0xFF);
    memcpy(&w.line[TSO_LINE_HDR], buf, (size_t)len);

    w.ptpb[0] = PTPB_CTL0_DATA_SINGLE;
    w.ptpb[1] = PTPB_CTL1_TERM;
    void *oput = w.line;
    memcpy(&w.ptpb[PTPB_OPUT], &oput, sizeof(oput));

    w.iopl[0] = upt;
    w.iopl[1] = ect;
    w.iopl[2] = &w.ecb;
    w.iopl[3] = w.ptpb;

    return putl_call(w.iopl, w.save);
}

#else /* host: src/irx#putl.c captures the call instead */

int tso_put_line(const char *buf, int len);

#endif /* __MVS__ */

int irxinout_tso(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;

    switch (function)
    {
        case RXFWRITE:
        case RXFWRITERR:
        case RXFTWRITE:
        {
            const char *p = NULL;
            int len = 0;
            if (data != NULL && data->pstr != NULL)
            {
                p = (const char *)data->pstr;
                len = (int)data->len;
            }

            /* len 0 is legal and must still produce a line -- SAY with
             * no operand (see test/tstsay.c).  A data line of length
             * zero is not something to rely on, so send one blank. */
            if (len <= 0)
            {
                return tso_put_line(" ", 1);
            }

            /* A longer line goes out in pieces of TSO_LINE_MAX, so the
             * work area stays a fixed size on the stack.  Every piece is
             * sent even when one fails; the worst code is returned. */
            int worst = 0;
            while (len > 0)
            {
                int n = (len > TSO_LINE_MAX) ? TSO_LINE_MAX : len;
                int rc = tso_put_line(p, n);
                if (rc != 0)
                {
                    worst = rc;
                }
                p += n;
                len -= n;
            }
            return worst;
        }

        case RXFREAD:
        case RXFREADP:
            /* TODO WP-33b: PULL / LINEIN via GETLINE */
            return IRXIO_TSO_UNSUPPORTED;

        default:
            return IRXIO_TSO_UNSUPPORTED;
    }
}
