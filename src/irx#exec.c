/* ------------------------------------------------------------------ */
/*  irx#exec.c - REXX/370 End-to-End Execution + IRXEXEC Service     */
/*                                                                    */
/*  irx_exec_run()      — Phase 2 pipeline (WP-18)                    */
/*  irx_exec_dispatch() — IRXEXEC Programming Service C-core          */
/*                        (z/OS 10-slot VLIST, WP-CPS-06 / TSK-218)  */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irx_init.h"
#include "irxbops.h"
#include "irxbvm.h"
#include "irxctrl.h"
#include "irxexbl.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxlstr.h"
#include "irxpars.h"
#include "irxtokn.h"
#include "irxvpool.h"
#include "irxwkblk.h"

/* ================================================================== */
/*  irx_exec_dispatch — IRXEXEC Programming Service C-core            */
/*  (asm() alias: IRXEDISP, called from asm/irxexec.asm)              */
/*                                                                    */
/*  Implements the z/OS 10-slot IRXEXEC VLIST form. SC28-1883-0 V1    */
/*  had a shorter parameter list; this dispatcher targets the z/OS    */
/*  stage of the spec. See WP-CPS-06 / TSK-218 for full rationale.   */
/* ================================================================== */

/* Validate ENVBLOCK eye-catcher. Returns non-zero if invalid. */
static int exec_envblk_bad(const struct envblock *env)
{
    return (env == NULL ||
            memcmp(env->envblock_id, ENVBLOCK_ID,
                   sizeof(env->envblock_id)) != 0);
}

/* Return non-zero if all n bytes of s are spaces (or NUL). */
static int exec_blank_n(const unsigned char *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
    {
        if (s[i] != (unsigned char)' ' && s[i] != '\0')
        {
            return 0;
        }
    }
    return 1;
}

int irx_exec_dispatch(struct execblk *execblk,
                      void *argtable,
                      int flags,
                      struct instblk *instblk,
                      void *reserved_parm5,
                      struct evalblock *evalblock,
                      void *workarea,
                      void *userfield,
                      struct envblock *envblock,
                      struct envblock *envblock_r0)
{
    struct envblock *env = NULL;
    int rsn = 0;
    const struct argtable_entry *ae;
    const char *first_arg = NULL;
    int first_arg_len = 0;
    int n_args = 0;
    struct instblk_entry *ents;
    int n_ents;
    char *src_buf = NULL;
    int src_len = 0;
    int total_src;
    int exit_rc = 0;
    int rc;
    int i;
    /* ARGTABLE_END is "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF" (8 bytes). */
    unsigned char all_ff[sizeof(ARGTABLE_END) - 1];

    /* P3 bit fields (COMMAND/FUNCTION/SUBROUTINE, extended-RC) are
     * parsed by the asm wrapper and forwarded as a plain int.
     * Call-type routing and bit 3 (extended syntax errors) are
     * deferred to WP-CPS-06b; all paths route to the same engine. */
    (void)flags;
    /* P5 reserved; P7 workarea and P8 userfield are for future use. */
    (void)reserved_parm5;
    (void)workarea;
    (void)userfield;

    memcpy(all_ff, ARGTABLE_END, sizeof(all_ff));

    /* ---- 1. Env resolution (three-path) ---- */
    if (envblock != NULL)
    {
        if (exec_envblk_bad(envblock))
        {
            return IRXEXEC_BADPLIST;
        }
        env = envblock;
    }
    else if (envblock_r0 != NULL)
    {
        if (exec_envblk_bad(envblock_r0))
        {
            return IRXEXEC_BADPLIST;
        }
        env = envblock_r0;
    }
    else
    {
        /* FINDENVB: locate env registered on current TCB. */
        if (irx_init_findenvb(&env, &rsn) != 0)
        {
            /* TODO(WP-CPS-06b): auto-init env when none found */
            return IRXEXEC_NOENV;
        }
    }

    /* ---- 2. Source acquisition ---- */
    if (instblk != NULL)
    {
        if (memcmp(instblk->instblk_acronym, INSTBLK_ID,
                   sizeof(instblk->instblk_acronym)) != 0)
        {
            return IRXEXEC_BADPLIST;
        }
        /* INSTBLK provided — use it; execblk metadata still processed below */
    }
    else if (execblk != NULL)
    {
        if (memcmp(execblk->exec_blk_acryn, EXECBLK_ID,
                   sizeof(execblk->exec_blk_acryn)) != 0)
        {
            return IRXEXEC_BADPLIST;
        }
        if (execblk->exec_blk_length != EXECBLK_V1_LEN &&
            execblk->exec_blk_length != EXECBLK_V2_LEN)
        {
            return IRXEXEC_BADPLIST;
        }
        /* NULL INSTBLK + valid EXECBLK = DD-based load path.
         * DD loading is IRXLOAD's job; IRXEXEC requires a caller-supplied
         * INSTBLK. The parameter list is well-formed, so this is
         * IRXEXEC_ERROR (RC=20), not BADPLIST (RC=32). */
        return IRXEXEC_ERROR;
    }
    else
    {
        /* Neither INSTBLK nor EXECBLK — cannot determine source. */
        return IRXEXEC_BADPLIST;
    }

    /* ---- 3. EXECBLK SUBCOM override ---- */
    if (execblk != NULL &&
        !exec_blank_n(execblk->exec_subcom,
                      (int)sizeof(execblk->exec_subcom)))
    {
        struct irx_wkblk_int *wk =
            (struct irx_wkblk_int *)env->envblock_userfield;
        if (wk != NULL)
        {
            memcpy(wk->wkbi_address, execblk->exec_subcom,
                   sizeof(execblk->exec_subcom));
        }
    }

    /* ---- 4. EVALBLOCK validation ---- */
    if (evalblock != NULL)
    {
        if (evalblock->evalblock_evpad1 != 0 ||
            evalblock->evalblock_evpad2 != 0 ||
            evalblock->evalblock_evlen != 0)
        {
            return IRXEXEC_BADPLIST;
        }
    }

    /* ---- 5. ARGTABLE parse ---- */
    /* Walk 8-byte entries (MVS-native: 4-byte ptr + 4-byte len) until
     * the X'FFFFFFFFFFFFFFFF' terminator.  ae++ steps by
     * sizeof(struct argtable_entry) which is consistent with
     * build_mock_argtable on both MVS and host. */
    if (argtable != NULL)
    {
        ae = (const struct argtable_entry *)argtable;
        while (memcmp(ae, all_ff, sizeof(all_ff)) != 0)
        {
            if (n_args == 0)
            {
                first_arg = (const char *)ae->argstring_ptr;
                first_arg_len = ae->argstring_length;
            }
            n_args++;
            ae++;
        }
    }
    /* Note: n_args counted but only first arg forwarded to engine.
     * Full multi-arg forwarding is WP-CPS-06b (TSK-223). */
    (void)n_args;

    /* ---- 6. Source reconstruction from INSTBLK ---- */
    ents = (struct instblk_entry *)instblk->instblk_address;
    n_ents = (instblk->instblk_usedlen > 0)
                 ? instblk->instblk_usedlen / (int)sizeof(struct instblk_entry)
                 : 0;

    /* Single-allocation source pool: sum(stmtlen) + (n-1) separators. */
    total_src = 0;
    for (i = 0; i < n_ents; i++)
    {
        total_src += ents[i].instblk_stmtlen;
    }
    if (n_ents > 1)
    {
        total_src += n_ents - 1;
    }

    {
        void *sb = NULL;
        if (irxstor(RXSMGET, total_src > 0 ? total_src : 1, &sb, env) != 0)
        {
            return IRXEXEC_ERROR;
        }
        src_buf = (char *)sb;
    }

    /* Concatenate lines with '\n' between them. c2asm370 translates
     * the '\n' literal to EBCDIC 0x15 at compile time; the tokenizer
     * (irx#tokn.c) treats that byte as a line separator on MVS. */
    src_len = 0;
    for (i = 0; i < n_ents; i++)
    {
        if (i > 0)
        {
            src_buf[src_len++] = '\n';
        }
        if (ents[i].instblk_stmtlen > 0)
        {
            memcpy(src_buf + src_len, ents[i].instblk_stmt_,
                   (size_t)ents[i].instblk_stmtlen);
            src_len += ents[i].instblk_stmtlen;
        }
    }

    /* ---- 7. Engine call ---- */
    /* EXECBLK DSNPTR/DSNLEN (PARSE SOURCE token4/5): the current
     * irx_exec_run does not accept DSN parameters — known gap,
     * does not block this WP. */
    rc = irx_exec_run(src_buf, src_len, first_arg, first_arg_len,
                      &exit_rc, env);

    /* ---- 8. EVALBLOCK write (NORESULT marker) ---- */
    /* Signals to the caller that no result string is available in
     * EVDATA. WP-CPS-06b fills EVDATA with the actual result when
     * the engine tracks return values. */
    if (evalblock != NULL)
    {
        evalblock->evalblock_evlen = EVALBLOCK_NORESULT;
    }

    /* ---- 9. Free source buffer ---- */
    {
        void *p = src_buf;
        irxstor(RXSMFRE, 0, &p, env);
    }

    /* ---- 10. Return engine exit code (→ R15 via asm wrapper) ---- */
    if (rc != 0)
    {
        return rc;
    }
    return exit_rc;
}

int irx_exec_run(const char *source, int source_len,
                 const char *args, int args_len,
                 int *rc_out, struct envblock *envblock)
{
    int own_env = 0;
    struct irx_token *tokens = NULL;
    int tok_count = 0;
    struct irx_tokn_error tok_err;
    struct lstr_alloc *alloc = NULL;
    struct irx_vpool *vpool = NULL;
    struct irx_parser parser;
    int rc;

    /* Save/restore slots for the source-retention fields. Populated at
     * step 2b (after we have a live envblock + wkblk), restored on
     * cleanup. Supports nested exec_run (e.g. the future INTERPRET
     * instruction from WP-23) without the inner call wiping the
     * outer's retention. */
    void *saved_source = NULL;
    int saved_source_len = 0;
    int retention_saved = 0;

    memset(&parser, 0, sizeof(parser));
    memset(&tok_err, 0, sizeof(tok_err));

    /* 1. Environment ------------------------------------------------ */
    if (envblock == NULL)
    {
        rc = irxinit(NULL, &envblock);
        if (rc != 0)
        {
            return rc;
        }
        own_env = 1;
    }

    /* 2. Allocator bridge (WP-11b) ---------------------------------- */
    alloc = irx_lstr_init(envblock);
    if (alloc == NULL)
    {
        rc = 20;
        goto cleanup;
    }

    /* 2b. Retain source pointer on the work block so SOURCELINE can
     * read it back. The caller-owned source buffer outlives the run;
     * we save the previous retention values and restore them on
     * cleanup so nested exec_run calls (future INTERPRET) don't
     * clobber an outer invocation's retention. */
    {
        struct irx_wkblk_int *wk =
            (struct irx_wkblk_int *)envblock->envblock_userfield;
        if (wk != NULL)
        {
            saved_source = wk->wkbi_source;
            saved_source_len = wk->wkbi_source_len;
            retention_saved = 1;
            wk->wkbi_source = (void *)source;
            wk->wkbi_source_len = source_len;
        }
    }

    /* 3. Bytecode path (default-on; opt-out via REXX370_BYTECODE=0) - */
    {
        struct irx_wkblk_int *wk =
            (struct irx_wkblk_int *)envblock->envblock_userfield;
        if (wk != NULL && wk->wkbi_use_bytecode)
        {
            struct irx_bc_execblk *bc = NULL;
            int bc_rc = 0;

            rc = irx_bc_compile(envblock, source, source_len, &bc);
            if (rc == IRXBC_ERR_UNSUP)
            {
                /* Unsupported construct — release bc and fall through
                 * to the token-walk path below. */
                if (bc != NULL)
                {
                    void *p = bc;
                    irxstor(RXSMFRE, 0, &p, envblock);
                }
                wk->wkbi_bc_fallback_count++;
            }
            else
            {
                if (rc == IRXBC_OK)
                {
                    rc = irx_bc_execute(envblock, bc, args, args_len, &bc_rc);
                    wk->wkbi_bc_exec_count++;
                }
                if (rc_out != NULL)
                {
                    *rc_out = bc_rc;
                }
                if (bc != NULL)
                {
                    void *p = bc;
                    irxstor(RXSMFRE, 0, &p, envblock);
                }
                goto cleanup;
            }
        }
    }

    /* 3. Tokenize --------------------------------------------------- */
    rc = irx_tokn_run(envblock, source, source_len,
                      &tokens, &tok_count, &tok_err);
    if (rc != 0)
    {
        goto cleanup;
    }

    /* 4. Variable pool ---------------------------------------------- */
    vpool = vpool_create(alloc, NULL);
    if (vpool == NULL)
    {
        rc = 20;
        goto cleanup;
    }

    /* 5. Parser init ------------------------------------------------- */
    rc = irx_pars_init(&parser, tokens, tok_count, vpool, alloc, envblock);
    if (rc != 0)
    {
        goto cleanup;
    }

    /* 5b. Top-level argument setup (WP-17) -------------------------- */
    if (args != NULL && args_len > 0)
    {
        Lstr *la;
        int *le;
        la = (Lstr *)alloc->alloc(
            (size_t)IRX_MAX_ARGS * sizeof(Lstr), alloc->ctx);
        le = (int *)alloc->alloc(
            (size_t)IRX_MAX_ARGS * sizeof(int), alloc->ctx);
        if (la == NULL || le == NULL)
        {
            if (la != NULL)
            {
                alloc->dealloc(la, (size_t)IRX_MAX_ARGS * sizeof(Lstr),
                               alloc->ctx);
            }
            if (le != NULL)
            {
                alloc->dealloc(le, (size_t)IRX_MAX_ARGS * sizeof(int),
                               alloc->ctx);
            }
            rc = 20;
            goto cleanup;
        }
        memset(la, 0, (size_t)IRX_MAX_ARGS * sizeof(Lstr));
        memset(le, 0, (size_t)IRX_MAX_ARGS * sizeof(int));
        if (Lfx(alloc, &la[0], (size_t)args_len) != LSTR_OK)
        {
            alloc->dealloc(la, (size_t)IRX_MAX_ARGS * sizeof(Lstr),
                           alloc->ctx);
            alloc->dealloc(le, (size_t)IRX_MAX_ARGS * sizeof(int),
                           alloc->ctx);
            rc = 20;
            goto cleanup;
        }
        memcpy(la[0].pstr, args, (size_t)args_len);
        la[0].len = (size_t)args_len;
        la[0].type = LSTRING_TY;
        le[0] = 1;
        parser.call_args = la;
        parser.call_arg_exists = le;
        parser.call_argc = 1;
    }

    /* 6. Label scan ------------------------------------------------- */
    rc = irx_ctrl_label_scan(&parser);
    if (rc != 0)
    {
        goto cleanup;
    }

    /* 7. Execute ----------------------------------------------------- */
    rc = irx_pars_run(&parser);

    if (rc_out != NULL)
    {
        *rc_out = parser.exit_rc;
    }

cleanup:
    irx_ctrl_cleanup(&parser);
    irx_pars_cleanup(&parser);
    if (vpool != NULL)
    {
        vpool_destroy(vpool);
    }
    if (tokens != NULL)
    {
        irx_tokn_free(envblock, tokens, tok_count);
    }
    /* Restore the pre-call retention values before we return — the
     * caller's source buffer stops being valid for us once control
     * leaves here, and any outer exec_run further up the stack must
     * see its own retention preserved. retention_saved guards against
     * restoring stale zeros when we jumped to cleanup before step 2b. */
    if (retention_saved && envblock != NULL)
    {
        struct irx_wkblk_int *wk =
            (struct irx_wkblk_int *)envblock->envblock_userfield;
        if (wk != NULL)
        {
            wk->wkbi_source = saved_source;
            wk->wkbi_source_len = saved_source_len;
        }
    }
    if (own_env)
    {
        irxterm(envblock);
    }
    return rc;
}
