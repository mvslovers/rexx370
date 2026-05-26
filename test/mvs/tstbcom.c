/* ------------------------------------------------------------------ */
/*  tstbcom.c - WP-BC-05 PR C: Compound Variable Tests                */
/*                                                                    */
/*  Tests OP_LOAD_STEM / OP_STORE_STEM / OP_DROP_STEM in the         */
/*  bytecode compiler and VM.  Uses bc_only() for tests where the     */
/*  token-walk interpreter behaviour matches, and equiv() for         */
/*  cross-validation.                                                 */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c */
/*          ../lstring370/src/lstr#xlt.c"                             */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbcom test/mvs/tstbcom.c \                        */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   $LSRC && /tmp/tstbcom      */
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
/*  bc_only: run only via bytecode VM; check expected output.         */
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
/*  equiv: run via token-walk and bytecode; check outputs match.      */
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
/*  Basic compound read / write                                        */
/* ------------------------------------------------------------------ */

static void test_compound_basic(struct envblock *env)
{
    printf("\n[Compound variable — basic read/write]\n");

    /* Constant tail: A.1 = "one"; say A.1 */
    equiv(env,
          "A.1 = \"one\"\nSAY A.1\n",
          "A.1 constant tail store and load");

    /* Variable tail: I = 1; A.I = "val"; say A.I */
    equiv(env,
          "I = 1\nA.I = \"val\"\nSAY A.I\n",
          "A.I variable tail store and load");

    /* NOVALUE: unset compound returns compound name */
    bc_only(env,
            "I = \"X\"\nSAY A.I\n",
            "A.X\n",
            "NOVALUE returns compound name A.X");

    /* NOVALUE: unset A.1 returns "A.1" */
    bc_only(env,
            "SAY A.1\n",
            "A.1\n",
            "NOVALUE constant tail returns A.1");

    /* Stem default: A. = \"miss\"; I unset → A.I → \"miss\" */
    bc_only(env,
            "A. = \"miss\"\nI = \"X\"\nSAY A.I\n",
            "miss\n",
            "stem default used when entry absent");

    /* Explicit entry overrides stem default */
    bc_only(env,
            "A. = \"miss\"\nI = \"X\"\nA.X = \"found\"\nSAY A.I\n",
            "found\n",
            "explicit entry overrides stem default");

    /* Bare stem read: A. = \"stemval\"; say A. */
    equiv(env,
          "A. = \"stemval\"\nSAY A.\n",
          "bare stem read");

    /* Bare stem write via compound assignment */
    bc_only(env,
            "A. = \"default\"\nSAY A.\n",
            "default\n",
            "bare stem write A. = value");
}

/* ------------------------------------------------------------------ */
/*  Multi-level compound variables                                     */
/* ------------------------------------------------------------------ */

static void test_compound_multilevel(struct envblock *env)
{
    printf("\n[Compound variable — multi-level]\n");

    /* A.B.C with all variable tails */
    equiv(env,
          "I = 1\nJ = 2\nA.I.J = \"nested\"\nSAY A.I.J\n",
          "A.I.J two variable tails store/load");

    /* A.1.2 constant tails */
    bc_only(env,
            "A.1.2 = \"deep\"\nSAY A.1.2\n",
            "deep\n",
            "A.1.2 constant tails store/load");

    /* Mixed: A.1.J */
    bc_only(env,
            "J = \"Y\"\nA.1.Y = \"mix\"\nSAY A.1.J\n",
            "mix\n",
            "A.1.J mixed constant and variable tail");

    /* Three-level: A.I.J.K */
    bc_only(env,
            "I = \"X\"\nJ = \"Y\"\nK = \"Z\"\nA.X.Y.Z = \"three\"\nSAY A.I.J.K\n",
            "three\n",
            "A.I.J.K three variable tails");

    /* Tail value uppercased: B = \"hello\"; A.B → A.HELLO */
    bc_only(env,
            "B = \"hello\"\nA.HELLO = \"up\"\nSAY A.B\n",
            "up\n",
            "tail value uppercased for lookup");

    /* NOVALUE multi-level */
    bc_only(env,
            "I = 1\nJ = 2\nSAY A.I.J\n",
            "A.1.2\n",
            "NOVALUE multi-level returns compound name");
}

/* ------------------------------------------------------------------ */
/*  Stem default fallback chain                                        */
/* ------------------------------------------------------------------ */

static void test_compound_stem_default(struct envblock *env)
{
    printf("\n[Compound variable — stem default]\n");

    /* Default read: A. = \"def\"; various tails → \"def\" */
    bc_only(env,
            "A. = \"def\"\nSAY A.1\nSAY A.X\nSAY A.1.2\n",
            "def\ndef\ndef\n",
            "stem default returned for all unset tails");

    /* Default does not apply to set tails */
    bc_only(env,
            "A. = \"def\"\nA.2 = \"two\"\nSAY A.1\nSAY A.2\n",
            "def\ntwo\n",
            "stem default does not override explicitly set tail");

    /* Chained indirect: J = I; A.J should resolve via J's value */
    bc_only(env,
            "I = 1\nJ = I\nA.1 = \"ok\"\nSAY A.J\n",
            "ok\n",
            "variable tail resolves via J = I = 1");
}

/* ------------------------------------------------------------------ */
/*  DROP statement                                                     */
/* ------------------------------------------------------------------ */

static void test_compound_drop(struct envblock *env)
{
    printf("\n[Compound variable — DROP]\n");

    /* DROP simple variable */
    bc_only(env,
            "X = \"hello\"\nDROP X\nSAY X\n",
            "X\n",
            "DROP simple variable → NOVALUE");

    /* DROP specific compound entry: DROP A.1 */
    bc_only(env,
            "A.1 = \"one\"\nDROP A.1\nSAY A.1\n",
            "A.1\n",
            "DROP A.1 → entry removed, NOVALUE");

    /* DROP A.I where I is a variable */
    bc_only(env,
            "I = 1\nA.1 = \"set\"\nDROP A.I\nSAY A.1\n",
            "A.1\n",
            "DROP A.I (I=1) removes A.1");

    /* DROP STEM. removes all entries */
    bc_only(env,
            "A.1 = \"one\"\nA.2 = \"two\"\nA. = \"def\"\nDROP A.\nSAY A.1\n",
            "A.1\n",
            "DROP A. removes all stem entries");

    /* After DROP STEM., stem default also gone */
    bc_only(env,
            "A. = \"def\"\nDROP A.\nSAY A.X\n",
            "A.X\n",
            "DROP A. removes stem default too");

    /* DROP multiple variables in one statement */
    bc_only(env,
            "X = \"x\"\nY = \"y\"\nDROP X Y\nSAY X\nSAY Y\n",
            "X\nY\n",
            "DROP multiple simple variables");

    /* DROP mix: simple and compound in one statement */
    bc_only(env,
            "X = \"x\"\nA.1 = \"one\"\nDROP X A.1\nSAY X\nSAY A.1\n",
            "X\nA.1\n",
            "DROP mix simple and compound");
}

/* ------------------------------------------------------------------ */
/*  Compound variables in expressions                                  */
/* ------------------------------------------------------------------ */

static void test_compound_expr(struct envblock *env)
{
    printf("\n[Compound variable — in expressions]\n");

    /* Arithmetic with compound variable */
    bc_only(env,
            "A.1 = 10\nA.2 = 20\nSAY A.1 + A.2\n",
            "30\n",
            "A.1 + A.2 arithmetic");

    /* Compound in IF condition */
    bc_only(env,
            "I = 1\nA.I = \"yes\"\nIF A.I = \"yes\" THEN SAY \"ok\"\n",
            "ok\n",
            "compound in IF condition");

    /* Compound as SAY target without assignment */
    bc_only(env,
            "A.3 = \"three\"\nI = 3\nSAY A.I\n",
            "three\n",
            "SAY A.I where I=3 and A.3 set");

    /* Each tail is resolved independently — no chained compound lookup.
     * B.A.I → B.(val A).(val I); A unset → "A", I=2 → name "B.A.2". */
    bc_only(env,
            "I = 2\nSAY B.A.I\n",
            "B.A.2\n",
            "B.A.I tails resolved independently (no chaining)");

    /* Concatenation with compound */
    equiv(env,
          "I = 1\nA.1 = \"val\"\nSAY \"result=\" || A.I\n",
          "compound in concat expression");
}

/* ------------------------------------------------------------------ */
/*  Compound variables in DO loops                                     */
/* ------------------------------------------------------------------ */

static void test_compound_do(struct envblock *env)
{
    printf("\n[Compound variable — in DO loops]\n");

    /* Build array via compound assignment in loop */
    bc_only(env,
            "DO I = 1 TO 3\n"
            "  A.I = I * 10\n"
            "END\n"
            "SAY A.1\nSAY A.2\nSAY A.3\n",
            "10\n20\n30\n",
            "compound array built in DO loop");

    /* Traverse array via compound read in loop */
    bc_only(env,
            "A.1 = \"one\"\nA.2 = \"two\"\nA.3 = \"three\"\n"
            "DO I = 1 TO 3\n"
            "  SAY A.I\n"
            "END\n",
            "one\ntwo\nthree\n",
            "compound array traversed in DO loop");
}

/* ------------------------------------------------------------------ */
/*  Compound variables across CALL/RETURN                              */
/* ------------------------------------------------------------------ */

static void test_compound_call(struct envblock *env)
{
    printf("\n[Compound variable — across CALL/RETURN]\n");

    /* Caller sets A.1; callee reads A.1 (shared vpool without PROCEDURE) */
    bc_only(env,
            "A.1 = \"caller\"\n"
            "CALL sub\n"
            "EXIT\n"
            "sub:\n"
            "SAY A.1\n"
            "RETURN\n",
            "caller\n",
            "callee reads caller compound variable");

    /* PROCEDURE: callee cannot see caller's compound without EXPOSE */
    bc_only(env,
            "A.1 = \"caller\"\n"
            "CALL sub\n"
            "EXIT\n"
            "sub:\n"
            "PROCEDURE\n"
            "SAY A.1\n"
            "RETURN\n",
            "A.1\n",
            "PROCEDURE isolates compound variable");

    /* PROCEDURE EXPOSE A.: callee sees all of stem A */
    bc_only(env,
            "A.1 = \"hello\"\nA.2 = \"world\"\n"
            "CALL sub\n"
            "EXIT\n"
            "sub:\n"
            "PROCEDURE EXPOSE A.\n"
            "SAY A.1\nSAY A.2\n"
            "RETURN\n",
            "hello\nworld\n",
            "PROCEDURE EXPOSE A. exposes all of stem");

    /* PROCEDURE EXPOSE A.: callee can modify stem default */
    bc_only(env,
            "A.1 = \"orig\"\n"
            "CALL sub\n"
            "SAY A.1\n"
            "EXIT\n"
            "sub:\n"
            "PROCEDURE EXPOSE A.\n"
            "A.1 = \"modified\"\n"
            "RETURN\n",
            "modified\n",
            "PROCEDURE EXPOSE A. callee modification visible to caller");
}

/* ------------------------------------------------------------------ */
/*  PARSE compound-target — must still be rejected (deferred to PR D) */
/* ------------------------------------------------------------------ */

static void test_parse_compound_still_rejected(struct envblock *env)
{
    struct irx_wkblk_int *wk;
    int src_len;
    int rc;
    int exit_rc = 0;
    const char *src = "PARSE ARG a.1\nSAY a.1\n";

    printf("\n[PARSE compound-target — still rejected (deferred)]\n");

    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        printf("  FAIL: no work block\n");
        tests_run++;
        tests_failed++;
        return;
    }

    src_len = (int)strlen(src);
    cap_reset();
    wk->wkbi_use_bytecode = 1;
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;

    CHECK(rc != 0, "PARSE compound target still rejected (IRXBC_ERR_PARSE_COMPOUND)");
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-05 PR C: Compound Variable Tests ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbcom: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_compound_basic(env);
    test_compound_multilevel(env);
    test_compound_stem_default(env);
    test_compound_drop(env);
    test_compound_expr(env);
    test_compound_do(env);
    test_compound_call(env);
    test_parse_compound_still_rejected(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
