/* ------------------------------------------------------------------ */
/*  tstberr.c - error message and +++ traceback (#281)                */
/*                                                                    */
/*  Runs the ERRTEST exec of #281 case by case and compares the       */
/*  output byte for byte with what TSO/E printed on z/OS (maintainer  */
/*  run, 2026-09-30).  Also checks the message forms of              */
/*  irx_emsg_syntax() directly.                                       */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxcond.h"
#include "irxemsg.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxpars.h"
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

#define CAPBUF_SIZE 4096

static char g_cap[CAPBUF_SIZE];
static int g_cap_len = 0;

static void cap_reset(void)
{
    g_cap_len = 0;
    g_cap[0] = '\0';
}

/* Every line the I/O routine is given -- SAY, trace and messages. */
static int capture_io(int function, PLstr data, struct envblock *envblock)
{
    (void)function;
    (void)envblock;
    if (data != NULL)
    {
        int n = (int)Llen(data);
        if (g_cap_len + n + 1 < CAPBUF_SIZE)
        {
            if (n > 0)
            {
                memcpy(g_cap + g_cap_len, Lpstr(data), (size_t)n);
            }
            g_cap_len += n;
            g_cap[g_cap_len++] = '\n';
            g_cap[g_cap_len] = '\0';
        }
    }
    return 0;
}

/* The ERRTEST exec of #281, with the line numbers of the z/OS run:
 * no blank line before sub1 (the z/OS lines 18, 21 and 23 fix that).
 * Case F is a NOP here -- rexx370 has no INTERPRET yet, and an exec
 * containing one would not compile to bytecode at all. */
static const char ERRTEST[] =
    "/* REXX - ERRTEST: runtime error message format (rexx370 #281) */\n"
    "parse upper arg case .\n"
    "say 'ERRTEST' case 'start'\n"
    "select\n"
    "  when case = 'A' then x = 1/0\n"
    "  when case = 'B' then call sub1\n"
    "  when case = 'C' then v = outer(5)\n"
    "  when case = 'D' then z = 1 + ,\n"
    "                           1/0\n"
    "  when case = 'E' then x = nosuchfn(1)\n"
    "  when case = 'F' then nop\n"
    "  otherwise say 'usage: ERRTEST A|B|C|D|E|F'\n"
    "end\n"
    "say 'ERRTEST' case 'not reached'\n"
    "exit 0\n"
    "sub1:\n"
    "  say 'in sub1'\n"
    "  y = 1/0\n"
    "  return\n"
    "outer:\n"
    "  return inner(arg(1)) + 1\n"
    "inner:\n"
    "  return arg(1) / 0\n";

static void set_name(struct envblock *env, const char *name)
{
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)env->envblock_workblok_ext;
    strcpy(wk->wkbi_exec_name, name);
}

static void run_case(struct envblock *env, const char *arg,
                     const char *expected, const char *tag)
{
    int exit_rc = 0;
    char label[96];

    cap_reset();
    int rc = irx_exec_run(ERRTEST, (int)strlen(ERRTEST), arg,
                          (int)strlen(arg), &exit_rc, env);
    snprintf(label, sizeof(label), "%s: run ends in error", tag);
    CHECK(rc != 0, label);
    snprintf(label, sizeof(label), "%s: output as on z/OS", tag);
    CHECK(strcmp(g_cap, expected) == 0, label);
    if (strcmp(g_cap, expected) != 0)
    {
        printf("    expected:\n%s    got:\n%s", expected, g_cap);
    }
}

static void test_errtest(struct envblock *env)
{
    printf("\n[ERRTEST, compared with z/OS]\n");
    set_name(env, "ERRTEST");

    run_case(env, "A",
             "ERRTEST A start\n"
             "     5 +++  x = 1/0\n"
             "IRX0042I Error running ERRTEST, line 5: "
             "Arithmetic overflow/underflow\n",
             "A: 1/0 at top level");

    run_case(env, "B",
             "ERRTEST B start\n"
             "in sub1\n"
             "    18 +++   y = 1/0\n"
             "     6 +++  call sub1\n"
             "IRX0042I Error running ERRTEST, line 18: "
             "Arithmetic overflow/underflow\n",
             "B: in a CALLed routine");

    run_case(env, "C",
             "ERRTEST C start\n"
             "    23 +++    return arg(1) / 0\n"
             "    21 +++   return inner(arg(1)) + 1\n"
             "     7 +++  v = outer(5)\n"
             "IRX0042I Error running ERRTEST, line 23: "
             "Arithmetic overflow/underflow\n",
             "C: two function calls deep");

    /* z/OS shows the continued clause with its records padded to the
     * record length; the source here has no records, so the line end
     * becomes one blank and the continuation keeps its own blanks. */
    run_case(env, "D",
             "ERRTEST D start\n"
             "     8 +++  z = 1 + ,                            1/0\n"
             "IRX0042I Error running ERRTEST, line 8: "
             "Arithmetic overflow/underflow\n",
             "D: continued clause");

    run_case(env, "E",
             "ERRTEST E start\n"
             "    10 +++  x = nosuchfn(1)\n"
             "IRX0043I Error running ERRTEST, line 10: Routine not found\n",
             "E: routine not found");

    set_name(env, "");
}

/* The token-walk interpreter (frozen, CON-18) reports the line and the
 * message, with the whole source line as the one +++ entry: it keeps
 * no clause boundaries and no call stack for a full traceback. */
static void test_token_walk(struct envblock *env)
{
    printf("\n[token-walk: line and message]\n");
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)env->envblock_workblok_ext;
    int exit_rc = 0;
    const char *src = "say 'a'\n"
                      "  x = 1/0\n";

    set_name(env, "TW");
    wk->wkbi_use_bytecode = 0;
    cap_reset();
    int rc = irx_exec_run(src, (int)strlen(src), NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 1;
    CHECK(rc != 0, "token-walk: run ends in error");
    CHECK(strcmp(g_cap, "a\n"
                        "     2 +++ x = 1/0\n"
                        "IRX0042I Error running TW, line 2: "
                        "Arithmetic overflow/underflow\n") == 0,
          "token-walk: +++ line and IRX0042I");
    if (rc == 0 || strstr(g_cap, "IRX0042I") == NULL)
    {
        printf("    got:\n%s", g_cap);
    }

    /* A BIF that fails in the token walk does not go through the
     * parser's fail(), so no error line was recorded: the message
     * must still name the line, not fall back to the no-line form. */
    const char *src2 = "say 'b'\n"
                       "x = substr('abc', 0)\n";
    wk->wkbi_use_bytecode = 0;
    cap_reset();
    rc = irx_exec_run(src2, (int)strlen(src2), NULL, 0, &exit_rc, env);
    wk->wkbi_use_bytecode = 1;
    CHECK(strstr(g_cap, "IRX0040I Error running TW, line 2: ") != NULL,
          "token-walk BIF error: message names the line");
    if (strstr(g_cap, "IRX0040I Error running TW, line 2: ") == NULL)
    {
        printf("    got:\n%s", g_cap);
    }
    set_name(env, "");
}

static void test_no_message(struct envblock *env)
{
    printf("\n[no message without an error]\n");
    int exit_rc = 0;
    const char *src = "signal on syntax\n"
                      "x = 1/0\n"
                      "exit 1\n"
                      "syntax:\n"
                      "say 'trapped' rc\n"
                      "exit 3\n";
    cap_reset();
    int rc = irx_exec_run(src, (int)strlen(src), NULL, 0, &exit_rc, env);
    CHECK(rc == 0 && exit_rc == 3, "SIGNAL ON SYNTAX traps the error");
    /* RC holds the error number in the handler (SC28-1883-0 p.153,
     * #308). */
    CHECK(strcmp(g_cap, "trapped 42\n") == 0,
          "trapped: no traceback, RC = 42");
    if (strcmp(g_cap, "trapped 42\n") != 0)
    {
        printf("    got:\n%s", g_cap);
    }

    const char *src2 = "say 'a'\nexit 4\n";
    cap_reset();
    rc = irx_exec_run(src2, (int)strlen(src2), NULL, 0, &exit_rc, env);
    CHECK(rc == 0 && exit_rc == 4 && strcmp(g_cap, "a\n") == 0,
          "EXIT: no message");
}

static void test_forms(struct envblock *env)
{
    printf("\n[irx_emsg_syntax forms]\n");
    struct irx_emsg_clause c;

    set_name(env, "X");
    cap_reset();
    irx_emsg_syntax(env, SYNTAX_STORAGE, 0, NULL, 0);
    CHECK(strcmp(g_cap, "IRX0005I Machine storage exhausted\n") == 0,
          "line 0: form without 'Error running' (p.395)");

    c.line = 123456;
    c.level = 1;
    c.text = "say x";
    c.text_len = 5;
    cap_reset();
    irx_emsg_syntax(env, SYNTAX_BAD_ARITH, 123456, &c, 1);
    CHECK(strncmp(g_cap, "?23456 +++ say x\n", 17) == 0,
          "line > 99999: '?' + last 5 digits (p.67)");

    c.line = 7;
    c.level = 3;
    c.text = NULL;
    c.text_len = 0;
    cap_reset();
    irx_emsg_syntax(env, SYNTAX_NO_LABEL, 7, &c, 1);
    CHECK(strcmp(g_cap, "     7 +++   \n"
                        "IRX0016I Error running X, line 7: "
                        "Label not found\n") == 0,
          "no clause text: prefix and indentation only");

    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)env->envblock_workblok_ext;
    CHECK(wk->wkbi_error_number == SYNTAX_NO_LABEL &&
              wk->wkbi_error_line == 7,
          "error number and line recorded in the work block");
    set_name(env, "");
}

/* ------------------------------------------------------------------ */
/*  Expression nesting (#294)                                          */
/* ------------------------------------------------------------------ */

#define NEST_SRC_SIZE 1024

/* The nesting forms, with the largest depth z/OS runs (MIKE-TODO
 * rounds 2 and 5): a function call or parenthesis takes one entry of
 * the evaluation stack, a pending binary operator one, a prefix
 * operator two (irxpars.h). */
enum nest_kind
{
    NEST_CALLS,  /* x = abs(abs(...1...))     */
    NEST_BARE,   /* x = (((1)))               */
    NEST_LEFT,   /* x = ((1+1)+1)...          */
    NEST_RIGHT,  /* x = 1+(1+(...1))          */
    NEST_PREFIX, /* x = - - ... - 1           */
    NEST_POWER   /* x = 1**1**...**1          */
};

struct nest_form
{
    enum nest_kind kind;
    const char *name;
    int max;       /* deepest that runs */
    int fallback;  /* bytecode leaves the error to the token walk */
    int unbounded; /* no error 39 at any depth: max is only a sample */
};

/* NEST_POWER: ** has one priority, so it is evaluated left to right
 * (SC28-1883-0 p.15) and a chain holds no operator open; z/OS runs
 * 1**1**... 2000 deep (MIKE-TODO round 5).  300 is what fits in the
 * line buffer here (#267). */
#define NEST_POWER_DEEP 300

static const struct nest_form NEST_FORMS[] = {
    {NEST_CALLS, "abs()", IRX_EXPR_NEST_MAX, 0, 0},
    {NEST_BARE, "(((1)))", IRX_EXPR_NEST_MAX, 0, 0},
    {NEST_LEFT, "((1+1)+1)", IRX_EXPR_NEST_MAX - 1, 0, 0},
    {NEST_RIGHT, "1+(1+(1))", IRX_EXPR_NEST_MAX / 2, 0, 0},
    {NEST_PREFIX, "- - 1", IRX_EXPR_NEST_MAX / 2, 1, 0},
    {NEST_POWER, "1**1**1", NEST_POWER_DEEP, 0, 1},
};

static void nest_put(char *buf, int cap, int *off, const char *text,
                     int times)
{
    for (int i = 0; i < times; i++)
    {
        *off += snprintf(buf + *off, (size_t)(cap - *off), "%s", text);
    }
}

/* Line 2 of the exec: x = <depth levels of kind> around 1.  Writes
 * the value it gives to result. */
static void nest_line(char *buf, int cap, enum nest_kind kind, int depth,
                      char *result, int rcap)
{
    int off = snprintf(buf, (size_t)cap, "x = ");
    int value = 1;
    switch (kind)
    {
        case NEST_CALLS:
        {
            nest_put(buf, cap, &off, "abs(", depth);
            nest_put(buf, cap, &off, "1", 1);
            nest_put(buf, cap, &off, ")", depth);
            break;
        }
        case NEST_BARE:
        {
            nest_put(buf, cap, &off, "(", depth);
            nest_put(buf, cap, &off, "1", 1);
            nest_put(buf, cap, &off, ")", depth);
            break;
        }
        case NEST_LEFT:
        {
            nest_put(buf, cap, &off, "(", depth);
            nest_put(buf, cap, &off, "1", 1);
            nest_put(buf, cap, &off, "+1)", depth);
            value = depth + 1;
            break;
        }
        case NEST_RIGHT:
        {
            nest_put(buf, cap, &off, "1+(", depth);
            nest_put(buf, cap, &off, "1", 1);
            nest_put(buf, cap, &off, ")", depth);
            value = depth + 1;
            break;
        }
        case NEST_PREFIX:
        {
            nest_put(buf, cap, &off, "- ", depth);
            nest_put(buf, cap, &off, "1", 1);
            value = depth % 2 ? -1 : 1;
            break;
        }
        case NEST_POWER:
        {
            nest_put(buf, cap, &off, "1", 1);
            nest_put(buf, cap, &off, "**1", depth);
            break;
        }
    }
    snprintf(result, (size_t)rcap, "%d", value);
}

/* Run src on one path; returns irx_exec_run's rc. */
static int nest_run(struct envblock *env, const char *src, int bytecode,
                    int *exit_rc, int *fallback)
{
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)env->envblock_workblok_ext;
    wk->wkbi_use_bytecode = bytecode;
    wk->wkbi_bc_fallback_count = 0;
    cap_reset();
    int rc = irx_exec_run(src, (int)strlen(src), NULL, 0, exit_rc, env);
    *fallback = wk->wkbi_bc_fallback_count;
    wk->wkbi_use_bytecode = 1;
    return rc;
}

/* One form at its z/OS maximum (runs) and one level deeper (error 39
 * on the clause, with its line), on both paths. */
static void nest_form(struct envblock *env, const struct nest_form *f)
{
    char line[NEST_SRC_SIZE];
    char src[NEST_SRC_SIZE + 64];
    char expect[NEST_SRC_SIZE + 128];
    char result[16];
    char label[96];
    int exit_rc = 0;
    int fallback = 0;

    for (int bc = 1; bc >= 0; bc--)
    {
        const char *path = bc ? "bytecode" : "token-walk";

        nest_line(line, (int)sizeof(line), f->kind, f->max, result,
                  (int)sizeof(result));
        snprintf(src, sizeof(src), "say 'a'\n%s\nsay x\n", line);
        int rc = nest_run(env, src, bc, &exit_rc, &fallback);
        snprintf(expect, sizeof(expect), "a\n%s\n", result);
        snprintf(label, sizeof(label), "%s %d deep runs (%s)", f->name,
                 f->max, path);
        CHECK(rc == 0 && strcmp(g_cap, expect) == 0 && fallback == 0,
              label);
        if (rc != 0 || strcmp(g_cap, expect) != 0 || fallback != 0)
        {
            printf("    rc=%d fallback=%d got:\n%s", rc, fallback, g_cap);
        }

        if (f->unbounded)
        {
            continue;
        }

        nest_line(line, (int)sizeof(line), f->kind, f->max + 1, result,
                  (int)sizeof(result));
        snprintf(src, sizeof(src), "say 'a'\n%s\nsay x\n", line);
        rc = nest_run(env, src, bc, &exit_rc, &fallback);
        snprintf(expect, sizeof(expect),
                 "a\n     2 +++ %s\n"
                 "IRX0039I Error running NEST, line 2: "
                 "Evaluation stack overflow\n",
                 line);
        snprintf(label, sizeof(label), "%s %d deep: error 39 (%s)",
                 f->name, f->max + 1, path);
        int want_fallback = bc && f->fallback;
        CHECK(rc != 0 && strcmp(g_cap, expect) == 0 &&
                  (!bc || fallback == want_fallback),
              label);
        if (rc == 0 || strcmp(g_cap, expect) != 0 ||
            (bc && fallback != want_fallback))
        {
            printf("    rc=%d fallback=%d got:\n%s", rc, fallback, g_cap);
        }
    }
}

static void test_nesting(struct envblock *env)
{
    printf("\n[expression nesting: error 39 (#294)]\n");
    set_name(env, "NEST");

    for (size_t i = 0; i < sizeof(NEST_FORMS) / sizeof(NEST_FORMS[0]); i++)
    {
        nest_form(env, &NEST_FORMS[i]);
    }

    /* The count is back to 0 after each clause: two clauses at the
     * limit, one after the other, both run. */
    char line[NEST_SRC_SIZE];
    char src[2 * NEST_SRC_SIZE + 64];
    char result[16];
    int exit_rc = 0;
    int fallback = 0;
    nest_line(line, (int)sizeof(line), NEST_CALLS, IRX_EXPR_NEST_MAX, result,
              (int)sizeof(result));
    snprintf(src, sizeof(src), "%s\ny%s\nsay x + y\n", line, line + 1);
    for (int bc = 1; bc >= 0; bc--)
    {
        int rc = nest_run(env, src, bc, &exit_rc, &fallback);
        CHECK(rc == 0 && strcmp(g_cap, "2\n") == 0,
              bc ? "two clauses at the limit (bytecode)"
                 : "two clauses at the limit (token-walk)");
    }

    /* SIGNAL ON SYNTAX traps error 39 like any other, and the clauses
     * before it ran; RC holds the number in the handler (#308).
     * Bytecode only: SIGNAL ON is a no-op on the frozen token-walk path
     * (CON-18). */
    nest_line(line, (int)sizeof(line), NEST_CALLS, IRX_EXPR_NEST_MAX + 1,
              result, (int)sizeof(result));
    snprintf(src, sizeof(src),
             "signal on syntax\nsay 'a'\n%s\nexit 1\nsyntax:\n"
             "say 'trapped' rc\nexit 3\n",
             line);
    int rc = nest_run(env, src, 1, &exit_rc, &fallback);
    CHECK(rc == 0 && exit_rc == 3 && strcmp(g_cap, "a\ntrapped 39\n") == 0 &&
              fallback == 0,
          "SIGNAL ON SYNTAX traps error 39, RC = 39 (bytecode)");
    if (rc != 0 || exit_rc != 3 || strcmp(g_cap, "a\ntrapped 39\n") != 0 ||
        fallback != 0)
    {
        printf("    rc=%d exit=%d fallback=%d got:\n%s", rc, exit_rc,
               fallback, g_cap);
    }
    set_name(env, "");
}

/* Internal calls share the control stack: 250 active calls run, the
 * 251st is error 11 "Control stack full" (z/OS, HANDOVER; #295).  The
 * exact boundary is still to be confirmed on z/OS.  Bytecode path: VM
 * calls cost no C stack, so only the count limits them. */
#define CALL_DEPTH_MAX 250
#define CALL_SRC_SIZE  512

static void test_call_depth(struct envblock *env)
{
    char src[CALL_SRC_SIZE];
    char expect[64];
    int exit_rc = 0;
    int fallback = 0;
    const char *body = "exit 0\n"
                       "down: procedure\n"
                       "parse arg n\n"
                       "if n = 0 then return 0\n"
                       "return down(n - 1) + 1\n";

    printf("\n[call depth: error 11 (#295)]\n");
    set_name(env, "NEST");

    /* down(k) holds k + 1 calls active at its deepest. */
    snprintf(src, sizeof(src), "say down(%d)\n%s", CALL_DEPTH_MAX - 1, body);
    int rc = nest_run(env, src, 1, &exit_rc, &fallback);
    snprintf(expect, sizeof(expect), "%d\n", CALL_DEPTH_MAX - 1);
    CHECK(rc == 0 && strcmp(g_cap, expect) == 0 && fallback == 0,
          "250 active calls run");
    if (rc != 0 || strcmp(g_cap, expect) != 0)
    {
        printf("    rc=%d fallback=%d got:\n%.300s\n", rc, fallback, g_cap);
    }

    /* The traceback has a line per active call, more than the capture
     * buffer holds, so the message is checked through the number and
     * line irx_emsg_syntax records. */
    snprintf(src, sizeof(src), "say down(%d)\n%s", CALL_DEPTH_MAX, body);
    rc = nest_run(env, src, 1, &exit_rc, &fallback);
    const struct irx_wkblk_int *wk =
        (const struct irx_wkblk_int *)env->envblock_workblok_ext;
    CHECK(rc != 0 && wk->wkbi_error_number == SYNTAX_CTL_STACK &&
              wk->wkbi_error_line == 6 &&
              strncmp(g_cap, "     6 +++", 10) == 0,
          "251 active calls: error 11 on line 6");
    if (rc == 0 || wk->wkbi_error_number != SYNTAX_CTL_STACK ||
        wk->wkbi_error_line != 6)
    {
        printf("    rc=%d error=%d line=%d\n", rc, wk->wkbi_error_number,
               wk->wkbi_error_line);
    }

    /* SIGNAL ON SYNTAX traps it like any other error. */
    snprintf(src, sizeof(src),
             "signal on syntax\nsay down(%d)\nexit 1\n"
             "syntax:\nsay 'trapped' rc\nexit 3\n%s",
             CALL_DEPTH_MAX, body + strlen("exit 0\n"));
    rc = nest_run(env, src, 1, &exit_rc, &fallback);
    CHECK(rc == 0 && exit_rc == 3 && strstr(g_cap, "trapped 11\n") != NULL,
          "SIGNAL ON SYNTAX traps error 11, RC = 11");
    if (rc != 0 || exit_rc != 3 || strstr(g_cap, "trapped 11\n") == NULL)
    {
        printf("    rc=%d exit=%d got:\n%.300s\n", rc, exit_rc, g_cap);
    }
    set_name(env, "");
}

int main(void)
{
    struct envblock *env = NULL;

    printf("=== #281 error message and traceback ===\n");
    if (irxinit(NULL, &env) != 0 || env == NULL)
    {
        fprintf(stderr, "tstberr: irxinit failed\n");
        return 1;
    }
    struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
    exte->io_routine = (void *)capture_io;

    test_errtest(env);
    test_token_walk(env);
    test_no_message(env);
    test_forms(env);
    test_nesting(env);
    test_call_depth(env);

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");
    return tests_failed > 0 ? 1 : 0;
}
