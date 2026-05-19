/* ------------------------------------------------------------------ */
/*  irxexbl.h - REXX/370 Bytecode EXECBLK Format (WP-BC-02)          */
/*                                                                    */
/*  Defines struct irx_bc_execblk — the in-memory bytecode container */
/*  produced by the bytecode compiler (irx#bcom.c) and consumed by   */
/*  the VM loop (irx#bvm.c).  Distinct from the IBM-defined struct    */
/*  execblk in irx.h, which is the IRXEXEC parameter block.          */
/*                                                                    */
/*  Layout:                                                           */
/*    [ struct irx_bc_execblk header        ]                        */
/*    [ Constants Table  (const_count items) ]                       */
/*    [ Symbol Table     (symbol_count items)]                       */
/*    [ Bytecode         (code_length bytes) ]                       */
/*    [ Trace Map        (optional)          ]                       */
/*                                                                    */
/*  Table entry format (IRXBC_ENTRY_SIZE == 64 bytes):              */
/*    byte[0]     = string length (0..IRXBC_STR_MAX)                */
/*    byte[1..63] = string data (not NUL-terminated)                */
/*                                                                    */
/*  When const_count == 0 and symbol_count == 0 the bytecode starts  */
/*  immediately after the header — backward-compatible with WP-BC-01. */
/*                                                                    */
/*  All operands in the bytecode are table indices or relative        */
/*  offsets — no absolute pointers.  The container is position-      */
/*  independent and will be serialisable to CEXEC in a later WP.    */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXEXBL_H
#define IRXEXBL_H

#include <stdint.h>

/* ================================================================== */
/*  Magic and version                                                 */
/* ================================================================== */

#define IRXBC_MAGIC   "RX37"
#define IRXBC_VERSION 1

/* ================================================================== */
/*  Bytecode EXECBLK header                                           */
/*                                                                    */
/*  sizeof(struct irx_bc_execblk) == 28 bytes (no padding on         */
/*  byte-aligned platforms; uint16_t at offset 4, uint32_t at 8+).  */
/* ================================================================== */

struct irx_bc_execblk
{
    char magic[4];             /* "RX37" — eye-catcher                */
    uint16_t version;          /* format version, currently 1         */
    uint16_t flags;            /* reserved, must be zero              */
    uint32_t const_count;      /* number of entries in constants table */
    uint32_t symbol_count;     /* number of entries in symbol table   */
    uint32_t code_length;      /* bytecode length in bytes            */
    uint32_t entry_offset;     /* offset into bytecode of entry point */
    uint32_t trace_map_offset; /* offset to trace map (0 = absent)    */
    /* Variable-length payload follows immediately after this header. */
};

/* ================================================================== */
/*  Table entry geometry                                              */
/* ================================================================== */

/* Bytes per entry in the constants and symbol tables. */
#define IRXBC_ENTRY_SIZE 64

/* Maximum string length that fits in one table entry. */
#define IRXBC_STR_MAX 63

/* ================================================================== */
/*  Payload access macros                                             */
/*                                                                    */
/*  When const_count == 0 and symbol_count == 0, IRXBC_CODE returns  */
/*  the same address as the old Phase 1 macro — backward-compatible.  */
/* ================================================================== */

/* Pointer to the start of the constants table. */
#define IRXBC_CONST_TBL(bc) \
    ((char *)(bc) + (int)sizeof(struct irx_bc_execblk))

/* Pointer to the start of the symbol table. */
#define IRXBC_SYM_TBL(bc) \
    (IRXBC_CONST_TBL(bc) + (int)(bc)->const_count * IRXBC_ENTRY_SIZE)

/* Pointer to start of bytecode within a container. */
#define IRXBC_CODE(bc)                     \
    ((unsigned char *)(IRXBC_SYM_TBL(bc) + \
                       (int)(bc)->symbol_count * IRXBC_ENTRY_SIZE))

/* Pointer to entry point within a container. */
#define IRXBC_ENTRY(bc) \
    (IRXBC_CODE(bc) + (bc)->entry_offset)

/* Total byte size of a container: header + tables + bytecode. */
#define IRXBC_TOTAL(bc) \
    ((int)sizeof(struct irx_bc_execblk) + (int)(bc)->const_count * IRXBC_ENTRY_SIZE + (int)(bc)->symbol_count * IRXBC_ENTRY_SIZE + (int)(bc)->code_length)

#endif /* IRXEXBL_H */
