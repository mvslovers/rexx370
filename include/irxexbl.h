/* ------------------------------------------------------------------ */
/*  irxexbl.h - REXX/370 Bytecode EXECBLK Format (WP-BC-01)          */
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
/*  All operands in the bytecode are indices or relative offsets —   */
/*  no absolute pointers.  The container is position-independent     */
/*  and will be serialisable to CEXEC in a later work package.       */
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
/*  Payload access macros                                             */
/*                                                                    */
/*  Phase 1: const_count == 0 and symbol_count == 0, so the          */
/*  bytecode starts at (char*)bc + sizeof(*bc) + entry_offset.       */
/*  Future phases will insert the constants and symbol tables         */
/*  between the header and the bytecode.                             */
/* ================================================================== */

/* Pointer to start of bytecode within a container. */
#define IRXBC_CODE(bc) \
    ((unsigned char *)(bc) + sizeof(struct irx_bc_execblk))

/* Pointer to entry point within a container. */
#define IRXBC_ENTRY(bc) \
    (IRXBC_CODE(bc) + (bc)->entry_offset)

/* Total byte size of a container: header + payload. */
#define IRXBC_TOTAL(bc) \
    ((int)(sizeof(struct irx_bc_execblk)) + (int)(bc)->code_length)

#endif /* IRXEXBL_H */
