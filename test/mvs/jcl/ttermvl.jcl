//TTERMVL  JOB (A),'IRXTERM TEST',REGION=4M,
//           CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//           NOTIFY=IBMUSER
//*---------------------------------------------------------------
//* TTERMVL - Live MVS test of IRXINIT INITENVB followed by IRXTERM
//*           through the production HLASM wrappers in
//*           IBMUSER.REXX370.V0R1M0D.LOAD
//*
//* See test/mvs/asm/ttermvl.asm for VLIST/R0 contracts and WTO format.
//* Expected: STEP exits with RC=0 (or RC=4 if IRXTERM warns); JESLOG
//*   'TTERMVL OK   ENVBLOCK=xxxxxxxx PRED=xxxxxxxx RC=xxxxxxxx'
//*---------------------------------------------------------------
//STEP1   EXEC PGM=TTERMVL,REGION=4M,COND=(8,LT)
//STEPLIB  DD DSN=IBMUSER.REXX370.V0R1M0D.LOAD,DISP=SHR
//SYSPRINT DD SYSOUT=*
//
