/* ------------------------------------------------------------------ */
/*  irxinit.c - IRXINIT: Initialize a Language Processor Environment  */
/*                                                                    */
/*  Provides irx_init_initenvb(), the 9-step C-core that implements   */
/*  the INITENVB function code: previous-env lookup, PARMBLOCK        */
/*  inheritance, ENVBLOCK allocation, IRXANCHR slot claim, and        */
/*  ECTENVBK update for TSO environments.                              */
/*                                                                    */
/*  Also provides irxinit(), the IBM-compatible compat wrapper that   */
/*  calls irx_init_initenvb() and then installs the full IRXEXTE,     */
/*  SUBCOMTB, and interpreter Work Block.                             */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 15 (IRXINIT)                            */
/*  Ref: CON-1 §3.1 (ENVBLOCK), §3.2 (PARMBLOCK inheritance),         */
/*       §3.8 (IRXEXTE), §6.2 (env-type detection),                   */
/*       §6.3 (INITENVB algorithm)                                     */
/*  Ref: CON-3 (ECTENVBK semantics — TSOFL-conditional, IRXPROBE-     */
/*       verified Phase α: TSOFL=1 unconditional overwrite,           */
/*       TSOFL=0 leave slot untouched)                                 */
/*  Ref: CON-14 (IRXPROBE Phase α actions A1/A3)                      */
/*  Ref: CON-4 (VERSION='0042', SLOT_FREE=0x00)                       */
/*  Ref: WP-I1c.1, TSK-195                                            */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "irx.h"
#include "irx_init.h"
#include "irxanchr.h"
#include "irxbif.h"
#include "irxbifs.h"
#include "irxenv.h"
#include "irxfunc.h"
#include "irxio.h"
#include "irxpars.h"
#include "irxwkblk.h"

#ifdef __MVS__
#include <clibos.h> /* __load(), __delete() — crent370 */
#endif

/* Lock the CON-1 §3.1 ENVBLOCK size on MVS — the IBM-reserved tail
 * at +304..+319 must stay intact so the physical layout remains
 * byte-exact against SC28-1883-0/-4. Only meaningful on the real
 * target; host builds use 8-byte pointers and have a different layout
 * which is irrelevant since host tests never exchange binaries with MVS.
 * _Static_assert is C11; c2asm370 is gnu99 — use typedef-array idiom. */
#ifdef __MVS__
typedef char envblock_size_is_320_[(sizeof(struct envblock) == 320) ? 1 : -1];
#endif

/* ECT ENVBK slot offset inside the ECT control block (IBM SYS1.AMODGEN). */
#define ECT_ENVBK_OFF 0x030

/* Default host command environment names (8 bytes each, EBCDIC-safe). */
#define DEFAULT_HOSTENV_TSO  "TSO     "
#define DEFAULT_HOSTENV_MVS  "MVS     "
#define DEFAULT_HOSTENV_LINK "LINK    "
#define DEFAULT_HANDLER_NAME "IRXSTAM "

#define DEFAULT_SUBCOMTB_ENTRIES 8

/* Allocate via irxstor; jump to cleanup: on failure. */
#define ALLOC(ptr, size, envblk)                                  \
    do                                                            \
    {                                                             \
        void *_tmp = NULL;                                        \
        int _rc = irxstor(RXSMGET, (int)(size), &_tmp, (envblk)); \
        if (_rc != 0)                                             \
            goto cleanup;                                         \
        (ptr) = _tmp;                                             \
    } while (0)

/* ================================================================== */
/*  Shared helper: load_default_parmblock                             */
/*                                                                    */
/*  CON-1 §6.3 step 4 module fallback. When no caller PARMBLOCK and   */
/*  no previous-env PARMBLOCK can supply the defaults, load the       */
/*  default PARMBLOCK module (IRXTSPRM under TSO, IRXPARMS in batch)  */
/*  and copy its FLAGS / MASKS / LANGUAGE / SUBPOOL into the          */
/*  effective slots. The module is DELETE'd before returning — only   */
/*  the byte values are needed; pb_copy is built later in step 5.     */
/*                                                                    */
/*  Returns 0 on success (defaults applied), non-zero on LOAD or      */
/*  validation failure (caller falls back to hardcoded defaults).     */
/*                                                                    */
/*  ISPF detection (IRXISPRM) and caller-supplied PARMMOD overrides   */
/*  are deferred. Storage for the loaded module is whatever subpool   */
/*  __load() chose (subpool 0); we DELETE it immediately.             */
/* ================================================================== */

#ifdef __MVS__
static int load_default_parmblock(int is_tso,
                                  unsigned char eff_flags[4],
                                  unsigned char eff_masks[4],
                                  unsigned char eff_language[3],
                                  int *eff_subpool)
{
    const char *modname = is_tso ? "IRXTSPRM" : "IRXPARMS";
    unsigned size = 0;
    char ac = 0;
    void *ep = __load(NULL, modname, &size, &ac);

    if (ep == NULL)
    {
        return 4; /* LOAD failed */
    }

    struct parmblock *modpb = (struct parmblock *)ep;
    int rc = 0;

    /* All three default modules carry the IBM-standard 'IRXPARMS'
     * eye-catcher per asm/irx{parms,tsprm,isprm}.asm. Validate before
     * trusting the data. */
    if (memcmp(modpb->parmblock_id, PARMBLOCK_ID, 8) != 0 ||
        memcmp(modpb->parmblock_version, PARMBLOCK_VERSION_0042, 4) != 0)
    {
        rc = 8; /* eye-catcher / version mismatch */
    }
    else
    {
        memcpy(eff_flags, modpb->parmblock_flags, 4);
        memcpy(eff_masks, modpb->parmblock_masks, 4);
        memcpy(eff_language, modpb->parmblock_language, 3);
        *eff_subpool = modpb->parmblock_subpool;
    }

    /* Always release the module: we only needed the byte values. */
    (void)__delete(modname);
    return rc;
}
#endif /* __MVS__ */

/* ================================================================== */
/*  Shared helper: init_subcomtb                                      */
/* ================================================================== */

static int init_subcomtb(struct subcomtb_header **hdr_out,
                         struct parmblock *pb,
                         struct envblock *envblk)
{
    struct subcomtb_header *hdr = NULL;
    struct subcomtb_entry *entries = NULL;
    int used = 0;

    ALLOC(hdr, sizeof(struct subcomtb_header), envblk);
    ALLOC(entries,
          DEFAULT_SUBCOMTB_ENTRIES * sizeof(struct subcomtb_entry),
          envblk);

    memcpy(entries[used].subcomtb_name, DEFAULT_HOSTENV_MVS, 8);
    memcpy(entries[used].subcomtb_routine, DEFAULT_HANDLER_NAME, 8);
    memset(entries[used].subcomtb_token, ' ', 16);
    used++;

    memcpy(entries[used].subcomtb_name, DEFAULT_HOSTENV_TSO, 8);
    memcpy(entries[used].subcomtb_routine, DEFAULT_HANDLER_NAME, 8);
    memset(entries[used].subcomtb_token, ' ', 16);
    used++;

    memcpy(entries[used].subcomtb_name, DEFAULT_HOSTENV_LINK, 8);
    memcpy(entries[used].subcomtb_routine, DEFAULT_HANDLER_NAME, 8);
    memset(entries[used].subcomtb_token, ' ', 16);
    used++;

    hdr->subcomtb_first = entries;
    hdr->subcomtb_total = DEFAULT_SUBCOMTB_ENTRIES;
    hdr->subcomtb_used = used;
    hdr->subcomtb_length = SUBCOMTB_ENTRY_LEN;
    memcpy(hdr->subcomtb_initial, DEFAULT_HOSTENV_MVS, 8);
    memset(hdr->_filler1, 0, 8);
    memset(hdr->subcomtb_ffff, 0xFF, 8);

    pb->parmblock_subcomtb = hdr;

    *hdr_out = hdr;
    return 0;

cleanup:
    if (entries != NULL)
    {
        void *p = entries;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (hdr != NULL)
    {
        void *p = hdr;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    return 20;
}

/* ================================================================== */
/*  Shared helper: init_wkblk_int                                     */
/* ================================================================== */

static int init_wkblk_int(struct irx_wkblk_int **wk_out,
                          struct envblock *envblk)
{
    struct irx_wkblk_int *wk = NULL;

    ALLOC(wk, sizeof(struct irx_wkblk_int), envblk);

    memcpy(wk->wkbi_id, WKBLK_INT_ID, 4);
    wk->wkbi_length = (int)sizeof(struct irx_wkblk_int);
    wk->wkbi_envblock = envblk;

    wk->wkbi_digits = NUMERIC_DIGITS_DEFAULT;
    wk->wkbi_fuzz = NUMERIC_FUZZ_DEFAULT;
    wk->wkbi_form = NUMFORM_SCIENTIFIC;

    wk->wkbi_trace = TRACE_NORMAL;
    wk->wkbi_sigl = 0;
    wk->wkbi_rc = 0;

    *wk_out = wk;
    return 0;

cleanup:
    return 20;
}

/* ================================================================== */
/*  irx_init_initenvb — INITENVB 9-step C-core (WP-I1c.1, WP-I1c.4)  */
/*                                                                    */
/*  Steps:                                                            */
/*   1. Previous-env lookup with module fallback                      */
/*      (caller hint → TCB anchor find → LOAD IRXTSPRM/IRXPARMS;      */
/*      parent-TCB walk still stubbed — WP-I1c.2)                     */
/*   2. PARMBLOCK build with flags/mask inheritance (CON-1 §3.2)      */
/*   3. Env-type detection: TSOFL from parmblock or is_tso()          */
/*   4. ENVBLOCK allocation (VERSION='0042', 320 bytes on MVS;        */
/*      subpool from eff_subpool via stack-local bootstrap parmblock) */
/*   5. PARMBLOCK copy allocation and link                            */
/*   6. IRXEXTE allocation + default routine pointers                 */
/*      (irxuid / irxmsgid / irxinout — WP-I1c.4)                     */
/*   7. IRXANCHR slot allocation                                      */
/*   8. ECTENVBK unconditional overwrite when TSOFL=1 (MVS only;     */
/*      IRXPROBE-verified Phase α, CON-14 cases A1/A3)               */
/*   9. Return *out_envblock, reason_code=0                           */
/*                                                                    */
/*  Returns: 0=OK, 20=error (out_reason_code set)                    */
/* ================================================================== */

int irx_init_initenvb(struct envblock *prev_envblock,
                      struct parmblock *caller_parmblock,
                      uint32_t user_field,
                      struct envblock **out_envblock,
                      int *out_reason_code)
{
    struct envblock *envblk = NULL;
    struct parmblock *pb_copy = NULL;
    struct irxexte *exte = NULL;
    int reason = 0;
    int tso_flag = 0; /* resolved in step 3 via is_tso() or caller PARMBLOCK */

    /* Effective parmblock fields, computed in step 2. */
    unsigned char eff_flags[4];
    unsigned char eff_masks[4];
    unsigned char eff_language[3];
    int eff_subpool = 0;

    if (out_envblock == NULL || out_reason_code == NULL)
    {
        return 20;
    }

    /* ----------------------------------------------------------------
     * Step 1: Previous-env lookup with module fallback.
     *
     * Priority order (CON-1 §6.3):
     *   a) Caller-supplied prev_envblock hint (if eye-catcher valid)
     *   b) Non-reentrant: find by current TCB in IRXANCHR table
     *   c) Parent-TCB walk (TSO only)            — stubbed, WP-I1c.2
     *   d) Module fallback: LOAD IRXTSPRM/IRXPARMS (CON-1 §6.3 step 4)
     *
     * (d) avoids the subpool-0 trap that otherwise costs ENVBLOCK
     * survival across subtask transitions under TSO: the IRXTSPRM
     * module sets SUBPOOL=78 (TSO private storage), which lets the
     * ENVBLOCK outlive the allocating subtask. With hardcoded
     * defaults (subpool 0) the env dies at TINITVL CALL return.
     * Live-verified 2026-04-28.
     *
     * P2 PARMMOD (caller-supplied module name from VLIST P2) is
     * forwarded through the wrapper but NOT honoured here yet — the
     * use-case for letting a caller pick a different default module
     * has not materialised. When it does, replace the auto-pick below
     * with the caller string.
     * ---------------------------------------------------------------- */
    {
        struct envblock *prev = NULL;

        /* (a) Caller-supplied hint */
        if (prev_envblock != NULL &&
            memcmp(prev_envblock->envblock_id, ENVBLOCK_ID, 8) == 0)
        {
            prev = prev_envblock;
        }

        /* (b) TCB-based lookup via IRXANCHR */
        if (prev == NULL)
        {
            void *tcb = NULL;
#ifdef __MVS__
            tcb = *(void **)0x21C; /* PSATOLD */
#endif
            if (tcb != NULL)
            {
                irxanchr_entry_t *slot = irx_anchor_find_by_tcb(tcb);
                if (slot != NULL)
                {
                    prev = (struct envblock *)(unsigned long)slot->envblock_ptr;
                }
            }
        }

        /* Resolve the effective parmblock from prev (if any). */
        if (prev != NULL && prev->envblock_parmblock != NULL)
        {
            struct parmblock *prev_pb =
                (struct parmblock *)prev->envblock_parmblock;
            memcpy(eff_flags, prev_pb->parmblock_flags, 4);
            memcpy(eff_masks, prev_pb->parmblock_masks, 4);
            memcpy(eff_language, prev_pb->parmblock_language, 3);
            eff_subpool = prev_pb->parmblock_subpool;
        }
        else
        {
            /* (d) Module fallback. On MVS attempt LOAD; on host or
             * LOAD failure, fall back to hardcoded defaults. */
            int loaded = 0;
#ifdef __MVS__
            if (load_default_parmblock(is_tso(),
                                       eff_flags, eff_masks,
                                       eff_language, &eff_subpool) == 0)
            {
                loaded = 1;
            }
#endif
            if (!loaded)
            {
                memset(eff_flags, 0, 4);
                memset(eff_masks, 0, 4);
                memcpy(eff_language, "ENU", 3);
                eff_subpool = 0;
            }
        }
    }

    /* ----------------------------------------------------------------
     * Step 2: PARMBLOCK build with flags/mask inheritance.
     *
     * CON-1 §3.2: for each flag byte i:
     *   new_flags[i] = (prev_flags[i] & ~caller_masks[i])
     *                | (caller_flags[i] & caller_masks[i])
     *
     * Where caller_masks[i] == 0 means "inherit from prev" and
     * caller_masks[i] == 0xFF means "use caller value".
     * ---------------------------------------------------------------- */
    if (caller_parmblock != NULL)
    {
        int i;
        for (i = 0; i < 4; i++)
        {
            unsigned char cmask = caller_parmblock->parmblock_masks[i];
            eff_flags[i] =
                (eff_flags[i] & (unsigned char)(~cmask)) |
                (caller_parmblock->parmblock_flags[i] & cmask);
        }
        if (caller_parmblock->parmblock_subpool != 0)
        {
            eff_subpool = caller_parmblock->parmblock_subpool;
        }
        memcpy(eff_language, caller_parmblock->parmblock_language, 3);
    }

    /* ----------------------------------------------------------------
     * Step 3: Env-type detection (CON-1 §6.2).
     *
     * TSOFL=1 → TSO environment. Detection hierarchy:
     *   - If the caller's parmblock had tsofl_mask set, respect it.
     *   - Otherwise auto-detect via is_tso().
     *
     * The resolved tso_flag value is reflected into pb_copy via the
     * bitfield accessor in step 5 (not by byte-level OR'ing here).
     * Byte-level writes against tsofl are platform-specific because
     * the int bitfield ordering differs between MVS (MSB-first) and
     * the cross-compile host (LSB-first); the bitfield accessor lays
     * down the right bit on whichever platform is compiling.
     * ---------------------------------------------------------------- */
    if (caller_parmblock != NULL && caller_parmblock->tsofl_mask)
    {
        tso_flag = (caller_parmblock->tsofl != 0) ? 1 : 0;
    }
    else
    {
        tso_flag = is_tso();
    }

    /* ----------------------------------------------------------------
     * Bootstrap subpool plumbing for steps 4 and 5.
     *
     * irxstor() reads the target subpool from
     * envblock->envblock_parmblock->parmblock_subpool. At this point
     * we have neither — the ENVBLOCK is what step 4 is about to
     * allocate. To get it into eff_subpool (e.g. 78 under TSO so the
     * env survives subtask transitions) we hand irxstor a stack-local
     * synthetic context that exposes only the resolved subpool.
     *
     * The synthetic structs live for the duration of irx_init_initenvb
     * and are referenced briefly by envblk->envblock_parmblock between
     * step 4 and step 5; after step 5 we overwrite that slot with the
     * heap-allocated pb_copy. RXSMFRE does not read parmblock_subpool
     * (freemain recovers it from getmain's prefix), so the stale
     * pointer in cleanup paths is harmless.
     * ---------------------------------------------------------------- */
    struct parmblock bootstrap_pb;
    struct envblock bootstrap_env;
    memset(&bootstrap_pb, 0, sizeof(bootstrap_pb));
    memset(&bootstrap_env, 0, sizeof(bootstrap_env));
    bootstrap_pb.parmblock_subpool = eff_subpool;
    bootstrap_env.envblock_parmblock = &bootstrap_pb;

    /* ----------------------------------------------------------------
     * Step 4: ENVBLOCK allocation (GETMAIN, subpool eff_subpool,
     * 320 bytes on MVS, eye-catcher 'ENVBLOCK', version '0042').
     * ---------------------------------------------------------------- */
    {
        void *storage = NULL;
        int rc = irxstor(RXSMGET, (int)sizeof(struct envblock),
                         &storage, &bootstrap_env);
        if (rc != 0)
        {
            reason = 1;
            goto cleanup;
        }
        envblk = (struct envblock *)storage;
    }

    memcpy(envblk->envblock_id, ENVBLOCK_ID, 8);
    memcpy(envblk->envblock_version, ENVBLOCK_VERSION_0042, 4);
    envblk->envblock_length = (int)sizeof(struct envblock);
    envblk->envblock_userfield = (void *)(unsigned long)user_field;
    /* Temporary parmblock pointer so step 5 (and any follow-up
     * irxstor call) reads the right subpool. Replaced with pb_copy
     * at the end of step 5. */
    envblk->envblock_parmblock = &bootstrap_pb;

    /* ----------------------------------------------------------------
     * Step 5: PARMBLOCK copy allocation.
     * ---------------------------------------------------------------- */
    {
        void *storage = NULL;
        int rc = irxstor(RXSMGET, (int)sizeof(struct parmblock),
                         &storage, envblk);
        if (rc != 0)
        {
            reason = 2;
            goto cleanup;
        }
        pb_copy = (struct parmblock *)storage;
    }

    memcpy(pb_copy->parmblock_id, PARMBLOCK_ID, 8);
    memcpy(pb_copy->parmblock_version, PARMBLOCK_VERSION_0042, 4);
    memcpy(pb_copy->parmblock_language, eff_language, 3);
    pb_copy->parmblock_subpool = eff_subpool;
    memcpy(pb_copy->parmblock_flags, eff_flags, 4);
    memset(pb_copy->parmblock_masks, 0, 4);
    memset(pb_copy->parmblock_addrspn, ' ', 8);
    memset(pb_copy->parmblock_ffff, 0xFF, 8);

    /* Reflect the resolved TSOFL through the bitfield accessor so byte
     * and bitfield views agree on both MVS (MSB-first) and host
     * (LSB-first). Mask is set to mark the value as caller-honoured for
     * any future inheritance lookup. Signed 1-bit field: -1 for true. */
    pb_copy->tsofl_mask = -1;
    pb_copy->tsofl = tso_flag ? -1 : 0;

    envblk->envblock_parmblock = pb_copy;

    /* ----------------------------------------------------------------
     * Step 6: IRXEXTE allocation + default routine pointers.
     *
     * Allocate sizeof(struct irxexte) bytes (zero-filled by GETMAIN /
     * calloc), set the entry count, and install the rexx370 internal
     * default routines for the slots that have a default implementation
     * today: IRXUID, IRXMSGID, IRXINOUT (plus the active-routine peers).
     * Slots whose service is not yet implemented (IRXEXEC, IRXLOAD,
     * IRXJCL, IRXSTK, IRXSAY, IRXERS, IRXHST, IRXHLT, IRXTXT, IRXLIN,
     * IRXRTE, IRXEXCOM, IRXIC, IRXSUBCM, IRXTERMA, IRXRLT) stay NULL
     * and will be filled when the corresponding service is built.
     * IRXINIT/IRXTERM self-references are installed by the compat
     * wrapper (Phase 6) which knows the wrapper symbol addresses.
     *
     * Lifetime contract: the irxuid / irxmsgid / irxinout symbols
     * resolve to CSECTs linked into the IRXINIT load module. The
     * pointers installed here remain dereferencable for as long as
     * that load module is resident in the calling task. Production
     * callers (IRXTMPW under TSO logon — WP-I1c.6) hold IRXINIT
     * resident for the entire session, so the pointers are valid for
     * every subtask that finds the ENVBLOCK via IRXANCHR / ECTENVBK.
     * Ad-hoc callers that DELETE IRXINIT (or end the task that
     * LOADed it) before the ENVBLOCK is reused will invalidate these
     * pointers. See follow-up TSK — Replaceable-Routine Load-Module
     * Strategy: https://www.notion.so/3503d99387878124811ae0aae197277d
     *
     * MODNAMET non-blank entries (replaceable-routine module overrides
     * via LOAD EP=) are intentionally ignored in this phase. The default
     * routines are always installed; caller-supplied module names for
     * IORT, EXROUT, GETFREER, EXECINIT, ATTNROUT, STACKRT, IRXEXECX,
     * IDROUT, MSGIDRT, EXECTERM will be honoured in a future phase
     * when an actual use-case appears. See follow-up TSK
     * https://www.notion.so/3503d99387878124811ae0aae197277d
     * (Replaceable-Routine Load-Module Strategy). MODNAMET DDNAME
     * slots (INDD/OUTDD/LOADDD) are read by IRXEXEC (WP-I3) later.
     * ---------------------------------------------------------------- */
    {
        void *storage = NULL;
        int rc = irxstor(RXSMGET, (int)sizeof(struct irxexte),
                         &storage, envblk);
        if (rc != 0)
        {
            reason = 3;
            goto cleanup;
        }
        exte = (struct irxexte *)storage;
    }

    exte->irxexte_entry_count = IRXEXTE_ENTRY_COUNT;
    exte->irxuid = (void *)irxuid;
    exte->userid_routine = (void *)irxuid;
    exte->irxmsgid = (void *)irxmsgid;
    exte->msgid_routine = (void *)irxmsgid;
    exte->irxinout = (void *)irxinout;
    exte->io_routine = (void *)irxinout;
    envblk->envblock_irxexte = exte;

    /* ----------------------------------------------------------------
     * Step 7: IRXANCHR slot allocation.
     *
     * The table is append-only (no recycling) and holds 62 usable
     * slots. On MVS each step starts fresh; on the cross-compile host
     * a long-running test process may exhaust the table. A full table
     * is not fatal: the environment is fully usable without a
     * registered slot — it just won't appear in irx_anchor_find_*
     * queries until a slot is freed by irxterm() and the next MVS step
     * starts with a clean table.
     * ---------------------------------------------------------------- */
    {
        void *tcb = NULL;
#ifdef __MVS__
        tcb = *(void **)0x21C; /* PSATOLD */
#endif
        uint32_t slot_token = 0;
        /* Ignore IRX_ANCHOR_RC_FULL — non-fatal, env remains usable. */
        (void)irx_anchor_alloc_slot(envblk, tcb, tso_flag, &slot_token);
    }

    /* ----------------------------------------------------------------
     * Step 8: ECTENVBK unconditional overwrite when TSOFL=1.
     *
     * IRXPROBE Phase α (TSK-192, CON-14) confirmed IBM writes
     * ECTENVBK unconditionally when TSOFL=1 (case A1: pre 18B09C90 →
     * post 18B7BC90 even though pre was non-NULL) and leaves it
     * untouched when TSOFL=0 (case A3: pre and post identical, slot
     * flags zero). This replaces the earlier conservative
     * claim-if-null hold-out.
     *
     * Not executed on host builds — the host wrapper irxinit() does
     * the equivalent simulation write after irx_init_initenvb returns
     * (using pb_copy->tsofl which is platform-portable).
     * ---------------------------------------------------------------- */
#ifdef __MVS__
    if (tso_flag)
    {
        void *ect = anch_walk();
        if (ect != NULL)
        {
            struct envblock **slot =
                (struct envblock **)((char *)ect + ECT_ENVBK_OFF);
            *slot = envblk;
            envblk->envblock_ectptr = ect;
        }
    }
    /* TSOFL=0: ECTENVBK is intentionally not touched. The env is
     * registered in the IRXANCHR table only — no TSO binding. */
#endif

    /* ----------------------------------------------------------------
     * Step 9: Return the new environment.
     * ---------------------------------------------------------------- */
    *out_envblock = envblk;
    *out_reason_code = 0;
    return 0;

cleanup:
    if (exte != NULL)
    {
        void *p = exte;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (pb_copy != NULL)
    {
        void *p = pb_copy;
        irxstor(RXSMFRE, 0, &p, envblk);
    }
    if (envblk != NULL)
    {
        void *p = envblk;
        irxstor(RXSMFRE, 0, &p, NULL);
    }

    *out_reason_code = reason;
    return 20;
}

/* ================================================================== */
/*  irx_init_findenvb — FINDENVB C-core (WP-I1c.2)                   */
/*                                                                    */
/*  Returns the most recently allocated (highest token) active,       */
/*  non-reentrant IRXANCHR slot whose TCB matches PSATOLD.            */
/*                                                                    */
/*  We iterate the table directly (rather than calling                */
/*  irx_anchor_find_by_tcb) for two reasons:                          */
/*    1. irx_anchor_find_by_tcb returns the highest-token entry       */
/*       regardless of the RENTRANT flag; we need to skip reentrant   */
/*       environments.                                                */
/*    2. irx_anchor_find_by_tcb early-exits on NULL tcb, but on the  */
/*       cross-compile host all slots record tcb_ptr=0 (PSATOLD is   */
/*       unavailable), so the NULL guard would prevent host testing.  */
/*                                                                    */
/*  Returns: 0=found, 4=no non-reentrant env on this TCB.            */
/* ================================================================== */

int irx_init_findenvb(struct envblock **out_envblock, int *out_reason_code)
{
    irxanchr_header_t *hdr;
    irxanchr_entry_t *slots;
    irxanchr_entry_t *best = NULL;
    uint32_t tcbptr;
    uint32_t i;

    if (out_envblock == NULL || out_reason_code == NULL)
    {
        if (out_reason_code != NULL)
        {
            *out_reason_code = 4;
        }
        return 4;
    }

    *out_envblock = NULL;
    *out_reason_code = 0;

    {
        void *tcb = NULL;
#ifdef __MVS__
        tcb = *(void **)0x21C; /* PSATOLD */
#endif
        tcbptr = (uint32_t)(unsigned long)tcb;
    }

    if (irx_anchor_get_handle(&hdr) != IRX_ANCHOR_RC_OK)
    {
        *out_reason_code = 4;
        return 4;
    }

    slots = (irxanchr_entry_t *)((char *)hdr + sizeof(irxanchr_header_t));

    for (i = 0; i < hdr->used; i++)
    {
        struct envblock *eb;
        struct parmblock *pb;

        if (slots[i].envblock_ptr == IRXANCHR_SLOT_FREE ||
            slots[i].envblock_ptr == IRXANCHR_SLOT_SENTINEL)
        {
            continue;
        }
        /* Slot occupancy is determined solely by envblock_ptr; the
         * flags field marks TSO-attachment, not in-use status (CON-14
         * IRXPROBE Phase α). FINDENVB walks both TSO and non-TSO envs. */
        if (slots[i].tcb_ptr != tcbptr)
        {
            continue;
        }

        /* On MVS (24-bit), the stored uint32_t holds the full address.
         * On the 64-bit cross-compile host, alloc_slot stashed the full
         * pointer in rsvd1 to avoid truncation (see irx#anch.c). */
#ifdef __MVS__
        eb = (struct envblock *)(unsigned long)slots[i].envblock_ptr;
#else
        {
            void *full_ptr = NULL;
            memcpy(&full_ptr, slots[i].rsvd1, sizeof(void *));
            eb = (struct envblock *)full_ptr;
        }
#endif
        if (eb == NULL || memcmp(eb->envblock_id, ENVBLOCK_ID, 8) != 0)
        {
            continue;
        }

        /* FINDENVB only returns non-reentrant environments (SC28-1883-0 §14). */
        pb = (struct parmblock *)eb->envblock_parmblock;
        if (pb != NULL && pb->rentrant)
        {
            continue;
        }

        if (best == NULL || slots[i].token > best->token)
        {
            best = &slots[i];
        }
    }

    if (best == NULL)
    {
        *out_reason_code = 4;
        return 4;
    }

    /* Return the envblock address using the same full-pointer recovery
     * as used in the loop above to avoid 32-bit truncation on host. */
#ifdef __MVS__
    *out_envblock = (struct envblock *)(unsigned long)best->envblock_ptr;
#else
    {
        void *full_ptr = NULL;
        memcpy(&full_ptr, best->rsvd1, sizeof(void *));
        *out_envblock = (struct envblock *)full_ptr;
    }
#endif
    return 0;
}

/* ================================================================== */
/*  irx_init_chekenvb — CHEKENVB C-core (WP-I1c.2)                   */
/*                                                                    */
/*  Validates a caller-supplied ENVBLOCK address by checking          */
/*  (1) the 'ENVBLOCK' eye-catcher at offset +0 and                  */
/*  (2) that the address appears in an active IRXANCHR slot.          */
/*                                                                    */
/*  Returns: 0=valid, 20=invalid (out_reason_code set).              */
/* ================================================================== */

int irx_init_chekenvb(struct envblock *envblock, int *out_reason_code)
{
    irxanchr_entry_t *slot;

    if (out_reason_code == NULL)
    {
        return 20;
    }

    *out_reason_code = 0;

    /* NULL or missing eye-catcher — cannot be a valid ENVBLOCK. */
    if (envblock == NULL ||
        memcmp(envblock->envblock_id, ENVBLOCK_ID, 8) != 0)
    {
        *out_reason_code = 4;
        return 20;
    }

    /* Eye-catcher is present; confirm it is registered in IRXANCHR. */
    slot = irx_anchor_find_by_envblock(envblock);
    if (slot == NULL)
    {
        *out_reason_code = 8;
        return 20;
    }

    return 0;
}

/* ================================================================== */
/*  irx_init_dispatch — central IRXINIT dispatcher (WP-I1c.2)        */
/*                                                                    */
/*  Routes a CL8 function code to the appropriate C-core.            */
/*  Designed so WP-I1c.5 (HLASM entry-point wrapper) can call a      */
/*  single C entry point after parsing the caller VLIST.             */
/*                                                                    */
/*  Unknown function code: RC=20, RSN=12.                            */
/* ================================================================== */

int irx_init_dispatch(const char funccode[IRXINIT_FUNCCODE_LEN],
                      struct envblock *prev_envblock,
                      struct parmblock *caller_parmblock,
                      uint32_t user_field,
                      struct envblock **envblock_inout,
                      int *out_reason_code)
{
    if (funccode == NULL || envblock_inout == NULL || out_reason_code == NULL)
    {
        if (out_reason_code != NULL)
        {
            *out_reason_code = 12;
        }
        return 20;
    }

    if (memcmp(funccode, "INITENVB", 8) == 0)
    {
        return irx_init_initenvb(prev_envblock, caller_parmblock,
                                 user_field, envblock_inout, out_reason_code);
    }

    if (memcmp(funccode, "FINDENVB", 8) == 0)
    {
        (void)prev_envblock;
        (void)caller_parmblock;
        (void)user_field;
        return irx_init_findenvb(envblock_inout, out_reason_code);
    }

    if (memcmp(funccode, "CHEKENVB", 8) == 0)
    {
        (void)prev_envblock;
        (void)caller_parmblock;
        (void)user_field;
        return irx_init_chekenvb(*envblock_inout, out_reason_code);
    }

    *out_reason_code = 12;
    return 20;
}

/* ================================================================== */
/*  irxinit — IBM-compatible IRXINIT wrapper                         */
/*                                                                    */
/*  Calls irx_init_initenvb() for the core 9 steps, then installs    */
/*  the full IRXEXTE (real function pointers), SUBCOMTB, internal     */
/*  Work Block, and BIF registry required by the interpreter.         */
/*                                                                    */
/*  On host (non-MVS) builds, mirrors the MVS step 8 contract on the */
/*  simulated ECTENVBK slot: when the resolved env carries TSOFL=1   */
/*  (read via pb->tsofl, portable across MVS MSB-first and host      */
/*  LSB-first int bitfield ordering), the slot is overwritten        */
/*  unconditionally — matching IBM behaviour verified by IRXPROBE     */
/*  Phase α. TSOFL=0 leaves the slot untouched.                       */
/*                                                                    */
/*  Returns: 0=OK, 20=init error                                      */
/* ================================================================== */

int irxinit(void *parms, struct envblock **envblock_ptr)
{
    struct envblock *envblk = NULL;
    struct workblok_ext *wkext = NULL;
    struct subcomtb_header *subcmd = NULL;
    struct irx_wkblk_int *wkbi = NULL;
    int reason = 0;
    int rc;

    if (envblock_ptr == NULL)
    {
        return 20;
    }

    /* Call the 9-step C-core. Pass caller's parmblock directly;
     * TSOFL auto-detection happens inside irx_init_initenvb(). */
    rc = irx_init_initenvb(NULL,
                           (struct parmblock *)parms,
                           0,
                           &envblk,
                           &reason);
    if (rc != 0)
    {
        return 20;
    }

    /* Allocate the Work Block Extension (IBM standard control block). */
    {
        void *storage = NULL;
        rc = irxstor(RXSMGET, (int)sizeof(struct workblok_ext),
                     &storage, envblk);
        if (rc != 0)
        {
            goto cleanup;
        }
        wkext = (struct workblok_ext *)storage;
    }
    envblk->envblock_workblok_ext = wkext;

    /* IRXEXTE default routine pointers (irxuid, irxmsgid, irxinout) are
     * installed by irx_init_initenvb() step 6. Compat-wrapper-specific
     * IRXEXTE overrides — e.g. self-references to the irxinit / irxterm
     * symbols for Phase 6 — would go here. None today. */

    /* SUBCOMTB (host command environments). */
    {
        struct parmblock *pb = (struct parmblock *)envblk->envblock_parmblock;
        rc = init_subcomtb(&subcmd, pb, envblk);
        if (rc != 0)
        {
            goto cleanup;
        }
    }

    /* Internal Work Block (interpreter state). */
    rc = init_wkblk_int(&wkbi, envblk);
    if (rc != 0)
    {
        goto cleanup;
    }
    envblk->envblock_userfield = wkbi;

    /* BIF registry and core registrations (WP-21a). */
    {
        struct irx_bif_registry *reg = NULL;
        rc = irx_bif_create(envblk, &reg);
        if (rc != 0)
        {
            goto cleanup;
        }
        wkbi->wkbi_bif_registry = reg;

        rc = irx_bif_register_all(envblk, reg);
        if (rc != 0)
        {
            goto cleanup;
        }
    }

    /* Host-only: mirror MVS step 8 ECTENVBK semantics on the
     * simulation slot. Read TSOFL via the bitfield accessor to stay
     * portable across MVS (MSB-first) and host (LSB-first) int
     * bitfield encodings — the byte view of parmblock_flags differs
     * between platforms, but pb->tsofl always lands on the right bit
     * for the compiling target. */
#ifndef __MVS__
    {
        struct parmblock *pb = (struct parmblock *)envblk->envblock_parmblock;
        if (pb != NULL && pb->tsofl != 0)
        {
            extern struct envblock **ectenvbk_slot(void);
            struct envblock **slot = ectenvbk_slot();
            if (slot != NULL)
            {
                *slot = envblk;
            }
        }
    }
#endif

    *envblock_ptr = envblk;
    return 0;

cleanup:
    /* irxterm handles full teardown including IRXANCHR slot (WP-I1c.3). */
    if (envblk != NULL)
    {
        irxterm(envblk);
    }
    return 20;
}
