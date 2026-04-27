/* REXX */
/*--------------------------------------------------------------------*/
/* PROBEA2 - Case A2: IRXINIT with caller-prev = TMP-env              */
/*                                                                    */
/*   Setup    fresh LOGON; DUMP captures the TMP-env address          */
/*   Action   IRXINIT INITENVB, R0 = TMP-env addr, TSOFL=1            */
/*   Observe  whether ECTENVBK is overwritten                         */
/*   Clarifies Q-INIT-2                                               */
/*                                                                    */
/* The TMP-env address must be read off the terminal log from the    */
/* "ECTENVBK" line of the PRE-DUMP and pasted as the second token    */
/* of the INITP call below.  Edit this exec before running, or use   */
/* the manual two-step variant in test/zos/README.md.                */
/*--------------------------------------------------------------------*/
PARSE ARG prev_env .
IF prev_env = "" THEN DO
  SAY "Usage:  EX 'hlq.REXX(PROBEA2)' '''xxxxxxxx'''"
  SAY "where xxxxxxxx is the hex TMP-env address from a prior PROBED."
  EXIT 4
END
SAY "#CASE=A2 DESC=""IRXINIT with caller-prev = TMP-env"""
SAY "#PREV=" prev_env
SAY "===A2-PRE==="
"FREE FILE(SYSPRINT)"
"ALLOC FILE(SYSPRINT) DA(*) LRECL(121) RECFM(F B A) REUSE"
ADDRESS LINKMVS "IRXPROBE MARK A2-PRE-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
SAY "===A2-ACTION==="
ADDRESS LINKMVS "IRXPROBE MARK A2-INITP"
ADDRESS LINKMVS "IRXPROBE INITP" prev_env
SAY "===A2-POST==="
ADDRESS LINKMVS "IRXPROBE MARK A2-POST-DUMP"
ADDRESS LINKMVS "IRXPROBE DUMP"
ADDRESS LINKMVS "IRXPROBE MARK A2-DONE"
SAY "===A2-DONE==="
EXIT 0
