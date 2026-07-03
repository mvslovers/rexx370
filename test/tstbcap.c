/* ------------------------------------------------------------------ */
/*  tstbcap.c - issue #212: bytecode fixed-table overflow falls back   */
/*                                                                    */
/*  The bytecode compiler keeps three FIXED tables (src/irx#bcom.c):   */
/*    BCOM_MAX_CODE 16384, BCOM_MAX_CONSTS 512, BCOM_MAX_SYMS 512.     */
/*  A program that overflows one of them cannot be represented as      */
/*  bytecode, but the token-walk interpreter has no such limits.  The  */
/*  overflow is raised inside irx_bc_compile() BEFORE any bytecode     */
/*  executes, so re-running under the interpreter is side-effect free  */
/*  — exactly like the existing UNSUP / STRTOOLONG / PARSE_COMPOUND    */
/*  fallback codes.                                                    */
/*                                                                    */
/*  This test verifies that a capacity overflow now returns the        */
/*  dedicated IRXBC_ERR_CAPACITY code (distinct from IRXBC_ERR_STOR,   */
/*  which stays for real irxstor failures) and that irx_exec_run       */
/*  falls back to the interpreter and runs the program correctly       */
/*  instead of aborting fatally:                                       */
/*    1. compile: >512 distinct constants  -> IRXBC_ERR_CAPACITY;      */
/*    2. compile: >512 distinct symbols    -> IRXBC_ERR_CAPACITY;      */
/*    3. compile: a small program          -> IRXBC_OK (no false       */
/*       positive at the boundary);                                    */
/*    4. run:     the overflowing program falls back (fallback>0) and  */
/*       produces the correct output with RC=0 (was fatal rc=20);      */
/*    5. run:     the small program stays on the VM (exec>0, no        */
/*       fallback) and produces the correct output.                    */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                   */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c  */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c  */
/*          ../lstring370/src/lstr#xlt.c"                              */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbcap test/tstbcap.c \                             */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \            */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \            */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \            */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \            */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \            */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \           */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c \            */
/*        $LSRC && /tmp/tstbcap                                       */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxbctl.h"
#include "irxbops.h"
#include "irxbvm.h"
#include "irxexbl.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxwkblk.h"

#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

/* Distinct-entry counts that straddle BCOM_MAX_CONSTS / BCOM_MAX_SYMS (512). */
#define OVER_LIMIT  700 /* comfortably over the 512-entry fixed tables    */
#define UNDER_LIMIT 100 /* comfortably under — must still compile to bc   */

#define SRCBUF_SIZE 8192

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
/*  Output capture                                                    */
/* ------------------------------------------------------------------ */

#define CAPBUF_SIZE 4096

static char g_cap[CAPBUF_SIZE];
static int g_cap_len = 0;

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
        if (g_cap_len + n + 1 < CAPBUF_SIZE)
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
/*  Program generators                                                */
/* ------------------------------------------------------------------ */

/* n assignments of DISTINCT string literals to one variable, then a
 * final SAY.  Each literal 'cI' is a distinct constant, so the const
 * table grows by one per line; the lone symbol 'v' never overflows.
 * The final value (and the SAY output) is "c<n-1>". */
static int gen_consts(char *buf, int cap, int n)
{
    int off = 0;
    for (int i = 0; i < n; i++)
    {
        off += snprintf(buf + off, (size_t)(cap - off), "v='c%d'\n", i);
    }
    off += snprintf(buf + off, (size_t)(cap - off), "say v\n");
    return off;
}

/* n assignments to DISTINCT variables, then a SAY of the first one.
 * Each name aI is a distinct symbol, so the symbol table grows by one
 * per line; the constant '1' is added once and deduped.  The SAY
 * output is "1". */
static int gen_syms(char *buf, int cap, int n)
{
    int off = 0;
    for (int i = 0; i < n; i++)
    {
        off += snprintf(buf + off, (size_t)(cap - off), "a%d=1\n", i);
    }
    off += snprintf(buf + off, (size_t)(cap - off), "say a0\n");
    return off;
}

/* ------------------------------------------------------------------ */
/*  Assertions                                                        */
/* ------------------------------------------------------------------ */

/* Direct compile: assert irx_bc_compile returns exactly expected_rc.
 * Frees any container it produced (only the IRXBC_OK path allocates). */
static void check_compile(struct envblock *env, const char *src, int len,
                          int expected_rc, const char *tag)
{
    struct irx_bc_execblk *bc = NULL;
    int rc = irx_bc_compile(env, src, len, &bc, NULL, NULL);
    char label[160];

    snprintf(label, sizeof(label), "compile: %s", tag);
    CHECK(rc == expected_rc, label);
    if (rc != expected_rc)
    {
        printf("    expected rc=%d, got rc=%d\n", expected_rc, rc);
    }

    if (bc != NULL)
    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

/* Full run through irx_exec_run with the bytecode path on.  When
 * want_fallback is set the compile must overflow and fall back to the
 * interpreter (fallback>0) yet still finish RC=0 with correct output;
 * otherwise the program must run on the VM (exec>0, no fallback). */
static void check_run(struct envblock *env, const char *src, int len,
                      const char *expected_out, int want_fallback,
                      const char *tag)
{
    struct irx_wkblk_int *wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
    int exit_rc = 0;
    int rc;
    int ok;
    char label[160];

    cap_reset();
    wk->wkbi_use_bytecode = 1;
    wk->wkbi_bc_exec_count = 0;
    wk->wkbi_bc_fallback_count = 0;
    rc = irx_exec_run(src, len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;

    if (want_fallback)
    {
        ok = rc == 0 && exit_rc == 0 && wk->wkbi_bc_fallback_count > 0 &&
             strcmp(g_cap, expected_out) == 0;
    }
    else
    {
        ok = rc == 0 && exit_rc == 0 && wk->wkbi_bc_fallback_count == 0 &&
             wk->wkbi_bc_exec_count > 0 && strcmp(g_cap, expected_out) == 0;
    }

    snprintf(label, sizeof(label), "run: %s", tag);
    CHECK(ok, label);
    if (!ok)
    {
        printf("    rc=%d exit_rc=%d exec=%d fallback=%d\n", rc, exit_rc,
               wk->wkbi_bc_exec_count, wk->wkbi_bc_fallback_count);
        printf("    expected:[%s] got:[%s]\n", expected_out, g_cap);
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    static char src[SRCBUF_SIZE];
    char expected[16];
    int len;
    int rc;

    printf("=== issue #212: bytecode fixed-table overflow -> fallback ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbcap: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    printf("\n[compile: fixed-table overflow -> IRXBC_ERR_CAPACITY]\n");

    /* Constants table overflow (add_const). */
    len = gen_consts(src, SRCBUF_SIZE, OVER_LIMIT);
    check_compile(env, src, len, IRXBC_ERR_CAPACITY,
                  "700 distinct constants -> CAPACITY");

    /* Symbols table overflow (add_sym). */
    len = gen_syms(src, SRCBUF_SIZE, OVER_LIMIT);
    check_compile(env, src, len, IRXBC_ERR_CAPACITY,
                  "700 distinct symbols -> CAPACITY");

    /* Boundary: a small program must still compile to bytecode. */
    len = gen_consts(src, SRCBUF_SIZE, UNDER_LIMIT);
    check_compile(env, src, len, IRXBC_OK,
                  "100 distinct constants -> OK (no false positive)");

    printf("\n[run: overflow falls back to the interpreter, not fatal]\n");

    /* The overflowing program must fall back and run to RC=0.  Before
     * the fix this returned a fatal rc=20 from irx_exec_run. */
    len = gen_consts(src, SRCBUF_SIZE, OVER_LIMIT);
    snprintf(expected, sizeof(expected), "c%d\n", OVER_LIMIT - 1);
    check_run(env, src, len, expected, 1,
              "700-constant program falls back and runs");

    /* The small program must stay on the VM (no spurious fallback). */
    len = gen_consts(src, SRCBUF_SIZE, UNDER_LIMIT);
    snprintf(expected, sizeof(expected), "c%d\n", UNDER_LIMIT - 1);
    check_run(env, src, len, expected, 0,
              "100-constant program runs on the VM");

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
