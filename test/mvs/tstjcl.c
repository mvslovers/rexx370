/* ------------------------------------------------------------------ */
/*  tstjcl.c - WP-CPS-08 IRXJCL Batch Entry Point unit tests         */
/*                                                                    */
/*  Tests irx_jcl_dispatch_main() directly (bypasses the @@CRT0      */
/*  entry path and the argv reconstruction in irx#jclm.c).           */
/*  Host build uses the filesystem backend; MVS build exercises the  */
/*  real IRXLOAD + IRXEXEC path.                                      */
/*                                                                    */
/*  Test cases:                                                        */
/*  T1:  NULL member              -> IRXJCL_BADPARM (24)             */
/*  T2:  empty member string      -> IRXJCL_BADPARM (24)             */
/*  T3:  sequential-mode marker   -> IRXJCL_BADPARM (24)             */
/*  T4:  member name > 8 chars    -> IRXJCL_BADPARM (24)             */
/*  T5:  leading space (no name)  -> IRXJCL_BADPARM (24)             */
/*  T6:  valid member, no args    -> IRXJCL_OK (0)                   */
/*  T7:  valid member, arg string -> IRXJCL_OK (0)                   */
/*  T8:  member not found         -> IRXJCL_ERROR (20)               */
/*  T9:  lowercase member name    -> uppercased, found, IRXJCL_OK    */
/*  T10: pre-existing env via FINDENVB -> IRXJCL_OK                  */
/*                                                                    */
/*  Host cross-compile (from repo root):                              */
/*    LSTR=contrib/lstring370-0.1.0-dev                               */
/*    gcc -I include -I $LSTR/include -Wall -Wextra -std=gnu99 \      */
/*        -o /tmp/tstjcl test/mvs/tstjcl.c \                         */
/*        'src/irx#jcl.c' 'src/irx#load.c' \                         */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \       */
/*        'src/irx#anch.c' 'src/irx#env.c' \                         */
/*        'src/irx#uid.c' 'src/irx#msid.c' \                         */
/*        'src/irx#cond.c' 'src/irx#bif.c' 'src/irx#bifs.c' \        */
/*        'src/irx#io.c' 'src/irx#lstr.c' 'src/irx#tokn.c' \         */
/*        'src/irx#vpol.c' 'src/irx#pars.c' 'src/irx#ctrl.c' \       */
/*        'src/irx#exec.c' 'src/irx#arith.c' \                       */
/*        $LSTR/lib/liblstring.a && /tmp/tstjcl                       */
/*                                                                    */
/*  MVS invocation:                                                   */
/*    TSO   :  CALL 'hlq.LOAD(TSTJCL)'                                */
/*    Batch :  EXEC PGM=TSTJCL                                        */
/*  Expected: CC=0                                                    */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxjcl.h"

/* ------------------------------------------------------------------
 * Test infrastructure
 * ------------------------------------------------------------------ */

/* Simulates the MVS ECT ECTENVBK slot on the host (test-only global;
 * never used in production code, guarded by the CLAUDE.md rule). */
void *_simulated_ectenvbk = NULL;

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

/* ------------------------------------------------------------------
 * Host-only test helpers
 * ------------------------------------------------------------------ */
#ifndef __MVS__

#include <sys/stat.h>

static char s_sysexec_dir[256];

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

static int setup_test_dirs(void)
{
    snprintf(s_sysexec_dir, sizeof(s_sysexec_dir), "/tmp/tstjcl_sysexec");
    mkdir(s_sysexec_dir, 0755);

    /* T6, T9: member HELLO — simple exec that exits 0 */
    write_test_file(s_sysexec_dir, "HELLO",
                    "/* hello */\n"
                    "exit 0\n");

    /* T7: member WITHARG — runs and exits 0 regardless of args */
    write_test_file(s_sysexec_dir, "WITHARG",
                    "/* witharg */\n"
                    "exit 0\n");

    /* T10: member ENVTEST — used with FINDENVB pre-seeded env */
    write_test_file(s_sysexec_dir, "ENVTEST",
                    "/* envtest */\n"
                    "exit 0\n");

    setenv("SYSEXEC", s_sysexec_dir, 1);
    unsetenv("SYSPROC");

    return 0;
}

#endif /* !__MVS__ */

/* ================================================================== */
/*  Tests                                                             */
/* ================================================================== */

static void test_null_member(void)
{
    printf("T1: NULL member -> BADPARM\n");
    int rc = irx_jcl_dispatch_main(NULL, NULL, 0);
    CHECK(rc == IRXJCL_BADPARM, "NULL member returns IRXJCL_BADPARM");
}

static void test_empty_member(void)
{
    printf("T2: empty member string -> BADPARM\n");
    int rc = irx_jcl_dispatch_main("", NULL, 0);
    CHECK(rc == IRXJCL_BADPARM, "empty member returns IRXJCL_BADPARM");
}

static void test_sequential_mode(void)
{
    /* A member whose first byte is 0x00 is the sequential-mode marker
     * from the binary PARM convention.  At the C level this is caught
     * by the empty/sequential-mode guard (member[0] == '\0').
     * The TODO(WP-CPS-08b) comment in the dispatcher preserves intent. */
    static const char seq_member[] = {'\0', 'A', 'B', '\0'};
    int rc;

    printf("T3: sequential-mode marker -> BADPARM\n");
    rc = irx_jcl_dispatch_main(seq_member, NULL, 0);
    CHECK(rc == IRXJCL_BADPARM, "sequential-mode marker returns IRXJCL_BADPARM");
}

static void test_member_too_long(void)
{
    printf("T4: member name > 8 chars -> BADPARM\n");
    int rc = irx_jcl_dispatch_main("TOOLONGMN", NULL, 0);
    CHECK(rc == IRXJCL_BADPARM, "9-char member returns IRXJCL_BADPARM");
}

static void test_leading_space(void)
{
    printf("T5: leading space (no member name) -> BADPARM\n");
    int rc = irx_jcl_dispatch_main(" HELLO", NULL, 0);
    CHECK(rc == IRXJCL_BADPARM, "leading-space member returns IRXJCL_BADPARM");
}

static void test_valid_no_args(void)
{
    printf("T6: valid member, no args -> IRXJCL_OK\n");
    int rc = irx_jcl_dispatch_main("HELLO", NULL, 0);
    CHECK(rc == IRXJCL_OK, "valid member returns IRXJCL_OK");
}

static void test_valid_with_args(void)
{
    static const char arg[] = "some test args";

    printf("T7: valid member, arg string -> IRXJCL_OK\n");
    int rc = irx_jcl_dispatch_main("WITHARG", arg, (int)strlen(arg));
    CHECK(rc == IRXJCL_OK, "member with args returns IRXJCL_OK");
}

static void test_member_not_found(void)
{
    printf("T8: member not found -> IRXJCL_ERROR\n");
    int rc = irx_jcl_dispatch_main("NOSUCHM", NULL, 0);
    CHECK(rc == IRXJCL_ERROR, "missing member returns IRXJCL_ERROR");
}

static void test_lowercase_member(void)
{
    printf("T9: lowercase member name -> uppercased, found\n");
    /* "hello" should be uppercased to "HELLO" which exists */
    int rc = irx_jcl_dispatch_main("hello", NULL, 0);
    CHECK(rc == IRXJCL_OK, "lowercase member uppercased and found");
}

static void test_findenvb_env(void)
{
    /* irxinit registers the new env in the anchor table.
     * irx_jcl_dispatch_main's FINDENVB path finds it (own_env=0),
     * runs ENVTEST, and returns without calling irxterm.
     * We clean up via irxterm after the dispatch returns. */
    struct envblock *env = NULL;
    int rc;
    int init_rc;

    printf("T10: pre-existing env via FINDENVB -> IRXJCL_OK\n");
    init_rc = irxinit(NULL, &env);
    if (init_rc != 0 || env == NULL)
    {
        CHECK(0, "irxinit succeeded (prerequisite)");
        return;
    }
    CHECK(1, "irxinit succeeded (prerequisite)");

    rc = irx_jcl_dispatch_main("ENVTEST", NULL, 0);
    CHECK(rc == IRXJCL_OK, "FINDENVB path used, exec runs");

    irxterm(env);
}

/* ================================================================== */
/*  main                                                              */
/* ================================================================== */

int main(void)
{
#ifndef __MVS__
    if (setup_test_dirs() != 0)
    {
        printf("FATAL: setup_test_dirs failed\n");
        return 1;
    }
#endif

    printf("=== TSTJCL: IRXJCL Batch Entry Point tests ===\n");

    test_null_member();
    test_empty_member();
    test_sequential_mode();
    test_member_too_long();
    test_leading_space();
    test_valid_no_args();
    test_valid_with_args();
    test_member_not_found();
    test_lowercase_member();
    test_findenvb_env();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return (tests_failed > 0) ? 1 : 0;
}
