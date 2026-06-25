/* ------------------------------------------------------------------ */
/*  test/host/tstcps_host.c — WP-PERF-01 profiling driver             */
/*                                                                    */
/*  Exercises the engine with a synthetic REXX microbench designed    */
/*  to cover the hot paths profiled in REXXCPS-style workloads:       */
/*  variable access, BIF dispatch, arithmetic, DO loops, CALL/RETURN, */
/*  and compound variables.                                            */
/*                                                                    */
/*  Build (Linux):                                                     */
/*    See scripts/host-profile.sh for the canonical build command.    */
/*    Quick form (no -pg):                                             */
/*      gcc -I include -I contrib/lstring370-0.1.0-dev/include \      */
/*          -Wall -Wextra -std=gnu99 -O0 -g \                         */
/*          -o /tmp/tstcps_host test/host/tstcps_host.c \             */
/*          'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \     */
/*          'src/irx#anch.c' 'src/irx#env.c'  'src/irx#uid.c'  \     */
/*          'src/irx#msid.c' 'src/irx#cond.c' 'src/irx#bif.c'  \     */
/*          'src/irx#bifs.c' 'src/irx#io.c'   'src/irx#lstr.c' \     */
/*          'src/irx#tokn.c' 'src/irx#vpol.c' 'src/irx#pars.c' \     */
/*          'src/irx#ctrl.c' 'src/irx#exec.c' 'src/irx#arith.c' \    */
/*          '../lstring370/src/lstr#cor.c'  \                         */
/*          '../lstring370/src/lstr#cvt.c'  \                         */
/*          '../lstring370/src/lstr#fmt.c'  \                         */
/*          '../lstring370/src/lstr#srch.c' \                         */
/*          '../lstring370/src/lstr#sub.c'  \                         */
/*          '../lstring370/src/lstr#wrd.c'  \                         */
/*          '../lstring370/src/lstr#xlt.c'                            */
/*                                                                    */
/*  Usage:                                                             */
/*    ./tstcps_host               — run embedded microbench            */
/*    ./tstcps_host --source=PATH — run REXX source file at PATH       */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

#include "irx.h"
#include "irxexec.h"
#include "irxfunc.h"

#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

/* ------------------------------------------------------------------ */
/*  Embedded microbench — mirrors REXXCPS hot paths.                  */
/*  Tune outer*inner so the total run time hits ~10-20 s on a         */
/*  modern Linux host. 1000*500 = 500 000 iterations is the           */
/*  default; reduce outer if runtime exceeds 30 s, increase if        */
/*  under 10 s. On macOS ARM this ran ~6 s; Linux x86_64 typically   */
/*  runs 1.5-2x slower for this interpreter-heavy workload.           */
/* ------------------------------------------------------------------ */
static const char MICROBENCH[] =
    "/* WP-PERF-01 embedded microbench */\n"
    "outer = 1000\n"
    "inner = 500\n"
    "acc = 0\n"
    "str = 'the quick brown fox jumps over the lazy dog'\n"
    "stem. = 0\n"
    "do o = 1 to outer\n"
    "   do i = 1 to inner\n"
    "      acc = acc + i\n"
    "      stem.o.i = acc\n"
    "      len = length(str)\n"
    "      pos1 = pos('brown', str)\n"
    "      sub = substr(str, pos1, 5)\n"
    "      wc = words(str)\n"
    "      x = (i * 3 + 7) / 2\n"
    "      y = x ** 2\n"
    "      call helper i\n"
    "   end\n"
    "end\n"
    "say 'acc =' acc\n"
    "say 'last sub =' sub\n"
    "say 'last x =' x\n"
    "say 'last y =' y\n"
    "say 'wc =' wc\n"
    "exit 0\n"
    "\n"
    "helper:\n"
    "   parse arg n\n"
    "   return n + 1\n";

/* ------------------------------------------------------------------ */
/*  Read a file into a malloc'd buffer. Returns NULL on error.        */
/* ------------------------------------------------------------------ */
static char *read_file(const char *path, int *len_out)
{
    FILE *f;
    long size;
    char *buf;

    f = fopen(path, "r");
    if (f == NULL)
    {
        fprintf(stderr, "tstcps_host: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);

    buf = malloc((size_t)(size + 1));
    if (buf == NULL)
    {
        fprintf(stderr, "tstcps_host: out of memory\n");
        fclose(f);
        return NULL;
    }
    *len_out = (int)fread(buf, 1, (size_t)size, f);
    buf[*len_out] = '\0';
    fclose(f);
    return buf;
}

/* ------------------------------------------------------------------ */
/*  Elapsed wall-clock in seconds (CLOCK_MONOTONIC).                  */
/* ------------------------------------------------------------------ */
static double wall_seconds(struct timespec *start, struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1.0e9;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    const char *source_path = NULL;
    const char *src;
    char *file_buf = NULL;
    int src_len;
    int exit_rc = -1;
    int rc;
    struct timespec t0, t1;
    struct rusage ru0, ru1;
    double wall;
    double cpu_user, cpu_sys;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (strncmp(argv[i], "--source=", 9) == 0)
        {
            source_path = argv[i] + 9;
        }
        else
        {
            fprintf(stderr, "tstcps_host: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "usage: tstcps_host [--source=PATH]\n");
            return 1;
        }
    }

    if (source_path != NULL)
    {
        file_buf = read_file(source_path, &src_len);
        if (file_buf == NULL)
        {
            return 1;
        }
        src = file_buf;
    }
    else
    {
        src = MICROBENCH;
        src_len = (int)strlen(MICROBENCH);
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    getrusage(RUSAGE_SELF, &ru0);

    rc = irx_exec_run(src, src_len, NULL, 0, &exit_rc, NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    getrusage(RUSAGE_SELF, &ru1);

    wall = wall_seconds(&t0, &t1);

    cpu_user = (double)(ru1.ru_utime.tv_sec - ru0.ru_utime.tv_sec) +
               (double)(ru1.ru_utime.tv_usec - ru0.ru_utime.tv_usec) / 1.0e6;
    cpu_sys = (double)(ru1.ru_stime.tv_sec - ru0.ru_stime.tv_sec) +
              (double)(ru1.ru_stime.tv_usec - ru0.ru_stime.tv_usec) / 1.0e6;

    fprintf(stderr, "wall=%.3fs  cpu_user=%.3fs  cpu_sys=%.3fs  exit_rc=%d\n",
            wall, cpu_user, cpu_sys, exit_rc);

    if (file_buf != NULL)
    {
        free(file_buf);
    }

    return (rc == 0) ? 0 : 1;
}
