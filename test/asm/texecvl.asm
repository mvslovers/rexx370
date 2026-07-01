         TITLE 'TEXECVL - Live MVS test: IRXINIT + IRXEXEC + IRXTERM'
*
*  TEXECVL - Minimal HLASM test caller that drives IRXINIT INITENVB,
*            then IRXEXEC of an in-storage REXX exec, then IRXTERM,
*            end-to-end through the production load modules from
*            STEPLIB.  This is the missing coverage for the IRXEXEC
*            VLIST-wrapper path (only tinitvl / ttermvl existed).
*
*  WP-VLIST-WPOOL / TSK-279.  This test drives the production IRXINIT
*  -> IRXEXEC -> IRXTERM VLIST wrappers end-to-end (the path httprexx
*  uses via LINK), which had NO MVS coverage — only tinitvl / ttermvl
*  existed.  Building it surfaced two stacked defects:
*
*    Bug A (prerequisite): the standalone IRXINIT load module reaches
*    INITENVB through irx_init_dispatch(), which built only the IBM
*    control blocks and never attached the interpreter Work Block.
*    IRXEXEC then failed at irx_lstr_init() with RC=20 before the pool
*    was ever stressed.  Fixed by irx_init_finish() (src/irx#init.c);
*    the wkblk now lives in envblock_workblok_ext (+0x18).
*
*    Bug B (the ticket): with the wkblk attached the exec runs, and a
*    real exec's recursive-descent compile + VM + Lstr chain nests far
*    past the 8 KB WPOOL (measured ~16.2 KB high-water for the modest
*    nested expression below).  The PDPPRLG bump-allocator runs past
*    WPOOL, corrupts adjacent GETMAIN storage and ABENDs S0C4.  Fixed
*    by enlarging WPOOL to 64 KB in all four wrappers.
*
*  RED (either bug present): RC=20 or ABEND S0C4.  GREEN (both fixed):
*  the exec runs to REXX 'exit 42' -> IRXEXEC R15 = 42 -> step RC=0.
*
*  Step 1: IRXINIT INITENVB via SC28-1883-0 §14 VLIST (7 slots) —
*          same shape as TINITVL/TTERMVL; yields the ENVBLOCK.
*
*  Step 2: IRXEXEC via the z/OS 10-slot VLIST form:
*            P1  EXECBLK   (NULL — source comes from INSTBLK)
*            P2  ARGTABLE  (NULL — no args)
*            P3  FLAGS     (0 — wrapper takes NOFLAG; C ignores flags)
*            P4  INSTBLK   (-> in-storage source, built below)
*            P5  reserved  (NULL)
*            P6  EVALBLOCK (NULL — result ignored)
*            P7  WORKAREA  (NULL)
*            P8  USERFIELD (NULL)
*            P9  ENVBLOCK  (the env from step 1)
*            P10 REXXRC    (out; VL marker on this slot)
*          R0 in = ENVBLOCK hint; R15 out = REXX exit value (42).
*
*  Step 3: IRXTERM (R0 = ENVBLOCK) to free the environment.
*
*  WTO output (50-char fixed layout — keeps the DC inside the IFOX00
*  col-16-to-col-71 operand window):
*    TEXECVL OK   ENV=xxxxxxxx RC=xxxxxxxx TRC=xxxxxxxx
*    TEXECVL FAIL ENV=xxxxxxxx RC=xxxxxxxx TRC=xxxxxxxx
*  (RC = IRXEXEC R15 = REXX exit value; TRC = IRXTERM RC.)
*
*  Return code (R15 to JCL / step COND CODE):
*     0  IRXINIT ok, IRXEXEC R15 = 42 (exec ran to 'exit 42')
*     8  LOAD EP= failed for IRXINIT / IRXEXEC / IRXTERM
*    20  IRXINIT non-zero, eye-catcher mismatch, or IRXEXEC R15 != 42
*    (a WPOOL overflow ABENDs S0C4 during step 2 and never returns)
*
*  Ref: SC28-1883-0 §14; z/OS REXX Reference (IRXEXEC 10-slot form)
*  Ref: WP-VLIST-WPOOL / TSK-279 / consumer: mvslovers/httprexx
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
TEXECVL  CSECT
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
*  --- evaluate: success iff the exec ran to 'exit 42' ------------
         L     R3,WRCE
         C     R3,=F'42'
         BNE   FAILED              IRXEXEC did not return the exit code
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
         WTO   'TEXECVL FAIL: LOAD EP=IRXINIT failed (check STEPLIB)'
         LA    R3,8
         B     EPILOG
*
NOIRXEX  EQU   *
         WTO   'TEXECVL FAIL: LOAD EP=IRXEXEC failed (check STEPLIB)'
         LA    R3,8
         B     EPILOG
*
NOIRXTM  EQU   *
         WTO   'TEXECVL FAIL: LOAD EP=IRXTERM failed (check STEPLIB)'
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
         DC    A(L03),A(L03E-L03)
         DC    A(L04),A(L04E-L04)
         DC    A(L05),A(L05E-L05)
         DC    A(L06),A(L06E-L06)
         DC    A(L07),A(L07E-L07)
IBENTE   EQU   *
*
*  --- REXX source lines ----------------------------------------
*  The deep nested-parenthesis expression (L04) forces the bytecode
*  recursive-descent compiler (bc_exp0..bc_exp8, ~9 frames/level) far
*  past the 8 KB WPOOL; 'exit 42' proves clean execution once fixed.
L01      DC    C'acc = 0'
L01E     EQU   *
L02      DC    C'do i = 1 to 3'
L02E     EQU   *
L03      DC    C'n = i + 1'
L03E     EQU   *
L04      DC    C'x = (1+(2+(3+(4+(5+(6+(7+(8+(9+(1+(2+(3+(4+'
         DC    C'(n+i))))))))))))))'
L04E     EQU   *
L05      DC    C'acc = acc + x'
L05E     EQU   *
L06      DC    C'end'
L06E     EQU   *
L07      DC    C'exit 42'
L07E     EQU   *
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
*    TEXECVL OK   ENV=XXXXXXXX RC=XXXXXXXX TRC=XXXXXXXX
*                     ^17          ^29          ^42
OKMSG    DC    CL50'TEXECVL OK   ENV=XXXXXXXX RC=XXXXXXXX TRC=XXXXXXXX'
FAILMSG  DC    CL50'TEXECVL FAIL ENV=XXXXXXXX RC=XXXXXXXX TRC=XXXXXXXX'
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
         END   TEXECVL
