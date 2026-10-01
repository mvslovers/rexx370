         TITLE 'IRXJCL - REXX/370 batch entry, thin (#299)'
*
*  IRXJCL - PGM=IRXJCL,PARM='member argument'.
*
*  A thin entry that drives the installed services instead of linking
*  the interpreter a second time:
*
*    1. split the PARM into member name and argument string,
*    2. find an environment (IRXINIT FINDENVB) or make one (INITENVB),
*    3. load the exec through the environment's load routine
*       (IRXEXTE load_routine, MODNAMET EXROUT -- IRXLDTSO),
*    4. run it: LOAD EP=IRXEXEC with the INSTBLK in P4,
*    5. FREE the INSTBLK, IRXTERM an environment of our own,
*    6. return IRXEXEC's R15 as the step's return code.
*
*  No C runtime: IRXINIT, IRXEXEC and the load routine run without
*  one, as they do under TSO.  The C-runtime IRXJCL lives on as the
*  lab module IRXJCLD (REXX370_BYTECODE / REXX370_BCDEBUG, TSTJCL).
*
*  Return codes (step CC is R15 mod 4096):
*    exec's EXIT value, or 20000+n after REXX error n (IRXEXEC)
*    20     the exec was not loaded or IRXEXEC is missing
*    28     no environment could be found or made
*    20021  the PARM names no valid member
*
*  PARM: R1 -> fullword -> halfword length + text (SC28-1883-0
*  Figure 9).  Leading blanks are skipped, the first word is the
*  member (1-8 characters, folded to upper case), and the argument
*  starts at the next non-blank after it; the rest goes unchanged.
*  R0 may carry an ENVBLOCK: used when CHEKENVB accepts it.
*
*  Messages for a load failure are the ones the C IRXJCL wrote:
*  IRX0406E (member not found) or IRX0005I (storage), then IRX0110I
*  and IRX0112I.  They take the route irx#emsg.c takes: WTO in a
*  non-TSO environment that allows it, else the environment's I/O
*  routine.  The I/O routine is C, so the call gets a stack pool of
*  its own, GETMAINed for the call only (PDP-DSA: DSANAB at +76).
*
*  NOTE for as370/IFOX00: RS-format LM/STM must be written D(B).
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
*  Offsets into control blocks (include/irx.h)
ENVPARMB EQU   16                 ENVBLOCK -> PARMBLOCK
ENVEXTE  EQU   28                 ENVBLOCK -> IRXEXTE
EXTELOAD EQU   8                  IRXEXTE  -> load_routine
EXTEIO   EQU   24                 IRXEXTE  -> io_routine
PBMODNT  EQU   16                 PARMBLOCK -> MODNAMET
PBFLAGS  EQU   36                 PARMBLOCK flags, 4 bytes
MNLOADDD EQU   16                 MODNAMET LOADDD (CL8)
FLTSO    EQU   X'80'              flags+0: TSOFL
FLNOWTO  EQU   X'08'              flags+2: NOMSGWTO
FLNOIO   EQU   X'04'              flags+2: NOMSGIO
RXFWRERR EQU   4                  I/O routine: write error message
LDNOMEM  EQU   4                  load routine: no storage
LDNOTFND EQU   8                  load routine: member not found
*
IRXJCL   CSECT
         STM   R14,R12,12(R13)
         BALR  R12,0
         USING *,R12
         LR    R11,R1             PARM list
         LR    R9,R0              ENVBLOCK, optional
*
         L     R0,=A(WALEN)
         GETMAIN RC,LV=(0)
         LTR   R15,R15
         BZ    GOTWA
         LA    R15,20             no storage for the workarea
         L     R14,12(,R13)
         LM    R0,R12,20(R13)
         BR    R14
GOTWA    ST    R13,4(,R1)
         ST    R1,8(,R13)
         LR    R13,R1
         USING WAREA,R13
         XC    WDFLAGS,WDFLAGS    PDP-DSA: no stack pool yet
         XC    WDLWA,WDLWA
         XC    WDNAB,WDNAB
         XC    WENV,WENV
         ST    R9,WR0
         XC    WINST,WINST
         XC    WARGP,WARGP
         XC    WXRC,WXRC
         MVI   WOWN,X'00'
         L     R0,=F'20021'
         ST    R0,WRC
*
* --- PARM: member and argument --------------------------------------
         LTR   R11,R11
         BZ    RETURN
         L     R2,0(,R11)
         LA    R2,0(,R2)          drop the VL bit
         LTR   R2,R2
         BZ    RETURN
         LH    R3,0(,R2)          text length
         LA    R4,2(,R2)          text
SKIPBL   LTR   R3,R3
         BNP   RETURN             empty: no member
         CLI   0(R4),C' '
         BNE   MEMBEG
         LA    R4,1(,R4)
         BCTR  R3,0
         B     SKIPBL
MEMBEG   CLI   0(R4),X'00'        sequential-mode PARM: not supported
         BE    RETURN
         LR    R5,R4              member start
         SR    R6,R6              member length
MEMLP    LTR   R3,R3
         BNP   MEMEND
         CLI   0(R4),C' '
         BE    MEMEND
         LA    R4,1(,R4)
         LA    R6,1(,R6)
         BCTR  R3,0
         B     MEMLP
MEMEND   CH    R6,=H'8'
         BH    RETURN             longer than a member name
         MVC   WMEMB,BLANKS
         BCTR  R6,0
         EX    R6,MVCMEMB         WMEMB <- member
         OC    WMEMB,BLANKS       EBCDIC: X'40' folds a-z to A-Z
*  R4/R3: the blank that ended the member (or the end).  The name is
*  "followed by one or more blanks, followed by the argument"
*  (SC28-1883-0 Figure 9), so the argument starts at the next
*  non-blank.
ARGSKIP  LTR   R3,R3
         BNP   NOARG
         CLI   0(R4),C' '
         BNE   ARGSET
         LA    R4,1(,R4)
         BCTR  R3,0
         B     ARGSKIP
ARGSET   DS    0H
         ST    R4,WARGT           ARGTABLE entry: address, length
         ST    R3,WARGT+4
         MVC   WARGT+8(8),FFS     end marker
         B     ARGDONE
NOARG    MVC   WARGT(8),FFS       no argument: end marker only
         MVC   WARGT+8(8),FFS
ARGDONE  LA    R0,WARGT
         ST    R0,WARGP
*
* --- environment: find one, else make one ---------------------------
         LA    R0,28
         ST    R0,WRC             from here on a failure is NOENV
         LOAD  EP=IRXINIT,ERRET=RETURN
         ST    R0,WEPA
*  Register 0 may name the environment (SC28-1883-0 p.215).  A valid
*  one is used; an invalid one is ignored and the current one found
*  (p.255, "Specifying the Address of the Environment Block").
         XC    WENVOUT,WENVOUT
         L     R0,WR0
         LTR   R0,R0
         BZ    FINDENV
         ST    R0,WENVOUT
         LA    R1,FCCHEK
         BAL   R10,CALLINIT
         LTR   R15,R15
         BZ    HAVEENV
         XC    WENVOUT,WENVOUT
FINDENV  LA    R1,FCFIND
         BAL   R10,CALLINIT
         LTR   R15,R15
         BZ    HAVEENV
         XC    WENVOUT,WENVOUT
         LA    R1,FCINIT
         BAL   R10,CALLINIT
         LTR   R15,R15
         BNZ   DELINIT
         MVI   WOWN,X'01'
HAVEENV  MVC   WENV,WENVOUT
DELINIT  DELETE EP=IRXINIT        nothing points into it (#256)
         L     R2,WENV
         LTR   R2,R2
         BZ    RETURN
*
* --- EXECBLK, V1 length: member, DD blank = LOADDD ------------------
         LA    R0,20
         ST    R0,WRC             from here on a failure is ERROR
         XC    WEXECB,WEXECB
         MVC   WEXECB(8),EXECBEYE
         LA    R0,48
         ST    R0,WEXECB+8
         MVC   WEXECB+16(8),WMEMB
         MVC   WEXECB+24(16),BLANKS  DD and SUBCOM blank
*
* --- load it through the environment's load routine -----------------
         L     R1,ENVEXTE(,R2)
         LTR   R1,R1
         BZ    LOADFAIL
         L     R1,EXTELOAD(,R1)
         LTR   R1,R1
         BZ    LOADFAIL
         ST    R1,WLDEP
         LA    R1,FCLOAD
         ST    R1,WVL+0           P1 function code
         LA    R1,WEXECB
         ST    R1,WLP2
         LA    R1,WLP2
         ST    R1,WVL+4           P2 -> EXECBLK
         LA    R1,WINST
         O     R1,=X'80000000'
         ST    R1,WVL+8           P3 INSTBLK out, VL
         LR    R0,R2
         LA    R1,WVL
         L     R15,WLDEP
         BALR  R14,R15
         LTR   R15,R15
         BNZ   LOADRC
         L     R1,WINST
         LTR   R1,R1
         BZ    LOADFAIL
*
* --- run it ----------------------------------------------------------
         LOAD  EP=IRXEXEC,ERRET=NOEXEC
         ST    R0,WEPA
         LA    R1,ZEROW
         ST    R1,WVL+0           P1 EXECBLK (none; P4 carries it)
         LA    R1,WARGP
         ST    R1,WVL+4           P2 ARGTABLE
         LA    R1,ZEROW
         ST    R1,WVL+8           P3 flags: COMMAND
         LA    R1,WINST
         ST    R1,WVL+12          P4 INSTBLK
         LA    R1,ZEROW
         ST    R1,WVL+16          P5 reserved
         ST    R1,WVL+20          P6 EVALBLOCK: none
         ST    R1,WVL+24          P7 work area
         ST    R1,WVL+28          P8 user field
         LA    R1,WENV
         ST    R1,WVL+32          P9 ENVBLOCK
         LA    R1,WXRC
         O     R1,=X'80000000'
         ST    R1,WVL+36          P10 REXX RC, VL
         L     R0,WENV
         LA    R1,WVL
         L     R15,WEPA
         BALR  R14,R15
         ST    R15,WRC            the exec's RC, or 20000+n
         DELETE EP=IRXEXEC
         B     FREEINST
*
NOEXEC   LA    R2,M0110
         LA    R3,L'M0110
         BAL   R10,SYSMSG
*
* --- FREE the INSTBLK ------------------------------------------------
FREEINST LA    R1,FCFREE
         ST    R1,WVL+0
         LA    R1,ZEROW
         ST    R1,WVL+4
         LA    R1,WINST
         O     R1,=X'80000000'
         ST    R1,WVL+8
         L     R0,WENV
         LA    R1,WVL
         L     R15,WLDEP
         BALR  R14,R15
         B     TERM
*
* --- the exec could not be loaded ------------------------------------
LOADRC   C     R15,=A(LDNOTFND)
         BE    NOTFOUND
         C     R15,=A(LDNOMEM)
         BNE   LOADFAIL
         LA    R2,M0005           IRX0005I, as a REXX error message
         LA    R3,L'M0005
         BAL   R10,ERRMSG
         B     LOADFAIL
NOTFOUND BAL   R10,MSG0406
LOADFAIL LA    R2,M0110
         LA    R3,L'M0110
         BAL   R10,SYSMSG
         LA    R2,M0112
         LA    R3,L'M0112
         BAL   R10,SYSMSG
*
* --- IRXTERM an environment of our own -------------------------------
TERM     CLI   WOWN,X'00'
         BE    RETURN
         LOAD  EP=IRXTERM,ERRET=RETURN
         LR    R15,R0
         L     R0,WENV
         BALR  R14,R15
         DELETE EP=IRXTERM
*
RETURN   L     R2,WRC             survives FREEMAIN (R0/R1/R15 only)
         LR    R1,R13
         L     R13,4(,R13)
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(R1)
         LR    R15,R2
         L     R14,12(,R13)
         LM    R0,R12,20(R13)
         BR    R14
*
* ---------------------------------------------------------------------
*  CALLINIT - IRXINIT with the function code R1 points at.  The slots
*  hold ADDRESSES of fullwords, never the values (IKJEFTRX).
*  In: R1 -> CL8 function code, WENVOUT = the ENVBLOCK to check
*  (CHEKENVB) or 0.  Out: R15, WENVOUT.  Link R10.
* ---------------------------------------------------------------------
CALLINIT ST    R1,WVL+0           P1 function code
         LA    R1,BLANKS
         ST    R1,WVL+4           P2 parameters module: default
         LA    R1,ZEROW
         ST    R1,WVL+8           P3 in-storage parameter list
         ST    R1,WVL+12          P4 user field
         ST    R1,WVL+16          P5 reserved
         LA    R1,WENVOUT
         ST    R1,WVL+20          P6 out: ENVBLOCK
         LA    R1,WREASON
         O     R1,=X'80000000'
         ST    R1,WVL+24          P7 out: reason, VL
         SR    R0,R0
         LA    R1,WVL
         L     R15,WEPA
         BALR  R14,R15
         BR    R10
*
* ---------------------------------------------------------------------
*  MSG0406 - IRX0406E REXX exec load file <dd> does not contain exec
*  member <member>.  <dd> is the MODNAMET LOADDD, SYSEXEC when blank
*  (irx_load_loaddd).  Link R10.
* ---------------------------------------------------------------------
MSG0406  ST    R10,WLINK2
         LA    R4,WTEXT
         MVC   0(L'T0406A,R4),T0406A
         LA    R4,L'T0406A(,R4)
         LA    R5,DDDEF           SYSEXEC unless the MODNAMET says
         L     R1,WENV
         L     R1,ENVPARMB(,R1)
         LTR   R1,R1
         BZ    MSGDD
         L     R1,PBMODNT(,R1)
         LTR   R1,R1
         BZ    MSGDD
         CLC   MNLOADDD(8,R1),BLANKS
         BE    MSGDD
         LA    R5,MNLOADDD(,R1)
MSGDD    BAL   R11,CPYTRIM        R5 CL8 -> R4, blanks dropped
         MVC   0(L'T0406B,R4),T0406B
         LA    R4,L'T0406B(,R4)
         LA    R5,WMEMB
         BAL   R11,CPYTRIM
         MVI   0(R4),C'.'
         LA    R4,1(,R4)
         LA    R3,WTEXT
         SR    R4,R3
         LR    R3,R4
         LA    R2,WTEXT
         BAL   R10,SYSMSG
         L     R10,WLINK2
         BR    R10
*
*  CPYTRIM - copy CL8 at R5 to R4 without trailing blanks, advance R4.
CPYTRIM  LA    R6,8
CPYLP    LA    R7,0(R6,R5)
         BCTR  R7,0
         CLI   0(R7),C' '
         BNE   CPYGO
         BCT   R6,CPYLP
         BR    R11                all blank: nothing
CPYGO    BCTR  R6,0
         EX    R6,MVCTRIM
         LA    R4,1(R6,R4)
         BR    R11
*
* ---------------------------------------------------------------------
*  SYSMSG - a system message (irx_emsg_system): WTO in a non-TSO
*  environment without NOMSGWTO, else the I/O routine.
*  ERRMSG - a REXX error message (irx_emsg_syntax): the I/O routine
*  unless a non-TSO environment says NOMSGIO, and WTO unless TSO or
*  NOMSGWTO.
*  In: R2 -> text without the IRX prefix, R3 = its length.  Link R10.
* ---------------------------------------------------------------------
SYSMSG   MVI   WROUTE,X'00'       X'01' WTO, X'02' I/O routine
         BAL   R11,MSGFLAGS
         TM    WMFL,FLTSO
         BO    SYSIO
         TM    WMFL+2,FLNOWTO
         BO    SYSIO
         MVI   WROUTE,X'01'
         B     MSGOUT
SYSIO    MVI   WROUTE,X'02'
         B     MSGOUT
*
ERRMSG   MVI   WROUTE,X'00'
         BAL   R11,MSGFLAGS
         TM    WMFL,FLTSO
         BO    ERRIO
         TM    WMFL+2,FLNOIO
         BO    ERRWTO
ERRIO    OI    WROUTE,X'02'
ERRWTO   TM    WMFL,FLTSO
         BO    MSGOUT
         TM    WMFL+2,FLNOWTO
         BO    MSGOUT
         OI    WROUTE,X'01'
*
*  MSGOUT - "IRX" + text into WMSGT, then the routes WROUTE names.
MSGOUT   MVC   WMSGT(3),=C'IRX'
         BCTR  R3,0
         EX    R3,MVCTEXT
         LA    R3,4(,R3)          3 for IRX, 1 back from the BCTR
         ST    R3,WMSGL
         TM    WROUTE,X'01'
         BZ    MSGIO
         LA    R1,4(,R3)          WTO list: length incl. the header
         STH   R1,WWTOL
         XC    WWTOL+2(2),WWTOL+2 MCS flags: none
         LA    R1,WWTOL
         SVC   35
MSGIO    TM    WROUTE,X'02'
         BZR   R10
         L     R1,WENV
         LTR   R1,R1
         BZR   R10
         L     R1,ENVEXTE(,R1)
         LTR   R1,R1
         BZR   R10
         L     R15,EXTEIO(,R1)
         LTR   R15,R15
         BZR   R10
         ST    R15,WIOEP
*  The I/O routine is c2asm370 code: its prologue takes its frame from
*  DSANAB, so give it a pool for this call.
         L     R0,=A(IOPOOLL)
         GETMAIN RC,LV=(0)
         LTR   R15,R15
         BNZR  R10                no storage: no message either
         ST    R1,WDNAB
         LA    R1,WMSGT
         ST    R1,WLSTR+0         Lstr pstr
         L     R1,WMSGL
         ST    R1,WLSTR+4         len
         ST    R1,WLSTR+8         maxlen
         XC    WLSTR+12(4),WLSTR+12  type LSTRING_TY = 0
         LA    R1,RXFWRERR
         ST    R1,WCPL+0          (int function, PLstr, envblock *)
         LA    R1,WLSTR
         ST    R1,WCPL+4
         L     R1,WENV
         ST    R1,WCPL+8
         LA    R1,WCPL
         L     R15,WIOEP
         BALR  R14,R15
         L     R1,WDNAB
         L     R0,=A(IOPOOLL)
         FREEMAIN RU,LV=(0),A=(R1)
         XC    WDNAB,WDNAB
         BR    R10
*
*  MSGFLAGS - PARMBLOCK flags of the environment into WMFL (zero
*  without one: a non-TSO environment with every route open).
MSGFLAGS XC    WMFL,WMFL
         L     R1,WENV
         LTR   R1,R1
         BZR   R11
         L     R1,ENVPARMB(,R1)
         LTR   R1,R1
         BZR   R11
         MVC   WMFL,PBFLAGS(R1)
         BR    R11
*
* --- EX targets ------------------------------------------------------
MVCMEMB  MVC   WMEMB(0),0(R5)
MVCTRIM  MVC   0(0,R4),0(R5)
MVCTEXT  MVC   WMSGT+3(0),0(R2)
*
* --- read-only static data (RENT) ------------------------------------
FCCHEK   DC    CL8'CHEKENVB'
FCFIND   DC    CL8'FINDENVB'
FCINIT   DC    CL8'INITENVB'
FCLOAD   DC    CL8'LOAD'
FCFREE   DC    CL8'FREE'
EXECBEYE DC    CL8'IRXEXECB'
DDDEF    DC    CL8'SYSEXEC'
BLANKS   DC    CL16' '
FFS      DC    8X'FF'
ZEROW    DC    F'0'
T0406A   DC    C'0406E REXX exec load file '
T0406B   DC    C' does not contain exec member '
M0005    DC    C'0005I Machine storage exhausted'
M0110    DC    C'0110I The REXX exec cannot be interpreted.'
M0112    DC    C'0112I The REXX exec cannot be loaded.'
         LTORG
*
*  The I/O routine's stack for one call.  IRXINOUT writes through
*  QSAM (asm/irxqsam.asm) and keeps its frames small; 16 KB leaves
*  room, and it is held only while the message is written.
IOPOOLL  EQU   16384
*
* --- workarea, PDP-DSA shape (asm/irxexec.asm) -----------------------
WAREA    DSECT
WDFLAGS  DS    F                  +0  DSAFLAGS (0)
WDPREV   DS    F                  +4  back chain
WDNEXT   DS    F                  +8  forward chain
         DS    15F                +12 caller R14-R12
WDLWA    DS    F                  +72 DSALWA (0)
WDNAB    DS    F                  +76 DSANAB: I/O routine pool
WRC      DS    F                  return code for the step
WR0      DS    A                  R0 on entry: ENVBLOCK, optional
WENV     DS    A                  ENVBLOCK
WENVOUT  DS    A                  IRXINIT out: ENVBLOCK
WREASON  DS    F                  IRXINIT out: reason
WEPA     DS    A                  IRXINIT / IRXEXEC entry
WLDEP    DS    A                  load routine entry
WIOEP    DS    A                  I/O routine entry
WINST    DS    A                  INSTBLK from the load routine
WARGP    DS    A                  -> ARGTABLE
WXRC     DS    F                  IRXEXEC P10
WLP2     DS    A                  load routine P2: -> EXECBLK
WLINK2   DS    A                  MSG0406 return
WVL      DS    10A                parameter lists
WCPL     DS    3A                 I/O routine C parameter list
WLSTR    DS    4F                 Lstr: pstr, len, maxlen, type
WMSGL    DS    F                  message length incl. IRX
WMFL     DS    XL4                PARMBLOCK flags
WOWN     DS    X                  X'01' IRXTERM at the end
WROUTE   DS    X                  message routes
WMEMB    DS    CL8                member, upper case
         DS    0F
WARGT    DS    XL16               ARGTABLE: one entry + end marker
WEXECB   DS    XL48               EXECBLK (V1)
WWTOL    DS    H                  WTO list: length, MCS flags, text
         DS    H
WMSGT    DS    CL124              IRX + message text
WTEXT    DS    CL96               IRX0406E text under construction
WALEN    EQU   *-WAREA
*
         END   IRXJCL
