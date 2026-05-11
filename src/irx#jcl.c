/* ------------------------------------------------------------------ */
/*  irx#jcl.c — IRXJCL Batch Entry Point C dispatcher                */
/*                                                                    */
/*  irx_jcl_dispatch_main() — implements the IRXJCL lifecycle for    */
/*  batch execution of REXX execs (WP-CPS-08 / TSK-220).             */
/*                                                                    */
/*  Lifecycle:                                                         */
/*   1  Validate member (null / empty / sequential-mode / too-long)   */
/*   2  Uppercase + blank-pad member to CL8 form                      */
/*   3  Env resolution: FINDENVB → auto-init (no R0 hint path)        */
/*   4  Build EXECBLK on stack                                        */
/*   5  IRXLOAD FC=LOAD                                               */
/*   6  Build ARGTABLE + EVALBLOCK, call IRXEXEC COMMAND             */
/*   7  IRXLOAD FC=FREE                                               */
/*   8  Conditional IRXTERM, return exec RC                           */
/*                                                                    */
/*  Architecture decisions (WP-CPS-08):                              */
/*   (a) @@CRT0 bootstrap: entry main() in irx#jclm.c; @@CRT0        */
/*       initialises the crent370 CRT/PPA — no __CRTGET errors.       */
/*   (b) R0-envblock hint path dropped: PGM=IRXJCL has no caller R0  */
/*       hint. Env found via FINDENVB or auto-init'd.                 */
/*   (c) @@START arg reconstruction: handled in irx#jclm.c main();   */
/*       this dispatcher receives decoded (member, arg, len) directly.*/
/*   (d) Sequential mode: member[0]==0x00 rejected (TODO WP-CPS-08b).*/
/*   (e) Programmatic CALL IRXJCL support: deferred to WP-CPS-08c    */
/*       (full HLASM wrapper with CRT bootstrap + R0 path).           */
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

int irx_jcl_dispatch_main(const char *member,
                           const char *arg_string,
                           int         arg_len)
{
    /* ---- Step 1: validate member ------------------------------------ */
    if (member == NULL)
    {
        return IRXJCL_BADPARM;
    }

    /* A first byte of 0x00 is the sequential-mode marker from the
     * binary PARM convention; also catches the empty-member case.
     * Sequential-mode exec loading is deferred to WP-CPS-08b. */
    if (member[0] == '\0') /* TODO(WP-CPS-08b) sequential-mode */
    {
        return IRXJCL_BADPARM;
    }

    /* Leading space means no actual member name precedes the arg. */
    if (member[0] == ' ')
    {
        return IRXJCL_BADPARM;
    }

    if ((int)strlen(member) > MVS_MEMBER_LEN)
    {
        return IRXJCL_BADPARM;
    }

    /* ---- Step 2: uppercase + blank-pad to CL8 ---------------------- */
    int member_len = (int)strlen(member);
    unsigned char member8[MVS_MEMBER_LEN];
    for (int i = 0; i < MVS_MEMBER_LEN; i++)
    {
        member8[i] = (i < member_len)
                         ? (unsigned char)toupper((unsigned char)member[i])
                         : (unsigned char)' ';
    }

    /* ---- Step 3: env resolution (FINDENVB → auto-init) ------------- */
    struct envblock *env = NULL;
    int own_env = 0;

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

    /* ---- Step 4: EXECBLK on stack ---------------------------------- */
    struct execblk eb;
    memset(&eb, 0, sizeof(eb));
    memcpy(eb.exec_blk_acryn, EXECBLK_ID, sizeof(eb.exec_blk_acryn));
    eb.exec_blk_length = EXECBLK_V2_LEN;
    memcpy(eb.exec_member, member8, sizeof(eb.exec_member));

    /* ---- Step 5: IRXLOAD FC=LOAD ----------------------------------- */
    struct instblk *instblk = NULL;
    int load_retv = 0;
    int rc_final;

    if (irx_load_dispatch(IRXLOAD_FC_LOAD, &eb, &instblk, env, &load_retv) !=
        IRXLOAD_OK)
    {
        rc_final = IRXJCL_ERROR;
        goto cleanup_env;
    }

    /* ---- Step 6: IRXEXEC COMMAND ----------------------------------- */
    /* ARGTABLE on stack: entry [0] + terminator [1].
     * Initialise the whole array to 0xFF first (terminator pattern),
     * then overwrite [0] only if an arg string is present. */
    struct argtable_entry argtab[2];
    memset(argtab, (int)ARGTAB_END_BYTE, sizeof(argtab));

    if (arg_string != NULL && arg_len > 0)
    {
        argtab[0].argstring_ptr = (void *)arg_string;
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

    /* ---- Step 7: IRXLOAD FC=FREE ----------------------------------- */
    {
        int free_retv = 0;
        irx_load_dispatch(IRXLOAD_FC_FREE, NULL, &instblk, env, &free_retv);
    }

    rc_final = exec_rc;

    /* ---- Step 8: conditional IRXTERM + return ---------------------- */
cleanup_env:
    if (own_env)
    {
        irxterm(env);
    }
    return rc_final;
}
