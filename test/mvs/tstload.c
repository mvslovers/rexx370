/* ------------------------------------------------------------------ */
/*  tstload.c - WP-CPS-07 IRXLOAD C-core unit tests                  */
/*                                                                    */
/*  Tests irx_load_dispatch() directly (bypasses the asm IRXLOAD      */
/*  wrapper).  Host build uses the filesystem backend; MVS build       */
/*  tests the BSAM path with real DD allocations (AC-11).             */
/*                                                                    */
/*  Test cases:                                                        */
/*  T1: LOAD valid member (SYSEXEC) -> RC=0, valid INSTBLK            */
/*  T2: LOAD+FREE cycle -> INSTBLK ptr cleared, no leak               */
/*  T3: LOAD member-not-found -> RC=8                                 */
/*  T4: LOAD DD not set (no SYSEXEC/SYSPROC env var) -> RC=8         */
/*  T5: LOAD bad EXECBLK eye-catcher -> RC=20                         */
/*  T6: FREE bad INSTBLK eye-catcher -> RC=20                         */
/*  T7: FREE NULL instblk_p dereference -> RC=20                      */
/*  T8: LOAD via explicit DDNAME in EXECBLK -> RC=0                  */
/*  T9: LOAD empty member (zero source lines) -> RC=0, usedlen=0     */
/*                                                                    */
/*  Host cross-compile:                                               */
/*    gcc -I include -Wall -Wextra -std=gnu99 \                       */
/*        -o /tmp/tstload test/mvs/tstload.c \                        */
/*        src/irx#load.c src/irx#stor.c                               */
/*                                                                    */
/*  MVS invocation:                                                   */
/*    TSO   :  CALL 'hlq.LOAD(TSTLOAD)'                               */
/*    Batch :  EXEC PGM=TSTLOAD                                       */
/*  Expected: CC=0                                                    */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxload.h"

/* ------------------------------------------------------------------
 * Test infrastructure
 * ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                       \
    do                                                         \
    {                                                          \
        tests_run++;                                           \
        if (cond)                                              \
        {                                                      \
            tests_passed++;                                    \
            printf("  PASS: %s\n", (msg));                     \
        }                                                      \
        else                                                   \
        {                                                      \
            tests_failed++;                                    \
            printf("  FAIL: %s (line %d)\n", (msg), __LINE__); \
        }                                                      \
    } while (0)

/* Build a minimal, valid EXECBLK.
 * member8: exactly 8 chars, blank-padded (e.g. "HELLO   ")
 * ddname8: exactly 8 chars, blank-padded, or all-blank for auto-search */
static void make_execblk(struct execblk *e,
                         const char *member8,
                         const char *ddname8)
{
    memset(e, 0, sizeof(*e));
    memcpy(e->exec_blk_acryn, EXECBLK_ID, sizeof(e->exec_blk_acryn));
    e->exec_blk_length = EXECBLK_V1_LEN;
    memcpy(e->exec_member, member8, sizeof(e->exec_member));
    memcpy(e->exec_ddname, ddname8, sizeof(e->exec_ddname));
}

/* ------------------------------------------------------------------
 * Host-only test helpers
 * (on MVS these are replaced by real JCL DD setup)
 * ------------------------------------------------------------------ */
#ifndef __MVS__

#include <sys/stat.h>

static char s_sysexec_dir[256];
static char s_altdd_dir[256];

/* Create a directory and one or more .rex files inside it. */
static void write_test_file(const char *dir, const char *member,
                            const char *content)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s.rex", dir, member);
    f = fopen(path, "w");
    if (f)
    {
        fputs(content, f);
        fclose(f);
    }
}

/* Set up temp directories and test .rex files.
 * Returns 0 on success. */
static int setup_test_dirs(void)
{
    snprintf(s_sysexec_dir, sizeof(s_sysexec_dir), "/tmp/tstload_sysexec");
    snprintf(s_altdd_dir, sizeof(s_altdd_dir), "/tmp/tstload_altdd");

    mkdir(s_sysexec_dir, 0755);
    mkdir(s_altdd_dir, 0755);

    /* T1 / T2 / T9: member HELLO in SYSEXEC */
    write_test_file(s_sysexec_dir, "HELLO",
                    "/* test REXX exec */\n"
                    "say 'hello'\n"
                    "exit 0\n");

    /* T8: member ALTM in ALTDD (separate directory) */
    write_test_file(s_altdd_dir, "ALTM",
                    "/* alt dd exec */\n"
                    "exit 0\n");

    /* T9: empty member (zero lines) */
    write_test_file(s_sysexec_dir, "EMPTY", "");

    /* Set env vars so irx#load.c's getenv() path finds the dirs. */
    setenv("SYSEXEC", s_sysexec_dir, 1);
    setenv("ALTDD", s_altdd_dir, 1);

    /* Unset SYSPROC so T4 can test "both DDs absent". */
    unsetenv("SYSPROC");

    return 0;
}

#endif /* !__MVS__ */

/* ================================================================== */
/*  Tests                                                             */
/* ================================================================== */

static void test_load_valid(void)
{
    struct execblk eb;
    struct instblk *ib = NULL;
    struct instblk_entry *ent;
    int retval = -1;
    int rc;

    printf("T1: LOAD valid member\n");
    make_execblk(&eb, "HELLO   ", "        ");

    rc = irx_load_dispatch(IRXLOAD_FC_LOAD, &eb, &ib, NULL, &retval);

    CHECK(rc == IRXLOAD_OK, "LOAD returns RC=0");
    CHECK(retval == IRXLOAD_OK, "retval written to 0");
    CHECK(ib != NULL, "INSTBLK pointer non-NULL");

    if (!ib)
    {
        return;
    }

    CHECK(memcmp(ib->instblk_acronym, INSTBLK_ID,
                 sizeof(ib->instblk_acronym)) == 0,
          "INSTBLK_ACRONYM = 'IRXINSTB'");
    CHECK(ib->instblk_hdrlen == INSTBLK_HDRLEN, "instblk_hdrlen = 128");
    CHECK(ib->instblk_address != NULL, "instblk_address non-NULL");
    CHECK(ib->instblk_usedlen >= 0, "instblk_usedlen non-negative");
    CHECK((ib->instblk_usedlen % (int)sizeof(struct instblk_entry)) == 0,
          "instblk_usedlen multiple of entry size");

    /* Verify entries point into valid memory (access first entry). */
    if (ib->instblk_usedlen >= (int)sizeof(struct instblk_entry))
    {
        ent = (struct instblk_entry *)ib->instblk_address;
        CHECK(ent->instblk_stmtlen >= 0, "first entry length >= 0");
    }

    /* Leave ib for T2. */
}

static void test_load_free_cycle(void)
{
    struct execblk eb;
    struct instblk *ib = NULL;
    int retval = -1;
    int rc;

    printf("T2: LOAD + FREE cycle\n");
    make_execblk(&eb, "HELLO   ", "        ");

    rc = irx_load_dispatch(IRXLOAD_FC_LOAD, &eb, &ib, NULL, &retval);
    CHECK(rc == IRXLOAD_OK && ib != NULL, "LOAD succeeds");

    if (!ib)
    {
        return;
    }

    rc = irx_load_dispatch(IRXLOAD_FC_FREE, NULL, &ib, NULL, &retval);
    CHECK(rc == IRXLOAD_OK, "FREE returns RC=0");
    CHECK(ib == NULL, "FREE clears caller pointer");
    CHECK(retval == IRXLOAD_OK, "retval written to 0 by FREE");
}

static void test_member_not_found(void)
{
    struct execblk eb;
    struct instblk *ib = NULL;
    int retval = -1;
    int rc;

    printf("T3: LOAD member not found\n");
    make_execblk(&eb, "NOSUCHM ", "        ");

    rc = irx_load_dispatch(IRXLOAD_FC_LOAD, &eb, &ib, NULL, &retval);
    CHECK(rc == IRXLOAD_NOTFOUND, "LOAD returns RC=8");
    CHECK(ib == NULL, "INSTBLK pointer stays NULL");
}

static void test_dd_not_set(void)
{
    struct execblk eb;
    struct instblk *ib = NULL;
    int retval = -1;
    int rc;

    printf("T4: LOAD DD not allocated\n");
    /* Use a DDNAME that is not in the environment. */
    make_execblk(&eb, "HELLO   ", "NOSUCHDD");

    rc = irx_load_dispatch(IRXLOAD_FC_LOAD, &eb, &ib, NULL, &retval);
    CHECK(rc == IRXLOAD_NOTFOUND, "LOAD returns RC=8 when DD absent");
    CHECK(ib == NULL, "INSTBLK pointer stays NULL");
}

static void test_bad_execblk(void)
{
    struct execblk eb;
    struct instblk *ib = NULL;
    int retval = -1;
    int rc;

    printf("T5: LOAD bad EXECBLK eye-catcher\n");
    make_execblk(&eb, "HELLO   ", "        ");
    memcpy(eb.exec_blk_acryn, "BADBLOCK", sizeof(eb.exec_blk_acryn));

    rc = irx_load_dispatch(IRXLOAD_FC_LOAD, &eb, &ib, NULL, &retval);
    CHECK(rc == IRXLOAD_ERROR, "LOAD returns RC=20 for bad eye-catcher");
    CHECK(ib == NULL, "INSTBLK pointer stays NULL");
}

static void test_free_bad_eyecatcher(void)
{
    struct instblk fake;
    struct instblk *p = &fake;
    int retval = -1;
    int rc;

    printf("T6: FREE bad INSTBLK eye-catcher\n");
    memset(&fake, 0, sizeof(fake));
    memcpy(fake.instblk_acronym, "BADBLOCK", sizeof(fake.instblk_acronym));

    rc = irx_load_dispatch(IRXLOAD_FC_FREE, NULL, &p, NULL, &retval);
    CHECK(rc == IRXLOAD_ERROR, "FREE returns RC=20 for bad eye-catcher");
}

static void test_free_null_ptr(void)
{
    struct instblk *p = NULL;
    int retval = -1;
    int rc;

    printf("T7: FREE NULL instblk pointer\n");
    rc = irx_load_dispatch(IRXLOAD_FC_FREE, NULL, &p, NULL, &retval);
    CHECK(rc == IRXLOAD_ERROR, "FREE returns RC=20 for NULL pointer");
}

static void test_load_explicit_ddname(void)
{
    struct execblk eb;
    struct instblk *ib = NULL;
    int retval = -1;
    int rc;

    printf("T8: LOAD via explicit DDNAME in EXECBLK\n");
    make_execblk(&eb, "ALTM    ", "ALTDD   ");

    rc = irx_load_dispatch(IRXLOAD_FC_LOAD, &eb, &ib, NULL, &retval);
    CHECK(rc == IRXLOAD_OK, "LOAD via explicit DDNAME returns RC=0");
    CHECK(ib != NULL, "INSTBLK non-NULL");

    if (ib)
    {
        int free_rc = irx_load_dispatch(IRXLOAD_FC_FREE, NULL, &ib, NULL, &retval);
        CHECK(free_rc == IRXLOAD_OK, "FREE after explicit-DDNAME LOAD ok");
    }
}

static void test_load_empty_member(void)
{
    struct execblk eb;
    struct instblk *ib = NULL;
    int retval = -1;
    int rc;

    printf("T9: LOAD empty member\n");
    make_execblk(&eb, "EMPTY   ", "        ");

    rc = irx_load_dispatch(IRXLOAD_FC_LOAD, &eb, &ib, NULL, &retval);
    CHECK(rc == IRXLOAD_OK, "LOAD empty member returns RC=0");

    if (ib)
    {
        CHECK(ib->instblk_usedlen == 0, "instblk_usedlen = 0 for empty exec");
        {
            int free_rc = irx_load_dispatch(IRXLOAD_FC_FREE, NULL, &ib, NULL, &retval);
            CHECK(free_rc == IRXLOAD_OK, "FREE empty exec ok");
        }
    }
}

/* ================================================================== */
/*  main                                                              */
/* ================================================================== */
int main(void)
{
    printf("TSTLOAD: IRXLOAD C-core tests\n");
    printf("------------------------------\n");

#ifndef __MVS__
    if (setup_test_dirs() != 0)
    {
        printf("FATAL: could not set up test directories\n");
        return 1;
    }
#endif

    test_load_valid();
    test_load_free_cycle();
    test_member_not_found();
    test_dd_not_set();
    test_bad_execblk();
    test_free_bad_eyecatcher();
    test_free_null_ptr();
    test_load_explicit_ddname();
    test_load_empty_member();

    printf("------------------------------\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
