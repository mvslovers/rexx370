/* ------------------------------------------------------------------ */
/*  tstldtso.c - EXROUT: the exec load routine is chosen per env      */
/*                                                                    */
/*  Like TSTIOTSO this means two different things in mbt's two legs,  */
/*  told apart by PARM (parm_batch = "0", parm_tso = "1"):            */
/*                                                                    */
/*    TSO leg    IRXTSPRM names IRXLDTSO in MODNAMET EXROUT, IRXINIT  */
/*               LOADs it into IRXEXTE's load_routine.  The test      */
/*               loads the fixture exec THROUGH that slot and         */
/*               compares every line with what IRXLOAD's own reader   */
/*               (stdio, linked into this program) makes of it.       */
/*    batch leg  IRXPARMS leaves EXROUT blank: load_routine stays     */
/*               NULL, IRXLOAD is used directly.                      */
/*                                                                    */
/*  The comparison is on the CONTENT -- line count, lengths, bytes --  */
/*  not on return codes: a reader that drops the short last block of  */
/*  an FB member returns 0 all the same.  The fixture TLDTSOX has 49  */
/*  records, one more block than 39 fit in (FB 80/3120).              */
/*                                                                    */
/*  The call through load_routine uses the five-slot form: from C, R0 */
/*  cannot be set, and the three-slot form takes the ENVBLOCK from R0. */
/*  The three-slot form is exercised by the assembler test TLDTSO.    */
/*                                                                    */
/*  Host: no load module to LOAD, so only the new function codes are  */
/*  checked (INIT, TERM, CLOSEDD, STATUS).                            */
/*                                                                    */
/*  GitHub #230.  (c) 2026 mvslovers - REXX/370 Project               */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxinstb.h"
#include "irxldrd.h"
#include "irxload.h"

#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        tests_run++;                     \
        if (cond)                        \
        {                                \
            tests_passed++;              \
            printf("  PASS: %s\n", msg); \
        }                                \
        else                             \
        {                                \
            tests_failed++;              \
            printf("  FAIL: %s\n", msg); \
        }                                \
    } while (0)

enum
{
    FIXTURE_LINES = 49 /* test/fixtures/tldtso/TLDTSOX */
};

/* High bit on the last address of an OS parameter list. */
#define VL_BIT 0x80000000UL

/* ------------------------------------------------------------------ */
/*  Sequence numbers (#231): the rule, fed one record at a time.      */
/*  Pure C, so it runs on the host and on MVS alike.                  */
/* ------------------------------------------------------------------ */
enum
{
    SEQ_LT_BYTES = 64 * (int)sizeof(struct line_info),
    SEQ_SRC_BYTES = 1024,
    FB_LRECL = 80,
    SEQ_COL = 72 /* numbers sit in columns 73-80 of an FB 80 record */
};

/* An FB 80 record: text padded to column 72, then seq in 73-80. */
static void fb_record(char out[FB_LRECL], const char *text, const char *seq)
{
    memset(out, ' ', FB_LRECL);
    memcpy(out, text, strlen(text));
    memcpy(out + SEQ_COL, seq, 8);
}

static int acc_open(struct irx_ld_acc *a, int recfm, int lrecl)
{
    void *lt = NULL;
    void *src = NULL;
    memset(a, 0, sizeof(*a));
    if (irxstor(RXSMGET, SEQ_LT_BYTES, &lt, NULL) != 0 ||
        irxstor(RXSMGET, SEQ_SRC_BYTES, &src, NULL) != 0)
    {
        return 0;
    }
    a->lt = lt;
    a->lt_cap = SEQ_LT_BYTES;
    a->tsrc = src;
    a->tsrc_cap = SEQ_SRC_BYTES;
    irx_ld_begin_member(a, recfm, lrecl);
    return 1;
}

static void acc_close(struct irx_ld_acc *a)
{
    void *p = a->lt;
    irxstor(RXSMFRE, 0, &p, NULL);
    p = a->tsrc;
    irxstor(RXSMFRE, 0, &p, NULL);
}

/* Line i of the accumulation equals want. */
static int line_is(const struct irx_ld_acc *a, int i, const char *want)
{
    int len = (int)strlen(want);
    return i < a->n && a->lt[i].length == len &&
           memcmp(a->tsrc + a->lt[i].offset, want, (size_t)len) == 0;
}

static void test_sequence_numbers(void)
{
    printf("--- #231: sequence numbers ---\n");
    struct irx_ld_acc a;
    char rec[FB_LRECL];

    /* FB, numbered: the first record is 80 long and ends in digits. */
    if (acc_open(&a, IRX_LD_RECFM_F, FB_LRECL))
    {
        fb_record(rec, "/* REXX */", "00000100");
        irx_ld_add_line(&a, rec, FB_LRECL);
        fb_record(rec, "say 'x'", "00000200");
        irx_ld_add_line(&a, rec, FB_LRECL);
        CHECK(a.numbered == 1, "FB: first record 73-80 numeric -> numbered");
        CHECK(line_is(&a, 0, "/* REXX */") && line_is(&a, 1, "say 'x'"),
              "FB: columns 73-80 dropped from every record");
        acc_close(&a);
    }

    /* FB, not numbered: a line that merely ENDS in eight digits is short
     * once trailing blanks are gone -- but the reader hands over the
     * record untrimmed, so it is 80 long with blanks in 73-80. */
    if (acc_open(&a, IRX_LD_RECFM_F, FB_LRECL))
    {
        fb_record(rec, "x = 12345678", "        ");
        irx_ld_add_line(&a, rec, FB_LRECL);
        fb_record(rec, "y = 1", "00000200");
        irx_ld_add_line(&a, rec, FB_LRECL);
        CHECK(a.numbered == 0, "FB: blank 73-80 in the first record -> not numbered");
        CHECK(line_is(&a, 0, "x = 12345678"), "FB: digits inside the text kept");
        CHECK(a.n == 2 && a.lt[1].length == FB_LRECL,
              "FB: later records untouched when the first is not numbered");
        acc_close(&a);
    }

    /* FB, first record shorter than LRECL (a reader that trims): the
     * last eight characters of a SHORT line are not columns 73-80. */
    if (acc_open(&a, IRX_LD_RECFM_F, FB_LRECL))
    {
        irx_ld_add_line(&a, "x = 12345678", 12);
        CHECK(a.numbered == 0 && line_is(&a, 0, "x = 12345678"),
              "FB: short first record ending in digits -> not numbered");
        acc_close(&a);
    }

    /* FB, 73-80 not all digits. */
    if (acc_open(&a, IRX_LD_RECFM_F, FB_LRECL))
    {
        fb_record(rec, "/* REXX */", "ABCD1234");
        irx_ld_add_line(&a, rec, FB_LRECL);
        CHECK(a.numbered == 0, "FB: 73-80 not all digits -> not numbered");
        acc_close(&a);
    }

    /* VB, numbered: the first eight characters of the first record. */
    if (acc_open(&a, IRX_LD_RECFM_V, 0))
    {
        irx_ld_add_line(&a, "00000100/* REXX */", 18);
        irx_ld_add_line(&a, "00000200say 1", 13);
        irx_ld_add_line(&a, "0000030", 7); /* shorter than a number */
        CHECK(a.numbered == 1, "VB: first 8 numeric -> numbered");
        CHECK(line_is(&a, 0, "/* REXX */") && line_is(&a, 1, "say 1") &&
                  line_is(&a, 2, ""),
              "VB: first 8 dropped from every record");
        acc_close(&a);
    }

    /* VB, not numbered. */
    if (acc_open(&a, IRX_LD_RECFM_V, 0))
    {
        irx_ld_add_line(&a, "/* REXX */", 10);
        irx_ld_add_line(&a, "00000200say 1", 13);
        CHECK(a.numbered == 0 && line_is(&a, 1, "00000200say 1"),
              "VB: first record not numbered -> nothing dropped");
        acc_close(&a);
    }

    /* Unknown format (U, host files): never numbered. */
    if (acc_open(&a, IRX_LD_RECFM_UNKNOWN, 0))
    {
        fb_record(rec, "/* REXX */", "00000100");
        irx_ld_add_line(&a, rec, FB_LRECL);
        CHECK(a.numbered == 0 && a.lt[0].length == FB_LRECL,
              "U/unknown: nothing dropped");
        acc_close(&a);
    }
}

static void make_execblk(struct execblk *e, const char *member8,
                         const char *ddname8)
{
    memset(e, 0, sizeof(*e));
    memcpy(e->exec_blk_acryn, EXECBLK_ID, sizeof(e->exec_blk_acryn));
    e->exec_blk_length = EXECBLK_V1_LEN;
    memcpy(e->exec_member, member8, sizeof(e->exec_member));
    memcpy(e->exec_ddname, ddname8, sizeof(e->exec_ddname));
}

/* The direct call: IRXLOAD's C core with the reader linked into this
 * program (stdio on MVS). */
static int load_direct(const char *fc, struct execblk *eb,
                       struct instblk **ib, struct envblock *env)
{
    int rv = -1;
    return irx_load_dispatch(fc, eb, ib, env, &rv);
}

#ifdef __MVS__
/* A call through IRXEXTE's load_routine, five-slot form: R1 -> five
 * addresses, the last with its high bit set. cc370 passes the argument
 * VALUES as that list, so the values are the addresses. */
typedef int (*exrout_fn)(const char *fc, struct execblk **eb,
                         struct instblk **ib, struct envblock **env,
                         int *rc);

static int load_via(void *routine, const char *fc, struct execblk *eb,
                    struct instblk **ib, struct envblock *env)
{
    struct execblk *ebp = eb;
    struct envblock *envp = env;
    int rv = -1;
    int *rvl = (int *)((unsigned long)&rv | VL_BIT);
    return ((exrout_fn)routine)(fc, &ebp, ib, &envp, rvl);
}

static int n_lines(const struct instblk *ib)
{
    return ib->instblk_usedlen / (int)sizeof(struct instblk_entry);
}

/* Same lines, same lengths, same bytes. */
static int same_content(const struct instblk *a, const struct instblk *b)
{
    if (n_lines(a) != n_lines(b))
    {
        return 0;
    }
    const struct instblk_entry *ea = a->instblk_address;
    const struct instblk_entry *eb = b->instblk_address;
    for (int i = 0; i < n_lines(a); i++)
    {
        if (ea[i].instblk_stmtlen != eb[i].instblk_stmtlen ||
            memcmp(ea[i].instblk_stmt_, eb[i].instblk_stmt_,
                   (size_t)ea[i].instblk_stmtlen) != 0)
        {
            printf("    line %d differs\n", i + 1);
            return 0;
        }
    }
    return 1;
}
#endif /* __MVS__ */

int main(int argc, char **argv)
{
    int expect_tso = (argc >= 2) ? atoi(argv[1]) : 0;

    printf("=== TSTLDTSO: exec load routine per environment (expect_tso=%d) ===\n",
           expect_tso);

    struct envblock *env = NULL;
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    if (env == NULL)
    {
        printf("\n=== %d/%d passed (%d failed) ===\n", tests_passed, tests_run,
               tests_failed);
        return 1;
    }

    test_sequence_numbers();

    /* The functions every exec load routine has to answer
     * (SC28-1883-0 p. 359), checked on the C core directly. */
    struct execblk eb;
    make_execblk(&eb, "TLDTSOX ", "        ");
    struct instblk *ib = (struct instblk *)&eb; /* anything non-NULL */
    CHECK(load_direct(IRXLOAD_FC_INIT, NULL, &ib, env) == IRXLOAD_OK,
          "INIT returns 0");
    CHECK(load_direct(IRXLOAD_FC_CLOSEDD, &eb, &ib, env) == IRXLOAD_OK,
          "CLOSEDD returns 0");
    CHECK(load_direct(IRXLOAD_FC_STATUS, &eb, &ib, env) == IRXLOAD_NOTLOADED &&
              ib == NULL,
          "STATUS: RC 4 and INSTBLK 0 (nothing is cached)");
    CHECK(load_direct(IRXLOAD_FC_TERM, NULL, &ib, env) == IRXLOAD_OK,
          "TERM returns 0");

#ifdef __MVS__
    struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
    struct parmblock *pb = (struct parmblock *)env->envblock_parmblock;
    struct modnamet *mnt =
        (pb != NULL) ? (struct modnamet *)pb->parmblock_modnamet : NULL;
    CHECK(exte != NULL && mnt != NULL, "IRXEXTE and MODNAMET present");

    /* The control: IRXLOAD's stdio reader, linked into this program. */
    struct instblk *ref = NULL;
    CHECK(load_direct(IRXLOAD_FC_LOAD, &eb, &ref, env) == IRXLOAD_OK &&
              ref != NULL,
          "control: stdio reader loads TLDTSOX");
    CHECK(ref != NULL && n_lines(ref) == FIXTURE_LINES,
          "control: all 49 records, including the short second block");

    if (exte != NULL && mnt != NULL && ref != NULL)
    {
        if (expect_tso)
        {
            CHECK(memcmp(mnt->modnamet_exrout, "IRXLDTSO", 8) == 0,
                  "TSO: MODNAMET still names IRXLDTSO (so the LOAD worked)");
            CHECK(exte->load_routine != NULL,
                  "TSO: load_routine is wired");

            struct instblk *got = NULL;
            int rc = (exte->load_routine != NULL)
                         ? load_via(exte->load_routine, IRXLOAD_FC_LOAD, &eb,
                                    &got, env)
                         : -1;
            CHECK(rc == IRXLOAD_OK && got != NULL,
                  "TSO: LOAD through load_routine returns an INSTBLK");
            if (got != NULL)
            {
                CHECK(n_lines(got) == FIXTURE_LINES,
                      "TSO: BPAM reader delivers all 49 records");
                CHECK(same_content(got, ref),
                      "TSO: every line identical to the stdio reader's");
                CHECK(memcmp(got->instblk_ddname, "SYSEXEC ", 8) == 0,
                      "TSO: instblk_ddname names the DD that answered");
                CHECK(load_via(exte->load_routine, IRXLOAD_FC_FREE, &eb, &got,
                               env) == IRXLOAD_OK &&
                          got == NULL,
                      "TSO: FREE through load_routine clears the pointer");
            }

            struct execblk none;
            make_execblk(&none, "NOSUCHEX", "        ");
            struct instblk *nf = NULL;
            CHECK(exte->load_routine != NULL &&
                      load_via(exte->load_routine, IRXLOAD_FC_LOAD, &none,
                               &nf, env) == IRXLOAD_NOTFOUND &&
                      nf == NULL,
                  "TSO: a missing member is NOTFOUND, not an error");
        }
        else
        {
            CHECK(memcmp(mnt->modnamet_exrout, "        ", 8) == 0,
                  "batch: MODNAMET EXROUT slot is blank");
            CHECK(exte->load_routine == NULL,
                  "batch: no load_routine loaded, IRXLOAD is used directly");
        }
    }

    if (ref != NULL)
    {
        CHECK(load_direct(IRXLOAD_FC_FREE, &eb, &ref, env) == IRXLOAD_OK,
              "control: FREE");
    }

    /* #231 on real members: TLDNUM is TLDPLN with columns 73-80
     * numbered. Both readers must hand back exactly TLDPLN. */
    struct execblk ebn;
    struct execblk ebp;
    make_execblk(&ebn, "TLDNUM  ", "        ");
    make_execblk(&ebp, "TLDPLN  ", "        ");
    struct instblk *plain = NULL;
    struct instblk *num = NULL;
    CHECK(load_direct(IRXLOAD_FC_LOAD, &ebp, &plain, env) == IRXLOAD_OK &&
              load_direct(IRXLOAD_FC_LOAD, &ebn, &num, env) == IRXLOAD_OK &&
              plain != NULL && num != NULL,
          "#231: stdio reader loads TLDNUM and TLDPLN");
    if (plain != NULL && num != NULL)
    {
        CHECK(same_content(num, plain),
              "#231 stdio: numbered member reads as its unnumbered twin");
        if (expect_tso && exte != NULL && exte->load_routine != NULL)
        {
            struct instblk *bnum = NULL;
            CHECK(load_via(exte->load_routine, IRXLOAD_FC_LOAD, &ebn, &bnum,
                           env) == IRXLOAD_OK &&
                      bnum != NULL && same_content(bnum, plain),
                  "#231 BPAM: numbered member reads as its unnumbered twin");
            if (bnum != NULL)
            {
                load_via(exte->load_routine, IRXLOAD_FC_FREE, &ebn, &bnum, env);
            }
        }
    }
    if (num != NULL)
    {
        load_direct(IRXLOAD_FC_FREE, &ebn, &num, env);
    }
    if (plain != NULL)
    {
        load_direct(IRXLOAD_FC_FREE, &ebp, &plain, env);
    }
#else
    (void)argv;
    printf("  (host build: EXROUT wiring and BPAM reader are MVS-only)\n");
#endif

    CHECK(irxterm(env) == 0, "irxterm OK (releases the loaded routine)");

    printf("\n=== %d/%d passed (%d failed) ===\n", tests_passed, tests_run,
           tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
