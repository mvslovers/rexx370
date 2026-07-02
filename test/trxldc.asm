         TITLE 'TRXLDC - __load-ep BALR caller for IRXINIT (discriminator)'
*  ------------------------------------------------------------------
*  trx_callv(ep, vlist) -- call an entry point obtained from __load
*  with R1 = VLIST and R0 = 0 (the IRXINIT SC28-1883 convention, the
*  exact register setup ttermvl uses for IRXINIT).  Lets TREXXVL drive
*  IRXINIT via __load+BALR instead of __linkds (LINK), isolating the
*  LINK SVC from the crash:
*    runs  -> the __linkds/LINK path is the culprit
*    crash -> IRXINIT invocation is innocent; look at __load IRXTERM /
*             trx_call / the C-runtime context
*
*  The LOAD-macro form is deliberately NOT used here: on MVS 3.8j the
*  as370 LOAD macro returns ep=0 after a prior LINK, so ep must come
*  from __load (same loader httprexx/rexx370 use).
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
*  int trx_callv(void *ep, void *vlist)
*
TRXCALLV CSECT
         STM   R14,R12,12(R13)    save caller regs
         BALR  R12,0
         USING *,R12
         L     R2,0(,R1)          R2 = ep     (routine entry, __load'd)
         L     R3,4(,R1)          R3 = vlist  (IRXINIT VLIST)
         L     R0,=A(WALEN)
         GETMAIN RU,LV=(0)
         LR    R9,R1              R9 = workarea (for FREEMAIN)
         ST    R13,4(,R1)         back chain
         ST    R1,8(,R13)         forward chain
         LR    R13,R1
         USING WAREA,R13
         LR    R15,R2             R15 = ep
         LR    R1,R3              R1  = vlist  (IRXINIT parameter list)
         SR    R0,R0              R0  = 0      (no previous-env hint)
         BALR  R14,R15            call IRXINIT
         LR    R4,R15             R4  = RC (preserve)
         L     R13,WDPREV         R13 = caller save area
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(9)
         LR    R15,R4             R15 = return code
         L     R14,12(,R13)       restore return address
*  RS format needs D(B): as370 assembles the RX-style D(,B) form
*  with BASE=0 (load from PSA low core) -- see test/trxcall.asm.
         LM    R0,R12,20(R13)     restore R0-R12
         BR    R14
         LTORG
WAREA    DSECT
WDFLAGS  DS    F                  +0  reserved
WDPREV   DS    F                  +4  back chain
WDNEXT   DS    F                  +8  forward chain
         DS    15F                +12..+71 save slots
WALEN    EQU   *-WAREA
         END   TRXCALLV
