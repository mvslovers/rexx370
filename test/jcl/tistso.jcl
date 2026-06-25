//TISTSO   JOB (A),'ISTSO TEST',REGION=4M,
//           CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//           NOTIFY=IBMUSER
//*---------------------------------------------------------------
//* TISTSO - Live MVS test of ISTSO EXTRACT-based TSO detection.
//*
//* Expected in batch: step RC=0, JESLOG shows
//*   'TISTSO: NOT TSO (batch)'
//*
//* From TSO: CALL 'IBMUSER.REXX370.V0R1M0D.LOAD(TISTSO)'
//*   Expected: RC=1, console shows 'TISTSO: IS TSO'
//*---------------------------------------------------------------
//STEP1   EXEC PGM=TISTSO,REGION=4M
//STEPLIB  DD DSN=IBMUSER.REXX370.V0R1M0D.LOAD,DISP=SHR
//SYSPRINT DD SYSOUT=*
//*
//STEP2   EXEC PGM=IKJEFT01
//SYSTSPRT DD SYSOUT=*
//SYSTSIN  DD *
  CALL 'IBMUSER.REXX370.V0R1M0D.LOAD(TISTSO)'
/*

