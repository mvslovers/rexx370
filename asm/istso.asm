         TITLE 'TSOENV - TSO Environment Detection'
*
*  ISTSO - Determine whether the current task is running under TSO
*          (foreground interactive or IKJEFTSR background).
*
*  Entry:  Standard MVS linkage (R14=ret, R13=caller SA)
*  Return: R15 = 0  batch (no TSO environment)
*          R15 = 1  TSO environment detected
*
*  RENT/REUS: dynamic workarea allocated via GETMAIN RU.
*  WEXTLST (EXTRACT MF=L in DSECT) and WEXTANS (2F answer area)
*  both reside in the workarea.  MF=(E,WEXTLST) fills the parm
*  list at runtime and issues SVC 9 -- no MVC of a static template
*  is needed (pattern from crent370 @@CRT0 PPASETUP).
*
*  IMPORTANT: R15 is NOT tested after EXTRACT. The SVC does not
*  set R15 reliably on MVS 3.8j (live-verified 2026-04-28 MVS-CE).
*
*  IMPORTANT: EXTRACT clobbers R0. The epilog restores R0 from the
*  caller save area (offset +20) after FREEMAIN.
*
*  Ref: WP-I1c.6 / GitHub mvslovers/rexx370#93
*
*  (c) 2026 mvslovers - REXX/370 Project
*
         PRINT NOGEN
R0       EQU   0
R1       EQU   1
R2       EQU   2
R3       EQU   3
R4       EQU   4
R9       EQU   9
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
         PRINT GEN
*
ISTSO    CSECT
         STM   R14,R12,12(R13)
         BALR  R12,0
         USING *,R12
*
* --- acquire dynamic workarea (RENT) ---
         L     R0,=A(WALEN)
         GETMAIN RU,LV=(0)
         LR    R9,R1
         ST    R13,4(,R1)
         ST    R1,8(,R13)
         LR    R13,R1
         USING WAREA,R13
*
* --- EXTRACT: MF=E fills parm list + issues SVC 9 ---
         EXTRACT WEXTANS,FIELDS=(TSO,PSB),MF=(E,WEXTLST)
*        NOTE: R15 is NOT tested - SVC 9 does not set it reliably
*
* --- evaluate answer area ---
         LM    R2,R3,WEXTANS         R2->TSO byte  R3->PSB (or 0)
         TM    0(R2),X'80'           TSO foreground bit?
         BO    YESTSO
         LTR   R3,R3                 non-zero PSB = background TSO?
         BNZ   YESTSO
         LA    R4,0                  batch: return 0
         B     EXIT
YESTSO   LA    R4,1                  TSO: return 1
EXIT     DS    0H
         L     R13,WDPREV
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(9)
         LR    R15,R4
         L     R14,12(,R13)
         L     R0,20(,R13)           restore R0 (EXTRACT clobbers it)
         LM    R1,R12,24(R13)
         BR    R14
*
         LTORG
*
* --- workarea DSECT ---
WAREA    DSECT
WDFLAGS  DS    F                  +0  reserved
WDPREV   DS    F                  +4  back chain
WDNEXT   DS    F                  +8  forward chain
         DS    15F               +12..+71 R14-R12 save slots
WEXTANS  DS    2F                 EXTRACT answer area: TSO+PSB ptrs
WEXTLST  EXTRACT MF=L             EXTRACT parm list (sized by macro)
WALEN    EQU   *-WAREA
*
         END   ISTSO
