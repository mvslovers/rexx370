/* ------------------------------------------------------------------ */
/*  irx#helo.c - IRX#HELO: REXX/370 "Hello World" demo driver         */
/*                                                                    */
/*  Standalone load module that runs an embedded REXX exec through    */
/*  irx_exec_run().  The exec exercises a cross-section of what's     */
/*  already in place: SAY, arithmetic, string BIFs, DATE/TIME/USERID, */
/*  NUMERIC DIGITS, DO loops, SELECT, PARSE VAR, PARSE ARG, and a     */
/*  recursive internal procedure with PROCEDURE EXPOSE.               */
/*                                                                    */
/*  Invocation:                                                       */
/*     TSO:    CALL 'hlq.LOAD(IRX#HELO)'                              */
/*     Batch:  // EXEC PGM=IRX#HELO                                   */
/*                                                                    */
/*  The program returns the REXX EXIT return code (0 on success).     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>

#include "irxexec.h"

#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

/* The embedded REXX exec. Kept as a single C string so the module is
 * fully self-contained and needs no SYSEXEC / SYSPROC dataset. Each
 * section is annotated so the console output tells the story. */
static const char helo_exec[] =
    "/* REXX/370 demo exec */\n"
    "say 'Hello from REXX/370!'\n"
    "say 'Running as' USERID() '(NUMERIC DIGITS =' DIGITS()')'\n"
    "say\n"
    "\n"
    "say '--- Arithmetic ---'\n"
    "say '  2 ** 28        =' 2 ** 28\n"
    "say '  355 / 113 (pi) =' 355 / 113\n"
    "numeric digits 20\n"
    "say '  pi at digits=20:' 355 / 113\n"
    "numeric digits 9\n"
    "\n"
    "fact = 1\n"
    "do i = 1 to 10\n"
    "   fact = fact * i\n"
    "end\n"
    "say '  10! (iterative) =' fact\n"
    "say\n"
    "\n"
    "greet = 'hello world'\n"
    "say '--- String BIFs on \"'greet'\" ---'\n"
    "say '  LENGTH    =' LENGTH(greet)\n"
    "say '  REVERSE   =' REVERSE(greet)\n"
    "say '  uppercase =' TRANSLATE(greet)\n"
    "say '  SUBSTR 7  =' SUBSTR(greet, 7)\n"
    "say '  WORDS/W2  =' WORDS(greet) '/' WORD(greet, 2)\n"
    "say '  CENTER    =' CENTER(greet, 20, '.')\n"
    "say\n"
    "\n"
    "say '--- DO + SELECT ---'\n"
    "do n = 1 to 5\n"
    "   select\n"
    "      when n // 2 = 0 then kind = 'even'\n"
    "      otherwise            kind = 'odd '\n"
    "   end\n"
    "   say '  n =' RIGHT(n, 2) 'is' kind\n"
    "end\n"
    "say\n"
    "\n"
    "say '--- Conversion BIFs ---'\n"
    "say '  C2X(\"ABC\") =' C2X('ABC')\n"
    "say '  D2X(255)   =' D2X(255)\n"
    "say '  X2D(\"FF\")  =' X2D('FF')\n"
    "say\n"
    "\n"
    "say '--- PARSE VAR ---'\n"
    "full = 'Grace Murray Hopper'\n"
    "parse var full first mid last\n"
    "say '  full   =' full\n"
    "say '  first  =' first\n"
    "say '  middle =' mid\n"
    "say '  last   =' last\n"
    "say\n"
    "\n"
    "say '--- TSK-155 ---'\n"
    "say '  C2X(ERRORTEXT(6))  =' C2X(ERRORTEXT(6))\n"
    "say '  C2X(ERRORTEXT(31)) =' C2X(ERRORTEXT(31))\n"
    "say 'Demo complete. Exiting with RC=0.'\n"
    "say\n"
    "exit 0\n";

int main(void)
{
    int exit_rc = 0;

    int rc = irx_exec_run(helo_exec, (int)(sizeof(helo_exec) - 1),
                          NULL, 0, &exit_rc, NULL);

    if (rc != 0)
    {
        printf("IRX#HELO: irx_exec_run failed (rc=%d)\n", rc);
        return rc;
    }
    return exit_rc;
}
