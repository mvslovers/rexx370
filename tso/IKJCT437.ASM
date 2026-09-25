         TITLE 'IKJCT437 - REXX/370 EXEC CLASSIFIER AND RUNNER'
*
*  IKJCT437 - Decide whether an implicitly invoked exec is REXX and,
*             if it is, run it.  Called from IKJCT430 where the
*             implicit member name is known and nothing has been
*             allocated or opened yet.
*
*  Entry   R0  -> CPPL          (for the ECT, hence ECTENVBK)
*          R1  -> CL8 member name, blank padded
*          R13 -> caller savearea,  R14 -> return
*  Exit    R15 = 0   not REXX -- IKJCT430 carries on with CLIST
*          R15 = 4   was REXX, already executed
*          all registers except R15 restored
*
*  ---- Why the checks are in this order ----
*
*  No BLDL.  The question is not "is REXX/370 on disk" but "is a REXX
*  environment usable now", and ECTENVBK (ECT+X'30') answers it for
*  free: NULL or no 'ENVBLOCK' eye-catcher means part A never ran, so
*  we load nothing at all and hand the exec back to CLIST.
*
*  One IRXLOAD call, not two.  IRXLOAD searches SYSEXEC then SYSPROC
*  and since rexx370 #222 records the DD that answered in
*  instblk_ddname.  That single field is the whole language decision:
*
*      SYSEXEC  -> REXX, no content check at all
*      SYSPROC  -> CLIST unless line 1 is a comment containing REXX
*
*  Measured on z/OS 2026-09-24: a SYSEXEC member with no comment
*  whatsoever still runs as REXX, so the DD really does decide alone.
*
*  ---- Constraints measured on this module, 2026-09-24 ----
*
*  The EXEC load module is AC=0.  Nothing authorized may be called
*  from here -- the opposite of IKJEFTRX, where a missing --ac 1
*  cost an S047.
*
*  IKJCT430 issues no SPKA/IPK/MODESET; it runs throughout in the key
*  it was entered in, so no key bracket is needed around the call.
*
*  VLIST slots hold ADDRESSES OF fullwords, not the values.  A literal
*  zero in a slot would be dereferenced as PSA[0] and yield a
*  plausible-looking garbage pointer; ZEROW is a static F'0' whose
*  address is stored instead.  Same trap as IRXINIT INITENVB.
*
*  NOTE for as370/IFOX00: RS-format LM/STM must be written D(B).
*
         PRINT NOGEN
R0       EQU   0
R11      EQU   11
R1       EQU   1
R2       EQU   2
R3       EQU   3
R4       EQU   4
R5       EQU   5
R6       EQU   6
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
CPPLECT  EQU   12                 CPPL -> ECT
ECTENVBK EQU   X'30'              ECT  -> ENVBLOCK
         PRINT GEN
*
IKJCT437 CSECT
         STM   R14,R12,12(R13)
         BALR  R12,0
         USING *,R12
         LR    R5,R0              CPPL
         LR    R6,R1              -> member name
         WTO   'C1 ENTERED'
*
* --- is a REXX environment up at all? ---
         LTR   R5,R5
         BZ    NOTREXX
         L     R2,CPPLECT(,R5)    ECT
         WTO   'C2 ECT READ'
         LTR   R2,R2
         BZ    NOTREXX
         L     R2,ECTENVBK(,R2)   ENVBLOCK
         WTO   'C3 ENVBLOCK READ'
         LTR   R2,R2
         BZ    NOTREXX
         CLC   0(8,R2),ENVBEYE    valid block, not a stale slot?
         BNE   NOTREXX
         WTO   'C4 ENV EYE OK'
*
* --- workarea ---
         L     R0,=A(WALEN)
         GETMAIN RC,LV=(0)
         LTR   R15,R15
         BNZ   NOTREXX
         LR    R3,R1              keep the area address
*  Zero the WHOLE area.  GETMAIN storage is not zeroed, and WHANDLED
*  plus the two LOAD flags are read on paths that never write them.
*  A garbage WHANDLED makes this module report "was REXX, already
*  executed" for an exec it never ran, and IKJCT430 then swallows the
*  member in silence -- it reports success and nothing happens.
         LR    R0,R1
         L     R1,=A(WALEN)
         SR    R14,R14            source length 0, pad byte 0
         SR    R15,R15
         MVCL  R0,R14
         LR    R1,R3
         ST    R13,4(,R1)
         ST    R1,8(,R13)
         LR    R13,R1
         USING WAREA,R13
         WTO   'C5 GETMAIN OK'
*
* --- EXECBLK: member from the caller, DD left blank so IRXLOAD
*     runs its own SYSEXEC-then-SYSPROC search ---
         MVC   WEXECB(8),EXECBEYE
         MVC   WEXECBLN,=A(48)
         MVC   WEXECBMB,0(R6)     member
         MVC   WEXECBDD,BLANK8    blank = search
         WTO   'C6 EXECBLK BUILT'
*
         LOAD  EP=IRXLOAD,ERRET=FREEWA
         LR    R2,R0
         ST    R0,WLOADEP         keep it -- FREE below reuses it
         MVI   WLDFL,X'01'        ... and it is DELETEd exactly once
         WTO   'C7 IRXLOAD LOADED'
         LA    R1,FCLOAD
         ST    R1,WLVL+0          P1 function code
         LA    R1,WEXECB
         ST    R1,WLP2
         LA    R1,WLP2
         ST    R1,WLVL+4          P2 -> EXECBLK
         LA    R1,WINST
         ST    R1,WLVL+8          P3 INSTBLK out
         LA    R1,ZEROW
         ST    R1,WLVL+12         P4 ENVBLOCK = default
         LA    R1,WLRET
         O     R1,=X'80000000'
         ST    R1,WLVL+16         P5 retcode, VL
         LA    R1,WLVL
         LR    R15,R2
         BALR  R14,R15
         LTR   R15,R15
         BNZ   FREEWA             no such exec -- let CLIST try
         WTO   'C8 IRXLOAD RC0'
*
* --- the DD that answered IS the language decision ---
         L     R4,WINST
         LTR   R4,R4
         BZ    FREEINST
*  instblk_ddname is at +32, not +28.  Offsets re-derived from
*  struct instblk in include/irx.h with 4-byte pointers:
*      +0 acronym(8)  +8 hdrlen  +12 filler  +16 address
*     +20 usedlen    +24 member(8)          +32 ddname(8)
*  and they sum to the documented 128-byte header.
         CLC   32(8,R4),DDEXEC    instblk_ddname = 'SYSEXEC '?
         WTO   'C9 DDNAME CHECKED'
         BE    RUNIT
         CLC   32(8,R4),DDPROC    'SYSPROC ' -- then line 1 decides
         BNE   FREEINST
         BAL   R11,CHKLINE1
         LTR   R15,R15
         BZ    FREEINST           no REXX identifier -- it is a CLIST
*
* --- run it: INSTBLK in P4, ENVBLOCK NULL so IRXEXEC does FINDENVB
RUNIT    DS    0H
         LOAD  EP=IRXEXEC,ERRET=FREEINST
         LR    R2,R0
         MVI   WEXFL,X'01'        DELETE it on the way out
         LA    R1,ZEROW
         ST    R1,WXVL+0          P1 EXECBLK  (none; P4 carries it)
         ST    R1,WXVL+4          P2 ARGTABLE
         ST    R1,WXVL+8          P3 flags
         LA    R1,WINST
         ST    R1,WXVL+12         P4 INSTBLK
         LA    R1,ZEROW
         ST    R1,WXVL+16         P5 reserved
         ST    R1,WXVL+20         P6 EVALBLOCK
         ST    R1,WXVL+24         P7 work area
         ST    R1,WXVL+28         P8 user field
         ST    R1,WXVL+32         P9 ENVBLOCK -- NULL: FINDENVB
         LA    R1,WXRC
         O     R1,=X'80000000'
         ST    R1,WXVL+36         P10 REXX RC, VL
         LA    R1,WXVL
         LR    R15,R2
         BALR  R14,R15
         MVI   WHANDLED,X'01'
*
FREEINST DS    0H
         L     R4,WINST
         LTR   R4,R4
         BZ    FREEWA
         LA    R1,FCFREE
         ST    R1,WLVL+0
         LA    R1,ZEROW
         ST    R1,WLVL+4
         LA    R1,WINST
         ST    R1,WLVL+8
         LA    R1,ZEROW
         ST    R1,WLVL+12
         LA    R1,WLRET
         O     R1,=X'80000000'
         ST    R1,WLVL+16
*  Reuse the entry point from the LOAD above.  A second LOAD would
*  raise the use count once per implicitly invoked exec and never
*  give it back -- on a 16 MB machine that is not a rounding error.
         L     R15,WLOADEP
         LTR   R15,R15
         BZ    FREEWA
         LA    R1,WLVL
         BALR  R14,R15
*
FREEWA   DS    0H
*  DELETE before the FREEMAIN -- the flags live in the area we are
*  about to hand back.
         CLI   WLDFL,X'00'
         BE    NODELLD
         DELETE EP=IRXLOAD
NODELLD  CLI   WEXFL,X'00'
         BE    NODELEX
         DELETE EP=IRXEXEC
NODELEX  DS    0H
*  FREEMAIN clobbers R0/R1/R15 only, so the verdict rides in R2
*  across it -- this module is RENT and has no writable statics.
         SR    R2,R2
         IC    R2,WHANDLED
         LR    R1,R13
         L     R13,4(,R13)
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(R1)
         DROP  R13
         LTR   R2,R2
         BNZ   WASREXX
*
NOTREXX  DS    0H
         LM    R14,R12,12(R13)
         SR    R15,R15
         BR    R14
*
WASREXX  DS    0H
         LM    R14,R12,12(R13)
         LA    R15,4
         BR    R14
*
* ------------------------------------------------------------------
*  CHKLINE1 - does line 1 start a comment that contains REXX?
*             R4 -> INSTBLK, link in R11.  R15 = 4 yes, 0 no.
*             SC28-1883 requires
*             both: the exec must START with a comment, and the
*             characters REXX must appear within line 1.
* ------------------------------------------------------------------
CHKLINE1 DS    0H
         SR    R15,R15
         L     R1,16(,R4)         instblk_address -> first entry
         LTR   R1,R1
         BZ    CL1DONE
         L     R2,20(,R4)         usedlen; zero means no lines
         LTR   R2,R2
         BZ    CL1DONE
         L     R0,0(,R1)          entry.stmt address
         L     R2,4(,R1)          entry.stmt length
         LTR   R0,R0
         BZ    CL1DONE
         C     R2,=F'2'
         BL    CL1DONE
         CLC   0(2,R0),SLASHST    must start with '/*'
         BNE   CL1DONE
*  Positions for a 4-byte compare are len-3, not len-4, and the old
*  BM let len=4 through with R2=0 -- BCT then wrapped to X'FFFFFFFF'
*  and scanned off the end of storage.
         S     R2,=F'3'           number of start positions
         BNP   CL1DONE
         LR    R1,R0
CL1SCAN  CLC   0(4,R1),REXXLIT
         BE    CL1YES
         LA    R1,1(,R1)
         BCT   R2,CL1SCAN
         B     CL1DONE
CL1YES   LA    R15,4
CL1DONE  BR    R11
*
* --- read-only static data (RENT-safe) ---
ENVBEYE  DC    C'ENVBLOCK'
EXECBEYE DC    C'IRXEXECB'
DDEXEC   DC    CL8'SYSEXEC'
DDPROC   DC    CL8'SYSPROC'
BLANK8   DC    CL8' '
FCLOAD   DC    CL8'LOAD'
FCFREE   DC    CL8'FREE'
SLASHST  DC    C'/*'
REXXLIT  DC    C'REXX'
ZEROW    DC    F'0'
*
         LTORG
*
WAREA    DSECT
WSAVE    DS    18F
WEXECB   DS    0F                 EXECBLK, 48 bytes
WEXECBAC DS    CL8
WEXECBLN DS    F
         DS    F
WEXECBMB DS    CL8
WEXECBDD DS    CL8
         DS    CL8
         DS    F
         DS    F
WEXECBL  EQU   *-WEXECB
WINST    DS    A                  INSTBLK pointer (IRXLOAD output)
WLVL     DS    5A                 IRXLOAD VLIST
WLP2     DS    A
WLRET    DS    F
WXVL     DS    10A                IRXEXEC VLIST
WXRC     DS    F
WLOADEP  DS    A                  IRXLOAD entry point, LOADed once
WHANDLED DS    X                  X'01' = the exec was run
WLDFL    DS    X                  X'01' = IRXLOAD is loaded
WEXFL    DS    X                  X'01' = IRXEXEC is loaded
         DS    0F
WALEN    EQU   *-WAREA
*
         END
