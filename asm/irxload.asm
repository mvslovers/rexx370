         TITLE 'IRXLOAD - REXX/370 IRXLOAD Entry Point Wrapper'
*
*  IRXLOAD - HLASM entry-point wrapper for the IRXLOAD Programming
*            Service (SC28-1883-0 §14).
*
*  Parses the caller VLIST, validates the high-bit endmarker on the
*  last parameter, validates the CL8 function code, and delegates to
*  the C-core dispatcher irx_load_dispatch (asm() alias IRXLDISP,
*  CON-4) which routes on the function code to one of:
*
*    LOAD  -> irx_load_load()   (reads REXX source, builds INSTBLK)
*    FREE  -> irx_load_free()   (releases INSTBLK and source pool)
*
*  Calling convention (per SC28-1883-0 §14 IRXLOAD):
*
*    CALL IRXLOAD,(FCODE,EXECBLK,INSTBLK,ENVBLK,RETCODE),VL
*
*  Each VLIST slot is a 4-byte address pointing at the parameter
*  value. The address of the LAST slot has its high-order bit set
*  to mark the end of the variable-length list.
*
*  P1  CL8    function code: 'LOAD    ' / 'FREE    '
*  P2  A      EXECBLK pointer (LOAD: required; FREE: ignored)
*  P3  A      INSTBLK pointer (LOAD: output; FREE: input to free)
*  P4  A      ENVBLOCK pointer (NULL -> default subpool 0)
*  P5  F      return code (output)
*
*  R15 (out) return code: 0=OK, 4=NOMEM, 8=NOTFOUND, 20=ERROR
*
*  Mirrors asm/irxinit.asm structure and PDP-DSA/WPOOL shape.
*  See irxinit.asm for the rationale behind the bootstrap design
*  (no @@CRT0 dependency, WPOOL as bump-allocator pool).
*
*  Ref: SC28-1883-0 §14 (IRXLOAD Programming Service)
*  Ref: CON-4 (asm() aliases)
*  Ref: WP-CPS-07 / TSK-219 / GitHub mvslovers/rexx370#116
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
IRXLOAD  CSECT
*
*  --- standard MVS entry linkage ---
         STM   R14,R12,12(R13)     save caller R14-R12 in caller SA
         BALR  R12,0
         USING *,R12
*
         LR    R11,R1              R11 = caller VLIST address
*
*  Defensive NULL check: a caller passing R1=0 (no parm list at all)
*  must not provoke an S0C5 in PARSELP and must not leak the workarea
*  we are about to GETMAIN.
         LTR   R11,R11             VLIST pointer NULL?
         BZ    NULLPLST            yes -> early exit, no GETMAIN
*
*  --- allocate dynamic workarea (PDP-DSA + locals + WPOOL) ---------
         L     R0,=A(WALEN)
         GETMAIN RU,LV=(0)
         LR    R8,R1               R8 = workarea ptr (saved for FREE)
*
*  Chain DSAs: caller SA <-> our DSA.
         ST    R13,4(,R1)          our DSA back-chain = caller SA
         ST    R1,8(,R13)          caller forward     = our DSA
         LR    R13,R1
         USING WAREA,R13
*
*  --- initialize PDP-DSA fields ---
         XC    WDFLAGS(4),WDFLAGS  DSAFLAGS = 0
         XC    WDLWA(4),WDLWA      DSALWA   = 0
         LA    R0,WPOOL
         ST    R0,WDNAB            DSANAB   = WPOOL
*
*  Zero WPARMS / WCPLIST so "slot never filled" is distinguishable.
         XC    WPARMS(20),WPARMS    5F = 20 bytes
         XC    WCPLIST(20),WCPLIST  5F = 20 bytes
*
*  --- parse VLIST: 5 entries, high-bit endmarker on slot 5 ----------
         LA    R2,5                expected slot count (countdown)
         LR    R3,R11              R3 = current caller VLIST entry
         LA    R4,WPARMS           R4 = our local parsed-addr array
*
PARSELP  L     R6,0(,R3)           raw VLIST entry (addr | maybe VL)
         LR    R7,R6
         N     R7,=X'7FFFFFFF'     clear VL bit -> bare address
         ST    R7,0(,R4)
         LTR   R6,R6               VL bit (sign bit) set?
         BM    PARSEVL             yes -> end of list
         LA    R3,4(,R3)
         LA    R4,4(,R4)
         BCT   R2,PARSELP
*
*  All 5 slots walked without a VL marker — malformed list.
         LA    R15,20
         B     ERREARLY
*
PARSEVL  EQU   *
*  VL found; R2 = remaining count (must be 1 for slot 5).
         CH    R2,=H'1'
         BE    FCCHK
*  VL on wrong slot — short list; no usable P5 / retval slot.
         LA    R15,20
         B     ERREARLY
*
FCCHK    EQU   *
*  --- validate function code ---
         L     R2,WPARMS+0         R2 = addr of P1 (CL8 funccode)
         CLC   0(8,R2),=CL8'LOAD'
         BE    BUILDC
         CLC   0(8,R2),=CL8'FREE'
         BE    BUILDC
*
*  Unknown funccode. WPARMS+16 is valid (VL on slot 5), so write P5.
         LA    R15,20
         B     SETRSN
*
BUILDC   EQU   *
*  --- build C-call plist for IRXLDISP ----------------------------
*
*    irx_load_dispatch(funccode, execblk, instblk_p, envblk, retval)
*
*  P1 funccode: pass address of CL8 string (WPARMS+0 content).
         L     R2,WPARMS+0         R2 = addr of funccode CL8
         ST    R2,WCPLIST+0
*
*  P2 execblk: deref once to get the EXECBLK pointer.
         L     R2,WPARMS+4         R2 = addr of EXECBLK ptr slot
         L     R3,0(,R2)           R3 = EXECBLK ptr (may be 0 for FREE)
         ST    R3,WCPLIST+4
*
*  P3 instblk_p: pass address of P3 slot so C can write *instblk_p.
         L     R2,WPARMS+8         R2 = addr of INSTBLK ptr slot
         ST    R2,WCPLIST+8
*
*  P4 envblk: deref once to get the ENVBLOCK pointer.
         L     R2,WPARMS+12        R2 = addr of ENVBLOCK ptr slot
         L     R3,0(,R2)           R3 = ENVBLOCK ptr (may be 0)
         ST    R3,WCPLIST+12
*
*  P5 retval: pass address of caller's return-code slot.
         L     R2,WPARMS+16        R2 = addr of retval int slot
         ST    R2,WCPLIST+16
*
*  --- call irx_load_dispatch ---
         LA    R1,WCPLIST
         L     R15,=V(IRXLDISP)
         BALR  R14,R15
*
*  R15 = RC from C; C has already written *retval through WCPLIST+16.
         LR    R3,R15              R3 = RC (preserved across teardown)
         B     EPILOG
*
SETRSN   EQU   *
*  --- error path with P5 write (bad funccode, VL on slot 5 valid) --
         LR    R3,R15              R3 = RC (20)
         L     R7,WPARMS+16        caller's retval slot address
         MVC   0(4,R7),=F'20'      *retval = 20
         B     EPILOG
*
ERREARLY EQU   *
*  --- error path without P5 write (VLIST malformed) -----------------
         LR    R3,R15              R3 = RC (20)
*  Fall through to EPILOG.
*
EPILOG   EQU   *
*  R3 = output RC.
*  Restore R13, FREEMAIN workarea, set R15, restore caller registers.
*
         L     R13,WDPREV          R13 = caller SA
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(8)
*
         LR    R15,R3              R15 = output RC
         L     R14,12(,R13)
         LM    R0,R12,20(R13)      restore R0-R12 from caller SA
         BR    R14
*
NULLPLST DS    0H
*  Caller passed R1=0.  R13 still points at caller SA (no GETMAIN).
         LA    R15,20
         L     R14,12(,R13)
         LM    R0,R12,20(R13)
         BR    R14
*
         LTORG
*
*  --- workarea DSECT (PDP-DSA shape, mirrors irxinit.asm) ----------
*
WAREA    DSECT
WDFLAGS  DS    F                   +0  DSAFLAGS (must be 0)
WDPREV   DS    F                   +4  DSAPREV  (back chain)
WDNEXT   DS    F                   +8  DSANEXT  (forward chain)
WDR14    DS    F                   +12 caller R14
WDR15    DS    F                   +16 caller R15
WDR0     DS    F                   +20 caller R0
WDR1     DS    F                   +24 caller R1
WDR2     DS    F                   +28 caller R2
WDR3     DS    F                   +32 caller R3
WDR4     DS    F                   +36 caller R4
WDR5     DS    F                   +40 caller R5
WDR6     DS    F                   +44 caller R6
WDR7     DS    F                   +48 caller R7
WDR8     DS    F                   +52 caller R8
WDR9     DS    F                   +56 caller R9
WDR10    DS    F                   +60 caller R10
WDR11    DS    F                   +64 caller R11
WDR12    DS    F                   +68 caller R12
WDLWA    DS    F                   +72 DSALWA  (must be 0)
WDNAB    DS    F                   +76 DSANAB  (must point to WPOOL)
*  Wrapper-local storage.
WPARMS   DS    5F                  bare addresses from 5-slot VLIST
WCPLIST  DS    5F                  parameter list for IRXLDISP call
*  Stack pool for nested c2asm370 PDPPRLG frames (see irxinit.asm).
WPOOL    DS    2048F               8 KB scratchpad
WALEN    EQU   *-WAREA
*
         END   IRXLOAD
