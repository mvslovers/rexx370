         TITLE 'IRXINIT - REXX/370 IRXINIT Entry Point Wrapper'
*
*  IRXINIT - HLASM entry-point wrapper for the IRXINIT Programming
*            Service (SC28-1883-0 §14).
*
*  Parses the caller VLIST, validates the high-bit endmarker on the
*  last parameter, extracts the CL8 function code, and delegates to
*  the C-core dispatcher irx_init_dispatch (asm() alias IRXIDISP,
*  CON-4) which routes on the function code to one of:
*
*    INITENVB  -> irx_init_initenvb()  (alias IRXIINIT)
*    FINDENVB  -> irx_init_findenvb()  (alias IRXIFIND)
*    CHEKENVB  -> irx_init_chekenvb()  (alias IRXICHEK)
*
*  Calling convention (per SC28-1883-0):
*
*    CALL IRXINIT,(FCODE,PARMMOD,USERFLD,WKBLKEXT,RESERVED,         *
*                  ENVBLK,REASON),VL
*
*  Each VLIST slot is a 4-byte address pointing at the parameter
*  value. The address of the LAST slot has its high-order bit set
*  to mark the end of the variable-length list.
*
*  R0  (in)  optional previous-env hint (passed to dispatcher)
*  R0  (out) ENVBLOCK on success, original R0 on failure
*  R15 (out) return code: 0 = ok, 4 = no env (FINDENVB),
*            20 = error (RSN written to caller REASON slot)
*
*  Built as a separate load module (entry IRXINIT). The C-core
*  (irx#init.c, irx#anch.c, ...) is NCAL-linked into the same
*  load module via project.toml include list.
*
*  Bootstrap design: this wrapper pre-allocates a WPOOL stack pool
*  inside its workarea and shapes the workarea as a PDP-DSA so
*  c2asm370-compiled callees can use their PDPPRLG bump-allocator
*  prologue (which reads DSANAB at offset +76 of the caller DSA).
*  irxstor() routes all storage through GETMAIN/FREEMAIN on MVS so
*  the CLIBCRT C-runtime is not on the call path — @@CRT0 is not
*  required for the dispatch to succeed.
*
*  Caveat: crent370 getmain() calls wtof() on its failure path,
*  which IS CLIBCRT-dependent; a getmain failure here (very
*  unlikely on a healthy system) would crash secondarily trying
*  to log. Acceptable tail risk on the bootstrap path. See #85.
*
*  Ref: SC28-1883-0 §14 (IRXINIT Programming Service)
*  Ref: WP-I1c.5 / TSK-198 / GitHub mvslovers/rexx370#83
*  Ref: CON-1 §6.3 (INITENVB algorithm)
*  Ref: CON-4 (asm() aliases)
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
IRXINIT  CSECT
*
*  --- standard MVS entry linkage ---
         STM   R14,R12,12(R13)     save caller R14-R12 in caller SA
         BALR  R12,0
         USING *,R12
*
*  Capture caller-supplied registers needed across the wrapper body.
*  R10 must be set BEFORE the NULL-R1 check below — the NULLPLST
*  early-exit restores caller R0 from R10 and would otherwise fault.
         LR    R10,R0              R10 = previous-env hint (R0 in)
         LR    R11,R1              R11 = caller VLIST address
*
*  Defensive NULL check: a caller passing R1=0 (no parm list at all)
*  must not provoke an S0C5 in PARSELP and must not leak the workarea
*  we are about to GETMAIN. Bail before any allocation; we cannot
*  reach a REASON slot either, so just return RC=20 with R0 intact.
         LTR   R11,R11             VLIST pointer NULL?
         BZ    NULLPLST            yes -> early exit, no GETMAIN
*
*  --- allocate dynamic workarea (PDP-DSA + locals + WPOOL) -------
*  WALEN ~ 8 KB so we cannot use LA (12-bit displacement); load via
*  literal pool instead. Subpool 0 (RU form) and no zero-fill.
         L     R0,=A(WALEN)
         GETMAIN RU,LV=(0)
         LR    R8,R1               R8 = workarea ptr (saved for FREE)
*
*  Chain DSAs: caller SA <-> our DSA. WDPREV = +4 (= old WSAVE+4).
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
*  Zero the workarea fields that the SETRSN path inspects so we can
*  reliably distinguish "slot address parsed" from "slot never seen".
*  GETMAIN RU does not zero-fill; WPARMS / WCPLIST start with junk.
         XC    WPARMS(28),WPARMS    7F  = 28 bytes
         XC    WCPLIST(24),WCPLIST  6F  = 24 bytes
*
*  Stash the caller-supplied previous-env hint for the C-call.
         ST    R10,WPREV
*
*  --- parse VLIST: 7 entries, high-bit endmarker on slot 7 ---
         LA    R2,7                expected slot count (countdown)
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
*  Walked all 7 slots without seeing the VL marker. The slot-7
*  WPARMS entry was filled from VLIST[6], which is past the caller's
*  actual list end and therefore undefined. We MUST NOT treat it as
*  a REASON-slot address — go to ERREARLY (no reason write).
         LA    R15,20
         B     ERREARLY
*
PARSEVL  EQU   *
*  VL marker found; R2 = remaining count (must be 1 if on slot 7).
         CH    R2,=H'1'
         BE    FCCHK
*  VL on the wrong slot — caller list is short. WPARMS+24 was never
*  filled (or holds a stale value); no usable REASON slot reachable.
         LA    R15,20
         B     ERREARLY
*
FCCHK    EQU   *
*  --- validate function code: exact CL8, no trailing blanks ---
         L     R2,WPARMS+0         R2 = address of FCODE
         CLC   0(8,R2),=CL8'INITENVB'
         BE    BUILDC
         CLC   0(8,R2),=CL8'FINDENVB'
         BE    BUILDC
         CLC   0(8,R2),=CL8'CHEKENVB'
         BE    BUILDC
*
*  Unknown function code (incl. trailing blanks). Slot 7 was reached
*  via the normal VL-marker path, so WPARMS+24 holds the caller's
*  REASON slot address and is safe to write through.
         LA    R15,20
         B     SETRSN
*
BUILDC   EQU   *
*  --- build C-call plist for IRXIDISP -----------------------------
*
*    irx_init_dispatch(funccode, prev_envblock, caller_parmblock,
*                      user_field, envblock_inout, out_reason_code)
*
*  PDPCLIB calling convention (verified via __crt0.asm CTHREAD,
*  per WP-I1c.5 Q-WRAP-1 RESOLVED): values go directly in plist
*  slots; pointer args are 4-byte addresses; in/out and out args
*  are addresses of caller storage.
*
         L     R2,WPARMS+0         R2 = addr of CL8 funccode
         ST    R2,WCPLIST+0
*
         L     R2,WPREV            R2 = previous-env hint
         ST    R2,WCPLIST+4
*
*  P2 (Parameters-Module-Name, WPARMS+4) is ignored for now —
*  MODNAMET-resolution lands with WP-I1c.4. The caller's PARMBLOCK
*  pointer per SC28-1883-0 §14 lives in P3 (In-Storage Parm List
*  Address) at WPARMS+8, and the user field is P4 at WPARMS+12.
*
         L     R2,WPARMS+8         R2 = addr of P3 (parmblock ptr slot)
         L     R3,0(,R2)           R3 = caller PARMBLOCK pointer
         ST    R3,WCPLIST+8        -> caller_parmblock argument
*
         L     R2,WPARMS+12        R2 = addr of P4 (user field slot)
         L     R3,0(,R2)           R3 = user_field value
         ST    R3,WCPLIST+12       -> user_field argument
*
         L     R2,WPARMS+20        R2 = addr of ENVBLK slot
         ST    R2,WCPLIST+16
*
         L     R2,WPARMS+24        R2 = addr of caller REASON slot
         ST    R2,WCPLIST+20
*
*  --- call irx_init_dispatch ---
         LA    R1,WCPLIST
         L     R15,=V(IRXIDISP)
         BALR  R14,R15
*
*  R15 = RC; the C-core has written *envblock_inout and *reason
*  through the addresses we passed.
*
         LR    R3,R15              R3 = RC (preserved across teardown)
         LTR   R3,R3
         BNZ   FAILR0              non-zero RC -> R0 stays as caller's
*
*  Success: R0 = new ENVBLOCK from caller's slot.
         L     R7,WPARMS+20
         L     R4,0(,R7)           R4 = new ENVBLOCK
         B     EPILOG
*
FAILR0   EQU   *
         L     R4,WPREV            R4 = caller's original R0
         B     EPILOG
*
SETRSN   EQU   *
*  --- error path WITH reason write ---------------------------------
*
*  Reached only via FCCHK (function code mismatch after the VLIST
*  parser saw the VL marker on slot 7). WPARMS+24 therefore holds
*  the caller's REASON slot address and can be written safely.
         LR    R3,R15              R3 = RC (20)
         LR    R4,R10              R4 = caller's original R0
         L     R7,WPARMS+24        caller REASON slot address
         MVC   0(4,R7),=F'1'       reason code 1
         B     EPILOG
*
ERREARLY EQU   *
*  --- error path WITHOUT reason write -------------------------------
*
*  Used by the two VLIST-shape failures ("walked all 7 without VL"
*  and "VL on wrong slot") where WPARMS+24 cannot be trusted to
*  hold a real caller REASON slot address. R15 is already set; we
*  return RC=20 with R0 restored to the caller's original R0.
         LR    R3,R15              R3 = RC (20)
         LR    R4,R10              R4 = caller's original R0
*  Fall through to EPILOG.
*
EPILOG   EQU   *
*  R3 = output RC, R4 = output R0 — both in safe regs (1..12).
*  Tear down: restore R13, FREEMAIN, set outputs, return.
*
         L     R13,WDPREV          R13 = caller SA (back chain in DSA)
*
*  WALEN ~ 8 KB so the LA form does not fit; use literal pool.
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(8)
*
         LR    R0,R4               R0  = output ENVBLOCK / original
         LR    R15,R3              R15 = output RC
*
*  Restore R14 and R1-R12 from caller SA (preserve our R0 / R15).
         L     R14,12(,R13)
         LM    R1,R12,24(R13)
         BR    R14
*
NULLPLST DS    0H
*  Caller passed R1=0 — no VLIST, no REASON slot reachable. We have
*  not allocated a workarea yet, so just restore caller R14/R1-R12
*  from the still-current caller SA (R13 unchanged) and return with
*  R0 = caller's original R0 (saved into R10 above) and R15 = 20.
         LA    R15,20              RC=20
         LR    R0,R10              caller's original R0
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
*  for nested c2asm370 frames; 8 KB covers ~25-50 frames @ ~150-300
*  bytes each, well above the deepest IRXINIT call chain.
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
WPREV    DS    F                   saved R0 in (previous-env hint)
WPARMS   DS    7F                  bare addresses parsed from VLIST
WCPLIST  DS    6F                  parameter list for IRXIDISP call
*  Stack pool for nested c2asm370 PDPPRLG frames.
WPOOL    DS    2048F               8 KB scratchpad
WPOOLEND EQU   *
WALEN    EQU   *-WAREA
*
         END   IRXINIT
