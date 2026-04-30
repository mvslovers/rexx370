         TITLE 'TISTSO - Standalone test caller for ISTSO'
*
*  TISTSO - Calls ISTSO (EXTRACT-based TSO detector) and reports
*           the result to the operator console via WTO.
*
*  Return code (R15 to JCL):
*     0  ISTSO returned 0 (batch - no TSO environment)
*     1  ISTSO returned 1 (TSO environment detected)
*
*  Expected JESLOG output:
*     Batch : TISTSO: NOT TSO (batch)
*     TSO   : TISTSO: IS TSO
*
*  Ref: WP-I1c.6 / GitHub mvslovers/rexx370#93
*
*  (c) 2026 mvslovers - REXX/370 Project
*
         PRINT NOGEN
R0       EQU   0
R1       EQU   1
R3       EQU   3
R8       EQU   8
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
         PRINT GEN
*
TISTSO   CSECT
         STM   R14,R12,12(R13)
         BALR  R12,0
         USING *,R12
*
* --- allocate dynamic workarea (RENT) ---
         L     R0,=A(WALEN)
         GETMAIN RU,LV=(0)
         LR    R8,R1
         ST    R13,4(,R1)
         ST    R1,8(,R13)
         LR    R13,R1
         USING WAREA,R13
*
* --- call ISTSO ---
         L     R15,=V(ISTSO)
         BALR  R14,R15
         LR    R3,R15                 save result (0=batch, 1=TSO)
*
* --- emit WTO based on result ---
         LTR   R3,R3
         BNZ   WTOYES
*
*  --- not TSO (batch) ---
         MVC   WWTOSK(WTOSKLEN),WTOSKEL
         MVC   WWTOSK+4(MSGLEN),NOMSG
         WTO   MF=(E,WWTOSK)
         B     EPILOG
*
*  --- TSO detected ---
WTOYES   MVC   WWTOSK(WTOSKLEN),WTOSKEL
         MVC   WWTOSK+4(MSGLEN),YESMSG
         WTO   MF=(E,WWTOSK)
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
         LTORG
*
* --- WTO parameter list skeleton (hand-built for IFOX00 col-71) ---
*  Layout: AL2(total len) AL2(MCS flags) 23C' '
WTOSKEL  DS    0H
         DC    AL2(WTOEND-WTOSKEL)
         DC    AL2(0)
         DC    23C' '
WTOEND   EQU   *
WTOSKLEN EQU   *-WTOSKEL
*
* --- 23-char message templates (text portion of WPL) ---
*    01234567890123456789012
*    0         1         2
YESMSG   DC    CL23'TISTSO: IS TSO'
NOMSG    DC    CL23'TISTSO: NOT TSO (batch)'
MSGLEN   EQU   23
*
* --- workarea DSECT ---
WAREA    DSECT
WDFLAGS  DS    F                 +0
WDPREV   DS    F                 +4  back chain
WDNEXT   DS    F                 +8  forward chain
         DS    15F              +12..+71 R14-R12 save slots
WWTOSK   DS    CL(WTOSKLEN)      WTO working area
WALEN    EQU   *-WAREA
*
         END   TISTSO
