         TITLE 'IKJEFTRX - REXX/370 ENVIRONMENT BRING-UP FOR IKJEFT01'
*
*  IKJEFTRX - Build the default REXX language processor environment
*             at TMP start.  Loads IRXANCHR so its CDE lives in the
*             Step-TCB JPQ (survives XCTL, found by command subtasks
*             via JPA lookup), then calls IRXINIT INITENVB, which
*             fills IRXANCHR slot 1.
*
*  Replaces the IRXTMPW TMP wrapper.  IRXTMPW reached only the logon
*  path; this CSECT is called from IKJEFT01 at the point where the
*  foreground and background paths converge, so the batch TMP is
*  covered too.
*
*  A BLDL decides whether REXX/370 is installed at all.  Without it
*  every logon on a system that carries no REXX/370 would drive a
*  LOAD into its ERRET path, which is a recovery entry, not a
*  question.  BLDL asks the question.
*
*  Linkage:  BAL from IKJEFT01 at the @RF00530 convergence.
*
*  On entry: R13 = caller savearea, R14 = return address.
*  On exit:  every register except R13 restored, R13 unchanged.
*            ALWAYS returns.  A failure here costs REXX, never the
*            user's TSO session.
*
*  ---- Two things measured on MVS/CE 2026-09-23, both fatal -------
*
*  1. The bound module MUST be linked --ac 1 into an APF library.
*     Nothing in THIS csect needs authorization, but IKJEFT01 itself
*     issues MODESET (SVC 107).  Without AC(1) the TMP ends S047.
*
*  2. The CALLER must bracket the call with the entry PSW key:
*         L     @01,TEPKEY(,WRKAPTR2)
*         SPKA  0(@01)
*         ... call ...
*         SPKA  0(@03)          @03 still holds the SPROK1 key
*     TK5's SPKA at @RF00530 leaves the PSW in the SPROK1 key, but
*     the PL/S DSA that R13 addresses belongs to the entry key.  The
*     STM below is the first instruction, and in the wrong key it
*     takes a protection exception - S0C4, before any WTO can show
*     where it died.
*
*  RENT: IKJEFT01 is reentrant, so this CSECT must be too.  The
*  workarea is GETMAINed - and because BLDL writes its results into
*  its list, that list lives in the workarea, not in static storage.
*
*  NOTE for as370/IFOX00: RS-format LM/STM must be written D(B).
*
         PRINT NOGEN
R0       EQU   0
R1       EQU   1
R2       EQU   2
R3       EQU   3
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
         PRINT GEN
*
IKJEFTRX CSECT
         STM   R14,R12,12(R13)    save caller regs in caller SA
         BALR  R12,0
         USING *,R12
*
* --- workarea first: BLDL needs a writable list ---
         L     R0,=A(WALEN)
         GETMAIN RC,LV=(0)
         LTR   R15,R15
         BNZ   RETURN             no storage - give up quietly
         LR    R3,R1              keep for FREEMAIN
         ST    R13,4(,R1)         backward chain
         ST    R1,8(,R13)         forward chain
         LR    R13,R1
         USING WAREA,R13
*
* --- is REXX/370 installed at all? ---
         MVC   WBLDL(BLDLLEN),BLDLPAT
*  Use the SYMBOLIC list form, as IBM's own callers do
*  (IKJEBECI:320).  BLDL 0,(1) with the address already in R1 is
*  accepted by the assembler but does not find modules that are
*  certainly there -- measured 2026-09-23.
         BLDL  0,WBLDL            DCB=0: search the link library
         LTR   R15,R15
         BNZ   RELEASE            not installed - no REXX, no noise
*
         LOAD  EP=IRXANCHR,ERRET=RELEASE
         LOAD  EP=IRXINIT,ERRET=RELEASE
         LR    R2,R0              EP comes back in R0; GETMAIN and
*                                 the macros below clobber it
         XC    WENVOUT,WENVOUT
         XC    WREASON,WREASON
*
* --- INITENVB parameter list (SC28-1883-0 section 14) ---
*
*  The slots hold ADDRESSES of fullwords, not the values.  IRXINIT
*  does L Rx,slot then L Ry,0(,Rx), so a literal zero in a slot
*  would dereference PSA[0] and hand back a plausible-looking
*  garbage pointer.  RESZERO is a static F'0' whose address we
*  store instead.
         LA    R1,FCODE
         ST    R1,WVLIST+0        P1 function code
         LA    R1,PARMODE
         ST    R1,WVLIST+4        P2 parm module name
         LA    R1,RESZERO
         ST    R1,WVLIST+8        P3 caller PARMBLK  = addr of zero
         ST    R1,WVLIST+12       P4 user field      = addr of zero
         ST    R1,WVLIST+16       P5 reserved        = addr of zero
         LA    R1,WENVOUT
         ST    R1,WVLIST+20       P6 out: ENVBLOCK
         LA    R1,WREASON
         O     R1,=X'80000000'    VL marker on the last slot
         ST    R1,WVLIST+24       P7 out: reason code
*
         SR    R0,R0              no previous-environment hint
         LA    R1,WVLIST
         LR    R15,R2
         BALR  R14,R15
*  No message either way.  Success shows itself: the first exec
*  from SYSEXEC runs as REXX.  A failed IRXINIT leaves ECTENVBK
*  empty, IKJCT437 then hands every exec back to CLIST, and TSO
*  goes on as without REXX/370.  (IKJ56942I, which said so at
*  every logon, is gone.)
*
RELEASE  DS    0H
         LR    R1,R13             our workarea
         L     R13,4(,R13)        back to caller savearea
         L     R0,=A(WALEN)
         FREEMAIN RU,LV=(0),A=(R1)
         DROP  R13
*
RETURN   DS    0H
         LM    R14,R12,12(R13)    D(B) - never D(,B), see header
         BR    R14
*
* --- read-only static data (RENT-safe) ---
FCODE    DC    CL8'INITENVB'
PARMODE  DC    CL8' '
RESZERO  DC    F'0'
*
*  BLDL list pattern.  Entries MUST be in ascending EBCDIC order:
*  IRXANCHR before IRXINIT (A=X'C1' < I=X'C9' in position 4).
*  Entry length is 58 (8-byte name + 50 bytes BLDL fills in), the
*  size IBM's own callers use -- IKJEBECI:798.  A 12-byte entry is
*  rejected with RC=4 even for names that are certainly there;
*  measured 2026-09-23 against IEFBR14 and IKJEFT01.
BLDLPAT  DS    0H
         DC    H'2'               number of entries
         DC    H'58'              length of each entry
         DC    CL8'IRXANCHR'
         DC    XL50'00'
         DC    CL8'IRXINIT'
         DC    XL50'00'
BLDLLEN  EQU   *-BLDLPAT
*
         LTORG
*
WAREA    DSECT
WSAVE    DS    18F                standard savearea
WVLIST   DS    7F                 INITENVB parameter list
WENVOUT  DS    F                  out: ENVBLOCK address
WREASON  DS    F                  out: reason code
WBLDL    DS    0H                 BLDL list (written by BLDL)
         DS    XL120              4 header + 2 x 58
WALEN    EQU   *-WAREA
*
         END
