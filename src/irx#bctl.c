/* ------------------------------------------------------------------ */
/*  irx#bctl.c - REXX/370 Bytecode Control-Flow Utilities (WP-BC-03) */
/*                                                                    */
/*  irx_bc_disasm() — walk an irx_bc_execblk code stream and produce  */
/*  a human-readable listing.  Used by tests and trace output.        */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irxbctl.h"
#include "irxbops.h"
#include "irxexbl.h"

/* ================================================================== */
/*  Opcode name table                                                 */
/* ================================================================== */

/* clang-format off */
static const struct { unsigned char op; const char *name; } op_names[] = {
    { OP_NOP,       "NOP"       },
    { OP_EXIT,      "EXIT"      },
    { OP_NEWCLAUSE, "NEWCLAUSE" },
    { OP_EXIT_RC,   "EXIT_RC"   },
    { OP_JMP,       "JMP"       },
    { OP_JF,        "JF"        },
    { OP_JT,        "JT"        },
    { OP_PUSH_LIT,  "PUSH_LIT"  },
    { OP_PUSH_TMP,  "PUSH_TMP"  },
    { OP_PUSH_OMITTED, "PUSH_OMITTED" },
    { OP_POP,       "POP"       },
    { OP_DUP,       "DUP"       },
    { OP_LOAD,      "LOAD"      },
    { OP_STORE,     "STORE"     },
    { OP_DROP,      "DROP"      },
    { OP_ADD,       "ADD"       },
    { OP_SUB,       "SUB"       },
    { OP_MUL,       "MUL"       },
    { OP_DIV,       "DIV"       },
    { OP_IDIV,      "IDIV"      },
    { OP_MOD,       "MOD"       },
    { OP_POW,       "POW"       },
    { OP_NEG,       "NEG"       },
    { OP_EQ,        "EQ"        },
    { OP_NE,        "NE"        },
    { OP_LT,        "LT"        },
    { OP_LE,        "LE"        },
    { OP_GT,        "GT"        },
    { OP_GE,        "GE"        },
    { OP_DEQ,       "DEQ"       },
    { OP_DNE,       "DNE"       },
    { OP_DLT,       "DLT"       },
    { OP_DLE,       "DLE"       },
    { OP_DGT,       "DGT"       },
    { OP_DGE,       "DGE"       },
    { OP_AND,       "AND"       },
    { OP_OR,        "OR"        },
    { OP_XOR,       "XOR"       },
    { OP_NOT,       "NOT"       },
    { OP_CONCAT,    "CONCAT"    },
    { OP_BCONCAT,   "BCONCAT"   },
    { OP_SAY,       "SAY"       },
    { OP_TOINT,     "TOINT"     },
    { OP_FORINIT,   "FORINIT"   },
    { OP_BYINIT,    "BYINIT"    },
    { OP_DECFOR,    "DECFOR"    },
    { OP_DOTEST,    "DOTEST"    },
    { OP_ITERATE,   "ITERATE"   },
    { OP_LEAVE,     "LEAVE"     },
    { OP_LABEL,         "LABEL"         },
    { OP_CALL,          "CALL"          },
    { OP_CALL_BIF,      "CALL_BIF"      },
    { OP_RETURN,        "RETURN"        },
    { OP_RETURNV,       "RETURNV"       },
    { OP_PARSE_BEGIN,   "PARSE_BEGIN"   },
    { OP_PARSE_END,     "PARSE_END"     },
    { OP_PVAR,          "PVAR"          },
    { OP_PDOT,          "PDOT"          },
    { OP_TR_SPACE,      "TR_SPACE"      },
    { OP_TR_LIT,        "TR_LIT"        },
    { OP_TR_ABS,        "TR_ABS"        },
    { OP_TR_REL,        "TR_REL"        },
    { OP_TR_END,        "TR_END"        },
    { OP_TR_VAR,        "TR_VAR"        },
    { OP_PUSH_SOURCE,   "PUSH_SOURCE"   },
    { OP_PUSH_NUMERIC,  "PUSH_NUMERIC"  },
    { OP_PROC,             "PROC"             },
    { OP_EXPOSE,           "EXPOSE"           },
    { OP_EXPOSE_INDIRECT,  "EXPOSE_INDIRECT"  },
    { OP_LOAD_STEM,        "LOAD_STEM"        },
    { OP_STORE_STEM,       "STORE_STEM"       },
    { OP_DROP_STEM,        "DROP_STEM"        },
    { OP_PVAR_STEM,        "PVAR_STEM"        },
    { OP_PULL_FROM_QUEUE,  "PULL_FROM_QUEUE"  },
    { OP_SIGNAL,           "SIGNAL"           },
    { OP_SIGNAL_VALUE,     "SIGNAL_VALUE"     },
    { OP_SIGNAL_ON,        "SIGNAL_ON"        },
    { OP_SIGNAL_OFF,       "SIGNAL_OFF"       },
    { OP_TRACE_TOGGLE,     "TRACE_TOGGLE"     },
    { OP_TRACE_SET,        "TRACE_SET"        },
    { OP_TRACE_VALUE,      "TRACE_VALUE"      },
    { OP_ADDRESS_TOGGLE,   "ADDRESS_TOGGLE"   },
    { OP_ADDRESS_SET,      "ADDRESS_SET"      },
    { OP_ADDRESS_VALUE,    "ADDRESS_VALUE"    },
};

/* clang-format on */

#define OP_NAME_COUNT ((int)(sizeof(op_names) / sizeof(op_names[0])))

/* Return the mnemonic for op, or "???" for unknown. */
static const char *op_name(unsigned char op)
{
    int i;
    for (i = 0; i < OP_NAME_COUNT; i++)
    {
        if (op_names[i].op == op)
        {
            return op_names[i].name;
        }
    }
    return "???";
}

/* ================================================================== */
/*  Append helpers — write to buf without exceeding capacity.         */
/* ================================================================== */

/* Write at most n chars of s into buf[*pos .. bufsz-1]. */
static void app_str(char *buf, int bufsz, int *pos, const char *s, int n)
{
    int i;
    for (i = 0; i < n && s[i] != '\0'; i++)
    {
        if (buf != NULL && *pos < bufsz - 1)
        {
            buf[*pos] = s[i];
        }
        (*pos)++;
    }
}

/* Write a C string into buf. */
static void app_cstr(char *buf, int bufsz, int *pos, const char *s)
{
    while (*s != '\0')
    {
        if (buf != NULL && *pos < bufsz - 1)
        {
            buf[*pos] = *s;
        }
        (*pos)++;
        s++;
    }
}

/* Format a signed decimal int into a tmp buffer then append it. */
static void app_int(char *buf, int bufsz, int *pos, int v)
{
    char tmp[16];
    int n = snprintf(tmp, sizeof(tmp), "%d", v);
    if (n > 0)
    {
        app_str(buf, bufsz, pos, tmp, n);
    }
}

/* Format a hex int into a tmp buffer then append it. */
static void app_hex4(char *buf, int bufsz, int *pos, int v)
{
    char tmp[8];
    int n = snprintf(tmp, sizeof(tmp), "%04X", (unsigned int)(v & 0xFFFF));
    if (n > 0)
    {
        app_str(buf, bufsz, pos, tmp, n);
    }
}

/* ================================================================== */
/*  irx_bc_disasm                                                     */
/* ================================================================== */

int irx_bc_disasm(const struct irx_bc_execblk *bc, char *buf, int bufsz)
{
    const unsigned char *code;
    const char *const_base;
    const char *sym_base;
    int n_consts, n_syms;
    int code_len;
    int pc;
    int pos = 0;

    if (bc == NULL)
    {
        return -1;
    }

    code = IRXBC_CODE(bc);
    code_len = (int)bc->code_length;
    const_base = IRXBC_CONST_TBL(bc);
    sym_base = IRXBC_SYM_TBL(bc);
    n_consts = (int)bc->const_count;
    n_syms = (int)bc->symbol_count;

    pc = 0;
    while (pc < code_len)
    {
        unsigned char op = code[pc];
        int sz = OP_SIZE(op);
        int target;

        /* Offset */
        app_hex4(buf, bufsz, &pos, pc);
        app_cstr(buf, bufsz, &pos, ": ");
        app_cstr(buf, bufsz, &pos, op_name(op));

        /* Operands */
        if (sz == 4 && op == OP_SIGNAL_ON)
        {
            /* cond:u8 + sym_idx:u16 */
            int cond = (int)code[pc + 1];
            int idx = (int)code[pc + 2] | ((int)code[pc + 3] << 8);
            app_cstr(buf, bufsz, &pos, " cond=");
            app_int(buf, bufsz, &pos, cond);
            app_cstr(buf, bufsz, &pos, " [");
            app_int(buf, bufsz, &pos, idx);
            app_cstr(buf, bufsz, &pos, "]");
            if (idx >= 0 && idx < n_syms)
            {
                const char *entry = sym_base + idx * IRXBC_ENTRY_SIZE;
                int slen = (int)(unsigned char)entry[0];
                app_cstr(buf, bufsz, &pos, " = ");
                app_str(buf, bufsz, &pos, entry + 1, slen);
            }
        }
        else if (sz == 4 && (op == OP_CALL || op == OP_CALL_BIF))
        {
            /* sym_idx:u16 + nargs:u8 */
            int idx = (int)code[pc + 1] | ((int)code[pc + 2] << 8);
            int nargs = (int)code[pc + 3];
            app_cstr(buf, bufsz, &pos, " [");
            app_int(buf, bufsz, &pos, idx);
            app_cstr(buf, bufsz, &pos, "]");
            if (idx >= 0 && idx < n_syms)
            {
                const char *entry = sym_base + idx * IRXBC_ENTRY_SIZE;
                int slen = (int)(unsigned char)entry[0];
                app_cstr(buf, bufsz, &pos, " = ");
                app_str(buf, bufsz, &pos, entry + 1, slen);
            }
            app_cstr(buf, bufsz, &pos, " nargs=");
            app_int(buf, bufsz, &pos, nargs);
        }
        else if (sz == 4 && (op == OP_LOAD_STEM || op == OP_STORE_STEM ||
                             op == OP_DROP_STEM || op == OP_PVAR_STEM))
        {
            /* stem_sym:u16 + tail_count:u8 */
            int idx = (int)code[pc + 1] | ((int)code[pc + 2] << 8);
            int tail_cnt = (int)code[pc + 3];
            app_cstr(buf, bufsz, &pos, " [");
            app_int(buf, bufsz, &pos, idx);
            app_cstr(buf, bufsz, &pos, "]");
            if (idx >= 0 && idx < n_syms)
            {
                const char *entry = sym_base + idx * IRXBC_ENTRY_SIZE;
                int slen = (int)(unsigned char)entry[0];
                app_cstr(buf, bufsz, &pos, " = ");
                app_str(buf, bufsz, &pos, entry + 1, slen);
            }
            app_cstr(buf, bufsz, &pos, " tails=");
            app_int(buf, bufsz, &pos, tail_cnt);
        }
        else if (sz == 4 && op == OP_DECFOR)
        {
            /* n:u8 + off:i16 */
            int n = (int)code[pc + 1];
            unsigned int u = (unsigned int)code[pc + 2] |
                             ((unsigned int)code[pc + 3] << 8);
            int off = (u >= 0x8000u) ? (int)u - 0x10000 : (int)u;
            target = pc + 4 + off;
            app_cstr(buf, bufsz, &pos, " n=");
            app_int(buf, bufsz, &pos, n);
            app_cstr(buf, bufsz, &pos, " ");
            app_int(buf, bufsz, &pos, off);
            app_cstr(buf, bufsz, &pos, " -> ");
            app_hex4(buf, bufsz, &pos, target);
        }
        else if (sz == 3 && (op == OP_PUSH_LIT || op == OP_LOAD ||
                             op == OP_STORE || op == OP_DROP ||
                             op == OP_LABEL || op == OP_PVAR ||
                             op == OP_TR_LIT || op == OP_TR_ABS ||
                             op == OP_TR_VAR ||
                             op == OP_EXPOSE || op == OP_EXPOSE_INDIRECT ||
                             op == OP_SIGNAL || op == OP_ADDRESS_SET))
        {
            /* u16 table index */
            int idx = (int)code[pc + 1] | ((int)code[pc + 2] << 8);
            app_cstr(buf, bufsz, &pos, " [");
            app_int(buf, bufsz, &pos, idx);
            app_cstr(buf, bufsz, &pos, "]");

            /* Print string value for PUSH_LIT / TR_LIT */
            if ((op == OP_PUSH_LIT || op == OP_TR_LIT) &&
                idx >= 0 && idx < n_consts)
            {
                const char *entry = const_base + idx * IRXBC_ENTRY_SIZE;
                int slen = (int)(unsigned char)entry[0];
                app_cstr(buf, bufsz, &pos, " = \"");
                app_str(buf, bufsz, &pos, entry + 1, slen);
                app_cstr(buf, bufsz, &pos, "\"");
            }
            /* Print symbol name for LOAD/STORE/DROP/LABEL/PVAR/EXPOSE/SIGNAL/
             * ADDRESS_SET/TR_VAR — all index the symbol table */
            else if ((op == OP_LOAD || op == OP_STORE || op == OP_DROP ||
                      op == OP_LABEL || op == OP_PVAR || op == OP_TR_VAR ||
                      op == OP_EXPOSE || op == OP_EXPOSE_INDIRECT ||
                      op == OP_SIGNAL || op == OP_ADDRESS_SET) &&
                     idx >= 0 && idx < n_syms)
            {
                const char *entry = sym_base + idx * IRXBC_ENTRY_SIZE;
                int slen = (int)(unsigned char)entry[0];
                app_cstr(buf, bufsz, &pos, " = ");
                app_str(buf, bufsz, &pos, entry + 1, slen);
            }
            /* TR_ABS: print the column number */
            else if (op == OP_TR_ABS)
            {
                app_cstr(buf, bufsz, &pos, " col=");
                app_int(buf, bufsz, &pos, idx);
            }
        }
        else if (sz == 3)
        {
            /* i16 jump/relative offset */
            unsigned int u = (unsigned int)code[pc + 1] |
                             ((unsigned int)code[pc + 2] << 8);
            int off = (u >= 0x8000u) ? (int)u - 0x10000 : (int)u;
            if (op == OP_TR_REL)
            {
                app_cstr(buf, bufsz, &pos, " rel=");
                app_int(buf, bufsz, &pos, off);
            }
            else
            {
                target = pc + 3 + off;
                app_cstr(buf, bufsz, &pos, " ");
                app_int(buf, bufsz, &pos, off);
                app_cstr(buf, bufsz, &pos, " -> ");
                app_hex4(buf, bufsz, &pos, target);
            }
        }
        else if (sz == 2)
        {
            /* u8 operand */
            app_cstr(buf, bufsz, &pos, " ");
            app_int(buf, bufsz, &pos, (int)code[pc + 1]);
        }

        app_cstr(buf, bufsz, &pos, "\n");
        pc += (sz > 0 ? sz : 1); /* guard against sz==0 infinite loop */
    }

    if (buf != NULL && bufsz > 0)
    {
        buf[pos < bufsz ? pos : bufsz - 1] = '\0';
    }
    return pos;
}
