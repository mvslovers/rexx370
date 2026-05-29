/* ------------------------------------------------------------------ */
/*  tstbcrxc.c - WP-BC-06 REXXCPS bytecode path driver                */
/*                                                                     */
/*  Verifies that the REXXCPS-derived kernel executes correctly via    */
/*  the bytecode path and produces output identical to the token-walk  */
/*  path.  Intended to be run as a standalone load module on MVS to   */
/*  measure wall-clock time with a larger iteration count (set count   */
/*  to 100+ on MVS before timing).                                     */
/*                                                                     */
/*  Cross-compile build (Linux/gcc):                                   */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                   */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c  */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c  */
/*          ../lstring370/src/lstr#xlt.c"                              */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                  */
/*        -o /tmp/tstbcrxc test/mvs/tstbcrxc.c \                       */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \            */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \            */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \            */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \            */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \            */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \           */
/*        src/irx#bcom.c  src/irx#bvm.c   $LSRC && /tmp/tstbcrxc      */
/*                                                                     */
/*  MVS usage (JCL):                                                   */
/*    //TSTBCRXC EXEC PGM=TSTBCRXC                                     */
/*                                                                     */
/*  (c) 2026 mvslovers - REXX/370 Project                              */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxwkblk.h"

#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

/* ------------------------------------------------------------------ */
/*  Test counters                                                       */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/*  Output capture                                                     */
/* ------------------------------------------------------------------ */

#define CAPBUF 4096

static char g_cap[CAPBUF];
static int g_cap_len;

static void cap_reset(void)
{
    g_cap_len = 0;
    g_cap[0] = '\0';
}

static int capture_io(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;
    if (function == RXFWRITE && data != NULL && Lpstr(data) != NULL)
    {
        int n = (int)Llen(data);
        if (g_cap_len + n + 1 < CAPBUF)
        {
            memcpy(g_cap + g_cap_len, Lpstr(data), (size_t)n);
            g_cap_len += n;
            g_cap[g_cap_len++] = '\n';
            g_cap[g_cap_len] = '\0';
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  REXXCPS-derived inner kernel                                       */
/*                                                                     */
/*  Stripped of timing BIFs, SIGNAL ON, TRACE, and ADDRESS.           */
/*  Uses only constructs supported by the current bytecode compiler:   */
/*  compound variables, SELECT/WHEN, PARSE VALUE/VAR/UPPER ARG,       */
/*  CALL/RETURN, BIF calls (substr, length, word).                     */
/*                                                                     */
/*  count = 5 is used for the correctness test (fast).                 */
/*  On MVS, rebuild with a higher count (e.g. 100) to time the path.  */
/* ------------------------------------------------------------------ */
static const char KERNEL_SMALL[] =
    "count = 5\n"
    "do i = 1 to count\n"
    "  flag = 0\n"
    "  do loop = 1 to 14\n"
    "    key1 = 'Key Bee'\n"
    "    acompound.key1.loop = substr(12345678, 6, 2)\n"
    "    if flag = acompound.key1.loop then say 'Failed1'\n"
    "    do j = 1 to 2\n"
    "      if j > acompound.key1.loop then say 'Failed2'\n"
    "      if 17 < length(j) - 1 then say 'Failed3'\n"
    "      if j = 'foobar' then say 'Failed4'\n"
    "      if substr(1234, 1, 1) = 9 then say 'Failed5'\n"
    "      if word(key1, 1) = '?' then say 'Failed6'\n"
    "      if j < 5 then do\n"
    "        acompound.key1.loop = acompound.key1.loop + 1\n"
    "        if j = 2 then leave\n"
    "      end\n"
    "      iterate\n"
    "    end\n"
    "    avar. = 1\n"
    "    select\n"
    "      when flag = 'string' then say 'FailedS1'\n"
    "      when avar.flag.2 = 0 then say 'FailedS2'\n"
    "      when flag = 5 + 99 then say 'FailedS3'\n"
    "      when flag then avar.1.2 = avar.1.2 * 1.1\n"
    "      when flag == 0 then flag = 0\n"
    "    end\n"
    "    if 1 then flag = 1\n"
    "    select\n"
    "      when flag == 'ring' then say 'FailedT1'\n"
    "      when avar.flag.3 = 0 then say 'FailedT2'\n"
    "      when flag then avar.1.2 = avar.1.2 * 1.1\n"
    "      when flag == 0 then flag = 1\n"
    "    end\n"
    "    parse value 'Foo Bar' with v1 +5 v2 .\n"
    "    call subroutine 'with' 2 'args'\n"
    "    parse value 'This is an awfully boring program' with p1 p2 p3 p4 p5\n"
    "  end loop\n"
    "end\n"
    "say count\n"
    "exit\n"
    "subroutine:\n"
    "  parse upper arg a1 a2 a3 .\n"
    "  parse var a3 b1 b2 b3 .\n"
    "  do 1\n"
    "    rc = a1 a2 a3\n"
    "    parse var rc c1 c2 c3\n"
    "  end\n"
    "  return\n";

/* ------------------------------------------------------------------ */
/*  Equivalence helper: token-walk vs bytecode                         */
/* ------------------------------------------------------------------ */

static void test_equiv(struct envblock *env)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(KERNEL_SMALL);
    int rc;
    int exit_rc = 0;
    char tw_out[CAPBUF];
    int tw_len;

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        CHECK(0, "work block available");
        return;
    }

    /* Token-walk run */
    cap_reset();
    wk->wkbi_use_bytecode = 0;
    rc = irx_exec_run(KERNEL_SMALL, src_len, NULL, 0, &exit_rc, env);
    memcpy(tw_out, g_cap, (size_t)(g_cap_len + 1));
    tw_len = g_cap_len;
    CHECK(rc == 0, "token-walk run succeeds");
    CHECK(exit_rc == 0, "token-walk exit rc == 0");
    CHECK(tw_len > 0, "token-walk produces output");

    /* Bytecode run */
    cap_reset();
    wk->wkbi_use_bytecode = 1;
    rc = irx_exec_run(KERNEL_SMALL, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;
    CHECK(rc == 0, "bytecode run succeeds");
    CHECK(exit_rc == 0, "bytecode exit rc == 0");
    CHECK(strcmp(tw_out, g_cap) == 0, "bytecode output matches token-walk");

    if (strcmp(tw_out, g_cap) != 0)
    {
        printf("    token-walk: [%s]\n", tw_out);
        printf("    bytecode:   [%s]\n", g_cap);
    }
}

/* ------------------------------------------------------------------ */
/*  BC-path proof (AC #6, WP-BC-09)                                    */
/*                                                                     */
/*  Runs REXXCPS-style source with the indirect pattern (var) through  */
/*  the bytecode path and verifies: wkbi_bc_exec_count > 0 AND         */
/*  wkbi_bc_fallback_count == 0, proving no UNSUP fallback occurred.  */
/* ------------------------------------------------------------------ */
static const char BC_PATH_SRC[] =
    "sep = ' '\n"
    "rc = 'This is an awfully boring program'\n"
    "parse var rc p1 (sep) p5\n"
    "say p1\n"
    "sep2 = 'b'\n"
    "parse var rc q1 (sep2) q5\n"
    "say q1\n"
    "say q5\n";

static void test_bc_path(struct envblock *env)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(BC_PATH_SRC);
    int bc_rc;
    int exit_rc = 0;

    printf("\n[BC-path proof — REXXCPS indirect pattern (AC #6)]\n");

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        CHECK(0, "work block available");
        return;
    }

    wk->wkbi_bc_exec_count = 0;
    wk->wkbi_bc_fallback_count = 0;

    cap_reset();
    wk->wkbi_use_bytecode = 1;
    bc_rc = irx_exec_run(BC_PATH_SRC, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;

    CHECK(bc_rc == 0, "REXXCPS indirect pattern: BC run succeeds");
    CHECK(exit_rc == 0, "REXXCPS indirect pattern: exit rc == 0");
    CHECK(wk->wkbi_bc_exec_count > 0,
          "REXXCPS indirect pattern: BC exec path taken");
    CHECK(wk->wkbi_bc_fallback_count == 0,
          "REXXCPS indirect pattern: no UNSUP fallback");
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-06: REXXCPS kernel token-walk vs bytecode ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbcrxc: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_equiv(env);
    test_bc_path(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
