         TITLE 'IKJCT437 - REXX/370 EXEC CLASSIFIER AND RUNNER'
*
*  IKJCT437 - Decide whether an exec is REXX and, if it is, run it.
*             Called from IKJCT430 before anything is stacked.
*
*  Entry IKJCT437  implicit (%name / name): the load routine searches
*          R1  -> CL8 member name, blank padded
*  Entry IKJCT43C  explicit EXEC 'ds(mem)': REXX only if line 1 is a
*                  comment containing REXX, whatever the library
*  Entry IKJCT43F  explicit EXEC 'ds(mem)' EXEC: REXX, forced
*          R1  -> CL8 member, then CL8 DD name (the DD DAIR
*                 allocated), 16 contiguous bytes
*  All     R0  -> CPPL          (for the ECT, hence ECTENVBK)
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
*  The exec is loaded through the environment's OWN exec load routine
*  (IRXEXTE load_routine, named in MODNAMET EXROUT), not by LOADing
*  IRXLOAD: IRXLOAD reads through stdio and needs a C runtime this
*  address space does not have (S0C4 in __CRTGET, JOB01171/JOB01209).
*  Under IRXTSPRM the routine is IRXLDTSO, which reads through BPAM
*  (rexx370 #230).  No load routine -> hand the exec back to CLIST.
*  Called the SC28-1883-0 Ch. 16 way: R0 = ENVBLOCK, three slots.
*
*  One LOAD call, not two.  The load routine searches SYSEXEC then
*  SYSPROC and records the DD that answered in instblk_ddname
*  (rexx370 #222).  That single field is the whole language decision:
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
R7       EQU   7
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
CPPLECT  EQU   12                 CPPL -> ECT
ECTENVBK EQU   X'30'              ECT  -> ENVBLOCK
ENVEXTE  EQU   28                 ENVBLOCK -> IRXEXTE
EXTELOAD EQU   8                  IRXEXTE  -> load_routine (EXROUT)
         PRINT GEN
*
IKJCT437 CSECT
*  Three entries share one body; R7 carries the mode.  Each entry
*  branches through its own address in R15, so none needs a base.
         STM   R14,R12,12(R13)
         SR    R7,R7              mode 0: implicit
         B     COMMON-IKJCT437(,R15)
         ENTRY IKJCT43C
IKJCT43C STM   R14,R12,12(R13)
         LA    R7,MODEC           mode 1: explicit, line 1 decides
         B     COMMON-IKJCT43C(,R15)
         ENTRY IKJCT43F
IKJCT43F STM   R14,R12,12(R13)
         LA    R7,MODEF           mode 2: explicit, forced
COMMON   BALR  R12,0
         USING *,R12
         LR    R5,R0              CPPL
         LR    R6,R1              -> member name
*
* --- is a REXX environment up at all? ---
         LTR   R5,R5
         BZ    NOTREXX
         L     R2,CPPLECT(,R5)    ECT
         LTR   R2,R2
         BZ    NOTREXX
         L     R2,ECTENVBK(,R2)   ENVBLOCK
         LTR   R2,R2
         BZ    NOTREXX
         CLC   0(8,R2),ENVBEYE    valid block, not a stale slot?
         BNE   NOTREXX
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
         ST    R2,WENV            ENVBLOCK (R2 since C3, untouched)
*
* --- EXECBLK: member from the caller.  Implicit: DD blank, so the
*     load routine runs its own search (LOADDD, then SYSPROC).
*     Explicit: the DD DAIR allocated, and nothing else ---
         MVC   WEXECB(8),EXECBEYE
         MVC   WEXECBLN,=A(48)
         MVC   WEXECBMB,0(R6)     member
         MVC   WEXECBDD,BLANK8    blank = search
         LTR   R7,R7
         BZ    EXBDONE
         MVC   WEXECBDD,8(R6)     explicit: the allocated DD
EXBDONE  DS    0H
*
* --- this environment's exec load routine ---
         L     R1,WENV
         L     R1,ENVEXTE(,R1)    IRXEXTE
         LTR   R1,R1
         BZ    FREEWA
         L     R1,EXTELOAD(,R1)   load_routine
         LTR   R1,R1
         BZ    FREEWA             none named -- let CLIST have it
         ST    R1,WLOADEP
         LA    R1,FCLOAD
         ST    R1,WLVL+0          P1 function code
         LA    R1,WEXECB
         ST    R1,WLP2
         LA    R1,WLP2
         ST    R1,WLVL+4          P2 -> EXECBLK
         LA    R1,WINST
         O     R1,=X'80000000'
         ST    R1,WLVL+8          P3 INSTBLK out, VL
         L     R0,WENV            R0 = ENVBLOCK
         LA    R1,WLVL
         L     R15,WLOADEP
         BALR  R14,R15
         LTR   R15,R15
         BNZ   FREEWA             no such exec -- let CLIST try
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
*  Explicit: the EXEC keyword forces REXX; without it line 1
*  decides, whatever library the data set is (measured on z/OS: a
*  CLIST in a SYSEXEC data set runs as CLIST when named explicitly).
*  Implicit: SYSPROC needs the REXX comment, every other DD -- the
*  LOADDD, SYSEXEC unless the parameters module names another --
*  holds REXX only (SC28-1883-0 p. 7).
         C     R7,=A(MODEF)
         BE    RUNIT
         LTR   R7,R7
         BNZ   CHKIT
         CLC   32(8,R4),DDPROC    'SYSPROC ' -- then line 1 decides
         BNE   RUNIT
CHKIT    BAL   R11,CHKLINE1
         LTR   R15,R15
         BZ    FREEINST           no REXX identifier -- it is a CLIST
*
* --- run it: INSTBLK in P4, and the ENVBLOCK we already checked
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
         LA    R1,WENV
         ST    R1,WXVL+32         P9 ENVBLOCK
         LA    R1,WXRC
         O     R1,=X'80000000'
         ST    R1,WXVL+36         P10 REXX RC, VL
         L     R0,WENV            R0 = ENVBLOCK
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
         O     R1,=X'80000000'
         ST    R1,WLVL+8
         L     R0,WENV
         L     R15,WLOADEP
         LA    R1,WLVL
         BALR  R14,R15
*
FREEWA   DS    0H
*  DELETE before the FREEMAIN -- the flag lives in the area we are
*  about to hand back.  The load routine is not ours to DELETE:
*  IRXINIT LOADed it for the environment and IRXTERM gives it back.
         CLI   WEXFL,X'00'
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
*  R0 cannot address anything: as a base register it means "no base",
*  so CLC 0(2,R0) compared PSA+0 with '/*' and every SYSPROC exec came
*  out as CLIST (measured JOB01212, member RXB).  R1 carries it.
         L     R2,4(,R1)          entry.stmt length -- before R1 moves
         L     R1,0(,R1)          entry.stmt address
         LTR   R1,R1
         BZ    CL1DONE
         C     R2,=F'2'
         BL    CL1DONE
         CLC   0(2,R1),SLASHST    must start with '/*'
         BNE   CL1DONE
*  Positions for a 4-byte compare are len-3, not len-4, and the old
*  BM let len=4 through with R2=0 -- BCT then wrapped to X'FFFFFFFF'
*  and scanned off the end of storage.
         S     R2,=F'3'           number of start positions
         BNP   CL1DONE
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
DDPROC   DC    CL8'SYSPROC'
BLANK8   DC    CL8' '
FCLOAD   DC    CL8'LOAD'
FCFREE   DC    CL8'FREE'
SLASHST  DC    C'/*'
REXXLIT  DC    C'REXX'
ZEROW    DC    F'0'
MODEC    EQU   1
MODEF    EQU   2
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
WINST    DS    A                  INSTBLK pointer (LOAD output)
WLVL     DS    3A                 load routine VLIST
WLP2     DS    A
WXVL     DS    10A                IRXEXEC VLIST
WXRC     DS    F
WLOADEP  DS    A                  load_routine entry (IRXEXTE)
WENV     DS    A                  ENVBLOCK from ECTENVBK
WHANDLED DS    X                  X'01' = the exec was run
WEXFL    DS    X                  X'01' = IRXEXEC is loaded
         DS    0F
WALEN    EQU   *-WAREA
*
         END
