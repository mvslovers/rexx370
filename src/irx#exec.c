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
#include "irxfunc.h"
#include "irxexec.h"
#include "irxlstr.h"
#include "irxtokn.h"
#include "irxvpool.h"
#include "irxpars.h"
#include "irxctrl.h"

int irx_exec_run(const char *source, int source_len,
                 const char *args, int args_len,
                 int *rc_out, struct envblock *envblock)
{
    int                   own_env   = 0;
    struct irx_token     *tokens    = NULL;
    int                   tok_count = 0;
    struct irx_tokn_error tok_err;
    struct lstr_alloc    *alloc     = NULL;
    struct irx_vpool     *vpool     = NULL;
    struct irx_parser     parser;
    int                   rc;

    memset(&parser,  0, sizeof(parser));
    memset(&tok_err, 0, sizeof(tok_err));

    /* 1. Environment ------------------------------------------------ */
    if (envblock == NULL) {
        rc = irxinit(NULL, &envblock);
        if (rc != 0) return rc;
        own_env = 1;
    }

    /* 2. Allocator bridge (WP-11b) ---------------------------------- */
    alloc = irx_lstr_init(envblock);
    if (alloc == NULL) { rc = 20; goto cleanup; }

    /* 3. Tokenize --------------------------------------------------- */
    rc = irx_tokn_run(envblock, source, source_len,
                      &tokens, &tok_count, &tok_err);
    if (rc != 0) goto cleanup;

    /* 4. Variable pool ---------------------------------------------- */
    vpool = vpool_create(alloc, NULL);
    if (vpool == NULL) { rc = 20; goto cleanup; }

    /* 5. Parser init ------------------------------------------------- */
    rc = irx_pars_init(&parser, tokens, tok_count, vpool, alloc, envblock);
    if (rc != 0) goto cleanup;

    /* 5b. Top-level argument setup (WP-17) -------------------------- */
    if (args != NULL && args_len > 0) {
        Lstr *la;
        int  *le;
        la = (Lstr *)alloc->alloc(
            (size_t)IRX_MAX_ARGS * sizeof(Lstr), alloc->ctx);
        le = (int  *)alloc->alloc(
            (size_t)IRX_MAX_ARGS * sizeof(int),  alloc->ctx);
        if (la == NULL || le == NULL) {
            if (la != NULL)
                alloc->dealloc(la, (size_t)IRX_MAX_ARGS * sizeof(Lstr),
                               alloc->ctx);
            if (le != NULL)
                alloc->dealloc(le, (size_t)IRX_MAX_ARGS * sizeof(int),
                               alloc->ctx);
            rc = 20;
            goto cleanup;
        }
        memset(la, 0, (size_t)IRX_MAX_ARGS * sizeof(Lstr));
        memset(le, 0, (size_t)IRX_MAX_ARGS * sizeof(int));
        if (Lfx(alloc, &la[0], (size_t)args_len) != LSTR_OK) {
            alloc->dealloc(la, (size_t)IRX_MAX_ARGS * sizeof(Lstr),
                           alloc->ctx);
            alloc->dealloc(le, (size_t)IRX_MAX_ARGS * sizeof(int),
                           alloc->ctx);
            rc = 20;
            goto cleanup;
        }
        memcpy(la[0].pstr, args, (size_t)args_len);
        la[0].len  = (size_t)args_len;
        la[0].type = LSTRING_TY;
        le[0]                = 1;
        parser.call_args       = la;
        parser.call_arg_exists = le;
        parser.call_argc       = 1;
    }

    /* 6. Label scan ------------------------------------------------- */
    rc = irx_ctrl_label_scan(&parser);
    if (rc != 0) goto cleanup;

    /* 7. Execute ----------------------------------------------------- */
    rc = irx_pars_run(&parser);

    if (rc_out != NULL)
        *rc_out = parser.exit_rc;

cleanup:
    irx_ctrl_cleanup(&parser);
    irx_pars_cleanup(&parser);
    if (vpool  != NULL) vpool_destroy(vpool);
    if (tokens != NULL) irx_tokn_free(envblock, tokens, tok_count);
    if (own_env)        irxterm(envblock);
    return rc;
}
