# Extensions beyond SC28-1883-0

rexx370 follows SC28-1883-0 (TSO/E Version 2 REXX Reference, December 1988).
The features below are **not** in that manual. They came to IBM REXX later
and are kept because no program written against the 1988 manual can depend on
their absence. In the source each one carries the comment
`extension: not in SC28-1883-0`.

Neither SC28-1883-0 nor SC28-1883-4 (August 1991) describes them. Both
function indexes go from BITXOR straight to CENTER. Their behaviour
therefore follows TSO/E REXX on z/OS, the rule the project applies wherever
the 1988 manual is silent.

| Feature | Where | Tests |
|---|---|---|
| Binary strings `'0101'b` | `src/irx#tokn.c` (scanner) | `test/ext/BINSTR` |
| `B2X(binary_string)` | `src/irx#bifs.c` | `test/ext/B2X` |
| `X2B(hexstring)` | `src/irx#bifs.c` | `test/ext/X2B` |

Every other built-in function and every DATE and TIME option that rexx370
accepts is in the 1988 manual. It was checked against the function index and
the option lists on 2026-09-30.

The extension execs run under `TSTSPEC` together with the conformance suite
(`test/spec/`, see `docs/spec-tests/README.md`), with the same helper rules.
Their expected values follow z/OS. The z/OS confirmation of the individual
values is recorded in each exec's header comment.
