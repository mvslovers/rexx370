/* ------------------------------------------------------------------ */
/*  irxqsam.h — QSAM output through asm/irxqsam.asm (#302)            */
/*                                                                    */
/*  OPEN a DD for OUTPUT, PUT records (move mode), CLOSE, without a C */
/*  runtime.  The caller owns the parameter block and the area behind */
/*  it (DCB, OPEN list, save area), so IRXQSAM stays RENT.  The caller */
/*  builds every record: RDW for RECFM V, full LRECL for F, and the   */
/*  carriage-control byte for A.                                      */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#ifndef IRXQSAM_H
#define IRXQSAM_H

#include <stddef.h>

enum qsam_func
{
    QSAM_OPEN = 1,  /* name = DDNAME; out: lrecl, blksize, recfm */
    QSAM_PUT = 2,   /* rec = the record */
    QSAM_CLOSE = 3, /* writes the last block */
};

enum qsam_rc
{
    QSAM_OK = 0,
    QSAM_NOT_OPENED = 8, /* DD missing */
    QSAM_ERROR = 20,     /* I/O error, not open, or area too small */
};

/* DCBRECFM bits */
enum qsam_recfm
{
    QSAM_RECFM_F = 0x80,
    QSAM_RECFM_V = 0x40,
    QSAM_RECFM_A = 0x04, /* ASA carriage control in the first byte */
};

/* The area IRXQSAM keeps behind the block: save area 72, DCB about 96,
 * OPEN list 4.  Sized with room to spare; IRXQSAM checks area_len. */
#define QSAM_AREA_LEN 256

/* Mirrors the QPARM DSECT in asm/irxqsam.asm, offset for offset. */
struct qsam_parm
{
    int func;                                    /* +0  enum qsam_func              */
    int rc;                                      /* +4  out                         */
    char name[8];                                /* +8  DDNAME, blank padded        */
    void *rec;                                   /* +16 PUT: record                 */
    unsigned short lrecl;                        /* +20 OPEN: LRECL (out)           */
    unsigned short blksize;                      /* +22 OPEN: BLKSIZE (out)         */
    unsigned char recfm;                         /* +24 OPEN: RECFM (out)           */
    unsigned char flags;                         /* +25 IRXQSAM's own               */
    unsigned short area_len;                     /* +26 length of area              */
    int reserved;                                /* +28                             */
    double area[QSAM_AREA_LEN / sizeof(double)]; /* +32 IRXQSAM's */
};

#ifdef __MVS__
typedef char qsam_area_ofs_ok_[(offsetof(struct qsam_parm, area) == 32) ? 1 : -1];
int qsam_call(struct qsam_parm *p) asm("IRXQSAM");
#endif

#endif /* IRXQSAM_H */
