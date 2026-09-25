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
#else
    (void)argv;
    printf("  (host build: EXROUT wiring and BPAM reader are MVS-only)\n");
#endif

    CHECK(irxterm(env) == 0, "irxterm OK (releases the loaded routine)");

    printf("\n=== %d/%d passed (%d failed) ===\n", tests_passed, tests_run,
           tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
