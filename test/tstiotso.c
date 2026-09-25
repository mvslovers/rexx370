/* ------------------------------------------------------------------ */
/*  tstiotso.c - WP-33-TSO: the I/O routine is chosen per environment  */
/*                                                                    */
/*  This test only means something on MVS, and it means two different  */
/*  things depending on which leg runs it.  mbt runs every test twice: */
/*  a plain batch step and a step under IKJEFT01, and passes the       */
/*  expectation as PARM (parm_batch = "0", parm_tso = "1") -- the same */
/*  arrangement TISTSO uses.                                           */
/*                                                                    */
/*    TSO leg    is_tso()=1 -> IRXTSPRM -> MODNAMET names IRXIOTSO     */
/*               -> IRXINIT LOADs it -> io_routine is NOT irxinout     */
/*    batch leg  is_tso()=0 -> IRXPARMS -> slot blank                  */
/*               -> io_routine IS irxinout (stdio, as before)          */
/*                                                                    */
/*  Why assert the POINTER and not the output: on MVS the TSO routine  */
/*  ends in PUTLINE and there is nothing to read back from inside the  */
/*  program.  What can go wrong silently is the wiring -- a MODNAMET   */
/*  that never reached the ENVBLOCK, a LOAD that failed and fell back, */
/*  an 8-character name collision resolving both branches to one       */
/*  CSECT.  Each of those leaves every return code at zero and every   */
/*  SAY apparently working, and each is caught here.                   */
/*                                                                    */
/*  The SAY output itself is the human-readable half: on the TSO leg   */
/*  it reaches SYSTSPRT through PUTLINE, on the batch leg through      */
/*  stdio.  THAT half is not asserted here -- the program cannot read  */
/*  its own SYSTSPRT -- so read the spool: every return code stays     */
/*  zero when the output goes nowhere (TPUT did exactly that).  Three  */
/*  shapes, because they fail differently: an ordinary line, an empty  */
/*  SAY (must still produce a line) and one longer than a screen.      */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxio.h"
#include "irxwkblk.h"
#include "lstring.h"

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

/* All blanks means "no override, use the default". IRXINIT blanks the
 * slot when a named module fails to LOAD, so a blank slot on the TSO
 * leg is a real failure and not merely an absent name. */
static int slot_is_blank(const unsigned char slot[8])
{
    for (int i = 0; i < 8; i++)
    {
        if (slot[i] != ' ')
        {
            return 0;
        }
    }
    return 1;
}

static void run_say(struct envblock *env, const char *src, const char *what)
{
    int exit_rc = 0;
    int rc = irx_exec_run(src, (int)strlen(src), NULL, 0, &exit_rc, env);
    CHECK(rc == 0, what);
}

int main(int argc, char **argv)
{
    int expect_tso = (argc >= 2) ? atoi(argv[1]) : 0;

    printf("=== TSTIOTSO: I/O routine per environment (expect_tso=%d) ===\n",
           expect_tso);

    struct envblock *env = NULL;
    CHECK(irxinit(NULL, &env) == 0, "irxinit OK");
    if (env == NULL)
    {
        printf("\n=== %d/%d passed (%d failed) ===\n", tests_passed, tests_run,
               tests_failed);
        return 1;
    }

    struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
    struct parmblock *pb = (struct parmblock *)env->envblock_parmblock;
    CHECK(exte != NULL, "IRXEXTE present");
    CHECK(pb != NULL, "PARMBLOCK present");

    struct modnamet *mnt =
        (pb != NULL) ? (struct modnamet *)pb->parmblock_modnamet : NULL;

#ifdef __MVS__
    /* Before WP-33-TSO this pointer was never filled: the MODNAMET
     * lives inside the parm module, which IRXINIT deletes after reading
     * the byte values. A NULL here means the copy step is gone again.
     *
     * MVS only: the host has no parm module to read a MODNAMET from,
     * so NULL is the correct answer there and not a defect. */
    CHECK(mnt != NULL, "MODNAMET reachable from the PARMBLOCK copy");

    if (mnt != NULL && exte != NULL)
    {
        if (expect_tso)
        {
            CHECK(memcmp(mnt->modnamet_iorout, "IRXIOTSO", 8) == 0,
                  "TSO: MODNAMET still names IRXIOTSO (so the LOAD worked)");
            CHECK(exte->io_routine != (void *)irxinout,
                  "TSO: io_routine is the loaded routine, not the default");
            /* IRXEXTE carries an Active and a Default slot per routine.
             * An override replaces the ACTIVE one only -- irxinout must
             * still hand back the built-in, or a caller asking for the
             * default explicitly would silently get the TSO routine. */
            CHECK(exte->irxinout == (void *)irxinout,
                  "TSO: the default slot still points at the built-in");
            CHECK(exte->irxinout != exte->io_routine,
                  "TSO: active and default are distinct");
        }
        else
        {
            CHECK(slot_is_blank(mnt->modnamet_iorout),
                  "batch: MODNAMET IORT slot is blank");
            CHECK(exte->io_routine == (void *)irxinout,
                  "batch: io_routine is the default stdio routine");
            CHECK(exte->irxinout == (void *)irxinout,
                  "batch: active and default are the same routine");
        }
    }
#else
    (void)mnt;
    (void)slot_is_blank;
    printf("  (host build: MODNAMET + wiring assertions are MVS-only)\n");
#endif

    /* Now actually say something through whichever routine is wired.
     * Three shapes, because they fail in different ways. */
    run_say(env, "SAY 'TSTIOTSO: ordinary line'\n", "SAY ordinary line rc=0");
    run_say(env, "SAY\n", "SAY with no operand rc=0 (must still emit a line)");
    run_say(env, "SAY COPIES('LONG', 60)\n", "SAY 240-char line rc=0");

    /* IRXTERM must give the loaded routine back (DELETE). A failure
     * here is how a use-count leak would first show up. */
    CHECK(irxterm(env) == 0, "irxterm OK (releases the loaded routine)");

    printf("\n=== %d/%d passed (%d failed) ===\n", tests_passed, tests_run,
           tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
