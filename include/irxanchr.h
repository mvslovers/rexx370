/* ------------------------------------------------------------------ */
/*  irxanchr.h - REXX/370 Environment Anchor                          */
/*                                                                    */
/*  Consolidated header for both the ECT-slot anchor API              */
/*  (ECTENVBK read-mostly discipline, CON-1 §6.1) and the            */
/*  IRXANCHR static environment anchor table (WP-I1a.1/WP-I1a.3).    */
/*                                                                    */
/*  === Part 1: ECT-Anchor API ===                                    */
/*                                                                    */
/*  A REXX Language Processor Environment is anchored in the TSO ECT  */
/*  at offset +30 (ECTENVBK). rexx370 treats that slot as read-mostly */
/*  per CON-1 §6.1: at any point it is in one of three states —       */
/*                                                                    */
/*    (a) NULL            no REXX environment active; rexx370 may     */
/*                        claim the slot on IRXINIT                   */
/*                                                                    */
/*    (b) non-rexx370     another REXX holds the anchor (on MVS 3.8j  */
/*                        typically BREXX/370, but the slot is also   */
/*                        compatible with any other env). rexx370     */
/*                        leaves the slot untouched and the caller    */
/*                        tracks its own ENVBLOCK via the IRXINIT     */
/*                        return value                                */
/*                                                                    */
/*    (c) our own env     we claimed the slot earlier; IRXTERM may    */
/*                        clear it back to NULL                       */
/*                                                                    */
/*  Cold-path walk on MVS 3.8j (validated on Hercules since 2019,     */
/*  offsets from IBM macros):                                         */
/*                                                                    */
/*     PSA + PSAAOLD    (0x224) -> ASCB                               */
/*     ASCB + ASCBASXB  (0x06C) -> ASXB                               */
/*     ASXB + ASXBLWA   (0x014) -> LWA                                */
/*     LWA  + LWAPECT   (0x020) -> ECT                                */
/*     ECT  + ECTENVBK  (0x030) -> ENVBLOCK                           */
/*                                                                    */
/*  In batch any link in this chain can be NULL (LWA is typical);     */
/*  the walk returns NULL and the anchor discipline reduces to local   */
/*  field-only semantics on the ENVBLOCK.                             */
/*                                                                    */
/*  IRXTERM (TSOFL=1 env): rolls ECTENVBK back to the most recent     */
/*  TSO-attached predecessor in the IRXANCHR table, or NULL if none.  */
/*  IRXTERM (TSOFL=0 env): never writes ECTENVBK.                     */
/*  The roll-back is guarded: ECTENVBK is only modified when it        */
/*  currently points to the terminating env (coexistence safety).     */
/*  Ref: IRXPROBE Phase α (CON-14), CON-1 §6.1.                      */
/*                                                                    */
/*  === Part 2: IRXANCHR Registry API ===                             */
/*                                                                    */
/*  IRXANCHR is a pre-initialized static data module loaded at        */
/*  runtime via LOAD EP=IRXANCHR. It contains a 32-byte header        */
/*  followed by 64 x 40-byte slots tracking active environments.      */
/*  Slot 0 and Slot 2 are permanent sentinels; neither is allocated.  */
/*  Layout mirrors asm/irxanchr.asm byte-for-byte.                    */
/*                                                                    */
/*  Note: envblock_ptr and tcb_ptr in irxanchr_entry_t are uint32_t   */
/*  (not void*) so sizeof(irxanchr_entry_t) == 40 holds on both       */
/*  MVS 3.8j and the Linux cross-compile host.                        */
/*                                                                    */
/*  Ref: CON-1 §3.1 (ENVBLOCK layout), §6.1 (read-mostly anchor),     */
/*       §6.3 step 8 (IRXINIT), §6.4 step 2 (IRXTERM),                */
/*       §14.2 (20-April decision with coexistence rationale).        */
/*  Ref: WP-I1a.1 (asm/irxanchr.asm), WP-I1a.3 (src/irx#anch.c)     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#ifndef IRXANCHR_H
#define IRXANCHR_H

#include <stddef.h>
#include <stdint.h>

#include "irx.h"

/* ================================================================== */
/*  Part 1: ECT-Anchor API                                            */
/* ================================================================== */

/* Cold-path walk PSA -> ASCB -> ASXB -> LWA -> ECT.
 * Returns ECT address, or NULL if any link in the chain is NULL
 * (typical in batch, where LWA is not established). */
void *anch_walk(void) asm("ANCHWALK");

/* TSO detection via crent370's CLIBPPA.ppaflag
 * (PPAFLAG_TSOFG for TSO foreground, PPAFLAG_TSOBG for TSO background
 * invoked via IKJEFT01). Returns 1 when either bit is set, 0 in pure
 * batch and on non-MVS builds. */
int anch_tso(void) asm("ANCHISTS");

/* Read ECTENVBK — the currently installed ENVBLOCK, or NULL when
 * the slot is empty or unreachable. */
struct envblock *anch_curr(void) asm("ANCHCURR");

/* DEPRECATED — no production callers; pending removal in WP-I1c.5.
 *
 * anch_push implements "read-mostly" claim-if-NULL semantics that
 * are IBM-incompatible: IBM's IRXINIT unconditionally overwrites
 * ECTENVBK regardless of its prior value. Do not add new callers. */
void anch_push(struct envblock *new_env) asm("ANCHPUSH");

/* ================================================================== */
/*  Part 2: IRXANCHR Registry — Constants                             */
/* ================================================================== */

#define IRXANCHR_EYECATCHER  "IRXANCHR" /* 8-byte eye-catcher (EBCDIC on MVS) */
#define IRXANCHR_VERSION     "0042"     /* 4-byte version string (rexx370 deviation from IBM '0100') */
#define IRXANCHR_TOTAL_SLOTS 64         /* total slots in the table            */

/* Value stored in envblock_ptr when a slot is available.
 * rexx370 deviation: 0x00000000 (IBM uses 0xFFFFFFFF as free marker).
 * Permanent sentinel slots use SLOT_SENTINEL — they are never SLOT_FREE. */
#define IRXANCHR_SLOT_FREE     ((uint32_t)0x00000000U)
#define IRXANCHR_SLOT_SENTINEL ((uint32_t)0xFFFFFFFFU)

/* Top bit of the flags field — set when the slot represents a
 * TSO-attached env (i.e. registered via ECTENVBK). For non-TSO envs
 * (TSOFL=0) the flags field stays 0x00000000. Slot occupancy is
 * determined by envblock_ptr (see IRXANCHR_SLOT_FREE / SLOT_SENTINEL),
 * NOT by this bit. Verified against IBM TSO/E z/OS via IRXPROBE
 * Phase α Case A1 vs A3 (CON-14). */
#define IRXANCHR_FLAG_TSO_ATTACHED ((uint32_t)0x40000000U)

/* ================================================================== */
/*  Part 2: IRXANCHR Registry — Structs                               */
/* ================================================================== */

/* Header: 32 bytes at module start */
typedef struct
{
    char id[8];          /* +0x00  'IRXANCHR' eye-catcher */
    char version[4];     /* +0x08  '0042' (rexx370)       */
    uint32_t total;      /* +0x0C  total slot count (64)  */
    uint32_t used;       /* +0x10  high-watermark          */
    uint32_t length;     /* +0x14  bytes per entry (40)   */
    uint8_t reserved[8]; /* +0x18  reserved[0..3] = AS-wide monotonic
                          *        token counter (rexx370 usage);
                          *        reserved[4..7] unused */
} irxanchr_header_t;

/* Entry: 40 bytes per slot */
typedef struct
{
    uint32_t envblock_ptr; /* +0x00  ENVBLOCK addr; IRXANCHR_SLOT_FREE = free */
    uint32_t token;        /* +0x04  slot token returned to allocator         */
    uint8_t rsvd1[16];     /* +0x08  reserved on MVS (24-bit: envblock_ptr
                            *        holds the full address).
                            * On 64-bit cross-compile host only: bytes
                            * [0..sizeof(void*)-1] stash the full envblock
                            * pointer because envblock_ptr truncates to 32
                            * bits.  Written by irx#anch.c alloc_slot(),
                            * read by irx#init.c irx_init_findenvb().
                            * No other writer exists; table_reset clears
                            * all slots including rsvd1. */
    uint32_t anchor_hint;  /* +0x18  opaque hint for fast slot re-find        */
    uint32_t tcb_ptr;      /* +0x1C  TCB address at alloc time                */
    uint32_t flags;        /* +0x20  IRXANCHR_FLAG_TSO_ATTACHED for TSO envs  */
    uint32_t rsvd2;        /* +0x24  reserved                                 */
} irxanchr_entry_t;

/* ================================================================== */
/*  Part 2: IRXANCHR Registry — Compile-time layout verification      */
/*  Typedef-array idiom: array of size -1 triggers a compile error.   */
/* ================================================================== */

typedef char irxanchr_header_size_ok_[(sizeof(irxanchr_header_t) == 32) ? 1 : -1];
typedef char irxanchr_entry_size_ok_[(sizeof(irxanchr_entry_t) == 40) ? 1 : -1];
typedef char irxanchr_entry_token_ofs_[(offsetof(irxanchr_entry_t, token) == 4) ? 1 : -1];
typedef char irxanchr_entry_rsvd1_ofs_[(offsetof(irxanchr_entry_t, rsvd1) == 8) ? 1 : -1];
typedef char irxanchr_entry_hint_ofs_[(offsetof(irxanchr_entry_t, anchor_hint) == 24) ? 1 : -1];
typedef char irxanchr_entry_tcb_ofs_[(offsetof(irxanchr_entry_t, tcb_ptr) == 28) ? 1 : -1];
typedef char irxanchr_entry_flags_ofs_[(offsetof(irxanchr_entry_t, flags) == 32) ? 1 : -1];

/* ================================================================== */
/*  Part 2: IRXANCHR Registry — Return codes                          */
/* ================================================================== */

#define IRX_ANCHOR_RC_OK        0 /* success                           */
#define IRX_ANCHOR_RC_FULL      1 /* alloc: no free slots left         */
#define IRX_ANCHOR_RC_NOT_FOUND 2 /* free: envblock not in table       */
#define IRX_ANCHOR_RC_LOAD_FAIL 3 /* get_handle: LOAD EP=IRXANCHR fail */
#define IRX_ANCHOR_RC_BAD_EYE   4 /* get_handle: eye-catcher mismatch  */

/* ================================================================== */
/*  Part 2: IRXANCHR Registry — API prototypes                        */
/*  Implementation in WP-I1a.3 (src/irx#anch.c).                     */
/* ================================================================== */

/* Claim a free slot; sets envblock_ptr, token, tcb_ptr, flags.
 * is_tso=1 sets flags=IRXANCHR_FLAG_TSO_ATTACHED (TSO env); is_tso=0
 * leaves flags=0x00000000 (non-TSO env). The flag does NOT track
 * slot occupancy — that lives in envblock_ptr.
 * Returns 0 on success, non-zero if table full or invalid. */
int irx_anchor_alloc_slot(void *envblock, void *tcb, int is_tso,
                          uint32_t *out_token) asm("ANCHALOC");

/* Release the slot for envblock (sets envblock_ptr = IRXANCHR_SLOT_FREE).
 * USED remains at high-watermark (never decremented).
 * Returns 0 on success, non-zero if not found. */
int irx_anchor_free_slot(void *envblock) asm("ANCHFREE");

/* Locate slot by envblock or tcb. Returns NULL if not found. */
irxanchr_entry_t *irx_anchor_find_by_envblock(void *envblock) asm("ANCHFENV");
irxanchr_entry_t *irx_anchor_find_by_tcb(void *tcb) asm("ANCHFTCB");

/* Walk backwards from current_slot-1 to slot 0. Returns the first slot
 * that is occupied by a TSO-attached env (envblock_ptr is USED and
 * flags has IRXANCHR_FLAG_TSO_ATTACHED set). Returns NULL when no such
 * predecessor exists.
 *
 * Used exclusively by irx_init_term() to compute the ECTENVBK rollback
 * target (IRXPROBE Phase α, CON-14). */
irxanchr_entry_t *irx_anchor_find_previous_used(
    const irxanchr_entry_t *current_slot) asm("ANCHFPRV");

/* LOAD EP=IRXANCHR wrapper; verifies eye-catcher.
 * Returns 0 on success, non-zero on load/validation failure. */
int irx_anchor_get_handle(irxanchr_header_t **out_anchor) asm("ANCHGET");

/* ================================================================== */
/*  Cross-compile test helper (host builds only)                      */
/* ================================================================== */

/* Returns a pointer to the host-side simulation buffer used by
 * irx_anchor_get_handle() on non-MVS builds. Tests may corrupt or
 * inspect this buffer directly. Never call on MVS. */
#ifndef __MVS__
void *_irxanchr_host_buf(void);
#endif

/* Reset the IRXANCHR table to initial state (USED=0, token counter=0,
 * all slots free, sentinels restored).
 *
 * WARNING: Test-only. On MVS this modifies the loaded module in place.
 * MUST NOT be called from production IRXINIT/IRXTERM code paths, or
 * from any context where other tasks may be scanning the registry —
 * the reset is not atomic and would wreck concurrent readers. */
void irx_anchor_table_reset(void) asm("ANCHRTST");

#endif /* IRXANCHR_H */
