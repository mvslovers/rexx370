/* ------------------------------------------------------------------ */
/*  irxmsgid.c - Message ID Replaceable Routine                       */
/*                                                                    */
/*  Manages the message prefix for REXX error messages.               */
/*  Default prefix: 'IRX' (producing messages like IRX0001I)          */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 16 (Message Identifier Routine)        */
/*  Ref: Architecture Design v0.1.0, Section 5.7                     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>

#include "irx.h"
#include "irxfunc.h"

#define MSGID_GET 0
#define MSGID_SET 1

static char default_prefix[4] = "IRX";

int irxmsgid(int function, char *prefix, struct envblock *envblock)
{
    (void)envblock; /* unused for now */

    if (prefix == NULL)
    {
        return 20;
    }

    switch (function)
    {
        case MSGID_GET:
            memcpy(prefix, default_prefix, 3);
            return 0;

        case MSGID_SET:
            memcpy(default_prefix, prefix, 3);
            return 0;

        default:
            return 20;
    }
}
