/* ------------------------------------------------------------------ */
/*  irxbctl.h - REXX/370 Bytecode Control-Flow Utilities (WP-BC-03)  */
/*                                                                    */
/*  irx_bc_disasm() — disassemble an irx_bc_execblk to a text buffer. */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXBCTL_H
#define IRXBCTL_H

#include "irxexbl.h"

/* irx_bc_disasm — disassemble the bytecode of bc into buf.
 *
 * Walks the code stream using OP_SIZE, emitting one line per instruction.
 * Each line has the form: "OOOO: MNEMONIC [operand]\n"
 * where OOOO is the byte offset from the start of the code array.
 *
 * Parameters:
 *   bc     - bytecode container (must not be NULL)
 *   buf    - output buffer (may be NULL to get required length only)
 *   bufsz  - capacity of buf in bytes
 *
 * Returns the number of characters that would be written (excluding the
 * final NUL), or -1 if bc is NULL.  If the required length exceeds bufsz,
 * the output is truncated; buf is always NUL-terminated when buf != NULL
 * and bufsz > 0.
 */
int irx_bc_disasm(const struct irx_bc_execblk *bc, char *buf, int bufsz);

#endif /* IRXBCTL_H */
