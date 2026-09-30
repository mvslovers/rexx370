/* ------------------------------------------------------------------ */
/*  irxemsg.h - REXX error message and +++ traceback (#281)           */
/*                                                                    */
/*  An error that is not trapped by SIGNAL ON SYNTAX ends the exec    */
/*  with a traceback and a message (SC28-1883-0 p.67-68, Appendix A): */
/*                                                                    */
/*       18 +++   y = 1/0                                             */
/*        6 +++  call sub1                                            */
/*    IRX0042I Error running ERRTEST, line 18: Arithmetic ...          */
/*                                                                    */
/*  The traceback lists the clause in error first, then every active  */
/*  CALL / function / INTERPRET clause, innermost first.  Both go      */
/*  through the environment's I/O routine; outside TSO/E the message  */
/*  line also goes by WTO unless PARMBLOCK NOMSGWTO is set (p.285).   */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#ifndef IRXEMSG_H
#define IRXEMSG_H

struct envblock;

/* One +++ line of the traceback. */
struct irx_emsg_clause
{
    int line;         /* source line number                        */
    int level;        /* nesting level: blanks after "+++" (>= 1)  */
    const char *text; /* clause text in the source; NULL = none    */
    int text_len;
};

/* Write the traceback (tb[0] = clause in error, n_tb entries) and the
 * IRX00nnI message for error number errnum (SYNTAX_MIN..SYNTAX_MAX):
 *   line > 0:  IRX00nnI Error running <exec>, line <line>: <text>
 *   line == 0: IRX00nnI <text>   -- the form Appendix A gives for an
 *              error outside any clause, e.g. error 5 when the initial
 *              storage cannot be obtained.
 * Records errnum and line in the work block.  Allocates nothing, so it
 * can report error 5 (storage exhausted) too. */
void irx_emsg_syntax(struct envblock *env, int errnum, int line,
                     const struct irx_emsg_clause *tb, int n_tb) asm("IRXEMSG");

/* Write a message that is not about a clause -- IRXJCL's load failure
 * (IRX0406E, IRX0110I, IRX0112I).  text is the whole line after the
 * "IRX" prefix, e.g. "0110I The REXX exec cannot be interpreted.".
 * Channel as measured on z/OS (#258): outside TSO/E by WTO only,
 * nothing in SYSTSPRT -- unless NOMSGWTO routes it to the I/O routine;
 * under TSO/E through the I/O routine.  On the host, where there is
 * no WTO, the I/O routine stands in for it. */
void irx_emsg_system(struct envblock *env, const char *text) asm("IRXEMSGS");

#endif /* IRXEMSG_H */
