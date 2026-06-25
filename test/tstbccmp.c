/* ------------------------------------------------------------------ */
/*  test/host/tstbc_compile.c — WP-BC-01 compiler unit tests          */
/*                                                                    */
/*  Verifies that irx_bc_compile() produces well-formed bytecode      */
/*  containers for the Phase 1 subset (EXIT and empty source).        */
/*                                                                    */
/*  Build (Linux):                                                     */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -O0 -g \                           */
/*        -o /tmp/tstbc_compile test/host/tstbc_compile.c \           */
/*        'src/irx#init.c'  'src/irx#term.c'  'src/irx#stor.c' \    */
/*        'src/irx#anch.c'  'src/irx#env.c'   'src/irx#uid.c'  \    */
/*        'src/irx#msid.c'  'src/irx#cond.c'  'src/irx#bif.c'  \    */
/*        'src/irx#bifs.c'  'src/irx#io.c'    'src/irx#lstr.c' \    */
/*        'src/irx#tokn.c'  'src/irx#vpol.c'  'src/irx#pars.c' \    */
/*        'src/irx#ctrl.c'  'src/irx#exec.c'  'src/irx#arith.c' \   */
/*        'src/irx#bcom.c'  'src/irx#bvm.c' \                        */
/*        '../lstring370/src/lstr#cor.c'  \                           */
/*        '../lstring370/src/lstr#cvt.c'  \                           */
/*        '../lstring370/src/lstr#fmt.c'  \                           */
/*        '../lstring370/src/lstr#srch.c' \                           */
/*        '../lstring370/src/lstr#sub.c'  \                           */
/*        '../lstring370/src/lstr#wrd.c'  \                           */
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
#include "irxfunc.h"

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
/*  Helper: compile a source string, run checks, free the container.  */
/* ------------------------------------------------------------------ */
static void test_compile_empty(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    const unsigned char *code;
    int rc;

    printf("  [compile: empty source]\n");

    rc = irx_bc_compile(env, "/* nothing */",
                        (int)strlen("/* nothing */"), &bc, NULL, NULL);
    CHECK(rc == IRXBC_OK, "compile returns IRXBC_OK");
    CHECK(bc != NULL, "bc pointer is non-NULL");

    if (bc == NULL)
    {
        return;
    }

    CHECK(memcmp(bc->magic, IRXBC_MAGIC, sizeof(bc->magic)) == 0,
          "magic is RX37");
    CHECK(bc->version == IRXBC_VERSION, "version == 1");
    CHECK(bc->flags == 0, "flags == 0");
    CHECK(bc->const_count == 0, "const_count == 0");
    CHECK(bc->symbol_count == 0, "symbol_count == 0");
    CHECK(bc->entry_offset == 0, "entry_offset == 0");
    CHECK(bc->trace_map_offset == 0, "trace_map_offset == 0");

    /* Empty source -> only OP_EXIT in bytecode. */
    CHECK(bc->code_length == 1, "code_length == 1 (OP_EXIT only)");

    code = IRXBC_CODE(bc);
    CHECK(code[0] == OP_EXIT, "bytecode[0] == OP_EXIT");

    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

static void test_compile_exit(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    const unsigned char *code;
    int rc;

    printf("  [compile: exit]\n");

    rc = irx_bc_compile(env, "exit", (int)strlen("exit"), &bc, NULL, NULL);
    CHECK(rc == IRXBC_OK, "compile returns IRXBC_OK");
    CHECK(bc != NULL, "bc pointer is non-NULL");

    if (bc == NULL)
    {
        return;
    }

    CHECK(memcmp(bc->magic, IRXBC_MAGIC, sizeof(bc->magic)) == 0,
          "magic is RX37");
    CHECK(bc->version == IRXBC_VERSION, "version == 1");
    CHECK(bc->const_count == 0, "const_count == 0");
    CHECK(bc->symbol_count == 0, "symbol_count == 0");

    /* exit -> OP_NEWCLAUSE + OP_EXIT + trailing OP_EXIT terminator
     * (the compiler appends an implicit OP_EXIT so a fall-through exits). */
    CHECK(bc->code_length == 3,
          "code_length == 3 (NEWCLAUSE + EXIT + trailing EXIT)");

    code = IRXBC_CODE(bc);
    CHECK(code[0] == OP_NEWCLAUSE, "bytecode[0] == OP_NEWCLAUSE");
    CHECK(code[1] == OP_EXIT, "bytecode[1] == OP_EXIT");
    CHECK(code[2] == OP_EXIT, "bytecode[2] == OP_EXIT (trailing terminator)");

    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

static void test_compile_unsupported(struct envblock *env)
{
    struct irx_bc_execblk *bc = NULL;
    int rc;

    printf("  [compile: unsupported construct]\n");

    /* INTERPRET cannot be statically bytecode-compiled (it generates and runs
     * code at run time), so it stays on the token-walk fallback path.  (CALL,
     * which this test used to check, is now compiled -- see TSTBCAL.) */
    rc = irx_bc_compile(env, "interpret x", (int)strlen("interpret x"),
                        &bc, NULL, NULL);
    CHECK(rc == IRXBC_ERR_UNSUP,
          "compile returns IRXBC_ERR_UNSUP for unsupported construct");
    CHECK(bc == NULL, "bc is NULL on error");
}

static void test_compile_null_out(struct envblock *env)
{
    int rc;

    printf("  [compile: NULL bc_out]\n");

    rc = irx_bc_compile(env, "exit", (int)strlen("exit"), NULL, NULL, NULL);
    CHECK(rc != IRXBC_OK, "compile rejects NULL bc_out");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    struct envblock *env = NULL;
    int rc;

    printf("=== WP-BC-01 Compiler Tests ===\n\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbc_compile: irxinit failed rc=%d\n", rc);
        return 1;
    }

    test_compile_empty(env);
    test_compile_exit(env);
    test_compile_unsupported(env);
    test_compile_null_out(env);

    irxterm(env);

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
