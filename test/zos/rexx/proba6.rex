/* REXX */
/*--------------------------------------------------------------------*/
/* PROBEA6 - Case A6: Try to TERM the TMP-default env                 */
/*                                                                    */
/*   Setup    fresh LOGON                                             */
/*   Action   IRXTERM on the TMP-default env                          */
/*   Observe  RC, RSN; whether IBM rejects the call; whether the      */
/*            session remains usable                                  */
/*                                                                    */
/* Risk: this case may destabilise the TSO session.  Run last in     */
/* its dedicated LOGON session.                                       */
/*                                                                    */
/* Two-step usage:                                                    */
/*   Step 1: EX 'hlq.REXX(PROBEA6)'                                   */
/*           Read the TMP-default ECTENVBK from the DUMP output.      */
/*   Step 2: EX 'hlq.REXX(PROBEA6)' 'xxxxxxxx'                        */
/*           Pass the captured address; the exec issues TERM.         */
/*--------------------------------------------------------------------*/
PARSE ARG envaddr .
"FREE FILE(SYSPRINT)"
"ALLOC FILE(SYSPRINT) DA(*) LRECL(121) RECFM(F B A) REUSE"
IF envaddr = "" THEN DO
  SAY "#CASE=A6 STEP=1 DESC=""Capture TMP-default ECTENVBK"""
  SAY "===A6-STEP1==="
  ADDRESS LINKMVS "IRXPROBE MARK A6-PRE-DUMP"
  ADDRESS LINKMVS "IRXPROBE DUMP"
  SAY "Now capture the ECTENVBK value above and re-run:"
  SAY "  EX 'hlq.REXX(PROBEA6)' 'xxxxxxxx'"
  EXIT 0
END
SAY "#CASE=A6 STEP=2 DESC=""IRXTERM TMP-default=" envaddr """"
SAY "===A6-STEP2==="
ADDRESS LINKMVS "IRXPROBE MARK A6-TERM-TMP"
ADDRESS LINKMVS "IRXPROBE TERM" envaddr
ADDRESS LINKMVS "IRXPROBE MARK A6-POST-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
ADDRESS LINKMVS "IRXPROBE MARK A6-DONE"
SAY "===A6-DONE==="
EXIT 0
