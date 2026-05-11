         TITLE 'IRXJCL - REXX/370 IRXJCL Batch Entry Point Wrapper'
*
*  IRXJCL - HLASM entry-point wrapper for the IRXJCL batch entry
*            point (WP-CPS-08 / TSK-220).
*
*  Decodes the caller VLIST (single slot, VL-bit on P1), saves
*  the R0 ENVBLOCK hint, and delegates to the C-core dispatcher
*  irx_jcl_dispatch (asm() alias IRXJDISP, CON-4).
*
*  IRXJCL is the program MVS loads when a JCL step specifies:
*
*    //STEP1  EXEC PGM=IRXJCL,PARM='MYMEMBER arg...'
*
*  It is also callable as a programming service:
*
*    CALL IRXJCL,(PARMPTR),VL
*
*  Calling convention (single-slot VLIST):
*
*    P1   A   address of PARM buffer (halfword-length + data)
*    R0       optional ENVBLOCK pointer (eyecatcher-validated)
*
*  PARM buffer layout (standard z/OS EXEC PARM= format):
*
*    +0   H   total data length (big-endian, may be 0 = no PARM)
*    +2   CL* data: member-name [ space arg-string ]
*
*  R0 (out) ENVBLOCK used (entry R0 if no P9 equivalent)
*  R15 (out) return code: 0=OK, 20=ERROR, 24=BADPARM, 28=NOENV
*
*  Architecture decisions (WP-CPS-08):
*    (a) Single-slot VLIST: VL must be on P1 (R2=1 at PARSEVL).
*        Any shorter list (R2>1 when VL seen) is malformed BADPARM.
*    (b) R0 ENVBLOCK hint: saved to R9 before GETMAIN clobbers R0,
*        then stored at WDR0 and forwarded as irx_jcl_dispatch P2.
*    (c) PARM decode: done in C (big-endian halfword read portable
*        across host and MVS without special asm handling).
*    (d) Null slot-address guard: WPARMS+0 checked after VLIST parse;
*        a zero slot address returns BADPARM instead of faulting.
*    (e) No P10 return-code slot (differs from IRXEXEC). R15 alone
*        carries the final RC.
*    (f) LA cannot load values > 4095; IRXJCL RCs (20/24/28) are
*        all <= 4095, so LA is sufficient here.
*
*  Mirrors asm/irxload.asm structure and PDP-DSA/WPOOL shape.
*  See irxload.asm for rationale behind the bootstrap design.
*
*  Refs: z/OS REXX Reference — IRXJCL Programming Service
*        SC28-1883-0, Chapter 14 — V1 baseline
*        WP-CPS-08 / TSK-220 / GitHub mvslovers/rexx370#126
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
IRXJCL   CSECT
*
*  --- standard MVS entry linkage ---
         STM   R14,R12,12(R13)     save caller R14-R12 in caller SA
         BALR  R12,0
         USING *,R12
*
         LR    R11,R1              R11 = caller VLIST address
*  Save caller R0 (optional ENVBLOCK hint) before GETMAIN clobbers R0.
         LR    R9,R0               R9  = caller R0
*
*  Defensive NULL check: no parm list at all.
         LTR   R11,R11
         BZ    NULLPLST
*
*  --- allocate dynamic workarea (PDP-DSA + locals + WPOOL) ---------
         L     R0,=A(WALEN)
         GETMAIN RU,LV=(0)
         LR    R8,R1               R8  = workarea ptr (saved for FREE)
*
*  Chain DSAs.
         ST    R13,4(,R1)          our DSA back-chain = caller SA
         ST    R1,8(,R13)          caller forward     = our DSA
         LR    R13,R1
         USING WAREA,R13
*
*  Save caller R0 in workarea now that R13 is our workarea.
         ST    R9,WDR0
*
*  --- initialize PDP-DSA fields ---
         XC    WDFLAGS(4),WDFLAGS
         XC    WDLWA(4),WDLWA
         LA    R0,WPOOL
         ST    R0,WDNAB
*
*  Zero parse tables before use.
         XC    WPARMS(4),WPARMS    1F
         XC    WCPLIST(12),WCPLIST 3F (2 dispatch args + sentinel)
*
*  --- parse VLIST: exactly 1 entry, VL-bit required on P1 ----------
*
*  R2 = countdown (1 = only P1 expected)
*  R3 = current VLIST entry pointer
*  R4 = current destination in WPARMS array
*
         LA    R2,1                countdown = 1
         LR    R3,R11              R3 = first VLIST entry
         LA    R4,WPARMS           R4 = WPARMS[0]
*
PARSELP  L     R6,0(,R3)           raw VLIST entry (addr | maybe VL)
         LR    R7,R6
         N     R7,=X'7FFFFFFF'     strip VL bit -> bare address
         ST    R7,0(,R4)           save in WPARMS[slot]
         LTR   R6,R6               VL bit (sign) set?
         BM    PARSEVL             yes -> end of list
         LA    R3,4(,R3)           advance VLIST pointer
         LA    R4,4(,R4)           advance WPARMS pointer
         BCT   R2,PARSELP
*
*  Slot walked without VL marker — malformed list.
         LA    R15,24              BADPARM = 24
         B     ERREARLY
*
PARSEVL  EQU   *
*  VL found.  R2 = remaining countdown value at the moment VL hit.
*    R2=1 -> VL on slot 1 (P1, the only valid slot)
*    R2>1 -> impossible here (countdown starts at 1), guarded by
*            BCT fall-through above; but check defensively.
         CH    R2,=H'1'
         BE    BUILDC              VL on P1 -> valid
         LA    R15,24              BADPARM
         B     ERREARLY
*
BUILDC   EQU   *
*  --- null slot-address guard (arch decision (d)) ------------------
*  WPARMS+0 holds the bare address of P1.  If it is zero the caller's
*  VLIST entry itself was zero — malformed; BADPARM.
*
         L     R6,WPARMS+0         R6 = bare P1 slot address
         LTR   R6,R6               zero slot address?
         BZ    NULLSLOT
*
*  --- build C-call plist for IRXJDISP ----------------------------
*
*  irx_jcl_dispatch(parm_buffer, envblock_r0)
*
*  P1 parm_buffer: WPARMS+0 already holds the PARM buffer address
*  (the bare slot value with VL bit stripped IS the buffer pointer
*  per the MVS LINK PARAM= convention; see asm/irxload.asm P1
*  funccode for the same single-fetch pattern).
         L     R2,WPARMS+0         R2 = PARM buffer addr (may be 0)
         ST    R2,WCPLIST+0
*
*  envblock_r0 (2nd arg): caller's original R0 (saved in WDR0).
         L     R2,WDR0             R2 = caller R0 (ENVBLOCK or 0)
         ST    R2,WCPLIST+4
*
*  --- call irx_jcl_dispatch ---
         LA    R1,WCPLIST
         L     R15,=V(IRXJDISP)
         BALR  R14,R15
*
*  R15 = RC from C dispatcher.
         LR    R3,R15              R3 = RC (preserved across teardown)
         B     EPILOG
*
NULLSLOT EQU   *
         LA    R15,24              BADPARM = 24
*        fall through to ERREARLY
*
ERREARLY EQU   *
*  VLIST malformed — workarea allocated; R3 = RC, no P10 to write.
         LR    R3,R15              R3 = RC
*        fall through to EPILOG
*
EPILOG   EQU   *
*  R0 on return: entry R0 (env hint passed in), per z/OS convention.
         L     R5,WDR0             R5 = entry R0
*
         L     R13,WDPREV          R13 = caller SA
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(8)
*
         LR    R15,R3              R15 = return code
         LR    R0,R5               R0  = envblock before LM clobbers R5
         L     R14,12(,R13)        caller R14
         LM    R1,R12,24(R13)      restore R1-R12 from caller SA
         BR    R14
*
NULLPLST DS    0H
*  R1=0 on entry; no GETMAIN, no VLIST to parse.
         LA    R15,24              BADPARM
         L     R14,12(,R13)
         LM    R0,R12,20(R13)
         BR    R14
*
         LTORG
*
*  --- workarea DSECT (PDP-DSA shape, mirrors irxload.asm) ----------
*
WAREA    DSECT
WDFLAGS  DS    F                   +0  DSAFLAGS (must be 0)
WDPREV   DS    F                   +4  DSAPREV  (back chain)
WDNEXT   DS    F                   +8  DSANEXT  (forward chain)
WDR14    DS    F                   +12 caller R14
WDR15    DS    F                   +16 caller R15
WDR0     DS    F                   +20 caller R0 (ENVBLOCK hint)
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
WPARMS   DS    1F                  bare address from 1-slot VLIST
WCPLIST  DS    3F                  C plist: 2 dispatch args + sentinel
*  Stack pool for nested c2asm370 PDPPRLG frames.
WPOOL    DS    2048F               8 KB scratchpad
WALEN    EQU   *-WAREA
*
         END   IRXJCL
