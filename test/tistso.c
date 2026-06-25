/* ------------------------------------------------------------------ */
/*  tistso.c — self-validating test for ISTSO (TSO detection)         */
/*                                                                    */
/*  is_tso()'s correct result depends on the run environment, so the  */
/*  test cannot know it a priori. The runner passes the EXPECTED value */
/*  as the program argument per leg (batch -> "0", TSO/IKJEFT01 ->     */
/*  "1"; see [[test]] parm_batch / parm_tso in project.toml), and this */
/*  test asserts is_tso() matches it -- a real pass/fail test on both  */
/*  the batch and the TSO leg.                                         */
/*                                                                    */
/*  crent370's @@CRT0 reconstructs argv from both the batch PARM= and  */
/*  the TSO `CALL 'ds(mem)' 'arg'` form (same as IRXDBG).              */
/*                                                                    */
/*  Ref: WP-I1c.6 / GitHub mvslovers/rexx370#93                       */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>

#include <mbtcheck.h>

#include "irxenv.h"   /* is_tso() -> asm("ISTSO") */

int main(int argc, char **argv)
{
    int expected = (argc >= 2) ? atoi(argv[1]) : 0;
    int actual = is_tso();

    printf("=== TISTSO: is_tso()=%d expected=%d ===\n", actual, expected);

    CHECK(actual == expected,
          expected ? "is_tso() == 1 (TSO environment)"
                   : "is_tso() == 0 (batch environment)");

    return mbt_test_summary("TISTSO");
}
