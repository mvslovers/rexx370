         TITLE 'TNOMEM - GETMAIN failure inside an exec, no C runtime'
*
*  TNOMEM - GitHub #234.  Drives IRXINIT -> IRXEXEC -> IRXTERM from
*  pure assembler (no @@CRT0, so no C runtime in the task), with an
*  exec that asks for more storage than the 8M runner region has.
*  irxstor's GETMAIN fails.  Before #234 that failure went through
*  libc370 getmain() -> wtof(), which needs a C runtime.
*
*  The test is that IRXEXEC comes back at all (any RC) and IRXTERM
*  then succeeds.  The WTO shows IRXEXEC's R15.
*
*  Built from test/asm/texecvl.asm; see there for the VLIST shapes.
*
*  Return code:  0 ok,  8 LOAD EP= failed,  20 IRXINIT/IRXTERM failed.
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
TNOMEM   CSECT
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
         XC    WRCE,WRCE
         XC    WRCT,WRCT
*  IRXEXEC parameter words (the fullwords the VLIST slots point at).
         XC    PXEXEC,PXEXEC       P1 EXECBLK   = NULL
         XC    PXARG,PXARG         P2 ARGTABLE  = NULL
         XC    PXFLAG,PXFLAG       P3 FLAGS     = 0 (-> NOFLAG)
         XC    PXRES5,PXRES5       P5 reserved  = NULL
         XC    PXEVAL,PXEVAL       P6 EVALBLOCK = NULL
         XC    PXWKA,PXWKA         P7 WORKAREA  = NULL
         XC    PXUSR,PXUSR         P8 USERFIELD = NULL
         XC    PXENV,PXENV         P9 ENVBLOCK  (set after IRXINIT)
         XC    PXRXRC,PXRXRC       P10 REXXRC   (out)
*  P4 INSTBLK ptr = address of the static in-storage source block.
         LA    R1,INSTBLK
         ST    R1,PXINST
*
*  --- build IRXINIT VLIST (7 slots, VL on slot 7) ----------------
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
*
*  --- step 1: LOAD + call IRXINIT --------------------------------
         LOAD  EP=IRXINIT,ERRET=NOIRXIN
         LR    R3,R0               R3 = IRXINIT entry-point address
         SR    R0,R0               no previous-env hint
         LA    R1,VLIN
         LR    R15,R3
         BALR  R14,R15
         ST    R15,WRCI            saved IRXINIT RC
*
*  --- gate step 2 on a usable ENVBLOCK ---------------------------
         L     R3,WRCI
         LTR   R3,R3
         BNZ   FAILED              IRXINIT failed
         L     R3,OUTENV
         LTR   R3,R3
         BZ    FAILED              RC=0 but no envblock
         CLC   0(8,R3),=CL8'ENVBLOCK'
         BNE   FAILED              eye-catcher mismatch
*  P9 ENVBLOCK value = the env we just created.
         L     R1,OUTENV
         ST    R1,PXENV
*
*  --- build IRXEXEC VLIST (10 slots, VL on slot 10) --------------
         LA    R1,PXEXEC
         ST    R1,VLEX+0           P1 EXECBLK
         LA    R1,PXARG
         ST    R1,VLEX+4           P2 ARGTABLE
         LA    R1,PXFLAG
         ST    R1,VLEX+8           P3 FLAGS
         LA    R1,PXINST
         ST    R1,VLEX+12          P4 INSTBLK
         LA    R1,PXRES5
         ST    R1,VLEX+16          P5 reserved
         LA    R1,PXEVAL
         ST    R1,VLEX+20          P6 EVALBLOCK
         LA    R1,PXWKA
         ST    R1,VLEX+24          P7 WORKAREA
         LA    R1,PXUSR
         ST    R1,VLEX+28          P8 USERFIELD
         LA    R1,PXENV
         ST    R1,VLEX+32          P9 ENVBLOCK
         LA    R1,PXRXRC
         O     R1,=X'80000000'     VL marker on last slot (P10)
         ST    R1,VLEX+36          P10 REXXRC
*
*  --- step 2: LOAD + call IRXEXEC (R0 = envblock hint) -----------
         LOAD  EP=IRXEXEC,ERRET=NOIRXEX
         LR    R3,R0               R3 = IRXEXEC entry-point address
         L     R0,OUTENV           R0 = ENVBLOCK hint
         LA    R1,VLEX
         LR    R15,R3
         BALR  R14,R15
         ST    R15,WRCE            IRXEXEC R15 = REXX exit value
*
*  --- step 3: LOAD + call IRXTERM (teardown, R0 = ENVBLOCK) ------
         LOAD  EP=IRXTERM,ERRET=NOIRXTM
         LR    R3,R0               R3 = IRXTERM entry-point address
         L     R0,OUTENV           R0 = ENVBLOCK to terminate
         LR    R15,R3
         BALR  R14,R15
         ST    R15,WRCT            saved IRXTERM RC
*
*  --- evaluate: IRXEXEC came back (any RC) and IRXTERM rc 0 ---
*  Reaching this point at all is the test: without the fix a failed
*  GETMAIN inside the exec went through libc370 wtof() and needed a
*  C runtime this program does not have.
         L     R3,WRCT
         LTR   R3,R3
         BNZ   FAILED              IRXTERM failed
*
*  --- success path ----------------------------------------------
         BAL   R14,WTOSETUP
         MVC   WTOWORK+4(MSGLEN),OKMSG
         BAL   R14,FILLHEX
         WTO   MF=(E,WTOWORK)
         LA    R3,0                exit RC = 0
         B     EPILOG
*
FAILED   EQU   *
*  --- failure path ----------------------------------------------
         BAL   R14,WTOSETUP
         MVC   WTOWORK+4(MSGLEN),FAILMSG
         BAL   R14,FILLHEX
         WTO   MF=(E,WTOWORK)
         LA    R3,20               exit RC = 20
         B     EPILOG
*
*  --- LOAD failure paths ----------------------------------------
NOIRXIN  EQU   *
         WTO   'TNOMEM FAIL: LOAD EP=IRXINIT failed (check STEPLIB)'
         LA    R3,8
         B     EPILOG
*
NOIRXEX  EQU   *
         WTO   'TNOMEM FAIL: LOAD EP=IRXEXEC failed (check STEPLIB)'
         LA    R3,8
         B     EPILOG
*
NOIRXTM  EQU   *
         WTO   'TNOMEM FAIL: LOAD EP=IRXTERM failed (check STEPLIB)'
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
*  --- FILLHEX: fill ENV@17, RC@29, TRC@42 in text portion ------
*  Text portion of the WPL starts at WTOWORK+4.
FILLHEX  DS    0H
         ST    R14,SAVR14
         L     R1,OUTENV
         LA    R2,WTOWORK+4+17
         BAL   R14,FMTHEX
         L     R1,WRCE
         LA    R2,WTOWORK+4+29
         BAL   R14,FMTHEX
         L     R1,WRCT
         LA    R2,WTOWORK+4+42
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
RESVZ    DC    F'0'                IRXINIT P5 reserved zero
HEXTAB   DC    C'0123456789ABCDEF'
*
*  --- in-storage REXX source (INSTBLK) -------------------------
*  Read-only (dispatch copies the reconstructed source into its own
*  pool), so it is RENT-safe as static CSECT data.  The 128-byte
*  header is followed by an 8-byte entry per source line (statement
*  address + length); dispatch concatenates the statements with '\n'
*  separators.  The critical fields dispatch reads (acronym @+0,
*  instblk_address @+16, instblk_usedlen @+20) use explicit labels,
*  so the block is robust against any trailing-field alignment.
INSTBLK  DS    0F
         DC    CL8'IRXINSTB'       +0   acronym
         DC    A(128)              +8   hdrlen
         DC    A(0)                +12  reserved
         DC    A(IBENTS)           +16  instblk_address -> entries
         DC    A(IBENTE-IBENTS)    +20  instblk_usedlen (n * 8)
         DC    CL8' '              +24  member  (PARSE SOURCE)
         DC    CL8' '              +32  ddname
         DC    CL8' '              +40  subcom
         DC    A(0)                +48  reserved
         DC    A(0)                +52  dsnlen
         DC    CL54' '             +56  dsname
         DC    H'0'                +110 reserved
         DC    A(0)                +112 extname_ptr
         DC    A(0)                +116 extname_len
         DC    A(0),A(0)           +120 reserved
*
*  --- INSTBLK entries: A(statement), A(length) per source line ---
IBENTS   DS    0F
         DC    A(L01),A(L01E-L01)
         DC    A(L02),A(L02E-L02)
IBENTE   EQU   *
*
*  --- REXX source: one allocation the 8M region cannot satisfy ---
*  7.5 MB stays below 16 MB, where getmain() would return NULL before
*  ever issuing the GETMAIN (and so never reach wtof).
L01      DC    C'x = copies(''x'', 7500000)'
L01E     EQU   *
L02      DC    C'exit 1'
L02E     EQU   *
*
*  --- WTO parameter list skeleton (hand-built, SVC-35 standard) -
*  See asm/tinitvl.asm prologue for the IFOX00 col-71 rationale.
WTOSKEL  DS    0H
         DC    AL2(WTOEND-WTOSKEL)
         DC    AL2(0)
         DC    60C' '
WTOEND   EQU   *
WTOSKLEN EQU   *-WTOSKEL
*
*  --- 50-char message templates --------------------------------
*    01234567890123456789012345678901234567890123456789
*    0         1         2         3         4
*    TNOMEM  OK   ENV=XXXXXXXX RC=XXXXXXXX TRC=XXXXXXXX
*                     ^17          ^29          ^42
OKMSG    DC    CL50'TNOMEM  OK   ENV=XXXXXXXX RC=XXXXXXXX TRC=XXXXXXXX'
FAILMSG  DC    CL50'TNOMEM  FAIL ENV=XXXXXXXX RC=XXXXXXXX TRC=XXXXXXXX'
MSGLEN   EQU   50
*
*  --- workarea DSECT -------------------------------------------
WAREA    DSECT
WDFLAGS  DS    F
WDPREV   DS    F
WDNEXT   DS    F
         DS    15F
PARMP    DS    F                   IRXINIT P3 PARMBLOCK ptr (=0)
USERP    DS    F                   IRXINIT P4 user field
OUTENV   DS    F                   IRXINIT P6 out: ENVBLOCK
OUTRSN   DS    F                   IRXINIT P7 out: reason
WRCI     DS    F                   IRXINIT RC
WRCE     DS    F                   IRXEXEC RC (= REXX exit value)
WRCT     DS    F                   IRXTERM RC
SAVR14   DS    F                   FILLHEX R14 save slot
*  IRXEXEC parameter words (VLIST slots point at these).
PXEXEC   DS    F                   P1 EXECBLK   ptr
PXARG    DS    F                   P2 ARGTABLE  ptr
PXFLAG   DS    F                   P3 FLAGS     value
PXINST   DS    F                   P4 INSTBLK   ptr
PXRES5   DS    F                   P5 reserved
PXEVAL   DS    F                   P6 EVALBLOCK ptr
PXWKA    DS    F                   P7 WORKAREA  ptr
PXUSR    DS    F                   P8 USERFIELD ptr
PXENV    DS    F                   P9 ENVBLOCK  ptr
PXRXRC   DS    F                   P10 REXXRC   out
*  VLISTs.
VLIN     DS    7F                  IRXINIT VLIST (7 slots)
VLEX     DS    10F                 IRXEXEC VLIST (10 slots)
*  FMTHEX scratch (UNPK requires 5-byte source -> 9-byte target).
FMTTMP   DS    F
FMTBUF   DS    CL16
*  Writable WTO parameter list (copied from WTOSKEL at runtime).
WTOWORK  DS    CL(WTOSKLEN)
WALEN    EQU   *-WAREA
*
         END   TNOMEM
