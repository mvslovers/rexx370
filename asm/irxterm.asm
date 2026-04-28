         TITLE 'IRXTERM - REXX/370 IRXTERM Entry Point Wrapper'
*
*  IRXTERM - HLASM entry-point wrapper for the IRXTERM Programming
*            Service (SC28-1883-0 §15).
*
*  Delegates to the C-core irx_init_term (asm() alias IRXITERM,
*  CON-4) which performs the 5-step teardown: validate eye-catcher,
*  IRXANCHR slot lookup, free IRXEXTE/PARMBLOCK, release slot, roll
*  ECTENVBK back to predecessor for TSO envs, and free the ENVBLOCK.
*
*  Calling convention (per SC28-1883-0):
*
*    CALL IRXTERM           (no parameter list)
*
*  R0  (in)  ENVBLOCK to terminate (NOT in a parameter list)
*  R0  (out) predecessor ENVBLOCK on success (read back from
*            ECTENVBK after the C-core rolled it back), or the
*            original R0 unchanged on failure
*  R15 (out) return code: 0 = ok, 20 = bad ENVBLOCK
*
*  Built as a separate load module (entry IRXTERM). The C-core
*  (irx#term.c, irx#anch.c, ...) is NCAL-linked into the same
*  load module via project.toml include list.
*
*  Bootstrap design: same as IRXINIT — workarea is a PDP-DSA with
*  an embedded WPOOL stack pool so c2asm370 callees can run their
*  PDPPRLG bump-allocator prologue. irxstor() uses GETMAIN on MVS,
*  bypassing the CLIBCRT C-runtime. See asm/irxinit.asm prologue
*  comment block and #85 for the full rationale and tail-risk
*  caveat (wtof on the getmain failure path).
*
*  Ref: SC28-1883-0 §15 (IRXTERM Programming Service)
*  Ref: WP-I1c.5 / TSK-198 / GitHub mvslovers/rexx370#83
*  Ref: CON-1 §6.4 (IRXTERM flow)
*  Ref: CON-14 / IRXPROBE Phase α (ECTENVBK predecessor rollback)
*
*  (c) 2026 mvslovers - REXX/370 Project
*
         PRINT NOGEN
R0       EQU   0
R1       EQU   1
R2       EQU   2
R3       EQU   3
R4       EQU   4
R8       EQU   8
R10      EQU   10
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
         PRINT GEN
*
IRXTERM  CSECT
*
*  --- standard MVS entry linkage ---
         STM   R14,R12,12(R13)     save caller R14-R12 in caller SA
         BALR  R12,0
         USING *,R12
*
*  Capture R0 in (the ENVBLOCK to terminate).
         LR    R10,R0              R10 = caller R0 (envblock)
*
*  --- allocate dynamic workarea (PDP-DSA + locals + WPOOL) -------
*  WALEN ~ 8 KB so we cannot use LA (12-bit displacement); load via
*  literal pool instead. Subpool 0 (RU form) and no zero-fill.
         L     R0,=A(WALEN)
         GETMAIN RU,LV=(0)
         LR    R8,R1               R8 = workarea ptr
*
*  Chain DSAs: caller SA <-> our DSA. Offsets +4/+8 are hardcoded
*  to WDPREV / WDNEXT in the WAREA DSECT — keep in sync.
         ST    R13,4(,R1)          our DSA back-chain = caller SA
         ST    R1,8(,R13)          caller forward     = our DSA
         LR    R13,R1
         USING WAREA,R13
*
*  --- initialize PDP-DSA fields the c2asm370 callees inspect ----
*
*  PDPPRLG (callee prologue) reads DSANAB at +76 of OUR DSA as the
*  bump-allocator base. WDFLAGS (+0) and WDLWA (+72) must be zero
*  per the PDP convention. R14-R12 (+12..+68) are written by the
*  callee's SAVE-equivalent on entry.
         XC    WDFLAGS(4),WDFLAGS  DSAFLAGS = 0
         XC    WDLWA(4),WDLWA      DSALWA   = 0
         LA    R0,WPOOL            R0 = start of stack pool
         ST    R0,WDNAB            DSANAB   = WPOOL
*
*  --- build C-call plist for IRXITERM -----------------------------
*
*    irx_init_term(envblock, &reason_code)
*
*  PDPCLIB convention: IN ENVBLOCK pointer goes directly in plist
*  slot 0 (value, not pointer-to-pointer); OUT reason takes the
*  address of the local WREASON cell.
*
         ST    R10,WCPLIST+0       envblock value
         LA    R2,WREASON
         ST    R2,WCPLIST+4
*
         LA    R1,WCPLIST
         L     R15,=V(IRXITERM)
         BALR  R14,R15
*
*  R15 = RC; *WREASON now holds the reason code. We discard the
*  reason — IBM IRXTERM has no caller-visible reason slot, R15 is
*  the only output channel.
*
         LR    R3,R15              R3 = RC (preserved through teardown)
         LTR   R3,R3
         BNZ   FAILR0              non-zero RC -> R0 stays as original
*
*  Success: read predecessor from ECTENVBK via anch_curr(). The
*  C-core has already rolled the slot back (TSO envs only); for
*  non-TSO envs the slot is unchanged and anch_curr returns
*  whatever was there before — which is the right answer for the
*  caller too.
         LA    R1,WCPLIST          minimal plist (no args)
         L     R15,=V(ANCHCURR)
         BALR  R14,R15
         LR    R4,R15              R4 = predecessor envblock or NULL
         B     EPILOG
*
FAILR0   EQU   *
         LR    R4,R10              R4 = original R0 (saved at entry)
*
EPILOG   EQU   *
*  R3 = RC, R4 = R0 output. Tear down workarea and return.
         L     R13,WDPREV          R13 = caller SA (back chain in DSA)
*
*  WALEN ~ 8 KB so the LA form does not fit; use literal pool.
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(8)
*
         LR    R0,R4               R0  = output ENVBLOCK / original
         LR    R15,R3              R15 = RC
*
         L     R14,12(,R13)
         LM    R1,R12,24(R13)
         BR    R14
*
         LTORG
*
*  --- workarea DSECT (PDP-DSA shape) ------------------------------
*
*  Offsets +0..+79 follow pdptop.copy exactly so c2asm370 callees
*  (PDPPRLG prologue) can chain through OUR DSA correctly. The
*  18F save area at +0..+71 maps onto the standard MVS save-area
*  offsets used by SAVE/RETURN, and WDLWA / WDNAB extend it to
*  the 80-byte PDP DSA. WPOOL provides the bump-allocator pool
*  for nested c2asm370 frames; see the WPOOL block below for the
*  sizing and zero-init rationale.
WAREA    DSECT
WDFLAGS  DS    F                   +0  DSAFLAGS (must be 0)
WDPREV   DS    F                   +4  DSAPREV  (back chain)
WDNEXT   DS    F                   +8  DSANEXT  (forward chain)
WDR14    DS    F                   +12 caller R14 (set by SAVE)
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
*  Wrapper-local storage (after the PDP-DSA proper).
WCPLIST  DS    2F                  C-call plist for IRXITERM
WREASON  DS    F                   reason-code OUT cell (discarded)
*  Stack pool for nested c2asm370 PDPPRLG frames. Sized for typical
*  IRXIDISP call depth (5-10 nested frames @ 88-300 bytes each); 8 KB
*  has comfortable margin. Intentionally not zero-filled — c2asm370-
*  emitted code SAVE-writes R14-R12 before reading any frame slot, so
*  XC initialization would cost 8 KB without functional benefit.
WPOOL    DS    2048F               8 KB scratchpad
WALEN    EQU   *-WAREA
*
         END   IRXTERM
