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
Their expected values follow z/OS and were confirmed there by a maintainer
run on 2026-09-30; every value matched.

## Limits that differ from z/OS

Not extensions, but places where rexx370 stops earlier than TSO/E REXX on
z/OS, because its C stack is smaller than z/OS's control stack would need.

**Nested blocks (#296).** z/OS has a control stack of 250 entries shared by
active DO, IF, SELECT and internal calls (a DO after THEN takes two); one more
is error 11, *Control stack full*. rexx370 counts the same way, but the
bytecode compiler recurses on the C stack for every block, about 430 bytes per
DO and 650 per IF-DO level, and a guard keeps `IRX_STACK_MARGIN` (16 KB) free
below the end of that stack. IRXEXEC runs on a 64 KB pool, so under IRXJCL and
TSO error 11 comes at:

| Nesting | z/OS | rexx370 (IRXEXEC, 64 KB) |
|---|---|---|
| `do 1` blocks | 250 | **114** run, 115 is error 11 |
| `if 1 then do` blocks | 125 | **74** run, 75 is error 11 |
| active internal calls | 250 | 250 (no C stack) |

Measured on MVSCE-LAB with `tso/lab/nest_depth.py` (JOB01626). The error
comes when the clause runs, with its line, and SIGNAL ON SYNTAX traps it, as on
z/OS. A C program that calls the interpreter on a larger stack gets further,
up to the 250 entries.
