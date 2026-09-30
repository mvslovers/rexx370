/* ------------------------------------------------------------------ */
/*  irx#emsg.c - REXX error message and +++ traceback (#281)          */
/*                                                                    */
/*  Format measured on z/OS (ERRTEST, #281) against SC28-1883-0       */
/*  p.67-68 and Appendix A:                                           */
/*                                                                    */
/*    - a traceback line: the line number right-justified in 6        */
/*      columns (above 99999: '?' and the last 5 digits), a blank,    */
/*      "+++", then `level` blanks and the clause text;              */
/*    - the message: IRX00nnI Error running <exec>, line <nn>: <text> */
/*      with the Appendix A text, i.e. ERRORTEXT(nn).                 */
/*                                                                    */
/*  Channels (p.285): both go through the I/O routine -- SYSTSPRT     */
/*  in batch, PUTLINE under TSO/E.  In an environment not integrated  */
/*  into TSO/E the message line also goes by WTO unless NOMSGWTO is   */
/*  set, and NOMSGIO keeps it off the I/O routine.  IRXJCL on z/OS    */
/*  shows exactly that split: traceback + message in SYSTSPRT, the    */
/*  message alone in the job log.                                     */
/*                                                                    */
/*  No allocation: every line is built in a stack buffer, so error 5  */
/*  (storage exhausted) can be reported too.  A clause longer than    */
/*  the buffer is cut.                                                */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxcond.h"
#include "irxemsg.h"
#include "irxfunc.h"
#include "irxwkblk.h"
#include "lstring.h"

#ifdef __MVS__
#include <clibwto.h>
#endif

/* One output line: a clause is at most 500 characters (error 12), the
 * traceback prefix 10 plus the nesting blanks. */
#define EMSG_LINE_MAX 600

/* Width of the line number in a traceback line, and the largest
 * number that fits; above it the line shows as '?' + 5 digits. */
#define EMSG_LINENO_WIDTH 6
#define EMSG_LINENO_MAX   99999
#define EMSG_LINENO_WRAP  100000

/* Message prefix length ("IRX"). */
#define EMSG_PREFIX_LEN 3

typedef int (*emsg_io_fn)(int, PLstr, struct envblock *);

static void emsg_io(struct envblock *env, int function, char *buf, int len)
{
    struct irxexte *exte = (struct irxexte *)env->envblock_irxexte;
    if (exte == NULL || exte->io_routine == NULL)
    {
        return;
    }
    Lstr ls;
    ls.pstr = (unsigned char *)buf;
    ls.len = (size_t)len;
    ls.maxlen = (size_t)len;
    ls.type = LSTRING_TY;
    ((emsg_io_fn)exte->io_routine)(function, &ls, env);
}

/* Build one traceback line into buf; returns its length. */
static int emsg_tb_line(char *buf, const struct irx_emsg_clause *c)
{
    int n;
    if (c->line > EMSG_LINENO_MAX)
    {
        n = snprintf(buf, EMSG_LINE_MAX, "?%05d +++",
                     c->line % EMSG_LINENO_WRAP);
    }
    else
    {
        n = snprintf(buf, EMSG_LINE_MAX, "%*d +++", EMSG_LINENO_WIDTH,
                     c->line);
    }
    for (int i = 0; i < c->level && n < EMSG_LINE_MAX; i++)
    {
        buf[n++] = ' ';
    }
    for (int i = 0; c->text != NULL && i < c->text_len && n < EMSG_LINE_MAX;
         i++)
    {
        char ch = c->text[i];
        /* A continued clause spans source lines; z/OS shows the records
         * side by side, so a line end becomes a blank here. */
        buf[n++] = (ch == '\n' || ch == '\r') ? ' ' : ch;
    }
    return n;
}

void irx_emsg_syntax(struct envblock *env, int errnum, int line,
                     const struct irx_emsg_clause *tb, int n_tb)
{
    char buf[EMSG_LINE_MAX];

    if (env == NULL)
    {
        return;
    }
    struct irx_wkblk_int *wk =
        (struct irx_wkblk_int *)env->envblock_workblok_ext;
    if (wk != NULL)
    {
        wk->wkbi_error_number = errnum;
        wk->wkbi_error_line = line;
    }

    for (int i = 0; tb != NULL && i < n_tb; i++)
    {
        int n = emsg_tb_line(buf, &tb[i]);
        emsg_io(env, RXFTWRITE, buf, n);
    }

    char prefix[EMSG_PREFIX_LEN + 1] = "IRX";
    irxmsgid(0, prefix, env);
    prefix[EMSG_PREFIX_LEN] = '\0';

    const char *text = irx_cond_errortext(errnum);
    int n;
    if (line > 0)
    {
        const char *name =
            (WKBI_HAS(wk, wkbi_exec_name) && wk->wkbi_exec_name[0] != '\0')
                ? wk->wkbi_exec_name
                : "";
        n = snprintf(buf, EMSG_LINE_MAX, "%s%04dI Error running %s, line %d: %s",
                     prefix, errnum, name, line, text != NULL ? text : "");
    }
    else
    {
        n = snprintf(buf, EMSG_LINE_MAX, "%s%04dI %s", prefix, errnum,
                     text != NULL ? text : "");
    }
    if (n < 0)
    {
        return;
    }
    if (n >= EMSG_LINE_MAX)
    {
        n = EMSG_LINE_MAX - 1;
    }

    struct parmblock *pb = (struct parmblock *)env->envblock_parmblock;
    int tso = (pb != NULL && pb->tsofl);
    if (tso || pb == NULL || !pb->nomsgio)
    {
        emsg_io(env, RXFWRITERR, buf, n);
    }
#ifdef __MVS__
    if (!tso && (pb == NULL || !pb->nomsgwto))
    {
        buf[n] = '\0';
        wtof("%s", buf);
    }
#endif
}
