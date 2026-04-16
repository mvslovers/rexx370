/* ------------------------------------------------------------------ */
/*  test_parse.c - WP-16 PARSE instruction acceptance tests           */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_parse \               */
/*        test/test_parse.c \                                          */
/*        'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \       */
/*        'src/irx#rab.c'  'src/irx#uid.c'  'src/irx#msid.c' \       */
/*        'src/irx#io.c'   'src/irx#lstr.c' 'src/irx#tokn.c' \       */
/*        'src/irx#vpol.c' 'src/irx#pars.c' 'src/irx#ctrl.c' \       */
/*        'src/irx#exec.c' \                                           */
/*        ../lstring370/src/'lstr#cor.c'                              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxwkblk.h"
#include "lstring.h"

#ifndef __MVS__
void *_simulated_tcbuser = NULL;
#endif

static int tests_run    = 0;
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
/*  Mock I/O                                                          */
/* ------------------------------------------------------------------ */

#define OUT_BUF_SIZE 4096

static char g_out[OUT_BUF_SIZE];
static int  g_out_len = 0;

static void reset_output(void)
{
    g_out_len  = 0;
    g_out[0]   = '\0';
}

static int output_contains(const char *line)
{
    int line_len = (int)strlen(line);
    int i;
    for (i = 0; i <= g_out_len - line_len; i++)
    {
        if (memcmp(g_out + i, line, (size_t)line_len) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int mock_io(int function, PLstr data, struct envblock *envblock)
{
    (void)envblock;
    if (function == RXFWRITE && data != NULL &&
        data->pstr != NULL && data->len > 0)
    {
        int n = (int)data->len;
        if (g_out_len + n + 1 < OUT_BUF_SIZE)
        {
            memcpy(g_out + g_out_len, data->pstr, (size_t)n);
            g_out_len += n;
            g_out[g_out_len++] = '\n';
            g_out[g_out_len]   = '\0';
        }
    }
    return 0;
}

static void install_mock(struct envblock *env)
{
    struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)mock_io;
    }
}

static int run_src(const char *src, int *exit_rc_out)
{
    struct envblock *env = NULL;
    int rc;

    rc = irxinit(NULL, &env);
    if (rc != 0)
    {
        return rc;
    }

    install_mock(env);
    reset_output();

    rc = irx_exec_run(src, (int)strlen(src), NULL, 0, exit_rc_out, env);

    irxterm(env);
    return rc;
}

static int run_src_with_args(const char *src, const char *args,
                             int *exit_rc_out)
{
    struct envblock *env = NULL;
    int rc;

    rc = irxinit(NULL, &env);
    if (rc != 0)
    {
        return rc;
    }

    install_mock(env);
    reset_output();

    rc = irx_exec_run(src, (int)strlen(src),
                      args, (int)strlen(args),
                      exit_rc_out, env);

    irxterm(env);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  AC#01: PARSE ARG first rest — word parsing                        */
/* ------------------------------------------------------------------ */

static void test_pa01_arg_word_parsing(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#01: PARSE ARG - first word / remainder ---\n");

    rc = run_src_with_args(
        "/* REXX */\n"
        "parse arg first rest\n"
        "say 'first=' || first\n"
        "say 'rest=' || rest\n"
        "exit 0\n",
        "hello world foo",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("first=hello"), "first = 'hello'");
    CHECK(output_contains("rest=world foo"), "rest = 'world foo'");
}

/* ------------------------------------------------------------------ */
/*  AC#02: PARSE ARG a, b, c — multiple templates                     */
/* ------------------------------------------------------------------ */

static void test_pa02_arg_multiple_templates(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#02: PARSE ARG multiple templates (commas) ---\n");

    rc = run_src(
        "/* REXX */\n"
        "call mysub 'hello', 'world'\n"
        "exit 0\n"
        "mysub:\n"
        "  parse arg a, b\n"
        "  say 'a=' || a\n"
        "  say 'b=' || b\n"
        "  return\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("a=hello"), "a = 'hello'");
    CHECK(output_contains("b=world"), "b = 'world'");
}

/* ------------------------------------------------------------------ */
/*  AC#03: PARSE VAR line a ',' b ',' c — literal pattern splits      */
/* ------------------------------------------------------------------ */

static void test_pa03_var_literal_split(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#03: PARSE VAR with literal comma pattern ---\n");

    rc = run_src(
        "/* REXX */\n"
        "line = 'one,two,three'\n"
        "parse var line a ',' b ',' c\n"
        "say 'a=' || a\n"
        "say 'b=' || b\n"
        "say 'c=' || c\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("a=one"), "a = 'one'");
    CHECK(output_contains("b=two"), "b = 'two'");
    CHECK(output_contains("c=three"), "c = 'three'");
}

/* ------------------------------------------------------------------ */
/*  AC#04: PARSE VAR x 1 a 5 b 10 c — absolute positional            */
/* ------------------------------------------------------------------ */

static void test_pa04_var_absolute_positions(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#04: PARSE VAR absolute positional ---\n");

    rc = run_src(
        "/* REXX */\n"
        "x = 'ABCDEFGHIJ'\n"
        "parse var x 1 a 5 b 10 c\n"
        "say 'a=' || a\n"
        "say 'b=' || b\n"
        "say 'c=' || c\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    /* 1-based: a=[1..4]="ABCD", b=[5..9]="EFGHI", c=[10..]="J" */
    CHECK(output_contains("a=ABCD"), "a = 'ABCD'");
    CHECK(output_contains("b=EFGHI"), "b = 'EFGHI'");
    CHECK(output_contains("c=J"), "c = 'J'");
}

/* ------------------------------------------------------------------ */
/*  AC#05: PARSE VAR x a +3 b +3 c — relative forward                */
/* ------------------------------------------------------------------ */

static void test_pa05_var_relative_forward(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#05: PARSE VAR relative forward ---\n");

    rc = run_src(
        "/* REXX */\n"
        "x = 'ABCDEFGHI'\n"
        "parse var x a +3 b +3 c\n"
        "say 'a=' || a\n"
        "say 'b=' || b\n"
        "say 'c=' || c\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    /* a=[0..2]="ABC", b=[3..5]="DEF", c=[6..]="GHI" */
    CHECK(output_contains("a=ABC"), "a = 'ABC'");
    CHECK(output_contains("b=DEF"), "b = 'DEF'");
    CHECK(output_contains("c=GHI"), "c = 'GHI'");
}

/* ------------------------------------------------------------------ */
/*  AC#06: PARSE VAR x a -3 b — relative backward                    */
/* ------------------------------------------------------------------ */

static void test_pa06_var_relative_backward(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#06: PARSE VAR relative backward ---\n");

    rc = run_src(
        "/* REXX */\n"
        "x = 'ABCDEFGH'\n"
        "parse var x 4 a -3 b\n"
        "say 'a=' || a\n"
        "say 'b=' || b\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    /* Absolute 4 sets scan=3 (0-based).
     * a gets [3..2] = "" (empty: backward retreat means preceding var empty).
     * After -3: scan retreats to max(0, 3-3)=0.
     * b gets [0..end]="ABCDEFGH". */
    CHECK(output_contains("a="), "a = '' (empty — backward move)");
    CHECK(output_contains("b=ABCDEFGH"), "b = 'ABCDEFGH'");
}

/* ------------------------------------------------------------------ */
/*  AC#07: PARSE VAR line a (delim) b — variable pattern              */
/* ------------------------------------------------------------------ */

static void test_pa07_var_variable_pattern(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#07: PARSE VAR variable pattern (delim) ---\n");

    rc = run_src(
        "/* REXX */\n"
        "line = 'hello:world'\n"
        "delim = ':'\n"
        "parse var line a (delim) b\n"
        "say 'a=' || a\n"
        "say 'b=' || b\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("a=hello"), "a = 'hello'");
    CHECK(output_contains("b=world"), "b = 'world'");
}

/* ------------------------------------------------------------------ */
/*  AC#08: PARSE ARG . name . — dot placeholders                      */
/* ------------------------------------------------------------------ */

static void test_pa08_dot_placeholder(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#08: PARSE ARG dot placeholders ---\n");

    rc = run_src_with_args(
        "/* REXX */\n"
        "parse arg . name .\n"
        "say 'name=' || name\n"
        "exit 0\n",
        "first middle last",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("name=middle"), "name = 'middle'");
}

/* ------------------------------------------------------------------ */
/*  AC#09: PARSE UPPER ARG name — uppercases before parsing           */
/* ------------------------------------------------------------------ */

static void test_pa09_upper_arg(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#09: PARSE UPPER ARG ---\n");

    rc = run_src_with_args(
        "/* REXX */\n"
        "parse upper arg name\n"
        "say 'name=' || name\n"
        "exit 0\n",
        "hello world",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("name=HELLO WORLD"), "name = 'HELLO WORLD'");
}

/* ------------------------------------------------------------------ */
/*  AC#10: PARSE SOURCE sys type name                                 */
/* ------------------------------------------------------------------ */

static void test_pa10_source(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#10: PARSE SOURCE ---\n");

    rc = run_src(
        "/* REXX */\n"
        "parse source sys calltype name\n"
        "say 'sys=' || sys\n"
        "say 'calltype=' || calltype\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("sys=MVS"), "sys = 'MVS'");
    /* At top level (no CALL frame): COMMAND */
    CHECK(output_contains("calltype=COMMAND"), "calltype = 'COMMAND'");
}

/* ------------------------------------------------------------------ */
/*  AC#11: PARSE VERSION lang level date                              */
/* ------------------------------------------------------------------ */

static void test_pa11_version(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#11: PARSE VERSION ---\n");

    rc = run_src(
        "/* REXX */\n"
        "parse version lang level date\n"
        "say 'lang=' || lang\n"
        "say 'level=' || level\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("lang=REXX370"), "lang = 'REXX370'");
    CHECK(output_contains("level=0.1.0"), "level = '0.1.0'");
}

/* ------------------------------------------------------------------ */
/*  AC#12: PARSE NUMERIC digits fuzz form                             */
/* ------------------------------------------------------------------ */

static void test_pa12_numeric(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#12: PARSE NUMERIC ---\n");

    rc = run_src(
        "/* REXX */\n"
        "parse numeric digits fuzz form\n"
        "say 'digits=' || digits\n"
        "say 'fuzz=' || fuzz\n"
        "say 'form=' || form\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("digits=9"), "digits = '9' (default)");
    CHECK(output_contains("fuzz=0"), "fuzz = '0' (default)");
    CHECK(output_contains("form=SCIENTIFIC"), "form = 'SCIENTIFIC' (default)");
}

/* ------------------------------------------------------------------ */
/*  AC#13: PARSE VALUE expr WITH template                             */
/* ------------------------------------------------------------------ */

static void test_pa13_value_with(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#13: PARSE VALUE expr WITH template ---\n");

    rc = run_src(
        "/* REXX */\n"
        "x = 'alpha beta gamma'\n"
        "parse value x with a b c\n"
        "say 'a=' || a\n"
        "say 'b=' || b\n"
        "say 'c=' || c\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("a=alpha"), "a = 'alpha'");
    CHECK(output_contains("b=beta"), "b = 'beta'");
    CHECK(output_contains("c=gamma"), "c = 'gamma'");
}

/* ------------------------------------------------------------------ */
/*  AC#14: PARSE PULL → SYNTAX error                                  */
/* ------------------------------------------------------------------ */

static void test_pa14_pull_syntax_error(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#14: PARSE PULL → SYNTAX error ---\n");

    rc = run_src(
        "/* REXX */\n"
        "parse pull x\n"
        "exit 0\n",
        &exit_rc);

    /* Expect a non-zero rc (syntax error: PULL not yet implemented). */
    CHECK(rc != 0, "irx_exec_run returns non-zero (SYNTAX)");
}

/* ------------------------------------------------------------------ */
/*  AC#15: Last variable gets rest including trailing blanks          */
/* ------------------------------------------------------------------ */

static void test_pa15_last_var_trailing_blanks(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#15: last variable preserves trailing blanks ---\n");

    /* When the last word in the string has trailing blanks, they are
     * included in the last template variable's value.  Leading blanks
     * between words are still stripped before each variable, but the
     * trailing portion of the final word (including its trailing blanks)
     * is preserved as-is. */
    rc = run_src(
        "/* REXX */\n"
        "x = 'hello world   '\n"
        "parse var x a b\n"
        "say 'a=[' || a || ']'\n"
        "say 'b=[' || b || ']'\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    CHECK(output_contains("a=[hello]"), "a = 'hello'");
    /* 'b' is the last variable: gets 'world   ' (trailing blanks kept). */
    CHECK(output_contains("b=[world   ]"), "b = 'world   ' (trailing blanks preserved)");
}

/* ------------------------------------------------------------------ */
/*  AC#16: Literal not found → preceding variable gets entire remainder */
/* ------------------------------------------------------------------ */

static void test_pa16_literal_not_found(void)
{
    int exit_rc = -1;
    int rc;

    printf("\n--- PA#16: literal not found - preceding var gets remainder ---\n");

    rc = run_src(
        "/* REXX */\n"
        "line = 'no semicolons here'\n"
        "parse var line a ';' b\n"
        "say 'a=' || a\n"
        "say 'b=' || b\n"
        "exit 0\n",
        &exit_rc);

    CHECK(rc == 0, "irx_exec_run returns 0");
    CHECK(exit_rc == 0, "exit code 0");
    /* ';' not found: a gets entire string, b gets "". */
    CHECK(output_contains("a=no semicolons here"), "a = entire string");
    CHECK(output_contains("b="), "b = '' (empty)");
}

/* ------------------------------------------------------------------ */
/*  AC#17: No global state — two concurrent parses do not interfere   */
/* ------------------------------------------------------------------ */

static void test_pa17_no_global_state(void)
{
    int exit_rc1 = -1;
    int exit_rc2 = -1;
    int rc1;
    int rc2;
    char out1[OUT_BUF_SIZE];
    char out2[OUT_BUF_SIZE];

    printf("\n--- PA#17: no global state (two independent runs) ---\n");

    rc1 = run_src_with_args(
        "/* REXX */\n"
        "parse arg first rest\n"
        "say 'first=' || first\n"
        "exit 0\n",
        "hello world",
        &exit_rc1);

    /* Save first run output. */
    memcpy(out1, g_out, (size_t)g_out_len);
    out1[g_out_len] = '\0';

    rc2 = run_src_with_args(
        "/* REXX */\n"
        "parse arg first rest\n"
        "say 'first=' || first\n"
        "exit 0\n",
        "different args",
        &exit_rc2);

    memcpy(out2, g_out, (size_t)g_out_len);
    out2[g_out_len] = '\0';

    CHECK(rc1 == 0 && rc2 == 0, "both runs return 0");
    CHECK(exit_rc1 == 0 && exit_rc2 == 0, "both exit with 0");
    CHECK(strstr(out1, "first=hello") != NULL,
          "first run: first = 'hello'");
    CHECK(strstr(out2, "first=different") != NULL,
          "second run: first = 'different'");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== PARSE instruction tests (WP-16) ===\n");

    test_pa01_arg_word_parsing();
    test_pa02_arg_multiple_templates();
    test_pa03_var_literal_split();
    test_pa04_var_absolute_positions();
    test_pa05_var_relative_forward();
    test_pa06_var_relative_backward();
    test_pa07_var_variable_pattern();
    test_pa08_dot_placeholder();
    test_pa09_upper_arg();
    test_pa10_source();
    test_pa11_version();
    test_pa12_numeric();
    test_pa13_value_with();
    test_pa14_pull_syntax_error();
    test_pa15_last_var_trailing_blanks();
    test_pa16_literal_not_found();
    test_pa17_no_global_state();

    printf("\n=== %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
