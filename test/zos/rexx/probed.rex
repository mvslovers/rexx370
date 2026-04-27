/* REXX */
/*--------------------------------------------------------------------*/
/* PROBED - Discovery driver (Case D)                                 */
/*                                                                    */
/* Read-only baseline: walk ECTENVBK and IRXANCHR, dump ENVBLOCK and  */
/* PARMBLOCK headers as they exist immediately after LOGON.  Does     */
/* not modify any state.  Safe to re-run.                             */
/*                                                                    */
/* Run from a fresh TSO LOGON session, capture the terminal log.      */
/*--------------------------------------------------------------------*/
SAY "#CASE=D DESC=""Discovery: TSO baseline state at logon"""
SAY "===D-PRE==="
"FREE FILE(SYSPRINT)"
"ALLOC FILE(SYSPRINT) DA(*) LRECL(121) RECFM(F B A) REUSE"
ADDRESS LINKMVS "IRXPROBE MARK D-OBSERVE"
ADDRESS LINKMVS "IRXPROBE DUMP"
ADDRESS LINKMVS "IRXPROBE MARK D-DONE"
SAY "===D-DONE==="
EXIT 0
