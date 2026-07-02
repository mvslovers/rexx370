         TITLE 'TLNKTERM - LINK IRXINIT + LOAD/BALR IRXTERM (asymmetry)'
*
*  TLNKTERM - reproduce httprexx's IRXINIT/IRXTERM invocation shape in
*             PURE ASM, to isolate the LINK-vs-BALR asymmetry from the
*             crent370 C-runtime context.
*
*  httprexx (and TREXXVL) drive IRXINIT via __linkds (LINK, SVC 6 ->
*  its own PRB) but IRXTERM via __load + BALR (runs under the caller's
*  PRB). ttermvl (which passes) uses LOAD+BALR for BOTH -> one PRB
*  throughout. This test keeps ttermvl's asm/no-C-runtime environment
*  but switches IRXINIT to LINK, so the ONLY change vs ttermvl is:
*      IRXINIT: LOAD+BALR  ->  LINK
*
*  Outcome:
*    IRXTERM RC=0/4  -> the LINK-vs-BALR asymmetry is NOT the cause;
*                       the crent370 C context is (TREXXVL/httprexx).
*    S0C1 in IRXTERM -> the asymmetry IS the cause, independent of C.
*    RC=8 "ep=0"     -> the as370 LOAD-macro-after-LINK quirk fired
*                       (inconclusive; distinct from a real crash).
*
*  (c) 2026 mvslovers - REXX/370 Project
*
         PRINT NOGEN
R0       EQU   0
R1       EQU   1
R2       EQU   2
R3       EQU   3
R4       EQU   4
R8       EQU   8
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
         PRINT GEN
*
TLNKTERM CSECT
*
*  --- standard MVS entry linkage ---
         STM   R14,R12,12(R13)
         BALR  R12,0
         USING *,R12
*
*  --- allocate dynamic workarea (RENT) ---------------------------
         L     R0,=A(WALEN)
         GETMAIN RU,LV=(0)
         LR    R8,R1               R8 = workarea ptr (saved for FREE)
         ST    R13,4(,R1)
         ST    R1,8(,R13)
         LR    R13,R1
         USING WAREA,R13
*
*  --- zero the slots used as inputs / outputs --------------------
         XC    PARMP,PARMP
         XC    USERP,USERP
         XC    OUTENV,OUTENV
         XC    OUTRSN,OUTRSN
         XC    WRCI,WRCI
         XC    WRCT,WRCT
         XC    WPRED,WPRED
*
*  --- build IRXINIT VLIST (7 slots, VL on slot 7) ----------------
         LA    R1,FCODE
         ST    R1,VLIST+0
         LA    R1,PARMODE
         ST    R1,VLIST+4
         LA    R1,PARMP
         ST    R1,VLIST+8
         LA    R1,USERP
         ST    R1,VLIST+12
         LA    R1,RESVZ
         ST    R1,VLIST+16
         LA    R1,OUTENV
         ST    R1,VLIST+20
         LA    R1,OUTRSN
         O     R1,=X'80000000'
         ST    R1,VLIST+24
*
*  --- step 1: LINK to IRXINIT (own PRB -- httprexx's __linkds) ---
*  R1 -> VLIST is passed straight through to IRXINIT, exactly as
*  __linkds does. No LOAD/DELETE: LINK manages the module.
         LA    R1,VLIST
         LINK  EP=IRXINIT
         ST    R15,WRCI            saved IRXINIT RC
*
*  --- gate step 2 on a usable ENVBLOCK ---------------------------
         L     R3,WRCI
         LTR   R3,R3
         BNZ   FAILRC              IRXINIT failed -> skip TERM
         L     R3,OUTENV
         LTR   R3,R3
         BZ    FAILEYE             RC=0 but no envblock -> bail
         CLC   0(8,R3),=CL8'ENVBLOCK'
         BNE   FAILEYE             eye-catcher mismatch -> bail
*
*  --- step 2: LOAD + BALR IRXTERM (caller PRB -- __load+HRXCALL) -
         LOAD  EP=IRXTERM,ERRET=NOIRXTM
         LTR   R0,R0
         BZ    EPZERO              LOAD returned ep=0 (MVS 3.8j quirk)
         LR    R3,R0               R3 = IRXTERM entry-point address
         L     R0,OUTENV           R0 = ENVBLOCK to terminate
         LR    R15,R3
         BALR  R14,R15
         ST    R15,WRCT            saved IRXTERM RC
         ST    R0,WPRED            saved predecessor (R0 out)
         DELETE EP=IRXTERM
*
*  --- evaluate IRXTERM result ------------------------------------
         L     R3,WRCT
         LTR   R3,R3
         BZ    OKPATH              RC=0 -> success
         CH    R3,=H'4'
         BE    OKPATH              RC=4 -> success-warning
         B     FAILRC              RC>=8 -> failure
*
OKPATH   EQU   *
         BAL   R14,WTOSETUP
         MVC   WTOWORK+4(MSGLEN),OKMSG
         BAL   R14,FILLHEX
         WTO   MF=(E,WTOWORK)
         L     R3,WRCT
         B     EPILOG
*
FAILEYE  EQU   *
         LA    R3,20
         ST    R3,WRCI
         B     FAILEMIT
*
FAILRC   EQU   *
FAILEMIT EQU   *
         BAL   R14,WTOSETUP
         MVC   WTOWORK+4(MSGLEN),FAILMSG
         BAL   R14,FILLHEX
         WTO   MF=(E,WTOWORK)
         L     R3,WRCT
         LTR   R3,R3
         BNZ   EPILOG
         L     R3,WRCI
         B     EPILOG
*
EPZERO   EQU   *
         WTO   'TLNKTERM INCONCLUSIVE: LOAD EP=IRXTERM returned ep=0'
         LA    R3,8
         B     EPILOG
*
NOIRXTM  EQU   *
         WTO   'TLNKTERM FAIL: LOAD EP=IRXTERM failed (check STEPLIB)'
         LA    R3,8
         B     EPILOG
*
*  --- common epilog: free workarea, restore regs, return --------
EPILOG   EQU   *
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
FILLHEX  DS    0H
         ST    R14,SAVR14
         L     R1,OUTENV
         LA    R2,WTOWORK+4+17
         BAL   R14,FMTHEX
         L     R1,WPRED
         LA    R2,WTOWORK+4+30
         BAL   R14,FMTHEX
         L     R1,WRCT
         LTR   R1,R1
         BNZ   FHRCT
         L     R1,WRCI
         LTR   R1,R1
         BZ    FHRCT
         B     FHEMIT
FHRCT    L     R1,WRCT
FHEMIT   LA    R2,WTOWORK+4+42
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
HEXTAB   DC    C'0123456789ABCDEF'
*
WTOSKEL  DS    0H
         DC    AL2(WTOEND-WTOSKEL)
         DC    AL2(0)
         DC    60C' '
WTOEND   EQU   *
WTOSKLEN EQU   *-WTOSKEL
*
OKMSG    DC    CL50'TLNKTRM OK   ENV=XXXXXXXX PRD=XXXXXXXX RC=XXXXXXXX'
FAILMSG  DC    CL50'TLNKTRM FAIL ENV=XXXXXXXX PRD=XXXXXXXX RC=XXXXXXXX'
MSGLEN   EQU   50
*
WAREA    DSECT
WDFLAGS  DS    F
WDPREV   DS    F
WDNEXT   DS    F
         DS    15F
PARMP    DS    F
USERP    DS    F
OUTENV   DS    F
OUTRSN   DS    F
WRCI     DS    F
WRCT     DS    F
WPRED    DS    F
SAVR14   DS    F
VLIST    DS    7F
FMTTMP   DS    F
FMTBUF   DS    CL16
WTOWORK  DS    CL(WTOSKLEN)
WALEN    EQU   *-WAREA
*
         END   TLNKTERM
