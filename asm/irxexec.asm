         TITLE 'IRXEXEC - REXX/370 IRXEXEC Entry Point Wrapper'
*
*  IRXEXEC - HLASM entry-point wrapper for the IRXEXEC Programming
*            Service (z/OS 10-slot VLIST form, WP-CPS-06 / TSK-218).
*
*  Parses the caller VLIST (10 slots, VL-bit on P9 or P10), saves
*  the R0 envblock hint, and delegates to the C-core dispatcher
*  irx_exec_dispatch (asm() alias IRXEDISP, CON-4).
*
*  Calling convention (z/OS IRXEXEC, 10-slot form):
*
*    CALL IRXEXEC,(EXECBLK,ARGTAB,FLAGS,INSTBLK,RESV5,
*                  EVALBLK,WKAREA,USRFLD,ENVBLK,REXXRC),VL
*
*  Each VLIST slot is a 4-byte address pointing at the parameter
*  value.  The address of the LAST slot has its high-order bit set
*  to mark the end of the variable-length list.
*
*  VLIST slot layout (z/OS-stage, differs from SC28-1883-0 V1):
*
*    P1   A      EXECBLK pointer (exec to run; may be NULL if P4 given)
*    P2   A      ARGTABLE pointer (argument string table)
*    P3   A      Flags fullword (bits 0-2 call type, bit 3 ext-RC)
*    P4   A      INSTBLK pointer (in-storage source; NULL = use DD)
*    P5   A      reserved (was CPPL in V1)
*    P6   A      EVALBLOCK pointer (for function result; NULL = ignored)
*    P7   A      work area pointer (reserved; NULL in most callers)
*    P8   A      user field pointer (reserved; NULL in most callers)
*    P9   A      ENVBLOCK pointer (NULL = inherit from R0 or ECT)
*    P10  F      REXX return code (output; optional; VL may be on P9)
*
*  R0 (in)  optional envblock pointer (additional to P9)
*  R0 (out) envblock pointer used for this call
*  R15 (out) return code: 0=OK, 28=NOENV, 32=BADPLIST, etc.
*
*  If P10 was supplied (VL on P10 rather than P9), the wrapper also
*  stores the R15 return code through the P10 pointer after the C call.
*
*  Architecture decisions (WP-CPS-06):
*    (a) P10 RC write: done here in the asm wrapper after the C call;
*        C dispatcher is agnostic about whether P10 was supplied.
*    (b) Source reconstruction: single allocation in C dispatcher
*        (size deterministic from sum(instblk_stmtlen) upfront).
*    (c) Separator byte: C literal '\n'; c2asm370 translates to
*        EBCDIC 0x15 at compile time; tokenizer accepts it.
*
*  V1 vs z/OS-stage:
*    SC28-1883-0 V1 had a shorter, different slot layout.
*    P7 (WORKAREA), P8 (USERFIELD), and P10 (return-code pointer)
*    are z/OS additions.  VL marker may be on P9 (P10 omitted) or P10.
*
*  Mirrors asm/irxload.asm structure and PDP-DSA/WPOOL shape.
*  See irxload.asm for rationale behind the bootstrap design.
*
*  Refs: z/OS REXX Reference (current edition) — canonical 10-slot VLIST
*        https://www.ibm.com/docs/en/zos/2.5.0?topic=ir-parameters
*        SC28-1883-0, Chapter 14 — V1 baseline (shorter VLIST)
*        WP-CPS-06 / TSK-218 / GitHub mvslovers/rexx370#120
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
IRXEXEC  CSECT
*
*  --- standard MVS entry linkage ---
         STM   R14,R12,12(R13)     save caller R14-R12 in caller SA
         BALR  R12,0
         USING *,R12
*
         LR    R11,R1              R11 = caller VLIST address
*  Save caller R0 (optional envblock-ptr) before GETMAIN clobbers R0.
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
*  Zero parse tables and flags before use.
         XC    WPARMS(40),WPARMS   10F = 40 bytes
         XC    WFLAGS(4),WFLAGS    P10-present flag
         XC    WDP10(4),WDP10      saved P10 bare addr
         XC    WCPLIST(44),WCPLIST 11F (10 dispatch args + sentinel)
*
*  --- parse VLIST: up to 10 entries, VL-bit legal on P9 or P10 -----
*
*  R2 = countdown from 10 (tracks which slot we are on)
*  R3 = current VLIST entry pointer
*  R4 = current destination in WPARMS array
*
         LA    R2,10               countdown = 10
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
*  All 10 slots walked without VL marker — malformed list.
         LA    R15,32              BADPLIST = 32
         B     ERREARLY
*
PARSEVL  EQU   *
*  VL found.  R2 = remaining countdown value at the moment VL hit.
*    R2=1 -> VL on slot 10 (P10 present)
*    R2=2 -> VL on slot  9 (P10 absent)
*    R2>2 -> VL on P1..P8 -> list too short -> BADPLIST
         CH    R2,=H'1'
         BE    P10PRES             VL on P10 -> P10 present
         CH    R2,=H'2'
         BE    BUILDC              VL on P9  -> P10 absent, proceed
         LA    R15,32              BADPLIST
         B     ERREARLY
*
P10PRES  EQU   *
*  P10 is present.  WPARMS+36 already holds the P10 bare address
*  (stored by the last loop iteration before PARSEVL).
*  Save it for the post-call RC write.
         OI    WFLAGS,X'80'        set P10-present flag
         L     R6,WPARMS+36        R6 = bare addr of caller's RC slot
         ST    R6,WDP10
*        fall through to BUILDC
*
BUILDC   EQU   *
*  --- build C-call plist for IRXEDISP ----------------------------
*
*  irx_exec_dispatch(execblk, argtable, flags_val,
*                    instblk, resv5, evalblk, wkarea, usrfld,
*                    envblock, envblock_r0)
*
*  P1 execblk: deref WPARMS+0 to get EXECBLK pointer.
         L     R2,WPARMS+0         R2 = addr of EXECBLK ptr slot
         L     R3,0(,R2)           R3 = EXECBLK ptr (may be 0)
         ST    R3,WCPLIST+0
*
*  P2 argtable: deref WPARMS+4 to get ARGTABLE pointer.
         L     R2,WPARMS+4
         L     R3,0(,R2)
         ST    R3,WCPLIST+4
*
*  P3 flags: WPARMS+8 -> addr of flags word -> value (int, by value).
*  Load flags int from *(*WPARMS+8) and pass the value directly.
         L     R2,WPARMS+8         R2 = addr of flags-word slot
         LTR   R2,R2               slot NULL?
         BZ    NOFLAG
         L     R3,0(,R2)           R3 = addr of caller's flags word
         LTR   R3,R3               NULL?
         BZ    NOFLAG
         L     R4,0(,R3)           R4 = flags int value
         ST    R4,WCPLIST+8
NOFLAG   EQU   *
*
*  P4 instblk: deref WPARMS+12 to get INSTBLK pointer.
         L     R2,WPARMS+12
         L     R3,0(,R2)
         ST    R3,WCPLIST+12
*
*  P5 reserved: deref WPARMS+16.
         L     R2,WPARMS+16
         L     R3,0(,R2)
         ST    R3,WCPLIST+16
*
*  P6 evalblk: deref WPARMS+20 to get EVALBLOCK pointer.
         L     R2,WPARMS+20
         L     R3,0(,R2)
         ST    R3,WCPLIST+20
*
*  P7 workarea: deref WPARMS+24.
         L     R2,WPARMS+24
         L     R3,0(,R2)
         ST    R3,WCPLIST+24
*
*  P8 userfield: deref WPARMS+28.
         L     R2,WPARMS+28
         L     R3,0(,R2)
         ST    R3,WCPLIST+28
*
*  P9 envblock: deref WPARMS+32 to get ENVBLOCK pointer.
         L     R2,WPARMS+32
         L     R3,0(,R2)
         ST    R3,WCPLIST+32
*
*  envblock_r0 (10th arg): caller's original R0 (saved in WDR0).
         L     R2,WDR0             R2 = caller R0 (envblock or 0)
         ST    R2,WCPLIST+36
*
*  --- call irx_exec_dispatch ---
         LA    R1,WCPLIST
         L     R15,=V(IRXEDISP)
         BALR  R14,R15
*
*  R15 = RC from C dispatcher.
         LR    R3,R15              R3 = RC (preserved across teardown)
*
*  --- P10 write: store RC through *P10 if P10 was supplied ----------
         TM    WFLAGS,X'80'        P10 present?
         BZ    NORP10              no -> skip
         L     R7,WDP10            R7 = addr of caller's RC fullword
         ST    R3,0(,R7)           *P10 = RC
NORP10   EQU   *
         B     EPILOG
*
ERREARLY EQU   *
*  VLIST malformed (VL before P9) — no workarea yet if early exit,
*  but we have our workarea here; R3 = RC, P10 never reached.
         LR    R3,R15              R3 = RC
*        fall through to EPILOG
*
EPILOG   EQU   *
*  Determine R0 for return: use P9 envblock if non-NULL, else entry R0.
*  (R0 on return must carry the envblock used, per z/OS convention.)
         L     R5,WPARMS+32        R5 = P9 envblock ptr
         LTR   R5,R5
         BNZ   HAVENV
         L     R5,WDR0             fallback to entry R0
HAVENV   EQU   *
*
         L     R13,WDPREV          R13 = caller SA
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(8)
*
         LR    R15,R3              R15 = return code
         L     R14,12(,R13)        caller R14
         LM    R0,R12,20(R13)      restore R0-R12 from caller SA
         LR    R0,R5               override R0 = envblock for caller
         BR    R14
*
NULLPLST DS    0H
*  R1=0 on entry; no GETMAIN, no VLIST to parse.
         LA    R15,32              BADPLIST
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
WDR0     DS    F                   +20 caller R0 (envblock hint)
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
WPARMS   DS    10F                 bare addresses from 10-slot VLIST
WFLAGS   DS    F                   parse flags (X'80' = P10 present)
WDP10    DS    F                   saved P10 bare address
WCPLIST  DS    11F                 C plist: 10 dispatcher args + sentinel
*  Stack pool for nested c2asm370 PDPPRLG frames.
WPOOL    DS    2048F               8 KB scratchpad
WALEN    EQU   *-WAREA
*
         END   IRXEXEC
