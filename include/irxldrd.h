#ifndef IRXLDRD_H
#define IRXLDRD_H

/* irxldrd.h - the reader behind IRXLOAD's LOAD function (internal)
**
** irx#load.c owns the exec-load logic that does not depend on HOW a
** member is read: DD search order, INSTBLK construction, FREE. The
** reading itself sits behind irx_ld_read_member(), and each load module
** links exactly one implementation of it:
**
**   src/irx#ldqs.c  stdio (fopen "DD:dd(member)") -- IRXLOAD, batch.
**                   Needs a C runtime.
**   src/irx#ldbp.c  BPAM through asm/irxbpam.asm -- IRXLDTSO, the
**                   exec load routine IRXTSPRM names in MODNAMET EXROUT.
**                   Needs no C runtime, so it works in a TSO address
**                   space where IKJEFT01 and IKJCT430 are assembler.
**
** Both append lines through irx_ld_add_line(), so line semantics
** (sequence numbers removed from a numbered member, trailing blanks
** stripped, nothing else) are the same whichever reader is linked.
**
** Ref: SC28-1883-0 Chapter 16 (Exec Load Routine); GitHub #230
*/

#include "irx.h"

/* Per-line accumulation entry: where the line starts in the source
 * pool, and how long it is after stripping trailing blanks. */
struct line_info
{
    int offset;
    int length;
};

/* Record format as far as sequence numbers are concerned. */
enum
{
    IRX_LD_RECFM_UNKNOWN = 0, /* U, or the reader cannot tell: keep all */
    IRX_LD_RECFM_F = 1,       /* fixed: numbers in the last 8 columns   */
    IRX_LD_RECFM_V = 2        /* variable: numbers in the first 8       */
};

/* Width of a sequence number field (SC28-1883-0 p. 358). */
enum
{
    IRX_LD_SEQ_LEN = 8
};

/* Growable accumulation state for one LOAD. All storage comes from
 * irxstor against env. */
struct irx_ld_acc
{
    struct envblock *env;
    struct line_info *lt; /* line table                            */
    int lt_cap;           /* capacity of lt in BYTES                */
    char *tsrc;           /* source pool                            */
    int tsrc_cap;         /* capacity of tsrc in bytes              */
    int n;                /* lines so far                           */
    int total;            /* source bytes so far                    */

    /* Set by the reader before the first line (irx_ld_begin_member). */
    int recfm;    /* IRX_LD_RECFM_*                                */
    int lrecl;    /* logical record length, fixed formats          */
    int numbered; /* -1 undecided, 0 no, 1 strip sequence numbers  */
};

/* Start a member: forget earlier lines and record the format the
 * sequence-number rule needs. Every reader calls this once, before its
 * first irx_ld_add_line(). */
void irx_ld_begin_member(struct irx_ld_acc *acc, int recfm,
                         int lrecl) asm("IRXLDBEG");

/* Append one record. text/len is the record as read -- untrimmed, a
 * trailing CR/LF allowed. The first record decides whether the member
 * is numbered (SC28-1883-0 p. 358): fixed format and its last 8
 * characters numeric, or variable format and its first 8 numeric. If
 * so, those 8 characters are dropped from every record. Then trailing
 * blanks are stripped. Returns 0, or IRXLOAD_NOMEM when a buffer could
 * not grow. */
int irx_ld_add_line(struct irx_ld_acc *acc, const char *text,
                    int len) asm("IRXLDADD");

/* Read every line of member8 (CL8, blank padded) from DD ddname
 * (NUL-terminated) into acc. Returns 0, IRXLOAD_NOTFOUND when the DD
 * cannot be opened or the member is not in it, IRXLOAD_NOMEM, or
 * IRXLOAD_ERROR for an I/O error. On anything but 0 the lines already
 * appended stay in acc; the caller discards them. MVS only -- the host
 * build reads files in irx#load.c itself. */
int irx_ld_read_member(const char *ddname, const unsigned char *member8,
                       struct irx_ld_acc *acc) asm("IRXLDRDM");

#endif /* IRXLDRD_H */
