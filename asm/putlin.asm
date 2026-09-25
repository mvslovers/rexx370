         TITLE 'PUTLCALL - Call the TSO PUTLINE service (IKJPUTL)'
*
*  PUTLCALL - Hand a prepared IOPL to PUTLINE.
*
*  The TSO half of the IRXINOUT replaceable routine (WP-33-TSO).
*  Called from tso_put_line() in src/irx#tsio.c:
*
*      int putl_call(void *iopl, void *savearea) asm("PUTLCALL");
*
*  Entry   R1 -> list of argument VALUES (c2asm370 convention):
*                  0(R1) = IOPL address (UPT, ECT, ECB, PTPB)
*                  4(R1) = 72-byte save area for IKJPUTL to use
*          R13 -> caller save area,  R14 -> return
*  Exit    R15 = PUTLINE return code
*
*  ---- Why PUTLINE and not TPUT ----
*
*  TPUT (SVC 93) returns without doing anything when the address
*  space has no TSB -- IKT0009C branches straight to its exit when
*  ASCBTSB is 0 -- and the batch TMP has none.  The background TMP
*  instead STACKs SYSTSIN/SYSTSPRT, and PUTLINE writes through that
*  stack.  So PUTLINE reaches the terminal in the foreground and
*  SYSTSPRT in the background; TPUT reaches only the first.
*
*  ---- Why the caller supplies the save area ----
*
*  IKJPUTL saves our registers into the area R13 points at.  Taking
*  it from the caller keeps this routine RENT without a GETMAIN; the
*  C side has it in its own work area next to the IOPL.
*
*  The call sequence is the one the PUTLINE macro generates without
*  ENTRY=: BALR to CVTPUTL (CVT+444) when its high bit says IKJPUTL
*  is loaded, LINK EP=IKJPUTL otherwise.
*
*  NOTE for as370/IFOX00: RS-format LM/STM must be written D(B).
*
*  Ref: SC28-1883-0 Chapter 16 (Replaceable Routines); WP-33-TSO
*  (c) 2026 mvslovers - REXX/370 Project
*
         PRINT NOGEN
R1       EQU   1
R2       EQU   2
R3       EQU   3
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
FLCCVT   EQU   16                 PSA -> CVT
CVTPUTL  EQU   444                CVT -> IKJPUTL, X'80' = loaded
SAR15    EQU   16                 R15 slot in a save area
SABACK   EQU   4                  back chain in a save area
SAFWD    EQU   8                  forward chain in a save area
         PRINT GEN
*
PUTLCALL CSECT
         STM   R14,R12,12(R13)
         BALR  R12,0
         USING *,R12
*
         L     R2,0(,R1)          IOPL
         L     R3,4(,R1)          save area for IKJPUTL
         ST    R13,SABACK(,R3)    chain the new area to ours
         ST    R3,SAFWD(,R13)
         LR    R13,R3
         LR    R1,R2              R1 -> IOPL, as PUTLINE expects
*
         L     R15,FLCCVT
         TM    CVTPUTL(R15),X'80' IKJPUTL resident?
         BNO   LINKIT
         L     R15,CVTPUTL(,R15)
         BALR  R14,R15
         B     BACK
LINKIT   DS    0H
         LINK  EP=IKJPUTL
*
BACK     DS    0H
         L     R13,SABACK(,R13)   back to the caller's save area
         ST    R15,SAR15(,R13)    PUTLINE rc into the R15 slot
         LM    R14,R12,12(R13)
         BR    R14
*
         LTORG
*
         END   PUTLCALL
