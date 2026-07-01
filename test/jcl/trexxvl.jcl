//TREXXVL  JOB (A),'IRXEXEC LINK TEST',REGION=4M,
//           CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//           NOTIFY=IBMUSER
//*---------------------------------------------------------------
//* TREXXVL - reproduce the httprexx IRXEXEC-via-LINK path in batch.
//*
//* Runs a hardcoded EBCDIC "say" exec through IRXINIT + IRXEXEC using
//* the VLIST (LINK) interface -- the exact path httprexx uses -- with
//* no httpd, no UFS, no encoding variable. See test/trexxvl.c.
//*
//* STEPLIB must point at the REXX370 LOAD library that holds
//* IRXINIT/IRXEXEC (the SAME library httpd loads them from) AND the
//* TREXXVL module (deploy it there with `make deploy` after
//* `make trexxvl`). Adjust the DSN below to your installation.
//*
//* Expected (bug in rexx370 IRXEXEC path):
//*   ... TREXXVL: IRXINIT link=0 rc=0 env=xxxxxxxx
//*   ... TREXXVL: calling IRXEXEC ...
//*   S0C1/S0C4 in IRXEXEC/IRXBEXEC  <-- reproduces httprexx
//*
//* Expected (rexx370 fine -> httprexx issue is UFS encoding):
//*   ... TREXXVL SAY: hello from irxexec via link
//*   ... TREXXVL: IRXEXEC link=0 rc=0 rexxrc=0
//*   ... TREXXVL: done OK      (STEP RC=0)
//*---------------------------------------------------------------
//STEP1   EXEC PGM=TREXXVL,REGION=4M
//STEPLIB  DD DSN=IBMUSER.REXX370.V1R0M0D.LINKLIB,DISP=SHR
//SYSPRINT DD SYSOUT=*
//SYSTSPRT DD SYSOUT=*
//
