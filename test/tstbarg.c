/* ------------------------------------------------------------------ */
/*  tstbarg.c - WP-BC-ARGOMIT: omitted function arguments f(a,,b)      */
/*                                                                    */
/*  Before WP-BC-ARGOMIT the bytecode expression compiler called      */
/*  bc_exp0() unconditionally for every argument slot, so an omitted   */
/*  argument (the empty slot in f(a,,b), f(,b), f(a,), or             */
/*  CALL f a,,c) failed with BC_UNSUP_EXPR_OPERAND and forced the      */
/*  whole program onto the token-walk fallback.  The compiler now      */
/*  emits OP_PUSH_OMITTED for an omitted slot; the VM delivers it as   */
/*  a non-NULL empty Lstr to a BIF (token-walk parity) and as a        */
/*  non-existent argument (arg_exists=0) to an internal routine.       */
/*                                                                    */
/*  This test verifies that:                                          */
/*    1. omitted args at every position (leading / middle / trailing) */
/*       compile and RUN on the bytecode path WITHOUT falling back;    */
/*    2. omitted args produce output identical to the token-walk       */
/*       interpreter (the frozen equivalence reference, CON-18);       */
/*    3. omitted != empty string: ARG(n,'O')/ARG(n,'E') see an         */
/*       omitted argument as omitted (f(a,,b)), but a present empty    */
/*       string (f(a,'',b)) as present;                                */
/*    4. format(total,,1) — the REXXCPS line-166 blocker — compiles    */
/*       on the bytecode path.                                         */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c */
/*          ../lstring370/src/lstr#xlt.c"                             */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbarg test/mvs/tstbarg.c \                        */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c \           */
/*        $LSRC && /tmp/tstbarg                                       */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
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
/*  equiv: run src via token-walk, then via bytecode, check that      */
/*  SAY output matches.  This is the equivalence proof against the     */
/*  frozen token-walk interpreter (CON-18).                           */
/* ------------------------------------------------------------------ */

static int equiv(struct envblock *env, const char *src, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int exit_rc = 0;
    char tw_out[CAPBUF_SIZE];
    char bc_out[CAPBUF_SIZE];
    char label[160];

    wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
    if (wk == NULL)
    {
        printf("  FAIL: %s — no work block\n", tag);
        tests_run++;
        tests_failed++;
        return 0;
    }

    cap_reset();
    wk->wkbi_use_bytecode = 0;
    (void)irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    memcpy(tw_out, g_cap, (size_t)(g_cap_len + 1));

    cap_reset();
    wk->wkbi_use_bytecode = 1;
    (void)irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
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
/*  no_fallback: run src via the bytecode path and verify both that   */
/*  the SAY output matches `expected` AND that the program compiled   */
/*  and ran on the bytecode path without any token-walk fallback.     */
/* ------------------------------------------------------------------ */

static int no_fallback(struct envblock *env, const char *src,
                       const char *expected, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int exit_rc = 0;
    char label[160];

    wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
    if (wk == NULL)
    {
        printf("  FAIL: %s — no work block\n", tag);
        tests_run++;
        tests_failed++;
        return 0;
    }

    cap_reset();
    wk->wkbi_use_bytecode = 1;
    wk->wkbi_bc_exec_count = 0;
    wk->wkbi_bc_fallback_count = 0;
    (void)irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;

    snprintf(label, sizeof(label), "no-fallback: %s", tag);
    CHECK(strcmp(g_cap, expected) == 0 &&
              wk->wkbi_bc_exec_count > 0 &&
              wk->wkbi_bc_fallback_count == 0,
          label);

    if (strcmp(g_cap, expected) != 0 ||
        wk->wkbi_bc_exec_count == 0 ||
        wk->wkbi_bc_fallback_count != 0)
    {
        printf("    expected:[%s] exec=%d fallback=%d\n",
               expected, wk->wkbi_bc_exec_count, wk->wkbi_bc_fallback_count);
        printf("    got:     [%s]\n", g_cap);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  ran_bc: confirm the program compiled and ran on the bytecode path  */
/*  (exec>0, fallback==0) WITHOUT checking the exact output.  Used for */
/*  BIF cases (e.g. FORMAT) where output correctness is already        */
/*  established by equiv() and only the no-fallback property is left   */
/*  to prove.                                                          */
/* ------------------------------------------------------------------ */

static int ran_bc(struct envblock *env, const char *src, const char *tag)
{
    struct irx_wkblk_int *wk;
    int src_len = (int)strlen(src);
    int exit_rc = 0;
    char label[160];

    wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
    if (wk == NULL)
    {
        printf("  FAIL: %s — no work block\n", tag);
        tests_run++;
        tests_failed++;
        return 0;
    }

    cap_reset();
    wk->wkbi_use_bytecode = 1;
    wk->wkbi_bc_exec_count = 0;
    wk->wkbi_bc_fallback_count = 0;
    (void)irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;

    snprintf(label, sizeof(label), "ran-on-bytecode: %s", tag);
    CHECK(wk->wkbi_bc_exec_count > 0 && wk->wkbi_bc_fallback_count == 0,
          label);

    if (wk->wkbi_bc_exec_count == 0 || wk->wkbi_bc_fallback_count != 0)
    {
        printf("    exec=%d fallback=%d  out=[%s]\n",
               wk->wkbi_bc_exec_count, wk->wkbi_bc_fallback_count, g_cap);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  omitted != empty string, via a CALL statement to an internal      */
/*  routine.  This is the discriminating proof: ARG(n,'O') reports an  */
/*  omitted argument as omitted, but a present empty string ('') as    */
/*  present.  The CALL form is used (not the expression form) because  */
/*  the token-walk interpreter only resolves internal routines through */
/*  CALL, so equiv() against token-walk is valid here.                 */
/* ------------------------------------------------------------------ */

static void test_omitted_vs_empty_call(struct envblock *env)
{
    printf("\n[omitted != empty string — CALL to internal routine]\n");

    /* Leading omitted arg 1 -> ARG(1,'O')=1 ARG(1,'E')=0 -> "1 0". */
    equiv(env,
          "call sub ,'x'\nexit\n"
          "sub: say arg(1,'o') arg(1,'e'); return",
          "CALL leading omitted arg");
    no_fallback(env,
                "call sub ,'x'\nexit\n"
                "sub: say arg(1,'o') arg(1,'e'); return",
                "1 0\n",
                "CALL leading omitted arg -> omitted");

    /* Present empty string arg 1 -> ARG(1,'O')=0 ARG(1,'E')=1 -> "0 1". */
    equiv(env,
          "call sub '','x'\nexit\n"
          "sub: say arg(1,'o') arg(1,'e'); return",
          "CALL empty-string arg 1");
    no_fallback(env,
                "call sub '','x'\nexit\n"
                "sub: say arg(1,'o') arg(1,'e'); return",
                "0 1\n",
                "CALL empty-string arg 1 -> present");

    /* Middle omitted arg 2 -> "1 0" for arg 2. */
    equiv(env,
          "call sub 'a',,'c'\nexit\n"
          "sub: say arg(2,'o') arg(2,'e'); return",
          "CALL middle omitted arg");
    no_fallback(env,
                "call sub 'a',,'c'\nexit\n"
                "sub: say arg(2,'o') arg(2,'e'); return",
                "1 0\n",
                "CALL middle omitted arg -> omitted");

    /* Trailing comma -> one final omitted arg 2 -> "1 0" for arg 2.
     * The clause is ended with ';' (not a bare newline): a trailing
     * comma at end of LINE is REXX line continuation, whereas ',;'
     * keeps the comma as an argument separator followed by a real
     * clause end — the genuine trailing-omitted case. */
    equiv(env,
          "call sub 'a',; exit\n"
          "sub: say arg(2,'o') arg(2,'e'); return",
          "CALL trailing omitted arg");
    no_fallback(env,
                "call sub 'a',; exit\n"
                "sub: say arg(2,'o') arg(2,'e'); return",
                "1 0\n",
                "CALL trailing omitted arg -> omitted");

    /* A present empty string in the middle stays present. */
    equiv(env,
          "call sub 'a','','c'\nexit\n"
          "sub: say arg(2,'o') arg(2,'e'); return",
          "CALL middle empty-string arg");
    no_fallback(env,
                "call sub 'a','','c'\nexit\n"
                "sub: say arg(2,'o') arg(2,'e'); return",
                "0 1\n",
                "CALL middle empty-string arg -> present");

    /* arg count includes the omitted slots (ARG() with no option). */
    equiv(env,
          "call sub 'a',,'c'\nexit\n"
          "sub: say arg(); return",
          "CALL arg() count includes omitted slot");
    no_fallback(env,
                "call sub 'a',,'c'\nexit\n"
                "sub: say arg(); return",
                "3\n",
                "CALL arg() count = 3 with middle omitted");
}

/* ------------------------------------------------------------------ */
/*  PARSE ARG must read the omitted-argument flag the same way as the  */
/*  ARG() BIF: an omitted argument parses as an empty string (matching */
/*  the token-walk).  This exercises the bytecode PARSE ARG path with  */
/*  an omitted argument, confirming it consults arg_exists rather than */
/*  reading a stale slot.                                              */
/*                                                                    */
/*  The result is delimited with explicit `||` concatenation, not      */
/*  abuttal: zero-space abuttal of a string and a symbol has a known   */
/*  blank-insertion divergence on the bytecode path (WP-BC-09), so     */
/*  using `||` keeps this test focused on the omitted-argument flag.   */
/* ------------------------------------------------------------------ */

static void test_omitted_parse_arg(struct envblock *env)
{
    printf("\n[omitted args — PARSE ARG]\n");

    /* Omitted arg 1 -> PARSE ARG a yields empty -> "<>". */
    equiv(env,
          "call sub ,'y'\nexit\n"
          "sub: parse arg a; say '<' || a || '>'; return",
          "PARSE ARG omitted arg 1");
    no_fallback(env,
                "call sub ,'y'\nexit\n"
                "sub: parse arg a; say '<' || a || '>'; return",
                "<>\n",
                "PARSE ARG omitted arg 1 -> empty");

    /* Present arg 1 -> PARSE ARG a yields the value -> "<hi>". */
    equiv(env,
          "call sub 'hi','y'\nexit\n"
          "sub: parse arg a; say '<' || a || '>'; return",
          "PARSE ARG present arg 1");
    no_fallback(env,
                "call sub 'hi','y'\nexit\n"
                "sub: parse arg a; say '<' || a || '>'; return",
                "<hi>\n",
                "PARSE ARG present arg 1 -> value");
}

/* ------------------------------------------------------------------ */
/*  omitted != empty string, via an EXPRESSION call to an internal    */
/*  routine.  The token-walk interpreter rejects expression-context    */
/*  internal-function calls (parse_function_call -> bif_dispatch ->    */
/*  BADFUNC), so this can only be checked on the bytecode path, where  */
/*  OP_CALL_BIF resolves the label and runs the routine.  no_fallback  */
/*  validates output AND the no-fallback property without comparing to */
/*  the token-walk.                                                    */
/* ------------------------------------------------------------------ */

static void test_omitted_expr_userfunc(struct envblock *env)
{
    printf("\n[omitted args — expression call to internal routine]\n");

    no_fallback(env,
                "y = rep(,'b')\nsay y\nexit\n"
                "rep: return arg(1,'o') arg(1,'e')",
                "1 0\n",
                "expr leading omitted -> omitted");

    no_fallback(env,
                "y = rep('a',,'c')\nsay y\nexit\n"
                "rep: return arg(2,'o') arg(2,'e')",
                "1 0\n",
                "expr middle omitted -> omitted");

    no_fallback(env,
                "y = rep('a',)\nsay y\nexit\n"
                "rep: return arg(2,'o') arg(2,'e')",
                "1 0\n",
                "expr trailing omitted -> omitted");

    no_fallback(env,
                "y = rep('a','','c')\nsay y\nexit\n"
                "rep: return arg(2,'o') arg(2,'e')",
                "0 1\n",
                "expr middle empty-string -> present");
}

/* ------------------------------------------------------------------ */
/*  omitted args to a BIF — the FORMAT(total,,1) REXXCPS line-166      */
/*  blocker, plus other positions.  For a BIF the omitted argument is  */
/*  delivered as a non-NULL empty Lstr, identical to the token-walk,   */
/*  so the optional-argument helpers apply their defaults.  equiv()    */
/*  proves output parity; ran_bc() proves the no-fallback property.    */
/* ------------------------------------------------------------------ */

static void test_omitted_bif(struct envblock *env)
{
    printf("\n[omitted args to a BIF — FORMAT and friends]\n");

    /* The literal REXXCPS-166 construct. */
    equiv(env, "total = 3\nsay format(total,,1)",
          "format(total,,1) — REXXCPS blocker");
    ran_bc(env, "total = 3\nsay format(total,,1)",
           "format(total,,1) compiles on bytecode path");

    /* Trailing omitted BIF arg. */
    equiv(env, "total = 3\nsay format(total,4,)",
          "format(total,4,) trailing omitted");
    ran_bc(env, "total = 3\nsay format(total,4,)",
           "format(total,4,) compiles on bytecode path");

    /* Two consecutive omitted BIF args. */
    equiv(env, "total = 3\nsay format(total,,,2)",
          "format(total,,,2) two omitted then present");
    ran_bc(env, "total = 3\nsay format(total,,,2)",
           "format(total,,,2) compiles on bytecode path");

    /* A present empty string vs an omitted arg behave the same for a  */
    /* BIF (both default), so these two stay equiv to the token-walk.  */
    equiv(env, "total = 3\nsay format(total,'',1)",
          "format(total,'',1) empty-string arg = default");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-ARGOMIT: omitted function arguments f(a,,b) ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbarg: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_omitted_vs_empty_call(env);
    test_omitted_parse_arg(env);
    test_omitted_expr_userfunc(env);
    test_omitted_bif(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
