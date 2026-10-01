         TITLE 'IRXQSAM - QSAM output, no C runtime'
*
*  IRXQSAM - OPEN a DD for OUTPUT, PUT records, CLOSE.
*
*  The output half of IRXINOUT, the default I/O routine (GitHub
*  #302).  Called from src/irx#io.c through include/irxqsam.h:
*
*      int qsam_call(struct qsam_parm *p) asm("IRXQSAM");
*
*  Entry   R1 -> list of argument VALUES (c2asm370 convention):
*                  0(R1) = QPARM address (layout below)
*          R13 -> caller save area,  R14 -> return
*  Exit    R15 = QPRC as well
*
*  QPFUNC  1 OPEN   QPNAME = DDNAME.  rc 0, or 8 = not opened (DD
*                   missing: OPEN says IEC130I and leaves DCBOFOPN
*                   off).  Returns LRECL, BLKSIZE, RECFM.
*          2 PUT    the record at QPREC, move mode.  The caller
*                   builds it: RDW for V, full LRECL for F, the
*                   carriage-control byte for A.  rc 20 when SYNAD
*                   reported an I/O error.
*          3 CLOSE  writes the last block.  Output that is never
*                   CLOSEd loses that block at step end, with every
*                   return code zero.
*
*  ---- Record format ----
*
*  A DD that names its own DCB values (DCB=(RECFM=FB,LRECL=80)) or
*  a data set that has them keeps them.  Where neither does, as on
*  SYSOUT=*, the OPEN exit fills in VB, LRECL 132 -- what libc370's
*  stdio wrote before #302 -- and a BLKSIZE that fits.
*
*  ---- Why no storage of its own ----
*
*  The DCB, the OPEN list and our save area live in QPAREA, which
*  the caller owns (getmain'ed through irxstor), so the module stays
*  RENT.  The model DCB is copied in on OPEN; its SYNAD and EXLST
*  addresses point back into this CSECT, which nothing writes.
*
*  NOTE for as370/IFOX00: RS-format LM/STM must be written D(B).
*
*  Ref: SC28-1883-0 Chapter 16 (Input/Output Routine); GitHub #302
*  (c) 2026 mvslovers - REXX/370 Project
*
         PRINT NOGEN
R0       EQU   0
R1       EQU   1
R2       EQU   2
R4       EQU   4
R5       EQU   5
R10      EQU   10
R11      EQU   11
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
FNOPEN   EQU   1
FNPUT    EQU   2
FNCLOSE  EQU   3
FOPEN    EQU   X'80'              QPFLAGS: DCB is open
FERR     EQU   X'20'              QPFLAGS: SYNAD reported an error
DEFLRECL EQU   132                default LRECL (VB)
         PRINT GEN
*
IRXQSAM  CSECT
         STM   R14,R12,12(R13)
         BALR  R12,0
         USING *,R12
*
         L     R10,0(,R1)         QPARM
         USING QPARM,R10
         LA    R11,QPAREA
         USING QAREA,R11
*
         MVC   QPRC,=F'20'        until something says otherwise
         LH    R2,QPALEN          does the caller's area fit?
         C     R2,=A(QALEN)
         BL    RETNOSA            no -- touch nothing in it
*
         ST    R13,QASAVE+4       chain our save area
         LA    R2,QASAVE
         ST    R2,8(,R13)
         LR    R13,R2
*
         LA    R4,QADCB
         USING IHADCB,R4
         L     R2,QPFUNC
         C     R2,=A(FNOPEN)
         BE    DOOPEN
         C     R2,=A(FNPUT)
         BE    DOPUT
         C     R2,=A(FNCLOSE)
         BE    DOCLOSE
         B     RETURN             unknown function: rc 20
*
*  ---- OPEN ----
DOOPEN   DS    0H
         MVC   QADCB(MDCBLEN),MDCB
         MVC   DCBDDNAM,QPNAME
         MVC   QAOPEN,MOPEN
         MVI   QPFLAGS,0
         OPEN  ((R4),(OUTPUT)),MF=(E,QAOPEN)
         TM    DCBOFLGS,DCBOFOPN
         BO    OPENOK
         MVC   QPRC,=F'8'         not opened (DD missing)
         B     RETURN
OPENOK   DS    0H
         OI    QPFLAGS,FOPEN
         MVC   QPLRECL,DCBLRECL
         MVC   QPBLKSI,DCBBLKSI
         MVC   QPRECFM,DCBRECFM
         XC    QPRC,QPRC
         B     RETURN
*
*  ---- PUT ----
DOPUT    DS    0H
         TM    QPFLAGS,FOPEN
         BZ    RETURN
         TM    QPFLAGS,FERR
         BO    RETURN             a failed DCB stays failed: rc 20
         L     R5,QPREC
         PUT   (R4),(R5)
         TM    QPFLAGS,FERR
         BO    RETURN
         XC    QPRC,QPRC
         B     RETURN
*
*  SYNAD: R14 returns into the access method, which then returns
*  from PUT normally; R10/R11/R12 are as they were at the PUT.
SYN      DS    0H
         OI    QPFLAGS,FERR
         BR    R14
*
*  ---- CLOSE ----
DOCLOSE  DS    0H
         TM    QPFLAGS,FOPEN
         BZ    CLOSED
         CLOSE ((R4)),MF=(E,QAOPEN)
CLOSED   DS    0H
         MVI   QPFLAGS,0
         XC    QPRC,QPRC
*
RETURN   DS    0H
         L     R13,QASAVE+4       back to the caller's save area
RETNOSA  DS    0H
         L     R15,QPRC
         L     R14,12(,R13)
         LM    R0,R12,20(R13)
         BR    R14
         DROP  R4,R10,R11,R12
*
*  ---- DCB OPEN exit: defaults where neither DD nor data set has
*  them.  Entered with R1 -> DCB, R14 = return, R15 = entry; only
*  R0, R1, R14 and R15 may be changed.
OPENEXIT DS    0H
         USING OPENEXIT,R15
         USING IHADCB,R1
         CLI   DCBRECFM,0
         BNE   OXLRECL
         MVI   DCBRECFM,DCBRECV+DCBRECBR    VB
OXLRECL  DS    0H
         CLC   DCBLRECL,=H'0'
         BNE   OXBLK
         MVC   DCBLRECL,=AL2(DEFLRECL)
OXBLK    DS    0H
         CLC   DCBBLKSI,=H'0'
         BNER  R14
         LH    R0,DCBLRECL        F: one record per block
         TM    DCBRECFM,DCBRECV
         BNO   OXSETBLK
         AH    R0,=H'4'           V: the record plus the BDW
OXSETBLK DS    0H
         STH   R0,DCBBLKSI
         BR    R14
         DROP  R1,R15
*
*  ---- models, copied into QAREA on OPEN ----
MDCB     DCB   DSORG=PS,MACRF=PM,DDNAME=IRXQSAM,SYNAD=SYN,EXLST=MEXLST
MDCBLEN  EQU   *-MDCB
MOPEN    OPEN  (MDCB,(OUTPUT)),MF=L
MEXLST   DC    X'85',AL3(OPENEXIT)   DCB OPEN exit, last entry
         LTORG
*
*  ---- the caller's parameter block (include/irxqsam.h) ----
QPARM    DSECT
QPFUNC   DS    F                  +0  function
QPRC     DS    F                  +4  return code (out)
QPNAME   DS    CL8                +8  DDNAME (OPEN)
QPREC    DS    A                  +16 PUT: record
QPLRECL  DS    H                  +20 OPEN: LRECL (out)
QPBLKSI  DS    H                  +22 OPEN: BLKSIZE (out)
QPRECFM  DS    X                  +24 OPEN: RECFM (out)
QPFLAGS  DS    X                  +25 ours
QPALEN   DS    H                  +26 length of QPAREA
         DS    F                  +28 reserved
QPAREA   DS    0D                 +32 ours: QAREA
*
QAREA    DSECT
QASAVE   DS    18F                save area for OPEN/PUT/CLOSE
         DS    0D
QADCB    DS    XL(MDCBLEN)        the DCB, copied from MDCB
         DS    0F
QAOPEN   DS    F                  OPEN/CLOSE list
QALEN    EQU   *-QAREA
*
         DCBD  DSORG=PS,DEVD=DA
*
         END   IRXQSAM
