         TITLE 'IRXTSPRM - REXX/370 TSO Default Parameter Module'
*
*  IRXTSPRM - Default PARMBLOCK for TSO (foreground/background) envs.
*
*  Static data module loaded by IRXINIT via LOAD EP=IRXTSPRM when
*  the caller provides no explicit PARMBLOCK (CON-1 §6.3, step 4).
*  No executable code — DC/DS directives only.
*
*  Control block sizes (from irx.h):
*    PARMBLOCK:       64 bytes
*    MODNAMET:       112 bytes (13 x 8-byte fields + 8-byte FFFF)
*    SUBCOMTB header: 40 bytes
*    SUBCOMTB entry:  32 bytes
*    PACKTB header:   48 bytes
*    PACKTB entry:     8 bytes (CL8 name, PACKTB_LENGTH=F'8')
*
IRXTSPRM CSECT
*
***      PARMBLOCK (64 bytes, offset from module entry point)
*
         DC    CL8'IRXPARMS'       +0x00  eye-catcher
         DC    CL4'0042'           +0x08  version (rexx370 deviation)
         DC    CL3'AE '            +0x0C  language: 2-byte NLS code
         DC    XL1'00'             +0x0F  reserved (_filler1)
         DC    A(MODNAMET)         +0x10  -> MODNAMET
         DC    A(SUBCOMTB)         +0x14  -> SUBCOMTB header
         DC    A(PACKTB)           +0x18  -> PACKTB header
         DC    CL8'        '       +0x1C  parse source token (blank)
         DC    X'8000C000'         +0x24  FLAGS: TSOFL | ALTMSGS | SPSHARE
         DC    X'FFFFFFFF'         +0x28  MASKS: all bits active
         DC    F'78'               +0x2C  SUBPOOL 78 (TSO private)
         DC    CL8'TSO/E   '       +0x30  ADDRSPN
         DC    XL8'FFFFFFFFFFFFFFFF' +0x38  end marker
*
***      MODNAMET (112 bytes: 13 x 8-byte fields + 8-byte FFFF)
*
MODNAMET DC    CL8'SYSTSIN '       DD names (3 fields)
         DC    CL8'SYSTSPRT'
         DC    CL8'SYSEXEC '
         DC    CL8'        '       replaceable routines (10 blank slots)
         DC    CL8'        '
         DC    CL8'        '
         DC    CL8'        '
         DC    CL8'        '
         DC    CL8'        '
         DC    CL8'        '
         DC    CL8'        '
         DC    CL8'        '
         DC    CL8'        '
         DC    XL8'FFFFFFFFFFFFFFFF' end marker
*
***      SUBCOMTB header (40 bytes) + 5 entries (160 bytes)
*
SUBCOMTB DC    A(SUBCOENT)         +0x00  first entry
         DC    F'5'                +0x04  total entries
         DC    F'5'                +0x08  used entries
         DC    F'32'               +0x0C  bytes per entry
         DC    CL8'TSO     '       +0x10  initial environment
         DC    XL8'00'             +0x18  reserved
         DC    XL8'FFFFFFFFFFFFFFFF' +0x20  end marker
*
SUBCOENT DC    CL8'TSO     '       entry 1: name
         DC    CL8'IRXSTAM '               handler
         DC    CL16'                '      token (blank)
         DC    CL8'MVS     '       entry 2: name
         DC    CL8'IRXSTAM '               handler
         DC    CL16'                '      token (blank)
         DC    CL8'LINK    '       entry 3: name
         DC    CL8'IRXSTAM '               handler
         DC    CL16'                '      token (blank)
         DC    CL8'ATTACH  '       entry 4: name
         DC    CL8'IRXSTAM '               handler
         DC    CL16'                '      token (blank)
         DC    CL8'CONSOLE '       entry 5: name
         DC    CL8'IRXSTAM '               handler
         DC    CL16'                '      token (blank)
*
***      PACKTB header (48 bytes) + 4 entries (32 bytes)
*        Entry order in memory: system (x2), local, user
*
PACKTB   DC    A(USRPKENT)         +0x00  user entries
         DC    F'1'                +0x04  user total
         DC    F'1'                +0x08  user used
         DC    A(LCLPKENT)         +0x0C  local entries
         DC    F'1'                +0x10  local total
         DC    F'1'                +0x14  local used
         DC    A(SYSPKENT)         +0x18  system entries
         DC    F'2'                +0x1C  system total
         DC    F'2'                +0x20  system used
         DC    F'8'                +0x24  bytes per entry
         DC    XL8'FFFFFFFFFFFFFFFF' +0x28  end marker
SYSPKENT DC    CL8'IRXEFMVS'       system entry 1
         DC    CL8'IRXEFPCK'       system entry 2
LCLPKENT DC    CL8'IRXFLOC '       local entry 1
USRPKENT DC    CL8'IRXFUSER'       user entry 1
*
         END   IRXTSPRM
