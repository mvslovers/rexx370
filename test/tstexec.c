/* ------------------------------------------------------------------ */
/*  tstexec.c - WP-CPS-06 IRXEXEC Service-Layer unit tests           */
/*                                                                    */
/*  Tests irx_exec_dispatch() directly (bypasses asm/irxexec.asm).   */
/*                                                                    */
/*  T1 : SUBROUTINE call, source "say 1+1\nexit 5", expect RC=5      */
/*  T2 : SUBROUTINE call with one arg, "parse arg x\nexit x", RC=42  */
/*  T3 : Bad INSTBLK eye-catcher                 -> BADPLIST         */
/*  T4 : NULL INSTBLK + NULL EXECBLK             -> BADPLIST         */
/*  T5 : envblock NULL + registered env (FINDENVB path) -> success   */
/*  T6 : envblock NULL + no registered env       -> NOENV (RC=28)    */
/*  T7 : envblock_r0 non-NULL (R0 path)          -> success          */
/*  T8 : Bad EXECBLK length (not V1 not V2)      -> BADPLIST         */
/*  T9 : EVALBLOCK valid on entry; NORESULT set on return            */
/*  T10: EVALBLOCK with EVPAD1 != 0              -> BADPLIST         */
/*  T11: EVALBLOCK NULL                          -> no write          */
/*  T12: VL-marker on P9 (P10 omitted) — parsed ok by dispatch args  */
/*  T13: VL-marker on P10 (ASM wrapper stores *P10) — asm-only,      */
/*       not testable from C; noted below                             */
/*  T14: EXECBLK SUBCOM non-blank -> wkbi_address overridden          */
/*  T15: ARGTABLE with 3 args -> first arg passed; others counted     */
/*                                                                    */
/*  Host cross-compile (superset link):                               */
/*    LSTRING_INC="-I contrib/lstring370-0.1.0-dev/include"           */
/*    LSTRING_SRC="<lstring370 sources>"                              */
/*    gcc -I include $LSTRING_INC -Wall -Wextra -std=gnu99 \          */
/*        -o /tmp/tstexec test/mvs/tstexec.c \                        */
/*        src/irx#exec.c src/irx#init.c src/irx#term.c \             */
/*        src/irx#stor.c src/irx#anch.c src/irx#env.c \              */
/*        src/irx#uid.c  src/irx#msid.c src/irx#cond.c \             */
/*        src/irx#bif.c  src/irx#bifs.c src/irx#io.c \              */
/*        src/irx#lstr.c src/irx#tokn.c src/irx#vpol.c \             */
/*        src/irx#pars.c src/irx#ctrl.c src/irx#arith.c \            */
/*        $LSTRING_SRC                                                */
/*                                                                    */
/*  MVS invocation:                                                   */
/*    TSO:   CALL 'hlq.LOAD(TSTEXEC)'                                 */
/*    Batch: EXEC PGM=TSTEXEC                                         */
/*  Expected: CC=0                                                    */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irx_init.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxwkblk.h"

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

/* Host-side test-only global: simulates ECTENVBK for irx_init_findenvb.
 * Declared extern in irx#anch.c for cross-compile builds only. */
#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

/* ------------------------------------------------------------------
 * build_mock_instblk — construct an INSTBLK from an array of lines.
 *
 * Lines are copied into a single flat source pool with '\n' between
 * them (same approach as irx_exec_dispatch's reconstruction step).
 * Memory is from malloc so tests can run without an envblock.
 * Caller must free the returned pointer and *out_pool when done.
 * ------------------------------------------------------------------ */
static struct instblk *build_mock_instblk(const char *lines[], int n_lines,
                                          char **out_pool)
{
    /* Compute total source bytes and INSTBLK size. */
    int total_src = 0;
    int i;
    char *src_pool;
    struct instblk *hdr;
    struct instblk_entry *ents;
    int hdr_sz;
    int pos;

    for (i = 0; i < n_lines; i++)
    {
        total_src += (int)strlen(lines[i]);
    }
    if (n_lines > 1)
    {
        total_src += n_lines - 1;
    }

    hdr_sz = (int)sizeof(struct instblk) +
             n_lines * (int)sizeof(struct instblk_entry);

    hdr = (struct instblk *)calloc(1, (size_t)hdr_sz);
    if (hdr == NULL)
    {
        return NULL;
    }

    src_pool = (char *)malloc((size_t)(total_src > 0 ? total_src : 1));
    if (src_pool == NULL)
    {
        free(hdr);
        return NULL;
    }

    /* Fill header. */
    memcpy(hdr->instblk_acronym, INSTBLK_ID, sizeof(hdr->instblk_acronym));
    hdr->instblk_hdrlen = INSTBLK_HDRLEN;
    hdr->instblk_usedlen = n_lines * (int)sizeof(struct instblk_entry);
    ents = (struct instblk_entry *)((char *)hdr + sizeof(struct instblk));
    hdr->instblk_address = ents;

    /* Fill source pool and entry table. */
    pos = 0;
    for (i = 0; i < n_lines; i++)
    {
        int len = (int)strlen(lines[i]);
        if (i > 0)
        {
            src_pool[pos++] = '\n';
        }
        if (len > 0)
        {
            memcpy(src_pool + pos, lines[i], (size_t)len);
        }
        ents[i].instblk_stmt_ = src_pool + pos;
        ents[i].instblk_stmtlen = len;
        pos += len;
    }

    *out_pool = src_pool;
    return hdr;
}

/* build_mock_argtable — build a one-argument ARGTABLE terminated by
 * X'FFFFFFFFFFFFFFFF'. Caller frees the returned pointer. */
static void *build_mock_argtable(const char *arg, int arg_len)
{
    /* Two entries: one data entry + one terminator. */
    struct argtable_entry *tbl =
        (struct argtable_entry *)malloc(2 * sizeof(struct argtable_entry));
    if (tbl == NULL)
    {
        return NULL;
    }
    tbl[0].argstring_ptr = (void *)arg;
    tbl[0].argstring_length = arg_len;
    /* End-of-table marker: all bits set (X'FFFFFFFFFFFFFFFF' per spec). */
    memset(&tbl[1], -1, sizeof(tbl[1]));
    return tbl;
}

/* build_argtable_n — build an N-argument ARGTABLE. Args must remain
 * valid for the lifetime of the argtable. */
static void *build_argtable_n(const char *args[], int lens[], int n)
{
    int i;
    struct argtable_entry *tbl =
        (struct argtable_entry *)malloc((size_t)(n + 1) *
                                        sizeof(struct argtable_entry));
    if (tbl == NULL)
    {
        return NULL;
    }
    for (i = 0; i < n; i++)
    {
        tbl[i].argstring_ptr = (void *)args[i];
        tbl[i].argstring_length = lens[i];
    }
    /* End-of-table marker: all bits set (X'FFFFFFFFFFFFFFFF' per spec). */
    memset(&tbl[n], -1, sizeof(tbl[n]));
    return tbl;
}

/* ------------------------------------------------------------------
 * Helper: create an env with irxinit; termination is caller's job.
 * ------------------------------------------------------------------ */
static struct envblock *make_env(void)
{
    struct envblock *env = NULL;
    if (irxinit(NULL, &env) != 0)
    {
        return NULL;
    }
    return env;
}

/* ================================================================== */
/*  T1 — SUBROUTINE call, "say 1+1\nexit 5", expect RC=5             */
/* ================================================================== */
static void test_subroutine_exit5(void)
{
    const char *lines[] = {"say 1+1", "exit 5"};
    char *pool = NULL;
    struct instblk *ib;
    struct envblock *env;
    int rc;

    printf("T1: SUBROUTINE call, exit 5\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 2, &pool);
    if (!ib)
    {
        irxterm(env);
        printf("  SKIP: build_mock_instblk failed\n");
        return;
    }

    rc = irx_exec_dispatch(NULL, NULL, 0, ib, NULL, NULL,
                           NULL, NULL, env, NULL);

    CHECK(rc == 5, "irx_exec_dispatch returns 5 (EXIT 5)");

    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  T2 — SUBROUTINE call with one arg, "parse arg x\nexit x", RC=42  */
/* ================================================================== */
static void test_arg_forwarding(void)
{
    const char *lines[] = {"parse arg x", "exit x"};
    char *pool = NULL;
    struct instblk *ib;
    struct envblock *env;
    const char *arg = "42";
    void *argtab;
    int rc;

    printf("T2: SUBROUTINE call with ARG(1)='42', expect RC=42\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 2, &pool);
    argtab = build_mock_argtable(arg, (int)strlen(arg));
    if (!ib || !argtab)
    {
        free(pool);
        free(ib);
        free(argtab);
        irxterm(env);
        printf("  SKIP: allocation failed\n");
        return;
    }

    rc = irx_exec_dispatch(NULL, argtab, 0, ib, NULL, NULL,
                           NULL, NULL, env, NULL);

    CHECK(rc == 42, "irx_exec_dispatch returns 42 (parse arg x; exit x)");

    free(argtab);
    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  T3 — Bad INSTBLK eye-catcher -> BADPLIST                         */
/* ================================================================== */
static void test_bad_instblk_eyecatcher(void)
{
    const char *lines[] = {"exit 0"};
    char *pool = NULL;
    struct instblk *ib;
    struct envblock *env;
    int rc;

    printf("T3: Bad INSTBLK eye-catcher -> BADPLIST\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 1, &pool);
    if (!ib)
    {
        irxterm(env);
        printf("  SKIP: build_mock_instblk failed\n");
        return;
    }
    memcpy(ib->instblk_acronym, "BADBLOCK", sizeof(ib->instblk_acronym));

    rc = irx_exec_dispatch(NULL, NULL, 0, ib, NULL, NULL,
                           NULL, NULL, env, NULL);

    CHECK(rc == IRXEXEC_BADPLIST, "bad eye-catcher -> BADPLIST (32)");

    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  T4 — NULL INSTBLK + NULL EXECBLK -> BADPLIST                     */
/* ================================================================== */
static void test_null_source(void)
{
    struct envblock *env;
    int rc;

    printf("T4: NULL INSTBLK + NULL EXECBLK -> BADPLIST\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    rc = irx_exec_dispatch(NULL, NULL, 0, NULL, NULL, NULL,
                           NULL, NULL, env, NULL);

    CHECK(rc == IRXEXEC_BADPLIST, "NULL source -> BADPLIST (32)");

    irxterm(env);
}

/* ================================================================== */
/*  T5 — envblock NULL + registered env (FINDENVB path) -> success   */
/* ================================================================== */
static void test_findenvb_path(void)
{
    const char *lines[] = {"exit 7"};
    char *pool = NULL;
    struct instblk *ib;
    struct envblock *env;
    int rc;

    printf("T5: FINDENVB path (envblock=NULL, envblock_r0=NULL)\n");

    /* irxinit registers the env so FINDENVB can locate it. */
    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 1, &pool);
    if (!ib)
    {
        irxterm(env);
        printf("  SKIP: build_mock_instblk failed\n");
        return;
    }

    /* Pass NULL for both envblock and envblock_r0 — dispatcher uses FINDENVB. */
    rc = irx_exec_dispatch(NULL, NULL, 0, ib, NULL, NULL,
                           NULL, NULL, NULL, NULL);

    CHECK(rc == 7, "FINDENVB path: exit 7 -> RC=7");

    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  T6 — envblock NULL + no registered env -> NOENV (RC=28)          */
/* ================================================================== */
static void test_noenv(void)
{
    const char *lines[] = {"exit 0"};
    char *pool = NULL;
    struct instblk *ib;
    int rc;

    printf("T6: No env registered -> NOENV (RC=28)\n");

    /* Foreground TSO has a pre-initialised REXX env from the READY
     * prompt's IRXINIT. We cannot tear that down without disturbing
     * IKJEFT01's own state, so this case only runs meaningfully in
     * Batch / Batch-TSO contexts where no pre-existing env exists. */
    {
        struct envblock *probe = NULL;
        int probe_rsn = 0;
        if (irx_init_findenvb(&probe, &probe_rsn) == 0 && probe != NULL)
        {
            printf("  SKIP: pre-existing env on TCB (Foreground TSO);"
                   " no-env scenario not reproducible here\n");
            return;
        }
    }

#ifndef __MVS__
    /* Ensure IRXANCHR slot table has no registered env by resetting
     * the simulated ECT anchor used on host builds. */
    _simulated_ectenvbk = NULL;
#endif

    ib = build_mock_instblk(lines, 1, &pool);
    if (!ib)
    {
        printf("  SKIP: build_mock_instblk failed\n");
        return;
    }

    /* No env registered; both env params NULL -> NOENV. */
    rc = irx_exec_dispatch(NULL, NULL, 0, ib, NULL, NULL,
                           NULL, NULL, NULL, NULL);

    CHECK(rc == IRXEXEC_NOENV, "no env -> NOENV (28)");

    free(pool);
    free(ib);
}

/* ================================================================== */
/*  T7 — envblock_r0 non-NULL (R0 path) -> success                   */
/* ================================================================== */
static void test_envblock_r0_path(void)
{
    const char *lines[] = {"exit 3"};
    char *pool = NULL;
    struct instblk *ib;
    struct envblock *env;
    int rc;

    printf("T7: envblock_r0 path (P9=NULL, R0=env)\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 1, &pool);
    if (!ib)
    {
        irxterm(env);
        printf("  SKIP: build_mock_instblk failed\n");
        return;
    }

    /* P9 = NULL; envblock_r0 = env (simulates R0 at asm entry). */
    rc = irx_exec_dispatch(NULL, NULL, 0, ib, NULL, NULL,
                           NULL, NULL, NULL, env);

    CHECK(rc == 3, "R0 path: exit 3 -> RC=3");

    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  T8 — Bad EXECBLK length (not V1 not V2) -> BADPLIST              */
/* ================================================================== */
static void test_bad_execblk_length(void)
{
    struct execblk eb;
    struct envblock *env;
    int rc;

    printf("T8: Bad EXECBLK length -> BADPLIST\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    memset(&eb, 0, sizeof(eb));
    memcpy(eb.exec_blk_acryn, EXECBLK_ID, sizeof(eb.exec_blk_acryn));
    eb.exec_blk_length = EXECBLK_V1_LEN / 2; /* clearly not V1 nor V2 */

    /* NULL INSTBLK + valid EXECBLK (but bad length) -> BADPLIST */
    rc = irx_exec_dispatch(&eb, NULL, 0, NULL, NULL, NULL,
                           NULL, NULL, env, NULL);

    CHECK(rc == IRXEXEC_BADPLIST, "bad EXECBLK length -> BADPLIST (32)");

    irxterm(env);
}

/* ================================================================== */
/*  T9 — EVALBLOCK provided, valid; NORESULT written on return        */
/* ================================================================== */
static void test_evalblock_noresult(void)
{
    const char *lines[] = {"exit 0"};
    char *pool = NULL;
    struct instblk *ib;
    struct envblock *env;
    struct evalblock *evb;
    int rc;

    printf("T9: EVALBLOCK -> NORESULT marker written on return\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 1, &pool);
    evb = (struct evalblock *)calloc(1, sizeof(struct evalblock) +
                                            EVALBLOCK_DATA_LEN);
    if (!ib || !evb)
    {
        free(pool);
        free(ib);
        free(evb);
        irxterm(env);
        printf("  SKIP: allocation failed\n");
        return;
    }

    /* evpad1, evpad2, evlen all 0 -> valid on entry. */
    rc = irx_exec_dispatch(NULL, NULL, 0, ib, NULL, evb,
                           NULL, NULL, env, NULL);

    CHECK(rc == 0, "exit 0 -> RC=0");
    CHECK(evb->evalblock_evlen == EVALBLOCK_NORESULT,
          "EVALBLOCK.evlen = 0x80000000 (NORESULT)");

    free(evb);
    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  T10 — EVALBLOCK with EVPAD1 != 0 -> BADPLIST                     */
/* ================================================================== */
static void test_evalblock_bad_pad1(void)
{
    const char *lines[] = {"exit 0"};
    char *pool = NULL;
    struct instblk *ib;
    struct envblock *env;
    struct evalblock evb;
    int rc;

    printf("T10: EVALBLOCK EVPAD1 != 0 -> BADPLIST\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 1, &pool);
    if (!ib)
    {
        irxterm(env);
        printf("  SKIP: build_mock_instblk failed\n");
        return;
    }

    memset(&evb, 0, sizeof(evb));
    evb.evalblock_evpad1 = 1; /* invalid per spec */

    rc = irx_exec_dispatch(NULL, NULL, 0, ib, NULL, &evb,
                           NULL, NULL, env, NULL);

    CHECK(rc == IRXEXEC_BADPLIST, "EVPAD1!=0 -> BADPLIST (32)");

    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  T11 — EVALBLOCK NULL -> no write attempted                        */
/* ================================================================== */
static void test_evalblock_null(void)
{
    const char *lines[] = {"exit 0"};
    char *pool = NULL;
    struct instblk *ib;
    struct envblock *env;
    int rc;

    printf("T11: NULL EVALBLOCK -> no write\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 1, &pool);
    if (!ib)
    {
        irxterm(env);
        printf("  SKIP: build_mock_instblk failed\n");
        return;
    }

    /* NULL evalblock: dispatch must not dereference it. */
    rc = irx_exec_dispatch(NULL, NULL, 0, ib, NULL, NULL,
                           NULL, NULL, env, NULL);

    CHECK(rc == 0, "NULL EVALBLOCK: exit 0 -> RC=0 (no crash)");

    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  T12 — VL on P9 (P10 omitted): dispatch arg layout still ok       */
/*                                                                    */
/*  The ASM wrapper sets P10 slot present/absent; C dispatch receives */
/*  10 parameters in both cases. From C we just verify that omitting  */
/*  P10 (passing NULL as envblock_r0 and relying on P9 env) works.   */
/* ================================================================== */
static void test_vl_on_p9(void)
{
    const char *lines[] = {"exit 2"};
    char *pool = NULL;
    struct instblk *ib;
    struct envblock *env;
    int rc;

    printf("T12: VL on P9 (P10 omitted) — dispatch still works\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 1, &pool);
    if (!ib)
    {
        irxterm(env);
        printf("  SKIP: build_mock_instblk failed\n");
        return;
    }

    /* envblock_r0=NULL simulates P10 being absent (VL on P9). */
    rc = irx_exec_dispatch(NULL, NULL, 0, ib, NULL, NULL,
                           NULL, NULL, env, NULL);

    CHECK(rc == 2, "VL-on-P9 path: exit 2 -> RC=2");

    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  T13 — VL on P10, RC stored via *P10: ASM wrapper behavior only.  */
/*         The asm wrapper stores R15 through the P10 pointer after   */
/*         the C call.  This cannot be validated from a C-only test.  */
/*         Verified on MVS with the full asm/irxexec.asm linked in.   */
/* ================================================================== */
static void test_vl_on_p10_note(void)
{
    printf("T13: VL on P10 (*P10 = RC) — ASM wrapper only; "
           "skipped in C host test\n");
    /* No CHECK calls: this case is asm-wrapper behavior, not C. */
}

/* ================================================================== */
/*  T14 — EXECBLK SUBCOM non-blank -> wkbi_address overridden         */
/* ================================================================== */
static void test_execblk_subcom(void)
{
    const char *lines[] = {"exit 0"};
    char *pool = NULL;
    struct instblk *ib;
    struct execblk eb;
    struct envblock *env;
    struct irx_wkblk_int *wk;
    int rc;

    printf("T14: EXECBLK SUBCOM non-blank -> wkbi_address overridden\n");

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 1, &pool);
    if (!ib)
    {
        irxterm(env);
        printf("  SKIP: build_mock_instblk failed\n");
        return;
    }

    memset(&eb, 0, sizeof(eb));
    memcpy(eb.exec_blk_acryn, EXECBLK_ID, sizeof(eb.exec_blk_acryn));
    eb.exec_blk_length = EXECBLK_V1_LEN;
    /* exec_subcom = "ISPF    " (non-blank) */
    memcpy(eb.exec_subcom, "ISPF    ", sizeof(eb.exec_subcom));

    rc = irx_exec_dispatch(&eb, NULL, 0, ib, NULL, NULL,
                           NULL, NULL, env, NULL);

    CHECK(rc == 0, "SUBCOM override: exit 0 -> RC=0");

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk != NULL)
    {
        CHECK(memcmp(wk->wkbi_address, "ISPF    ",
                     sizeof(wk->wkbi_address)) == 0,
              "wkbi_address updated to 'ISPF    '");
    }
    else
    {
        printf("  SKIP: wkblk NULL (wkbi_address check skipped)\n");
    }

    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  T15 — ARGTABLE with 3 args: first forwarded, rest counted only   */
/* ================================================================== */
static void test_argtable_multi(void)
{
    /* Exec reads only ARG(1); exit with its numeric value. */
    const char *lines[] = {"parse arg x", "exit x"};
    char *pool = NULL;
    struct instblk *ib;
    struct envblock *env;

    /* Three args: only the first ("10") should reach the engine. */
    enum
    {
        N_ARGS = 3
    };

    const char *args[N_ARGS];
    int lens[N_ARGS];
    void *argtab;
    int rc;

    args[0] = "10";
    args[1] = "20";
    args[2] = "30";
    lens[0] = 2;
    lens[1] = 2;
    lens[2] = 2;

    printf("T15: ARGTABLE %d args -> first passed to engine, RC=10\n", N_ARGS);

    env = make_env();
    if (!env)
    {
        printf("  SKIP: irxinit failed\n");
        return;
    }

    ib = build_mock_instblk(lines, 2, &pool);
    argtab = build_argtable_n(args, lens, N_ARGS);
    if (!ib || !argtab)
    {
        free(pool);
        free(ib);
        free(argtab);
        irxterm(env);
        printf("  SKIP: allocation failed\n");
        return;
    }

    rc = irx_exec_dispatch(NULL, argtab, 0, ib, NULL, NULL,
                           NULL, NULL, env, NULL);

    CHECK(rc == 10, "first arg '10' forwarded: exit x -> RC=10");

    free(argtab);
    free(pool);
    free(ib);
    irxterm(env);
}

/* ================================================================== */
/*  main                                                              */
/* ================================================================== */
int main(void)
{
    printf("TSTEXEC: IRXEXEC Service-Layer tests (WP-CPS-06)\n");
    printf("--------------------------------------------------\n");

    test_subroutine_exit5();
    test_arg_forwarding();
    test_bad_instblk_eyecatcher();
    test_null_source();
    test_findenvb_path();
    test_noenv();
    test_envblock_r0_path();
    test_bad_execblk_length();
    test_evalblock_noresult();
    test_evalblock_bad_pad1();
    test_evalblock_null();
    test_vl_on_p9();
    test_vl_on_p10_note();
    test_execblk_subcom();
    test_argtable_multi();

    printf("--------------------------------------------------\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
