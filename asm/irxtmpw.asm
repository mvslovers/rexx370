         TITLE 'IRXTMPW - REXX/370 TMP Wrapper'
*
*  IRXTMPW - Try loading IRXANCHR so its CDE lives in
*            the Step-TCB JPQ (survives XCTL, found by command
*            subtasks via JPA lookup). XCTL to IKJEFT01.
*
*  Must be link-edited AC(1),RENT and REUS into an APF-authorized 
*  library (e.g. SYS2.LINKLIB).
*
*  On entry: R1 = PARM pointer, R13 = caller SA, R14 = return.
*  On exit:  no return - XCTLs to IKJEFT01 (which ends via SVC 3).
*
         PRINT NOGEN
R0       EQU   0
R1       EQU   1
R5       EQU   5
R6       EQU   6
R7       EQU   7
R8       EQU   8
R12      EQU   12
R14      EQU   14
R15      EQU   15
         PRINT GEN
*
IRXTMPW  CSECT
         BALR  R12,0
         USING *,R12
*
* --- save registers that IKJEFT01 expects to be preserved ---
         LR    R5,R0
         LR    R6,R1
         LR    R7,R14
         LR    R8,R15
*
* --- try loading IRXANCHR ---
         LOAD  EP=IRXANCHR,ERRET=NOANCH
*
* --- restore registers for IKJEFT01 ---
NOANCH   DS    0H
         LOAD  EP=IRXINIT,ERRET=NOINIT
NOINIT   DS    0H
         LR    R0,R5
         LR    R1,R6
         LR    R14,R7
         LR    R15,R8
*
* --- give control to IKJEFT01 ---
         XCTL  EP=IKJEFT01
*
         LTORG
         END   IRXTMPW
