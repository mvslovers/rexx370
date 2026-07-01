/* ------------------------------------------------------------------ */
/*  tstbcat.c - WP-BC-RT02: abuttal vs. blank concatenation            */
/*                                                                    */
/*  Before WP-BC-RT02 the bytecode compiler's implicit-concatenation   */
/*  branch (src/irx#bcom.c, bc_exp3) unconditionally emitted           */
/*  OP_BCONCAT (0x61, "insert one blank").  It never emitted           */
/*  OP_CONCAT (0x60, no blank) for a true abuttal — two value terms    */
/*  directly adjacent in source with no whitespace and no '||'.  The   */
/*  token-walk gets this right via toks_adjacent() / parse_concat      */
/*  (src/irx#pars.c); the compiler omitted the adjacency check.        */
/*                                                                    */
/*  Symptom: 'a'v'b' produced "a X b" instead of "aXb"; the stem       */
/*  default 1.0''loop became "1.0  1" (two blanks) instead of "1.01",  */
/*  an invalid number that aborted REXXCPS with RC24 (IRXBC_ERR_ARITH).*/
/*  Diagnosed in WP-BC-RT01 (docs/diag/wp-bc-rt01.md).                 */
/*                                                                    */
/*  This test verifies:                                               */
/*    1. abuttal compiles to OP_CONCAT, blank to OP_BCONCAT, mixed     */
/*       expressions to the right mix (disassembler proof);            */
/*    2. the b1 root-cause demo (docs/diag/wp-bc-rt01-b1.rexx) and the */
/*       min2 RC24 repro (docs/diag/wp-bc-rt01-min2.rexx) now run on   */
/*       the bytecode path with RC=0 and token-walk-identical output;  */
/*    3. equivalence vs. token-walk across abuttal / blank / mixed     */
/*       concatenation variants (CON-18: byte-identical behaviour).    */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    LSTR="-I contrib/lstring370-0.1.0-dev/include"                  */
/*    LSRC="../lstring370/src/lstr#cor.c ../lstring370/src/lstr#cvt.c */
/*          ../lstring370/src/lstr#fmt.c ../lstring370/src/lstr#srch.c */
/*          ../lstring370/src/lstr#sub.c ../lstring370/src/lstr#wrd.c */
/*          ../lstring370/src/lstr#xlt.c"                             */
/*    gcc -I include $LSTR -Wall -Wextra -std=gnu99 \                 */
/*        -o /tmp/tstbcat test/mvs/tstbcat.c \                         */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        src/irx#bcom.c  src/irx#bvm.c   src/irx#bctl.c \           */
/*        $LSRC && /tmp/tstbcat                                       */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxbctl.h"
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
/*  equiv: token-walk vs bytecode output match (CON-18 reference).     */
/* ------------------------------------------------------------------ */

static int equiv(struct envblock *env, const char *src, const char *tag)
{
    struct irx_wkblk_int *wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
    int src_len = (int)strlen(src);
    int exit_rc = 0;
    char tw_out[CAPBUF_SIZE];
    char bc_out[CAPBUF_SIZE];
    char label[160];

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
/*  bc_exact: bytecode run is RC=0, never falls back to token-walk,    */
/*  and produces exactly the expected output.  This is the end-to-end  */
/*  proof for the b1 / min2 repros (min2 aborted with RC24 pre-fix).   */
/* ------------------------------------------------------------------ */

static int bc_exact(struct envblock *env, const char *src,
                    const char *expected, const char *tag)
{
    struct irx_wkblk_int *wk = (struct irx_wkblk_int *)env->envblock_workblok_ext;
    int src_len = (int)strlen(src);
    int exit_rc = 0;
    int rc;
    char label[160];

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
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 0;

    snprintf(label, sizeof(label), "bc-exact: %s", tag);
    CHECK(rc == 0 && exit_rc == 0 && strcmp(g_cap, expected) == 0 &&
              wk->wkbi_bc_exec_count > 0 && wk->wkbi_bc_fallback_count == 0,
          label);

    if (rc != 0 || exit_rc != 0 || strcmp(g_cap, expected) != 0 ||
        wk->wkbi_bc_exec_count == 0 || wk->wkbi_bc_fallback_count != 0)
    {
        printf("    rc=%d exit_rc=%d exec=%d fallback=%d\n", rc, exit_rc,
               wk->wkbi_bc_exec_count, wk->wkbi_bc_fallback_count);
        printf("    expected:[%s]\n", expected);
        printf("    got:     [%s]\n", g_cap);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Disassembler-based opcode proof.                                   */
/*                                                                    */
/*  disasm_has matches ": <mnem>" so a query for "CONCAT" does NOT     */
/*  also match the "BCONCAT" line (the disasm format is               */
/*  "OOOO: MNEMONIC ...").                                             */
/* ------------------------------------------------------------------ */

static int disasm_has(const char *disasm, const char *mnem)
{
    char needle[32];
    snprintf(needle, sizeof(needle), ": %s", mnem);
    return strstr(disasm, needle) != NULL;
}

static void disasm_check(struct envblock *env, const char *src,
                         int want_concat, int want_bconcat, const char *tag)
{
    struct irx_bc_execblk *bc = NULL;
    int reason = 0;
    int line = 0;
    int rc;
    char buf[CAPBUF_SIZE];
    char label[160];
    int has_c;
    int has_b;

    rc = irx_bc_compile(env, src, (int)strlen(src), &bc, &reason, &line);
    if (rc != 0 || bc == NULL)
    {
        snprintf(label, sizeof(label), "disasm: %s (compile rc=%d)", tag, rc);
        CHECK(0, label);
        return;
    }

    buf[0] = '\0';
    irx_bc_disasm(bc, buf, sizeof(buf));
    has_c = disasm_has(buf, "CONCAT");
    has_b = disasm_has(buf, "BCONCAT");

    snprintf(label, sizeof(label), "disasm: %s", tag);
    CHECK(has_c == want_concat && has_b == want_bconcat, label);
    if (has_c != want_concat || has_b != want_bconcat)
    {
        printf("    want CONCAT=%d BCONCAT=%d, got CONCAT=%d BCONCAT=%d\n",
               want_concat, want_bconcat, has_c, has_b);
        printf("    disasm:\n%s", buf);
    }

    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

/* ------------------------------------------------------------------ */
/*  Codegen proof: abuttal -> CONCAT, blank -> BCONCAT, mixed -> both. */
/* ------------------------------------------------------------------ */

static void test_codegen(struct envblock *env)
{
    printf("\n[codegen: OP_CONCAT (abuttal) vs OP_BCONCAT (blank)]\n");

    /* Pure abuttal: 'a'v'b' — three adjacent terms, no blanks, no '||'.
     * Two junctions, both OP_CONCAT, no OP_BCONCAT. */
    disasm_check(env, "say 'a'v'b'", 1, 0, "'a'v'b' -> CONCAT, no BCONCAT");

    /* Pure blank concatenation: two symbols separated by whitespace. */
    disasm_check(env, "say a b", 0, 1, "a b -> BCONCAT, no CONCAT");

    /* Mixed: '<'a'>' is abuttal (CONCAT), ' b' is blank (BCONCAT). */
    disasm_check(env, "say '<'a'>' b", 1, 1, "'<'a'>' b -> CONCAT and BCONCAT");

    /* Explicit '||' is unaffected: still OP_CONCAT, no blank. */
    disasm_check(env, "say a||b", 1, 0, "a||b -> CONCAT, no BCONCAT");

    /* Number abuttal string: the min2 junction 1.0''loop pattern. */
    disasm_check(env, "say 1.0'x'", 1, 0, "1.0'x' -> CONCAT, no BCONCAT");
}

/* ------------------------------------------------------------------ */
/*  The WP-BC-RT01 repros land here as regression tests.               */
/* ------------------------------------------------------------------ */

/* docs/diag/wp-bc-rt01-b1.rexx — clean abuttal root-cause demo. */
static const char B1_SRC[] =
    "v='X'\n"
    "say 'a'v'b'\n"
    "say '['v']'\n";

/* docs/diag/wp-bc-rt01-min2.rexx — minimal RC24 repro.
 * Pre-fix bytecode: avar. default becomes "1.0  1" -> RC24 on the *1.1. */
static const char MIN2_SRC[] =
    "loop=1\n"
    "avar.=1.0''loop\n"
    "say 'avar.1.2=['avar.1.2']'\n"
    "avar.1.2=avar.1.2*1.1\n"
    "say 'result=['avar.1.2']'\n";

static void test_repros(struct envblock *env)
{
    printf("\n[WP-BC-RT01 repros: b1 and min2]\n");

    /* b1: abuttal now concatenates with no blank, identical to token-walk. */
    equiv(env, B1_SRC, "b1");
    bc_exact(env, B1_SRC, "aXb\n[X]\n", "b1 -> aXb / [X]");

    /* min2: the stem default is a valid number "1.01"; the *1.1 no longer
     * sees the blank-poisoned "1.0  1" and the program completes RC=0. */
    equiv(env, MIN2_SRC, "min2");
    bc_exact(env, MIN2_SRC, "avar.1.2=[1.01]\nresult=[1.111]\n",
             "min2 -> 1.01 / 1.111, RC=0 (no RC24)");
}

/* ------------------------------------------------------------------ */
/*  Equivalence across concatenation variants (CON-18).                */
/* ------------------------------------------------------------------ */

static void test_equivalence(struct envblock *env)
{
    printf("\n[equivalence: concatenation variants vs token-walk]\n");

    /* Abuttal of a literal and a variable, both orders. */
    equiv(env, "v='X'\nsay 'a'v", "literal then var abuttal");
    equiv(env, "v='X'\nsay v'b'", "var then literal abuttal");

    /* Blank concatenation stays a single blank. */
    equiv(env, "a=1\nb=2\nsay a b", "blank concat of two vars");

    /* Mixed abuttal and blank in one expression. */
    equiv(env, "v='X'\nsay '['v']' v", "mixed abuttal + blank");

    /* Three-way abuttal chain. */
    equiv(env, "a='1'\nb='2'\nc='3'\nsay a||b||c", "explicit || chain");
    equiv(env, "a='1'\nsay '['a']['a']'", "multi-junction abuttal chain");

    /* Abuttal in an assignment RHS, then displayed. */
    equiv(env, "loop=2\nx='v'loop'w'\nsay x", "abuttal in assignment RHS");

    /* Numeric abuttal that feeds arithmetic (the min2 shape, simple var). */
    equiv(env, "n.=1.0''2\nsay n.5+1", "numeric abuttal into arithmetic");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    struct envblock *env = NULL;
    struct irxexte *exte;
    int rc;

    printf("=== WP-BC-RT02: abuttal vs blank concatenation ===\n");

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbcat: irxinit failed rc=%d\n", rc);
        return 1;
    }

    exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    test_codegen(env);
    test_repros(env);
    test_equivalence(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");

    return tests_failed > 0 ? 1 : 0;
}
