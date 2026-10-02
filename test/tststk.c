/* ------------------------------------------------------------------ */
/*  tststk.c - C stack high-water per workload (#258)                  */
/*                                                                    */
/*  How much of the C stack does the interpreter really use?  IRXJCL  */
/*  runs on libc370's default 256 KB stack (@@CRT1 MAINSTK), IRXEXEC  */
/*  on the 64 KB WPOOL of asm/irxexec.asm.  PDPPRLG has no bounds     */
/*  check, so a stack that is too small corrupts whatever follows it  */
/*  instead of failing.  Before either is changed, measure.           */
/*                                                                    */
/*  Method (MVS only).  PDPPRLG stacks grow upward from the PPA that  */
/*  @@CRT1 GETMAINs together with the stack; __ppaget() returns it    */
/*  and ppastkln is the length of the whole block.  Before each run   */
/*  everything above the painter's own frame is filled with a         */
/*  pattern; after the run the block is scanned downward for the      */
/*  first byte that is no longer the pattern.  The distance from the  */
/*  PPA to that byte is the high-water mark.                          */
/*                                                                    */
/*  Workloads: hello, the REXXCPS kernel, and depth series of nested  */
/*  parentheses, nested function calls, nested IF/DO and DO blocks,   */
/*  on both paths; recursive function calls on the bytecode path      */
/*  only (the token-walk path cannot run them; the VM runs 250 active */
/*  calls, #295).  The depth                                          */
/*  series show how the need grows with nesting.  Overflowing the     */
/*  stack would corrupt storage rather than fail, so a series stops   */
/*  once a run has used a third of it: the next step doubles.         */
/*                                                                    */
/*  On the host the execs run and are checked, but nothing is         */
/*  measured: the host stack grows downward and has no PPA.           */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxcond.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxpars.h"
#include "irxwkblk.h"

#ifdef __MVS__
#include <mvs/crt.h>
#else
void *_simulated_ectenvbk = NULL;
#endif

#define SRCBUF_SIZE 8192
#define CAPBUF_SIZE 1024

/* Bytes above the painter's local variable that are left alone: its
 * own frame lies there.  The painter calls nothing while painting. */
#define PAINT_SLACK 1024

/* A run that leaves less than this unused has too little margin to
 * trust the stack size it ran on. */
#define STK_MARGIN 16384

#define STK_PATTERN 0xA5

/* A depth series stops once a run used this fraction (1/n). */
#define STK_STOP_DIV 3

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                   \
    do                                     \
    {                                      \
        tests_run++;                       \
        if (cond)                          \
        {                                  \
            printf("  PASS: %s\n", (msg)); \
        }                                  \
        else                               \
        {                                  \
            tests_failed++;                \
            printf("  FAIL: %s\n", (msg)); \
        }                                  \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Output capture: discarded, only the last line is kept to check    */
/* ------------------------------------------------------------------ */

static char g_cap[CAPBUF_SIZE];

static int capture_io(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;
    if (function == RXFWRITE && data != NULL && Lpstr(data) != NULL)
    {
        size_t n = Llen(data);
        if (n >= sizeof(g_cap))
        {
            n = sizeof(g_cap) - 1;
        }
        memcpy(g_cap, Lpstr(data), n);
        g_cap[n] = '\0';
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Stack paint / scan                                                */
/* ------------------------------------------------------------------ */

#ifdef __MVS__
static unsigned char *stk_base(void)
{
    return (unsigned char *)__ppaget();
}

static unsigned stk_len(void)
{
    return __ppaget()->ppastkln;
}

/* Paint from just above this frame to the end of the stack block.  No
 * call inside the loop, so nothing is pushed onto what is painted. */
static void stk_paint(void)
{
    unsigned char here = 0;
    unsigned char *p = &here + PAINT_SLACK;
    unsigned char *end = stk_base() + stk_len();

    while (p < end)
    {
        *p++ = STK_PATTERN;
    }
}

/* High-water mark in bytes from the start of the block. */
static unsigned stk_highwater(void)
{
    unsigned char *base = stk_base();
    unsigned char *p = base + stk_len();

    while (p > base && p[-1] == STK_PATTERN)
    {
        p--;
    }
    return (unsigned)(p - base);
}
#else
static unsigned stk_len(void)
{
    return 0;
}

static void stk_paint(void)
{
}

static unsigned stk_highwater(void)
{
    return 0;
}
#endif

/* ------------------------------------------------------------------ */
/*  Workloads                                                         */
/* ------------------------------------------------------------------ */

static const char REXXCPS[] =
    "do i = 1 to 2\n"
    "  flag = 0\n"
    "  do loop = 1 to 14\n"
    "    key1 = 'Key Bee'\n"
    "    acompound.key1.loop = substr(12345678, 6, 2)\n"
    "    do j = 1 to 2\n"
    "      if j > acompound.key1.loop then say 'Failed2'\n"
    "      if word(key1, 1) = '?' then say 'Failed6'\n"
    "      if j < 5 then do\n"
    "        acompound.key1.loop = acompound.key1.loop + 1\n"
    "        if j = 2 then leave\n"
    "      end\n"
    "    end\n"
    "    avar. = 1\n"
    "    select\n"
    "      when flag = 'string' then say 'FailedS1'\n"
    "      when flag then avar.1.2 = avar.1.2 * 1.1\n"
    "      when flag == 0 then flag = 0\n"
    "    end\n"
    "    parse value 'Foo Bar' with v1 +5 v2 .\n"
    "    call subroutine 'with' 2 'args'\n"
    "  end loop\n"
    "end\n"
    "say 'done'\n"
    "exit\n"
    "subroutine:\n"
    "  parse upper arg a1 a2 a3 .\n"
    "  parse var a3 b1 b2 b3 .\n"
    "  return\n";

/* x = (((...(1+1)+1)...)+1): depth opening parentheses; says depth+1. */
static int gen_parens(char *buf, int cap, int depth)
{
    int off = snprintf(buf, (size_t)cap, "x = ");
    for (int i = 0; i < depth; i++)
    {
        off += snprintf(buf + off, (size_t)(cap - off), "(");
    }
    off += snprintf(buf + off, (size_t)(cap - off), "1");
    for (int i = 0; i < depth; i++)
    {
        off += snprintf(buf + off, (size_t)(cap - off), "+1)");
    }
    off += snprintf(buf + off, (size_t)(cap - off), "\nsay x\n");
    return off;
}

/* A function calling itself depth times; says depth. */
static int gen_recurse(char *buf, int cap, int depth)
{
    return snprintf(buf, (size_t)cap,
                    "say down(%d)\n"
                    "exit\n"
                    "down: procedure\n"
                    "  parse arg n\n"
                    "  if n = 0 then return 0\n"
                    "  return down(n - 1) + 1\n",
                    depth);
}

/* depth nested blocks, each opened by open and closed by END, around
 * x = 1; says 1. */
static int gen_blocks(char *buf, int cap, int depth, const char *open)
{
    int off = 0;
    for (int i = 0; i < depth; i++)
    {
        off += snprintf(buf + off, (size_t)(cap - off), "%s\n", open);
    }
    off += snprintf(buf + off, (size_t)(cap - off), "x = 1\n");
    for (int i = 0; i < depth; i++)
    {
        off += snprintf(buf + off, (size_t)(cap - off), "end\n");
    }
    off += snprintf(buf + off, (size_t)(cap - off), "say x\n");
    return off;
}

static int gen_do(char *buf, int cap, int depth)
{
    return gen_blocks(buf, cap, depth, "do 1");
}

static int gen_if(char *buf, int cap, int depth)
{
    return gen_blocks(buf, cap, depth, "if 1 then do");
}

/* if 1 then if 1 then ... x = 1: a chain of depth IFs; says 1. */
static int gen_ifchain(char *buf, int cap, int depth)
{
    int off = 0;
    for (int i = 0; i < depth; i++)
    {
        off += snprintf(buf + off, (size_t)(cap - off), "if 1 then ");
    }
    off += snprintf(buf + off, (size_t)(cap - off), "x = 1\nsay x\n");
    return off;
}

/* x = abs(abs(...abs(1)...)): depth nested function calls; says 1. */
static int gen_calls(char *buf, int cap, int depth)
{
    int off = snprintf(buf, (size_t)cap, "x = ");
    for (int i = 0; i < depth; i++)
    {
        off += snprintf(buf + off, (size_t)(cap - off), "abs(");
    }
    off += snprintf(buf + off, (size_t)(cap - off), "1");
    for (int i = 0; i < depth; i++)
    {
        off += snprintf(buf + off, (size_t)(cap - off), ")");
    }
    off += snprintf(buf + off, (size_t)(cap - off), "\nsay x\n");
    return off;
}

/* Run one exec on a freshly painted stack; report and check.  Returns
 * the high-water mark. */
static unsigned measure(struct envblock *env, const char *src, int len,
                        int bytecode, const char *expect, const char *tag)
{
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)env->envblock_workblok_ext;
    int exit_rc = 0;

    g_cap[0] = '\0';
    wk->wkbi_use_bytecode = bytecode;
    wk->wkbi_bc_exec_count = 0;
    wk->wkbi_bc_fallback_count = 0;

    stk_paint();
    int rc = irx_exec_run(src, len, NULL, 0, &exit_rc, env);
    unsigned hw = stk_highwater();

    const char *path = !bytecode                        ? "token-walk"
                       : wk->wkbi_bc_fallback_count > 0 ? "fallback"
                                                        : "bytecode";
    printf("  %-24s %-10s high-water %6u of %6u\n", tag, path, hw,
           stk_len());

    char label[96];
    snprintf(label, sizeof(label), "%s: rc=%d exit=%d out=[%s]", tag, rc,
             exit_rc, g_cap);
    CHECK(rc == 0 && exit_rc == 0 && strcmp(g_cap, expect) == 0, label);
    snprintf(label, sizeof(label), "%s: %u bytes left, need %u", tag,
             stk_len() - hw, STK_MARGIN);
    CHECK(stk_len() == 0 || stk_len() - hw >= STK_MARGIN, label);
    return hw;
}

/* Deep enough that the next, doubled depth could overflow? */
static int too_deep(unsigned hw)
{
    return stk_len() != 0 && hw > stk_len() / STK_STOP_DIV;
}

/* A depth series: one workload generator, run at growing depths. */
struct series
{
    const char *name;
    int (*gen)(char *buf, int cap, int depth);
    int depths[4];
    int token_walk; /* also run on the token-walk path */
    int says_depth; /* output is depth (+1 for parens), not 1 */
};

static const struct series SERIES[] = {
    /* The deepest that runs, as on z/OS: 39 parentheses with a pending
     * +, 40 calls; deeper is error 39 (#294, irxpars.h). */
    {"parens", gen_parens, {10, 20, 30, 39}, 1, 1},
    {"abs()", gen_calls, {10, 20, 30, 40}, 1, 0},
    {"if chain", gen_ifchain, {10, 20, 40, 80}, 1, 0},
    /* Each DO, IF and SELECT takes a control stack entry: 250 DO, 125
     * IF-DO at most (#296).  No guard here (wkbi_stack_end is NULL),
     * so the series measures what the nesting itself needs. */
    {"if-do", gen_if, {10, 50, 100, 125}, 1, 0},
    {"do", gen_do, {10, 50, 100, 250}, 1, 0},
    /* down(249) holds 250 calls active, the most the VM runs (#295). */
    {"recursion", gen_recurse, {1, 15, 100, 249}, 0, 1},
};

static void run_series(struct envblock *env, const struct series *sr,
                       char *src)
{
    for (size_t i = 0; i < sizeof(sr->depths) / sizeof(sr->depths[0]); i++)
    {
        int depth = sr->depths[i];
        if (depth == 0)
        {
            break;
        }
        char tag[32];
        char expect[16];
        int len = sr->gen(src, SRCBUF_SIZE, depth);
        int out = !sr->says_depth ? 1 : sr->gen == gen_parens ? depth + 1
                                                              : depth;
        snprintf(expect, sizeof(expect), "%d", out);
        snprintf(tag, sizeof(tag), "%s depth %d", sr->name, depth);
        unsigned hw = measure(env, src, len, 1, expect, tag);
        if (sr->token_walk)
        {
            unsigned tw = measure(env, src, len, 0, expect, tag);
            hw = tw > hw ? tw : hw;
        }
        if (too_deep(hw))
        {
            printf("  deeper %s skipped: a third of the stack used\n",
                   sr->name);
            break;
        }
    }
}

#ifdef __MVS__
/* The C stack guard (#296): with wkbi_stack_end set just past what a
 * few levels need, a deep exec must stop with its error, and the stack
 * must not have been used past that end.  GUARD_ROOM above the margin
 * is room for the run itself and a few levels. */
#define GUARD_ROOM 4096

static void guard_case(struct envblock *env, const char *src, int len,
                       int bytecode, int want, int also, const char *tag)
{
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)env->envblock_workblok_ext;
    unsigned char here = 0;
    unsigned char *end = &here + IRX_STACK_MARGIN + GUARD_ROOM;
    int exit_rc = 0;

    g_cap[0] = '\0';
    wk->wkbi_use_bytecode = bytecode;
    wk->wkbi_error_number = 0;
    wk->wkbi_stack_end = end;
    stk_paint();
    int rc = irx_exec_run(src, len, NULL, 0, &exit_rc, env);
    unsigned char *top = stk_base() + stk_highwater();
    wk->wkbi_stack_end = NULL;
    wk->wkbi_use_bytecode = 1;

    printf("  guard %-18s %-10s error %d, %ld bytes below the end\n", tag,
           bytecode ? "bytecode" : "token-walk", wk->wkbi_error_number,
           (long)(end - top));
    char label[96];
    snprintf(label, sizeof(label), "guard %s (%s): error %d", tag,
             bytecode ? "bytecode" : "token-walk", want);
    CHECK(rc != 0 && (wk->wkbi_error_number == want ||
                      (also != 0 && wk->wkbi_error_number == also)),
          label);
    snprintf(label, sizeof(label), "guard %s (%s): stack kept below the end",
             tag, bytecode ? "bytecode" : "token-walk");
    CHECK(top <= end, label);
}

static void test_guard(struct envblock *env, char *src)
{
    printf("--- C stack guard (#296) ---\n");
    int len = gen_do(src, SRCBUF_SIZE, 15);
    guard_case(env, src, len, 1, SYNTAX_CTL_STACK, 0, "do 15");
    len = gen_if(src, SRCBUF_SIZE, 15);
    guard_case(env, src, len, 1, SYNTAX_CTL_STACK, 0, "if-do 15");
    /* The token walk checks each IF and each expression level: either
     * can be the one that finds the stack short. */
    guard_case(env, src, len, 0, SYNTAX_CTL_STACK, SYNTAX_EVAL_STACK,
               "if-do 15");
    len = gen_calls(src, SRCBUF_SIZE, 40);
    guard_case(env, src, len, 1, SYNTAX_EVAL_STACK, 0, "abs() 40");
    guard_case(env, src, len, 0, SYNTAX_EVAL_STACK, 0, "abs() 40");
}
#endif

int main(void)
{
    static char src[SRCBUF_SIZE];
    struct envblock *env = NULL;

    printf("=== TSTSTK: C stack high-water per workload (#258) ===\n");

    if (irxinit(NULL, &env) != 0 || env == NULL)
    {
        printf("  FAIL: irxinit\n");
        return 1;
    }
    struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    int len = snprintf(src, sizeof(src), "say 'hello'\n");
    measure(env, src, len, 1, "hello", "hello");

    measure(env, REXXCPS, (int)strlen(REXXCPS), 1, "done", "rexxcps");
    measure(env, REXXCPS, (int)strlen(REXXCPS), 0, "done", "rexxcps");

    for (size_t i = 0; i < sizeof(SERIES) / sizeof(SERIES[0]); i++)
    {
        run_series(env, &SERIES[i], src);
    }

#ifdef __MVS__
    test_guard(env, src);
#endif

    irxterm(env);

    printf("--- %d checks, %d failed ---\n", tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
