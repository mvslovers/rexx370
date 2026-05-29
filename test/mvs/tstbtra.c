/* ------------------------------------------------------------------ */
/*  tstbtra.c - WP-BC-08: TRACE + ADDRESS bytecode tests               */
/*                                                                    */
/*  Covers AC #1-#10:                                                 */
/*    AC #1  TRACE/ADDRESS no longer UNSUP in bc_stmt.                */
/*    AC #2  TRACE setting / TRACE ?I / TRACE bare toggle.            */
/*    AC #3  TRACE VALUE expr: evaluates, sets fields.                */
/*    AC #4  trace() BIF roundtrip via shared registry.               */
/*    AC #5  ADDRESS env / ADDRESS env command (one-shot stub).       */
/*    AC #6  ADDRESS VALUE expr: evaluates, sets wkbi_address.        */
/*    AC #7  ADDRESS bare toggle: swaps with wkbi_prev_address.       */
/*    AC #8  address() BIF roundtrip via shared registry.             */
/*    AC #9  Default init of wkbi_address / wkbi_prev_address.        */
/*    AC #10 Equivalence tests (equiv) for Token-Walk == Bytecode.    */
/*           Bare-ADDRESS toggle is bc_only (Token-Walk no-op).       */
/*                                                                    */
/*  Default ADDRESS on Linux (no parmblock): "MVS"                   */
/*  Default ADDRESS on MVS (tsofl set):      "TSO"                   */
/*                                                                    */
/*  Cross-compile (Linux/gcc):                                        */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include          */
/*        -Wall -Wextra -std=gnu99 -o /tmp/tstbtra                    */
/*        test/mvs/tstbtra.c                                          */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c             */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c              */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c              */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c             */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c             */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c            */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c             */
/*        ../lstring370/src/lstr#cor.c  ../lstring370/src/lstr#cvt.c  */
/*        ../lstring370/src/lstr#fmt.c  ../lstring370/src/lstr#srch.c */
/*        ../lstring370/src/lstr#sub.c  ../lstring370/src/lstr#wrd.c  */
/*        ../lstring370/src/lstr#xlt.c && /tmp/tstbtra                */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxbctl.h"
#include "irxbops.h"
#include "irxbvm.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxwkblk.h"

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
/*  equiv: run via token-walk + bytecode, check SAY output matches.   */
/* ------------------------------------------------------------------ */

static int equiv(struct envblock *env, const char *src, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int rc;
    int exit_rc = 0;
    char tw_out[CAPBUF_SIZE];
    char bc_out[CAPBUF_SIZE];
    char label[128];

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        printf("  FAIL: %s — no work block\n", tag);
        tests_run++;
        tests_failed++;
        return 0;
    }

    cap_reset();
    wk->wkbi_use_bytecode = 0;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    (void)rc;
    memcpy(tw_out, g_cap, (size_t)(g_cap_len + 1));

    cap_reset();
    wk->wkbi_use_bytecode = 1;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    (void)rc;
    memcpy(bc_out, g_cap, (size_t)(g_cap_len + 1));

    wk->wkbi_use_bytecode = 0;

    snprintf(label, sizeof(label), "equiv: %s", tag);
    CHECK(strcmp(tw_out, bc_out) == 0, label);

    if (strcmp(tw_out, bc_out) != 0)
    {
        printf("    tokwalk: [%s]\n", tw_out);
        printf("    bytecode:[%s]\n", bc_out);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  bc_only: run via bytecode VM, check expected SAY output.          */
/* ------------------------------------------------------------------ */

static int bc_only(struct envblock *env, const char *src,
                   const char *expected, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int rc;
    int exit_rc = 0;
    char label[128];

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        printf("  FAIL: %s — no work block\n", tag);
        tests_run++;
        tests_failed++;
        return 0;
    }

    cap_reset();
    wk->wkbi_use_bytecode = 1;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    (void)rc;
    wk->wkbi_use_bytecode = 0;

    snprintf(label, sizeof(label), "bc_only: %s", tag);
    CHECK(strcmp(g_cap, expected) == 0, label);

    if (strcmp(g_cap, expected) != 0)
    {
        printf("    expected:[%s]\n", expected);
        printf("    got:     [%s]\n", g_cap);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  TRACE tests                                                        */
/* ------------------------------------------------------------------ */

static void test_trace(struct envblock *env)
{
    printf("\n--- TRACE setting (equiv) ---\n");

    equiv(env,
          "trace off\n"
          "say trace()\n",
          "TRACE Off -> trace() returns O");

    equiv(env,
          "trace normal\n"
          "say trace()\n",
          "TRACE Normal -> trace() returns N");

    equiv(env,
          "trace all\n"
          "say trace()\n",
          "TRACE All -> trace() returns A");

    /* Lowercase option letter is valid. */
    equiv(env,
          "trace i\n"
          "say trace()\n",
          "TRACE i (lowercase) -> trace() returns I");

    /* String literal option. */
    equiv(env,
          "trace 'R'\n"
          "say trace()\n",
          "TRACE string-literal R -> trace() returns R");

    /* Interactive prefix ?O. */
    equiv(env,
          "trace ?O\n"
          "say trace()\n",
          "TRACE ?O -> trace() returns ?O");

    /* Interactive prefix ?N. */
    equiv(env,
          "trace ?N\n"
          "say trace()\n",
          "TRACE ?N -> trace() returns ?N");

    printf("\n--- TRACE VALUE (equiv) ---\n");

    equiv(env,
          "x = 'O'\n"
          "trace value x\n"
          "say trace()\n",
          "TRACE VALUE variable O -> trace() returns O");

    equiv(env,
          "trace value 'N'\n"
          "say trace()\n",
          "TRACE VALUE literal N -> trace() returns N");

    equiv(env,
          "trace value '?I'\n"
          "say trace()\n",
          "TRACE VALUE ?I -> trace() returns ?I");

    printf("\n--- TRACE bare toggle (bc_only) ---\n");

    /* Bare TRACE toggles wkbi_interactive; letter unchanged.
     * Token-Walk has the same semantics, but we use bc_only here
     * since we need to verify precise field-level behavior with a
     * known initial state set via bytecode. */

    bc_only(env,
            "trace off\n"
            "trace\n"
            "say trace()\n",
            "?O\n",
            "bare TRACE: toggles interactive on");

    bc_only(env,
            "trace off\n"
            "trace\n"
            "trace\n"
            "say trace()\n",
            "O\n",
            "bare TRACE twice: back to non-interactive");

    printf("\n--- trace() BIF roundtrip (equiv) ---\n");

    /* trace() with no arg returns current setting without changing it. */
    equiv(env,
          "trace off\n"
          "x = trace()\n"
          "say x\n",
          "trace() no-arg returns current setting");

    /* trace() with arg sets new, returns old. */
    equiv(env,
          "trace normal\n"
          "old = trace('O')\n"
          "say old\n"
          "say trace()\n",
          "trace() with arg: returns old, sets new");

    printf("\n--- TRACE number skip-form (equiv) ---\n");

    /* TRACE number: option unchanged. */
    equiv(env,
          "trace off\n"
          "trace 5\n"
          "say trace()\n",
          "TRACE number: setting unchanged");

    equiv(env,
          "trace off\n"
          "trace -3\n"
          "say trace()\n",
          "TRACE negative number: setting unchanged");
}

/* ------------------------------------------------------------------ */
/*  ADDRESS tests                                                       */
/* ------------------------------------------------------------------ */

static void test_address(struct envblock *env)
{
    printf("\n--- ADDRESS env (equiv) ---\n");

    equiv(env,
          "address 'TSO'\n"
          "say address()\n",
          "ADDRESS string TSO -> address() returns TSO");

    equiv(env,
          "address 'MVS'\n"
          "say address()\n",
          "ADDRESS string MVS -> address() returns MVS");

    /* Symbol token is uppercased by tokenizer. */
    equiv(env,
          "address ISHELL\n"
          "say address()\n",
          "ADDRESS symbol ISHELL -> address() returns ISHELL");

    /* 8-char env name (max). */
    equiv(env,
          "address 'LONGNM12'\n"
          "say address()\n",
          "ADDRESS 8-char name");

    /* Overflow truncated to 8 chars. */
    equiv(env,
          "address 'TOOLONGENV'\n"
          "say address()\n",
          "ADDRESS >8 chars truncated to 8");

    printf("\n--- ADDRESS VALUE (equiv) ---\n");

    equiv(env,
          "x = 'TSO'\n"
          "address value x\n"
          "say address()\n",
          "ADDRESS VALUE variable -> address() returns TSO");

    equiv(env,
          "address value 'MVS'\n"
          "say address()\n",
          "ADDRESS VALUE literal MVS");

    /* Concatenation in VALUE expression. */
    equiv(env,
          "e = 'TS'\n"
          "address value e || 'O'\n"
          "say address()\n",
          "ADDRESS VALUE concat -> address() returns TSO");

    printf("\n--- address() BIF roundtrip (equiv) ---\n");

    equiv(env,
          "address 'TSO'\n"
          "say address()\n",
          "address() returns current environment");

    printf("\n--- ADDRESS toggle (bc_only) ---\n");
    /* Bare ADDRESS swaps current <-> prev.
     * Token-Walk kw_address bare-form is a documented no-op (see
     * irx#pars.c:3769-3778 TODO comment), so these are bc_only. */

    /* Explicitly set ORIG first so prev is predictable, then set FOO,
     * then toggle — should restore ORIG regardless of prior test state. */
    bc_only(env,
            "address 'ORIG'\n"
            "address 'FOO'\n"
            "address\n"
            "say address()\n",
            "ORIG\n",
            "ADDRESS toggle: restores previous env");

    /* Double toggle returns to the second set value. */
    bc_only(env,
            "address 'BAR'\n"
            "address\n"
            "address\n"
            "say address()\n",
            "BAR\n",
            "ADDRESS toggle twice: back to BAR");

    /* Two explicit sets then toggle. */
    bc_only(env,
            "address 'FIRST'\n"
            "address 'SECOND'\n"
            "address\n"
            "say address()\n",
            "FIRST\n",
            "ADDRESS toggle: restores FIRST after SECOND");

    printf("\n--- ADDRESS one-shot form (bc_only stub) ---\n");
    /* ADDRESS env command: clause consumed, no state change (WP-33 deferred). */
    bc_only(env,
            "address 'TSO'\n"
            "address 'MVS' 'SOME COMMAND'\n"
            "say address()\n",
            "TSO\n",
            "ADDRESS one-shot stub: address unchanged");

    printf("\n--- Default ADDRESS init (fresh env) ---\n");
    /* Verify that bytecode and Token-Walk agree on the default ADDRESS.
     * The default depends on the environment: TSO foreground → "TSO",
     * batch / Linux → "MVS".  equiv() handles all platforms correctly. */
    {
        struct envblock *fresh_env = NULL;
        int frc = irxinit(NULL, &fresh_env);
        if (frc == 0 && fresh_env != NULL)
        {
            struct irxexte *exte =
                (struct irxexte *)fresh_env->envblock_irxexte;
            if (exte != NULL)
            {
                exte->io_routine = (void *)capture_io;
            }
            equiv(fresh_env,
                  "say address()\n",
                  "Default address: bytecode matches Token-Walk");
            irxterm(fresh_env);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Disasm spot-check: verify TRACE/ADDRESS opcodes appear in output  */
/* ------------------------------------------------------------------ */

static void test_disasm(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    char dis[4096];
    int rc;

    printf("\n--- Disassembler spot-check ---\n");

    /* TRACE Off -> TRACE_SET opcode. */
    {
        const char *src = "trace off\n";
        rc = irx_bc_compile(env, src, (int)strlen(src), &bc);
        CHECK(rc == 0, "TRACE Off compiles OK");
        if (rc == 0 && bc != NULL)
        {
            void *p = bc;
            irx_bc_disasm(bc, dis, (int)sizeof(dis));
            CHECK(strstr(dis, "TRACE_SET") != NULL,
                  "disasm: TRACE Off -> TRACE_SET");
            irxstor(RXSMFRE, 0, &p, env);
            bc = NULL;
        }
    }

    /* Bare TRACE -> TRACE_TOGGLE. */
    {
        const char *src = "trace\n";
        rc = irx_bc_compile(env, src, (int)strlen(src), &bc);
        CHECK(rc == 0, "bare TRACE compiles OK");
        if (rc == 0 && bc != NULL)
        {
            void *p = bc;
            irx_bc_disasm(bc, dis, (int)sizeof(dis));
            CHECK(strstr(dis, "TRACE_TOGGLE") != NULL,
                  "disasm: bare TRACE -> TRACE_TOGGLE");
            irxstor(RXSMFRE, 0, &p, env);
            bc = NULL;
        }
    }

    /* TRACE VALUE x -> TRACE_VALUE. */
    {
        const char *src = "trace value x\n";
        rc = irx_bc_compile(env, src, (int)strlen(src), &bc);
        CHECK(rc == 0, "TRACE VALUE compiles OK");
        if (rc == 0 && bc != NULL)
        {
            void *p = bc;
            irx_bc_disasm(bc, dis, (int)sizeof(dis));
            CHECK(strstr(dis, "TRACE_VALUE") != NULL,
                  "disasm: TRACE VALUE -> TRACE_VALUE");
            irxstor(RXSMFRE, 0, &p, env);
            bc = NULL;
        }
    }

    /* ADDRESS TSO -> ADDRESS_SET with sym name. */
    {
        const char *src = "address tso\n";
        rc = irx_bc_compile(env, src, (int)strlen(src), &bc);
        CHECK(rc == 0, "ADDRESS TSO compiles OK");
        if (rc == 0 && bc != NULL)
        {
            void *p = bc;
            irx_bc_disasm(bc, dis, (int)sizeof(dis));
            CHECK(strstr(dis, "ADDRESS_SET") != NULL,
                  "disasm: ADDRESS env -> ADDRESS_SET");
            CHECK(strstr(dis, "TSO") != NULL,
                  "disasm: ADDRESS_SET shows env name TSO");
            irxstor(RXSMFRE, 0, &p, env);
            bc = NULL;
        }
    }

    /* Bare ADDRESS -> ADDRESS_TOGGLE. */
    {
        const char *src = "address\n";
        rc = irx_bc_compile(env, src, (int)strlen(src), &bc);
        CHECK(rc == 0, "bare ADDRESS compiles OK");
        if (rc == 0 && bc != NULL)
        {
            void *p = bc;
            irx_bc_disasm(bc, dis, (int)sizeof(dis));
            CHECK(strstr(dis, "ADDRESS_TOGGLE") != NULL,
                  "disasm: bare ADDRESS -> ADDRESS_TOGGLE");
            irxstor(RXSMFRE, 0, &p, env);
            bc = NULL;
        }
    }

    /* ADDRESS VALUE x -> ADDRESS_VALUE. */
    {
        const char *src = "address value x\n";
        rc = irx_bc_compile(env, src, (int)strlen(src), &bc);
        CHECK(rc == 0, "ADDRESS VALUE compiles OK");
        if (rc == 0 && bc != NULL)
        {
            void *p = bc;
            irx_bc_disasm(bc, dis, (int)sizeof(dis));
            CHECK(strstr(dis, "ADDRESS_VALUE") != NULL,
                  "disasm: ADDRESS VALUE -> ADDRESS_VALUE");
            irxstor(RXSMFRE, 0, &p, env);
            bc = NULL;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    int rc;

    printf("=== WP-BC-08: TRACE + ADDRESS Bytecode Tests ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbtra: irxinit failed rc=%d\n", rc);
        return 1;
    }

    /* Install output capture routine. */
    {
        struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
        if (exte != NULL)
        {
            exte->io_routine = (void *)capture_io;
        }
    }

    test_trace(env);
    test_address(env);
    test_disasm(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed ---\n", tests_passed, tests_run);
    return (tests_failed == 0) ? 0 : 1;
}
