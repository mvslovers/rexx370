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
*  Known gap (deferred): the crent370 C runtime (CLIBCRT per-TCB
*  control block) is normally initialized by the @@CRT0 entry on
*  module load. Because the OS branches directly to IRXINIT here,
*  __crtset() is not yet called. The first call into the C-core
*  will hit calloc() with an uninitialized heap unless either
*    (a) a runtime-bootstrap call (__crtset / @@CRTSET) is added
*        in this prologue, or
*    (b) irxstor() switches subpool 0 to GETMAIN on MVS.
*  See WP-I1c.5e follow-up; the structural wrapper logic below is
*  correct and stays valid once the bootstrap path is added.
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
         LR    R10,R0              R10 = previous-env hint (R0 in)
         LR    R11,R1              R11 = caller VLIST address
*
*  --- allocate dynamic workarea (DSA + locals + C-call plist) ---
         LA    R0,WALEN
         GETMAIN RU,LV=(0)
         LR    R8,R1               R8 = workarea ptr (saved for FREE)
*
*  Chain save areas: caller SA <-> our DSA. WSAVE is at offset 0
*  of the DSECT, so WSAVE+4 = offset 4 in our DSA.
         ST    R13,4(,R1)          our DSA back-chain = caller SA
         ST    R1,8(,R13)          caller forward     = our DSA
         LR    R13,R1
         USING WAREA,R13
*
*  Zero the workarea fields that the SETRSN path inspects so we can
*  reliably distinguish "slot address parsed" from "slot never seen".
*  GETMAIN does not zero-fill (subpool 0, RU); WPARMS / WCPLIST start
*  with whatever was in storage.
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
*  Walked all 7 slots without seeing the VL marker.
         LA    R15,20
         B     SETRSN
*
PARSEVL  EQU   *
*  VL marker found; R2 = remaining count (must be 1 if on slot 7).
         CH    R2,=H'1'
         BE    FCCHK
         LA    R15,20
         B     SETRSN
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
*  Unknown function code (incl. trailing blanks).
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
         L     R2,WPARMS+4         R2 = addr of PARMMOD slot
         L     R3,0(,R2)           R3 = caller parmblock value
         ST    R3,WCPLIST+8
*
         L     R2,WPARMS+8         R2 = addr of USERFLD slot
         L     R3,0(,R2)           R3 = user_field value
         ST    R3,WCPLIST+12
*
         L     R2,WPARMS+20        R2 = addr of caller ENVBLK slot
         ST    R2,WCPLIST+16        (in/out for CHEKENVB; out otherwise)
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
*  --- error path: write RSN=1 to caller REASON slot if available ---
*
*  WPARMS+24 holds the caller REASON slot address only when slot 7
*  was reached during parsing. The "no VL marker anywhere" path
*  walks all 7 slots and stores their addresses, so WPARMS+24 is
*  valid there. The "VL on wrong slot" path bails before reaching
*  slot 7 and leaves WPARMS+24 untouched (=0); skip the write.
         LR    R3,R15              R3 = RC (20)
         LR    R4,R10              R4 = caller's original R0
         L     R7,WPARMS+24
         LTR   R7,R7
         BZ    EPILOG
         MVC   0(4,R7),=F'1'       reason code 1
*
EPILOG   EQU   *
*  R3 = output RC, R4 = output R0 — both in safe regs (1..12).
*  Tear down: restore R13, FREEMAIN, set outputs, return.
*
         L     R13,WSAVE+4         R13 = caller SA (back chain in DSA)
*
         LA    R0,WALEN
         FREEMAIN RU,LV=(0),A=(8)
*
         LR    R0,R4               R0  = output ENVBLOCK / original
         LR    R15,R3              R15 = output RC
*
*  Restore R14 and R1-R12 from caller SA (preserve our R0 / R15).
         L     R14,12(,R13)
         LM    R1,R12,24(,R13)
         BR    R14
*
         LTORG
*
*  --- workarea DSECT --------------------------------------------------
WAREA    DSECT
WSAVE    DS    18F                 standard 72-byte save area
WPREV    DS    F                   saved R0 in (previous-env hint)
WPARMS   DS    7F                  bare addresses parsed from VLIST
WCPLIST  DS    6F                  parameter list for IRXIDISP call
WALEN    EQU   *-WAREA
*
         END   IRXINIT
