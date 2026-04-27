//ASMPROB JOB (ACCT),'IRXPROBE BUILD',CLASS=A,MSGCLASS=H,
//             NOTIFY=&SYSUID,REGION=0M
//*-------------------------------------------------------------------*
//* ASMPROB - Assemble and link IRXPROBE on z/OS                      *
//*                                                                   *
//* Customise:                                                        *
//*   &USER     replace with your TSO userid (defaults to job NOTIFY) *
//*   SYSIN  IN substitute the actual source dataset/member or use    *
//*            //SYSIN DD * pasted from test/zos/asm/irxprobe.asm     *
//*   SYSLMOD   target load library; member IRXPROBE is created       *
//*-------------------------------------------------------------------*
//ASM      EXEC PGM=ASMA90,REGION=0M,
//             PARM='OBJECT,NODECK,LIST,XREF(SHORT)'
//SYSLIB   DD   DISP=SHR,DSN=SYS1.MACLIB
//         DD   DISP=SHR,DSN=SYS1.MODGEN
//SYSPRINT DD   SYSOUT=*
//SYSLIN   DD   DSN=&&OBJ,DISP=(NEW,PASS),
//             SPACE=(CYL,(1,1)),UNIT=SYSALLDA,
//             DCB=(RECFM=FB,LRECL=80,BLKSIZE=3200)
//SYSUT1   DD   UNIT=SYSALLDA,SPACE=(CYL,(1,1))
//SYSIN    DD   DISP=SHR,DSN=&SYSUID..REXX370.SOURCE(IRXPROBE)
//*
//LKED     EXEC PGM=IEWL,COND=(0,LT,ASM),
//             PARM='LIST,MAP,XREF,RENT,REUS,AMODE=31,RMODE=24'
//SYSLIB   DD   DISP=SHR,DSN=SYS1.CSSLIB
//SYSLIN   DD   DSN=&&OBJ,DISP=(OLD,DELETE)
//         DD   *
  ENTRY IRXPROBE
  NAME  IRXPROBE(R)
/*
//SYSUT1   DD   UNIT=SYSALLDA,SPACE=(CYL,(1,1))
//SYSPRINT DD   SYSOUT=*
//SYSLMOD  DD   DISP=SHR,DSN=&SYSUID..REXX370.LOAD
//
