/* ------------------------------------------------------------------ */
/*  irxbops.h - REXX/370 Bytecode Opcode Definitions (WP-BC-02)      */
/*                                                                    */
/*  Opcode encoding for the bytecode VM.                             */
/*                                                                    */
/*  Operand-bearing opcodes carry inline operands:                   */
/*    3-byte: op + u16-le index (PUSH_LIT, LOAD, STORE, DROP)        */
/*    2-byte: op + u8 count     (POP)                                */
/*    1-byte: all others                                              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXBOPS_H
#define IRXBOPS_H

/* ================================================================== */
/*  Control opcodes (Phase 1 + Phase 2)                              */
/* ================================================================== */

#define OP_NOP       0x00 /* 1 byte — no operation                   */
#define OP_EXIT      0x01 /* 1 byte — terminate execution, RC=0      */
#define OP_NEWCLAUSE 0x02 /* 1 byte — clause boundary (TRACE hook)   */
#define OP_EXIT_RC   0x03 /* 1 byte — pop TOS, convert to int, exit  */

/* ================================================================== */
/*  Stack ops (WP-BC-02)                                             */
/* ================================================================== */

#define OP_PUSH_LIT 0x10 /* 3 bytes: op + idx:u16 — push const[idx] */
#define OP_PUSH_TMP 0x11 /* 1 byte  — reserved for future use       */
#define OP_POP      0x12 /* 2 bytes: op + n:u8 — discard n items    */
#define OP_DUP      0x13 /* 1 byte  — duplicate top of stack        */

/* ================================================================== */
/*  Variable ops (WP-BC-02)                                          */
/* ================================================================== */

#define OP_LOAD  0x20 /* 3 bytes: op + idx:u16 — push vpool[sym[idx]] */
#define OP_STORE 0x21 /* 3 bytes: op + idx:u16 — pop, store vpool[sym[idx]] */
#define OP_DROP  0x22 /* 3 bytes: op + idx:u16 — drop vpool[sym[idx]] */

/* ================================================================== */
/*  Arithmetic ops (WP-BC-02) — all 1 byte                          */
/*  Binary (pop b, pop a, push result): ADD SUB MUL DIV IDIV MOD POW */
/*  Unary (pop a, push result):         NEG                          */
/* ================================================================== */

#define OP_ADD  0x30 /* a + b                                   */
#define OP_SUB  0x31 /* a - b                                   */
#define OP_MUL  0x32 /* a * b                                   */
#define OP_DIV  0x33 /* a / b  (normal division)                */
#define OP_IDIV 0x34 /* a % b  (integer division, truncate-to-0)*/
#define OP_MOD  0x35 /* a // b (remainder)                      */
#define OP_POW  0x36 /* a ** b                                   */
#define OP_NEG  0x37 /* -a     (unary minus)                    */

/* ================================================================== */
/*  Comparison ops (WP-BC-02) — all 1 byte                          */
/*  Pop b, pop a, push "1" or "0".                                   */
/* ================================================================== */

#define OP_EQ  0x40 /* =   fuzzy equal                         */
#define OP_NE  0x41 /* \=  fuzzy not-equal                     */
#define OP_LT  0x42 /* <   less than                           */
#define OP_LE  0x43 /* <=  less or equal                       */
#define OP_GT  0x44 /* >   greater than                        */
#define OP_GE  0x45 /* >=  greater or equal                    */
#define OP_DEQ 0x46 /* ==  strict equal                        */
#define OP_DNE 0x47 /* \== strict not-equal                    */
#define OP_DLT 0x48 /* <<  strict less                         */
#define OP_DLE 0x49 /* <<= strict less-or-equal                */
#define OP_DGT 0x4A /* >>  strict greater                      */
#define OP_DGE 0x4B /* >>= strict greater-or-equal             */

/* ================================================================== */
/*  Logical/boolean ops (WP-BC-02) — all 1 byte                     */
/*  Boolean values are "0" (false) or "1" (true).                    */
/* ================================================================== */

#define OP_AND 0x50 /* &  — logical and                        */
#define OP_OR  0x51 /* |  — logical or                         */
#define OP_XOR 0x52 /* && — logical exclusive or               */
#define OP_NOT 0x53 /* \  — logical not (unary)                */

/* ================================================================== */
/*  String ops (WP-BC-02) — all 1 byte                              */
/* ================================================================== */

#define OP_CONCAT  0x60 /* ||  — explicit concatenation            */
#define OP_BCONCAT 0x61 /* abuttal with one blank                  */

/* ================================================================== */
/*  Per-opcode size in bytes (including the opcode byte itself)       */
/* ================================================================== */

/* clang-format off */
#define OP_SIZE(op)                   \
    (((op) == OP_PUSH_LIT) ? 3 :     \
     ((op) == OP_POP)      ? 2 :     \
     ((op) == OP_LOAD)     ? 3 :     \
     ((op) == OP_STORE)    ? 3 :     \
     ((op) == OP_DROP)     ? 3 :     \
     1)
/* clang-format on */

/* ================================================================== */
/*  Compiler and VM return codes                                      */
/* ================================================================== */

#define IRXBC_OK         0  /* success                               */
#define IRXBC_ERR_STOR   20 /* irxstor allocation failed             */
#define IRXBC_ERR_TOKN   21 /* tokenizer returned an error           */
#define IRXBC_ERR_UNSUP  22 /* unsupported construct                 */
#define IRXBC_ERR_OPCODE 23 /* unknown opcode encountered by VM      */
#define IRXBC_ERR_ARITH  24 /* arithmetic error (type, divzero etc.) */
#define IRXBC_ERR_STACK  25 /* stack underflow or overflow           */

#endif /* IRXBOPS_H */
