/* REXX */
/*--------------------------------------------------------------------*/
/* PROBEA1 - Case A1: First IRXINIT after logon                       */
/*                                                                    */
/*   Setup    fresh LOGON; DUMP shows the TMP-default env             */
/*   Action   IRXINIT INITENVB, TSOFL=1, no caller-prev               */
/*   Observe  ECTENVBK pre/post, new env address                      */
/*   Clarifies Q-INIT-1                                               */
/*--------------------------------------------------------------------*/
SAY "#CASE=A1 DESC=""First IRXINIT after logon"""
SAY "===A1-PRE==="
"FREE FILE(SYSPRINT)"
"ALLOC FILE(SYSPRINT) DA(*) LRECL(121) RECFM(F B A) REUSE"
ADDRESS LINKMVS "IRXPROBE MARK A1-PRE-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
SAY "===A1-ACTION==="
ADDRESS LINKMVS "IRXPROBE MARK A1-INIT"
ADDRESS LINKMVS "IRXPROBE INIT"
SAY "===A1-POST==="
ADDRESS LINKMVS "IRXPROBE MARK A1-POST-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
ADDRESS LINKMVS "IRXPROBE MARK A1-DONE"
SAY "===A1-DONE==="
EXIT 0
