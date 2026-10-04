/* ------------------------------------------------------------------ */
/*  tstspec.c - SC28-1883-0 conformance suite driver (#261)           */
/*                                                                    */
/*  Runs every exec under test/spec/ (conformance) and test/ext/      */
/*  (extensions, docs/extensions.md) through the IRXJCL core          */
/*  (irx_jcl_dispatch_main: IRXLOAD from SYSEXEC + IRXEXEC) and       */
/*  gates on its return code: each exec exits with the number of      */
/*  failed cases, 0 = all passed.                                     */
/*                                                                    */
/*  Execs that fail because of a known, filed defect carry the issue  */
/*  number in spec_members[].  They are reported as XFAIL and do not  */
/*  fail the run.  One that starts to pass is reported as XPASS and   */
/*  DOES fail the run, so the entry gets removed with the fix.        */
/*                                                                    */
/*  MVS: the execs are pre-loaded into the SYSEXEC fixture PDS        */
/*       (project.toml [[test.fixture]]).                             */
/*  Host: they are copied from test/spec/ and test/ext/ (relative to  */
/*       the repo root, where make test-host runs) into a temporary   */
/*       directory named by $SYSEXEC as <MEMBER>.rex, the host        */
/*       IRXLOAD convention.  The host run also checks that every     */
/*       file in both directories is listed here, so a new exec       */
/*       cannot be silently left out.                                 */
/*                                                                    */
/*  Expected values and their spec references: internals/spec-tests/. */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irxjcl.h"
#include "mbtcheck.h"

#ifndef __MVS__
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* Simulates the MVS ECT ECTENVBK slot on the host (test-only global). */
void *_simulated_ectenvbk = NULL;
#endif

/* One exec of the suite.  known_issue: 0 = must pass; otherwise the
 * rexx370 issue whose defect makes this exec fail today.  norun: the
 * defect takes the whole address space down (not just the exec), so the
 * exec is listed but not run until the issue is fixed. */
struct spec_member
{
    const char *name;
    int known_issue;
    int norun;
};

static const struct spec_member spec_members[] = {
    {"ABBREV", 0, 0},
    {"ABS", 267, 0},
    {"ADDRESS", 278, 0},
    {"ARG", 274, 0},
    {"ARGOPT", 280, 0},
    {"ARITH", 267, 0},
    {"ARITHNEG", 268, 0},
    {"B2X", 0, 0},
    {"BINSTR", 264, 0},
    {"BITAND", 270, 0},
    {"BITOR", 270, 0},
    {"BITXOR", 270, 0},
    {"C2D", 264, 0},
    {"C2DODD", 264, 0},
    {"C2X", 264, 0},
    {"C2XODD", 264, 0},
    {"CENTER", 0, 0},
    {"COMPARE", 0, 0},
    {"COMPCHN", 266, 0},
    {"COMPNOT", 266, 0},
    {"COMPOPS", 266, 0},
    {"COMPSLSH", 266, 0},
    {"CONCAT", 0, 0},
    {"CONCATX", 264, 0},
    {"COPIES", 0, 0},
    {"D2C", 283, 0},
    {"D2X", 0, 0},
    {"DATATYPE", 272, 0},
    {"DATE", 276, 0},
    {"DATEC", 276, 0},
    {"DATEO", 276, 0},
    {"DELSTR", 0, 0},
    {"DELWORD", 271, 0},
    {"DIGITS", 265, 0},
    {"ERRORTXT", 280, 0},
    {"EVALORD", 0, 0},
    {"FIND", 0, 0},
    {"FORM", 265, 0},
    {"FORMAT", 269, 0},
    {"FORMATBL", 267, 0},
    {"FUZZ", 265, 0},
    {"INDEX", 0, 0},
    {"INSERT", 0, 0},
    {"JUSTIFY", 271, 0},
    {"LASTPOS", 0, 0},
    {"LEFT", 0, 0},
    {"LENGTH", 0, 0},
    {"MAX", 0, 0},
    {"MIN", 0, 0},
    {"NUMFMT", 267, 0},
    {"OVERLAY", 0, 0},
    {"PARSE1", 273, 0},
    {"PARSE2", 273, 0},
    {"PARSE3", 273, 0},
    {"PARSE4", 273, 0},
    {"POS", 0, 0},
    {"PREFIX", 267, 0},
    {"RANDOM", 279, 0},
    {"REVERSE", 0, 0},
    {"RIGHT", 0, 0},
    {"SIGN", 0, 0},
    {"SOURCELN", 280, 0},
    {"SPACE", 0, 0},
    {"STRIP", 0, 0},
    {"SUBSTR", 0, 0},
    {"SUBWORD", 0, 0},
    {"SYMBOL", 275, 0},
    {"TIME", 276, 0},
    {"TRACE", 277, 0},
    {"TRANSLAT", 271, 0},
    {"TRANSLHX", 264, 0},
    {"TRUNC", 267, 0},
    {"VALUE", 275, 0},
    {"VERIFY", 0, 0},
    {"WORD", 0, 0},
    {"WORDINDX", 0, 0},
    {"WORDLEN", 0, 0},
    {"WORDPOS", 0, 0},
    {"WORDS", 0, 0},
    {"X2B", 0, 0},
    {"X2C", 0, 0},
    {"X2D", 267, 0},
    {"XRANGE", 0, 0},
    {"XRANGEHX", 264, 0},
};

enum
{
    SPEC_COUNT = (int)(sizeof(spec_members) / sizeof(spec_members[0])),
    MSG_LEN = 80
};

#ifndef __MVS__

/* Conformance execs, and the execs for rexx370's extensions beyond
 * SC28-1883-0 (docs/extensions.md). Member names are unique across both. */
static const char *const spec_src_dirs[] = {"test/spec", "test/ext"};

enum
{
    SRC_DIR_COUNT = (int)(sizeof(spec_src_dirs) / sizeof(spec_src_dirs[0]))
};

static char sysexec_dir[] = "/tmp/tstspecXXXXXX";

static int is_listed(const char *name)
{
    for (int i = 0; i < SPEC_COUNT; i++)
    {
        if (strcmp(spec_members[i].name, name) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int copy_member(const char *name)
{
    char src[256];
    char dst[256];
    FILE *in = NULL;
    for (int d = 0; d < SRC_DIR_COUNT && in == NULL; d++)
    {
        snprintf(src, sizeof(src), "%s/%s", spec_src_dirs[d], name);
        in = fopen(src, "r");
    }
    if (in == NULL)
    {
        return -1;
    }
    snprintf(dst, sizeof(dst), "%s/%s.rex", sysexec_dir, name);
    FILE *out = fopen(dst, "w");
    if (out == NULL)
    {
        fclose(in);
        return -1;
    }
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return 0;
}

/* Copy every listed exec into the temporary SYSEXEC directory and
 * check test/spec/ for execs missing from spec_members[]. */
static int host_setup(void)
{
    if (mkdtemp(sysexec_dir) == NULL)
    {
        printf("FATAL: cannot create %s\n", sysexec_dir);
        return -1;
    }
    for (int i = 0; i < SPEC_COUNT; i++)
    {
        char msg[MSG_LEN];
        snprintf(msg, sizeof(msg), "%s present in test/spec or test/ext",
                 spec_members[i].name);
        CHECK(copy_member(spec_members[i].name) == 0, msg);
    }

    for (int di = 0; di < SRC_DIR_COUNT; di++)
    {
        DIR *d = opendir(spec_src_dirs[di]);
        if (d == NULL)
        {
            printf("FATAL: cannot open %s (run from the repo root)\n",
                   spec_src_dirs[di]);
            return -1;
        }
        struct dirent *e;
        while ((e = readdir(d)) != NULL)
        {
            if (e->d_name[0] == '.')
            {
                continue;
            }
            char msg[MSG_LEN];
            snprintf(msg, sizeof(msg), "%s listed in spec_members[]",
                     e->d_name);
            CHECK(is_listed(e->d_name), msg);
        }
        closedir(d);
    }

    setenv("SYSEXEC", sysexec_dir, 1);
    unsetenv("SYSPROC");
    return 0;
}

static void host_cleanup(void)
{
    for (int i = 0; i < SPEC_COUNT; i++)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.rex", sysexec_dir,
                 spec_members[i].name);
        unlink(path);
    }
    rmdir(sysexec_dir);
}

#endif /* !__MVS__ */

int main(void)
{
#ifndef __MVS__
    if (host_setup() != 0)
    {
        return 1;
    }
#endif

    int xfail = 0;
    for (int i = 0; i < SPEC_COUNT; i++)
    {
        const struct spec_member *m = &spec_members[i];
        if (m->norun)
        {
            printf("  SKIP: %-8s not run, crashes (#%d)\n", m->name,
                   m->known_issue);
            xfail++;
            continue;
        }
        printf("--- %s\n", m->name);
        fflush(stdout);
        int rc = irx_jcl_dispatch_main(m->name, NULL, 0);
        fflush(stdout);

        char msg[MSG_LEN];
        if (m->known_issue == 0)
        {
            snprintf(msg, sizeof(msg), "%-8s rc=%d", m->name, rc);
            CHECK(rc == 0, msg);
        }
        else if (rc != 0)
        {
            printf("  XFAIL: %-8s rc=%d (known, #%d)\n", m->name, rc,
                   m->known_issue);
            xfail++;
        }
        else
        {
            snprintf(msg, sizeof(msg),
                     "%-8s passes now: XPASS, drop #%d from the list",
                     m->name, m->known_issue);
            CHECK(0, msg);
        }
    }
    printf("TSTSPEC: %d execs, %d known failures\n", SPEC_COUNT, xfail);

#ifndef __MVS__
    host_cleanup();
#endif
    return mbt_test_summary("TSTSPEC");
}
