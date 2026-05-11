/* ------------------------------------------------------------------ */
/*  irx#jclm.c — IRXJCL main() entry point                           */
/*                                                                    */
/*  Called by @@CRT0.  @@START parses the MVS EXEC PARM= field into  */
/*  argv by splitting on whitespace (NUL-terminating each token in   */
/*  place inside the PARM buffer):                                    */
/*    argv[0]       program name ("IRXJCL")                           */
/*    argv[1]       member name (first whitespace-delimited word)     */
/*    argv[2..n-1]  words of the argument string                      */
/*                                                                    */
/*  z/OS IRXJCL expects the argument as a single string, not as       */
/*  individual tokens. main() re-joins argv[2..] by restoring the    */
/*  NULs that @@START inserted back to spaces, then passes the        */
/*  contiguous arg-string to irx_jcl_dispatch_main().                */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irxjcl.h"

/* Minimum argc when only a member name is supplied (no arg words).
 * argv[0]=program-name, argv[1]=member-name. */
#define ARGC_MEMBER_ONLY 2

int main(int argc, char *argv[])
{
    const char *member = NULL;
    const char *arg_string = NULL;
    int arg_len = 0;

    if (argc < ARGC_MEMBER_ONLY || argv[1] == NULL || argv[1][0] == '\0')
    {
        return IRXJCL_BADPARM;
    }
    member = argv[1];

    if (argc > ARGC_MEMBER_ONLY && argv[2] != NULL)
    {
        /* @@START NUL-terminated each PARM token in place.  Restore
         * the NULs to spaces so argv[2..argc-1] forms one contiguous
         * arg-string, as z/OS IRXJCL passes to IRXEXEC. */
        char *start = argv[2];
        char *end = argv[argc - 1] + strlen(argv[argc - 1]);
        char *p;
        for (p = start; p < end; p++)
        {
            if (*p == '\0')
            {
                *p = ' ';
            }
        }
        arg_string = start;
        arg_len = (int)(end - start);
    }

    return irx_jcl_dispatch_main(member, arg_string, arg_len);
}
