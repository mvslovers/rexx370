/* ------------------------------------------------------------------ */
/*  irxfunc.h - REXX/370 Service Function Prototypes                  */
/*                                                                    */
/*  Prototypes for all IRXxxxx programming services and replaceable   */
/*  routines. Functions are grouped by implementation phase.          */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXFUNC_H
#define IRXFUNC_H

#include "irx.h"
#include "irxwkblk.h"
#include "lstring.h"

/* ================================================================== */
/*  Phase 1: Foundation Services                                      */
/* ================================================================== */

/* --- Environment Lifecycle --- */

/* IRXINIT - Initialize a Language Processor Environment
 * Creates ENVBLOCK, PARMBLOCK, IRXEXTE, Work Block Extension,
 * internal Work Block, and hooks everything together.
 * Publishes the ENVBLOCK on ECTENVBK (or records batch state).
 *
 * Parameters:
 *   parms       - Initialization parameters (or NULL for defaults)
 *   envblock_ptr- Output: pointer to created ENVBLOCK
 *
 * Returns: 0=OK, 20=init error, 28=storage error
 *
 * MVS symbol: IRXINITC. The HLASM entry-point wrapper in
 * asm/irxinit.asm owns the bare IRXINIT symbol (it is the
 * IBM-spec Programming Service entry point and dispatches to
 * irx_init_dispatch). The compat wrapper here installs the
 * Phase 2+ state (BIFs, SUBCOMTB, wkbi) on top of that.
 */
int irxinit(void *parms, struct envblock **envblock_ptr) asm("IRXINITC");

/* IRXTERM - Terminate a Language Processor Environment
 * Frees all storage associated with the environment and pops it
 * off ECTENVBK (lenient if another environment was pushed on top).
 *
 * Parameters:
 *   envblock_ptr - Pointer to ENVBLOCK to terminate
 *
 * Returns: 0=OK, 20=term error
 *
 * MVS symbol: IRXTERMC. The HLASM IRXTERM Programming Service
 * entry point lives in asm/irxterm.asm; this is the Phase 2+
 * teardown compat wrapper.
 */
int irxterm(struct envblock *envblock_ptr) asm("IRXTERMC");

/* --- Storage Management Replaceable Routine --- */

/* IRXSTOR - Acquire or release storage
 * All interpreter memory allocation goes through this routine.
 * This is a replaceable routine - can be overridden via MODNAMET.
 *
 * Parameters:
 *   function - RXSMGET (acquire) or RXSMFRE (release)
 *   length   - For RXSMGET: requested size. For RXSMFRE: 0
 *   addr_ptr - For RXSMGET: output pointer. For RXSMFRE: input pointer
 *   envblock - The owning ENVBLOCK
 *
 * Returns: 0=OK, 20=storage not available
 */
int irxstor(int function, int length, void **addr_ptr,
            struct envblock *envblock);

/* --- User ID Replaceable Routine --- */

/* IRXUID - Get the current user ID
 * TSO: extracts from PSCB (PSCBUSER)
 * Non-TSO: extracts from ACEE or JCT
 *
 * Parameters:
 *   userid  - Output buffer (8 bytes, blank-padded)
 *   envblock- The owning ENVBLOCK
 *
 * Returns: 0=OK, 20=error
 */
int irxuid(char *userid, struct envblock *envblock);

/* --- Message ID Replaceable Routine --- */

/* IRXMSGID - Get/Set message prefix
 * Default prefix: 'IRX' (e.g. IRX0001I)
 *
 * Parameters:
 *   function - 0=GET, 1=SET
 *   prefix   - For GET: output (3 bytes). For SET: input (3 bytes)
 *   envblock - The owning ENVBLOCK
 *
 * Returns: 0=OK
 */
int irxmsgid(int function, char *prefix, struct envblock *envblock);

/* ================================================================== */
/*  Phase 2+: Stubs for forward reference                             */
/* ================================================================== */

/* IRXINOUT - Default I/O Replaceable Routine
 * Handles RXFWRITE (SAY), RXFWRITERR, RXFTWRITE output.
 * RXFREAD / RXFREADP are stubbed pending WP-33 (PULL / LINEIN).
 *
 * Parameters:
 *   function - I/O function code (RXFWRITE, RXFREAD, etc.)
 *   data     - String to write (RXFWRITE) or read buffer (RXFREAD)
 *   envblock - The owning ENVBLOCK
 *
 * Returns: 0=OK, 20=error
 */
int irxinout(int function, PLstr data, struct envblock *envblock);

/* IRXEXEC - Execute a REXX exec */
int irxexec(struct irxexec_plist *plist);

/* IRXEXCOM - Variable access */
int irxexcom(char *irxid, void *dummy1, void *dummy2,
             struct shvblock *shvblock, struct envblock *envblock,
             int *retval);

/* IRXSUBCM - Subcommand table management */
int irxsubcm(void *parms, struct envblock *envblock);

/* IRXRLT - Get result */
int irxrlt(void *parms, struct envblock *envblock);

/* IRXIC - Immediate command / Trace control */
int irxic(void *parms, struct envblock *envblock);

/* IRXJCL - Batch entry point (PGM=IRXJCL) */
int irxjcl(void *cppl_or_parm);

/* ================================================================== */
/*  Internal Helper Functions                                         */
/* ================================================================== */

/* Get the irx_wkblk_int for a given ENVBLOCK */
struct irx_wkblk_int *irx_get_wkblk(struct envblock *envblock);

/* Validate an ENVBLOCK (check eye-catcher, pointers) */
int irx_validate_envblock(struct envblock *envblock);

#endif /* IRXFUNC_H */
