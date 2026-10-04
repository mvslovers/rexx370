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

/* The prefix is a constant: a load module that LINKs as RENT may hold no
 * writable data (cc370#100), and a SET that changed it would have
 * changed it for every environment at once.  SC28-1883-0 (p.390) gives
 * this routine no parameters at all: its return code says whether the
 * message ID is shown -- see #326. */
static const char msgid_prefix[3] = {'I', 'R', 'X'};

int irxmsgid(int function, char *prefix, struct envblock *envblock)
{
    (void)envblock;

    if (prefix == NULL || function != MSGID_GET)
    {
        return 20;
    }
    memcpy(prefix, msgid_prefix, sizeof(msgid_prefix));
    return 0;
}
