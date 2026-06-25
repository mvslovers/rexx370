/* ------------------------------------------------------------------ */
/*  test/host/tstbc_rexxcps.c — WP-BC-06 bytecode REXXCPS driver      */
/*                                                                     */
/*  Runs a stripped REXXCPS-derived kernel via token-walk and the      */
/*  bytecode path.  Verifies that both paths produce identical output  */
/*  and reports wall-clock timing for each.                            */
/*                                                                     */
/*  The kernel is derived from the inner body of REXXCPS 2.2:          */
/*  timing BIFs (TIME), trace instructions (TRACE VALUE), and          */
/*  SIGNAL/ADDRESS constructs are removed so the bytecode compiler     */
/*  can compile the whole source without IRXBC_ERR_UNSUP.              */
/*                                                                     */
/*  Build (Linux):                                                      */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \         */
/*        -Wall -Wextra -std=gnu99 -O2 \                               */
/*        -o /tmp/tstbc_rexxcps test/host/tstbc_rexxcps.c \            */
/*        'src/irx#bvm.c'   'src/irx#bcom.c'  'src/irx#bctl.c' \      */
/*        'src/irx#init.c'  'src/irx#term.c'  'src/irx#stor.c' \      */
/*        'src/irx#anch.c'  'src/irx#env.c'   'src/irx#uid.c'  \      */
/*        'src/irx#msid.c'  'src/irx#cond.c'  'src/irx#bif.c'  \      */
/*        'src/irx#bifs.c'  'src/irx#io.c'    'src/irx#lstr.c' \      */
/*        'src/irx#tokn.c'  'src/irx#vpol.c'  'src/irx#pars.c' \      */
/*        'src/irx#ctrl.c'  'src/irx#exec.c'  'src/irx#arith.c' \     */
/*        '../lstring370/src/lstr#cor.c'  \                            */
/*        '../lstring370/src/lstr#cvt.c'  \                            */
/*        '../lstring370/src/lstr#fmt.c'  \                            */
/*        '../lstring370/src/lstr#srch.c' \                            */
/*        '../lstring370/src/lstr#sub.c'  \                            */
/*        '../lstring370/src/lstr#wrd.c'  \                            */
/*        '../lstring370/src/lstr#xlt.c'  \                            */
/*        && /tmp/tstbc_rexxcps                                         */
/*                                                                     */
/*  Usage:                                                              */
/*    ./tstbc_rexxcps [--iterations=N]                                  */
/*                                                                     */
/*  (c) 2026 mvslovers - REXX/370 Project                              */
/* ------------------------------------------------------------------ */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxwkblk.h"

#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

/* ------------------------------------------------------------------ */
/*  REXXCPS inner kernel (stripped version)                            */
/*                                                                     */
/*  Removed from the original REXXCPS 2.2:                            */
/*   - SIGNAL ON NOVALUE (bytecode UNSUP)                              */
/*   - TRACE VALUE / TRACE OFF (bytecode UNSUP)                        */
/*   - ADDRESS VALUE ADDRESS() (bytecode UNSUP)                        */
/*   - TIME('R') calibration and adjustment loops (no timing BIF)      */
/*   - DO J=1.1 TO 2.2 BY 1.1 simplified to DO J=1 TO 2              */
/*     (avoids decimal loop parameters in the inner hot path)          */
/*   - Second CALL argument group (commas in CALL: bytecode UNSUP)     */
/*                                                                     */
/*  The outer iteration count is set by the 'count' variable.          */
/*  Increase N (--iterations) to extend the measurement window.       */
/* ------------------------------------------------------------------ */
static const char KERNEL_PREFIX[] =
    "count = ";

/*  Note: PARSE variable-delimiter (varname) is not yet supported in   */
/*  the bytecode compiler.  The kernel uses only literal/absolute/     */
/*  relative and word-split PARSE templates.                           */
static const char KERNEL_BODY[] =
    "\n"
    "do i = 1 to count\n"
    "  flag = 0\n"
    "  do loop = 1 to 14\n"
    "    key1 = 'Key Bee'\n"
    "    acompound.key1.loop = substr(12345678, 6, 2)\n"
    "    if flag = acompound.key1.loop then say 'Failed1'\n"
    "    do j = 1 to 2\n"
    "      if j > acompound.key1.loop then say 'Failed2'\n"
    "      if 17 < length(j) - 1 then say 'Failed3'\n"
    "      if j = 'foobar' then say 'Failed4'\n"
    "      if substr(1234, 1, 1) = 9 then say 'Failed5'\n"
    "      if word(key1, 1) = '?' then say 'Failed6'\n"
    "      if j < 5 then do\n"
    "        acompound.key1.loop = acompound.key1.loop + 1\n"
    "        if j = 2 then leave\n"
    "      end\n"
    "      iterate\n"
    "    end\n"
    "    avar. = 1\n"
    "    select\n"
    "      when flag = 'string' then say 'FailedS1'\n"
    "      when avar.flag.2 = 0 then say 'FailedS2'\n"
    "      when flag = 5 + 99 then say 'FailedS3'\n"
    "      when flag then avar.1.2 = avar.1.2 * 1.1\n"
    "      when flag == 0 then flag = 0\n"
    "    end\n"
    "    if 1 then flag = 1\n"
    "    select\n"
    "      when flag == 'ring' then say 'FailedT1'\n"
    "      when avar.flag.3 = 0 then say 'FailedT2'\n"
    "      when flag then avar.1.2 = avar.1.2 * 1.1\n"
    "      when flag == 0 then flag = 1\n"
    "    end\n"
    "    parse value 'Foo Bar' with v1 +5 v2 .\n"
    "    call subroutine 'with' 2 'args'\n"
    "    parse value 'This is an awfully boring program' with p1 p2 p3 p4 p5\n"
    "  end loop\n"
    "end\n"
    "say count\n"
    "exit\n"
    "subroutine:\n"
    "  parse upper arg a1 a2 a3 .\n"
    "  parse var a3 b1 b2 b3 .\n"
    "  do 1\n"
    "    rc = a1 a2 a3\n"
    "    parse var rc c1 c2 c3\n"
    "  end\n"
    "  return\n";

/* ------------------------------------------------------------------ */
/*  Output capture                                                     */
/* ------------------------------------------------------------------ */

#define CAPBUF 16384

static char g_cap[CAPBUF];
static int g_cap_len;

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
        if (g_cap_len + n + 1 < CAPBUF)
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
/*  Monotonic wall-clock seconds. Host uses CLOCK_MONOTONIC; MVS has no
 *  portable monotonic clock here, so timing reads 0 there. This is a
 *  correctness test (token-walk vs bytecode equivalence) that also
 *  reports timing on the host.                                        */
/* ------------------------------------------------------------------ */
#ifndef __MVS__
#include <time.h>
static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1.0e9;
}
#else
static double now_s(void) { return 0.0; }
#endif

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    int iterations = 10;
    char src[2048];
    int src_len;
    struct envblock *env = NULL;
    struct irx_wkblk_int *wk;
    char tw_out[CAPBUF];
    char bc_out[CAPBUF];
    double t0;
    double tw_wall, bc_wall;
    int rc;
    int exit_rc = 0;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (strncmp(argv[i], "--iterations=", 13) == 0)
        {
            iterations = atoi(argv[i] + 13);
            if (iterations < 1)
            {
                iterations = 1;
            }
        }
        else
        {
            fprintf(stderr,
                    "tstbc_rexxcps: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "usage: tstbc_rexxcps [--iterations=N]\n");
            return 1;
        }
    }

    /* Build source: "count = N\n<BODY>" */
    src_len = snprintf(src, sizeof(src), "%s%d%s",
                       KERNEL_PREFIX, iterations, KERNEL_BODY);
    if (src_len < 0 || src_len >= (int)sizeof(src))
    {
        fprintf(stderr, "tstbc_rexxcps: source buffer overflow\n");
        return 1;
    }

    rc = irxinit(NULL, &env);
    if (rc != 0 || env == NULL)
    {
        fprintf(stderr, "tstbc_rexxcps: irxinit failed rc=%d\n", rc);
        return 1;
    }

    /* Install output capture */
    wk = (struct irx_wkblk_int *)env->envblock_userfield;
    if (wk == NULL)
    {
        fprintf(stderr, "tstbc_rexxcps: no work block\n");
        irxterm(env);
        return 1;
    }
    struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
    if (exte != NULL)
    {
        exte->io_routine = (void *)capture_io;
    }

    /* ---- Token-walk run ------------------------------------------- */
    cap_reset();
    wk->wkbi_use_bytecode = 0;
    t0 = now_s();
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    tw_wall = now_s() - t0;
    memcpy(tw_out, g_cap, (size_t)(g_cap_len + 1));
    if (rc != 0)
    {
        fprintf(stderr,
                "tstbc_rexxcps: token-walk failed rc=%d exit=%d\n",
                rc, exit_rc);
        irxterm(env);
        return 1;
    }

    /* ---- Bytecode run --------------------------------------------- */
    cap_reset();
    wk->wkbi_use_bytecode = 1;
    t0 = now_s();
    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, env);
    bc_wall = now_s() - t0;
    wk->wkbi_use_bytecode = 0;
    memcpy(bc_out, g_cap, (size_t)(g_cap_len + 1));
    if (rc != 0)
    {
        fprintf(stderr,
                "tstbc_rexxcps: bytecode run failed rc=%d exit=%d\n",
                rc, exit_rc);
        irxterm(env);
        return 1;
    }

    irxterm(env);

    /* ---- Results -------------------------------------------------- */
    printf("=== TSTRXCPS: REXXCPS kernel, %d iterations ===\n", iterations);
#ifndef __MVS__
    printf("  token-walk  : %.3f s\n", tw_wall);
    printf("  bytecode    : %.3f s\n", bc_wall);
    if (bc_wall > 0.0)
    {
        printf("  speedup     : %.2fx\n", tw_wall / bc_wall);
    }
#endif

    /* Correctness gate: the token-walk and bytecode paths must produce the
     * identical output for the same exec (the timing above is diagnostic). */
    if (strcmp(tw_out, bc_out) != 0)
    {
        printf("  FAIL: token-walk and bytecode output match\n");
        fprintf(stderr, "  token-walk: [%s]\n  bytecode:   [%s]\n",
                tw_out, bc_out);
        return 1;
    }
    printf("  PASS: token-walk and bytecode output match\n");
    return 0;
}
