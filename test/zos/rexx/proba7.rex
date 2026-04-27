/* REXX */
/*--------------------------------------------------------------------*/
/* PROBEA7 - Case A7: IRXEXEC with explicit env parameter             */
/*                                                                    */
/*   Setup    fresh LOGON; INIT to create Env_A; capture address      */
/*   Action   IRXEXEC with explicit Env_A while ECTENVBK still        */
/*            points at the TMP-default env                           */
/*   Observe  which env IRXEXEC actually uses                         */
/*   Clarifies Q-EXEC-1                                               */
/*                                                                    */
/* The HLASM IRXPROBE EXEC subcommand is a stub in this revision     */
/* (returns NOT_IMPLEMENTED).  This driver is included to anchor      */
/* the case definition; full IRXEXEC linkage is a follow-up to        */
/* issue #74 once Phase alpha for cases D + A1..A6 has run.          */
/*--------------------------------------------------------------------*/
PARSE ARG envaddr .
"FREE FILE(SYSPRINT)"
"ALLOC FILE(SYSPRINT) DA(*) LRECL(121) RECFM(F B A) REUSE"
IF envaddr = "" THEN DO
  SAY "#CASE=A7 STEP=1 DESC=""Create Env_A; record its address"""
  SAY "===A7-STEP1==="
  ADDRESS LINKMVS "IRXPROBE MARK A7-PRE-DUMP"
  ADDRESS LINKMVS "IRXPROBE DUMP"
  ADDRESS LINKMVS "IRXPROBE MARK A7-INIT-A"
  ADDRESS LINKMVS "IRXPROBE INIT"
  SAY "Now capture the 'new envblock' value above and re-run:"
  SAY "  EX 'hlq.REXX(PROBEA7)' 'xxxxxxxx'"
  EXIT 0
END
SAY "#CASE=A7 STEP=2 DESC=""IRXEXEC with Env_A=" envaddr """"
SAY "===A7-STEP2==="
ADDRESS LINKMVS "IRXPROBE MARK A7-EXEC"
ADDRESS LINKMVS "IRXPROBE EXEC" envaddr
ADDRESS LINKMVS "IRXPROBE MARK A7-POST-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
ADDRESS LINKMVS "IRXPROBE MARK A7-DONE"
SAY "===A7-DONE==="
EXIT 0
