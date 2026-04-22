         TITLE 'IRXTMPW - REXX/370 TMP Wrapper'
*
*  IRXTMPW - If IRXANCHR is installed, LOAD it so its CDE lives in
*            the Step-TCB JPQ (survives XCTL, found by command
*            subtasks via JPA lookup). Otherwise, skip the LOAD
*            silently. Either way, XCTL to IKJEFT01.
*
*  Must be link-edited AC(1),REUS into an APF-authorized library
*  (e.g. SYS1.LINKLIB).
*
*  On entry: R1 = PARM pointer, R13 = caller SA, R14 = return.
*  On exit:  no return - XCTLs to IKJEFT01 (which ends via SVC 3).
*
R0       EQU   0
R1       EQU   1
R2       EQU   2
R8       EQU   8
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
*
IRXTMPW  CSECT
*
         STM   R14,R12,12(R13)
         BALR  R12,0
         USING *,R12
*
         ST    R1,PARMSAVE
*
         LA    R2,SAVEAREA
         ST    R13,4(,R2)
         ST    R2,8(,R13)
         LR    R13,R2
*
*  --- BLDL: check if IRXANCHR is in LNKLST/JOBLIB/STEPLIB ---
*
         MVC   BLDLCNT,=H'1'        one entry
         MVC   BLDLLEN,=H'50'       length per entry
         MVC   BLDLNAM,=CL8'IRXANCHR'
         BLDL  0,BLDLLST            DCB=0 -> JOBLIB/LNKLST
         LTR   R15,R15
         BNZ   SKIPLOAD             not found - skip quietly
*
*  --- IRXANCHR found - LOAD it ---
*
         LOAD  EP=IRXANCHR,ERRET=SKIPLOAD
         ST    R0,ANCHADR
*
*  Announce the anchor address to terminal
*
*         LA    R1,ANCHADR
*         LA    R8,ANCHHEX
*         BAL   R14,HEX4
*         TPUT  LOADMSG,L'LOADMSG
*
*  --- XCTL to IKJEFT01 (always, even if no IRXANCHR) ---
*
SKIPLOAD DS    0H
         L     R13,SAVEAREA+4
         L     R1,PARMSAVE
         L     R14,12(,R13)
         XCTL  EP=IKJEFT01
*
*---------------------------------------------------------------
HEX4     LA    R15,4
HEXL     IC    R0,0(,R1)
         LR    R2,R0
         SRL   R2,4
         N     R2,=F'15'
         IC    R2,HEXTAB(R2)
         STC   R2,0(,R8)
         LR    R2,R0
         N     R2,=F'15'
         IC    R2,HEXTAB(R2)
         STC   R2,1(,R8)
         LA    R1,1(,R1)
         LA    R8,2(,R8)
         BCT   R15,HEXL
         BR    R14
*
*---------------------------------------------------------------
*  BLDL list - must be fullword aligned
*---------------------------------------------------------------
         DS    0F
BLDLLST  DS    0CL54            full BLDL list area
BLDLCNT  DS    H                FF = # entries
BLDLLEN  DS    H                LL = length per entry
BLDLNAM  DS    CL8              member name
BLDLRET  DS    CL42             return data (TTR, K, Z, C, user)
*
*---------------------------------------------------------------
LOADMSG  DS    0CL32
         DC    C' IRXANCHR LOADED AT '
ANCHHEX  DC    CL8'00000000'
         DC    CL3' '
*
HEXTAB   DC    C'0123456789ABCDEF'
*
ANCHADR  DS    F
PARMSAVE DS    F
*
SAVEAREA DS    18F
*
         LTORG
         END   IRXTMPW
