//TINITVL  JOB (A),'IRXINIT TEST',REGION=4M,
//           CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//           NOTIFY=IBMUSER
//*---------------------------------------------------------------
//* TINITVL - Live MVS test of IRXINIT INITENVB through the
//*           production HLASM wrapper in IBMUSER.REXX370.V0R1M0D.LOAD
//*
//* See test/mvs/asm/tinitvl.asm for VLIST shape and WTO format.
//* Expected: STEP exits with RC=0; JESLOG shows
//*   'TINITVL OK   ENVBLOCK=xxxxxxxx RC=00000000 REASON=00000000'
//*---------------------------------------------------------------
//STEP1   EXEC PGM=TINITVL,REGION=4M
//STEPLIB  DD DSN=IBMUSER.REXX370.V0R1M0D.LOAD,DISP=SHR
//SYSPRINT DD SYSOUT=*
//
