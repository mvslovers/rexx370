/* REXX */
/*--------------------------------------------------------------------*/
/* PROBEA3 - Case A3: Non-TSO env in TSO session                      */
/*                                                                    */
/*   Setup    fresh LOGON; DUMP shows TMP-default env (TSOFL=1)       */
/*   Action   IRXINIT INITENVB, TSOFL=0                               */
/*   Observe  whether ECTENVBK is overwritten by a non-TSO env        */
/*   Clarifies Q-INIT-3                                               */
/*--------------------------------------------------------------------*/
SAY "#CASE=A3 DESC=""IRXINIT TSOFL=0 in TSO session"""
SAY "===A3-PRE==="
"FREE FILE(SYSPRINT)"
"ALLOC FILE(SYSPRINT) DA(*) LRECL(121) RECFM(F B A) REUSE"
ADDRESS LINKMVS "IRXPROBE MARK A3-PRE-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
SAY "===A3-ACTION==="
ADDRESS LINKMVS "IRXPROBE MARK A3-INITNT"
ADDRESS LINKMVS "IRXPROBE INITNT"
SAY "===A3-POST==="
ADDRESS LINKMVS "IRXPROBE MARK A3-POST-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
ADDRESS LINKMVS "IRXPROBE MARK A3-DONE"
SAY "===A3-DONE==="
EXIT 0
