//UPLPROB  JOB (ACCT),'IRXPROBE UPLOAD',CLASS=A,MSGCLASS=H,
//             NOTIFY=&SYSUID
//*-------------------------------------------------------------------*
//* UPLPROB - Allocate the source PDS for IRXPROBE on z/OS            *
//*                                                                   *
//* Run this once per system before submitting ASMPROB.  Then upload  *
//* test/zos/asm/irxprobe.asm to &SYSUID..REXX370.SOURCE(IRXPROBE)    *
//* via your usual transfer (e.g. zowe files upload, ftp, ind$file).  *
//*                                                                   *
//* Likewise allocate a REXX exec PDS and upload the eight drivers    *
//* from test/zos/rexx into it as members PROBED, PROBEA1 .. PROBEA7. *
//*-------------------------------------------------------------------*
//ALLOCSRC EXEC PGM=IEFBR14
//SOURCE   DD   DISP=(NEW,CATLG),DSN=&SYSUID..REXX370.SOURCE,
//             SPACE=(TRK,(15,5,20)),UNIT=SYSALLDA,
//             DCB=(RECFM=FB,LRECL=80,BLKSIZE=3120,DSORG=PO)
//LOAD     DD   DISP=(NEW,CATLG),DSN=&SYSUID..REXX370.LOAD,
//             SPACE=(TRK,(30,10,20)),UNIT=SYSALLDA,
//             DCB=(RECFM=U,LRECL=0,BLKSIZE=27998,DSORG=PO)
//REXX     DD   DISP=(NEW,CATLG),DSN=&SYSUID..REXX370.REXX,
//             SPACE=(TRK,(15,5,20)),UNIT=SYSALLDA,
//             DCB=(RECFM=FB,LRECL=80,BLKSIZE=3120,DSORG=PO)
//
