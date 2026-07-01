/* trexxvl.c - standalone reproducer for the httprexx IRXEXEC-via-LINK crash.
 *
 * Runs in-storage REXX execs through the IRXINIT / IRXEXEC VLIST (LINK)
 * interface -- exactly the path httprexx uses -- as a batch program with
 * HARDCODED EBCDIC sources. This removes httpd, UFS, and the file encoding
 * from the equation.
 *
 * Two cases, escalating:
 *   1. a single "say" line (baseline);
 *   2. the exact REXX that httprexx's transpiler produces for hello.rxp
 *      (multi-line: parse/if, say with ||, a do-loop over words()/word()).
 *
 *   - if case 2 abends in IRXEXEC/IRXBEXEC, the bug is that CONTENT on the
 *     rexx370 VLIST/in-storage path (VM/WPOOL/BIF), reproduced standalone;
 *   - if both print their SAY lines and exit clean, rexx370 is fine and the
 *     httprexx failure is upstream (the transpiler's EBCDIC output on MVS, or
 *     the file read) -- add debug output in httprexx to see it.
 *
 * Build: [[module]] TREXXVL (startup=crt1), deployed into the REXX370 LOAD lib.
 * Run:   test/jcl/trexxvl.jcl (STEPLIB = the REXX370 LOAD lib).
 */
#include <cliblink.h>
#include <clibwto.h>
#include <string.h>

#include "lstring.h"
#include "irx.h"
#include "irxwkblk.h"

int main(void);

/* WTO each SAY line so a successful run is visible in the joblog. */
static int repro_io(int function, PLstr data, struct envblock *env)
{
    (void)env;
    if (function == RXFWRITE && data != NULL && Lpstr(data) != NULL)
    {
        wtof("TREXXVL SAY: %.*s", (int)Llen(data), (char *)Lpstr(data));
    }
    return 0;
}

/* Run one exec (nlines source lines) through IRXINIT + IRXEXEC via LINK,
 * building a multi-entry INSTBLK exactly like httprexx. Returns 0 on success. */
static int run_lines(const char *label, const char *lines[], int nlines)
{
    struct instblk        ib;
    struct instblk_entry  ents[16];
    int                   i;

    static const char fcode[] = "INITENVB";
    void            *p_parmmod = NULL;
    void            *p_userfld = NULL;
    void            *p_wkblk   = NULL;
    void            *p_resv    = NULL;
    struct envblock *env       = NULL;
    int              reason    = 0;
    unsigned         vinit[7];

    void            *p_execblk = NULL;
    void            *p_argtab  = NULL;
    int              p_flags   = IRXEXEC_SUBROUTINE;
    void            *p_instblk;
    void            *p_resv5   = NULL;
    void            *p_evalblk = NULL;
    void            *p_wkarea  = NULL;
    void            *p_usrfld  = NULL;
    void            *p_envblk;
    int              rexxrc    = 0;
    unsigned         vexec[10];

    int prc = 0;
    int rc;

    if (nlines > 16)
    {
        return 99;
    }

    /* --- IRXINIT --- */
    vinit[0] = (unsigned)fcode;
    vinit[1] = (unsigned)&p_parmmod;
    vinit[2] = (unsigned)&p_userfld;
    vinit[3] = (unsigned)&p_wkblk;
    vinit[4] = (unsigned)&p_resv;
    vinit[5] = (unsigned)&env;
    vinit[6] = (unsigned)&reason | 0x80000000U;
    rc = __linkds("IRXINIT", NULL, vinit, &prc);
    wtof("TREXXVL[%s]: IRXINIT link=%d rc=%d env=%08X", label, rc, prc,
         (unsigned)env);
    if (rc != 0 || prc != 0 || env == NULL)
    {
        return 20;
    }

    /* --- override the I/O routine (SAY -> repro_io) --- */
    {
        struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
        if (exte != NULL)
        {
            exte->io_routine = (void *)repro_io;
            exte->irxinout   = (void *)repro_io;
        }
    }

    /* --- build the multi-entry INSTBLK (one entry per line) --- */
    memset(&ib, 0, sizeof(ib));
    memcpy(ib.instblk_acronym, INSTBLK_ID, 8);
    ib.instblk_hdrlen  = INSTBLK_HDRLEN;
    ib.instblk_address = ents;
    ib.instblk_usedlen = nlines * (int)sizeof(struct instblk_entry);
    memset(ib.instblk_member, ' ', 8);
    memset(ib.instblk_ddname, ' ', 8);
    memset(ib.instblk_subcom, ' ', 8);
    for (i = 0; i < nlines; i++)
    {
        ents[i].instblk_stmt_   = (void *)lines[i];
        ents[i].instblk_stmtlen = (int)strlen(lines[i]);
    }

    /* --- IRXEXEC --- */
    p_instblk = &ib;
    p_envblk  = env;
    vexec[0] = (unsigned)&p_execblk;
    vexec[1] = (unsigned)&p_argtab;
    vexec[2] = (unsigned)&p_flags;
    vexec[3] = (unsigned)&p_instblk;
    vexec[4] = (unsigned)&p_resv5;
    vexec[5] = (unsigned)&p_evalblk;
    vexec[6] = (unsigned)&p_wkarea;
    vexec[7] = (unsigned)&p_usrfld;
    vexec[8] = (unsigned)&p_envblk;
    vexec[9] = (unsigned)&rexxrc | 0x80000000U;

    wtof("TREXXVL[%s]: IRXEXEC %d line(s), instblk=%08X", label, nlines,
         (unsigned)&ib);
    rc = __linkds("IRXEXEC", NULL, vexec, &prc);
    wtof("TREXXVL[%s]: IRXEXEC link=%d rc=%d rexxrc=%d", label, rc, prc, rexxrc);

    return (rc == 0 && prc == 0) ? 0 : 8;
}

int main(void)
{
    /* Escalating cases, simplest first. The FIRST one that abends pinpoints
     * the construct the rexx370 bytecode VM (IRXBEXEC) mishandles on the
     * IRXEXEC/in-storage path. Each runs under its own fresh LPE. */
    static const char *c_say[]    = { "say 'plain say'" };
    static const char *c_cat[]    = { "say 'a' || 'b'" };
    static const char *c_catex[]  = { "say 'x=' || (1 + 1) || '.'" };
    static const char *c_parse[]  = { "parse arg name",
                                      "say 'name=[' || name || ']'" };
    static const char *c_words[]  = { "x = 'a b c'",
                                      "say words(x)" };
    static const char *c_word[]   = { "x = 'a b c'",
                                      "say word(x, 2)" };
    static const char *c_do[]     = { "do i = 1 to 3",
                                      "say 'i=' || i",
                                      "end" };
    static const char *c_hello[]  = {
        "parse arg name; if name = '' then name = 'World'",
        "say '<h1>Hello, ' || (name) || '!</h1>'",
        "items = 'Hercules MVS REXX'",
        "do i = 1 to words(items)",
        "say '  <li>' || (word(items, i)) || '</li>'",
        "end"
    };

    int rc = 0;

    wtof("TREXXVL: === 1. plain say ===");
    rc |= run_lines("say",   c_say,   1);
    wtof("TREXXVL: === 2. literal || literal ===");
    rc |= run_lines("cat",   c_cat,   1);
    wtof("TREXXVL: === 3. literal || (expr) ===");
    rc |= run_lines("catex", c_catex, 1);
    wtof("TREXXVL: === 4. parse arg + || ===");
    rc |= run_lines("parse", c_parse, 2);
    wtof("TREXXVL: === 5. words() BIF ===");
    rc |= run_lines("words", c_words, 2);
    wtof("TREXXVL: === 6. word() BIF ===");
    rc |= run_lines("word",  c_word,  2);
    wtof("TREXXVL: === 7. do-loop + || ===");
    rc |= run_lines("do",    c_do,    3);
    wtof("TREXXVL: === 8. full transpiled hello.rxp ===");
    rc |= run_lines("hello", c_hello, 6);

    wtof("TREXXVL: all cases done rc=%d", rc);
    return rc ? 8 : 0;
}
