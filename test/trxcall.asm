         TITLE 'TRXCALL - call IRXTERM with R0 = ENVBLOCK'
*  ------------------------------------------------------------------
*  TRXCALL: plain OS-linkage shim, the same shape as httprexx
*  asm/htrxterm.asm (HRXCALL).
*
*    trc = trx_call(ep, env);      ep=0(R1), env=4(R1)
*
*  IRXTERM takes the ENVBLOCK directly in R0 (no parameter list) and
*  returns RC in R15 -- the documented TSO/E register interface.  A
*  LINK SVC cannot set R0, hence this shim.
*
*  WARNING (root cause of mvslovers/rexx370 "IRXTERM C-host crash",
*  see docs/irxterm-c-host-crash.md): RS-format instructions (LM,
*  STM, ...) must write the operand as D(B) -- as370 silently
*  assembles the RX-style D(,B) form with BASE=0, turning the
*  register restore into a load from PSA low core.  The epilog below
*  therefore uses 20(R13), NOT 20(,R13).
*  ------------------------------------------------------------------
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
TRXCALL  CSECT
         STM   R14,R12,12(R13)    save caller regs
         BALR  R12,0
         USING *,R12
         L     R2,0(,R1)          R2 = ep  (routine entry)
         L     R3,4(,R1)          R3 = env (ENVBLOCK)
         L     R0,=A(WALEN)
         GETMAIN RU,LV=(0)
         LR    R9,R1              R9 = workarea (for FREEMAIN)
         ST    R13,4(,R1)         back chain
         ST    R1,8(,R13)         forward chain
         LR    R13,R1
         USING WAREA,R13
         LR    R15,R2             R15 = ep
         LR    R0,R3              R0  = env
         BALR  R14,R15            call IRXTERM (R0 = env)
         LR    R4,R15             R4  = RC (preserve)
         L     R13,WDPREV         R13 = caller save area
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(9)
         LR    R15,R4             R15 = return code
         L     R14,12(,R13)       restore return address
         LM    R0,R12,20(R13)     restore R0-R12 (D(B) - see above)
         BR    R14
*
         LTORG
*
WAREA    DSECT
WDFLAGS  DS    F                  +0  reserved
WDPREV   DS    F                  +4  back chain
WDNEXT   DS    F                  +8  forward chain
         DS    15F                +12..+71 save slots
WALEN    EQU   *-WAREA
         END   TRXCALL
