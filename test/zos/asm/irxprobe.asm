         TITLE 'IRXPROBE - REXX/370 ECTENVBK Behaviour Probe (z/OS)'
*
**********************************************************************
*                                                                    *
*  IRXPROBE - Research tool for measuring IBM TSO/E REXX behaviour   *
*             of IRXINIT / IRXTERM / IRXEXEC against ECTENVBK and    *
*             the IRXANCHR registry on z/OS.                         *
*                                                                    *
*  Phase alpha: capture authoritative IBM reference logs for the     *
*  CON-3 Open Questions Q-INIT-1..4, Q-TERM-1, Q-EXEC-1.             *
*                                                                    *
*  Target system: z/OS only.  HLASM directives (AMODE/RMODE) and     *
*  z/OS-only services (LINK, LOAD via IRXANCHR module) are used      *
*  freely.  Phase beta will port a subset to MVS 3.8j (IFOX00) in    *
*  a separate ticket.                                                *
*                                                                    *
*  Invocation:                                                       *
*    CALL 'hlq.LOAD(IRXPROBE)' 'subcommand [args]'                    *
*    ADDRESS LINKMVS "IRXPROBE subcommand [args]"   (from REXX)       *
*                                                                    *
*  Subcommands:                                                       *
*    DUMP         Dump ECTENVBK + IRXANCHR + ENVBLOCK + PARMBLOCK     *
*    INIT         IRXINIT INITENVB, TSOFL=1, no caller-prev           *
*    INITP  hex   IRXINIT INITENVB, R0 = hex env address (prev)       *
*    INITNT       IRXINIT INITENVB, TSOFL=0                           *
*    TERM   hex   IRXTERM, R0 = hex env address                       *
*    EXEC   hex   IRXEXEC stub (env address argument, see notes)      *
*    MARK   text  Write '== text ==' marker line                      *
*                                                                    *
*  Output: SYSPRINT DD (RECFM=FBA, LRECL=121).                        *
*  Driver REXX execs (test/zos/rexx/proba*.rex) orchestrate the       *
*  case sequences and concatenate logs into a master record.         *
*                                                                    *
*  Return codes:                                                      *
*     0  success                                                      *
*     4  warning (unknown subcommand, parse error)                    *
*     8  error  (service call failed; see SYSPRINT for RC/RSN)        *
*                                                                    *
*  (c) 2026 mvslovers - REXX/370 Project                              *
*                                                                    *
**********************************************************************
*
IRXPROBE CSECT
IRXPROBE AMODE 31
IRXPROBE RMODE 24
*
*  Register equates (HLASM has no built-in R0..R15)
*
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
*
         SAVE  (14,12),,*
         LR    R12,R15
         LA    R11,2048(,R12)         second base ...
         LA    R11,2048(,R11)         ... R11 = R12 + 4096
         USING IRXPROBE,R12,R11       8 KB of addressability
         LR    R2,R1                  preserve param-list pointer
         LA    R15,SAVEAR1
         ST    R13,4(R15)
         ST    R15,8(R13)
         LR    R13,R15
*
*  Open SYSPRINT
*
         OPEN  (SYSPRINT,OUTPUT)
         TM    SYSPRINT+48,X'10'      DCB OFLAG bit OPEN?
         BO    OPENED1
         WTO   'IRXPROBE: SYSPRINT OPEN failed',ROUTCDE=11
         LA    R15,8
         B     EXIT
OPENED1  DS    0H
*
*  Banner
*
         BAL   R14,WBANNER
*
*  Build CMDBUF from the caller's parameter area, supporting both
*  TSO CALL and REXX ADDRESS LINKMVS conventions.  Detection is by
*  threshold on the halfword at R2+0:
*
*    halfword < 256  -> TSO CALL    (R1 -> halfword len + text)
*    halfword >= 256 -> LINKMVS     (R1 -> array of descriptor addrs)
*
*  This works on z/OS because LINKMVS descriptors live in REXX's
*  above-the-line storage, so any descriptor address starts at
*  X'01000000' or higher (halfword >= 256).  TSO CALL parm length
*  is bounded by ~120 bytes in practice, well under 256.
*
         BAL   R14,BUILDCMD
*
*  Initialise dispatch loop state
*
         XC    WORK_FINRC,WORK_FINRC
         XC    LASTENV,LASTENV
         LA    R3,CMDBUF
         ST    R3,CMDPTR
         L     R4,CMDLEN
         LA    R5,0(R4,R3)             R5 = CMDBUF + length
         ST    R5,CMDEND
*
*  Segment loop: extract subcmd (and optional args) from CMDBUF,
*  dispatch, emit a segment marker, repeat until end of buffer.
*  Subcommands are delimited by ';'.
*
SEGLOOP  L     R3,CMDPTR
         L     R5,CMDEND
*
*  Skip leading separators (blanks and ';')
*
SKIPSEP  CR    R3,R5
         BNL   ALLDONE
         CLI   0(R3),C' '
         BE    SKIPADV
         CLI   0(R3),C';'
         BE    SKIPADV
         B     SKIPDONE
SKIPADV  LA    R3,1(,R3)
         B     SKIPSEP
SKIPDONE DS    0H
*
*  Extract subcommand (first token in segment) into SUBCMD,
*  uppercased and blank-padded to CL8.
*
         MVC   SUBCMD,BLANK80
         LA    R6,SUBCMD
         LA    R7,8                    max 8 chars
SUBSCAN  CR    R3,R5
         BNL   SUBEND
         CLI   0(R3),C' '
         BE    SUBEND
         CLI   0(R3),C';'
         BE    SUBEND
         LTR   R7,R7
         BZ    SUBOVER                 over 8 chars: skip rest
         MVC   0(1,R6),0(R3)
         OI    0(R6),X'40'             EBCDIC uppercase mask
         LA    R6,1(,R6)
         LA    R3,1(,R3)
         BCTR  R7,0
         B     SUBSCAN
SUBOVER  CR    R3,R5
         BNL   SUBEND
         CLI   0(R3),C' '
         BE    SUBEND
         CLI   0(R3),C';'
         BE    SUBEND
         LA    R3,1(,R3)
         B     SUBOVER
SUBEND   DS    0H
*
*  Extract args (rest of segment until ';' or end) into ARGBUF.
*
         MVC   ARGBUF(L'ARGBUF),BLANK80
*
*  Skip blanks between subcmd and args
*
ARGSKIP  CR    R3,R5
         BNL   ARGREADY
         CLI   0(R3),C' '
         BNE   ARGREADY
         LA    R3,1(,R3)
         B     ARGSKIP
ARGREADY DS    0H
         LA    R6,ARGBUF
         LA    R7,L'ARGBUF
ARGSCAN  CR    R3,R5
         BNL   ARGEND
         CLI   0(R3),C';'
         BE    ARGEND
         LTR   R7,R7
         BZ    ARGOVER
         MVC   0(1,R6),0(R3)
         LA    R6,1(,R6)
         LA    R3,1(,R3)
         BCTR  R7,0
         B     ARGSCAN
ARGOVER  CR    R3,R5
         BNL   ARGEND
         CLI   0(R3),C';'
         BE    ARGEND
         LA    R3,1(,R3)
         B     ARGOVER
ARGEND   DS    0H
*
*  Step past the ';' separator if present
*
         CR    R3,R5
         BNL   POSTSEP
         CLI   0(R3),C';'
         BNE   POSTSEP
         LA    R3,1(,R3)
POSTSEP  ST    R3,CMDPTR
*
*  Emit segment marker:  '== <subcmd> =='
*
         MVC   LINETXT(120),BLANK120
         MVI   LINETXT,C'='
         MVI   LINETXT+1,C'='
         MVC   LINETXT+3(8),SUBCMD
         MVI   LINETXT+12,C'='
         MVI   LINETXT+13,C'='
         BAL   R14,WLINE
*
*  Dispatch on SUBCMD; each handler returns via BR R14 (eventually
*  through HANDRET).
*
         CLC   SUBCMD,=CL8'DUMP'
         BE    GO_DUMP
         CLC   SUBCMD,=CL8'INIT'
         BE    GO_INIT
         CLC   SUBCMD,=CL8'INITP'
         BE    GO_INITP
         CLC   SUBCMD,=CL8'INITNT'
         BE    GO_INITNT
         CLC   SUBCMD,=CL8'TERM'
         BE    GO_TERM
         CLC   SUBCMD,=CL8'TERM_LAS'
         BE    GO_TERMLA
         CLC   SUBCMD,=CL8'EXEC'
         BE    GO_EXEC
         CLC   SUBCMD,=CL8'MARK'
         BE    GO_MARK
         CLC   SUBCMD,BLANK80
         BE    GO_HELP
*
         MVC   LINETXT(120),=CL120'?? unknown subcommand:'
         MVC   LINETXT+23(8),SUBCMD
         BAL   R14,WLINE
         LA    R15,4
         B     UPDRC
*
GO_DUMP   BAL  R14,DODUMP
         B     UPDRC
GO_INIT   BAL  R14,DOINIT
         B     UPDRC
GO_INITP  BAL  R14,DOINITP
         B     UPDRC
GO_INITNT BAL  R14,DOINITNT
         B     UPDRC
GO_TERM   BAL  R14,DOTERM
         B     UPDRC
GO_TERMLA BAL  R14,DOTERMLA
         B     UPDRC
GO_EXEC   BAL  R14,DOEXEC
         B     UPDRC
GO_MARK   BAL  R14,DOMARK
         B     UPDRC
GO_HELP   BAL  R14,DOHELP
         B     UPDRC
*
*  UPDRC: track max RC across segments; loop to next segment.
*
UPDRC    L     R3,WORK_FINRC
         CR    R15,R3
         BNH   UPDRCLP
         ST    R15,WORK_FINRC
UPDRCLP  B     SEGLOOP
*
*  ALLDONE: end of CMDBUF reached, exit with accumulated RC.
*
ALLDONE  L     R15,WORK_FINRC
         B     EXIT
*
**********************************************************************
*  HANDRET - common return path for handlers.  Updates the running    *
*           max RC in WORK_FINRC, then returns to the dispatch loop  *
*           via the saved R14 (WORK_R14D).                           *
**********************************************************************
*
HANDRET  L     R3,WORK_FINRC
         CR    R15,R3
         BNH   HRSAME
         ST    R15,WORK_FINRC
HRSAME   L     R14,WORK_R14D
         BR    R14
*
**********************************************************************
*  HELP                                                              *
**********************************************************************
*
DOHELP   ST    R14,WORK_R14D
         BAL   R14,WHELP
         LA    R15,0
         B     HANDRET
*
**********************************************************************
*  MARK text - emit a free-text marker line                          *
**********************************************************************
*
DOMARK   ST    R14,WORK_R14D
         MVC   LINETXT(120),=CL120'-- '
         MVC   LINETXT+3(L'ARGBUF),ARGBUF
         BAL   R14,WLINE
         LA    R15,0
         B     HANDRET
*
**********************************************************************
*  DUMP - read-only inspection of ECTENVBK, IRXANCHR, ENVBLOCK,      *
*         PARMBLOCK.                                                  *
**********************************************************************
*
DODUMP   ST    R14,WORK_R14D
*
*  Walk PSA -> ASCB -> ASXB -> LWA -> ECT
*
         L     R3,X'224'              R3 = ASCB (PSAAOLD)
         ST    R3,WORK_ASCB
*
         LTR   R3,R3
         BZ    NOECT
         L     R4,X'6C'(,R3)          R4 = ASXB (ASCBASXB)
         ST    R4,WORK_ASXB
         LTR   R4,R4
         BZ    NOECT
         L     R5,X'14'(,R4)          R5 = LWA (ASXBLWA)
         ST    R5,WORK_LWA
         LTR   R5,R5
         BZ    NOLWA
         L     R6,X'20'(,R5)          R6 = ECT (LWAPECT)
         ST    R6,WORK_ECT
         LTR   R6,R6
         BZ    NOECT
*
         L     R7,X'30'(,R6)          R7 = ECTENVBK
         ST    R7,WORK_ENVB
*
         MVC   LBLBUF(20),=CL20'  ECT-Address'
         L     R1,WORK_ECT
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'  ECTENVBK'
         L     R1,WORK_ENVB
         BAL   R14,WKVHEX
         B     DUMPANCH
*
NOLWA    MVC   LINETXT(120),=CL120'  LWA = NULL  (pure batch?)'
         BAL   R14,WLINE
         B     DUMPANCH
*
NOECT    MVC   LINETXT(120),=CL120'  ECT = NULL  (chain unreachable)'
         BAL   R14,WLINE
*
DUMPANCH DS    0H
*
*  LOAD EP=IRXANCHR; dump first slots
*
         LOAD  EP=IRXANCHR,ERRET=NOANCH
         LR    R8,R0                  R8 = IRXANCHR address
         ST    R8,WORK_ANCH
         MVC   LBLBUF(20),=CL20'  IRXANCHR'
         LR    R1,R8
         BAL   R14,WKVHEX
         BAL   R14,WANCHHDR
         BAL   R14,WANCHSLT
         DELETE EP=IRXANCHR
         B     DUMPENV
*
NOANCH   MVC   LINETXT(120),=CL120'  IRXANCHR = (LOAD failed)'
         BAL   R14,WLINE
*
DUMPENV  DS    0H
         L     R7,WORK_ENVB
         LTR   R7,R7
         BZ    DUMPDONE
         BAL   R14,WENV
         L     R8,X'10'(,R7)          R8 = PARMBLOCK
         ST    R8,WORK_PARM
         LTR   R8,R8
         BZ    DUMPDONE
         BAL   R14,WPARM
*
DUMPDONE LA    R15,0
         B     HANDRET
*
**********************************************************************
*  INIT [module] - IRXINIT INITENVB; default module IRXTSPRM (TSO)   *
**********************************************************************
*
DOINIT   ST    R14,WORK_R14D
         MVC   FCODE,=CL8'INITENVB'
         MVC   PARMODE,=CL8'IRXTSPRM'
         BAL   R14,SETPMOD            ARGBUF override (if non-blank)
         XC    PARMP,PARMP
         XC    USERP,USERP
         XC    PREVP,PREVP            R0 input = 0 (no prev)
         BAL   R14,DOIRXINIT
         LA    R15,0
         B     HANDRET
*
**********************************************************************
*  INITP hex - IRXINIT INITENVB, R0 = hex env address (prev),         *
*              module fixed to IRXTSPRM                              *
**********************************************************************
*
DOINITP  ST    R14,WORK_R14D
         BAL   R14,PARSEHEX           ARGBUF -> WORK_TMPA
         LTR   R15,R15
         BNZ   ARGERR
         MVC   FCODE,=CL8'INITENVB'
         MVC   PARMODE,=CL8'IRXTSPRM'
         XC    PARMP,PARMP
         XC    USERP,USERP
         MVC   PREVP,WORK_TMPA
         BAL   R14,DOIRXINIT
         LA    R15,0
         B     HANDRET
*
**********************************************************************
*  INITNT [module] - IRXINIT INITENVB; default module IRXPARMS       *
*                    (non-TSO defaults)                              *
**********************************************************************
*
DOINITNT ST    R14,WORK_R14D
         MVC   FCODE,=CL8'INITENVB'
         MVC   PARMODE,=CL8'IRXPARMS'
         BAL   R14,SETPMOD            ARGBUF override (if non-blank)
         XC    PARMP,PARMP
         XC    USERP,USERP
         XC    PREVP,PREVP
         BAL   R14,DOIRXINIT
         LA    R15,0
         B     HANDRET
*
**********************************************************************
*  SETPMOD - if ARGBUF[0] is non-blank, copy first 8 chars (uppercase,*
*            blank-padded) into PARMODE, overriding the default.     *
**********************************************************************
*
SETPMOD  DS    0H
         CLI   ARGBUF,C' '             ARGBUF blank?
         BER   R14                     yes -> keep default, return
*
         ST    R14,WORK_R14H
         MVC   PARMODE,=CL8' '         clear, will be overlaid
         LA    R3,ARGBUF
         LA    R5,PARMODE
         LA    R7,8
SPMCPY   CLI   0(R3),C' '
         BE    SPMD
         MVC   0(1,R5),0(R3)
         OI    0(R5),X'40'             EBCDIC uppercase mask
         LA    R3,1(,R3)
         LA    R5,1(,R5)
         BCT   R7,SPMCPY
SPMD     L     R14,WORK_R14H
         BR    R14
*
**********************************************************************
*  TERM hex - IRXTERM with the hex env address from ARGBUF.          *
**********************************************************************
*
DOTERM   ST    R14,WORK_R14D
         BAL   R14,PARSEHEX
         LTR   R15,R15
         BNZ   ARGERR
         BAL   R14,DOIRXTERM
         LA    R15,0
         B     HANDRET
*
**********************************************************************
*  TERM_LAST - IRXTERM on the env that the most-recent successful    *
*              INIT/INITP/INITNT in this same invocation produced.   *
*              State is carried in LASTENV.  Empty -> diagnostic.    *
**********************************************************************
*
DOTERMLA ST    R14,WORK_R14D
         MVC   LBLBUF(20),=CL20'  lastenv'
         L     R1,LASTENV
         BAL   R14,WKVHEX
         L     R1,LASTENV
         LTR   R1,R1
         BZ    TLNONE
         ST    R1,WORK_TMPA
         BAL   R14,DOIRXTERM
         LA    R15,0
         B     HANDRET
TLNONE   MVC   LINETXT(120),=CL120'  ?? no prior INIT in this run'
         BAL   R14,WLINE
         LA    R15,4
         B     HANDRET
*
**********************************************************************
*  DOIRXTERM - shared IRXTERM call.  Caller has placed the env       *
*              address in WORK_TMPA.  Returns via BR R14 (caller's   *
*              return slot is its own; this routine uses WORK_R14T). *
**********************************************************************
*
DOIRXTERM ST   R14,WORK_R14T
         MVC   LBLBUF(20),=CL20'  arg envblock'
         L     R1,WORK_TMPA
         BAL   R14,WKVHEX
*
         BAL   R14,READECT
         MVC   LBLBUF(20),=CL20'  pre  ECTENVBK'
         L     R1,WORK_ENVB
         BAL   R14,WKVHEX
*
         LOAD  EP=IRXTERM,ERRET=NOIRXTM
         LR    R3,R0                  R3 = IRXTERM entry-point addr
*
         L     R0,WORK_TMPA           env address
         LR    R15,R3
         BALR  R14,R15                CALL IRXTERM
         ST    R15,WORK_RC
*
         DELETE EP=IRXTERM
*
         BAL   R14,READECT
         MVC   LBLBUF(20),=CL20'  post ECTENVBK'
         L     R1,WORK_ENVB
         BAL   R14,WKVHEX
*
         MVC   LBLBUF(20),=CL20'  IRXTERM RC'
         L     R1,WORK_RC
         BAL   R14,WKVDEC
*
         L     R14,WORK_R14T
         BR    R14
*
**********************************************************************
*  EXEC - IRXEXEC stub.  Calling IRXEXEC requires an EXECBLK with    *
*         an EXECNAME pointing at the exec dataset member, plus an   *
*         ARGTABLE and EVALBLOCK.  This stub does the minimum to    *
*         exercise the linkage; full implementation tracked in the   *
*         follow-up to issue #74.                                    *
**********************************************************************
*
DOEXEC   ST    R14,WORK_R14D
         BAL   R14,PARSEHEX
         LTR   R15,R15
         BNZ   ARGERR
         MVC   LBLBUF(20),=CL20'  arg envblock'
         L     R1,WORK_TMPA
         BAL   R14,WKVHEX
         MVC   LINETXT(120),=CL120'  status = NOT_IMPLEMENTED'
         BAL   R14,WLINE
         MVC   LINETXT(120),=CL120'  see test/zos/README.md case A7'
         BAL   R14,WLINE
         LA    R15,0
         B     HANDRET
*
**********************************************************************
*  ARGERR - bad hex argument; consumes the segment, RC=4             *
**********************************************************************
*
ARGERR   MVC   LINETXT(120),=CL120'  ?? bad hex argument: '
         MVC   LINETXT+24(40),ARGBUF
         BAL   R14,WLINE
         LA    R15,4
         B     HANDRET
*
**********************************************************************
*  NOIRXIN / NOIRXTM - LOAD EP= failure handlers; both abort the     *
*                      current segment with RC=8 and return to the   *
*                      dispatch loop so the rest of the sequence     *
*                      still runs.                                   *
**********************************************************************
*
NOIRXIN  MVC   LINETXT(120),=CL120'  ?? LOAD EP=IRXINIT failed'
         BAL   R14,WLINE
         LA    R15,8
         B     HANDRET
*
NOIRXTM  MVC   LINETXT(120),=CL120'  ?? LOAD EP=IRXTERM failed'
         BAL   R14,WLINE
         LA    R15,8
         B     HANDRET
*
**********************************************************************
*  EXIT - close SYSPRINT, restore regs, return                       *
**********************************************************************
*
EXIT     DS    0H
         ST    R15,WORK_FINRC
         CLOSE (SYSPRINT)
         L     R13,4(,R13)
         L     R14,12(,R13)
         L     R15,WORK_FINRC
         LM    R0,R12,20(R13)
         BR    R14
*
**********************************************************************
*  DOIRXINIT - common IRXINIT INITENVB call sequence                 *
*                                                                    *
*    Inputs:  FCODE   8-byte function code ('INITENVB')              *
*             PARMODE 8-byte parameter-module name (CL8, blank-pad)  *
*             PARMP   address of caller PARMBLOCK or 0               *
*             USERP   user field (fullword)                          *
*             PREVP   value to load into R0 (caller-prev) or 0       *
*    Output:  WORK_NEWE   new ENVBLOCK address                       *
*             WORK_RSN    reason code                                *
*             WORK_RC     RC from IRXINIT                            *
*             prints  pre/post ECTENVBK and the output values        *
*                                                                    *
*  IRXINIT INITENVB parameter list per CON-1 §3.x / SC28-1883-0:     *
*    P1  function code           (CL8)                               *
*    P2  parameter module name   (CL8, blank = use system default)   *
*    P3  caller PARMBLOCK        (A, 0 = use module-supplied)        *
*    P4  user field              (F)                                 *
*    P5  reserved — addr of fullword zero (must be non-NULL)         *
*    P6  out: ENVBLOCK address   (A)                                 *
*    P7  out: reason code        (F)                                 *
**********************************************************************
*
DOIRXINIT DS   0H
         ST    R14,WORK_R14R
*
         BAL   R14,READECT
         MVC   LBLBUF(20),=CL20'  pre  ECTENVBK'
         L     R1,WORK_ENVB
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'  module'
         LA    R1,PARMODE
         BAL   R14,WKVTXT8M
*
         XC    WORK_NEWE,WORK_NEWE
         XC    WORK_RSN,WORK_RSN
*
*  Build IRXINIT parameter list (high-bit set on P7 only).
*
         LA    R3,FCODE
         ST    R3,PLIST+0          P1: function code
         LA    R3,PARMODE
         ST    R3,PLIST+4          P2: parameter module name
         LA    R3,PARMP
         ST    R3,PLIST+8          P3: caller PARMBLOCK (or 0)
         LA    R3,USERP
         ST    R3,PLIST+12         P4: user field
         LA    R3,RESVZ
         ST    R3,PLIST+16         P5: addr of reserved zero
         LA    R3,WORK_NEWE
         ST    R3,PLIST+20         P6: out envblock addr
         LA    R3,WORK_RSN
         ST    R3,PLIST+24         P7: out reason code
         OI    PLIST+24,X'80'      VL=1 marker on last entry
*
         LOAD  EP=IRXINIT,ERRET=NOIRXIN
         LR    R3,R0                  R3 = IRXINIT entry-point addr
*
         L     R0,PREVP               caller-prev or 0
         LA    R1,PLIST
         LR    R15,R3
         BALR  R14,R15
         ST    R15,WORK_RC
*
         DELETE EP=IRXINIT
*
*  Record the new env address in LASTENV whenever IRXINIT actually
*  produced one (WORK_NEWE non-zero), regardless of RC.  Some
*  successful inits return RC=4 (warning); some non-zero RCs still
*  leave a usable env behind.  TERM_LAST in a later segment will
*  read this slot.
*
         L     R3,WORK_NEWE
         LTR   R3,R3
         BZ    DIRXNORC
         ST    R3,LASTENV
DIRXNORC DS    0H
*
         MVC   LBLBUF(20),=CL20'  IRXINIT RC'
         L     R1,WORK_RC
         BAL   R14,WKVDEC
         MVC   LBLBUF(20),=CL20'  reason'
         L     R1,WORK_RSN
         BAL   R14,WKVDEC
         MVC   LBLBUF(20),=CL20'  new envblock'
         L     R1,WORK_NEWE
         BAL   R14,WKVHEX
*
         BAL   R14,READECT
         MVC   LBLBUF(20),=CL20'  post ECTENVBK'
         L     R1,WORK_ENVB
         BAL   R14,WKVHEX
*
         L     R14,WORK_R14R
         BR    R14
*
**********************************************************************
*  BUILDCMD - copy the caller's parameter text into CMDBUF and       *
*             record its length in CMDLEN.  Detects calling          *
*             convention from the halfword at R2+0:                  *
*                                                                    *
*    halfword < 256  -> TSO CALL    R2 -> halfword len + text         *
*    halfword >= 256 -> LINKMVS     R2 -> array of descriptor addrs   *
*                                                                    *
*  For LINKMVS, walks the descriptor list and concatenates each      *
*  parameter's text into CMDBUF separated by single spaces, so that  *
*  segment processing sees the same form a TSO CALL would deliver.   *
*  Truncates if the combined text would exceed L'CMDBUF.             *
**********************************************************************
*
BUILDCMD ST    R14,WORK_R14C
         MVI   CMDBUF,C' '
         MVC   CMDBUF+1(L'CMDBUF-1),CMDBUF       fill via overlap MVC
         XC    CMDLEN,CMDLEN
*
         LH    R3,0(,R2)
         CL    R3,=F'256'
         BL    BLD_TSO                 halfword < 256 -> TSO CALL
*
*  ----- LINKMVS path
*
         LR    R6,R2                   R6 = list pointer
         LA    R5,CMDBUF
         LA    R7,L'CMDBUF             remaining bytes in CMDBUF
BLDLMK   L     R3,0(,R6)
         LR    R8,R3                   save raw value (high bit)
         N     R3,=X'7FFFFFFF'
         LTR   R3,R3
         BZ    BLD_DONE
         LH    R4,0(,R3)
         LA    R3,2(,R3)               R3 -> text
         LTR   R4,R4
         BNP   BLD_NEXT                empty descriptor
         CR    R4,R7
         BNH   BLD_OK
         LR    R4,R7                   cap to remaining
BLD_OK   LR    R9,R4                   save text length
         BCTR  R4,0
         EX    R4,BLD_MVC
         AR    R5,R9
         SR    R7,R9
BLD_NEXT LTR   R8,R8                   high bit on this descriptor?
         BM    BLD_DONE                yes -> last entry
         LTR   R7,R7
         BNP   BLD_DONE                no room for separator
         MVI   0(R5),C' '
         LA    R5,1(,R5)
         BCTR  R7,0
         LA    R6,4(,R6)               next descriptor
         B     BLDLMK
*
*  ----- TSO CALL path
*
BLD_TSO  LH    R4,0(,R2)
         LA    R3,2(,R2)               R3 -> text
         LA    R6,L'CMDBUF
         CR    R4,R6
         BNH   BLD_TSOK
         LR    R4,R6                   cap to L'CMDBUF
BLD_TSOK LR    R9,R4
         LTR   R4,R4
         BNP   BLD_RET
         BCTR  R4,0
         EX    R4,BLD_TMVC
         LA    R5,CMDBUF
         AR    R5,R9
         B     BLD_RET
*
*  ----- finish: compute used length and return
*
BLD_DONE DS    0H
BLD_RET  LA    R3,CMDBUF
         LA    R4,L'CMDBUF
         LR    R6,R5
         SR    R6,R3                   R6 = bytes used
         ST    R6,CMDLEN
         L     R14,WORK_R14C
         BR    R14
*
*  EX targets (must not fall through into them)
*
         B     BLD_RET                 safety: skip the EX-only MVCs
BLD_MVC  MVC   0(0,R5),0(R3)
BLD_TMVC MVC   CMDBUF(0),0(R3)
*
**********************************************************************
*  READECT - re-read ECTENVBK -> WORK_ENVB                           *
*            Returns 0 in WORK_ENVB if chain unreachable.            *
**********************************************************************
*
READECT  DS    0H
         XC    WORK_ENVB,WORK_ENVB
         L     R3,X'224'              ASCB
         LTR   R3,R3
         BZR   R14
         L     R4,X'6C'(,R3)          ASXB
         LTR   R4,R4
         BZR   R14
         L     R5,X'14'(,R4)          LWA
         LTR   R5,R5
         BZR   R14
         L     R6,X'20'(,R5)          ECT
         LTR   R6,R6
         BZR   R14
         L     R7,X'30'(,R6)          ECTENVBK
         ST    R7,WORK_ENVB
         BR    R14
*
**********************************************************************
*  WANCHHDR - dump IRXANCHR header (R8 = anchor)                     *
**********************************************************************
*
WANCHHDR DS    0H
         ST    R14,WORK_R14W
         MVC   LINETXT(120),=CL120'  IRXANCHR_HEADER'
         BAL   R14,WLINE
         MVC   LBLBUF(20),=CL20'    eyecatch'
         LR    R1,R8                  R1 -> 8-byte eye-catcher
         BAL   R14,WKVTXT8
         MVC   LBLBUF(20),=CL20'    version'
         LA    R1,8(,R8)
         BAL   R14,WKVTXT4
         MVC   LBLBUF(20),=CL20'    total'
         L     R1,12(,R8)
         BAL   R14,WKVDEC
         MVC   LBLBUF(20),=CL20'    used'
         L     R1,16(,R8)
         BAL   R14,WKVDEC
         MVC   LBLBUF(20),=CL20'    length'
         L     R1,20(,R8)
         BAL   R14,WKVDEC
         L     R14,WORK_R14W
         BR    R14
*
**********************************************************************
*  WANCHSLT - dump first 4 slots of IRXANCHR (R8 = anchor)           *
*             Slot layout assumed compatible with the rexx370 spec   *
*             (40 bytes per slot).  When IBM's layout differs, the   *
*             field hex still gives a usable diff baseline.          *
**********************************************************************
*
WANCHSLT DS    0H
         ST    R14,WORK_R14W
         LA    R9,32(,R8)             R9 -> first slot
         LA    R6,0                   R6 = slot index 0..3
*
WANCHLP  L     R3,0(,R9)              envblock_ptr
         C     R3,=F'-1'
         BE    SLOT_SENT
         LTR   R3,R3
         BZ    SLOT_FREE
         MVC   LINETXT(120),=CL120'  slot 0  USED'
         B     SLOTPRT
SLOT_SENT MVC  LINETXT(120),=CL120'  slot 0  SENTINEL'
         B     SLOTPRT
SLOT_FREE MVC  LINETXT(120),=CL120'  slot 0  FREE'
SLOTPRT  CVD   R6,WORK_DEC
         UNPK  WORK_HEX(2),WORK_DEC+6(2)
         OI    WORK_HEX+1,X'F0'
         MVC   LINETXT+7(1),WORK_HEX+1
         BAL   R14,WLINE
*
*  Slot fields
*
         MVC   LBLBUF(20),=CL20'      envblock_ptr'
         L     R1,0(,R9)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'      token'
         L     R1,4(,R9)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'      anchor_hint'
         L     R1,24(,R9)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'      tcb_ptr'
         L     R1,28(,R9)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'      flags'
         L     R1,32(,R9)
         BAL   R14,WKVHEX
*
         LA    R9,40(,R9)             next slot
         LA    R6,1(,R6)
         CL    R6,=F'4'
         BL    WANCHLP
*
         L     R14,WORK_R14W
         BR    R14
*
**********************************************************************
*  WENV - dump ENVBLOCK header (R7 = ENVBLOCK)                       *
**********************************************************************
*
WENV     DS    0H
         ST    R14,WORK_R14W
         MVC   LINETXT(120),=CL120'  ENVBLOCK'
         BAL   R14,WLINE
         MVC   LBLBUF(20),=CL20'    eyecatch'
         LR    R1,R7
         BAL   R14,WKVTXT8M
         MVC   LBLBUF(20),=CL20'    version'
         LA    R1,8(,R7)
         BAL   R14,WKVTXT4
         MVC   LBLBUF(20),=CL20'    length'
         L     R1,12(,R7)
         BAL   R14,WKVDEC
         MVC   LBLBUF(20),=CL20'    parmblock'
         L     R1,16(,R7)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    userfield'
         L     R1,20(,R7)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    workblok ext'
         L     R1,24(,R7)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    irxexte'
         L     R1,28(,R7)
         BAL   R14,WKVHEX
         L     R14,WORK_R14W
         BR    R14
*
**********************************************************************
*  WPARM - dump PARMBLOCK header (R8 = PARMBLOCK)                    *
**********************************************************************
*
WPARM    DS    0H
         ST    R14,WORK_R14W
         MVC   LINETXT(120),=CL120'  PARMBLOCK'
         BAL   R14,WLINE
         MVC   LBLBUF(20),=CL20'    eyecatch'
         LR    R1,R8
         BAL   R14,WKVTXT8M
         MVC   LBLBUF(20),=CL20'    version'
         LA    R1,8(,R8)
         BAL   R14,WKVTXT4
         MVC   LBLBUF(20),=CL20'    language'
         LA    R1,12(,R8)
         BAL   R14,WKVTXT3
         MVC   LBLBUF(20),=CL20'    modnamet'
         L     R1,16(,R8)              +0x10
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    subcomtb'
         L     R1,20(,R8)              +0x14
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    packtb'
         L     R1,24(,R8)              +0x18
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    parsetok'
         LA    R1,28(,R8)              +0x1C
         BAL   R14,WKVTXT8M
         MVC   LBLBUF(20),=CL20'    flags'
         L     R1,36(,R8)              +0x24
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    masks'
         L     R1,40(,R8)              +0x28
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    subpool'
         L     R1,44(,R8)              +0x2C  (fullword)
         BAL   R14,WKVDEC
         MVC   LBLBUF(20),=CL20'    addrspn'
         LA    R1,48(,R8)              +0x30
         BAL   R14,WKVTXT8M
         L     R14,WORK_R14W
         BR    R14
*
**********************************************************************
*  WBANNER / WHELP                                                    *
**********************************************************************
*
WBANNER  DS    0H
         ST    R14,WORK_R14B
         MVC   LINETXT(120),=CL120'#IRXPROBE-v1'
         BAL   R14,WLINE
         L     R14,WORK_R14B
         BR    R14
*
WHELP    DS    0H
         ST    R14,WORK_R14B
         MVC   LINETXT(120),=CL120'IRXPROBE subcommands:'
         BAL   R14,WLINE
         MVC   LINETXT(120),=CL120'  DUMP   INIT   INITP hex  INITNT'
         BAL   R14,WLINE
         MVC   LINETXT(120),=CL120'  TERM hex   EXEC hex   MARK text'
         BAL   R14,WLINE
         MVC   LINETXT(120),=CL120'See test/zos/README.md for details.'
         BAL   R14,WLINE
         L     R14,WORK_R14B
         BR    R14
*
**********************************************************************
*  WLINE - write LINETXT to SYSPRINT                                 *
**********************************************************************
*
WLINE    DS    0H
         ST    R14,WORK_R14L
         MVI   LINEBUF,C' '           ANSI carriage control = blank
         PUT   SYSPRINT,LINEBUF
         MVC   LINETXT(120),BLANK120
         L     R14,WORK_R14L
         BR    R14
*
**********************************************************************
*  WKVHEX - emit '<label> = xxxxxxxx' line                           *
*    Inputs: LBLBUF (CL20)  R1 = fullword to emit as 8 hex chars    *
**********************************************************************
*
WKVHEX   DS    0H
         ST    R14,WORK_R14H
         ST    R1,WORK_TMPA
         MVC   LINETXT(20),LBLBUF
         MVC   LINETXT+20(3),=CL3' = '
         L     R1,WORK_TMPA
         LA    R2,LINETXT+23
         BAL   R14,FMTHEX8
         BAL   R14,WLINE
         L     R14,WORK_R14H
         BR    R14
*
**********************************************************************
*  WKVDEC - emit '<label> = nnn' line                                *
*    Inputs: LBLBUF (CL20)  R1 = signed binary fullword              *
**********************************************************************
*
WKVDEC   DS    0H
         ST    R14,WORK_R14H
         MVC   LINETXT(20),LBLBUF
         MVC   LINETXT+20(3),=CL3' = '
         CVD   R1,WORK_DEC
         OI    WORK_DEC+7,X'0F'       ensure positive sign nibble
         UNPK  WORK_HEX(11),WORK_DEC(8)
         MVC   LINETXT+23(11),WORK_HEX
*  Strip leading zeros (cosmetic): replace runs of '0' with blank
*  except the last digit.
         LA    R3,LINETXT+23
         LA    R4,10
SKIPZERO CLI   0(R3),C'0'
         BNE   ZDONE
         MVI   0(R3),C' '
         LA    R3,1(,R3)
         BCT   R4,SKIPZERO
ZDONE    DS    0H
         BAL   R14,WLINE
         L     R14,WORK_R14H
         BR    R14
*
**********************************************************************
*  WKVTXT8 / WKVTXT8M / WKVTXT4 / WKVTXT3                            *
*  Emit '<label> = ''xxxxxxxx''' lines, copying N bytes from R1.     *
*  WKVTXT8M = R1 is the address itself (used when caller passes      *
*  a register holding an address rather than data).                  *
**********************************************************************
*
WKVTXT8  DS    0H
         ST    R14,WORK_R14H
         MVC   LINETXT(20),LBLBUF
         MVC   LINETXT+20(4),=CL4' = '''
         MVC   LINETXT+24(8),0(R1)
         MVI   LINETXT+32,C''''
         BAL   R14,WLINE
         L     R14,WORK_R14H
         BR    R14
*
WKVTXT8M EQU   WKVTXT8                same calling pattern
*
WKVTXT4  DS    0H
         ST    R14,WORK_R14H
         MVC   LINETXT(20),LBLBUF
         MVC   LINETXT+20(4),=CL4' = '''
         MVC   LINETXT+24(4),0(R1)
         MVI   LINETXT+28,C''''
         BAL   R14,WLINE
         L     R14,WORK_R14H
         BR    R14
*
WKVTXT3  DS    0H
         ST    R14,WORK_R14H
         MVC   LINETXT(20),LBLBUF
         MVC   LINETXT+20(4),=CL4' = '''
         MVC   LINETXT+24(3),0(R1)
         MVI   LINETXT+27,C''''
         BAL   R14,WLINE
         L     R14,WORK_R14H
         BR    R14
*
**********************************************************************
*  FMTHEX8 - convert R1 to 8 EBCDIC hex chars at addr in R2          *
**********************************************************************
*
FMTHEX8  DS    0H
         ST    R1,WORK_TMPA
         UNPK  WORK_HEX(9),WORK_TMPA(5)
         TR    WORK_HEX(8),HEXTAB-X'F0'
         MVC   0(8,R2),WORK_HEX
         BR    R14
*
**********************************************************************
*  PARSEHEX - parse ARGBUF as 1..8 EBCDIC hex chars -> WORK_TMPA     *
*    Returns R15=0 on success, R15=4 on parse error or empty input   *
**********************************************************************
*
*  EBCDIC ordering matters here: lowercase letters X'81'-X'89'..,
*  uppercase X'C1'-X'C9'.., and digits X'F0'-X'F9' are arranged
*  with letters BELOW digits.  A naive 'CLI char,C''0''; BL stop'
*  rejects letters, so the range checks must walk the encoded
*  values in the right order.  The pattern below tests each of
*  the three hex regions explicitly.
*
PARSEHEX DS    0H
         XC    WORK_TMPA,WORK_TMPA
         LA    R3,ARGBUF
         LA    R4,L'ARGBUF
         L     R9,=F'0'                accumulator
         LA    R5,0                    digit count
*
*  Skip leading non-hex characters (spaces, NULs, anything else
*  that REXX or LINKMVS may prepend to the parameter data).
*
PHSKIP   CLI   0(R3),C'0'
         BL    PHCKLET                 < X'F0' -> not a digit
         CLI   0(R3),C'9'
         BNH   PHLOOP                  X'F0'..X'F9' = digit
         B     PHADV                   X'FA'..X'FF' = invalid
PHCKLET  CLI   0(R3),C'A'
         BL    PHCKLOW                 < X'C1' -> not uppercase
         CLI   0(R3),C'F'
         BNH   PHLOOP                  X'C1'..X'C6' = uppercase hex
         B     PHADV                   X'C7'..X'EF' = invalid
PHCKLOW  CLI   0(R3),C'a'
         BL    PHADV                   < X'81'
         CLI   0(R3),C'f'
         BNH   PHLOOP                  X'81'..X'86' = lowercase hex
         B     PHADV                   X'87'..X'C0' = invalid
*
PHADV    LA    R3,1(,R3)
         BCT   R4,PHSKIP
         LA    R15,4                   no hex chars found
         BR    R14
*
*  Parse hex digits left-to-right; stop at first non-hex char.
*  Same EBCDIC region tests as PHSKIP; on the no-match branches
*  jump to PHEND (we've already collected at least one digit).
*
PHLOOP   CLI   0(R3),C'0'
         BL    PHCK2LET
         CLI   0(R3),C'9'
         BNH   PHDIGIT
         B     PHEND
PHCK2LET CLI   0(R3),C'A'
         BL    PHCK2LOW
         CLI   0(R3),C'F'
         BNH   PHALPHA
         B     PHEND
PHCK2LOW CLI   0(R3),C'a'
         BL    PHEND
         CLI   0(R3),C'f'
         BH    PHEND
*
PHALPHA  IC    R10,0(,R3)
         N     R10,=F'15'
         LA    R10,9(,R10)
         N     R10,=F'15'              alpha nibble (10..15)
         B     PHACCUM
*
PHDIGIT  IC    R10,0(,R3)
         N     R10,=F'15'              digit nibble
*
PHACCUM  SLL   R9,4
         AR    R9,R10
         LA    R3,1(,R3)
         LA    R5,1(,R5)
         CL    R5,=F'8'
         BNL   PHEND                   collected 8 digits
         BCT   R4,PHLOOP
*
PHEND    LTR   R5,R5
         BZ    PHERR                   no digits found
         ST    R9,WORK_TMPA
         LA    R15,0
         BR    R14
*
PHERR    LA    R15,4
         BR    R14
*
**********************************************************************
*  Static data                                                       *
**********************************************************************
*
SAVEAR1  DS    18F
*
HEXTAB   DC    C'0123456789ABCDEF'
BLANK40  DC    XL1'40'
BLANK80  DC    CL80' '
BLANK120 DC    CL120' '
*
SUBCMD   DS    CL8
ARGBUF   DS    CL64
*
*  Sequenced-subcommand command buffer, populated by BUILDCMD
*
CMDBUF   DS    CL128
CMDLEN   DS    F                       bytes used in CMDBUF
CMDPTR   DS    F                       current scan position
CMDEND   DS    F                       end-of-buffer pointer
LASTENV  DS    F                       env from most recent INIT*
*
LINEBUF  DS    0CL121
LINEBUF_CC DC  C' '
LINETXT  DS    CL120
*
LBLBUF   DS    CL20
*
FCODE    DS    CL8
PARMODE  DS    CL8                     IRXINIT P2: parm-module name
PARMP    DS    F                       IRXINIT P3: caller PARMBLOCK
USERP    DS    F                       IRXINIT P4: user field
PREVP    DS    F                       R0 input: caller-prev env
RESVZ    DC    F'0'                    IRXINIT P5 reserved zero
PLIST    DS    8F
*
WORK_ASCB    DS  F
WORK_ASXB    DS  F
WORK_LWA     DS  F
WORK_ECT     DS  F
WORK_ENVB    DS  F
WORK_ANCH    DS  F
WORK_PARM    DS  F
WORK_NEWE    DS  F
WORK_RSN     DS  F
WORK_RC      DS  F
WORK_FINRC   DS  F
WORK_TMPA    DS  F
WORK_TMPB    DS  CL16
WORK_DEC     DS  D
WORK_HEX     DS  CL16
WORK_R14R    DS  F
WORK_R14L    DS  F
WORK_R14H    DS  F
WORK_R14B    DS  F
WORK_R14W    DS  F
WORK_R14C    DS  F                     BUILDCMD save slot
WORK_R14D    DS  F                     handler dispatch save slot
WORK_R14T    DS  F                     DOIRXTERM save slot
*
SYSPRINT DCB   DDNAME=SYSPRINT,DSORG=PS,MACRF=PM,LRECL=121,RECFM=FBA,  X
               BLKSIZE=121
*
         LTORG
         END   IRXPROBE
