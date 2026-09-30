/* ------------------------------------------------------------------ */
/*  irxjcl.h — IRXJCL Batch Entry Point interface                     */
/*                                                                    */
/*  irx_jcl_dispatch_main() is the C core of IRXJCL.  Entry is       */
/*  main(argc, argv) in src/irx#jclm.c, called by @@CRT0, which      */
/*  initialises the crent370 CRT/PPA before main() runs.  main()     */
/*  reconstructs the single-arg-string from @@START-split argv tokens */
/*  and delegates to irx_jcl_dispatch_main().                         */
/*                                                                    */
/*  Invocation (JCL batch):                                           */
/*    EXEC PGM=IRXJCL,PARM='MYMEMBER arg1 arg2...'                    */
/*                                                                    */
/*  Programmatic CALL IRXJCL,... support is deferred to WP-CPS-08c.  */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXJCL_H
#define IRXJCL_H

/*
 * irx_jcl_dispatch_main — core IRXJCL lifecycle.
 *
 *   member      NUL-terminated member name (1-8 chars; will be uppercased)
 *   arg_string  argument string passed to IRXEXEC (may be NULL)
 *   arg_len     byte length of arg_string (ignored when arg_string is NULL)
 *
 * Returns one of the irxjcl_rc values below.
 */
int irx_jcl_dispatch_main(const char *member,
                           const char *arg_string,
                           int         arg_len);

/* Return codes (process exit code / R15), SC28-1883-0 Figure 11. */
enum irxjcl_rc
{
    IRXJCL_OK = 0,              /* exec ran; check exit RC via EVALBLOCK */
    IRXJCL_BADPARM = 20021,     /* member NULL/empty/sequential/too long; */
                                /* the step CC shows it mod 4096         */
    IRXJCL_NOENV = 28,          /* cannot locate/create a Language Env   */
    IRXJCL_ERROR = 20,          /* the exec was not executed (load)      */
    IRXJCL_SYNTAX_BASE = 20000, /* + REXX error number when the exec     */
                                /* ended in an error (z/OS: 42 -> CC     */
                                /* 3658 = 20042 mod 4096, #281)          */
};

#endif /* IRXJCL_H */
