/* ------------------------------------------------------------------ */
/*  tstbcal.c - WP-BC-04 function call / CALL / RETURN equivalence    */
/*                                                                    */
/*  For each REXX snippet, runs via the token-walk interpreter and    */
/*  the bytecode VM, then verifies both produce identical SAY output. */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c */
/*          ../lstring370/src/lstr#xlt.c"                             */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbcal test/mvs/tstbcal.c \                        */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   $LSRC && /tmp/tstbcal      */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
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

#define CAPBUF_SIZE 8192

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
/*  Equivalence helper: run src via token-walk, then via bytecode,    */
/*  check that SAY output matches.                                    */
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

    /* Token-walk run */
    cap_reset();
    wk->wkbi_use_bytecode = 0;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    (void)rc;
    memcpy(tw_out, g_cap, (size_t)(g_cap_len + 1));

    /* Bytecode run */
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
/*  bc_only: run src only via bytecode VM, check expected SAY output. */
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
/*  BIF call in expression context (OP_CALL_BIF)                      */
/* ------------------------------------------------------------------ */

static void test_bif_expr(struct envblock *env)
{
    printf("\n[BIF in expression]\n");

    equiv(env, "SAY LENGTH(\"hello\")", "LENGTH in SAY");
    equiv(env, "SAY LENGTH(\"\")", "LENGTH empty string");
    equiv(env, "SAY COPIES(\"ab\", 3)", "COPIES basic");
    equiv(env, "SAY LEFT(\"hello\", 3)", "LEFT basic");
    equiv(env, "SAY RIGHT(\"hello\", 3)", "RIGHT basic");
    equiv(env, "SAY SUBSTR(\"abcdef\", 2, 3)", "SUBSTR basic");
    equiv(env, "SAY REVERSE(\"abcd\")", "REVERSE");
    equiv(env, "SAY STRIP(\"  hello  \")", "STRIP both");
    equiv(env, "SAY UPPER(\"hello\")", "UPPER");
    equiv(env, "SAY LOWER(\"HELLO\")", "LOWER");

    /* BIF result used in expression */
    equiv(env,
          "x = LENGTH(\"hello\")\nSAY x",
          "LENGTH assigned to var");
    equiv(env,
          "SAY LENGTH(\"hello\") + 0",
          "LENGTH in arithmetic");
    equiv(env,
          "SAY COPIES(\"ab\", 2) || \"c\"",
          "COPIES in concat");

    /* Nested BIF calls */
    equiv(env,
          "SAY LENGTH(COPIES(\"ab\", 3))",
          "LENGTH(COPIES()) nested");
    equiv(env,
          "SAY REVERSE(UPPER(\"hello\"))",
          "REVERSE(UPPER()) nested");
}

/* ------------------------------------------------------------------ */
/*  CALL statement (OP_CALL) — BIF path                               */
/* ------------------------------------------------------------------ */

static void test_call_bif_stmt(struct envblock *env)
{
    printf("\n[CALL stmt — BIF]\n");

    /* CALL stores result in RESULT variable */
    equiv(env,
          "CALL LENGTH \"hello\"\nSAY RESULT",
          "CALL LENGTH, SAY RESULT");
    equiv(env,
          "CALL COPIES \"ab\", 3\nSAY RESULT",
          "CALL COPIES 2-arg, SAY RESULT");
    equiv(env,
          "CALL REVERSE \"abcd\"\nSAY RESULT",
          "CALL REVERSE, SAY RESULT");
}

/* ------------------------------------------------------------------ */
/*  Internal function CALL / RETURN (OP_CALL + OP_RETURN/RETURNV)     */
/* ------------------------------------------------------------------ */

static void test_internal_call(struct envblock *env)
{
    printf("\n[internal CALL / RETURN]\n");

    /* Subroutine that just SAYs — no return value */
    equiv(env,
          "CALL greet\n"
          "EXIT\n"
          "greet:\n"
          "  SAY \"hello from sub\"\n"
          "RETURN\n",
          "CALL sub that SAYs");

    /* Subroutine called multiple times */
    equiv(env,
          "CALL tick\n"
          "CALL tick\n"
          "CALL tick\n"
          "EXIT\n"
          "tick:\n"
          "  SAY \"tick\"\n"
          "RETURN\n",
          "CALL sub 3 times");

    /* RETURN with value — caller reads RESULT */
    equiv(env,
          "CALL double 5\n"
          "SAY RESULT\n"
          "EXIT\n"
          "double:\n"
          "  RETURN ARG(1) * 2\n",
          "CALL RETURN ARG(1)*2");

    /* RETURN with value, multiple calls via expression syntax.
     * Token-walk doesn't support user-defined func() in expr context,
     * so these are bytecode-only tests. */
    bc_only(env,
            "SAY double(3)\n"
            "SAY double(7)\n"
            "EXIT\n"
            "double:\n"
            "  RETURN ARG(1) * 2\n",
            "6\n14\n",
            "function call expr double()");

    bc_only(env,
            "x = double(4) + 1\n"
            "SAY x\n"
            "EXIT\n"
            "double:\n"
            "  RETURN ARG(1) * 2\n",
            "9\n",
            "function result in expr");
}

/* ------------------------------------------------------------------ */
/*  ARG() BIF inside bytecode VM                                      */
/* ------------------------------------------------------------------ */

static void test_arg_bif(struct envblock *env)
{
    printf("\n[ARG() BIF]\n");

    /* ARG(1) returns first arg */
    equiv(env,
          "CALL echo \"world\"\n"
          "SAY RESULT\n"
          "EXIT\n"
          "echo:\n"
          "  RETURN ARG(1)\n",
          "ARG(1) passthrough");

    /* Multiple args */
    equiv(env,
          "CALL add 3, 4\n"
          "SAY RESULT\n"
          "EXIT\n"
          "add:\n"
          "  RETURN ARG(1) + ARG(2)\n",
          "ARG(1)+ARG(2) two args");

    /* ARG() argc */
    equiv(env,
          "CALL f 10, 20, 30\n"
          "SAY RESULT\n"
          "EXIT\n"
          "f:\n"
          "  RETURN ARG()\n",
          "ARG() returns argc");

    /* SC28-1883 §4.3.3: no-value RETURN resets RESULT to uninitialized.
     * After CALL sub (no RETURN expr), RESULT evaluates to its own name. */
    bc_only(env,
            "RESULT = \"old\"\n"
            "CALL noop\n"
            "SAY RESULT\n"
            "EXIT\n"
            "noop:\n"
            "  RETURN\n",
            "RESULT\n",
            "RESULT uninitialized after no-value RETURN");
}

/* ------------------------------------------------------------------ */
/*  RETURN at top level exits program with RC=0                        */
/* ------------------------------------------------------------------ */

static void test_return_top(struct envblock *env)
{
    printf("\n[RETURN at top level]\n");

    /* RETURN at top level should stop execution */
    bc_only(env,
            "SAY \"before\"\nRETURN\nSAY \"after\"\n",
            "before\n",
            "RETURN stops at top level");

    /* RETURN expr at top level — expr evaluated, execution stops */
    bc_only(env,
            "SAY \"before\"\nRETURN 0\nSAY \"after\"\n",
            "before\n",
            "RETURN 0 stops at top level");
}

/* ------------------------------------------------------------------ */
/*  BIF calls in various expression positions                          */
/* ------------------------------------------------------------------ */

static void test_bif_contexts(struct envblock *env)
{
    printf("\n[BIF in various contexts]\n");

    /* BIF in IF condition */
    equiv(env,
          "IF LENGTH(\"hi\") = 2 THEN SAY \"yes\" ELSE SAY \"no\"",
          "BIF in IF condition");

    /* BIF in DO count */
    equiv(env,
          "DO LENGTH(\"abc\")\n  SAY \"x\"\nEND",
          "BIF as DO count");

    /* BIF in assignment */
    equiv(env,
          "n = LENGTH(\"hello world\")\nSAY n",
          "BIF in assignment");

    /* Chained BIF results in comparison */
    equiv(env,
          "IF LENGTH(\"abc\") > LENGTH(\"ab\") THEN SAY \"longer\"",
          "BIF result in comparison");
}

/* ------------------------------------------------------------------ */
/*  WP-BC-OC09: BIF direct-dispatch cache                              */
/*                                                                    */
/*  The VM caches BIF resolution per symbol index, replacing a per-    */
/*  call linear registry walk.  These cases pin the properties that    */
/*  the cache must not break: a user-defined routine shadows a same-   */
/*  named BIF (the cache is never consulted for that symbol), repeated */
/*  and mixed BIF calls stay correct, the cache is shared across the   */
/*  expression (OP_CALL_BIF) and CALL (OP_CALL) forms, and an unknown  */
/*  name still falls through to the not-found error instead of being   */
/*  swallowed.                                                         */
/* ------------------------------------------------------------------ */

static void test_bif_dispatch_cache(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    int unsup_reason = 0;
    int unsup_line = 0;
    int bc_rc = 0;
    int rc;
    const char *unknown_src = "SAY ZZNOSUCH(\"x\")";

    printf("\n[WP-BC-OC09 BIF dispatch cache]\n");

    /* A user-defined routine shadows a same-named BIF.  label_pc wins,
     * so the BIF cache for this symbol is never populated or used. */
    equiv(env,
          "CALL length \"ignored\"\n"
          "SAY RESULT\n"
          "EXIT\n"
          "length:\n"
          "  RETURN \"user-length\"\n",
          "user routine shadows BIF (CALL form)");

    /* Same shadow in expression form (OP_CALL_BIF).  Token-walk has no
     * user-defined func() in expr context, so this is bytecode-only. */
    bc_only(env,
            "SAY length(\"ignored\")\n"
            "EXIT\n"
            "length:\n"
            "  RETURN \"user-length\"\n",
            "user-length\n",
            "user routine shadows BIF (expr form)");

    /* Repeated calls of one BIF exercise the cache hot path. */
    equiv(env,
          "DO i = 1 TO 4\n"
          "  SAY LENGTH(\"abcd\")\n"
          "END",
          "repeated BIF call (hot path)");

    /* Distinct BIFs populate distinct cache slots. */
    equiv(env,
          "SAY SUBSTR(\"abcdef\", 2, 3)\n"
          "SAY LENGTH(\"hello\")\n"
          "SAY WORD(\"alpha beta gamma\", 2)\n"
          "SAY FORMAT(3.14159, 2, 2)\n"
          "SAY REVERSE(\"xyz\")",
          "mixed distinct BIFs (SUBSTR/LENGTH/WORD/FORMAT/REVERSE)");

    /* The same BIF reached through both the expression form and the
     * CALL-statement form shares one cache slot (same symbol index). */
    equiv(env,
          "SAY LENGTH(\"abcde\")\n"
          "CALL LENGTH \"xy\"\n"
          "SAY RESULT\n"
          "SAY LENGTH(\"abcde\")",
          "shared cache across expr + CALL forms");

    /* A user routine and a real BIF with different names keep separate
     * slots and resolutions in the same run. */
    equiv(env,
          "CALL mylen \"abc\"\n"
          "SAY RESULT\n"
          "SAY LENGTH(\"abcd\")\n"
          "EXIT\n"
          "mylen:\n"
          "  RETURN ARG(1)\n",
          "user routine and BIF coexist");

    /* Unknown name: resolution returns NULL on every call, so the VM
     * still reports the not-found error (IRXBC_ERR_UNSUP).  Driven
     * directly through the VM so the exec-layer token-walk fallback
     * does not mask the bytecode behavior — this confirms the fast-path
     * does not swallow unknown names. */
    rc = irx_bc_compile(env, unknown_src, (int)strlen(unknown_src),
                        &bc, &unsup_reason, &unsup_line);
    CHECK(rc == IRXBC_OK && bc != NULL,
          "unknown BIF compiles (compiler is BIF-agnostic)");
    if (rc == IRXBC_OK && bc != NULL)
    {
        rc = irx_bc_execute(env, bc, NULL, 0, &bc_rc);
        CHECK(rc == IRXBC_ERR_UNSUP,
              "unknown BIF -> not-found error (not swallowed)");
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-04 Function Call / CALL / RETURN Tests ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbcal: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_bif_expr(env);
    test_call_bif_stmt(env);
    test_internal_call(env);
    test_arg_bif(env);
    test_return_top(env);
    test_bif_contexts(env);
    test_bif_dispatch_cache(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
