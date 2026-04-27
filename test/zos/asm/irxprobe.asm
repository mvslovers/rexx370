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
*  Parse PARM (REXX 'ADDRESS LINKMVS' convention)
*    R2 -> array of fullword addresses; high-order bit on last entry.
*    Each address -> halfword length followed by character data.
*    First entry  becomes SUBCMD (CL8, uppercase, blank-padded).
*    Second entry becomes ARGBUF (optional).
*
         MVC   SUBCMD,BLANK80          subcommand defaults to blank
         MVC   ARGBUF(L'ARGBUF),BLANK80
*
*  ----- first parameter (subcommand)
*
         L     R3,0(,R2)               R3 = first param descriptor addr
         LR    R10,R3                  save raw value (incl. high bit)
         N     R3,=X'7FFFFFFF'         clear high-bit for addressing
         LTR   R3,R3
         BZ    DISPATCH                no descriptor -> help
         LH    R4,0(,R3)               R4 = halfword text length
         LTR   R4,R4
         BNP   ENDP1                   empty subcommand
         LA    R3,2(,R3)               R3 -> text
         LA    R5,SUBCMD               R5 -> dest
         LA    R6,8                    max 8 chars
         CR    R4,R6
         BNH   GOTLEN1
         LR    R4,R6
GOTLEN1  LR    R7,R4
COPYSUB  MVC   0(1,R5),0(R3)
         OI    0(R5),X'40'             EBCDIC uppercase mask
         LA    R3,1(,R3)
         LA    R5,1(,R5)
         BCT   R7,COPYSUB
*
ENDP1    LTR   R10,R10                 high-bit on first entry?
         BM    DISPATCH                yes -> no further params
*
*  ----- second parameter (argument; optional)
*
         LA    R2,4(,R2)
         L     R3,0(,R2)
         N     R3,=X'7FFFFFFF'
         LTR   R3,R3
         BZ    DISPATCH
         LH    R4,0(,R3)
         LTR   R4,R4
         BNP   DISPATCH
         LA    R3,2(,R3)
         LA    R6,L'ARGBUF
         CR    R4,R6
         BNH   GOTLEN2
         LR    R4,R6
GOTLEN2  BCTR  R4,0
         EX    R4,EXMVCARG
         B     DISPATCH
EXMVCARG MVC   ARGBUF(0),0(R3)         executed via EX
*
*  Dispatch on SUBCMD
*
DISPATCH DS    0H
         CLC   SUBCMD,=CL8'DUMP'
         BE    DODUMP
         CLC   SUBCMD,=CL8'INIT'
         BE    DOINIT
         CLC   SUBCMD,=CL8'INITP'
         BE    DOINITP
         CLC   SUBCMD,=CL8'INITNT'
         BE    DOINITNT
         CLC   SUBCMD,=CL8'TERM'
         BE    DOTERM
         CLC   SUBCMD,=CL8'EXEC'
         BE    DOEXEC
         CLC   SUBCMD,=CL8'MARK'
         BE    DOMARK
         CLC   SUBCMD,BLANK80
         BE    DOHELP
*
         MVC   LINETXT(120),=CL120'?? unknown subcommand'
         MVC   LINETXT+22(8),SUBCMD
         BAL   R14,WLINE
         LA    R15,4
         B     EXIT
*
**********************************************************************
*  HELP                                                              *
**********************************************************************
*
DOHELP   DS    0H
         BAL   R14,WHELP
         LA    R15,0
         B     EXIT
*
**********************************************************************
*  MARK - emit '== text ==' separator line                           *
**********************************************************************
*
DOMARK   DS    0H
         MVC   LINETXT(120),=CL120'== '
         MVC   LINETXT+3(70),ARGBUF
         BAL   R14,WLINE
         LA    R15,0
         B     EXIT
*
**********************************************************************
*  DUMP - read-only inspection of ECTENVBK, IRXANCHR, ENVBLOCK,      *
*         PARMBLOCK.                                                  *
**********************************************************************
*
DODUMP   DS    0H
         MVC   LINETXT(120),=CL120'DUMP'
         BAL   R14,WLINE
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
DUMPDONE DS    0H
         LA    R15,0
         B     EXIT
*
**********************************************************************
*  INIT - IRXINIT INITENVB, TSOFL=1, no caller-prev                  *
**********************************************************************
*
DOINIT   DS    0H
         MVC   LINETXT(120),=CL120'INIT'
         BAL   R14,WLINE
         MVC   FCODE,=CL8'INITENVB'
         XC    PARMP,PARMP
         XC    USERP,USERP
         XC    PREVP,PREVP            R0 input = 0 (no prev)
         OI    FLAGS+3,X'80'          TSOFL = 1 (placeholder)
         BAL   R14,DOIRXINIT
         LA    R15,0
         B     EXIT
*
**********************************************************************
*  INITP - IRXINIT INITENVB, TSOFL=1, R0 = hex env address (prev)    *
**********************************************************************
*
DOINITP  DS    0H
         MVC   LINETXT(120),=CL120'INITP'
         BAL   R14,WLINE
         BAL   R14,PARSEHEX           ARGBUF -> WORK_TMPA
         LTR   R15,R15
         BNZ   ARGERR
         MVC   FCODE,=CL8'INITENVB'
         XC    PARMP,PARMP
         XC    USERP,USERP
         MVC   PREVP,WORK_TMPA
         OI    FLAGS+3,X'80'
         BAL   R14,DOIRXINIT
         LA    R15,0
         B     EXIT
*
**********************************************************************
*  INITNT - IRXINIT INITENVB, TSOFL=0 (non-TSO)                      *
**********************************************************************
*
DOINITNT DS    0H
         MVC   LINETXT(120),=CL120'INITNT (TSOFL=0)'
         BAL   R14,WLINE
         MVC   FCODE,=CL8'INITENVB'
         XC    PARMP,PARMP
         XC    USERP,USERP
         XC    PREVP,PREVP
         XC    FLAGS,FLAGS            TSOFL = 0
         BAL   R14,DOIRXINIT
         LA    R15,0
         B     EXIT
*
**********************************************************************
*  TERM - IRXTERM with R0 = hex env address                          *
**********************************************************************
*
DOTERM   DS    0H
         MVC   LINETXT(120),=CL120'TERM'
         BAL   R14,WLINE
         BAL   R14,PARSEHEX
         LTR   R15,R15
         BNZ   ARGERR
*
         MVC   LBLBUF(20),=CL20'  arg envblock'
         L     R1,WORK_TMPA
         BAL   R14,WKVHEX
*
*  Capture ECTENVBK pre-call
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
         LA    R15,0
         B     EXIT
*
**********************************************************************
*  EXEC - IRXEXEC stub.  Calling IRXEXEC requires an EXECBLK with    *
*         an EXECNAME pointing at the exec dataset member, plus an   *
*         ARGTABLE and EVALBLOCK.  This stub does the minimum to    *
*         exercise the linkage; full implementation tracked in the   *
*         follow-up to issue #74.                                    *
**********************************************************************
*
DOEXEC   DS    0H
         MVC   LINETXT(120),=CL120'EXEC (stub)'
         BAL   R14,WLINE
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
         B     EXIT
*
**********************************************************************
*  ARGERR - bad hex argument                                         *
**********************************************************************
*
ARGERR   DS    0H
         MVC   LINETXT(120),=CL120'  ?? bad hex argument: '
         MVC   LINETXT+24(40),ARGBUF
         BAL   R14,WLINE
         LA    R15,4
         B     EXIT
*
**********************************************************************
*  NOIRXIN / NOIRXTM - LOAD EP= failure handlers                     *
**********************************************************************
*
NOIRXIN  DS    0H
         MVC   LINETXT(120),=CL120'  ?? LOAD EP=IRXINIT failed'
         BAL   R14,WLINE
         LA    R15,8
         B     EXIT
*
NOIRXTM  DS    0H
         MVC   LINETXT(120),=CL120'  ?? LOAD EP=IRXTERM failed'
         BAL   R14,WLINE
         LA    R15,8
         B     EXIT
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
*             PARMP   address of caller PARMBLOCK or 0               *
*             USERP   user field address or 0                        *
*             PREVP   value to load into R0 (caller-prev) or 0       *
*             FLAGS   flags fullword (TSOFL bit etc.)                *
*    Output:  WORK_NEWE   new ENVBLOCK address                       *
*             WORK_NEWWB  workblock-extension address                *
*             WORK_RSN    reason code                                *
*             WORK_RC     RC from IRXINIT                            *
*             prints  pre/post ECTENVBK and the four output values   *
**********************************************************************
*
DOIRXINIT DS   0H
         ST    R14,WORK_R14R
*
         BAL   R14,READECT
         MVC   LBLBUF(20),=CL20'  pre  ECTENVBK'
         L     R1,WORK_ENVB
         BAL   R14,WKVHEX
*
         XC    WORK_NEWE,WORK_NEWE
         XC    WORK_NEWWB,WORK_NEWWB
         XC    WORK_RSN,WORK_RSN
*
*  Build IRXINIT parameter list (all addresses high-bit clear; VL=1
*  on last entry).
*
         LA    R3,FCODE
         ST    R3,PLIST+0
         LA    R3,PARMP
         ST    R3,PLIST+4
         LA    R3,USERP
         ST    R3,PLIST+8
         LA    R3,WORK_NEWE
         ST    R3,PLIST+12
         LA    R3,WORK_NEWWB
         ST    R3,PLIST+16
         LA    R3,WORK_RSN
         ST    R3,PLIST+20
         LA    R3,FLAGS
         ST    R3,PLIST+24
         OI    PLIST+24,X'80'         high-bit on last entry
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
         MVC   LBLBUF(20),=CL20'  IRXINIT RC'
         L     R1,WORK_RC
         BAL   R14,WKVDEC
         MVC   LBLBUF(20),=CL20'  reason'
         L     R1,WORK_RSN
         BAL   R14,WKVDEC
         MVC   LBLBUF(20),=CL20'  new envblock'
         L     R1,WORK_NEWE
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'  workblok ext'
         L     R1,WORK_NEWWB
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
         MVC   LBLBUF(20),=CL20'    flags'
         L     R1,20(,R8)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    masks'
         L     R1,24(,R8)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    addrspn'
         LA    R1,28(,R8)
         BAL   R14,WKVTXT8M
         MVC   LBLBUF(20),=CL20'    subpool'
         XC    WORK_TMPA,WORK_TMPA
         IC    R1,36(,R8)
         ST    R1,WORK_TMPA
         L     R1,WORK_TMPA
         BAL   R14,WKVDEC
         MVC   LBLBUF(20),=CL20'    modnamet'
         L     R1,40(,R8)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    subcomtb'
         L     R1,44(,R8)
         BAL   R14,WKVHEX
         MVC   LBLBUF(20),=CL20'    packtb'
         L     R1,48(,R8)
         BAL   R14,WKVHEX
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
PARSEHEX DS    0H
         XC    WORK_TMPA,WORK_TMPA
         LA    R3,ARGBUF
         LA    R4,L'ARGBUF
         L     R9,=F'0'                accumulator
         LA    R5,0                    digit count
*
*  Skip leading blanks
*
PHSKIP   CLI   0(R3),C' '
         BNE   PHLOOP
         LA    R3,1(,R3)
         BCT   R4,PHSKIP
         LA    R15,4                   nothing but blanks
         BR    R14
*
*  Parse digits left-to-right; accept '0'-'9', 'A'-'F', 'a'-'f'.
*  Stop on blank or NUL or after 8 digits.
*
PHLOOP   CLI   0(R3),C' '
         BE    PHEND
         CLI   0(R3),X'00'
         BE    PHEND
         IC    R10,0(,R3)              char (zero-extended)
*
         CLI   0(R3),C'0'
         BL    PHERR
         CLI   0(R3),C'9'
         BH    PHALPHA
         N     R10,=F'15'              digit nibble
         B     PHACCUM
*
PHALPHA  CLI   0(R3),C'A'
         BL    PHERR
         CLI   0(R3),C'F'
         BH    PHLOWER
         B     PHLET
PHLOWER  CLI   0(R3),C'a'
         BL    PHERR
         CLI   0(R3),C'f'
         BH    PHERR
PHLET    N     R10,=F'15'
         LA    R10,9(,R10)
         N     R10,=F'15'              alpha nibble (10..15)
*
PHACCUM  SLL   R9,4
         AR    R9,R10
         LA    R3,1(,R3)
         LA    R5,1(,R5)
         CL    R5,=F'8'
         BNL   PHEND
         BCT   R4,PHLOOP
*
PHEND    LTR   R5,R5
         BZ    PHERR                   no digits at all
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
LINEBUF  DS    0CL121
LINEBUF_CC DC  C' '
LINETXT  DS    CL120
*
LBLBUF   DS    CL20
*
FCODE    DS    CL8
PARMP    DS    F
USERP    DS    F
PREVP    DS    F
FLAGS    DS    F
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
WORK_NEWWB   DS  F
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
*
SYSPRINT DCB   DDNAME=SYSPRINT,DSORG=PS,MACRF=PM,LRECL=121,RECFM=FBA,  X
               BLKSIZE=121
*
         LTORG
         END   IRXPROBE
