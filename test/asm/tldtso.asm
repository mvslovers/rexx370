         TITLE 'TLDTSO - EXROUT load + IRXEXEC from assembler, no CRT'
*
*  TLDTSO - The acceptance test for IRXLDTSO (GitHub #230).
*
*  An assembler program -- no @@CRT0, so no C runtime anywhere in
*  this task -- does what IKJCT437 will do:
*
*    1  IRXINIT INITENVB (7-slot VLIST, no parm module name).  Under
*       IKJEFT01 IRXINIT picks IRXTSPRM, which names IRXLDTSO in
*       MODNAMET EXROUT and IRXIOTSO in IORT.
*    2  ENVBLOCK+28 -> IRXEXTE, IRXEXTE+8 -> load_routine.
*    3  LOAD the fixture exec TLDTSOX through load_routine in the form
*       SC28-1883-0 Chapter 16 gives a replaceable routine: R0 =
*       ENVBLOCK, R1 -> three addresses (function, EXECBLK, INSTBLK),
*       VL on the third.
*    4  IRXEXEC the INSTBLK.  The exec counts 45 increments spread over
*       both blocks of the member and ends 'exit n', so R15 = 45 only
*       if every record arrived -- including the short second block.
*       Its SAY goes through IRXIOTSO (PUTLINE) and must appear in
*       SYSTSPRT: read the spool, the RC cannot show it.
*    5  FREE through load_routine, then IRXTERM.
*
*  Before IRXLDTSO, step 3 was IRXLOAD, whose stdio reader ABENDs
*  S0C4 without a C runtime (asm/irxload.asm header).
*
*  PARM '1' = TSO leg (load_routine must be there), anything else =
*  batch leg (IRXPARMS leaves EXROUT blank, load_routine stays 0; the
*  test then only checks that and terminates).
*
*  WTO (50-char layout, see texecvl.asm):
*    TLDTSO OK   ENV=xxxxxxxx XRC=xxxxxxxx LRC=xxxxxxxx
*    TLDTSO FAIL ENV=xxxxxxxx XRC=xxxxxxxx LRC=xxxxxxxx
*  (XRC = IRXEXEC R15 = exit value, LRC = load_routine LOAD R15.)
*
*  Return code:  0 ok,  8 LOAD EP= failed,  20 any check failed.
*
*  (c) 2026 mvslovers - REXX/370 Project
*
         PRINT NOGEN
R0       EQU   0
R1       EQU   1
R2       EQU   2
R3       EQU   3
R4       EQU   4
R5       EQU   5
R6       EQU   6
R7       EQU   7
R8       EQU   8
R9       EQU   9
R10      EQU   10
R11      EQU   11
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
ENVEXTE  EQU   28                 ENVBLOCK -> IRXEXTE
EXTELOAD EQU   8                  IRXEXTE  -> load_routine
EXPECTN  EQU   45                 exit value of TLDTSOX
         PRINT GEN
*
TLDTSO   CSECT
         STM   R14,R12,12(R13)
         BALR  R12,0
         USING *,R12
         LR    R9,R1              PARM list
*
         L     R0,=A(WALEN)
         GETMAIN RU,LV=(0)
         LR    R8,R1
         ST    R13,4(,R1)
         ST    R1,8(,R13)
         LR    R13,R1
         USING WAREA,R13
*
         XC    PARMP(WZEROLEN),PARMP   all inputs/outputs = 0
         LA    R1,EXECBLK
         ST    R1,PEXECB
*
*  --- PARM: '1' means the TSO leg -------------------------------
         MVI   WTSO,0
         LTR   R9,R9
         BZ    NOPARM
         L     R2,0(,R9)          -> halfword length + text
         LA    R2,0(,R2)          drop the VL bit
         LH    R3,0(,R2)
         LTR   R3,R3
         BNP   NOPARM
         CLI   2(R2),C'1'
         BNE   NOPARM
         MVI   WTSO,1
NOPARM   DS    0H
*
*  --- step 1: IRXINIT INITENVB ----------------------------------
         LA    R1,FCODE
         ST    R1,VLIN+0
         LA    R1,PARMODE
         ST    R1,VLIN+4
         LA    R1,PARMP
         ST    R1,VLIN+8
         LA    R1,USERP
         ST    R1,VLIN+12
         LA    R1,RESVZ
         ST    R1,VLIN+16
         LA    R1,OUTENV
         ST    R1,VLIN+20
         LA    R1,OUTRSN
         O     R1,=X'80000000'
         ST    R1,VLIN+24
         LOAD  EP=IRXINIT,ERRET=NOLOAD
         LR    R15,R0
         SR    R0,R0
         LA    R1,VLIN
         BALR  R14,R15
         LTR   R15,R15
         BNZ   FAILED
         L     R4,OUTENV
         LTR   R4,R4
         BZ    FAILED
         CLC   0(8,R4),=CL8'ENVBLOCK'
         BNE   FAILED
*
*  --- step 2: IRXEXTE -> load_routine ---------------------------
         L     R5,ENVEXTE(,R4)
         LTR   R5,R5
         BZ    FAILED
         L     R6,EXTELOAD(,R5)
         ST    R6,WLOADR
         CLI   WTSO,1
         BE    TSOLEG
*  Batch leg: no EXROUT named, so nothing may have been loaded.
         LTR   R6,R6
         BNZ   FAILTERM
         B     TERMOK
*
TSOLEG   DS    0H
         LTR   R6,R6
         BZ    FAILTERM           IRXLDTSO not wired
*
*  --- step 3: LOAD through load_routine (3-slot form, R0) -------
         LA    R1,FCLOAD
         ST    R1,VLLD+0
         LA    R1,PEXECB
         ST    R1,VLLD+4
         LA    R1,PINSTB
         O     R1,=X'80000000'
         ST    R1,VLLD+8
         L     R0,OUTENV
         LA    R1,VLLD
         L     R15,WLOADR
         BALR  R14,R15
         ST    R15,WRCL
         LTR   R15,R15
         BNZ   FAILTERM
         L     R1,PINSTB
         LTR   R1,R1
         BZ    FAILTERM
         ST    R1,PXINST
*
*  --- step 4: IRXEXEC the loaded INSTBLK (10-slot form) ---------
         L     R1,OUTENV
         ST    R1,PXENV
         LA    R1,PXEXEC
         ST    R1,VLEX+0
         LA    R1,PXARG
         ST    R1,VLEX+4
         LA    R1,PXFLAG
         ST    R1,VLEX+8
         LA    R1,PXINST
         ST    R1,VLEX+12
         LA    R1,PXRES5
         ST    R1,VLEX+16
         LA    R1,PXEVAL
         ST    R1,VLEX+20
         LA    R1,PXWKA
         ST    R1,VLEX+24
         LA    R1,PXUSR
         ST    R1,VLEX+28
         LA    R1,PXENV
         ST    R1,VLEX+32
         LA    R1,PXRXRC
         O     R1,=X'80000000'
         ST    R1,VLEX+36
         LOAD  EP=IRXEXEC,ERRET=NOLOAD
         LR    R15,R0
         L     R0,OUTENV
         LA    R1,VLEX
         BALR  R14,R15
         ST    R15,WRCE
*
*  --- step 5: FREE through load_routine -------------------------
         LA    R1,FCFREE
         ST    R1,VLLD+0
         L     R0,OUTENV
         LA    R1,VLLD
         L     R15,WLOADR
         BALR  R14,R15
         LTR   R15,R15
         BNZ   FAILTERM
         L     R1,PINSTB
         LTR   R1,R1              FREE must clear the pointer
         BNZ   FAILTERM
*
         L     R1,WRCE
         C     R1,=A(EXPECTN)
         BNE   FAILTERM
*
TERMOK   DS    0H
         BAL   R14,DOTERM
         LTR   R15,R15
         BNZ   FAILED
         BAL   R14,WTOSETUP
         MVC   WTOWORK+4(MSGLEN),OKMSG
         BAL   R14,FILLHEX
         WTO   MF=(E,WTOWORK)
         LA    R3,0
         B     EPILOG
*
FAILTERM DS    0H
         BAL   R14,DOTERM
FAILED   DS    0H
         BAL   R14,WTOSETUP
         MVC   WTOWORK+4(MSGLEN),FAILMSG
         BAL   R14,FILLHEX
         WTO   MF=(E,WTOWORK)
         LA    R3,20
         B     EPILOG
*
NOLOAD   DS    0H
         WTO   'TLDTSO FAIL: LOAD EP= failed (check STEPLIB)'
         LA    R3,8
         B     EPILOG
*
*  --- IRXTERM (R0 = ENVBLOCK); R15 = its rc -------------------
DOTERM   DS    0H
         ST    R14,SAVTRM
         LOAD  EP=IRXTERM,ERRET=NOLOAD
         LR    R15,R0
         L     R0,OUTENV
         BALR  R14,R15
         L     R14,SAVTRM
         BR    R14
*
EPILOG   DS    0H
         L     R13,WDPREV
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(8)
         LR    R15,R3
         L     R14,12(,R13)
         LM    R1,R12,24(R13)
         BR    R14
*
WTOSETUP DS    0H
         MVC   WTOWORK(WTOSKLEN),WTOSKEL
         BR    R14
*
*  ENV@16, XRC@29, LRC@42 in the text portion (WTOWORK+4).
FILLHEX  DS    0H
         ST    R14,SAVR14
         L     R1,OUTENV
         LA    R2,WTOWORK+4+16
         BAL   R14,FMTHEX
         L     R1,WRCE
         LA    R2,WTOWORK+4+29
         BAL   R14,FMTHEX
         L     R1,WRCL
         LA    R2,WTOWORK+4+42
         BAL   R14,FMTHEX
         L     R14,SAVR14
         BR    R14
*
FMTHEX   DS    0H
         ST    R1,FMTTMP
         UNPK  FMTBUF(9),FMTTMP(5)
         TR    FMTBUF(8),HEXTAB-X'F0'
         MVC   0(8,R2),FMTBUF
         BR    R14
*
         LTORG
*
FCODE    DC    CL8'INITENVB'
PARMODE  DC    CL8' '
RESVZ    DC    F'0'
FCLOAD   DC    CL8'LOAD'
FCFREE   DC    CL8'FREE'
HEXTAB   DC    C'0123456789ABCDEF'
*
*  EXECBLK: input only, so static is RENT-safe.  V1 length X'30'.
EXECBLK  DS    0F
         DC    CL8'IRXEXECB'       +0  acronym
         DC    A(X'30')            +8  length
         DC    A(0)                +12 reserved
         DC    CL8'TLDTSOX'        +16 member
         DC    CL8' '              +24 ddname: SYSEXEC, SYSPROC
         DC    CL8' '              +32 subcom
         DC    A(0)                +40 dsnptr
         DC    A(0)                +44 dsnlen
*
WTOSKEL  DS    0H
         DC    AL2(WTOEND-WTOSKEL)
         DC    AL2(0)
         DC    60C' '
WTOEND   EQU   *
WTOSKLEN EQU   *-WTOSKEL
*
*    01234567890123456789012345678901234567890123456789
*    TLDTSO OK   ENV=XXXXXXXX XRC=XXXXXXXX LRC=XXXXXXXX
*                    ^16          ^29          ^42
OKMSG    DC    CL50'TLDTSO OK   ENV=XXXXXXXX XRC=XXXXXXXX LRC=XXXXXXXX'
FAILMSG  DC    CL50'TLDTSO FAIL ENV=XXXXXXXX XRC=XXXXXXXX LRC=XXXXXXXX'
MSGLEN   EQU   50
*
WAREA    DSECT
WDFLAGS  DS    F
WDPREV   DS    F
WDNEXT   DS    F
         DS    15F
PARMP    DS    F                  IRXINIT P3 PARMBLOCK ptr (=0)
USERP    DS    F                  IRXINIT P4 user field
OUTENV   DS    F                  IRXINIT P6 out: ENVBLOCK
OUTRSN   DS    F                  IRXINIT P7 out: reason
WRCE     DS    F                  IRXEXEC R15 (exit value)
WRCL     DS    F                  load_routine LOAD R15
WLOADR   DS    F                  load_routine entry
PEXECB   DS    F                  -> EXECBLK
PINSTB   DS    F                  INSTBLK ptr (LOAD out, FREE in)
PXEXEC   DS    F                  IRXEXEC P1 .. P10
PXARG    DS    F
PXFLAG   DS    F
PXINST   DS    F
PXRES5   DS    F
PXEVAL   DS    F
PXWKA    DS    F
PXUSR    DS    F
PXENV    DS    F
PXRXRC   DS    F
WZEROLEN EQU   *-PARMP
WTSO     DS    X                  1 = TSO leg
         DS    0F
SAVR14   DS    F
SAVTRM   DS    F
VLIN     DS    7F
VLLD     DS    3F
VLEX     DS    10F
FMTTMP   DS    F
FMTBUF   DS    CL16
WTOWORK  DS    CL(WTOSKLEN)
WALEN    EQU   *-WAREA
*
         END   TLDTSO
