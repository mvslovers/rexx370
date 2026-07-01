//TEXECVL  JOB (A),'IRXEXEC TEST',REGION=8M,
//           CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//           NOTIFY=IBMUSER
//*---------------------------------------------------------------
//* TEXECVL - Live MVS test of IRXINIT INITENVB, IRXEXEC of an
//*           in-storage REXX exec, then IRXTERM, through the
//*           production HLASM wrappers in
//*           IBMUSER.REXX370.V0R1M0D.LOAD
//*
//* Documents the WP-VLIST-WPOOL (TSK-279) reproducer for manual
//* submission; the mbt v2 runner (make test-mvs) generates its own
//* batch + TSO steps and does not consume this file.
//*
//* See test/asm/texecvl.asm for the VLIST/INSTBLK contracts and the
//* WTO format. Expected: STEP exits with RC=0; JESLOG shows
//*   'TEXECVL OK   ENV=xxxxxxxx RC=0000002A TRC=xxxxxxxx'
//* (RC=0000002A is the REXX 'exit 42'). Against the unfixed 8 KB
//* WPOOL the step ABENDs S0C4 instead.
//*---------------------------------------------------------------
//STEP1   EXEC PGM=TEXECVL,REGION=8M
//STEPLIB  DD DSN=IBMUSER.REXX370.V0R1M0D.LOAD,DISP=SHR
//SYSPRINT DD SYSOUT=*
//SYSTSPRT DD SYSOUT=*
//
