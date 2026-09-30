/* ------------------------------------------------------------------ */
/*  tstbclm.c - bytecode trace map: clause -> line / source (#281)    */
/*                                                                    */
/*  Compiles REXX snippets and checks the trace map the compiler      */
/*  writes behind the bytecode: one entry per clause with its line,   */
/*  its source text and its static DO/SELECT depth, and the lookup   */
/*  irx_bc_line_at() the VM uses on the error path.                   */
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

/* The source of the snippet being checked, for the clause text. */
static const char *g_src;

static struct irx_bc_execblk *compile(struct envblock *env, const char *src)
{
    struct irx_bc_execblk *bc = NULL;
    g_src = src;
    int rc = irx_bc_compile(env, src, (int)strlen(src), &bc, NULL, NULL);
    if (rc != IRXBC_OK)
    {
        printf("  compile rc=%d for [%s]\n", rc, src);
        return NULL;
    }
    return bc;
}

static void release(struct envblock *env, struct irx_bc_execblk *bc)
{
    if (bc != NULL)
    {
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, env);
    }
}

/* Entry i: line, depth and clause text as expected. */
static void check_ent(const struct irx_bc_execblk *bc, uint32_t i,
                      uint32_t line, uint16_t depth, const char *text,
                      const char *tag)
{
    char label[160];
    if (i >= IRXBC_TRACE_COUNT(bc))
    {
        snprintf(label, sizeof(label), "%s: entry %u exists", tag,
                 (unsigned)i);
        CHECK(0, label);
        return;
    }
    const struct irx_bc_line_ent *e = &IRXBC_TRACE_MAP(bc)[i];
    size_t tl = strlen(text);

    snprintf(label, sizeof(label), "%s: line %u", tag, (unsigned)line);
    CHECK(e->line == line, label);
    snprintf(label, sizeof(label), "%s: depth %u", tag, (unsigned)depth);
    CHECK(e->depth == depth, label);
    snprintf(label, sizeof(label), "%s: text [%s]", tag, text);
    int ok = e->src_len == tl && memcmp(g_src + e->src_off, text, tl) == 0;
    CHECK(ok, label);
    if (!ok)
    {
        printf("    got line=%u depth=%u text=[%.*s]\n", (unsigned)e->line,
               (unsigned)e->depth, (int)e->src_len, g_src + e->src_off);
    }
}

static void test_one_per_line(struct envblock *env)
{
    printf("\n[one clause per line]\n");
    struct irx_bc_execblk *bc = compile(env, "say 1\nsay 2\nsay 3\n");
    if (bc == NULL)
    {
        CHECK(0, "compile");
        return;
    }
    CHECK(bc->trace_map_offset != 0, "trace map present");
    CHECK(bc->trace_map_offset % 4 == 0, "trace map 4-byte aligned");
    CHECK(IRXBC_TRACE_COUNT(bc) == 3, "3 entries");
    check_ent(bc, 0, 1, 0, "say 1", "clause 1");
    check_ent(bc, 1, 2, 0, "say 2", "clause 2");
    check_ent(bc, 2, 3, 0, "say 3", "clause 3");
    CHECK(IRXBC_TRACE_MAP(bc)[0].pc < IRXBC_TRACE_MAP(bc)[1].pc &&
              IRXBC_TRACE_MAP(bc)[1].pc < IRXBC_TRACE_MAP(bc)[2].pc,
          "pcs ascending");
    CHECK((int)IRXBC_TRACE_MAP(bc)[2].pc < (int)bc->code_length,
          "last pc inside the code");
    release(env, bc);
}

static void test_comments_and_semicolons(struct envblock *env)
{
    printf("\n[comment lines, ';' and blank lines]\n");
    struct irx_bc_execblk *bc =
        compile(env, "a = 1; b = 2\n/* note */\n\nsay a b\n");
    if (bc == NULL)
    {
        CHECK(0, "compile");
        return;
    }
    CHECK(IRXBC_TRACE_COUNT(bc) == 3, "3 entries");
    check_ent(bc, 0, 1, 0, "a = 1", "first of two on a line");
    check_ent(bc, 1, 1, 0, "b = 2", "second of two on a line");
    check_ent(bc, 2, 4, 0, "say a b", "after comment and blank line");
    release(env, bc);
}

static void test_strings(struct envblock *env)
{
    printf("\n[clause text includes the quotes]\n");
    struct irx_bc_execblk *bc =
        compile(env, "say 'abc'\nx = 'it''s'\ny = \"q\"\n");
    if (bc == NULL)
    {
        CHECK(0, "compile");
        return;
    }
    check_ent(bc, 0, 1, 0, "say 'abc'", "string");
    check_ent(bc, 1, 2, 0, "x = 'it''s'", "doubled quote");
    check_ent(bc, 2, 3, 0, "y = \"q\"", "double quotes");
    release(env, bc);
}

static void test_if_then(struct envblock *env)
{
    printf("\n[IF ... THEN: two clauses]\n");
    struct irx_bc_execblk *bc =
        compile(env, "if 1 = 1 then say 'yes'\nelse say 'no'\n");
    if (bc == NULL)
    {
        CHECK(0, "compile");
        return;
    }
    check_ent(bc, 0, 1, 0, "if 1 = 1", "IF clause stops before THEN");
    check_ent(bc, 1, 1, 0, "say 'yes'", "THEN body");
    check_ent(bc, 2, 2, 0, "say 'no'", "ELSE body");
    release(env, bc);
}

static void test_select(struct envblock *env)
{
    /* z/OS: a clause after WHEN ... THEN inside SELECT traces with one
     * nesting level (ERRTEST case A, #281). */
    printf("\n[SELECT / WHEN depth]\n");
    struct irx_bc_execblk *bc =
        compile(env, "select\n"
                     "  when 1 = 2 then say 'a'\n"
                     "  otherwise say 'b'\n"
                     "end\n");
    if (bc == NULL)
    {
        CHECK(0, "compile");
        return;
    }
    check_ent(bc, 0, 1, 0, "select", "SELECT at depth 0");
    check_ent(bc, 1, 2, 1, "when 1 = 2", "WHEN at depth 1");
    check_ent(bc, 2, 2, 1, "say 'a'", "THEN body at depth 1");
    check_ent(bc, 3, 3, 1, "say 'b'", "OTHERWISE body at depth 1");
    release(env, bc);
}

static void test_do_loop(struct envblock *env)
{
    printf("\n[DO: body deeper, iterate code back on the DO clause]\n");
    struct irx_bc_execblk *bc =
        compile(env, "do i = 1 to 3\n  say i\nend\nsay 'done'\n");
    if (bc == NULL)
    {
        CHECK(0, "compile");
        return;
    }
    check_ent(bc, 0, 1, 0, "do i = 1 to 3", "DO clause");
    check_ent(bc, 1, 2, 1, "say i", "body at depth 1");
    check_ent(bc, 2, 1, 0, "do i = 1 to 3", "iterate section is the DO");
    check_ent(bc, 3, 4, 0, "say 'done'", "after the loop");
    release(env, bc);
}

static void test_continuation(struct envblock *env)
{
    /* z/OS reports a continued clause with the line it starts on
     * (ERRTEST case D, #281). */
    printf("\n[continued clause]\n");
    struct irx_bc_execblk *bc = compile(env, "z = 1 + ,\n    2\nsay z\n");
    if (bc == NULL)
    {
        CHECK(0, "compile");
        return;
    }
    check_ent(bc, 0, 1, 0, "z = 1 + ,\n    2", "spans both lines");
    check_ent(bc, 1, 3, 0, "say z", "next clause");
    release(env, bc);
}

static void test_labels(struct envblock *env)
{
    printf("\n[labels and routines]\n");
    struct irx_bc_execblk *bc =
        compile(env, "call f\nexit\nf:\n  y = 2\n  return\n");
    if (bc == NULL)
    {
        CHECK(0, "compile");
        return;
    }
    check_ent(bc, 0, 1, 0, "call f", "CALL");
    check_ent(bc, 1, 2, 0, "exit", "EXIT");
    check_ent(bc, 2, 4, 0, "y = 2", "first clause after the label");
    check_ent(bc, 3, 5, 0, "return", "RETURN");
    release(env, bc);
}

static void test_lookup(struct envblock *env)
{
    printf("\n[irx_bc_line_at]\n");
    struct irx_bc_execblk *bc =
        compile(env, "a = 1\ndo 2\n  b = a + 1\nend\nsay b\n");
    if (bc == NULL)
    {
        CHECK(0, "compile");
        return;
    }
    const struct irx_bc_line_ent *map = IRXBC_TRACE_MAP(bc);
    uint32_t n = IRXBC_TRACE_COUNT(bc);
    int all = 1;
    for (uint32_t pc = 0; pc < bc->code_length; pc++)
    {
        const struct irx_bc_line_ent *e = irx_bc_line_at(bc, pc);
        /* Must be the last entry starting at or before pc. */
        const struct irx_bc_line_ent *want = NULL;
        for (uint32_t i = 0; i < n; i++)
        {
            if (map[i].pc <= pc)
            {
                want = &map[i];
            }
        }
        if (e != want)
        {
            all = 0;
            printf("    pc %u: got %p want %p\n", (unsigned)pc, (void *)e,
                   (void *)want);
        }
    }
    CHECK(all, "every pc maps to the last entry at or before it");
    CHECK(irx_bc_line_at(bc, bc->code_length + 100) == &map[n - 1],
          "pc past the end -> last clause");
    CHECK(irx_bc_line_at(NULL, 0) == NULL, "NULL container -> NULL");
    release(env, bc);

    bc = compile(env, "/* nothing */");
    if (bc != NULL)
    {
        CHECK(bc->trace_map_offset == 0, "no clause -> no trace map");
        CHECK(irx_bc_line_at(bc, 0) == NULL, "no trace map -> NULL");
        CHECK(IRXBC_TOTAL(bc) == (int)sizeof(struct irx_bc_execblk) +
                                     (int)bc->code_length,
              "IRXBC_TOTAL without a map");
        release(env, bc);
    }
}

int main(void)
{
    struct envblock *env = NULL;

    printf("=== #281 bytecode trace map ===\n");
    if (irxinit(NULL, &env) != 0 || env == NULL)
    {
        fprintf(stderr, "tstbclm: irxinit failed\n");
        return 1;
    }

    test_one_per_line(env);
    test_comments_and_semicolons(env);
    test_strings(env);
    test_if_then(env);
    test_select(env);
    test_do_loop(env);
    test_continuation(env);
    test_labels(env);
    test_lookup(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");
    return tests_failed > 0 ? 1 : 0;
}
