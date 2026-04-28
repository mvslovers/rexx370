         TITLE 'TTERMVL - Live MVS test caller for IRXINIT + IRXTERM'
*
*  TTERMVL - Minimal HLASM test caller that drives IRXINIT INITENVB
*            followed by IRXTERM end-to-end, exercising both
*            production load modules from STEPLIB.
*
*  Step 1: IRXINIT INITENVB via SC28-1883-0 §14 VLIST (same shape
*          as TINITVL — see asm/tinitvl.asm prologue for details).
*
*  Step 2: IRXTERM via SC28-1883-0 §15 contract:
*            R0  in  = ENVBLOCK to terminate
*            R0  out = predecessor ENVBLOCK (or original on failure)
*            R15 out = RC (0 ok, 4 warning, 20 bad ENVBLOCK)
*
*  WTO output (60-char fixed layout):
*    TTERMVL OK   ENVBLOCK=xxxxxxxx PRED=xxxxxxxx RC=xxxxxxxx
*    TTERMVL FAIL ENVBLOCK=xxxxxxxx PRED=xxxxxxxx RC=xxxxxxxx
*
*  Return code (R15 to JCL):
*     0  IRXINIT ok and IRXTERM RC in {0, 4}
*     4  IRXTERM warning (returned RC=4)
*     8  LOAD EP= failed for IRXINIT or IRXTERM
*    20  IRXINIT non-zero, eye-catcher mismatch, or IRXTERM RC=20+
*
*  Ref: SC28-1883-0 §14, §15
*  Ref: WP-I1c.5 / TSK-198 / GitHub mvslovers/rexx370#83 / #87
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
         PRINT GEN
*
TTERMVL  CSECT
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
*  --- step 1: LOAD + call IRXINIT --------------------------------
         LOAD  EP=IRXINIT,ERRET=NOIRXIN
         LR    R3,R0               R3 = IRXINIT entry-point address
         SR    R0,R0               no previous-env hint
         LA    R1,VLIST
         LR    R15,R3
         BALR  R14,R15
         ST    R15,WRCI            saved IRXINIT RC
         DELETE EP=IRXINIT
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
*  --- step 2: LOAD + call IRXTERM --------------------------------
         LOAD  EP=IRXTERM,ERRET=NOIRXTM
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
*  --- success path ----------------------------------------------
OKPATH   EQU   *
         BAL   R14,WTOSETUP
         MVC   WTOWORK+4(MSGLEN),OKMSG
         BAL   R14,FILLHEX
         WTO   MF=(E,WTOWORK)
         L     R3,WRCT             pass IRXTERM RC up to JCL
         B     EPILOG
*
*  --- failure path: bad eye-catcher (no ENVBLOCK to TERM) -------
FAILEYE  EQU   *
*  Force a deterministic RC into WRCT for the WTO output (we never
*  ran TERM; show 0 to indicate "TERM not attempted"). The exit
*  code will still be 20 because INIT did not produce a valid env.
         LA    R3,20
         ST    R3,WRCI             override WRCI so message shows it
         B     FAILEMIT
*
*  --- failure path: non-zero RC from INIT or TERM ---------------
FAILRC   EQU   *
FAILEMIT EQU   *
         BAL   R14,WTOSETUP
         MVC   WTOWORK+4(MSGLEN),FAILMSG
         BAL   R14,FILLHEX
         WTO   MF=(E,WTOWORK)
*  Choose the worst RC for the JCL exit code. If TERM ran and
*  failed, prefer its RC; otherwise use the INIT RC.
         L     R3,WRCT
         LTR   R3,R3
         BNZ   EPILOG
         L     R3,WRCI
         B     EPILOG
*
*  --- LOAD failure paths ----------------------------------------
NOIRXIN  EQU   *
         WTO   'TTERMVL FAIL: LOAD EP=IRXINIT failed (check STEPLIB)'
         LA    R3,8
         B     EPILOG
*
NOIRXTM  EQU   *
         WTO   'TTERMVL FAIL: LOAD EP=IRXTERM failed (check STEPLIB)'
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
*  --- WTOSETUP: copy MF=L skeleton from CSECT into workarea ----
WTOSETUP DS    0H
         MVC   WTOWORK(WTOSKLEN),WTOSKEL
         BR    R14
*
*  --- FILLHEX: fill ENV@22, PRED@36, RC@48 in text portion -----
*  Text portion of the WPL starts at WTOWORK+4. The RC slot shows
*  WRCT if TERM ran, otherwise WRCI (FAILRC and FAILEMIT both
*  funnel here after the appropriate slot has been set).
FILLHEX  DS    0H
         ST    R14,SAVR14
         L     R1,OUTENV
         LA    R2,WTOWORK+4+22
         BAL   R14,FMTHEX
         L     R1,WPRED
         LA    R2,WTOWORK+4+36
         BAL   R14,FMTHEX
*  Pick the diagnostically-relevant RC: TERM RC if TERM ran (WRCT
*  non-zero or WRCI=0), otherwise INIT RC.
         L     R1,WRCT
         LTR   R1,R1
         BNZ   FHRCT
         L     R1,WRCI
         LTR   R1,R1
         BZ    FHRCT               both zero -> show WRCT (=0)
         B     FHEMIT              WRCT=0 but WRCI!=0 -> show WRCI
FHRCT    L     R1,WRCT
FHEMIT   LA    R2,WTOWORK+4+48
         BAL   R14,FMTHEX
         L     R14,SAVR14
         BR    R14
*
*  --- FMTHEX: R1 -> 8 hex chars at R2 ---------------------------
FMTHEX   DS    0H
         ST    R1,FMTTMP
         UNPK  FMTBUF(9),FMTTMP(5)
         TR    FMTBUF(8),HEXTAB-X'F0'
         MVC   0(8,R2),FMTBUF
         BR    R14
*
         LTORG
*
*  --- static input data ----------------------------------------
FCODE    DC    CL8'INITENVB'
PARMODE  DC    CL8' '
RESVZ    DC    F'0'
HEXTAB   DC    C'0123456789ABCDEF'
*
*  --- WTO list-form skeleton (60-char text) -------------------
WTOSKEL  WTO   MF=L,                                                   *
               TEXT='                                                            '
WTOSKLEN EQU   *-WTOSKEL
*
*  --- 60-char message templates -------------------------------
*    01234567890123456789012345678901234567890123456789012345678901
*    0         1         2         3         4         5         6
*    TTERMVL OK   ENVBLOCK=XXXXXXXX PRED=XXXXXXXX RC=XXXXXXXX
*                          ^22            ^36          ^48
OKMSG    DC    CL60'TTERMVL OK   ENVBLOCK=XXXXXXXX PRED=XXXXXXXX RC=XXXXXXXX'
FAILMSG  DC    CL60'TTERMVL FAIL ENVBLOCK=XXXXXXXX PRED=XXXXXXXX RC=XXXXXXXX'
MSGLEN   EQU   60
*
*  --- workarea DSECT ------------------------------------------
WAREA    DSECT
WDFLAGS  DS    F
WDPREV   DS    F
WDNEXT   DS    F
         DS    15F
PARMP    DS    F
USERP    DS    F
OUTENV   DS    F
OUTRSN   DS    F
WRCI     DS    F                   IRXINIT RC
WRCT     DS    F                   IRXTERM RC
WPRED    DS    F                   IRXTERM out: predecessor envblock
SAVR14   DS    F
VLIST    DS    7F
FMTTMP   DS    F
FMTBUF   DS    CL16
WTOWORK  DS    CL(WTOSKLEN)
WALEN    EQU   *-WAREA
*
         END   TTERMVL
