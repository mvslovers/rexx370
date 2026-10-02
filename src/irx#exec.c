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
#include "irxcond.h"
#include "irxctrl.h"
#include "irxemsg.h"
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
    return irx_exec_dispatch_stk(execblk, argtable, flags, instblk,
                                 reserved_parm5, evalblock, workarea,
                                 userfield, envblock, envblock_r0, NULL);
}

int irx_exec_dispatch_stk(struct execblk *execblk,
                          void *argtable,
                          int flags,
                          struct instblk *instblk,
                          void *reserved_parm5,
                          struct evalblock *evalblock,
                          void *workarea,
                          void *userfield,
                          struct envblock *envblock,
                          struct envblock *envblock_r0,
                          void *stack_end)
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
            (struct irx_wkblk_int *)env->envblock_workblok_ext;
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
            irx_emsg_syntax(env, SYNTAX_STORAGE, 0, NULL, 0);
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
    /* "Error running <name>" names the INSTBLK member (#281); the
     * previous name comes back afterwards for a nested exec. */
    char saved_name[sizeof(((struct irx_wkblk_int *)0)->wkbi_exec_name)];
    struct irx_wkblk_int *wkn =
        (struct irx_wkblk_int *)env->envblock_workblok_ext;
    if (!WKBI_HAS(wkn, wkbi_exec_name))
    {
        wkn = NULL; /* environment from an older IRXINIT */
    }
    if (wkn != NULL)
    {
        int nl = (int)sizeof(instblk->instblk_member);
        memcpy(saved_name, wkn->wkbi_exec_name, sizeof(saved_name));
        while (nl > 0 && instblk->instblk_member[nl - 1] == ' ')
        {
            nl--;
        }
        memcpy(wkn->wkbi_exec_name, instblk->instblk_member, (size_t)nl);
        wkn->wkbi_exec_name[nl] = '\0';
    }

    /* An error that ends the exec is recorded by the message it
     * prints; clear the slot so an earlier run cannot answer. */
    struct irx_wkblk_int *wke =
        (struct irx_wkblk_int *)env->envblock_workblok_ext;
    if (wke != NULL)
    {
        wke->wkbi_error_number = 0;
    }

    /* The C stack this run may use (#296), for its duration: a nested
     * IRXEXEC runs on a stack of its own and restores the outer end. */
    void *saved_stack_end = NULL;
    int has_stack_end = WKBI_HAS(wke, wkbi_stack_end);
    if (has_stack_end)
    {
        saved_stack_end = wke->wkbi_stack_end;
        if (stack_end != NULL)
        {
            wke->wkbi_stack_end = stack_end;
        }
    }

    rc = irx_exec_run(src_buf, src_len, first_arg, first_arg_len,
                      &exit_rc, env);

    if (has_stack_end)
    {
        wke->wkbi_stack_end = saved_stack_end;
    }
    if (wkn != NULL)
    {
        memcpy(wkn->wkbi_exec_name, saved_name, sizeof(saved_name));
    }

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
    /* An exec that ended in error n returns 20000 + n (irxexec.h). */
    if (rc != 0 && wke != NULL && wke->wkbi_error_number >= SYNTAX_MIN &&
        wke->wkbi_error_number <= SYNTAX_MAX)
    {
        return IRXEXEC_SYNTAX_BASE + wke->wkbi_error_number;
    }
    if (rc != 0)
    {
        return rc;
    }
    return exit_rc;
}

/* Bytecode compile-time limitations that the token-walk interpreter can
 * still run.  Each is raised inside irx_bc_compile() (during bc_program),
 * BEFORE any bytecode executes, so falling back re-runs the program from a
 * clean slate with no risk of double side effects:
 *   UNSUP          - a construct the compiler does not yet handle
 *   STRTOOLONG     - a literal or symbol exceeds IRXBC_STR_MAX; the bytecode
 *                    const/symbol table stores its length in a single byte,
 *                    so 64+ byte strings cannot be represented (the
 *                    interpreter has no such limit)
 *   PARSE_COMPOUND - a compound-variable target in a PARSE template
 *   CAPACITY       - the program overflows one of the compiler's fixed
 *                    tables (code / constants / symbols); the interpreter
 *                    has no such limits.  This is a *capacity* limit, kept
 *                    distinct from IRXBC_ERR_STOR (a real irxstor failure,
 *                    which stays fatal — see irx#bcom.c).
 * Execute-time errors from irx_bc_execute (OPCODE/ARITH/STACK/IO/...) are
 * deliberately excluded: that bytecode already ran with side effects and
 * must stay fatal rather than silently re-run under the interpreter. */
static int bc_err_is_fallback(int rc)
{
    return rc == IRXBC_ERR_UNSUP ||
           rc == IRXBC_ERR_STRTOOLONG ||
           rc == IRXBC_ERR_PARSE_COMPOUND ||
           rc == IRXBC_ERR_CAPACITY;
}

/* ------------------------------------------------------------------ */
/*  Error reporting outside the VM (#281)                             */
/* ------------------------------------------------------------------ */

/* Tokenizer error -> SYNTAX number (Appendix A). */
static int tokn_errnum(int code)
{
    switch (code)
    {
        case TOKERR_STORAGE:
            return SYNTAX_STORAGE;
        case TOKERR_UNTERMINATED_STR:
        case TOKERR_UNTERMINATED_CMT:
            return 6; /* unmatched comment or quote */
        case TOKERR_INVALID_HEX:
        case TOKERR_ODD_HEX_GROUP:
        case TOKERR_INVALID_BIN:
        case TOKERR_BAD_BIN_GROUP:
            return 15; /* invalid hex constant */
        case TOKERR_BAD_CHAR:
            return 13; /* invalid character in data */
        default:
            return SYNTAX_INTERNAL;
    }
}

/* A compile failure that is not a fallback -> SYNTAX number. */
static int bc_compile_errnum(int rc)
{
    switch (rc)
    {
        case IRXBC_ERR_STOR:
            return SYNTAX_STORAGE;
        case IRXBC_ERR_LOOP:
            return SYNTAX_CTL_STACK;
        default:
            return SYNTAX_INTERNAL;
    }
}

/* Token-walk interpreter error -> SYNTAX number: what the failing
 * operation raised since `base`, else a default for the parser code. */
static int pars_errnum(int code, struct envblock *env, unsigned int base)
{
    const struct irx_wkblk_int *wk =
        (const struct irx_wkblk_int *)env->envblock_workblok_ext;
    if (wk != NULL &&
        !(WKBI_HAS(wk, wkbi_cond_seq) && wk->wkbi_cond_seq == base) &&
        wk->wkbi_last_condition != NULL && wk->wkbi_last_condition->valid &&
        wk->wkbi_last_condition->code >= SYNTAX_MIN &&
        wk->wkbi_last_condition->code <= SYNTAX_MAX)
    {
        return wk->wkbi_last_condition->code;
    }
    switch (code)
    {
        case IRXPARS_NOMEM:
            return SYNTAX_STORAGE;
        case IRXPARS_DIVZERO:
        case IRXPARS_OVERFLOW:
            return SYNTAX_OVERFLOW;
        case IRXPARS_BADFUNC:
            return SYNTAX_NO_ROUTINE;
        case IRXPARS_BADARG:
            return SYNTAX_BAD_CALL;
        default:
            return SYNTAX_INTERNAL;
    }
}

/* Report with the whole source line as the one +++ entry.  The
 * token-walk path knows the line of an error but not its clause, and
 * keeps no call stack for a full traceback (it is frozen, CON-18). */
static void report_line(struct envblock *env, int errnum, int line,
                        const char *source, int source_len)
{
    struct irx_emsg_clause c;
    int cur = 1;
    int i = 0;

    if (line <= 0 || source == NULL)
    {
        irx_emsg_syntax(env, errnum, line, NULL, 0);
        return;
    }
    while (i < source_len && cur < line)
    {
        if (source[i++] == '\n')
        {
            cur++;
        }
    }
    int end = i;
    while (end < source_len && source[end] != '\n')
    {
        end++;
    }
    int start = i;
    while (start < end && source[start] == ' ')
    {
        start++;
    }
    c.line = line;
    c.level = 1;
    c.text = source + start;
    c.text_len = end - start;
    irx_emsg_syntax(env, errnum, line, &c, 1);
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
    unsigned int cond_base = 0; /* #281: conditions after this are ours */

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
            (struct irx_wkblk_int *)envblock->envblock_workblok_ext;
        if (wk != NULL)
        {
            saved_source = wk->wkbi_source;
            saved_source_len = wk->wkbi_source_len;
            retention_saved = 1;
            wk->wkbi_source = (void *)source;
            wk->wkbi_source_len = source_len;
            if (WKBI_HAS(wk, wkbi_cond_seq))
            {
                cond_base = wk->wkbi_cond_seq;
            }
        }
    }

    /* 3. Bytecode path (default-on; opt-out via REXX370_BYTECODE=0) - */
    {
        struct irx_wkblk_int *wk =
            (struct irx_wkblk_int *)envblock->envblock_workblok_ext;
        if (wk != NULL && wk->wkbi_use_bytecode)
        {
            struct irx_bc_execblk *bc = NULL;
            int bc_rc = 0;
            int unsup_reason = 0;
            int unsup_line = 0;

            rc = irx_bc_compile(envblock, source, source_len, &bc,
                                &unsup_reason, &unsup_line);
            if (bc_err_is_fallback(rc))
            {
                /* A compile-time limitation (unsupported construct, an
                 * over-long literal/symbol, ...) — release bc and fall
                 * through to the token-walk path below, which has no such
                 * limits.  No bytecode ran, so this is side-effect free. */
                if (bc != NULL)
                {
                    void *p = bc;
                    irxstor(RXSMFRE, 0, &p, envblock);
                }
                wk->wkbi_bc_fallback_count++;
                /* Record the FIRST fallback's reason/line for the
                 * REXX370_BCDEBUG diagnostic (WP-BC-DIAG).  First wins
                 * so the earliest cause survives later fallbacks. */
                if (wk->wkbi_bc_unsup_reason == 0)
                {
                    wk->wkbi_bc_unsup_reason = unsup_reason;
                    wk->wkbi_bc_unsup_line = unsup_line;
                }
            }
            else if (rc == IRXBC_ERR_TOKN)
            {
                /* The source does not scan.  Nothing ran; the token-walk
                 * path below tokenizes again and reports the error with
                 * its line (#281).  Not a fallback: nothing is counted. */
            }
            else
            {
                if (rc != IRXBC_OK)
                {
                    /* Compile failed outright (storage, nesting): before
                     * any clause, so the message carries no line. */
                    irx_emsg_syntax(envblock, bc_compile_errnum(rc), 0, NULL,
                                    0);
                }
                if (rc == IRXBC_OK)
                {
                    rc = irx_bc_execute(envblock, bc, source, source_len, args,
                                        args_len, &bc_rc);
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
        report_line(envblock, tokn_errnum(tok_err.error_code),
                    tok_err.error_line, source, source_len);
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
    if (rc != 0)
    {
        /* A BIF failing in the token walk returns without the parser's
         * fail(), so no error line is recorded; the token the parser
         * stopped at still names it (#281). */
        int line = parser.error_line;
        if (line <= 0 && parser.tokens != NULL && parser.tok_count > 0)
        {
            int tp = parser.tok_pos;
            if (tp >= parser.tok_count)
            {
                tp = parser.tok_count - 1;
            }
            if (tp > 0 && parser.tokens[tp].tok_type == TOK_EOF)
            {
                tp--;
            }
            if (tp >= 0)
            {
                line = parser.tokens[tp].tok_line;
            }
        }
        report_line(envblock,
                    pars_errnum(parser.error_code, envblock, cond_base), line,
                    source, source_len);
    }

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
            (struct irx_wkblk_int *)envblock->envblock_workblok_ext;
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
