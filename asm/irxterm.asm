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
*  Known gap (deferred): same crent370 C-runtime bootstrap concern
*  as IRXINIT — see asm/irxinit.asm prologue. WP-I1c.5e follow-up.
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
IRXTERM  AMODE 24
IRXTERM  RMODE 24
*
*  --- standard MVS entry linkage ---
         STM   R14,R12,12(R13)     save caller R14-R12 in caller SA
         BALR  R12,0
         USING *,R12
*
*  Capture R0 in (the ENVBLOCK to terminate).
         LR    R10,R0              R10 = caller R0 (envblock)
*
*  --- allocate dynamic workarea ---
         LA    R0,WALEN
         GETMAIN RU,LV=(0)
         LR    R8,R1               R8 = workarea ptr
*
*  Chain save areas: caller SA <-> our DSA. WSAVE is at offset 0
*  of the DSECT, so WSAVE+4 = offset 4 in our DSA.
         ST    R13,4(,R1)          our DSA back-chain = caller SA
         ST    R1,8(,R13)          caller forward     = our DSA
         LR    R13,R1
         USING WAREA,R13
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
         L     R13,WSAVE+4         R13 = caller SA
*
         LA    R0,WALEN
         FREEMAIN RU,LV=(0),A=(8)
*
         LR    R0,R4               R0  = output ENVBLOCK / original
         LR    R15,R3              R15 = RC
*
         L     R14,12(,R13)
         LM    R1,R12,24(,R13)
         BR    R14
*
         LTORG
*
*  --- workarea DSECT ----------------------------------------------
WAREA    DSECT
WSAVE    DS    18F                 standard 72-byte save area
WCPLIST  DS    2F                  C-call plist for IRXITERM
WREASON  DS    F                   reason-code OUT cell (discarded)
WALEN    EQU   *-WAREA
*
         END   IRXTERM
