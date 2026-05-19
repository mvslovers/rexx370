/* ------------------------------------------------------------------ */
/*  irxbops.h - REXX/370 Bytecode Opcode Definitions (WP-BC-01)      */
/*                                                                    */
/*  Opcode encoding for the bytecode VM.  All Phase 1 opcodes are    */
/*  exactly 1 byte with no operands.  Future work packages extend     */
/*  this set with operand-bearing opcodes.                            */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXBOPS_H
#define IRXBOPS_H

/* ================================================================== */
/*  Opcode table (Phase 1 — minimal set)                             */
/* ================================================================== */

#define OP_NOP       0x00 /* 1 byte — no operation, advance PC       */
#define OP_EXIT      0x01 /* 1 byte — terminate execution, RC=0      */
#define OP_NEWCLAUSE 0x02 /* 1 byte — clause boundary (TRACE hook)   */

/* ================================================================== */
/*  Per-opcode size in bytes (including the opcode byte itself)       */
/*                                                                    */
/*  All Phase 1 opcodes are 1 byte.  Future phases add operands;      */
/*  extend the switch in OP_SIZE before introducing them.            */
/* ================================================================== */

#define OP_SIZE(op) 1 /* Phase 1: every opcode is 1 byte         */

/* ================================================================== */
/*  Compiler and VM return codes                                      */
/* ================================================================== */

#define IRXBC_OK         0  /* success                               */
#define IRXBC_ERR_STOR   20 /* irxstor allocation failed             */
#define IRXBC_ERR_TOKN   21 /* tokenizer returned an error           */
#define IRXBC_ERR_UNSUP  22 /* unsupported construct (Phase 1 limit) */
#define IRXBC_ERR_OPCODE 23 /* unknown opcode encountered by VM      */

#endif /* IRXBOPS_H */
