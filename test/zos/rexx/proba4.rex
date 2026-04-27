/* REXX */
/*--------------------------------------------------------------------*/
/* PROBEA4 - Case A4: Multi-env stack on same TCB                     */
/*                                                                    */
/*   Setup    fresh LOGON; TMP-default env exists                     */
/*   Action   two IRXINIT calls in succession (Env_A then Env_B)      */
/*   Observe  ECTENVBK after each call; IRXANCHR state                */
/*   Clarifies Q-INIT-4                                               */
/*--------------------------------------------------------------------*/
SAY "#CASE=A4 DESC=""Two IRXINITs on same TCB"""
SAY "===A4-PRE==="
"FREE FILE(SYSPRINT)"
"ALLOC FILE(SYSPRINT) DA(*) LRECL(121) RECFM(F B A) REUSE"
ADDRESS LINKMVS "IRXPROBE MARK A4-PRE-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
SAY "===A4-ACTION-1==="
ADDRESS LINKMVS "IRXPROBE MARK A4-INIT-A"
ADDRESS LINKMVS "IRXPROBE INIT"
ADDRESS LINKMVS "IRXPROBE MARK A4-MID-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
SAY "===A4-ACTION-2==="
ADDRESS LINKMVS "IRXPROBE MARK A4-INIT-B"
ADDRESS LINKMVS "IRXPROBE INIT"
SAY "===A4-POST==="
ADDRESS LINKMVS "IRXPROBE MARK A4-POST-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
ADDRESS LINKMVS "IRXPROBE MARK A4-DONE"
SAY "===A4-DONE==="
EXIT 0
