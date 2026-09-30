# SC28-1883-0 conformance: numeric group

Members in `test/spec/`: ABS, SIGN, MAX, MIN, TRUNC, FORMAT, FORMATBL, FORM,
FUZZ, DIGITS, RANDOM, NUMFMT, ARITH, ARITHNEG.

Authority: SC28-1883-0 (TSO/E Version 2 REXX Reference, December 1988) only.
Expected values were read on the PDF page images (printed pages 47, 78, 87,
90-91, 95, 97-99, 104, 139-148). Derived values for arithmetic were checked
with Python `decimal` (prec = NUMERIC DIGITS, ROUND_HALF_UP), which
reproduces the manual's own digits-5 tables on p.143/144 exactly; power
results were checked with a simulation of the p.143 algorithm
(left-to-right binary, DIGITS+L+1 intermediate precision, final division
by 1 at DIGITS).

General notes

- **BREXX defaults differ.** The BREXX tests assume NUMERIC DIGITS 30
  (trunc.rexx comment) or 15-digit reals (numfmt.rexx). The 1988 default is
  9 (p.140, p.47). Every BREXX case whose value depends on that was
  recomputed at DIGITS 9; the column "BREXX said" shows the old value.
- **Trailing zeros.** p.141: significant trailing zeros are retained for
  addition, subtraction and multiplication; division and power remove them
  (p.142, p.143). This decides NUMFMT 10, 12, 25, 28.
- **Operand truncation.** p.141: before every operation the operands are
  truncated to DIGITS+1 significant digits, then the result is rounded to
  DIGITS. TRUNC 16-27 and ARITH 43 rest on this.
- **Harness workarounds (not cases).** Values computed under a reduced
  NUMERIC setting are stored in a variable, the setting is restored, and
  only then is the helper called (NUMERIC settings are inherited by the
  helper, `fails + 1` would otherwise be rounded). No line ends in `,,`
  (rexx370 passes an extra empty argument there); a long operand is put
  into a variable first (FORMAT 96).
- **Splits.** FORMAT with blanks around the sign (`' - 12.73'`) ends the
  exec in rexx370 today, so those three cases live in FORMATBL. A negative
  power exponent ends the exec (`2**-3`, also `2**n` with n = -3), so those
  cases live in ARITHNEG. In FORM, FUZZ and DIGITS the one construct that
  ends the exec today (NUMERIC FORM VALUE / NUMERIC FORM (expr), NUMERIC
  FUZZ and NUMERIC DIGITS without expression) was moved to the last case.

## ABS (p.78)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `abs('12.3')` | `'12.3'` | p.78 example | spec |  |  |
| 2 | `abs(' -0.307')` | `'0.307'` | p.78 example | spec |  |  |
| 3 | `abs(-12.345)` | `'12.345'` | p.78 derived: "returns the absolute value ... has no sign" | brexx:abs.rexx:3 |  |  |
| 4 | `abs(12.345)` | `'12.345'` | p.78 derived: absolute value | brexx:abs.rexx:4 |  |  |
| 5 | `abs(-0.0)` | `'0'` | p.141/142 derived: the argument is the prefix expression 0-0.0, whose zero result is already 0 | brexx:abs.rexx:5 |  | tests prefix minus rather than ABS itself |
| 6 | `abs(0.0)` | `'0'` | p.78/141 derived: formatted per NUMERIC settings; a zero result is always 0 | brexx:abs.rexx:6 |  | inferred that ABS formatting follows the arithmetic zero rule |
| 7 | `numeric digits 3; abs(-12.345)` | `'12.3'` | p.78 derived: "formatted according to the current NUMERIC settings" | new |  |  |

BREXX abs.rexx 1-2 are the manual examples (cases 1-2).

## SIGN (p.98-99)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `sign('12.3')` | `'1'` | p.99 example | spec |  |  |
| 2 | `sign(' -0.307')` | `'-1'` | p.99 example | spec |  |  |
| 3 | `sign(0.0)` | `'0'` | p.99 example | spec |  |  |
| 4 | `sign('0')` | `'0'` | p.98 derived: number+0 first; <0 -> -1, 0 -> 0, >0 -> 1 | brexx:sign.rexx:4 |  |  |
| 5 | `sign('-0')` | `'0'` | p.98 derived: number+0 first; <0 -> -1, 0 -> 0, >0 -> 1 | brexx:sign.rexx:5 |  |  |
| 6 | `sign('0.4')` | `'1'` | p.98 derived: number+0 first; <0 -> -1, 0 -> 0, >0 -> 1 | brexx:sign.rexx:6 |  |  |
| 7 | `sign('-10')` | `'-1'` | p.98 derived: number+0 first; <0 -> -1, 0 -> 0, >0 -> 1 | brexx:sign.rexx:7 |  |  |
| 8 | `sign('15')` | `'1'` | p.98 derived: number+0 first; <0 -> -1, 0 -> 0, >0 -> 1 | brexx:sign.rexx:8 |  |  |

BREXX sign.rexx 1-3 are the manual examples (cases 1-3).

## MAX (p.95)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `max(12,6,7,9)` | `'12'` | p.95 example | spec |  |  |
| 2 | `max(17.3,19,17.03)` | `'19'` | p.95 example | spec |  |  |
| 3 | `max(-7,-3,-4.3)` | `'-3'` | p.95 example | spec |  |  |
| 4 | `max(1,2,3,4,5,6,7,8,9,max(10,11,12,13))` | `'13'` | p.95 example | spec |  |  |
| 5 | `max( 10.1 )` | `'10.1'` | p.95 derived: largest number, formatted per NUMERIC DIGITS | brexx:max.rexx:4 |  |  |
| 6 | `max( -10.1, 3.8 )` | `'3.8'` | p.95 derived: largest number, formatted per NUMERIC DIGITS | brexx:max.rexx:5 |  |  |
| 7 | `max( 10.1, 10.2, 10.3 )` | `'10.3'` | p.95 derived: largest number, formatted per NUMERIC DIGITS | brexx:max.rexx:6 |  |  |
| 8 | `max( 10.3, 10.2, 10.3 )` | `'10.3'` | p.95 derived: largest number, formatted per NUMERIC DIGITS | brexx:max.rexx:7 |  |  |
| 9 | `max( 10.1, 10.4, 10.3 )` | `'10.4'` | p.95 derived: largest number, formatted per NUMERIC DIGITS | brexx:max.rexx:9 |  |  |
| 10 | `max( 10.3, 10.2, 10.1 )` | `'10.3'` | p.95 derived: largest number, formatted per NUMERIC DIGITS | brexx:max.rexx:10 |  |  |
| 11 | `max( 1, 2, 4, 5 )` | `'5'` | p.95 derived: largest number, formatted per NUMERIC DIGITS | brexx:max.rexx:11 |  |  |
| 12 | `max( -0, 0 )` | `'0'` | p.95 derived: largest number, formatted per NUMERIC DIGITS | brexx:max.rexx:12 |  | -0 is the prefix expression 0-0, already 0 before MAX is called (p.141/142); tests little of MAX itself |
| 13 | `max( 1,2,3,4,5,6,7,8,7,6,5,4,3,2 )` | `'8'` | p.95 derived: largest number, formatted per NUMERIC DIGITS | brexx:max.rexx:13 |  |  |
| 14 | `numeric digits 3; max(1.234, 1)` | `'1.23'` | p.95 derived: "formatted according to the current setting of NUMERIC DIGITS" | new |  |  |

Dropped: max.rexx:1-3 duplicate the manual examples; max.rexx:8 duplicates max.rexx:6. Not tested: more than 20 arguments (p.95 says "up to 20", but does not say that 21 is an error).

## MIN (p.95)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `min(12,6,7,9)` | `'6'` | p.95 example | spec |  |  |
| 2 | `min(17.3,19,17.03)` | `'17.03'` | p.95 example | spec |  |  |
| 3 | `min(-7,-3,-4.3)` | `'-7'` | p.95 example | spec |  |  |
| 4 | `min( 10.1 )` | `'10.1'` | p.95 derived: smallest number, formatted per NUMERIC DIGITS | brexx:min.rexx:4 |  |  |
| 5 | `min( -10.1, 3.8 )` | `'-10.1'` | p.95 derived: smallest number, formatted per NUMERIC DIGITS | brexx:min.rexx:5 |  |  |
| 6 | `min( 10.1, 10.2, 10.3 )` | `'10.1'` | p.95 derived: smallest number, formatted per NUMERIC DIGITS | brexx:min.rexx:6 |  |  |
| 7 | `min( 10.1, 10.2, 10.1 )` | `'10.1'` | p.95 derived: smallest number, formatted per NUMERIC DIGITS | brexx:min.rexx:7 |  |  |
| 8 | `min( 10.4, 10.1, 10.3 )` | `'10.1'` | p.95 derived: smallest number, formatted per NUMERIC DIGITS | brexx:min.rexx:9 |  |  |
| 9 | `min( 10.3, 10.2, 10.1 )` | `'10.1'` | p.95 derived: smallest number, formatted per NUMERIC DIGITS | brexx:min.rexx:10 |  |  |
| 10 | `min( 5, 2, 4, 1 )` | `'1'` | p.95 derived: smallest number, formatted per NUMERIC DIGITS | brexx:min.rexx:11 |  |  |
| 11 | `min( -0, 0 )` | `'0'` | p.95 derived: smallest number, formatted per NUMERIC DIGITS | brexx:min.rexx:12 |  | as MAX 12: -0 is already 0 before MIN is called |
| 12 | `min( 8,2,3,4,5,6,7,1,7,6,5,4,3,2 )` | `'1'` | p.95 derived: smallest number, formatted per NUMERIC DIGITS | brexx:min.rexx:13 |  |  |
| 13 | `numeric digits 3; min(9.876, 10)` | `'9.88'` | p.95 derived: "formatted according to the current setting of NUMERIC DIGITS" | new |  |  |

Dropped: min.rexx:1-3 duplicate the manual examples; min.rexx:8 duplicates min.rexx:6. Not tested: more than 20 arguments (p.95 says "up to 20", but does not say that 21 is an error).

## TRUNC (p.104)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `trunc(12.3)` | `'12'` | p.104 example | spec |  |  |
| 2 | `trunc(127.09782,3)` | `'127.097'` | p.104 example | spec |  |  |
| 3 | `trunc(127.1,3)` | `'127.100'` | p.104 example | spec |  |  |
| 4 | `trunc(127,2)` | `'127.00'` | p.104 example | spec |  |  |
| 5 | `trunc(1234.5678, 2)` | `'1234.56'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:5 |  |  |
| 6 | `trunc(-1234.5678)` | `'-1234'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:6 |  |  |
| 7 | `trunc(.5678)` | `'0'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:7 |  |  |
| 8 | `trunc(.00123)` | `'0'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:8 |  |  |
| 9 | `trunc(.00123,4)` | `'0.0012'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:9 |  |  |
| 10 | `trunc(.00127,4)` | `'0.0012'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:10 |  |  |
| 11 | `trunc(.1678)` | `'0'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:11 |  |  |
| 12 | `trunc(1234.5678)` | `'1234'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:12 |  |  |
| 13 | `trunc(4.5678, 7)` | `'4.5678000'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:13 |  |  |
| 14 | `trunc(10000005.0,2)` | `'10000005.00'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:14 |  |  |
| 15 | `trunc(10000000.5,2)` | `'10000000.50'` | p.104 derived: truncate, pad with zeros | brexx:trunc.rexx:15 |  |  |
| 16 | `trunc(10000000.05,2)` | `'10000000.10'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:16; brexx:trunc.rexx:93 | 10000000.05 |  |
| 17 | `trunc(10000000.005,2)` | `'10000000.00'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:17 |  |  |
| 18 | `trunc(10000005.5,2)` | `'10000005.50'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:18 |  |  |
| 19 | `trunc(10000000.55,2)` | `'10000000.60'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:19; brexx:trunc.rexx:94 | 10000000.55 |  |
| 20 | `trunc(10000000.055,2)` | `'10000000.10'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:20; brexx:trunc.rexx:95 | 10000000.05 |  |
| 21 | `trunc(10000000.0055,2)` | `'10000000.00'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:21 |  |  |
| 22 | `trunc(10000000.04,2)` | `'10000000.00'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:22 | 10000000.04 |  |
| 23 | `trunc(10000000.045,2)` | `'10000000.00'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:23; brexx:trunc.rexx:96 | 10000000.04 |  |
| 24 | `trunc(10000000.45,2)` | `'10000000.50'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:24; brexx:trunc.rexx:97 | 10000000.45 |  |
| 25 | `trunc(99999999.,2)` | `'99999999.00'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:28 |  |  |
| 26 | `trunc(99999999.9,2)` | `'99999999.90'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:29 |  |  |
| 27 | `trunc(99999999.99,2)` | `'100000000.00'` | p.104 derived: number+0 first (rounded to DIGITS 9, operands truncated to DIGITS+1), then truncated to n places | brexx:trunc.rexx:30; brexx:trunc.rexx:98 | 99999999.99 | 99999999.99 rounds to 100000000 (9 digits), no exponent needed |
| 28 | `trunc(1E2,0)` | `'100'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:31 |  |  |
| 29 | `trunc(12E1,0)` | `'120'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:32 |  |  |
| 30 | `trunc(123.,0)` | `'123'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:33 |  |  |
| 31 | `trunc(123.1,0)` | `'123'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:34 |  |  |
| 32 | `trunc(123.12,0)` | `'123'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:35 |  |  |
| 33 | `trunc(123.123,0)` | `'123'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:36 |  |  |
| 34 | `trunc(123.1234,0)` | `'123'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:37 |  |  |
| 35 | `trunc(123.12345,0)` | `'123'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:38 |  |  |
| 36 | `trunc(1E2,1)` | `'100.0'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:39 |  |  |
| 37 | `trunc(12E1,1)` | `'120.0'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:40 |  |  |
| 38 | `trunc(123.,1)` | `'123.0'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:41 |  |  |
| 39 | `trunc(123.1,1)` | `'123.1'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:42 |  |  |
| 40 | `trunc(123.12,1)` | `'123.1'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:43 |  |  |
| 41 | `trunc(123.123,1)` | `'123.1'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:44 |  |  |
| 42 | `trunc(123.1234,1)` | `'123.1'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:45 |  |  |
| 43 | `trunc(123.12345,1)` | `'123.1'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:46 |  |  |
| 44 | `trunc(1E2,2)` | `'100.00'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:47 |  |  |
| 45 | `trunc(12E1,2)` | `'120.00'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:48 |  |  |
| 46 | `trunc(123.,2)` | `'123.00'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:49 |  |  |
| 47 | `trunc(123.1,2)` | `'123.10'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:50 |  |  |
| 48 | `trunc(123.12,2)` | `'123.12'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:51 |  |  |
| 49 | `trunc(123.123,2)` | `'123.12'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:52 |  |  |
| 50 | `trunc(123.1234,2)` | `'123.12'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:53 |  |  |
| 51 | `trunc(123.12345,2)` | `'123.12'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:54 |  |  |
| 52 | `trunc(1E2,3)` | `'100.000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:55 |  |  |
| 53 | `trunc(12E1,3)` | `'120.000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:56 |  |  |
| 54 | `trunc(123.,3)` | `'123.000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:57 |  |  |
| 55 | `trunc(123.1,3)` | `'123.100'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:58 |  |  |
| 56 | `trunc(123.12,3)` | `'123.120'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:59 |  |  |
| 57 | `trunc(123.123,3)` | `'123.123'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:60 |  |  |
| 58 | `trunc(123.1234,3)` | `'123.123'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:61 |  |  |
| 59 | `trunc(123.12345,3)` | `'123.123'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:62 |  |  |
| 60 | `trunc(1E2,4)` | `'100.0000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:63 |  |  |
| 61 | `trunc(12E1,4)` | `'120.0000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:64 |  |  |
| 62 | `trunc(123.,4)` | `'123.0000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:65 |  |  |
| 63 | `trunc(123.1,4)` | `'123.1000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:66 |  |  |
| 64 | `trunc(123.12,4)` | `'123.1200'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:67 |  |  |
| 65 | `trunc(123.123,4)` | `'123.1230'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:68 |  |  |
| 66 | `trunc(123.1234,4)` | `'123.1234'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:69 |  |  |
| 67 | `trunc(123.12345,4)` | `'123.1234'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:70 |  |  |
| 68 | `trunc(1E2,5)` | `'100.00000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:71 |  |  |
| 69 | `trunc(12E1,5)` | `'120.00000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:72 |  |  |
| 70 | `trunc(123.,5)` | `'123.00000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:73 |  |  |
| 71 | `trunc(123.1,5)` | `'123.10000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:74 |  |  |
| 72 | `trunc(123.12,5)` | `'123.12000'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:75 |  |  |
| 73 | `trunc(123.123,5)` | `'123.12300'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:76 |  |  |
| 74 | `trunc(123.1234,5)` | `'123.12340'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:77 |  |  |
| 75 | `trunc(123.12345,5)` | `'123.12345'` | p.104 derived: truncate / add trailing zeros | brexx:trunc.rexx:78 |  |  |
| 76 | `trunc(127.96)` | `'127'` | p.104 derived: truncation never rounds | brexx:trunc.rexx:79 |  |  |
| 77 | `trunc(1.9999,2)` | `'1.99'` | p.104 derived: truncation never rounds | brexx:trunc.rexx:80 |  |  |
| 78 | `trunc(-1.9999,2)` | `'-1.99'` | p.104 derived: truncation never rounds | brexx:trunc.rexx:81 |  |  |
| 79 | `trunc(0.999999,0)` | `'0'` | p.104 derived: truncation never rounds | brexx:trunc.rexx:82 |  |  |
| 80 | `trunc(9.95,1)` | `'9.9'` | p.104 derived: truncation never rounds | brexx:trunc.rexx:83 |  |  |
| 81 | `trunc(1e50,2)` | `'1' \|\| copies('0',50) \|\| '.00'` | p.104 derived: 1E+50 after number+0; "The result will never be in exponential form" | brexx:trunc.rexx:84 |  |  |
| 82 | `trunc(1e-30,2)` | `'0.00'` | p.104 derived: truncated to 2 places | brexx:trunc.rexx:85 |  |  |
| 83 | `trunc(0.1+0.2,3)` | `'0.300'` | p.104/141 derived: computed argument at DIGITS 9 (1/3 = 0.333333333, 2/3 = 0.666666667) | brexx:trunc.rexx:88 |  |  |
| 84 | `trunc(1/3,5)` | `'0.33333'` | p.104/141 derived: computed argument at DIGITS 9 (1/3 = 0.333333333, 2/3 = 0.666666667) | brexx:trunc.rexx:89 |  |  |
| 85 | `trunc(2/3,5)` | `'0.66666'` | p.104/141 derived: computed argument at DIGITS 9 (1/3 = 0.333333333, 2/3 = 0.666666667) | brexx:trunc.rexx:90 |  |  |
| 86 | `trunc(5*3,2)` | `'15.00'` | p.104/141 derived: computed argument at DIGITS 9 (1/3 = 0.333333333, 2/3 = 0.666666667) | brexx:trunc.rexx:91 |  |  |
| 87 | `y = '1.10'; z = trunc(y,1); y` | `'1.10'` | p.104 derived: a function does not change its argument | brexx:trunc.rexx:92 |  |  |
| 88 | `z (from case 87)` | `'1.1'` | p.104 derived: truncated to 1 place | new |  |  |

The BREXX file states "default NUMERIC DIGITS (30): no rounding before
truncation" for its cases 16-24. The 1988 default is 9 and p.104 says the
number is first rounded "just as though the operation number+0 had been
carried out" (Note: "rounded according to the current setting of NUMERIC
DIGITS"). Cases 16-27 therefore carry the DIGITS 9 values; BREXX's own
cases 93-98 (run under an explicit `numeric digits 9`) agree with them and
are folded into the same rows.

Dropped:

| BREXX case | expression | reason |
|---|---|---|
| trunc.rexx:1-4 | manual examples | duplicates of cases 1-4 |
| trunc.rexx:86 | `trunc(-0.001,2)` -> `0.00` | sign of a zero result of TRUNC is not settled by p.104 ("-0.00" vs "0.00") |
| trunc.rexx:87 | `trunc(-0.5)` -> `0` | same: "integer part" of -0.5, negative-zero form not described |
| trunc.rexx:93-98 | DIGITS 9 variants | same expressions and values as cases 16, 19, 20, 23, 24, 27 |
| trunc.rexx:99 | `trunc(123.12345,5)` at DIGITS 9 | duplicate of case 75 (DIGITS 9 is the default) |

ERROR-CASE: `trunc(1,-1)` and `trunc(1,1.5)` (p.104: "n must be a
nonnegative whole number"); `trunc('abc')` (not a number).
## FORMAT (p.90-91)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `format('3',4)` | <code>'&nbsp;&nbsp;&nbsp;3'</code> | p.91 example | spec; brexx:format.rexx:1 |  |  |
| 2 | `format('1.73',4,0)` | <code>'&nbsp;&nbsp;&nbsp;2'</code> | p.91 example | spec; brexx:format.rexx:2 |  |  |
| 3 | `format('1.73',4,3)` | <code>'&nbsp;&nbsp;&nbsp;1.730'</code> | p.91 example | spec; brexx:format.rexx:3 |  |  |
| 4 | `format('-.76',4,1)` | <code>'&nbsp;&nbsp;-0.8'</code> | p.91 example | spec; brexx:format.rexx:4 |  |  |
| 5 | `format('3.03',4)` | <code>'&nbsp;&nbsp;&nbsp;3.03'</code> | p.91 example | spec; brexx:format.rexx:5 |  |  |
| 6 | `format('0.000')` | `'0'` | p.91 example | spec; brexx:format.rexx:8 |  |  |
| 7 | `format('12345.73',,,2,2)` | `'1.234573E+04'` | p.91 example | spec; brexx:format.rexx:9 |  |  |
| 8 | `format('12345.73',,3,,0)` | `'1.235E+4'` | p.91 example | spec; brexx:format.rexx:10 |  |  |
| 9 | `format('1.234573',,3,,0)` | `'1.235'` | p.91 example | spec; brexx:format.rexx:11 |  |  |
| 10 | `format('12345.73',,,3,6)` | `'12345.73'` | p.91 example | spec; brexx:format.rexx:14 |  |  |
| 11 | `format('1234567e5',,3,0)` | `'123456700000.000'` | p.91 example | spec; brexx:format.rexx:15 |  |  |
| 12 | `format('123.45',,3,2,0)` | `'1.235E+02'` | p.91 derived: expp given, expt 0: exponential form; zero-padded exponent as in example 9 | brexx:format.rexx:12 |  |  |
| 13 | `format('1.2345',,3,2,0)` | <code>'1.235&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: exponent 0 with non-zero expp: "expp+2 blanks are supplied" | brexx:format.rexx:13 |  |  |
| 14 | `format(12.34)` | `'12.34'` | p.90 derived: only number: as number+0 | brexx:format.rexx:16 |  |  |
| 15 | `format(12.34,4)` | <code>'&nbsp;&nbsp;12.34'</code> | p.91 derived: before pads on the left; "If after is not the same size as the decimal part ... rounded (or extended with zeros)" | brexx:format.rexx:17 |  |  |
| 16 | `format(12.34,4,4)` | <code>'&nbsp;&nbsp;12.3400'</code> | p.91 derived: before pads on the left; "If after is not the same size as the decimal part ... rounded (or extended with zeros)" | brexx:format.rexx:18 |  |  |
| 17 | `format(12.34,4,1)` | <code>'&nbsp;&nbsp;12.3'</code> | p.91 derived: before pads on the left; "If after is not the same size as the decimal part ... rounded (or extended with zeros)" | brexx:format.rexx:19 |  |  |
| 18 | `format(12.35,4,1)` | <code>'&nbsp;&nbsp;12.4'</code> | p.91 derived: before pads on the left; "If after is not the same size as the decimal part ... rounded (or extended with zeros)" | brexx:format.rexx:20 |  |  |
| 19 | `format(12.34,,4)` | `'12.3400'` | p.91 derived: before pads on the left; "If after is not the same size as the decimal part ... rounded (or extended with zeros)" | brexx:format.rexx:21 |  |  |
| 20 | `format(12.34,4,0)` | <code>'&nbsp;&nbsp;12'</code> | p.91 derived: before pads on the left; "If after is not the same size as the decimal part ... rounded (or extended with zeros)" | brexx:format.rexx:22 |  |  |
| 21 | `format(99.995,3,2)` | `'100.00'` | p.91 derived: before pads on the left; "If after is not the same size as the decimal part ... rounded (or extended with zeros)" | brexx:format.rexx:23 |  |  |
| 22 | `format(0.111,,4)` | `'0.1110'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:24 |  |  |
| 23 | `format(0.0111,,4)` | `'0.0111'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:25 |  |  |
| 24 | `format(0.00111,,4)` | `'0.0011'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:26 |  |  |
| 25 | `format(0.000111,,4)` | `'0.0001'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:27 |  |  |
| 26 | `format(0.0000111,,4)` | `'0.0000'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:28 |  |  |
| 27 | `format(0.00000111,,4)` | `'0.0000'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:29 |  |  |
| 28 | `format(0.555,,4)` | `'0.5550'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:30 |  |  |
| 29 | `format(0.0555,,4)` | `'0.0555'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:31 |  |  |
| 30 | `format(0.00555,,4)` | `'0.0056'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:32 |  |  |
| 31 | `format(0.000555,,4)` | `'0.0006'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:33 |  |  |
| 32 | `format(0.0000555,,4)` | `'0.0001'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:34 |  |  |
| 33 | `format(0.00000555,,4)` | `'0.0000'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:35 |  |  |
| 34 | `format(0.999,,4)` | `'0.9990'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:36 |  |  |
| 35 | `format(0.0999,,4)` | `'0.0999'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:37 |  |  |
| 36 | `format(0.00999,,4)` | `'0.0100'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:38 |  |  |
| 37 | `format(0.000999,,4)` | `'0.0010'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:39 |  |  |
| 38 | `format(0.0000999,,4)` | `'0.0001'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:40 |  |  |
| 39 | `format(0.00000999,,4)` | `'0.0000'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:41 |  |  |
| 40 | `format(0.455,,4)` | `'0.4550'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:42 |  |  |
| 41 | `format(0.0455,,4)` | `'0.0455'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:43 |  |  |
| 42 | `format(0.00455,,4)` | `'0.0046'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:44 |  |  |
| 43 | `format(0.000455,,4)` | `'0.0005'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:45 |  |  |
| 44 | `format(0.0000455,,4)` | `'0.0000'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:46 |  |  |
| 45 | `format(0.00000455,,4)` | `'0.0000'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)"; traditional rounding, p.141 | brexx:format.rexx:47 |  |  |
| 46 | `format(1.00000045,,6)` | `'1.000000'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)" | brexx:format.rexx:48 |  |  |
| 47 | `format(1.0000000045,,8)` | `'1.00000000'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)" | brexx:format.rexx:50 |  | 11 significant digits; pre-rounding to DIGITS 9 or not gives the same value |
| 48 | `format(12.34,,,,0)` | `'1.234E+1'` | p.91 derived: expt 0: exponential unless the exponent is 0; expp pads the exponent | brexx:format.rexx:51 |  |  |
| 49 | `format(12.34,,,3,0)` | `'1.234E+001'` | p.91 derived: expt 0: exponential unless the exponent is 0; expp pads the exponent | brexx:format.rexx:52 |  |  |
| 50 | `format(12.34,,,3,)` | `'12.34'` | p.91 derived: integer part 2 does not exceed expt 9: simple form, no blanks (cf. example 12345.73,,,3,6) | brexx:format.rexx:53 |  |  |
| 51 | `format(1.234,,,3,0)` | <code>'1.234&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: expt 0, exponent 0, expp 3: expp+2 = 5 blanks | brexx:format.rexx:54 |  |  |
| 52 | `format(12.34,3,,,0)` | <code>'&nbsp;&nbsp;1.234E+1'</code> | p.91 derived: before applies to the integer part of the mantissa | brexx:format.rexx:55 |  | inferred: p.91 does not show before with exponential form |
| 53 | `format(12.34,,2,,0)` | `'1.23E+1'` | p.91 derived: expt 0: exponential unless the exponent is 0; expp pads the exponent | brexx:format.rexx:56 |  |  |
| 54 | `format(12.34,,3,,0)` | `'1.234E+1'` | p.91 derived: expt 0: exponential unless the exponent is 0; expp pads the exponent | brexx:format.rexx:57 |  |  |
| 55 | `format(12.34,,4,,0)` | `'1.2340E+1'` | p.91 derived: expt 0: exponential unless the exponent is 0; expp pads the exponent | brexx:format.rexx:58 |  |  |
| 56 | `format(12.345,,3,,0)` | `'1.235E+1'` | p.91 derived: expt 0: exponential unless the exponent is 0; expp pads the exponent | brexx:format.rexx:59 |  |  |
| 57 | `format(99.999,,,,)` | `'99.999'` | p.91 derived: expt default 9: simple form | brexx:format.rexx:60 |  |  |
| 58 | `format(99.999,,2,,)` | `'100.00'` | p.91 derived: "If after is not the same size as the decimal part ... rounded (or extended with zeros)" | brexx:format.rexx:61 |  |  |
| 59 | `format(.999999,,5,2,2)` | `'9.99999E-01'` | p.91 derived: 6 decimal places > 2*expt=4: exponential; exponent padded to expp | brexx:format.rexx:64 |  | no carry: exponential whether the trigger is tested before or after rounding (1.00000 still needs 5 places > 4) |
| 60 | `format(.999999,,6,2,2)` | `'9.999990E-01'` | p.91 derived: as 59, after 6 extends with a zero | brexx:format.rexx:66 |  |  |
| 61 | `format(90.999,,0)` | `'91'` | p.91 derived: after 0 rounds to an integer | brexx:format.rexx:67 |  |  |
| 62 | `format(0099.999,5,3,,)` | <code>'&nbsp;&nbsp;&nbsp;99.999'</code> | p.91 derived: leading zeros removed, before 5 pads | brexx:format.rexx:68 |  |  |
| 63 | `format(0.0000001,4,,,3)` | <code>'&nbsp;&nbsp;&nbsp;1E-7'</code> | p.91 derived: 7 decimal places > 2*expt=6: exponential | brexx:format.rexx:71 |  |  |
| 64 | `format(0.0000001,4,4,,3)` | <code>'&nbsp;&nbsp;&nbsp;1.0000E-7'</code> | p.91 derived: as 63, after 4 | brexx:format.rexx:72 |  |  |
| 65 | `format(0.000001,4,4,,3)` | <code>'&nbsp;&nbsp;&nbsp;0.0000'</code> | p.91 derived: 6 decimal places, not > 6: simple form, rounded to 4 places | brexx:format.rexx:73 |  |  |
| 66 | `format(0.0000001,4,5,,2)` | <code>'&nbsp;&nbsp;&nbsp;1.00000E-7'</code> | p.91 derived: 7 > 4: exponential | brexx:format.rexx:74 |  |  |
| 67 | `format(0.0000001,4,4,4,3)` | <code>'&nbsp;&nbsp;&nbsp;1.0000E-0007'</code> | p.91 derived: as 64 with expp 4 | brexx:format.rexx:75 |  |  |
| 68 | `format(1000,4,4,,3)` | <code>'&nbsp;&nbsp;&nbsp;1.0000E+3'</code> | p.91 derived: integer part 4 places > expt 3: exponential | brexx:format.rexx:76 |  |  |
| 69 | `format(0.0000001,,,0,3)` | `'0.0000001'` | p.91 derived: expp 0: simple form, overrides expt | brexx:format.rexx:78 |  |  |
| 70 | `format('.00001',,,2,9)` | `'0.00001'` | p.91 derived: decimal places do not exceed 2*expt=18: simple form; no blanks without exponential form | brexx:format.rexx:79 |  |  |
| 71 | `format('.000001',,,2,9)` | `'0.000001'` | p.91 derived: decimal places do not exceed 2*expt=18: simple form; no blanks without exponential form | brexx:format.rexx:80 |  |  |
| 72 | `format('.0000001',,,2,9)` | `'0.0000001'` | p.91 derived: decimal places do not exceed 2*expt=18: simple form; no blanks without exponential form | brexx:format.rexx:81 |  |  |
| 73 | `format('.00000001',,,2,9)` | `'0.00000001'` | p.91 derived: decimal places do not exceed 2*expt=18: simple form; no blanks without exponential form | brexx:format.rexx:82 |  |  |
| 74 | `format(9.9999999,1,10,1,1)` | <code>'9.9999999000&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:88 |  |  |
| 75 | `format(9.9999999,1,10,1,2)` | <code>'9.9999999000&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:89 |  |  |
| 76 | `format(9.9999999,1,10,2,1)` | <code>'9.9999999000&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:90 |  |  |
| 77 | `format(9.9999999,1,10,2,2)` | <code>'9.9999999000&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:91 |  |  |
| 78 | `format(9.9999999,1,10,2,3)` | <code>'9.9999999000&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:92 |  |  |
| 79 | `format(9.9999999,1,10,4,3)` | <code>'9.9999999000&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:93 |  |  |
| 80 | `format(9.9999999,1,8,1,1)` | <code>'9.99999990&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:94 |  |  |
| 81 | `format(9.9999999,1,8,1,2)` | <code>'9.99999990&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:95 |  |  |
| 82 | `format(9.99999999,1,10,1,1)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:96 |  |  |
| 83 | `format(9.99999999,1,10,1,2)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:97 |  |  |
| 84 | `format(9.99999999,1,10,1,3)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:98 |  |  |
| 85 | `format(9.99999999,1,10,2,1)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:99 |  |  |
| 86 | `format(9.99999999,1,10,2,2)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:100 |  |  |
| 87 | `format(9.99999999,1,10,2,3)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:101 |  |  |
| 88 | `format(9.99999999,1,10,3,1)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:102 |  |  |
| 89 | `format(9.99999999,1,10,3,2)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:103 |  |  |
| 90 | `format(9.99999999,1,10,3,3)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:104 |  |  |
| 91 | `format(9.99999999,1,10,4,3)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:105 |  |  |
| 92 | `format(9.99999999,1,10,5,3)` | <code>'9.9999999900&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:106 |  |  |
| 93 | `format(9.99999999,1,8,1,1)` | <code>'9.99999999&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:107 |  |  |
| 94 | `format(9.99999999,1,8,1,2)` | <code>'9.99999999&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:108 |  |  |
| 95 | `format(9.99999999,1,8,2,1)` | <code>'9.99999999&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.91 derived: decimal places > 2*expt: exponential, exponent 0 with expp given: expp+2 blanks | brexx:format.rexx:109 |  |  |
| 96 | `format('0.0000000000000000000001',,,0,)` | `'0.0000000000000000000001'` | p.91 derived: expp 0: simple form | brexx:format.rexx:110 |  | operand held in a variable (line length) |
| 97 | `format(0.0000000000000000001,4)` | <code>'&nbsp;&nbsp;&nbsp;1E-19'</code> | p.91 derived: 19 decimal places > 18: exponential | brexx:format.rexx:69 |  |  |
| 98 | `format(0.0000000000000000001,4,4)` | <code>'&nbsp;&nbsp;&nbsp;1.0000E-19'</code> | p.91 derived: as 97, after 4 | brexx:format.rexx:70 |  |  |
| 99 | `format(0.0000000000000000000001)` | `'1E-22'` | p.90/146 derived: only number: number+0 = 1E-22 (22 places > 18) | brexx:format.rexx:77 |  |  |
| 100 | `numeric form engineering; format('12345.73',,,2,2)` | `'12.34573E+03'` | p.47/147 derived: FORMAT page does not mention FORM; p.47: NUMERIC FORM controls the exponential form of "arithmetic built-in functions"; engineering = power of ten multiple of 3 | brexx:format.rexx:115 |  | inferred from p.47 (FORMAT counted as an arithmetic built-in function) |
| 101 | `numeric form engineering; format('12345.73',,3,,0)` | `'12.346E+3'` | p.47/147 derived: FORMAT page does not mention FORM; p.47: NUMERIC FORM controls the exponential form of "arithmetic built-in functions"; engineering = power of ten multiple of 3 | brexx:format.rexx:116 |  | inferred from p.47 (FORMAT counted as an arithmetic built-in function) |
| 102 | `numeric form engineering; format('123.45',,3,2,0)` | <code>'123.450&nbsp;&nbsp;&nbsp;&nbsp;'</code> | p.47/147 derived: FORMAT page does not mention FORM; p.47: NUMERIC FORM controls the exponential form of "arithmetic built-in functions"; engineering = power of ten multiple of 3; exponent 0 with expp 2: 4 blanks | brexx:format.rexx:117 |  | inferred from p.47 (FORMAT counted as an arithmetic built-in function) |
| 103 | `numeric form engineering; format(12.34,,,,0)` | `'12.34'` | p.47/147 derived: FORMAT page does not mention FORM; p.47: NUMERIC FORM controls the exponential form of "arithmetic built-in functions"; engineering = power of ten multiple of 3; exponent 0, no expp: no blanks (cf. example 1.234573,,3,,0) | brexx:format.rexx:118 |  | inferred from p.47 (FORMAT counted as an arithmetic built-in function) |
| 104 | `length(format(1,60))` | `'60'` | p.91 derived: before 60 pads to 60 characters | brexx:bufovfl.rexx:7 |  |  |
| 105 | `strip(format(1,60))` | `'1'` | p.91 derived: as 104 | brexx:bufovfl.rexx:8 |  |  |
| 106 | `length(format(1,2,60))` | `'63'` | p.91 derived: before 2 + "." + after 60 = 63 characters | brexx:bufovfl.rexx:10 |  |  |
| 107 | `format(1.000000045,,7)` | `'1.0000001'` | p.47/147 derived: number rounded to NUMERIC DIGITS (operands truncated to DIGITS+1, p.141) before after/expp/expt are applied: 1.000000045 -> 1.00000005 -> 7 places | brexx:format.rexx:49 |  | inferred from p.47 (FORMAT counted as an arithmetic built-in function) |
| 108 | `numeric digits 3; format('12345.73',,,2,2)` | `'1.23E+04'` | p.47/147 derived: number rounded to NUMERIC DIGITS (operands truncated to DIGITS+1, p.141) before after/expp/expt are applied: 12300, exponential with expp 2 | brexx:format.rexx:113 |  | inferred from p.47 (FORMAT counted as an arithmetic built-in function) |
| 109 | `numeric digits 3; format('12345.73',,3,,0)` | `'1.230E+4'` | p.47/147 derived: number rounded to NUMERIC DIGITS (operands truncated to DIGITS+1, p.141) before after/expp/expt are applied: 12300, expt 0, after 3 | brexx:format.rexx:114 |  | inferred from p.47 (FORMAT counted as an arithmetic built-in function) |

Note on the expp blanks: the manual example `FORMAT('12345.73',,,3,6) ->
'12345.73'` shows that a non-zero expp adds no blanks when the simple form
is chosen because expt is not exceeded. The expp+2 blanks therefore appear
only when exponential form is selected and the exponent comes out 0
(cases 13, 51, 74-95, 102). This agrees with BREXX's values.

Dropped:

| BREXX case | expression | reason |
|---|---|---|
| format.rexx:6, 7, 111, 112 | `' - 12.73'` cases | moved to FORMATBL (exec dies) |
| format.rexx:1-5, 8-11, 14, 15 | manual examples | duplicates of cases 1-11 |
| format.rexx:62, 83 | `format(99.999,,2,,2)` -> `1.00E+2` | rounding to `after` carries into a new integer digit: across expt 2 in simple form (100.00), or 9.9999E+1 -> 10.00E+1 -> 1.00E+2 in exponential form. p.91 describes neither the order of trigger test and rounding nor the renormalisation after a carry |
| format.rexx:63, 84 | `format(.999999,,4,2,2)` -> `'1.0000    '` | same carry: 9.99999E-1 -> 10.0000E-1 -> 1.0000E+0 (then expp+2 blanks), or simple 1.0000 if tested after rounding |
| format.rexx:65, 85 | `format(.9999999,,5,2,2)` -> `'1.00000    '` | same carry |
| format.rexx:86, 87 | `.0000001` / `.00000001` with `,,,2,9` | duplicates of cases 72, 73 |
| bufovfl.rexx:12-15 | `D2P` | BREXX extension, not in SC28-1883-0 |
| bufovfl.rexx:17-37 | ALLOCATE, EXECIO | BREXX / TSO environment, out of scope |

ERROR-CASE (p.91 states each one is an error):

| expression | rule |
|---|---|
| `format(123,2)` | "If before is not large enough to contain the integer part of the number, an error results" |
| `format(1E10,,,1,0)` | exponent 10 needs 2 places: "if expp is not large enough to contain the exponent, an error results" |
| `format(1,,,10,0)` | "The expp must be less than 10" |
| `format('abc')` | not a number |

## FORMATBL (p.91) -- split from FORMAT

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `format(' - 12.73',,4)` | `'-12.7300'` | p.91 example | spec; brexx:format.rexx:6 |  |  |
| 2 | `format(' - 12.73')` | `'-12.73'` | p.91 example | spec; brexx:format.rexx:7 |  |  |
| 3 | `numeric digits 3; format(' - 12.73')` | `'-12.7'` | p.90 derived: number only: exactly number+0, rounded to DIGITS 3 | brexx:format.rexx:112 |  |  |
| 4 | `numeric digits 3; format(' - 12.73',,4)` | `'-12.7000'` | p.47/147 derived: rounded to DIGITS 3 (-12.7), then extended to 4 places | brexx:format.rexx:111 |  | inferred from p.47 (FORMAT counted as an arithmetic built-in function) |

Split because a number with a blank between sign and digits ends the exec in rexx370 (run_rc 24), hiding every later FORMAT case.

## FORM (p.90, NUMERIC p.47)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `form()` | `'SCIENTIFIC'` | p.90 example | spec; brexx:form.rexx:1 |  |  |
| 2 | `numeric form engineering; form()` | `'ENGINEERING'` | p.90 derived: returns the current NUMERIC FORM | new |  |  |
| 3 | `form() after numeric form scientific` | `'SCIENTIFIC'` | p.90 derived: as 2 | new |  |  |
| 4 | `numeric form value 'SCIENTIFIC'; form()` | `'SCIENTIFIC'` | p.47 derived: FORM VALUE expression | new |  |  |
| 5 | `numeric form ('ENGIN' \|\| 'EERING'); form()` | `'ENGINEERING'` | p.47 derived: "VALUE may be omitted if the expression does not begin with a symbol or a literal string" | new |  |  |

ERROR-CASE: `numeric form value 'FOO'` (p.47: the result "must be either SCIENTIFIC or ENGINEERING").

## FUZZ (p.91, NUMERIC p.47)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `fuzz()` | `'0'` | p.91 example | spec; brexx:fuzz.rexx:1 |  |  |
| 2 | `numeric fuzz 1; fuzz()` | `'1'` | p.91 derived: returns the current NUMERIC FUZZ | new |  |  |
| 3 | `numeric fuzz 1+1; fuzz()` | `'2'` | p.47 derived: expression is evaluated | new |  |  |
| 4 | `numeric fuzz 3; numeric fuzz; fuzz()` | `'0'` | p.47 derived: "If no expression is given, then the default value of 0 is used" | new |  |  |

ERROR-CASE: `numeric fuzz 9` under DIGITS 9 and `numeric fuzz -1` (p.47: "zero or a positive whole number that is smaller than the current NUMERIC DIGITS").

## DIGITS (p.87, NUMERIC p.47)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `digits()` | `'9'` | p.87 example | spec |  |  |
| 2 | `numeric digits 5; digits()` | `'5'` | p.87 derived: returns the current NUMERIC DIGITS | new |  |  |
| 3 | `numeric digits 12; digits()` | `'12'` | p.87 derived: as 2; DIGITS may exceed 9 (p.47 "no limit") | new |  |  |
| 4 | `numeric digits 4+1; digits()` | `'5'` | p.47 derived: expression is evaluated | new |  |  |
| 5 | `numeric digits 12; numeric digits; digits()` | `'9'` | p.47 derived: "If no expression is given, then the default value of 9 is used" | new |  |  |

No BREXX file. ERROR-CASE: `numeric digits 0`, `numeric digits 2.5` (p.47: "must be a positive whole number"), `numeric digits 1` while FUZZ is 1 (p.47: "larger than the current NUMERIC FUZZ").

## RANDOM (p.97-98)

Per the brief only range, form and reproducibility are tested; the manual examples (305, 7, 123, 0) are values that "may differ from implementation to implementation" (note 3) and are not used.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `v = random(); v >= 0 & v <= 999` | `'1'` | p.97 derived: default min 0, max 999 | brexx:random.rexx:1 |  |  |
| 2 | `v == trunc(v)` | `'1'` | p.97 derived: "nonnegative whole number" | new |  |  |
| 3 | `v = random(5,8); v >= 5 & v <= 8` | `'1'` | p.97 derived: range min to max inclusive | brexx:random.rexx:2-3 |  |  |
| 4 | `v = random(2); v >= 0 & v <= 2` | `'1'` | p.97 derived: "If only one argument is specified, the range will be from 0 to that number" | brexx:random.rexx:4 |  |  |
| 5 | `random(5,5)` | `'5'` | p.97 derived: inclusive range of width 0 | new |  |  |
| 6 | `v = random(0,100000); in range` | `'1'` | p.97 derived: range "must not exceed 100000" (100000 does not exceed it) | new |  |  |
| 7 | `random(,,1983) == random(,,1983)` | `'1'` | p.97 derived: seed "if repeatable results are desired" | brexx:random.rexx:5 |  | inferred from the p.97 example RANDOM(,,1983) -> 123 /* reproducible */ and note 1: re-specifying the seed restarts the sequence |
| 8 | `r1 = random(,,1983); in 0..999` | `'1'` | p.97 derived: default range with a seed | new |  |  |
| 9 | `40-value sequence after random(1,6,12345), taken twice` | `'1'` | p.97-98 derived: note 1, same seed gives the same sequence | new |  | inferred from the p.97 example RANDOM(,,1983) /* reproducible */ and note 1, as case 7 |
| 10 | `words(s1)` | `'40'` | harness check | new |  |  |
| 11 | `every word of s1 is a whole number 1..6` | `'1'` | p.97 derived: range 1..6 | new |  |  |
| 12 | `v = random(10,20,7); in range` | `'1'` | p.97 derived: range with a seed | new |  |  |

Dropped: min > max and negative bounds (p.97 does not say what happens).
ERROR-CASE: `random(0,100001)` (p.97: the magnitude of the range "must not
exceed 100000"); `random(1,6,1.5)` (seed "must be a whole number").
## NUMFMT (chapter 6, p.139-147)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `2.40 + 1` | `'3.40'` | p.139 example | spec |  |  |
| 2 | `2.40 - 2` | `'0.40'` | p.139 example | spec |  |  |
| 3 | `2.5 * 2` | `'5.0'` | p.139 example | spec |  |  |
| 4 | `2 / 3` | `'0.666666667'` | p.139 example (text: "0.666666667") | spec; brexx:numfmt.rexx:3 | 0.666666666666667 |  |
| 5 | `1e6 * 1e6` | `'1E+12'` | p.140 example | spec |  |  |
| 6 | `1 / 3E10` | `'3.33333333E-11'` | p.140 example | spec |  |  |
| 7 | `numeric digits 5; 54321*54321` | `'2.9508E+9'` | p.146 example | spec |  |  |
| 8 | `123.45 * 1e11` | `'1.2345E+13'` | p.147 example | spec |  |  |
| 9 | `numeric form engineering; 123.45 * 1e11` | `'12.345E+12'` | p.147 example | spec |  |  |
| 10 | `10000000000 * 10000000000` | `'1.00000000E+20'` | p.141/146 derived: trailing zeros retained for multiplication (p.141); 21 places > 9: exponential form if places before the point > DIGITS or after the point > 2*DIGITS | new |  | exact product 1 followed by 20 zeros, rounded to 9 digits |
| 11 | `.00000000001 * .00000000001` | `'1E-22'` | p.146 derived: 22 places after the point > 18 | new |  | p.146 shows the long form 0.000...01 as what "pure" arithmetic would give |
| 12 | `numeric digits 5; 99999 + 1` | `'1.0000E+5'` | p.142/146 derived: 100000 rounded to 5 digits, trailing zeros kept for addition; 6 places > 5: exponential form if places before the point > DIGITS or after the point > 2*DIGITS | new |  |  |
| 13 | `2.40 - 2.40` | `'0'` | p.140 derived: "A zero result is always expressed as the single digit 0" | new |  |  |
| 14 | `0 * 1.5` | `'0'` | p.140 derived: zero result | brexx:numfmt.rexx:11 |  |  |
| 15 | `1e20 * 1` | `'1E+20'` | p.146 derived: 21 places > 9: exponential form if places before the point > DIGITS or after the point > 2*DIGITS | brexx:numfmt.rexx:8 | 100000000000000000000 |  |
| 16 | `0.1 + 0.2` | `'0.3'` | p.142 derived: addition | brexx:numfmt.rexx:1, 17 |  |  |
| 17 | `1 / 3` | `'0.333333333'` | p.142 derived: division rounded to 9 digits | brexx:numfmt.rexx:2, 18 | 0.333333333333333 (case 2; case 18 agrees with 1988) |  |
| 18 | `-1 / 3` | `'-0.333333333'` | p.142 derived: as 17 | brexx:numfmt.rexx:4 | -0.333333333333333 |  |
| 19 | `2.5 * 1.1` | `'2.75'` | p.142 derived: multiplication | brexx:numfmt.rexx:5 |  |  |
| 20 | `1 / 4` | `'0.25'` | p.142 derived: division, trailing zeros removed | brexx:numfmt.rexx:6 |  |  |
| 21 | `123.456 * 1` | `'123.456'` | p.142 derived: trailing zeros retained for multiplication (p.141) | brexx:numfmt.rexx:7 |  |  |
| 22 | `1e-5 * 1` | `'0.00001'` | p.146 derived: 5 places, not > 18 | brexx:numfmt.rexx:9, 21 |  |  |
| 23 | `2**60` | `'1.1529215E+18'` | p.143 derived: power, 9 digits, trailing zeros removed; 19 places > 9 | brexx:numfmt.rexx:10, 19 | 1152921504606850000 (case 10; case 19 agrees with 1988) |  |
| 24 | `5000 * 2**0` | `'5000'` | p.143 derived: x**0 = 1 | brexx:numfmt.rexx:12 |  |  |
| 25 | `f = 1; do i = 1 to 25; f = f * i; end; f` | `'1.55112100E+25'` | p.141/146 derived: trailing zeros retained for multiplication (p.141) (digits 1551121004... rounded: 155112100); 26 places > 9 | brexx:numfmt.rexx:13 | 15511210043331000000000000 |  |
| 26 | `7.7 + 0` | `'7.7'` | p.142 derived: number+0 | brexx:numfmt.rexx:14 |  |  |
| 27 | `numeric digits 20; 20! by the same loop` | `'2432902008176640000'` | p.146 derived: 19 places, not > 20 | brexx:numfmt.rexx:15 |  |  |
| 28 | `numeric digits 20; f * 1 (f from case 25)` | `'1.55112100E+25'` | p.141/146 derived: f already has 9 digits; 26 places > 20 | brexx:numfmt.rexx:16 | 1.5511210043331E+25 |  |
| 29 | `1e9 * 1` | `'1E+9'` | p.146 derived: 10 places > 9: exponential form if places before the point > DIGITS or after the point > 2*DIGITS | brexx:numfmt.rexx:20 |  |  |
| 30 | `123456.789 * 1000` | `'123456789'` | p.142 derived: 123456789.000 rounded to 9 digits | brexx:numfmt.rexx:22 |  |  |
| 31 | `123456.789 * 10000` | `'1.23456789E+9'` | p.146 derived: 10 places > 9 | brexx:numfmt.rexx:23 |  |  |
| 32 | `0.5**20` | `'0.000000953674316'` | p.143 derived: power: 9.53674316E-7; 15 places, not > 18: simple form | brexx:numfmt.rexx:24 |  |  |
| 33 | `numeric digits 1; 2.5 * 0.1` | `'0.3'` | p.141 derived: 0.25 rounded to 1 digit, 5 rounds up | brexx:numfmt.rexx:25 |  |  |
| 34 | `numeric digits 1; v2 = 2.5` | `'2.5'` | p.13 derived: a literal is a string; no arithmetic, no rounding | brexx:numfmt.rexx:26 |  |  |
| 35 | `10000000.55` | `'10000000.55'` | p.13 derived: as 34 | brexx:numfmt.rexx:27 |  |  |
| 36 | `3.14159265358979` | `'3.14159265358979'` | p.13 derived: as 34 | brexx:numfmt.rexx:28 |  |  |
| 37 | `10000000.45 \|\| ''` | `'10000000.45'` | p.13 derived: concatenation, no arithmetic | brexx:numfmt.rexx:29 |  |  |
| 38 | `'a' 10000000.45` | `'a 10000000.45'` | p.13 derived: blank concatenation | brexx:numfmt.rexx:30 |  |  |
| 39 | `v = 10000000.45; v` | `'10000000.45'` | p.13 derived: assignment does not round | brexx:numfmt.rexx:31 |  |  |

numfmt.rexx cases 1-16 were written for BREXX's 15-digit reals; the 1988
default precision is 9 (p.140), so cases 2, 3, 4, 8, 10, 13, 16 differ.
Folded, not dropped: numfmt.rexx:3, 17, 18, 19, 21 have the same
expression and (1988) value as rows 4, 16, 17, 23, 22 and are cited there.

ERROR-CASE: overflow of the exponent, e.g. `1E999999999 * 10`: error 42,
IRX0042I (p.402: "required an exponent greater than the limit of 9
digits"); see also p.148.

## ARITH (chapter 6, p.141-145)

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `numeric digits 5; 12+7.00` | `'19.00'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 2 | `numeric digits 5; 1.3-1.07` | `'0.23'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 3 | `numeric digits 5; 1.3-2.07` | `'-0.77'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 4 | `numeric digits 5; 1.20*3` | `'3.60'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 5 | `numeric digits 5; 7*3` | `'21'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 6 | `numeric digits 5; 0.9*0.8` | `'0.72'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 7 | `numeric digits 5; 1/3` | `'0.33333'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 8 | `numeric digits 5; 2/3` | `'0.66667'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 9 | `numeric digits 5; 5/2` | `'2.5'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 10 | `numeric digits 5; 1/10` | `'0.1'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 11 | `numeric digits 5; 12/12` | `'1'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 12 | `numeric digits 5; 8.0/2` | `'4'` | p.143 example (NUMERIC DIGITS 5) | spec |  |  |
| 13 | `numeric digits 5; 2**3` | `'8'` | p.144 example (NUMERIC DIGITS 5) | spec |  |  |
| 14 | `numeric digits 5; 1.7**8` | `'69.758'` | p.144 example (NUMERIC DIGITS 5) | spec |  | the 2**-3 row of the same table is in ARITHNEG |
| 15 | `numeric digits 5; 2%3` | `'0'` | p.144 example (NUMERIC DIGITS 5) | spec |  |  |
| 16 | `numeric digits 5; 2.1//3` | `'2.1'` | p.144 example (NUMERIC DIGITS 5) | spec |  |  |
| 17 | `numeric digits 5; 10%3` | `'3'` | p.144 example (NUMERIC DIGITS 5) | spec |  |  |
| 18 | `numeric digits 5; 10//3` | `'1'` | p.144 example (NUMERIC DIGITS 5) | spec |  |  |
| 19 | `numeric digits 5; -10//3` | `'-1'` | p.144 example (NUMERIC DIGITS 5) | spec |  |  |
| 20 | `numeric digits 5; 10.2//1` | `'0.2'` | p.144 example (NUMERIC DIGITS 5) | spec |  |  |
| 21 | `numeric digits 5; 10//0.3` | `'0.1'` | p.144 example (NUMERIC DIGITS 5) | spec |  |  |
| 22 | `numeric digits 5; 4.9999 = 5 (fuzz 0)` | `'0'` | p.145 example | spec |  |  |
| 23 | `numeric digits 5; 4.9999 < 5 (fuzz 0)` | `'1'` | p.145 example | spec |  |  |
| 24 | `numeric digits 5; 4.9999 = 5 (fuzz 1)` | `'1'` | p.145 example | spec |  |  |
| 25 | `numeric digits 5; 4.9999 < 5 (fuzz 1)` | `'0'` | p.145 example | spec |  |  |
| 26 | `0**0` | `'1'` | p.143 text: "x**0 = 1 for all x, including 0**0" | spec |  |  |
| 27 | `7.5**0` | `'1'` | p.143 derived: x**0 = 1 | new |  |  |
| 28 | `(-2)**3` | `'-8'` | p.143 derived: repeated multiplication | new |  |  |
| 29 | `(-2)**2` | `'4'` | p.143 derived: as 28 | new |  |  |
| 30 | `0**3` | `'0'` | p.143 derived: as 28 | new |  |  |
| 31 | `2**1.0` | `'2'` | p.143/147 derived: 1.0 is a whole number ("decimal part which is all zeros") | new |  |  |
| 32 | `-10%3` | `'-3'` | p.144 derived: integer part; sign as for normal division | new |  |  |
| 33 | `10//-3` | `'1'` | p.144 derived: "The sign of the remainder, if non-zero, is the same as that of the original dividend" | new |  |  |
| 34 | `-10//-3` | `'-1'` | p.144 derived: as 33 | new |  |  |
| 35 | `7.5%2` | `'3'` | p.144 derived: integer part of 3.75 | new |  |  |
| 36 | `7.5//2` | `'1.5'` | p.144 derived: residue 7.5 - 2*3 | new |  |  |
| 37 | `-' 3 '` | `'-3'` | p.15 derived: "Prefix - ... Same as 0-term"; blanks around a number allowed (p.140) | new |  |  |
| 38 | `+' 3.0 '` | `'3.0'` | p.142 derived: "If either number is zero, the other number ... is used as the result" | new |  |  |
| 39 | `(1.0 = 1)` | `'1'` | p.145 derived: (A - B) compared with 0 | new |  |  |
| 40 | `(' 12 ' = 12)` | `'1'` | p.140/145 derived: leading/trailing blanks allowed in a number | new |  |  |
| 41 | `(1 == 1.0)` | `'0'` | p.14/145 derived: == is a strict comparison | new |  |  |
| 42 | `('1E1' = 10)` | `'1'` | p.145/146 derived: exponential notation is valid input | new |  |  |
| 43 | `numeric digits 5; 12345678 = 12345679` | `'1'` | p.141/145 derived: operands truncated to DIGITS+1 = 6 digits (12345600 both) before the subtraction; difference 0 | new |  | inferred from the p.141 truncation rule; ANSI-style rounding of the operands gives the same answer |
| 44 | `numeric digits 5; 2/3 = 0.66667` | `'1'` | p.145 derived: 2/3 is 0.66667 under DIGITS 5 (p.143); difference 0 | new |  |  |

No BREXX file (all rows are manual examples or new). ERROR-CASE:

| expression | rule |
|---|---|
| `10000000000%3` (DIGITS 9) | p.144 example: the result needs 10 digits, "the operation is in error" |
| `10000000000//3` (DIGITS 9) | p.144: remainder fails when integer division would |
| `2**0.5` | p.143/147: the power must be a whole number |
| `1/0`, `1%0`, `1//0` | error 42: IRX0042I, p.402 ("often as a result of trying to divide a number by 0") |
| `'abc' + 1` | not a number |

## ARITHNEG (p.143-144) -- split from ARITH

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `numeric digits 5; 2**-3` | `'0.125'` | p.144 example (NUMERIC DIGITS 5) | spec |  |  |
| 2 | `2**-1` | `'0.5'` | p.143 derived: "the absolute value of the power is used, and then the result is inverted (divided into 1)" | new |  |  |
| 3 | `(-2)**-3` | `'-0.125'` | p.143 derived: as 2; (-2)**3 = -8, 1/-8 | new |  |  |

Split because any negative power exponent ends the exec in rexx370 today
(run_rc 24; `2**-3`, `2**(-3)` and `2**n` with n = -3 all do).

ERROR-CASE: `0**-1` (brexx:powzero.rexx:6): the inversion (p.143)
divides 1 by 0, error 42, IRX0042I (p.402: "often as a result of trying
to divide a number by 0").

## BREXX files owned by this group

| file | used in | not used |
|---|---|---|
| abs.rexx | ABS 1-6 | -- |
| sign.rexx | SIGN 1-8 | -- |
| max.rexx | MAX 1-13 | 8 (duplicate) |
| min.rexx | MIN 1-12 | 8 (duplicate) |
| trunc.rexx | TRUNC 1-87 | 86, 87 (negative zero, unsettled), 93-99 (duplicates) |
| format.rexx | FORMAT, FORMATBL | 62-63, 65, 83-85 (rounding carry, ambiguous), 86-87 (duplicates) |
| form.rexx | FORM 1 | -- |
| fuzz.rexx | FUZZ 1 | -- |
| random.rexx | RANDOM 1, 3, 4, 7 | concrete values never tested |
| numfmt.rexx | NUMFMT 4, 14-39 | -- (3, 17-19, 21 folded into rows 4, 16, 17, 23, 22) |
| powzero.rexx | -- | its one case `0**-1` is an ERROR-CASE (uses SIGNAL ON SYNTAX) |
| bufovfl.rexx | FORMAT 104-106 | D2P (BREXX extension), ALLOCATE/EXECIO (out of scope) |

## Host run (brxrun, bytecode VM, 2026-09-30)

| member | cases | result |
|---|---|---|
| ABS | 7 | 6 pass; fail 7 (DIGITS 3 not applied: 12.345) |
| SIGN | 8 | PASS |
| MAX | 14 | PASS |
| MIN | 13 | PASS |
| TRUNC | 88 | 81 pass; fail 16, 19, 20, 22, 23, 24, 27 (no rounding to DIGITS 9 before truncation) |
| FORMAT | 109 | 74 pass; fail 13, 51, 74-95 (no expp+2 blanks), 59, 60, 64, 66-68, 98 (expt trigger for exponential form not applied), 100-103 (FORM ENGINEERING ignored) |
| FORMATBL | 4 | died at case 1 (run_rc 24) |
| FORM | 5 | 3 pass; died at case 4 (NUMERIC FORM VALUE, run_rc 20) |
| FUZZ | 4 | 3 pass; died at case 4 (NUMERIC FUZZ without expression, run_rc 20) |
| DIGITS | 5 | 4 pass; died at case 5 (NUMERIC DIGITS without expression, run_rc 20) |
| RANDOM | 12 | 9 pass; fail 4 (`random(2)` returned 512 / 18), 11 (`random(1,6,12345)` returned 0), 12 (`random(10,20,7)` returned 0) |
| NUMFMT | 39 | 29 pass; fail 1-3, 10, 12, 25, 28 (trailing zeros dropped), 29, 31 (no exponential form past DIGITS integer places), 32 (0.5**20 gave 9.53674325E-7) |
| ARITH | 44 | 37 pass; fail 1, 4 (trailing zeros), 14 (1.7**8 gave 69.76), 24, 25 (NUMERIC FUZZ not applied), 38 (`+' 3.0 '` returned the operand unchanged), 43 (DIGITS 5 comparison) |
| ARITHNEG | 3 | died at case 1 (run_rc 24) |

The token-walk path (`REXX370_BYTECODE=0`) gives the same exit codes for
ABS, TRUNC, FORMAT, NUMFMT, ARITH, RANDOM and MAX.
