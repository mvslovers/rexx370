# SC28-1883-0 conformance suite

`test/spec/` holds REXX execs that check rexx370 against **SC28-1883-0**
(TSO/E Version 2 REXX Reference, December 1988), the binding specification of
this project. `TSTSPEC` (`test/tstspec.c`) runs every exec through the IRXJCL
core and gates on its return code; each exec exits with the number of failed
cases.

```sh
make test-host ARGS="--only TSTSPEC"    # host, fast
make test-mvs  ARGS="--only TSTSPEC"    # MVS: execs pre-loaded into SYSEXEC
```

The cases started from the BREXX/370 test collection (`brexx370/test/`, mostly
from CMS-370-BREXX, Unlicense) and the examples printed in the manual. Every
expected value was checked against the 1988 manual, by the author of the
group and then by a second reader who re-derived it from the page image.
BREXX behaviour, later IBM manuals and other interpreters do not count.

## Group tables

One file per group, one table per exec: expression, expected value, printed
page, whether the value is a manual example or derived from a rule, and the
BREXX expectation where it differed. Each file also lists the dropped cases
and the cases whose specified outcome is an error.

| File | Execs |
|---|---|
| [strings.md](strings.md) | ABBREV CENTER COMPARE COPIES DELSTR INDEX INSERT JUSTIFY LASTPOS LEFT LENGTH OVERLAY POS REVERSE RIGHT SPACE STRIP SUBSTR TRANSLAT TRANSLHX VERIFY XRANGE XRANGEHX |
| [words.md](words.md) | DELWORD FIND SUBWORD WORD WORDINDX WORDLEN WORDPOS WORDS |
| [conversion.md](conversion.md) | BITAND BITOR BITXOR C2D C2DODD C2X C2XODD D2C D2X DATATYPE X2C X2D |
| [numeric.md](numeric.md) | ABS ARITH ARITHNEG DIGITS FORM FORMAT FORMATBL FUZZ MAX MIN NUMFMT RANDOM SIGN TRUNC |
| [language.md](language.md) | ADDRESS ARG ARGOPT COMPCHN COMPNOT COMPOPS COMPSLSH CONCAT CONCATX DATE DATEC DATEO ERRORTXT EVALORD PARSE1-4 PREFIX SOURCELN SYMBOL TIME TRACE VALUE |

## Rules for the execs

- **Strict comparison.** The helper `t` compares with `==` against the exact
  result string the manual specifies, including blanks and exponent form.
- **EBCDIC cases use `te`.** A result that depends on the character set
  (C2D of a letter, XRANGE, TRANSLATE tables, strict comparison of letters
  with digits) carries the EBCDIC value and is skipped on the host.
- **Helpers read `arg(n)`, never `parse arg`.** rexx370 currently strips the
  leading blank of the last PARSE variable (#273); `parse arg` would make
  leading-blank cases pass falsely.
- **No line ends in `,,`**: that passes an extra empty argument (#274). The
  one exception is ARG case 32, which tests exactly that.
- **Lines of at most 72 columns, ASCII only**: the execs are FB 80 members on
  MVS.
- **An exec stops at its first runtime error** until SIGNAL ON SYNTAX works
  (WP-CPS-09a-FU). Constructs that end an exec were split into their own exec
  (`C2DODD`, `FORMATBL`, `COMPSLSH`, …) or moved to the last case; still, a
  "died at case n" hides the cases after n. Cases whose specified outcome is
  an error are listed in the tables as ERROR-CASE and not executed yet.
- The execs are generated together with their tables; change both together.

## Known failures

`spec_members[]` in `test/tstspec.c` carries, per exec, the issue whose defect
makes it fail today. Those execs are reported as XFAIL. An exec that starts to
pass is reported as XPASS and fails the run, so the entry is removed together
with the fix. `norun` marks an exec whose defect takes the whole address
space down; none does today (ARGOPT was one until #262 was fixed).

## Where the manual is unclear

SC28-1883-0 is binding. Where it is silent or contradicts itself, the
behaviour of TSO/E REXX on z/OS decides (maintainer run of the disputed cases,
2026-09-30). Rows decided that way say so in their spec column:

- **NUMERIC and FORMAT.** p.47 says NUMERIC DIGITS/FORM apply to "arithmetic
  built-in functions"; p.77 says built-in functions are "unaffected by
  changes to the NUMERIC settings, except where stated". z/OS applies DIGITS
  and FORM to FORMAT (`numeric digits 3; format('12345.73',,,2,2)` gives
  `1.23E+04`): FORMAT 100-103, 107-109, FORMATBL 4.
  The later z/OS manual states it (SA32-0972-00 p.105: the exponent part
  "is formatted according to the current NUMERIC settings of DIGITS and
  FORM"); the 1988 FORMAT page has no such sentence.
- **Blanks in DATATYPE(,'X').** p.85 says "only between pairs", p.10 allows
  an odd leading group in hex strings. z/OS: `datatype('A BC DF','X')` is 1
  (DATATYPE 89, 90).
- **`d2x(0)` / `d2c(0)`.** p.88 does not say. z/OS: `'0'` and `'00'x`
  (D2X 23, D2C 22-23).

Still dropped, with the reason in the group tables: cases the manual does not
settle and that were not run on z/OS (for example WORDPOS with an empty
phrase, TRACE after `trace('F')`).

rexx370 also implements binary strings, B2X and X2B, which are not in the
1988 manual. They are listed in [docs/extensions.md](../../docs/extensions.md) and
tested by the execs in `test/ext/`, which TSTSPEC runs together with this
suite.
