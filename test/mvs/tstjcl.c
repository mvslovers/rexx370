/* ------------------------------------------------------------------ */
/*  tstjcl.c - WP-CPS-08 IRXJCL Batch Entry Point unit tests         */
/*                                                                    */
/*  Tests irx_jcl_dispatch() directly (bypasses the asm IRXJCL        */
/*  wrapper).  Host build uses the filesystem backend; MVS build       */
/*  exercises the real IRXLOAD + IRXEXEC path.                        */
/*                                                                    */
/*  Test cases:                                                        */
/*  T1:  NULL parm_buffer         -> IRXJCL_BADPARM (24)             */
/*  T2:  zero-length PARM         -> IRXJCL_BADPARM (24)             */
/*  T3:  sequential-mode marker   -> IRXJCL_BADPARM (24)             */
/*  T4:  member name > 8 chars    -> IRXJCL_BADPARM (24)             */
/*  T5:  leading space (no name)  -> IRXJCL_BADPARM (24)             */
/*  T6:  valid member, no args    -> IRXJCL_OK (0)                   */
/*  T7:  valid member, arg string -> IRXJCL_OK (0)                   */
/*  T8:  member not found         -> IRXJCL_ERROR (20)               */
/*  T9:  lowercase member name    -> uppercased, found, IRXJCL_OK    */
/*  T10: explicit envblock_r0     -> used directly, IRXJCL_OK        */
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

/* Maximum PARM buffer size used in this test file. */
#define PARM_BUF_MAX 128

/* Bit-shift for the high byte of a big-endian halfword. */
#define PARM_LEN_SHIFT 8
/* Mask to extract the low byte. */
#define PARM_LOW_BYTE ((size_t)0xFFU)

/* Build a PARM buffer from raw bytes.  Same format as make_parm but
 * accepts pre-built byte data (e.g. data containing embedded 0x00). */
static void make_parm_bytes(unsigned char *buf,
                            const unsigned char *data, size_t len)
{
    size_t i;
    buf[0] = (unsigned char)((len >> PARM_LEN_SHIFT) & PARM_LOW_BYTE);
    buf[1] = (unsigned char)(len & PARM_LOW_BYTE);
    for (i = 0; i < len; i++)
    {
        buf[2 + i] = data[i];
    }
}

/* Build a PARM buffer from a C string (convenience wrapper). */
static void make_parm(unsigned char *buf, const char *data)
{
    make_parm_bytes(buf, (const unsigned char *)data, strlen(data));
}

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

    /* T10: member ENVTEST — used with explicit envblock_r0 */
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

static void test_null_parm(void)
{
    printf("T1: NULL parm_buffer -> BADPARM\n");
    int rc = irx_jcl_dispatch(NULL, NULL);
    CHECK(rc == IRXJCL_BADPARM, "NULL parm_buffer returns IRXJCL_BADPARM");
}

static void test_zero_length_parm(void)
{
    unsigned char buf[PARM_BUF_MAX];
    int rc;

    printf("T2: zero-length PARM -> BADPARM\n");
    make_parm(buf, "");
    rc = irx_jcl_dispatch(buf, NULL);
    CHECK(rc == IRXJCL_BADPARM, "zero-length PARM returns IRXJCL_BADPARM");
}

static void test_sequential_mode(void)
{
    /* Sequential mode: first data byte == 0x00.
     * Build the PARM buffer via make_parm_bytes because make_parm takes
     * a C string (which cannot embed 0x00 as non-terminal data). */
    static const unsigned char seq_data[] = {0x00, 'A', 'B'};
    unsigned char buf[PARM_BUF_MAX];
    int rc;

    printf("T3: sequential-mode marker -> BADPARM\n");
    make_parm_bytes(buf, seq_data, sizeof(seq_data));
    rc = irx_jcl_dispatch(buf, NULL);
    CHECK(rc == IRXJCL_BADPARM, "sequential-mode PARM returns IRXJCL_BADPARM");
}

static void test_member_too_long(void)
{
    unsigned char buf[PARM_BUF_MAX];
    int rc;

    printf("T4: member name > 8 chars -> BADPARM\n");
    /* 9 non-space chars before any space */
    make_parm(buf, "TOOLONGMN");
    rc = irx_jcl_dispatch(buf, NULL);
    CHECK(rc == IRXJCL_BADPARM, "9-char member returns IRXJCL_BADPARM");
}

static void test_leading_space(void)
{
    unsigned char buf[PARM_BUF_MAX];
    int rc;

    printf("T5: leading space (no member name) -> BADPARM\n");
    make_parm(buf, " HELLO");
    rc = irx_jcl_dispatch(buf, NULL);
    CHECK(rc == IRXJCL_BADPARM, "leading-space PARM returns IRXJCL_BADPARM");
}

static void test_valid_no_args(void)
{
    unsigned char buf[PARM_BUF_MAX];
    int rc;

    printf("T6: valid member, no args -> IRXJCL_OK\n");
    make_parm(buf, "HELLO");
    rc = irx_jcl_dispatch(buf, NULL);
    CHECK(rc == IRXJCL_OK, "valid member returns IRXJCL_OK");
}

static void test_valid_with_args(void)
{
    unsigned char buf[PARM_BUF_MAX];
    int rc;

    printf("T7: valid member, arg string -> IRXJCL_OK\n");
    make_parm(buf, "WITHARG some test args");
    rc = irx_jcl_dispatch(buf, NULL);
    CHECK(rc == IRXJCL_OK, "member with args returns IRXJCL_OK");
}

static void test_member_not_found(void)
{
    unsigned char buf[PARM_BUF_MAX];
    int rc;

    printf("T8: member not found -> IRXJCL_ERROR\n");
    make_parm(buf, "NOSUCHM");
    rc = irx_jcl_dispatch(buf, NULL);
    CHECK(rc == IRXJCL_ERROR, "missing member returns IRXJCL_ERROR");
}

static void test_lowercase_member(void)
{
    unsigned char buf[PARM_BUF_MAX];
    int rc;

    printf("T9: lowercase member name -> uppercased, found\n");
    /* "hello" should be uppercased to "HELLO" which exists */
    make_parm(buf, "hello");
    rc = irx_jcl_dispatch(buf, NULL);
    CHECK(rc == IRXJCL_OK, "lowercase member uppercased and found");
}

static void test_explicit_envblock(void)
{
    struct envblock *env = NULL;
    unsigned char buf[PARM_BUF_MAX];
    int rc;
    int init_rc;

    printf("T10: explicit envblock_r0 -> used directly\n");
    init_rc = irxinit(NULL, &env);
    if (init_rc != 0 || env == NULL)
    {
        CHECK(0, "irxinit succeeded (prerequisite)");
        return;
    }
    CHECK(1, "irxinit succeeded (prerequisite)");

    make_parm(buf, "ENVTEST");
    rc = irx_jcl_dispatch(buf, env);
    CHECK(rc == IRXJCL_OK, "explicit envblock_r0 accepted and exec runs");

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

    test_null_parm();
    test_zero_length_parm();
    test_sequential_mode();
    test_member_too_long();
    test_leading_space();
    test_valid_no_args();
    test_valid_with_args();
    test_member_not_found();
    test_lowercase_member();
    test_explicit_envblock();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return (tests_failed > 0) ? 1 : 0;
}
