/* ------------------------------------------------------------------ */
/*  test/host/tstbc_expr.c — WP-BC-02 expression + assignment tests   */
/*                                                                    */
/*  Two test modes:                                                   */
/*    1. Compile-only: check bytecode structure for known snippets.   */
/*    2. Equivalence: run the same program via token-walk and via the  */
/*       bytecode VM; compare exit codes.  A program ending with      */
/*       EXIT <expr> is used as the observable output.               */
/*                                                                    */
/*  Build (Linux):                                                    */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -O0 -g \                           */
/*        -o /tmp/tstbc_expr test/host/tstbc_expr.c \                 */
/*        'src/irx#init.c'  'src/irx#term.c'  'src/irx#stor.c' \    */
/*        'src/irx#anch.c'  'src/irx#env.c'   'src/irx#uid.c'  \    */
/*        'src/irx#msid.c'  'src/irx#cond.c'  'src/irx#bif.c'  \    */
/*        'src/irx#bifs.c'  'src/irx#io.c'    'src/irx#lstr.c' \    */
/*        'src/irx#tokn.c'  'src/irx#vpol.c'  'src/irx#pars.c' \    */
/*        'src/irx#ctrl.c'  'src/irx#exec.c'  'src/irx#arith.c' \   */
/*        'src/irx#bcom.c'  'src/irx#bvm.c' \                        */
/*        '../lstring370/src/lstr#cor.c' \                            */
/*        '../lstring370/src/lstr#cvt.c' \                            */
/*        '../lstring370/src/lstr#fmt.c' \                            */
/*        '../lstring370/src/lstr#srch.c' \                           */
/*        '../lstring370/src/lstr#sub.c' \                            */
/*        '../lstring370/src/lstr#wrd.c' \                            */
/*        '../lstring370/src/lstr#xlt.c'                              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxbops.h"
#include "irxbvm.h"
#include "irxexbl.h"
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
/*  Run src through both interpreter paths; compare exit codes.       */
/*  expected_tw and expected_bc are the expected exit codes.          */
/* ------------------------------------------------------------------ */
static void run_both(const char *desc, const char *src,
                     int expected_tw, int expected_bc)
{
    struct envblock *env = NULL;
    struct irx_wkblk_int *wk;
    int tw_rc = -999;
    int bc_rc = -999;
    int ok;
    char msg[80];
    int src_len = (int)strlen(src);

    printf("  [%s]\n", desc);

    /* token-walk */
    ok = (irxinit(NULL, &env) == 0 && env != NULL);
    if (ok)
    {
        wk = (struct irx_wkblk_int *)env->envblock_userfield;
        if (wk != NULL)
        {
            wk->wkbi_use_bytecode = 0;
        }
        ok = (irx_exec_run(src, src_len, NULL, 0, &tw_rc, env) == 0);
        irxterm(env);
        env = NULL;
    }
    if (!ok)
    {
        tw_rc = -1;
    }

    /* bytecode */
    ok = (irxinit(NULL, &env) == 0 && env != NULL);
    if (ok)
    {
        wk = (struct irx_wkblk_int *)env->envblock_userfield;
        if (wk != NULL)
        {
            wk->wkbi_use_bytecode = 1;
        }
        ok = (irx_exec_run(src, src_len, NULL, 0, &bc_rc, env) == 0);
        irxterm(env);
        env = NULL;
    }
    if (!ok)
    {
        bc_rc = -1;
    }

    snprintf(msg, sizeof(msg), "%s: token-walk rc==%d", desc, expected_tw);
    CHECK(tw_rc == expected_tw, msg);
    snprintf(msg, sizeof(msg), "%s: bytecode rc==%d", desc, expected_bc);
    CHECK(bc_rc == expected_bc, msg);
    snprintf(msg, sizeof(msg), "%s: bytecode==token-walk", desc);
    CHECK(bc_rc == tw_rc, msg);
}

/* Both paths should agree; use when only one expected value is needed. */
#define EQUIV(desc, src, expected) \
    run_both(desc, src, expected, expected)

/* ------------------------------------------------------------------ */
/*  Compile-only structural checks                                    */
/* ------------------------------------------------------------------ */
static void test_compile_assignment(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    const unsigned char *code;
    int rc;

    printf("  [compile: x = 5]\n");

    rc = irx_bc_compile(env, "x = 5", (int)strlen("x = 5"), &bc, NULL, NULL);
    CHECK(rc == IRXBC_OK, "compile returns IRXBC_OK");
    CHECK(bc != NULL, "bc pointer is non-NULL");

    if (bc == NULL)
    {
        return;
    }

    CHECK(bc->const_count == 1, "const_count == 1");
    CHECK(bc->symbol_count == 1, "symbol_count == 1");

    code = IRXBC_CODE(bc);
    /* OP_NEWCLAUSE, OP_PUSH_LIT idx:u16, OP_STORE idx:u16, OP_EXIT */
    CHECK(code[0] == OP_NEWCLAUSE, "code[0]==OP_NEWCLAUSE");
    CHECK(code[1] == OP_PUSH_LIT, "code[1]==OP_PUSH_LIT");
    CHECK(code[2] == 0, "code[2]==0 (const idx lo)");
    CHECK(code[3] == 0, "code[3]==0 (const idx hi)");
    CHECK(code[4] == OP_STORE, "code[4]==OP_STORE");
    CHECK(code[5] == 0, "code[5]==0 (sym idx lo)");
    CHECK(code[6] == 0, "code[6]==0 (sym idx hi)");
    CHECK(code[7] == OP_EXIT, "code[7]==OP_EXIT");
    CHECK(bc->code_length == 8, "code_length==8");

    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

static void test_compile_expr(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    const unsigned char *code;
    int rc;

    printf("  [compile: x = 3 + 4]\n");

    rc = irx_bc_compile(env, "x = 3 + 4", (int)strlen("x = 3 + 4"), &bc, NULL, NULL);
    CHECK(rc == IRXBC_OK, "compile returns IRXBC_OK");
    CHECK(bc != NULL, "bc pointer is non-NULL");

    if (bc == NULL)
    {
        return;
    }

    CHECK(bc->const_count == 2, "const_count==2 (literals '3','4')");
    CHECK(bc->symbol_count == 1, "symbol_count==1 ('X')");

    code = IRXBC_CODE(bc);
    CHECK(code[0] == OP_NEWCLAUSE, "code[0]==OP_NEWCLAUSE");
    CHECK(code[1] == OP_PUSH_LIT, "code[1]==OP_PUSH_LIT (3)");
    CHECK(code[4] == OP_PUSH_LIT, "code[4]==OP_PUSH_LIT (4)");
    CHECK(code[7] == OP_ADD, "code[7]==OP_ADD");
    CHECK(code[8] == OP_STORE, "code[8]==OP_STORE");
    CHECK(code[11] == OP_EXIT, "code[11]==OP_EXIT");

    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

static void test_compile_exit_rc(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    const unsigned char *code;
    int rc;

    printf("  [compile: EXIT 7]\n");

    rc = irx_bc_compile(env, "EXIT 7", (int)strlen("EXIT 7"), &bc, NULL, NULL);
    CHECK(rc == IRXBC_OK, "compile returns IRXBC_OK");
    CHECK(bc != NULL, "bc pointer is non-NULL");

    if (bc == NULL)
    {
        return;
    }

    CHECK(bc->const_count == 1, "const_count==1 (literal '7')");
    CHECK(bc->symbol_count == 0, "symbol_count==0");

    code = IRXBC_CODE(bc);
    /* OP_NEWCLAUSE, PUSH_LIT 0 0, OP_EXIT_RC */
    CHECK(code[0] == OP_NEWCLAUSE, "code[0]==OP_NEWCLAUSE");
    CHECK(code[1] == OP_PUSH_LIT, "code[1]==OP_PUSH_LIT");
    CHECK(code[4] == OP_EXIT_RC, "code[4]==OP_EXIT_RC");
    CHECK(bc->code_length == 5, "code_length==5");

    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

/* ------------------------------------------------------------------ */
/*  Equivalence tests — use EXIT expr to expose the computed value    */
/* ------------------------------------------------------------------ */

static void test_equiv_literal(void)
{
    EQUIV("exit literal", "EXIT 42", 42);
}

static void test_equiv_add(void)
{
    EQUIV("add", "EXIT 3 + 4", 7);
}

static void test_equiv_sub(void)
{
    EQUIV("sub", "EXIT 10 - 3", 7);
}

static void test_equiv_mul(void)
{
    EQUIV("mul", "EXIT 3 * 4", 12);
}

static void test_equiv_idiv(void)
{
    EQUIV("idiv", "EXIT 10 % 3", 3);
}

static void test_equiv_mod(void)
{
    EQUIV("mod", "EXIT 10 // 3", 1);
}

static void test_equiv_pow(void)
{
    EQUIV("pow", "EXIT 2 ** 8", 256);
}

static void test_equiv_neg(void)
{
    EQUIV("neg", "EXIT -(10 - 3)", -7);
}

static void test_equiv_parens(void)
{
    EQUIV("parens", "EXIT (2 + 3) * 4", 20);
}

static void test_equiv_cmp_eq_true(void)
{
    EQUIV("cmp_eq_true", "EXIT (3 = 3)", 1);
}

static void test_equiv_cmp_eq_false(void)
{
    EQUIV("cmp_eq_false", "EXIT (3 = 4)", 0);
}

static void test_equiv_cmp_gt(void)
{
    EQUIV("cmp_gt", "EXIT (5 > 3)", 1);
}

static void test_equiv_cmp_lt(void)
{
    EQUIV("cmp_lt", "EXIT (3 < 5)", 1);
}

static void test_equiv_cmp_ne(void)
{
    EQUIV("cmp_ne", "EXIT (3 \\= 4)", 1);
}

static void test_equiv_cmp_ge(void)
{
    EQUIV("cmp_ge", "EXIT (3 >= 3)", 1);
}

static void test_equiv_cmp_le(void)
{
    EQUIV("cmp_le", "EXIT (3 <= 3)", 1);
}

static void test_equiv_strict_eq(void)
{
    EQUIV("strict_eq_true", "EXIT ('abc' == 'abc')", 1);
    EQUIV("strict_eq_false", "EXIT ('abc' == 'ABC')", 0);
}

static void test_equiv_logical_and(void)
{
    EQUIV("and_tt", "EXIT (1 & 1)", 1);
    EQUIV("and_tf", "EXIT (1 & 0)", 0);
}

static void test_equiv_logical_or(void)
{
    EQUIV("or_ff", "EXIT (0 | 0)", 0);
    EQUIV("or_tf", "EXIT (1 | 0)", 1);
}

static void test_equiv_logical_xor(void)
{
    EQUIV("xor_tf", "EXIT (0 && 1)", 1);
    EQUIV("xor_tt", "EXIT (1 && 1)", 0);
}

static void test_equiv_logical_not(void)
{
    EQUIV("not_true", "EXIT (\\1)", 0);
    EQUIV("not_false", "EXIT (\\0)", 1);
}

static void test_equiv_var_assign(void)
{
    EQUIV("var_assign", "x = 5\nEXIT x", 5);
}

static void test_equiv_var_arith(void)
{
    EQUIV("var_arith",
          "x = 10\n"
          "y = x - 3\n"
          "EXIT y",
          7);
}

static void test_equiv_two_vars(void)
{
    EQUIV("two_vars",
          "a = 3\n"
          "b = 4\n"
          "EXIT a + b",
          7);
}

static void test_equiv_novalue(void)
{
    /* In REXX: unset variable Y has value "Y" (its name in uppercase).
     * "Y" == "Y" is a strict comparison that returns 1. */
    EQUIV("novalue", "EXIT (y == 'Y')", 1);
}

static void test_equiv_concat(void)
{
    EQUIV("concat_explicit", "EXIT ('hello' || 'world' == 'helloworld')", 1);
    EQUIV("concat_abuttal", "EXIT ('foo' 'bar' == 'foo bar')", 1);
}

static void test_equiv_arith_to_logical(void)
{
    /* Arithmetic result used as boolean operand. */
    EQUIV("arith_to_logical", "EXIT ((1 + 0) & 1)", 1);
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    struct envblock *env = NULL;
    int rc;

    printf("=== WP-BC-02 Expression + Assignment Tests ===\n\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbc_expr: irxinit failed rc=%d\n", rc);
        return 1;
    }

    /* Compile-only structural checks */
    test_compile_assignment(env);
    test_compile_expr(env);
    test_compile_exit_rc(env);

    irxterm(env);

    /* Equivalence harness — each creates its own envblocks */
    test_equiv_literal();
    test_equiv_add();
    test_equiv_sub();
    test_equiv_mul();
    test_equiv_idiv();
    test_equiv_mod();
    test_equiv_pow();
    test_equiv_neg();
    test_equiv_parens();
    test_equiv_cmp_eq_true();
    test_equiv_cmp_eq_false();
    test_equiv_cmp_gt();
    test_equiv_cmp_lt();
    test_equiv_cmp_ne();
    test_equiv_cmp_ge();
    test_equiv_cmp_le();
    test_equiv_strict_eq();
    test_equiv_logical_and();
    test_equiv_logical_or();
    test_equiv_logical_xor();
    test_equiv_logical_not();
    test_equiv_var_assign();
    test_equiv_var_arith();
    test_equiv_two_vars();
    test_equiv_novalue();
    test_equiv_concat();
    test_equiv_arith_to_logical();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
