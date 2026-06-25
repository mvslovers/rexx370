/* ------------------------------------------------------------------ */
/*  tstbctl.c - WP-BC-03 bytecode control-flow equivalence tests      */
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
/*        -o /tmp/tstbctl test/mvs/tstbctl.c \                        */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c \           */
/*        $LSRC && /tmp/tstbctl                                        */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
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
/*  Equivalence helper: run src via token-walk, then via bytecode,    */
/*  check that SAY output matches.                                    */
/*                                                                    */
/*  Returns 1 if outputs match, 0 otherwise.                          */
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

    /* Always reset after bytecode run */
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
/*  SAY tests                                                         */
/* ------------------------------------------------------------------ */

static void test_say(struct envblock *env)
{
    printf("\n[SAY]\n");
    equiv(env, "SAY \"Hello\"", "SAY literal");
    equiv(env, "SAY 42", "SAY integer");
    equiv(env, "SAY 1+1", "SAY expression");
    equiv(env, "SAY \"\"", "SAY empty string");
    equiv(env, "SAY \"A\" || \"B\"", "SAY concat");
    equiv(env, "SAY 2**10", "SAY power");
    equiv(env, "SAY 10 // 3", "SAY modulo");
}

/* ------------------------------------------------------------------ */
/*  IF tests                                                          */
/* ------------------------------------------------------------------ */

static void test_if(struct envblock *env)
{
    printf("\n[IF]\n");
    equiv(env, "IF 1 THEN SAY \"yes\"", "IF true then SAY");
    equiv(env, "IF 0 THEN SAY \"yes\"", "IF false then SAY");
    equiv(env,
          "IF 1 THEN SAY \"yes\"\nELSE SAY \"no\"",
          "IF/ELSE true");
    equiv(env,
          "IF 0 THEN SAY \"yes\"\nELSE SAY \"no\"",
          "IF/ELSE false");
    equiv(env,
          "x = 5\nIF x > 3 THEN SAY \"big\"",
          "IF variable comparison");
    equiv(env,
          "x = 5\nIF x > 10 THEN SAY \"big\"\nELSE SAY \"small\"",
          "IF/ELSE variable");
    equiv(env,
          "x = 5\n"
          "IF x = 1 THEN SAY \"one\"\n"
          "ELSE IF x = 5 THEN SAY \"five\"\n"
          "ELSE SAY \"other\"",
          "IF/ELSE IF chain");
}

/* ------------------------------------------------------------------ */
/*  DO block in IF                                                    */
/* ------------------------------------------------------------------ */

static void test_do_block(struct envblock *env)
{
    printf("\n[DO block]\n");
    equiv(env,
          "IF 1 THEN DO\n  SAY \"a\"\n  SAY \"b\"\nEND",
          "IF THEN DO block");
    equiv(env,
          "IF 0 THEN DO\n  SAY \"skip\"\nEND\nELSE DO\n  SAY \"else\"\nEND",
          "IF/ELSE DO blocks");
}

/* ------------------------------------------------------------------ */
/*  DO FOREVER / DO count                                             */
/* ------------------------------------------------------------------ */

static void test_do_simple(struct envblock *env)
{
    printf("\n[DO count / FOREVER]\n");
    equiv(env,
          "DO 3\n  SAY \"x\"\nEND",
          "DO 3");
    equiv(env,
          "DO 0\n  SAY \"skip\"\nEND\nSAY \"after\"",
          "DO 0 skips body");
    equiv(env,
          "x = \"\"\nDO 4\n  x = x || \"*\"\nEND\nSAY x",
          "DO count concat");
    equiv(env,
          "i = 0\nDO FOREVER\n  i = i + 1\n  IF i = 3 THEN LEAVE\nEND\nSAY i",
          "DO FOREVER with LEAVE");
}

/* ------------------------------------------------------------------ */
/*  DO WHILE / DO UNTIL                                               */
/* ------------------------------------------------------------------ */

static void test_do_while_until(struct envblock *env)
{
    printf("\n[DO WHILE / UNTIL]\n");
    equiv(env,
          "i = 0\nDO WHILE i < 3\n  i = i + 1\nEND\nSAY i",
          "DO WHILE");
    equiv(env,
          "DO WHILE 0\n  SAY \"skip\"\nEND\nSAY \"after\"",
          "DO WHILE false skips body");
    equiv(env,
          "i = 0\nDO UNTIL i >= 3\n  i = i + 1\nEND\nSAY i",
          "DO UNTIL");
    equiv(env,
          "DO UNTIL 1\n  SAY \"once\"\nEND",
          "DO UNTIL true executes once");
}

/* ------------------------------------------------------------------ */
/*  DO TO / BY                                                        */
/* ------------------------------------------------------------------ */

static void test_do_to(struct envblock *env)
{
    printf("\n[DO TO / BY]\n");
    equiv(env,
          "DO i = 1 TO 3\n  SAY i\nEND",
          "DO TO basic");
    equiv(env,
          "DO i = 1 TO 5 BY 2\n  SAY i\nEND",
          "DO TO BY 2");
    equiv(env,
          "DO i = 5 TO 1 BY -1\n  SAY i\nEND",
          "DO TO count down");
    equiv(env,
          "DO i = 1 TO 0\n  SAY \"skip\"\nEND\nSAY \"after\"",
          "DO TO empty range");
    equiv(env,
          "n = 0\nDO i = 1 TO 5\n  n = n + i\nEND\nSAY n",
          "DO TO sum 1..5");
}

/* ------------------------------------------------------------------ */
/*  ITERATE / LEAVE                                                   */
/* ------------------------------------------------------------------ */

static void test_iterate_leave(struct envblock *env)
{
    printf("\n[ITERATE / LEAVE]\n");
    equiv(env,
          "DO i = 1 TO 5\n  IF i = 3 THEN ITERATE\n  SAY i\nEND",
          "ITERATE skips middle element");
    equiv(env,
          "DO i = 1 TO 5\n  IF i = 3 THEN LEAVE\n  SAY i\nEND",
          "LEAVE exits at 3");
    equiv(env,
          "i = 0\nDO WHILE i < 10\n  i = i + 1\n"
          "  IF i = 3 THEN ITERATE\n  SAY i\n"
          "  IF i = 5 THEN LEAVE\nEND",
          "WHILE ITERATE+LEAVE");
}

/* ------------------------------------------------------------------ */
/*  Nested DO                                                         */
/* ------------------------------------------------------------------ */

static void test_nested_do(struct envblock *env)
{
    printf("\n[nested DO]\n");
    equiv(env,
          "DO i = 1 TO 2\n  DO j = 1 TO 2\n    SAY i || j\n  END\nEND",
          "nested DO TO 2x2");
    equiv(env,
          "DO i = 1 TO 3\n  IF i = 2 THEN DO\n"
          "    DO j = 1 TO 2\n      SAY j\n    END\n  END\nEND",
          "nested DO in IF");
}

/* ------------------------------------------------------------------ */
/*  SELECT                                                            */
/* ------------------------------------------------------------------ */

static void test_select(struct envblock *env)
{
    printf("\n[SELECT]\n");
    equiv(env,
          "SELECT\n  WHEN 1 = 1 THEN SAY \"yes\"\nEND",
          "SELECT first WHEN true");
    equiv(env,
          "x = 2\n"
          "SELECT\n"
          "  WHEN x = 1 THEN SAY \"one\"\n"
          "  WHEN x = 2 THEN SAY \"two\"\n"
          "  WHEN x = 3 THEN SAY \"three\"\n"
          "END",
          "SELECT match second WHEN");
    equiv(env,
          "x = 9\n"
          "SELECT\n"
          "  WHEN x = 1 THEN SAY \"one\"\n"
          "  WHEN x = 2 THEN SAY \"two\"\n"
          "  OTHERWISE SAY \"other\"\n"
          "END",
          "SELECT OTHERWISE");
    equiv(env,
          "SELECT\n"
          "  WHEN 0 THEN SAY \"no\"\n"
          "  OTHERWISE SAY \"catch\"\n"
          "END",
          "SELECT no WHEN matches, OTHERWISE");
}

/* ------------------------------------------------------------------ */
/*  irx_bc_disasm smoke test                                          */
/* ------------------------------------------------------------------ */

static void test_disasm(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    char buf[2048];
    int rc, n;

    printf("\n[disasm]\n");

    rc = irx_bc_compile(env, "SAY \"hi\"", (int)strlen("SAY \"hi\""), &bc, NULL, NULL);
    CHECK(rc == IRXBC_OK, "compile SAY for disasm");
    if (bc != NULL)
    {
        n = irx_bc_disasm(bc, buf, (int)sizeof(buf));
        CHECK(n > 0, "irx_bc_disasm returns > 0 chars");
        CHECK(strstr(buf, "SAY") != NULL, "disasm output contains SAY");
        CHECK(strstr(buf, "PUSH_LIT") != NULL, "disasm output contains PUSH_LIT");
        {
            void *p = bc;
            irxstor(RXSMFRE, 0, &p, env);
        }
    }

    /* NULL container returns -1 */
    n = irx_bc_disasm(NULL, buf, (int)sizeof(buf));
    CHECK(n == -1, "disasm(NULL) == -1");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-03 Bytecode Control-Flow Equivalence Tests ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbctl: irxinit failed rc=%d\n", rc);
        return 1;
    }

    /* Install capture I/O routine so SAY output goes to our buffer. */
    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_say(env);
    test_if(env);
    test_do_block(env);
    test_do_simple(env);
    test_do_while_until(env);
    test_do_to(env);
    test_iterate_leave(env);
    test_nested_do(env);
    test_select(env);
    test_disasm(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
