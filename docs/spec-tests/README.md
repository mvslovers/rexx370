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
- **Helpers only `RETURN`.** `EXIT` inside a called routine crashes rexx370
  today (#262).
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
with the fix. `ARGOPT` is not run at all (`norun`) because #262 takes the
whole address space down.

## Open interpretations

The manual leaves these open. The suite takes the reading below and marks the
affected rows as inferred:

- **FORMAT counts as an "arithmetic built-in function" (p.47).** So NUMERIC
  DIGITS rounds its number and NUMERIC FORM ENGINEERING applies (FORMAT
  100-103, 107-109, FORMATBL 4).
- **Blanks in DATATYPE(,'X').** p.85 allows them "only between pairs", which
  contradicts the odd leading group that hex strings allow (p.10). The
  disputed cases are dropped.
- **`d2x(0)` / `d2c(0)`.** p.88 does not say whether the result is a null
  string or `'0'` (`'00'x`). Dropped.
- **TRACE after `trace('F')`.** Whether the function reports `F` or `N` is not
  stated. Dropped.

rexx370 also implements binary strings, B2X and X2B, which are not in the
1988 manual; the suite has no cases for them.
