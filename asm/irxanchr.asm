         TITLE 'IRXANCHR - REXX/370 Environment Anchor Table'
*
*  IRXANCHR - Active environment anchor table for rexx370.
*
*  Static data module: 32-byte header followed by 64 x 40-byte slot
*  array.  Loaded at runtime via LOAD EP=IRXANCHR.  Slots are managed
*  by the irx_anchor_* API in src/irx#anchor.c (WP-I1a.3).
*
*  Header layout (32 bytes, offset from module start):
*    +0x00  8B  ENVTABLE_ID      C'IRXANCHR'  eye-catcher
*    +0x08  4B  ENVTABLE_VERSION C'0100'
*    +0x0C  4B  ENVTABLE_TOTAL   64  slot count (REXX/370 adaptation)
*    +0x10  4B  ENVTABLE_USED    0   high-watermark, initially zero
*    +0x14  4B  ENVTABLE_LENGTH  40  bytes per entry
*    +0x18  8B  (reserved)       zeros
*
*  Slot layout (40 bytes):
*    +0x00  4B  ENVBLOCK_PTR     X'FFFFFFFF' = free slot
*    +0x04  4B  TOKEN
*    +0x08 16B  (reserved)
*    +0x18  4B  ANCHOR_HINT
*    +0x1C  4B  TCB_PTR
*    +0x20  4B  FLAGS            0x40000000 = in-use
*    +0x24  4B  (reserved)
*
*  Slot 0 is a permanent sentinel and is never allocated by the API.
*
*  Module total: 32 + 64*40 = 2592 bytes (X'A20').
*
IRXANCHR CSECT
         AMODE 24
         RMODE 24
*
***      Header (32 bytes)
*
         DC    C'IRXANCHR'         +0x00  eye-catcher
         DC    C'0100'             +0x08  version
         DC    F'64'               +0x0C  TOTAL: 64 slots
         DC    F'0'                +0x10  USED: 0 initial
         DC    F'40'               +0x14  LENGTH: 40 bytes per entry
         DC    XL8'00'             +0x18  reserved
*
***      Entry array: 64 x 40 bytes
*        Header is exactly 32 bytes (4 doublewords), so the array
*        start is already doubleword-aligned.  DS 0D is documentation.
*
         DS    0D
*
*        Slot 0 - permanent sentinel, never allocated
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 1
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 2
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 3
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 4
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 5
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 6
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 7
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 8
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 9
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 10
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 11
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 12
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 13
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 14
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 15
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 16
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 17
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 18
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 19
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 20
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 21
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 22
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 23
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 24
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 25
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 26
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 27
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 28
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 29
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 30
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 31
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 32
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 33
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 34
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 35
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 36
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 37
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 38
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 39
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 40
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 41
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 42
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 43
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 44
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 45
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 46
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 47
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 48
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 49
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 50
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 51
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 52
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 53
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 54
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 55
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 56
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 57
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 58
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 59
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 60
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 61
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 62
         DC    XL4'FFFFFFFF',XL36'00'
*        Slot 63
         DC    XL4'FFFFFFFF',XL36'00'
*
         END   IRXANCHR
