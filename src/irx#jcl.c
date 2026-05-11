/* ------------------------------------------------------------------ */
/*  irx#jcl.c — IRXJCL Batch Entry Point C dispatcher                */
/*                                                                    */
/*  irx_jcl_dispatch() — asm alias IRXJDISP (CON-4)                   */
/*                                                                    */
/*  Implements the 11-step IRXJCL lifecycle for batch execution of    */
/*  REXX execs named in the JCL PARM= field (WP-CPS-08 / TSK-220).  */
/*                                                                    */
/*  Lifecycle:                                                         */
/*   1  Null-check parm_buffer                                        */
/*   2  Decode PARM: big-endian halfword length, data                 */
/*   3  Sequential-mode guard (first data byte == 0x00)              */
/*   4  Parse member name (first non-blank token, 1–8 chars)         */
/*   5  Parse arg string (remainder after member name + separator)   */
/*   6  Three-path env resolution (R0 → FINDENVB → auto-init)        */
/*   7  Build EXECBLK on stack                                        */
/*   8  IRXLOAD FC=LOAD                                               */
/*   9  Build ARGTABLE + EVALBLOCK, call IRXEXEC COMMAND             */
/*  10  IRXLOAD FC=FREE                                               */
/*  11  Conditional IRXTERM, return exec RC                           */
/*                                                                    */
/*  Scope guards (WP-CPS-08 scope boundary):                         */
/*    - Sequential mode: detected and rejected (TODO WP-CPS-08b)     */
/*    - EVDATA result: not read back (TODO WP-CPS-08b)               */
/*    - RC conversion (20021→3637): not implemented (WP-CPS-08b)     */
/*    - TCB-match / TSOFL inspection for auto-init: not done         */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <string.h>

#include "irx.h"
#include "irx_init.h"
#include "irxexec.h"
#include "irxfunc.h"
#include "irxjcl.h"
#include "irxload.h"

/* MVS PDS member names are 1–8 characters (blank-padded to CL8). */
#define MVS_MEMBER_LEN 8

/* EVALBLOCK on-stack size: evsize=34 doublewords = 272 bytes.
 * Layout: 16-byte header (evpad1 + evsize + evlen + evpad2) +
 *         256 bytes of EVDATA scratch.  We memset the whole buffer
 *         to zero and then set evsize; irx_exec_dispatch validates
 *         that evpad1/evpad2/evlen are all zero on entry. */
#define EVALBLK_DWORDS 34
#define EVALBLK_BYTES  (EVALBLK_DWORDS * 8) /* 272 */

/* Byte value used to fill the ARGTABLE terminator entries.
 * ARGTABLE_END = 8 bytes of 0xFF (see include/irx.h). */
#define ARGTAB_END_BYTE ((unsigned char)'\xFF')

/* Validate ENVBLOCK eye-catcher; returns non-zero if bad. */
static int jcl_envblk_bad(const struct envblock *env)
{
    return (env == NULL ||
            memcmp(env->envblock_id, ENVBLOCK_ID,
                   sizeof(env->envblock_id)) != 0);
}

int irx_jcl_dispatch(void *parm_buffer, struct envblock *envblock_r0)
{
    /* ---- Step 1: null guard ---------------------------------------- */
    if (parm_buffer == NULL)
    {
        return IRXJCL_BADPARM;
    }

    /* ---- Step 2: PARM decode (big-endian halfword length) ----------- */
    const unsigned char *p = (const unsigned char *)parm_buffer;
    int len = ((int)p[0] << 8) | (int)p[1];

    if (len == 0)
    {
        return IRXJCL_BADPARM;
    }

    const char *data = (const char *)(p + 2);

    /* ---- Step 3: sequential-mode guard ------------------------------ */
    /* A PARM whose first data byte is 0x00 indicates sequential mode,
     * where the exec resides in a sequential dataset rather than a PDS.
     * Sequential mode requires different IRXLOAD handling deferred to
     * WP-CPS-08b. */
    if ((unsigned char)data[0] == 0x00)
    {
        /* TODO(WP-CPS-08b): sequential-mode support */
        return IRXJCL_BADPARM;
    }

    /* ---- Step 4: parse member name ---------------------------------- */
    /* First non-blank token in PARM data (up to first space or end). */
    int member_end;
    for (member_end = 0; member_end < len; member_end++)
    {
        if (data[member_end] == ' ')
        {
            break;
        }
    }

    if (member_end == 0 || member_end > MVS_MEMBER_LEN)
    {
        return IRXJCL_BADPARM;
    }

    /* Uppercase into a blank-padded 8-char member name (CL8 form).
     * Use unsigned char to avoid implementation-defined narrowing from
     * the int returned by toupper(); memcpy to exec_member (char[8])
     * is a bitwise copy with no conversion. */
    unsigned char member8[MVS_MEMBER_LEN];
    for (int i = 0; i < MVS_MEMBER_LEN; i++)
    {
        member8[i] = (i < member_end)
                         ? (unsigned char)toupper((unsigned char)data[i])
                         : (unsigned char)' ';
    }

    /* ---- Step 5: parse arg string ----------------------------------- */
    /* Remainder of PARM after member name + one separator space. */
    const char *arg = NULL;
    int arg_len = 0;

    if (member_end < len)
    {
        int arg_start = member_end + 1;
        if (arg_start < len)
        {
            arg = data + arg_start;
            arg_len = len - arg_start;
        }
    }

    /* ---- Step 6: three-path env resolution -------------------------- */
    struct envblock *env = NULL;
    int own_env = 0;

    if (!jcl_envblk_bad(envblock_r0))
    {
        env = envblock_r0;
    }
    else
    {
        int rsn = 0;
        if (irx_init_findenvb(&env, &rsn) != 0)
        {
            /* No existing env on this TCB — auto-init a full Phase 2 env.
             * Must use irxinit() (not irx_init_initenvb) because
             * irx_exec_run() accesses envblock->envblock_userfield as
             * struct irx_wkblk_int *, which irxinit() populates. */
            if (irxinit(NULL, &env) != 0)
            {
                return IRXJCL_NOENV;
            }
            own_env = 1;
        }
    }

    /* ---- Step 7: EXECBLK on stack ----------------------------------- */
    struct execblk eb;
    memset(&eb, 0, sizeof(eb));
    memcpy(eb.exec_blk_acryn, EXECBLK_ID, sizeof(eb.exec_blk_acryn));
    eb.exec_blk_length = EXECBLK_V2_LEN;
    memcpy(eb.exec_member, member8, sizeof(eb.exec_member));

    /* ---- Step 8: IRXLOAD FC=LOAD ------------------------------------ */
    struct instblk *instblk = NULL;
    int load_retv = 0;
    int rc_final;

    if (irx_load_dispatch(IRXLOAD_FC_LOAD, &eb, &instblk, env, &load_retv) !=
        IRXLOAD_OK)
    {
        rc_final = IRXJCL_ERROR;
        goto cleanup_env;
    }

    /* ---- Step 9: IRXEXEC COMMAND ------------------------------------ */
    /* ARGTABLE on stack: entry [0] + terminator [1].
     * irx_exec_dispatch iterates with ae++ (stride = sizeof entry).
     * Initialise the whole array to 0xFF first (terminator pattern),
     * then overwrite [0] only if an arg string is present. */
    struct argtable_entry argtab[2];
    memset(argtab, (int)ARGTAB_END_BYTE, sizeof(argtab));

    if (arg != NULL && arg_len > 0)
    {
        argtab[0].argstring_ptr = (void *)arg;
        argtab[0].argstring_length = arg_len;
    }

    /* EVALBLOCK on stack — header must be zero on entry per spec. */
    unsigned char evalblk_buf[EVALBLK_BYTES];
    struct evalblock *evalblk = (struct evalblock *)(void *)evalblk_buf;
    memset(evalblk_buf, 0, sizeof(evalblk_buf));
    evalblk->evalblock_evsize = EVALBLK_DWORDS;

    /* flags = IRXEXEC_COMMAND (0x00000000) — z/OS codebase value.
     * Spec ticket quoted 0x80000000 but include/irx.h defines
     * IRXEXEC_COMMAND as 0x00000000 (the correct z/OS value). */
    int exec_rc = irx_exec_dispatch(NULL,
                                    argtab,
                                    IRXEXEC_COMMAND,
                                    instblk,
                                    NULL,
                                    evalblk,
                                    NULL,
                                    NULL,
                                    env,
                                    NULL);

    /* ---- Step 10: IRXLOAD FC=FREE ----------------------------------- */
    {
        int free_retv = 0;
        irx_load_dispatch(IRXLOAD_FC_FREE, NULL, &instblk, env, &free_retv);
    }

    rc_final = exec_rc;

    /* ---- Step 11: conditional IRXTERM + return ---------------------- */
cleanup_env:
    if (own_env)
    {
        irxterm(env);
    }
    return rc_final;
}
