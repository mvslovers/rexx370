/* IRX#STOR.C - Storage Management Replaceable Routine
**
** All memory allocation/deallocation for REXX/370 goes through
** this routine. It is a replaceable routine — callers can install
** a custom implementation via the Module Name Table (MODNAMET).
**
** Behaviour:
**   MVS  → GETMAIN/FREEMAIN issued here, by inline assembler, for ALL
**          subpools including 0 (#298). libc370's getmain() reports a
**          failure with wtof(), and that one call brought the printf
**          engine and stdio, about 42 KB, into every module that
**          allocates (libc370#283). Nothing here needs a C runtime, so
**          the entry-point wrappers (asm/irxinit.asm, asm/irxterm.asm)
**          can dispatch into the C core without @@CRT0 (#85, #234).
**
**          Every block carries libc370's 8-byte prefix, byte for byte:
**            +0  subpool << 24 | GETMAIN'd length (rounded to 64)
**            +4  storage key << 24 | requested size
**          and the caller gets the address after it. Release needs no
**          length from the caller. The layout must not change: under
**          TSO the TMP builds the environment with the INSTALLED IRXINIT,
**          and a newer IRXTERM frees that storage (and the other way
**          round), so blocks from either allocator must free with the
**          other.
**
**          Storage is zeroed on GETMAIN, as callers rely on (#293), and
**          cleared again before FREEMAIN, as libc370 does. A failure
**          writes one WTO in libc370's wording, which
**          tso/lab/region_ladder.py recognises.
**   Host → calloc/free (cross-compile / unit tests).
**
** Ref: SC28-1883-0, Chapter 16 (Storage Management)
** Ref: Architecture Design v0.1.0, Section 5.5
** Ref: GitHub mvslovers/rexx370#85, #234, #298
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxwkblk.h"

#ifdef __MVS__

/* Prefix ahead of every block, see the header. */
enum
{
    STOR_PREFIX = 8,
    STOR_ROUND = 64,
    STOR_MAX = 0x00FFFFFF,
    STOR_PSATOLD = 0x21C, /* PSA: current TCB */
    STOR_TCBPKF = 0x1C,   /* TCB: storage protection key */
    STOR_MSG_MAX = 124    /* one WTO line */
};

/* WTO parameter list, MCS flags 0. */
struct stor_wto
{
    short len;
    short mcsflags;
    char text[STOR_MSG_MAX];
};

static int stor_puts(char *buf, int pos, const char *s)
{
    while (*s != '\0' && pos < STOR_MSG_MAX)
    {
        buf[pos++] = *s++;
    }
    return pos;
}

static int stor_putu(char *buf, int pos, unsigned v)
{
    char digits[10];
    int n = 0;

    do
    {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    while (n > 0 && pos < STOR_MSG_MAX)
    {
        buf[pos++] = digits[--n];
    }
    return pos;
}

static int stor_putx(char *buf, int pos, unsigned v)
{
    for (int shift = 28; shift >= 0 && pos < STOR_MSG_MAX; shift -= 4)
    {
        buf[pos++] = "0123456789ABCDEF"[(v >> shift) & 0xF];
    }
    return pos;
}

static void stor_wto(struct stor_wto *msg, int len)
{
    msg->len = (short)(len + 4);
    msg->mcsflags = 0;
    __asm__ volatile("LA\t1,0(,%0)\n\t"
                     "SVC\t35             WTO"
                     :
                     : "r"(msg)
                     : "0", "1", "14", "15");
}

/* Clear n bytes at p (MVCL with a zero pad byte, no libc). */
static void stor_clear(void *p, unsigned n)
{
    __asm__ volatile("LR\t14,%0\n\t"
                     "LR\t15,%1\n\t"
                     "SLR\t0,0\n\t"
                     "SLR\t1,1\n\t"
                     "MVCL\t14,0"
                     :
                     : "r"(p), "r"(n)
                     : "0", "1", "14", "15", "memory");
}

static void *stor_getmain(unsigned size, unsigned sp)
{
    if (size == 0 || size > STOR_MAX - STOR_PREFIX)
    {
        return NULL;
    }
    sp &= 0xFF;
    unsigned lv = (size + STOR_PREFIX + STOR_ROUND - 1) &
                  (STOR_MAX & ~(unsigned)(STOR_ROUND - 1));

    int rc;
    char *r1;
    __asm__ volatile("GETMAIN RC,LV=(%2),SP=(%3)\n\t"
                     "LR\t%0,15\n\t"
                     "LR\t%1,1"
                     : "=r"(rc), "=r"(r1)
                     : "r"(lv), "r"(sp)
                     : "0", "1", "14", "15");
    if (rc != 0)
    {
        struct stor_wto msg;
        int n = stor_puts(msg.text, 0, "IRXSTOR getmain request for ");
        n = stor_putu(msg.text, n, lv);
        n = stor_puts(msg.text, n, " bytes from sp=");
        n = stor_putu(msg.text, n, sp);
        n = stor_puts(msg.text, n, " failed, rc=");
        n = stor_putu(msg.text, n, (unsigned)rc);
        stor_wto(&msg, n);
        return NULL;
    }

    /* Key is informational (freemain does not use it); in problem
     * state the TCB's key is the PSW key. */
    const unsigned char *tcb =
        *(const unsigned char *const *)(uintptr_t)STOR_PSATOLD;
    unsigned key = tcb[STOR_TCBPKF];

    stor_clear(r1, lv);
    ((unsigned *)r1)[0] = sp << 24 | lv;
    ((unsigned *)r1)[1] = key << 24 | size;
    return r1 + STOR_PREFIX;
}

static void stor_freemain(void *addr)
{
    char *blk = (char *)addr - STOR_PREFIX;
    unsigned sp = ((unsigned *)blk)[0] >> 24;
    unsigned lv = ((unsigned *)blk)[0] & STOR_MAX;
    unsigned size = ((unsigned *)blk)[1] & STOR_MAX;

    /* A zero length is a block freed twice; a length that does not
     * match the rounding is a damaged prefix.  Either way FREEMAIN
     * with these values would release the wrong storage: report and
     * keep it. */
    if (lv == 0 || size == 0 || lv < size + STOR_PREFIX ||
        lv - (size + STOR_PREFIX) >= STOR_ROUND)
    {
        struct stor_wto msg;
        int n = stor_puts(msg.text, 0,
                          lv == 0 || size == 0
                              ? "IRXSTOR duplicate freemain at "
                              : "IRXSTOR freemain: bad storage prefix at ");
        n = stor_putx(msg.text, n, (unsigned)(uintptr_t)blk);
        stor_wto(&msg, n);
        return;
    }

    stor_clear(blk, lv);
    __asm__ volatile("FREEMAIN RC,A=(%0),LV=(%1),SP=(%2)"
                     :
                     : "r"(blk), "r"(lv), "r"(sp)
                     : "0", "1", "14", "15", "memory");
}
#endif

/* ------------------------------------------------------------------ */
/*  irxstor - Acquire or release storage                              */
/*                                                                    */
/*  function  - RXSMGET (0) = acquire, RXSMFRE (1) = release          */
/*  length    - RXSMGET: requested size. RXSMFRE: size to free.       */
/*  addr_ptr  - RXSMGET: output addr. RXSMFRE: input addr.            */
/*  envblock  - Owning ENVBLOCK (may be NULL during bootstrap)        */
/*                                                                    */
/*  Returns: 0=OK, 20=storage not available or invalid address        */
/* ------------------------------------------------------------------ */

int irxstor(int function, int length, void **addr_ptr,
            struct envblock *envblock)
{
#ifdef __MVS__
    int subpool = 0;

    /* Determine subpool from PARMBLOCK if available */
    if (envblock != NULL && envblock->envblock_parmblock != NULL)
    {
        struct parmblock *pb = (struct parmblock *)envblock->envblock_parmblock;
        if (pb->parmblock_subpool > 0)
        {
            subpool = pb->parmblock_subpool;
        }
    }
#else
    (void)envblock;
#endif

    switch (function)
    {
        case RXSMGET:
            if (length <= 0 || addr_ptr == NULL)
            {
                return 20;
            }
#ifdef __MVS__
            {
                /* Zeroed, with the prefix that lets RXSMFRE recover
                 * subpool and length (see the header). */
                void *ptr = stor_getmain((unsigned)length, (unsigned)subpool);
                if (ptr == NULL)
                {
                    return 20;
                }
                *addr_ptr = ptr;
            }
#else
            {
                void *ptr = calloc(1, (size_t)length);
                if (ptr == NULL)
                {
                    return 20;
                }
                *addr_ptr = ptr;
            }
#endif
            return 0;

        case RXSMFRE:
            if (addr_ptr == NULL || *addr_ptr == NULL)
            {
                return 20;
            }
#ifdef __MVS__
            stor_freemain(*addr_ptr);
#else
            free(*addr_ptr);
#endif
            *addr_ptr = NULL;
            return 0;

        default:
            return 20;
    }
}
