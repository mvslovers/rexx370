/* ------------------------------------------------------------------ */
/*  irx#bcom.c - REXX/370 Bytecode Compiler Skeleton (WP-BC-01)      */
/*                                                                    */
/*  irx_bc_compile() — Entry point.                                  */
/*                                                                    */
/*  Phase 1 scope:                                                    */
/*    - Recognises EXIT and empty clauses only                       */
/*    - Emits OP_NEWCLAUSE + OP_EXIT for explicit EXIT statements    */
/*    - Emits OP_EXIT at source end (implicit program termination)   */
/*    - Any other construct returns IRXBC_ERR_UNSUP                  */
/*                                                                    */
/*  Memory: caller must free the returned irx_bc_execblk with        */
/*    irxstor(RXSMFRE, 0, &p, envblock)                              */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxbops.h"
#include "irxbvm.h"
#include "irxexbl.h"
#include "irxfunc.h"
#include "irxtokn.h"
#include "irxwkblk.h"

/* Maximum bytecode bytes the Phase 1 compiler can emit. */
#define BCOM_MAX_CODE 64

/* ------------------------------------------------------------------ */
/*  emit — append one opcode byte to the local buffer.               */
/*  Returns 0 on success, IRXBC_ERR_STOR if the buffer is full.     */
/* ------------------------------------------------------------------ */
static int emit(unsigned char *buf, int *len, unsigned char op)
{
    if (*len >= BCOM_MAX_CODE)
    {
        return IRXBC_ERR_STOR;
    }
    buf[(*len)++] = op;
    return IRXBC_OK;
}

/* ------------------------------------------------------------------ */
/*  irx_bc_compile                                                    */
/* ------------------------------------------------------------------ */
int irx_bc_compile(struct envblock *envblock,
                   const char *source, int source_len,
                   struct irx_bc_execblk **bc_out)
{
    struct irx_token *tokens = NULL;
    int tok_count = 0;
    struct irx_tokn_error tok_err;
    unsigned char code[BCOM_MAX_CODE];
    int code_len = 0;
    int rc = IRXBC_OK;
    int i;
    int hit_exit = 0;
    struct irx_bc_execblk *bc = NULL;
    int total_size;
    void *mem = NULL;

    memset(&tok_err, 0, sizeof(tok_err));

    if (bc_out == NULL)
    {
        return IRXBC_ERR_UNSUP;
    }
    *bc_out = NULL;

    /* 1. Tokenize -------------------------------------------------- */
    rc = irx_tokn_run(envblock, source, source_len,
                      &tokens, &tok_count, &tok_err);
    if (rc != 0)
    {
        return IRXBC_ERR_TOKN;
    }

    /* 2. Walk token stream ----------------------------------------- */
    for (i = 0; i < tok_count && !hit_exit; i++)
    {
        const struct irx_token *t = &tokens[i];

        switch (t->tok_type)
        {
            case TOK_EOC:
                /* Empty clause boundary — no bytecode emitted. */
                break;

            case TOK_EOF:
                /* End of source — stop walking. */
                i = tok_count; /* break outer loop */
                break;

            case TOK_SYMBOL:
                if (t->tok_upper != NULL &&
                    strcmp(t->tok_upper, "EXIT") == 0)
                {
                    /* EXIT statement: emit clause marker + exit opcode. */
                    rc = emit(code, &code_len, OP_NEWCLAUSE);
                    if (rc != IRXBC_OK)
                    {
                        goto cleanup;
                    }
                    rc = emit(code, &code_len, OP_EXIT);
                    if (rc != IRXBC_OK)
                    {
                        goto cleanup;
                    }
                    hit_exit = 1;
                }
                else
                {
                    /* Any other symbol in Phase 1 is unsupported. */
                    rc = IRXBC_ERR_UNSUP;
                    goto cleanup;
                }
                break;

            default:
                /* Any token type not listed above is unsupported. */
                rc = IRXBC_ERR_UNSUP;
                goto cleanup;
        }
    }

    /* 3. Implicit program exit ------------------------------------- */
    if (!hit_exit)
    {
        rc = emit(code, &code_len, OP_EXIT);
        if (rc != IRXBC_OK)
        {
            goto cleanup;
        }
    }

    /* 4. Allocate EXECBLK + bytecode payload ----------------------- */
    total_size = (int)sizeof(struct irx_bc_execblk) + code_len;
    if (irxstor(RXSMGET, total_size, &mem, envblock) != 0)
    {
        rc = IRXBC_ERR_STOR;
        goto cleanup;
    }

    /* 5. Populate header ------------------------------------------ */
    bc = (struct irx_bc_execblk *)mem;
    memset(bc, 0, (size_t)total_size);
    memcpy(bc->magic, IRXBC_MAGIC, sizeof(bc->magic));
    bc->version = IRXBC_VERSION;
    bc->flags = 0;
    bc->const_count = 0;
    bc->symbol_count = 0;
    bc->code_length = (uint32_t)code_len;
    bc->entry_offset = 0;
    bc->trace_map_offset = 0;

    /* 6. Copy bytecode payload ------------------------------------ */
    memcpy(IRXBC_CODE(bc), code, (size_t)code_len);

    *bc_out = bc;
    bc = NULL; /* ownership transferred */

cleanup:
    if (tokens != NULL)
    {
        irx_tokn_free(envblock, tokens, tok_count);
    }
    if (bc != NULL)
    {
        /* Allocation succeeded but a later step failed. */
        void *p = bc;
        irxstor(RXSMFRE, 0, &p, envblock);
    }
    return rc;
}
