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
                      "say 'trapped'\n"
                      "exit 3\n";
    cap_reset();
    int rc = irx_exec_run(src, (int)strlen(src), NULL, 0, &exit_rc, env);
    CHECK(rc == 0 && exit_rc == 3, "SIGNAL ON SYNTAX traps the error");
    CHECK(strcmp(g_cap, "trapped\n") == 0, "trapped: no traceback");

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

    irxterm(env);

    printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
    {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ---\n");
    return tests_failed > 0 ? 1 : 0;
}
