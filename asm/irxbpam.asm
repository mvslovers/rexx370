         TITLE 'IRXBPAM - Read a PDS member through BPAM, no C runtime'
*
*  IRXBPAM - OPEN a DD, FIND a member, READ its blocks, CLOSE.
*
*  The I/O half of IRXLDTSO, the exec load routine for TSO
*  environments (GitHub #230).  Called from src/irx#ldbp.c:
*
*      int bpam_call(struct bpam_parm *p) asm("IRXBPAM");
*
*  Entry   R1 -> list of argument VALUES (c2asm370 convention):
*                  0(R1) = BPARM address (layout below)
*          R13 -> caller save area,  R14 -> return
*  Exit    R15 = BPRC as well
*
*  BPFUNC  1 OPEN   BPNAME = DDNAME.  rc 0, or 8 = not opened (DD
*                   missing: OPEN says IEC130I and leaves DCBOFOPN
*                   off).  Returns BLKSIZE, LRECL, RECFM.
*          2 FIND   BPNAME = member.  rc = FIND's: 0 found,
*                   4 not found, 8 I/O error.
*          3 READ   next block of the member into BPBUF.  BPLEN =
*                   bytes read, 0 at the end of the member.  rc 20
*                   when SYNAD reported an I/O error.
*          4 CLOSE
*
*  ---- Why this exists ----
*
*  IRXLOAD reads through fopen("DD:dd(member)"), and stdio needs a C
*  runtime.  A TSO address space driven from assembler has none (see
*  asm/irxload.asm, "WHAT THIS WRAPPER GIVES YOU").  OPEN/FIND/READ/
*  CHECK need nothing but the DCB.
*
*  ---- Why no storage of its own ----
*
*  The DCB, the DECB, the OPEN list and our save area all live in
*  BPAREA, which the caller owns (getmain'ed through irxstor), so the
*  module stays RENT.  The models below are copied in on OPEN; their
*  EODAD/SYNAD addresses point back into this CSECT.
*
*  ---- Block length ----
*
*  A short block -- the last one of an FB member -- is shorter than
*  DCBBLKSI.  Its length is DCBBLKSI minus the residual count of the
*  CSW, halfword at IOB+14 (IOBCSW+5; DECB+16 -> IOB), the way
*  libc370's @@AREAD does it.
*
*  ---- FIND and IHBINNRA ----
*
*  FIND ...,D expands through IHBINNRA, which loads R1 from the DCB
*  operand BEFORE R0 from the name operand.  Registers 0 and 1 are
*  therefore unusable for either operand -- the trap behind KB
*  MVS-BLDL-0001.  R4/R5 are used here.
*
*  ---- Concatenations ----
*
*  SYSEXEC/SYSPROC are often concatenated.  The DCB takes BLKSIZE
*  from the first data set (KB MVS-JCL-0001); a later library with
*  larger blocks fails the READ, which lands in SYNAD -> rc 20.
*
*  NOTE for as370/IFOX00: RS-format LM/STM must be written D(B).
*
*  Ref: SC28-1883-0 Chapter 16 (Exec Load Routine); GitHub #230
*  (c) 2026 mvslovers - REXX/370 Project
*
         PRINT NOGEN
R0       EQU   0
R1       EQU   1
R2       EQU   2
R3       EQU   3
R4       EQU   4
R5       EQU   5
R10      EQU   10
R11      EQU   11
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
FNOPEN   EQU   1
FNFIND   EQU   2
FNREAD   EQU   3
FNCLOSE  EQU   4
FOPEN    EQU   X'80'              BPFLAGS: DCB is open
FEOD     EQU   X'40'              BPFLAGS: EODAD reached
FERR     EQU   X'20'              BPFLAGS: SYNAD reported an error
IOBRESID EQU   14                 IOB -> CSW residual count (H)
DECBIOB  EQU   16                 DECB -> IOB address
         PRINT GEN
*
IRXBPAM  CSECT
         STM   R14,R12,12(R13)
         BALR  R12,0
         USING *,R12
*
         L     R10,0(,R1)         BPARM
         USING BPARM,R10
         LA    R11,BPAREA
         USING BAREA,R11
*
         MVC   BPRC,=F'20'        until something says otherwise
         LH    R2,BPALEN          does the caller's area fit?
         C     R2,=A(BALEN)
         BL    RETNOSA            no -- touch nothing in it
*
         ST    R13,BASAVE+4       chain our save area
         LA    R2,BASAVE
         ST    R2,8(,R13)
         LR    R13,R2
*
         LA    R4,BADCB
         USING IHADCB,R4
         L     R2,BPFUNC
         C     R2,=A(FNOPEN)
         BE    DOOPEN
         C     R2,=A(FNFIND)
         BE    DOFIND
         C     R2,=A(FNREAD)
         BE    DOREAD
         C     R2,=A(FNCLOSE)
         BE    DOCLOSE
         B     RETURN             unknown function: rc 20
*
*  ---- OPEN ----
DOOPEN   DS    0H
         MVC   BADCB(MDCBLEN),MDCB
         MVC   DCBDDNAM,BPNAME
         MVC   BAOPEN,MOPEN
         XC    BADECB,BADECB
         MVI   BPFLAGS,0
         OPEN  ((R4),(INPUT)),MF=(E,BAOPEN)
         TM    DCBOFLGS,DCBOFOPN
         BO    OPENOK
         MVC   BPRC,=F'8'         not opened (DD missing)
         B     RETURN
OPENOK   DS    0H
         OI    BPFLAGS,FOPEN
         MVC   BPBLKSI,DCBBLKSI
         MVC   BPLRECL,DCBLRECL
         MVC   BPRECFM,DCBRECFM
         XC    BPRC,BPRC
         B     RETURN
*
*  ---- FIND ----
DOFIND   DS    0H
         TM    BPFLAGS,FOPEN
         BZ    RETURN
         NI    BPFLAGS,255-FEOD-FERR
         LA    R5,BPNAME
         FIND  (R4),(R5),D
         ST    R15,BPRC
         B     RETURN
*
*  ---- READ ----
DOREAD   DS    0H
         TM    BPFLAGS,FOPEN
         BZ    RETURN
         TM    BPFLAGS,FEOD
         BO    READEOD
         LA    R3,BADECB
         L     R5,BPBUF
         XC    BADECB,BADECB
         READ  (R3),SF,(R4),(R5),'S',MF=E
         CHECK (R3)
AFTERCHK DS    0H
         TM    BPFLAGS,FEOD
         BO    READEOD
         TM    BPFLAGS,FERR
         BO    RETURN             rc stays 20
         L     R14,BADECB+DECBIOB IOB
         SLR   R1,R1
         ICM   R1,B'0011',IOBRESID(R14)  residual count
         LH    R2,DCBBLKSI
         SR    R2,R1
         ST    R2,BPLEN
         XC    BPRC,BPRC
         B     RETURN
READEOD  DS    0H
         XC    BPLEN,BPLEN
         XC    BPRC,BPRC
         B     RETURN
*
*  EODAD: entered from CHECK with R2-R13 as they were at CHECK, so
*  R10/R11/R12 still address BPARM, BAREA and this CSECT.
EOD      DS    0H
         OI    BPFLAGS,FEOD
         B     AFTERCHK
*
*  SYNAD: same registers as at CHECK; R14 returns into the access
*  method, which then returns from CHECK normally.
SYN      DS    0H
         OI    BPFLAGS,FERR
         BR    R14
*
*  ---- CLOSE ----
DOCLOSE  DS    0H
         TM    BPFLAGS,FOPEN
         BZ    CLOSED
         CLOSE ((R4)),MF=(E,BAOPEN)
CLOSED   DS    0H
         MVI   BPFLAGS,0
         XC    BPRC,BPRC
*
RETURN   DS    0H
         L     R13,BASAVE+4       back to the caller's save area
RETNOSA  DS    0H
         L     R15,BPRC
         L     R14,12(,R13)
         LM    R0,R12,20(R13)
         BR    R14
         DROP  R4
*
*  ---- models, copied into BAREA on OPEN ----
MDCB     DCB   DSORG=PO,MACRF=R,DDNAME=IRXBPAM,EODAD=EOD,SYNAD=SYN
MDCBLEN  EQU   *-MDCB
MOPEN    OPEN  (MDCB,(INPUT)),MF=L
         LTORG
*
*  ---- the caller's parameter block (src/irx#ldbp.c) ----
BPARM    DSECT
BPFUNC   DS    F                  +0  function
BPRC     DS    F                  +4  return code (out)
BPNAME   DS    CL8                +8  DDNAME (OPEN) / member (FIND)
BPBUF    DS    A                  +16 READ buffer
BPLEN    DS    F                  +20 READ: bytes read (out)
BPBLKSI  DS    H                  +24 OPEN: BLKSIZE (out)
BPLRECL  DS    H                  +26 OPEN: LRECL (out)
BPRECFM  DS    X                  +28 OPEN: RECFM (out)
BPFLAGS  DS    X                  +29 ours
BPALEN   DS    H                  +30 length of BPAREA
BPAREA   DS    0D                 +32 ours: BAREA
*
BAREA    DSECT
BASAVE   DS    18F                save area for OPEN/READ/CHECK
         DS    0D
BADCB    DS    XL(MDCBLEN)        the DCB, copied from MDCB
         DS    0F
BADECB   DS    XL20               DECB for READ/CHECK
BAOPEN   DS    F                  OPEN/CLOSE list
BALEN    EQU   *-BAREA
*
         DCBD  DSORG=PO,DEVD=DA
*
         END   IRXBPAM
