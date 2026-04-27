/* REXX */
/*--------------------------------------------------------------------*/
/* PROBEA5 - Case A5: Term latest env                                 */
/*                                                                    */
/*   Setup    fresh LOGON; INIT to create Env_A; capture address      */
/*   Action   IRXTERM Env_A                                           */
/*   Observe  ECTENVBK pre/post; IRXTERM RC, RSN; IRXANCHR slot       */
/*   Clarifies Q-TERM-1                                               */
/*                                                                    */
/* Two-step usage:                                                    */
/*   Step 1: EX 'hlq.REXX(PROBEA5)'                                   */
/*           Read the new envblock address from the INIT output.      */
/*   Step 2: EX 'hlq.REXX(PROBEA5)' 'xxxxxxxx'                        */
/*           Pass the captured address; the exec issues TERM.         */
/*                                                                    */
/* Splitting the case in two REXX invocations keeps the env address  */
/* readable on the terminal between steps.  The case is still         */
/* executed within a single LOGON session.                            */
/*--------------------------------------------------------------------*/
PARSE ARG envaddr .
"FREE FILE(SYSPRINT)"
"ALLOC FILE(SYSPRINT) DA(*) LRECL(121) RECFM(F B A) REUSE"
IF envaddr = "" THEN DO
  SAY "#CASE=A5 STEP=1 DESC=""Create Env_A; record its address"""
  SAY "===A5-STEP1==="
  ADDRESS LINKMVS "IRXPROBE MARK A5-PRE-DUMP"
  ADDRESS LINKMVS "IRXPROBE DUMP"
  ADDRESS LINKMVS "IRXPROBE MARK A5-INIT-A"
  ADDRESS LINKMVS "IRXPROBE INIT"
  SAY "Now capture the 'new envblock' value above and re-run:"
  SAY "  EX 'hlq.REXX(PROBEA5)' 'xxxxxxxx'"
  EXIT 0
END
SAY "#CASE=A5 STEP=2 DESC=""IRXTERM Env_A=" envaddr """"
SAY "===A5-STEP2==="
ADDRESS LINKMVS "IRXPROBE MARK A5-TERM"
ADDRESS LINKMVS "IRXPROBE TERM" envaddr
ADDRESS LINKMVS "IRXPROBE MARK A5-POST-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
ADDRESS LINKMVS "IRXPROBE MARK A5-DONE"
SAY "===A5-DONE==="
EXIT 0
