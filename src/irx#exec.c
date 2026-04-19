/* ------------------------------------------------------------------ */
/*  irx#exec.c - REXX/370 End-to-End Execution (WP-18)                */
/*                                                                    */
/*  irx_exec_run() wires all Phase 2 components together:             */
/*  environment -> tokenizer -> variable pool -> parser -> cleanup.   */
/*                                                                    */
/*  This is pure glue — no new logic. Each step uses the public API   */
/*  of the component it calls.                                         */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxctrl.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxlstr.h"
#include "irxpars.h"
#include "irxtokn.h"
#include "irxvpool.h"
#include "irxwkblk.h"

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
