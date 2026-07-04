/* trexxvl.c - httprexx-shaped end-to-end test: IRXINIT via LINK, IRXEXEC
 * via LINK, IRXTERM via __load + BALR (R0 = ENVBLOCK).
 *
 * Runs in-storage REXX execs through the IRXINIT / IRXEXEC VLIST (LINK)
 * interface -- exactly the path httprexx uses -- as a batch program with
 * HARDCODED EBCDIC sources. This removes httpd, UFS, and the file encoding
 * from the equation.
 *
 * Case 0c additionally covers the bare IRXINIT -> IRXTERM round trip with
 * no exec in between; cases 1-8 escalate from a single SAY to the full
 * transpiled hello.rxp.  Each case runs under its own fresh LPE and tears
 * it down via IRXTERM -- the exact invocation shape that used to crash
 * against a stale LINKLIB build (see docs/irxterm-c-host-crash.md).
 *
 * Build: [[test]] TREXXVL (startup=crt1), deployed into the TESTLIB;
 * STEPLIB = TESTLIB + LINKLIB so the production IRX* modules resolve.
 */
#include <cliblink.h>
#include <clibos.h>
#include <clibwto.h>
#include <string.h>

#include "irx.h"
#include "irxwkblk.h"
#include "lstring.h"

/* Call IRXTERM (entry from __load) with R0 = env; see test/trxcall.asm.
 * Mirror of httprexx HRXCALL. */
extern int trx_call(void *ep, void *env) asm("TRXCALL");

/* Alternate IRXINIT invocation for the (currently unused) mode-3 leg:
 * __load ep + BALR with R1=vlist, R0=0 -- see test/trxldc.asm.  Kept for
 * the #204 (foreign-PPA getenv) follow-up work. */
extern int trx_callv(void *ep, void *vlist) asm("TRXCALLV");

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

/* Tear down one env via IRXTERM: __load ep + trx_call (R0 = env).
 * Returns IRXTERM's RC, or -1 when the module cannot be loaded. */
static int term_env(const char *label, struct envblock *env)
{
    unsigned int lsize = 0;
    char lac = 0;
    void *ep = __load(NULL, "IRXTERM", &lsize, &lac);
    int trc = -1;

    if (ep)
    {
        trc = trx_call(ep, env);
        wtof("TREXXVL[%s]: IRXTERM rc=%d", label, trc);
        __delete("IRXTERM");
    }
    else
    {
        wtof("TREXXVL[%s]: IRXTERM __load FAILED", label);
    }
    return trc;
}

/* Run one exec (nlines source lines) through IRXINIT + IRXEXEC via LINK,
 * building a multi-entry INSTBLK exactly like httprexx, then tear the
 * env down via IRXTERM.  Returns 0 on success. */
static int run_lines(const char *label, const char *lines[], int nlines)
{
    struct instblk ib;
    struct instblk_entry ents[16];
    int i;

    static const char fcode[] = "INITENVB";
    void *p_parmmod = NULL;
    void *p_userfld = NULL;
    void *p_wkblk = NULL;
    void *p_resv = NULL;
    struct envblock *env = NULL;
    int reason = 0;
    unsigned vinit[7];

    void *p_execblk = NULL;
    void *p_argtab = NULL;
    int p_flags = IRXEXEC_SUBROUTINE;
    void *p_instblk;
    void *p_resv5 = NULL;
    void *p_evalblk = NULL;
    void *p_wkarea = NULL;
    void *p_usrfld = NULL;
    void *p_envblk;
    int rexxrc = 0;
    unsigned vexec[10];

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
            exte->irxinout = (void *)repro_io;
        }
    }

    /* --- build the multi-entry INSTBLK (one entry per line) --- */
    memset(&ib, 0, sizeof(ib));
    memcpy(ib.instblk_acronym, INSTBLK_ID, 8);
    ib.instblk_hdrlen = INSTBLK_HDRLEN;
    ib.instblk_address = ents;
    ib.instblk_usedlen = nlines * (int)sizeof(struct instblk_entry);
    memset(ib.instblk_member, ' ', 8);
    memset(ib.instblk_ddname, ' ', 8);
    memset(ib.instblk_subcom, ' ', 8);
    for (i = 0; i < nlines; i++)
    {
        ents[i].instblk_stmt_ = (void *)lines[i];
        ents[i].instblk_stmtlen = (int)strlen(lines[i]);
    }

    /* --- IRXEXEC --- */
    p_instblk = &ib;
    p_envblk = env;
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

    rc = __linkds("IRXEXEC", NULL, vexec, &prc);
    wtof("TREXXVL[%s]: IRXEXEC link=%d rc=%d rexxrc=%d", label, rc, prc,
         rexxrc);

    /* --- IRXTERM: tear down the env AFTER the exec (httprexx path) --- */
    if (term_env(label, env) != 0)
    {
        return 12;
    }

    return (rc == 0 && prc == 0) ? 0 : 8;
}

/* Case 0c: IRXINIT via __linkds (LINK) -> IRXTERM via __load + BALR,
 * with NO IRXEXEC in between -- the bare httprexx round trip. */
static int run_noexec_term(const char *label)
{
    static const char fcode[] = "INITENVB";
    void *p_parmmod = NULL;
    void *p_userfld = NULL;
    void *p_wkblk = NULL;
    void *p_resv = NULL;
    struct envblock *env = NULL;
    int reason = 0;
    unsigned vinit[7];
    int prc = 0;
    int rc = 0;

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

    return (term_env(label, env) == 0) ? 0 : 12;
}

/* Case 0d (#204 repro): IRXINIT via __load + BALR (R1 = VLIST, R0 = 0),
 * then IRXTERM.  Unlike 0c (LINK -> fresh PRB -> PPA = NULL -> getenv
 * skipped), a BALR from this live crent370 host leaves IRXINIT running
 * against the *host's* (foreign) PPA.  env_get_safe() must recognise that
 * no CLIBCRT is registered for the current TCB and skip getenv() rather
 * than fault (S0C4).  MVS-only: there is no PPA/TCB on the host build. */
static int run_balr_init_term(const char *label)
{
    static const char fcode[] = "INITENVB";
    void *p_parmmod = NULL;
    void *p_userfld = NULL;
    void *p_wkblk = NULL;
    void *p_resv = NULL;
    struct envblock *env = NULL;
    int reason = 0;
    unsigned vinit[7];
    unsigned int lsize = 0;
    char lac = 0;
    void *ep;
    int rc;

    vinit[0] = (unsigned)fcode;
    vinit[1] = (unsigned)&p_parmmod;
    vinit[2] = (unsigned)&p_userfld;
    vinit[3] = (unsigned)&p_wkblk;
    vinit[4] = (unsigned)&p_resv;
    vinit[5] = (unsigned)&env;
    vinit[6] = (unsigned)&reason | 0x80000000U;

    ep = __load(NULL, "IRXINIT", &lsize, &lac);
    if (ep == NULL)
    {
        wtof("TREXXVL[%s]: IRXINIT __load FAILED", label);
        return 20;
    }

    rc = trx_callv(ep, vinit);
    wtof("TREXXVL[%s]: IRXINIT balr rc=%d reason=%d env=%08X", label, rc,
         reason, (unsigned)env);
    __delete("IRXINIT");

    if (rc != 0 || env == NULL)
    {
        return 20;
    }

    return (term_env(label, env) == 0) ? 0 : 12;
}

int main(void)
{
    /* Escalating cases, simplest first.  Each runs under its own fresh
     * LPE and ends with a real IRXTERM. */
    static const char *c_say[] = {"say 'plain say'"};
    static const char *c_cat[] = {"say 'a' || 'b'"};
    static const char *c_catex[] = {"say 'x=' || (1 + 1) || '.'"};
    static const char *c_parse[] = {"parse arg name",
                                    "say 'name=[' || name || ']'"};
    static const char *c_words[] = {"x = 'a b c'",
                                    "say words(x)"};
    static const char *c_word[] = {"x = 'a b c'",
                                   "say word(x, 2)"};
    static const char *c_do[] = {"do i = 1 to 3",
                                 "say 'i=' || i",
                                 "end"};
    /* #203: a trailing comma continues the clause onto the next
     * physical line; the || binds its right operand across the join. */
    static const char *c_cont[] = {"b = 'x' || ,", "'y'", "say b"};
    static const char *c_hello[] = {
        "parse arg name; if name = '' then name = 'World'",
        "say '<h1>Hello, ' || (name) || '!</h1>'",
        "items = 'Hercules MVS REXX'",
        "do i = 1 to words(items)",
        "say '  <li>' || (word(items, i)) || '</li>'",
        "end"};

    int rc = 0;

    wtof("TREXXVL: === 0c. IRXINIT via __linkds (LINK) -> IRXTERM ===");
    rc |= run_noexec_term("linkds");

    wtof("TREXXVL: === 0d. IRXINIT via __load+BALR (foreign PPA #204) ===");
    rc |= run_balr_init_term("balr");

    wtof("TREXXVL: === 1. plain say ===");
    rc |= run_lines("say", c_say, 1);
    wtof("TREXXVL: === 2. literal || literal ===");
    rc |= run_lines("cat", c_cat, 1);
    wtof("TREXXVL: === 3. literal || (expr) ===");
    rc |= run_lines("catex", c_catex, 1);
    wtof("TREXXVL: === 4. parse arg + || ===");
    rc |= run_lines("parse", c_parse, 2);
    wtof("TREXXVL: === 5. words() BIF ===");
    rc |= run_lines("words", c_words, 2);
    wtof("TREXXVL: === 6. word() BIF ===");
    rc |= run_lines("word", c_word, 2);
    wtof("TREXXVL: === 7. do-loop + || ===");
    rc |= run_lines("do", c_do, 3);
    wtof("TREXXVL: === 8. full transpiled hello.rxp ===");
    rc |= run_lines("hello", c_hello, 6);
    wtof("TREXXVL: === 9. line continuation (trailing comma) ===");
    rc |= run_lines("cont", c_cont, 3);

    wtof("TREXXVL: all cases done rc=%d", rc);
    return rc ? 8 : 0;
}
