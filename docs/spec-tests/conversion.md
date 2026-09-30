# Conversion functions -- SC28-1883-0 conformance cases

Group **conversion** of the spec suite (#261): BITAND, BITOR, BITXOR, C2D, C2X, D2C, D2X, DATATYPE, X2C, X2D. Members live in `test/spec/`. Every expected value is a manual example or derived from the rule quoted in the `spec` column; page numbers are *printed* pages of SC28-1883-0 (PDF page = printed + 15).

General rules used throughout (printed p.77, general notes on the built-in functions): built-ins work with NUMERIC DIGITS 9 "except where stated" (C2D, X2D and DATATYPE W state it); "a null string can be supplied"; "if a pad character is specified, it must be exactly one character long"; a suboption letter "can be in upper- or lowercase"; the examples "assume an EBCDIC implementation".

Hexadecimal strings (printed p.9-10): "A single leading 0 is added, if necessary ... The blanks, which may only be present at byte boundaries (and not at the beginning or end of the string)"; the manual lists `"1 d8"x` as valid. X2C and X2D (p.109) repeat the rule for their argument.

Conventions: byte-valued results (BIT*, D2C, most X2C) are compared as `c2x(result)` against an upper-case hex literal, which keeps the comparison readable and independent of the character set. `te` marks cases whose result depends on EBCDIC (letters converted to or from codes, a blank pad); they are skipped on an ASCII host. The `expected` column gives the value as the manual writes it.

Splits: `C2DODD` and `C2XODD` hold the cases whose argument is a hex string with an odd number of digits (`'a'x`, `'101'x`, valid by p.9). On the host today such a literal stops the whole exec before its first clause (run_rc 21), so they are kept apart from `C2D` / `C2X`. In `X2D` the two prefix-plus cases (`x2d(+1E+2)`, `x2d(+.1E2)`) are placed last for the same reason (runtime death).

Harness workaround (not a case): no call line ends in `,,` (argument separator plus continuation). rexx370 passes an extra empty argument for that form today, so every call stays on one line of at most 72 columns and long values go into a setup variable (`hx` in `X2D`).

BREXX test files owned by this group: bitand, bitor, bitxor, c2d, c2x, d2c, d2x, datatyp, x2c, x2d (sources below), b2x, x2b (dropped, see the end).

## BITAND (p.80) -- member `BITAND`

9 cases: 4 manual examples, 5 derived. BREXX source: `bitand.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `bitand('73'x,'27'x)` | `'23'x` | p.80 example | spec; brexx:bitand.rexx:1 |  |  |
| 2 | `bitand('13'x,'5555'x)` | `'1155'x` | p.80 example | spec; brexx:bitand.rexx:2 |  |  |
| 3 | `bitand('13'x,'5555'x,'74'x)` | `'1154'x` | p.80 example | spec; brexx:bitand.rexx:3 |  |  |
| 4 | `bitand('pQrS',,'BF'x)` | `'pqrs'` | p.80 example (EBCDIC) | spec |  | te (EBCDIC only) |
| 5 | `bitand('123456'x,'3456'x)` | `'101456'x` | p.80 derived: 'If no pad character is provided, the ... operation terminates when the shorter of the two strings is exhausted, and the unprocessed portion of the longer string is appended' | brexx:bitand.rexx:4 |  |  |
| 6 | `bitand('3456'x,'123456'x,'99'x)` | `'101410'x` | p.80 derived: 'If pad is provided, it is used to extend the shorter of the two strings on the right' | brexx:bitand.rexx:5 |  |  |
| 7 | `bitand('123456'x,,'55'x)` | `'101454'x` | p.80 derived: 'The default for string2 is the zero length (null) string' + pad rule | brexx:bitand.rexx:6 |  |  |
| 8 | `bitand('foobar')` | `'foobar'` | p.80 derived: 'The default for string2 is the zero length (null) string' + no-pad rule | brexx:bitand.rexx:7 |  |  |
| 9 | `bitand('FOOBAR',,'BF'x)` | `'foobar'` | p.80 derived from the 'pQrS' example (EBCDIC: upper = lower + '40'x) | brexx:bitand.rexx:8 |  | te (EBCDIC only) |

ERROR-CASEs (specified outcome is an error; not in the exec, waiting for SIGNAL ON SYNTAX):

| expression | status | spec |
|---|---|---|
| `bitand('12'x,'34'x,'ab')` | ERROR-CASE | p.77: 'If a pad character is specified, it must be exactly one character long.' |

## BITOR (p.80) -- member `BITOR`

10 cases: 5 manual examples, 5 derived. BREXX source: `bitor.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `bitor('15'x,'24'x)` | `'35'x` | p.80 example | spec; brexx:bitor.rexx:1 |  |  |
| 2 | `bitor('15'x,'2456'x)` | `'3556'x` | p.80 example | spec; brexx:bitor.rexx:2 |  |  |
| 3 | `bitor('15'x,'2456'x,'F0'x)` | `'35F6'x` | p.80 example | spec; brexx:bitor.rexx:3 |  |  |
| 4 | `bitor('1111'x,,'4D'x)` | `'5D5D'x` | p.80 example | spec; brexx:bitor.rexx:4 |  | BREXX wrote '5D5d'x and compared with \= (non-strict); same value |
| 5 | `bitor('Fred',,'40'x)` | `'FRED'` | p.80 example (EBCDIC) | spec |  | te (EBCDIC only) |
| 6 | `bitor('123456'x,'3456'x)` | `'367656'x` | p.80 derived: 'If no pad character is provided, the ... operation terminates when the shorter of the two strings is exhausted, and the unprocessed portion of the longer string is appended' | brexx:bitor.rexx:5 |  |  |
| 7 | `bitor('3456'x,'123456'x,'99'x)` | `'3676DF'x` | p.80 derived: 'If pad is provided, it is used to extend the shorter of the two strings on the right' | brexx:bitor.rexx:6 |  |  |
| 8 | `bitor('123456'x,,'55'x)` | `'577557'x` | p.80 derived: 'The default for string2 is the zero length (null) string' + pad rule | brexx:bitor.rexx:7 |  |  |
| 9 | `bitor('foobar')` | `'foobar'` | p.80 derived: 'The default for string2 is the zero length (null) string' + no-pad rule | brexx:bitor.rexx:8 |  |  |
| 10 | `bitor('foobar',,'40'x)` | `'FOOBAR'` | p.80 derived from the 'Fred' example (EBCDIC) | brexx:bitor.rexx:9 |  | te (EBCDIC only) |

ERROR-CASEs (specified outcome is an error; not in the exec, waiting for SIGNAL ON SYNTAX):

| expression | status | spec |
|---|---|---|
| `bitor('12'x,'34'x,'ab')` | ERROR-CASE | p.77: pad 'must be exactly one character long' |

## BITXOR (p.81) -- member `BITXOR`

11 cases: 6 manual examples, 5 derived. BREXX source: `bitxor.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `bitxor('12'x,'22'x)` | `'30'x` | p.81 example | spec; brexx:bitxor.rexx:1 |  |  |
| 2 | `bitxor('1211'x,'22'x)` | `'3011'x` | p.81 example | spec; brexx:bitxor.rexx:2 |  |  |
| 3 | `bitxor('C711'x,'222222'x,' ')` | `'E53362'x` | p.81 example | spec; brexx:bitxor.rexx:3 |  | te (EBCDIC only); te: the pad is a blank, '40'x only on EBCDIC |
| 4 | `bitxor('1111'x,'444444'x)` | `'555544'x` | p.81 example | spec |  |  |
| 5 | `bitxor('1111'x,'444444'x,'40'x)` | `'555504'x` | p.81 example | spec; brexx:bitxor.rexx:4 |  |  |
| 6 | `bitxor('1111'x,,'4D'x)` | `'5C5C'x` | p.81 example | spec; brexx:bitxor.rexx:5 |  |  |
| 7 | `bitxor('123456'x,'3456'x)` | `'266256'x` | p.81 derived: 'If no pad character is provided, the ... operation terminates when the shorter of the two strings is exhausted, and the unprocessed portion of the longer string is appended' | brexx:bitxor.rexx:6 |  |  |
| 8 | `bitxor('3456'x,'123456'x,'99'x)` | `'2662CF'x` | p.81 derived: 'If pad is provided, it is used to extend the shorter of the two strings on the right' | brexx:bitxor.rexx:7 |  |  |
| 9 | `bitxor('123456'x,,'55'x)` | `'476103'x` | p.81 derived: 'The default for string2 is the zero length (null) string' + pad rule | brexx:bitxor.rexx:8 |  |  |
| 10 | `bitxor('foobar')` | `'foobar'` | p.81 derived: 'The default for string2 is the zero length (null) string' + no-pad rule | brexx:bitxor.rexx:9 |  |  |
| 11 | `bitxor('FooBar',,'40'x)` | `'fOObAR'` | p.81 derived: XOR with 40x flips case (EBCDIC) | brexx:bitxor.rexx:10 |  | te (EBCDIC only) |

ERROR-CASEs (specified outcome is an error; not in the exec, waiting for SIGNAL ON SYNTAX):

| expression | status | spec |
|---|---|---|
| `bitxor('12'x,'34'x,'ab')` | ERROR-CASE | p.77: pad 'must be exactly one character long' |

## C2D (p.83-84) -- member `C2D`

30 cases: 12 manual examples, 18 derived. BREXX source: `c2d.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `c2d('09'X)` | `'9'` | p.83 example | spec; brexx:c2d.rexx:1 |  |  |
| 2 | `c2d('81'X)` | `'129'` | p.83 example | spec; brexx:c2d.rexx:2 |  |  |
| 3 | `c2d('FF81'X)` | `'65409'` | p.83 example | spec; brexx:c2d.rexx:4 |  |  |
| 4 | `c2d('a')` | `'129'` | p.83 example (EBCDIC) | spec; brexx:c2d.rexx:34 |  | te (EBCDIC only) |
| 5 | `c2d('81'X,1)` | `'-127'` | p.84 example | spec; brexx:c2d.rexx:6 |  |  |
| 6 | `c2d('81'X,2)` | `'129'` | p.84 example | spec; brexx:c2d.rexx:7 |  |  |
| 7 | `c2d('FF81'X,2)` | `'-127'` | p.84 example | spec; brexx:c2d.rexx:8,30 |  |  |
| 8 | `c2d('FF81'X,1)` | `'-127'` | p.84 example | spec; brexx:c2d.rexx:9,29 |  |  |
| 9 | `c2d('FF7F'X,1)` | `'127'` | p.84 example | spec; brexx:c2d.rexx:10,23 |  |  |
| 10 | `c2d('F081'X,2)` | `'-3967'` | p.84 example | spec; brexx:c2d.rexx:11 |  |  |
| 11 | `c2d('F081'X,1)` | `'-127'` | p.84 example | spec; brexx:c2d.rexx:12 |  |  |
| 12 | `c2d('0031'X,0)` | `'0'` | p.84 example | spec; brexx:c2d.rexx:33 |  |  |
| 13 | `c2d('')` | `'0'` | p.83 derived: 'If string is the null string, then 0 is returned' | brexx:c2d.rexx:5,14 |  |  |
| 14 | `c2d('ff80'x,1)` | `'-128'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:13,26 |  |  |
| 15 | `c2d('ff'x)` | `'255'` | p.83 derived: 'If n is not specified, the sequence ... is processed as an unsigned binary number' | brexx:c2d.rexx:16 |  |  |
| 16 | `c2d('ffff'x)` | `'65535'` | p.83 derived: 'If n is not specified, the sequence ... is processed as an unsigned binary number' | brexx:c2d.rexx:17 |  |  |
| 17 | `c2d('ffff'x,2)` | `'-1'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:18 |  |  |
| 18 | `c2d('ffff'x,1)` | `'-1'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:19 |  |  |
| 19 | `c2d('fffe'x,2)` | `'-2'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:20 |  |  |
| 20 | `c2d('fffe'x,1)` | `'-2'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:21 |  |  |
| 21 | `c2d('ffff'x,3)` | `'65535'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' (padded, not sign-extended) | brexx:c2d.rexx:22 |  |  |
| 22 | `c2d('ff7f'x,2)` | `'-129'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:24 |  |  |
| 23 | `c2d('ff7f'x,3)` | `'65407'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:25 |  |  |
| 24 | `c2d('ff80'x,2)` | `'-128'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:27 |  |  |
| 25 | `c2d('ff80'x,3)` | `'65408'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:28 |  |  |
| 26 | `c2d('ff81'x,3)` | `'65409'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:31 |  |  |
| 27 | `c2d('ffffffffff'x,5)` | `'-1'` | p.84 derived: 'If n is specified, the given string is padded on the left with 00x characters (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:c2d.rexx:32 |  |  |
| 28 | `c2d('foo')` | `'8820374'` | p.83 derived: 'foo' is '869696'x in EBCDIC, unsigned | brexx:c2d.rexx:35 |  | te (EBCDIC only) |
| 29 | `c2d('bar')` | `'8552857'` | p.83 derived: 'bar' is '828199'x in EBCDIC, unsigned | brexx:c2d.rexx:36 |  | te (EBCDIC only) |
| 30 | `c2d('FFFFFFFF'x) under NUMERIC DIGITS 10` | `'4294967295'` | p.83 derived: 'the result must not have more digits than the current setting of NUMERIC DIGITS' (setup: NUMERIC DIGITS 10) | new |  |  |

ERROR-CASEs (specified outcome is an error; not in the exec, waiting for SIGNAL ON SYNTAX):

| expression | status | spec |
|---|---|---|
| `c2d('FFFFFFFF'x) under NUMERIC DIGITS 9` | ERROR-CASE | p.83: 'If the result cannot be expressed as a whole number, an error results' (10 digits > 9) |

## C2DODD (p.83) -- member `C2DODD`

2 cases: 0 manual examples, 2 derived. BREXX source: `c2d.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `c2d('a'x)` | `'10'` | p.9 derived: hex string 'A single leading 0 is added, if necessary' | brexx:c2d.rexx:3 |  |  |
| 2 | `c2d('101'x)` | `'257'` | p.83 derived: 'If n is not specified, the sequence ... is processed as an unsigned binary number'; p.9 leading 0 | brexx:c2d.rexx:15 |  |  |

## C2X (p.84) -- member `C2X`

7 cases: 2 manual examples, 5 derived. BREXX source: `c2x.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `c2x('72s')` | `'F7F2A2'` | p.84 example (EBCDIC) | spec; brexx:c2x.rexx:1 |  | te (EBCDIC only) |
| 2 | `c2x('0123'X)` | `'0123'` | p.84 example | spec; brexx:c2x.rexx:2 |  |  |
| 3 | `c2x('foobar')` | `'869696828199'` | p.84 derived: EBCDIC codes of f,o,b,a,r | brexx:c2x.rexx:3 |  | te (EBCDIC only) |
| 4 | `c2x('')` | `''` | p.84 derived: 'data ... can be of any length'; p.77 'a null string can be supplied' (null result inferred) | brexx:c2x.rexx:4 |  |  |
| 5 | `c2x('0123456789abcdef'x)` | `'0123456789ABCDEF'` | p.84 derived: upper-case digits as in the examples (inferred) | brexx:c2x.rexx:6 |  |  |
| 6 | `c2x('ffff'x)` | `'FFFF'` | p.84 derived (upper case, inferred) | brexx:c2x.rexx:7 |  |  |
| 7 | `c2x('ffffffff'x)` | `'FFFFFFFF'` | p.84 derived (upper case, inferred) | brexx:c2x.rexx:8 |  |  |

## C2XODD (p.84) -- member `C2XODD`

1 cases: 0 manual examples, 1 derived. BREXX source: `c2x.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `c2x('101'x)` | `'0101'` | p.9 derived: leading 0 added to the hex string | brexx:c2x.rexx:5 |  |  |

## D2C (p.88) -- member `D2C`

21 cases: 9 manual examples, 12 derived. BREXX source: `d2c.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `d2c(9)` | `'09'x` | p.88 example | spec; brexx:d2c.rexx:1 |  |  |
| 2 | `d2c(129)` | `'81'x` | p.88 example | spec; brexx:d2c.rexx:2,12 |  |  |
| 3 | `d2c(129,1)` | `'81'x` | p.88 example | spec; brexx:d2c.rexx:3,23 |  |  |
| 4 | `d2c(129,2)` | `'0081'x` | p.88 example | spec; brexx:d2c.rexx:4 |  |  |
| 5 | `d2c(257,1)` | `'01'x` | p.88 example | spec; brexx:d2c.rexx:5 |  |  |
| 6 | `d2c(-127,1)` | `'81'x` | p.88 example | spec; brexx:d2c.rexx:6,15 |  |  |
| 7 | `d2c(-127,2)` | `'FF81'x` | p.88 example | spec; brexx:d2c.rexx:7,19 |  |  |
| 8 | `d2c(-1,4)` | `'FFFFFFFF'x` | p.88 example | spec; brexx:d2c.rexx:8 |  |  |
| 9 | `d2c(12,0)` | `''` | p.88 example | spec; brexx:d2c.rexx:9 |  |  |
| 10 | `d2c(127)` | `'7F'x` | p.88 derived: 'If n is not specified ... there are no leading 00x characters' | brexx:d2c.rexx:10 |  |  |
| 11 | `d2c(128)` | `'80'x` | p.88 derived: 'If n is not specified ... there are no leading 00x characters' | brexx:d2c.rexx:11 |  |  |
| 12 | `d2c(1)` | `'01'x` | p.88 derived: 'If n is not specified ... there are no leading 00x characters' | brexx:d2c.rexx:13 |  |  |
| 13 | `d2c(-1,1)` | `'FF'x` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, then the result will be truncated on the left' | brexx:d2c.rexx:14 |  |  |
| 14 | `d2c(-128,1)` | `'80'x` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, then the result will be truncated on the left' | brexx:d2c.rexx:16 |  |  |
| 15 | `d2c(-129,1)` | `'7F'x` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, then the result will be truncated on the left' (FF7F truncated) | brexx:d2c.rexx:17 |  |  |
| 16 | `d2c(-1,2)` | `'FFFF'x` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, then the result will be truncated on the left' | brexx:d2c.rexx:18 |  |  |
| 17 | `d2c(-128,2)` | `'FF80'x` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, then the result will be truncated on the left' | brexx:d2c.rexx:20 |  |  |
| 18 | `d2c(-129,2)` | `'FF7F'x` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, then the result will be truncated on the left' | brexx:d2c.rexx:21 |  |  |
| 19 | `d2c(129,0)` | `''` | p.88 derived from the d2c(12,0) example | brexx:d2c.rexx:22 |  |  |
| 20 | `d2c(256+129,2)` | `'0181'x` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, then the result will be truncated on the left' | brexx:d2c.rexx:24 |  |  |
| 21 | `d2c(256*256+256+129,3)` | `'010181'x` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, then the result will be truncated on the left' | brexx:d2c.rexx:25 |  |  |

ERROR-CASEs (specified outcome is an error; not in the exec, waiting for SIGNAL ON SYNTAX):

| expression | status | spec |
|---|---|---|
| `d2c(-127)` | ERROR-CASE | p.88: 'If n is not specified, wholenumber must be a nonnegative number or an error will result' |
| `d2c(1.5)` | ERROR-CASE | p.88 syntax names a wholenumber; error inferred |

Dropped:

| case | reason |
|---|---|
| d2c(0) (not in BREXX, considered) | not settled: 'no leading 00x characters' read literally gives '', the binary representation of 0 gives '00'x |

## D2X (p.88-89) -- member `D2X`

22 cases: 9 manual examples, 13 derived. BREXX source: `d2x.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `d2x(9)` | `'9'` | p.89 example | spec; brexx:d2x.rexx:1 |  |  |
| 2 | `d2x(129)` | `'81'` | p.89 example | spec; brexx:d2x.rexx:2,13 |  |  |
| 3 | `d2x(129,1)` | `'1'` | p.89 example | spec; brexx:d2x.rexx:3 |  |  |
| 4 | `d2x(129,2)` | `'81'` | p.89 example | spec; brexx:d2x.rexx:4,24 |  |  |
| 5 | `d2x(129,4)` | `'0081'` | p.89 example | spec; brexx:d2x.rexx:5 |  |  |
| 6 | `d2x(257,2)` | `'01'` | p.89 example | spec; brexx:d2x.rexx:6 |  |  |
| 7 | `d2x(-127,2)` | `'81'` | p.89 example | spec; brexx:d2x.rexx:7,16 |  |  |
| 8 | `d2x(-127,4)` | `'FF81'` | p.89 example | spec; brexx:d2x.rexx:8 |  |  |
| 9 | `d2x(12,0)` | `''` | p.89 example | spec; brexx:d2x.rexx:9 |  |  |
| 10 | `d2x(127)` | `'7F'` | p.88 derived: 'If n is not specified ... there are no leading 0 characters' | brexx:d2x.rexx:11 |  |  |
| 11 | `d2x(128)` | `'80'` | p.88 derived: 'If n is not specified ... there are no leading 0 characters' | brexx:d2x.rexx:12 |  |  |
| 12 | `d2x(1)` | `'1'` | p.88 derived: 'If n is not specified ... there are no leading 0 characters' | brexx:d2x.rexx:14 |  |  |
| 13 | `d2x(-1,2)` | `'FF'` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, it will be truncated on the left' | brexx:d2x.rexx:15 |  |  |
| 14 | `d2x(-128,2)` | `'80'` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, it will be truncated on the left' | brexx:d2x.rexx:17 |  |  |
| 15 | `d2x(-129,2)` | `'7F'` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, it will be truncated on the left' | brexx:d2x.rexx:18 |  |  |
| 16 | `d2x(-1,3)` | `'FFF'` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, it will be truncated on the left' | brexx:d2x.rexx:19 |  |  |
| 17 | `d2x(-127,3)` | `'F81'` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, it will be truncated on the left' | brexx:d2x.rexx:20 |  |  |
| 18 | `d2x(-128,4)` | `'FF80'` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, it will be truncated on the left' | brexx:d2x.rexx:21 |  |  |
| 19 | `d2x(-129,5)` | `'FFF7F'` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, it will be truncated on the left' | brexx:d2x.rexx:22 |  |  |
| 20 | `d2x(129,0)` | `''` | p.89 derived from the d2x(12,0) example | brexx:d2x.rexx:23 |  |  |
| 21 | `d2x(256+129,4)` | `'0181'` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, it will be truncated on the left' | brexx:d2x.rexx:25 |  |  |
| 22 | `d2x(256*256+256+129,6)` | `'010181'` | p.88 derived: 'If n is specified ... the input string will be sign-extended to the required length. If the number is too big to fit into n characters, it will be truncated on the left' | brexx:d2x.rexx:26 |  |  |

ERROR-CASEs (specified outcome is an error; not in the exec, waiting for SIGNAL ON SYNTAX):

| expression | status | spec |
|---|---|---|
| `d2x(-1)` | ERROR-CASE | p.88: 'If n is not specified, wholenumber must be a nonnegative number or an error will result' |
| `d2x(1.5)` | ERROR-CASE | p.88 syntax names a wholenumber; error inferred |

Dropped:

| case | reason |
|---|---|
| brexx:d2x.rexx:10 `d2x(0)` -> '0' | not settled: 'no leading 0 characters' read literally gives '', the hexadecimal representation of 0 gives '0' |

## DATATYPE (p.84-85) -- member `DATATYPE`

88 cases: 12 manual examples, 76 derived. BREXX source: `datatyp.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `datatype(' 12 ')` | `'NUM'` | p.85 example | spec; brexx:datatyp.rexx:1 |  |  |
| 2 | `datatype('')` | `'CHAR'` | p.85 example | spec; brexx:datatyp.rexx:2,16 |  |  |
| 3 | `datatype('123*')` | `'CHAR'` | p.85 example | spec; brexx:datatyp.rexx:3 |  |  |
| 4 | `datatype('12.3','N')` | `'1'` | p.85 example | spec; brexx:datatyp.rexx:4 |  |  |
| 5 | `datatype('12.3','W')` | `'0'` | p.85 example | spec; brexx:datatyp.rexx:5,46 |  |  |
| 6 | `datatype('Fred','M')` | `'1'` | p.85 example | spec; brexx:datatyp.rexx:6 |  |  |
| 7 | `datatype('','M')` | `'0'` | p.85 example | spec; brexx:datatyp.rexx:7,33 |  |  |
| 8 | `datatype('Fred','L')` | `'0'` | p.85 example | spec |  |  |
| 9 | `datatype('?20K','S')` | `'1'` | p.85 example | spec |  | text extract mangles it; read on the page image |
| 10 | `datatype('BCd3','X')` | `'1'` | p.85 example | spec; brexx:datatyp.rexx:10 |  |  |
| 11 | `datatype('BC d3','X')` | `'1'` | p.85 example | spec; brexx:datatyp.rexx:11 |  |  |
| 12 | `datatype(1,)` | `'NUM'` | p.77 example (general notes) | spec |  |  |
| 13 | `datatype('Minx','L')` | `'0'` | p.85 derived: L 'only characters from the range a-z' | brexx:datatyp.rexx:8 |  |  |
| 14 | `datatype('3d?','s')` | `'1'` | p.85 derived: S 'only characters that are valid in REXX symbols' (p.10); p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' | brexx:datatyp.rexx:9 |  |  |
| 15 | `datatype('foobar')` | `'CHAR'` | p.84 derived: 'NUM if string is a valid REXX number, otherwise CHAR' | brexx:datatyp.rexx:12 |  |  |
| 16 | `datatype('foo bar')` | `'CHAR'` | p.84 derived (as above) | brexx:datatyp.rexx:13 |  |  |
| 17 | `datatype('123.456.789')` | `'CHAR'` | p.140 number definition: one optional decimal point | brexx:datatyp.rexx:14 |  |  |
| 18 | `datatype('123.456')` | `'NUM'` | p.140 number definition | brexx:datatyp.rexx:15 |  |  |
| 19 | `datatype('DeadBeef','A')` | `'1'` | p.84 derived: A 'only characters from the ranges a-z, A-Z, and 0-9' | brexx:datatyp.rexx:17 |  |  |
| 20 | `datatype('Dead Beef','A')` | `'0'` | p.84 derived (blank not in a-z, A-Z, 0-9) | brexx:datatyp.rexx:18 |  |  |
| 21 | `datatype('1234ABCD','A')` | `'1'` | p.84 derived (A rule) | brexx:datatyp.rexx:19 |  |  |
| 22 | `datatype('','A')` | `'0'` | p.84 derived: 'If string is null, 0 is returned (except when type is X, which returns 1)' | brexx:datatyp.rexx:20 |  |  |
| 23 | `datatype('foobar','B')` | `'0'` | p.84 derived: B 'only the characters 0 and/or 1' | brexx:datatyp.rexx:21 |  |  |
| 24 | `datatype('01001101','B')` | `'1'` | p.84 derived (B rule) | brexx:datatyp.rexx:22 |  |  |
| 25 | `datatype('0110 1101','B')` | `'0'` | p.84 derived: B 'only the characters 0 and/or 1' -- no blanks allowed in 1988 | brexx:datatyp.rexx:23 | 1 | later manuals allow blanks between groups of four; the 1988 text does not |
| 26 | `datatype('','B')` | `'0'` | p.84 derived: 'If string is null, 0 is returned (except when type is X, which returns 1)' | brexx:datatyp.rexx:24 | 1 |  |
| 27 | `datatype('foobar','L')` | `'1'` | p.85 derived (L rule) | brexx:datatyp.rexx:25 |  |  |
| 28 | `datatype('FooBar','L')` | `'0'` | p.85 derived (L rule) | brexx:datatyp.rexx:26 |  |  |
| 29 | `datatype('foo bar','L')` | `'0'` | p.85 derived (L rule, blank) | brexx:datatyp.rexx:27 |  |  |
| 30 | `datatype('','L')` | `'0'` | p.84 derived: 'If string is null, 0 is returned (except when type is X, which returns 1)' | brexx:datatyp.rexx:28 |  |  |
| 31 | `datatype('foobar','M')` | `'1'` | p.85 derived: M 'only characters from the ranges a-z and A-Z' | brexx:datatyp.rexx:29 |  |  |
| 32 | `datatype('FooBar','M')` | `'1'` | p.85 derived (M rule) | brexx:datatyp.rexx:30 |  |  |
| 33 | `datatype('foo bar','M')` | `'0'` | p.85 derived (M rule, blank) | brexx:datatyp.rexx:31 |  |  |
| 34 | `datatype('FOOBAR','M')` | `'1'` | p.85 derived (M rule) | brexx:datatyp.rexx:32 |  |  |
| 35 | `datatype('foo bar','N')` | `'0'` | p.85 derived: N 'a valid REXX number' | brexx:datatyp.rexx:34 |  |  |
| 36 | `datatype('1324.1234','N')` | `'1'` | p.85 derived (N rule) | brexx:datatyp.rexx:35 |  |  |
| 37 | `datatype('123.456.789','N')` | `'0'` | p.85 derived; p.140 number definition | brexx:datatyp.rexx:36 |  |  |
| 38 | `datatype('','N')` | `'0'` | p.84 derived: 'If string is null, 0 is returned (except when type is X, which returns 1)' | brexx:datatyp.rexx:37 |  |  |
| 39 | `datatype('foo bar','S')` | `'0'` | p.85 derived: blank is not a symbol character (p.10) | brexx:datatyp.rexx:38 |  |  |
| 40 | `datatype('','S')` | `'0'` | p.84 derived: 'If string is null, 0 is returned (except when type is X, which returns 1)' | brexx:datatyp.rexx:39 |  |  |
| 41 | `datatype('foo bar','U')` | `'0'` | p.85 derived: U 'only characters from the range A-Z' | brexx:datatyp.rexx:40 |  |  |
| 42 | `datatype('Foo Bar','U')` | `'0'` | p.85 derived (U rule) | brexx:datatyp.rexx:41 |  |  |
| 43 | `datatype('FOOBAR','U')` | `'1'` | p.85 derived (U rule) | brexx:datatyp.rexx:42 |  |  |
| 44 | `datatype('','U')` | `'0'` | p.84 derived: 'If string is null, 0 is returned (except when type is X, which returns 1)' | brexx:datatyp.rexx:43 |  |  |
| 45 | `datatype('Foobar','W')` | `'0'` | p.85 derived: W 'a REXX whole number under the current setting of NUMERIC DIGITS' | brexx:datatyp.rexx:44 |  |  |
| 46 | `datatype('123','W')` | `'1'` | p.85 derived (W rule) | brexx:datatyp.rexx:45 |  |  |
| 47 | `datatype('','W')` | `'0'` | p.84 derived: 'If string is null, 0 is returned (except when type is X, which returns 1)' | brexx:datatyp.rexx:47 |  |  |
| 48 | `datatype('123.123','W')` | `'0'` | p.147 derived: whole number 'has a decimal part which is all zeros' | brexx:datatyp.rexx:48 |  |  |
| 49 | `datatype('123.123E3','W')` | `'1'` | p.147 derived: 123123 | brexx:datatyp.rexx:49 |  |  |
| 50 | `datatype('123.0000005','W')` | `'0'` | p.147 derived: not whole, and rounded to 9 digits (123.000001) not whole either | brexx:datatyp.rexx:50,84 |  |  |
| 51 | `datatype('123.0000006','W')` | `'0'` | p.147 derived: as above (both readings give 0) | brexx:datatyp.rexx:51 |  |  |
| 52 | `datatype(' 23','W')` | `'1'` | p.140 derived: leading/trailing blanks allowed in a number | brexx:datatyp.rexx:52 |  |  |
| 53 | `datatype(' 23 ','W')` | `'1'` | p.140 derived (as above) | brexx:datatyp.rexx:53 |  |  |
| 54 | `datatype('23 ','W')` | `'1'` | p.140 derived (as above) | brexx:datatyp.rexx:54 |  |  |
| 55 | `datatype('123.00','W')` | `'1'` | p.147 derived: decimal part all zeros | brexx:datatyp.rexx:55 |  |  |
| 56 | `datatype('123000E-2','W')` | `'1'` | p.147 derived: 1230.00 | brexx:datatyp.rexx:56 |  |  |
| 57 | `datatype('123000E+2','W')` | `'1'` | p.147 derived: 12300000, 8 digits | brexx:datatyp.rexx:57 |  |  |
| 58 | `datatype('Foobar','X')` | `'0'` | p.85 derived: X 'only characters from the ranges a-f, A-F, 0-9, and blank' | brexx:datatyp.rexx:58 |  |  |
| 59 | `datatype('DeadBeef','X')` | `'1'` | p.85 derived (X rule) | brexx:datatyp.rexx:59 |  |  |
| 60 | `datatype('A B C','X')` | `'0'` | p.85 derived: 'so long as blanks only appear between pairs of hexadecimal characters' | brexx:datatyp.rexx:60 |  |  |
| 61 | `datatype('123ABC','X')` | `'1'` | p.85 derived (X rule) | brexx:datatyp.rexx:62 |  |  |
| 62 | `datatype('123AHC','X')` | `'0'` | p.85 derived (H not hex) | brexx:datatyp.rexx:63 |  |  |
| 63 | `datatype('','X')` | `'1'` | p.85 derived: 'Also returns 1 if string is a null string' | brexx:datatyp.rexx:64 |  |  |
| 64 | `datatype('0.000E-2','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:65 |  |  |
| 65 | `datatype('0.000E-1','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:66 |  |  |
| 66 | `datatype('0.000E0','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:67 |  |  |
| 67 | `datatype('0.000E1','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:68 |  |  |
| 68 | `datatype('0.000E2','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:69 |  |  |
| 69 | `datatype('0.000E3','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:70 |  |  |
| 70 | `datatype('0.000E4','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:71 |  |  |
| 71 | `datatype('0.000E5','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:72 |  |  |
| 72 | `datatype('0.000E6','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:73 |  |  |
| 73 | `datatype('0E-1','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:74 |  |  |
| 74 | `datatype('0E0','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:75 |  |  |
| 75 | `datatype('0E1','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:76 |  |  |
| 76 | `datatype('0E2','w')` | `'1'` | p.147 derived: value 0 is a whole number; p.77: 'If a function has a suboption selected by the first character of a string, that character can be in upper- or lowercase' (w) | brexx:datatyp.rexx:77 |  |  |
| 77 | `datatype('??@##_Foo$Bar!!!','S')` | `'1'` | p.85 derived: ?, @, #, _, $, ! and letters are symbol characters (p.10) | brexx:datatyp.rexx:78 |  |  |
| 78 | `datatype(' + 0.003 ')` | `'NUM'` | p.139 derived: listed as a valid number | new |  |  |
| 79 | `datatype('17.')` | `'NUM'` | p.139 derived: listed as a valid number | new |  |  |
| 80 | `datatype('.5')` | `'NUM'` | p.139 derived: listed as a valid number | new |  |  |
| 81 | `datatype('4E9')` | `'NUM'` | p.139 derived: listed as a valid number | new |  |  |
| 82 | `datatype('0.73e-7')` | `'NUM'` | p.139 derived: listed as a valid number | new |  |  |
| 83 | `datatype('.')` | `'CHAR'` | p.140 derived: 'a single period alone is not a valid number' | new |  |  |
| 84 | `datatype('12','Number')` | `'1'` | p.84 derived: 'all letters following the significant letter are ignored' | new |  |  |
| 85 | `datatype('Fred','Uppercase')` | `'0'` | p.84 derived (as above) | new |  |  |
| 86 | `datatype('1E10','W')` | `'0'` | p.147 derived: 'it must be possible to express its integer part simply as digits within the precision set by NUMERIC DIGITS' (11 digits > 9) | new |  |  |
| 87 | `datatype(' F7','X')` | `'0'` | p.85 derived: blanks 'only ... between pairs of hexadecimal characters' (leading blank) | new |  |  |
| 88 | `datatype('F7 ','X')` | `'0'` | p.85 derived (trailing blank) | new |  |  |

Dropped:

| case | reason |
|---|---|
| brexx:datatyp.rexx:61 `datatype('A BC DF','X')` -> 1 | ambiguous: DATATYPE says blanks 'only appear between pairs of hexadecimal characters' (the leading 'A' is no pair -> 0), while hex strings (p.9-10, example "1 d8"x) and X2C/X2D (p.108-109) pad a leading 0 and allow blanks at byte boundaries (-> 1) |
| brexx:datatyp.rexx:79-81 `datatype(x,'T')` | BREXX extension, type T does not exist in SC28-1883-0 |
| brexx:datatyp.rexx:82-83 `datatype('123.0000003','W')` / `'123.0000004'` under NUMERIC DIGITS 9 (commented out in BREXX, #194) | not settled: the result depends on whether DATATYPE rounds to NUMERIC DIGITS before the whole-number test (rounded: 123.000000 -> 1; unrounded -> 0). p.85 says 'under the current setting of NUMERIC DIGITS' and p.147 says numbers used directly are rounded, but DATATYPE is not in p.147's list |
| type C, D (p.85) | DBCS types, out of scope for this suite |
| S with the cent sign (p.10 symbol character) | the source must stay plain ASCII |

## X2C (p.108-109) -- member `X2C`

9 cases: 3 manual examples, 6 derived. BREXX source: `x2c.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `x2c('F7F2 A2')` | `'72s'` | p.109 example (EBCDIC) | spec; brexx:x2c.rexx:9 |  | te (EBCDIC only) |
| 2 | `x2c('F7f2a2')` | `'72s'` | p.109 example (EBCDIC) | spec; brexx:x2c.rexx:10 |  | te (EBCDIC only) |
| 3 | `x2c('F')` | `'0F'x` | p.109 example | spec; brexx:x2c.rexx:1 |  |  |
| 4 | `x2c('C18283')` | `'Abc'` | p.108 derived: EBCDIC codes of A, b, c | brexx:x2c.rexx:2 |  | te (EBCDIC only) |
| 5 | `x2c('DeadBeef')` | `'DEADBEEF'x` | p.108 derived (mixed case digits, as in F7f2a2) | brexx:x2c.rexx:3 |  |  |
| 6 | `x2c('1 02 03')` | `'010203'x` | p.108-109 derived: 'padded with a leading 0 to make an even number' + 'Blanks can optionally be added (at byte boundaries only, not leading or trailing)' | brexx:x2c.rexx:4,8 |  | odd first group: settled by the hex-string rule and example "1 d8"x (p.9-10); see DATATYPE drop for the contrary DATATYPE wording |
| 7 | `x2c('11 0222 3333 044444')` | `'1102223333044444'x` | p.109 derived: 'Blanks can optionally be added (at byte boundaries only, not leading or trailing)' | brexx:x2c.rexx:5 |  |  |
| 8 | `x2c('')` | `''` | p.108 derived (null result inferred; p.77 'a null string can be supplied') | brexx:x2c.rexx:6 |  |  |
| 9 | `x2c('2')` | `'02'x` | p.108 derived: 'padded with a leading 0' | brexx:x2c.rexx:7 |  |  |

ERROR-CASEs (specified outcome is an error; not in the exec, waiting for SIGNAL ON SYNTAX):

| expression | status | spec |
|---|---|---|
| `x2c('0G')` | ERROR-CASE | p.108: hexstring is 'a string of hexadecimal characters'; error inferred |
| `x2c(' F7')` | ERROR-CASE | p.109: blanks 'not leading or trailing'; error inferred |
| `x2c('F7 ')` | ERROR-CASE | p.109: as above; error inferred |
| `x2c('F7F 2')` | ERROR-CASE | p.109: blanks 'at byte boundaries only'; error inferred |

## X2D (p.109) -- member `X2D`

71 cases: 12 manual examples, 59 derived. BREXX source: `x2d.rexx`.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `x2d('0E')` | `'14'` | p.109 example | spec; brexx:x2d.rexx:1 |  |  |
| 2 | `x2d('81')` | `'129'` | p.109 example | spec; brexx:x2d.rexx:2 |  |  |
| 3 | `x2d('F81')` | `'3969'` | p.109 example | spec; brexx:x2d.rexx:3 |  |  |
| 4 | `x2d('FF81')` | `'65409'` | p.109 example | spec; brexx:x2d.rexx:4 |  |  |
| 5 | `x2d('81',2)` | `'-127'` | p.109 example | spec; brexx:x2d.rexx:6 |  |  |
| 6 | `x2d('81',4)` | `'129'` | p.109 example | spec; brexx:x2d.rexx:7 |  |  |
| 7 | `x2d('F081',4)` | `'-3967'` | p.109 example | spec; brexx:x2d.rexx:8 |  |  |
| 8 | `x2d('F081',3)` | `'129'` | p.109 example | spec; brexx:x2d.rexx:9 |  |  |
| 9 | `x2d('F081',2)` | `'-127'` | p.109 example | spec; brexx:x2d.rexx:10 |  |  |
| 10 | `x2d('F081',1)` | `'1'` | p.109 example | spec; brexx:x2d.rexx:11 |  |  |
| 11 | `x2d('0031',0)` | `'0'` | p.109 example | spec; brexx:x2d.rexx:80 |  | disabled in BREXX (#192), manual example |
| 12 | `x2d('F0')` | `'240'` | p.109 derived: 'If n is not specified, hexstring is processed as an unsigned binary number' | brexx:x2d.rexx:5 |  |  |
| 13 | `x2d('ff80',2)` | `'-128'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:12,40 |  |  |
| 14 | `x2d('ff80',1)` | `'0'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:13,39 |  |  |
| 15 | `x2d('ff 80',1)` | `'0'` | p.109 derived: 'Blanks can optionally be added (at byte boundaries only, not leading or trailing)'; they are ignored | brexx:x2d.rexx:14 |  |  |
| 16 | `x2d('')` | `'0'` | p.109 derived: 'If hexstring is the null string, then 0 is returned' | brexx:x2d.rexx:15 |  |  |
| 17 | `x2d('101')` | `'257'` | p.109 derived: 'If n is not specified, hexstring is processed as an unsigned binary number' | brexx:x2d.rexx:16 |  |  |
| 18 | `x2d('ff')` | `'255'` | p.109 derived: 'If n is not specified, hexstring is processed as an unsigned binary number' | brexx:x2d.rexx:17 |  |  |
| 19 | `x2d('ffff')` | `'65535'` | p.109 derived: 'If n is not specified, hexstring is processed as an unsigned binary number' | brexx:x2d.rexx:18 |  |  |
| 20 | `x2d('ffff',2)` | `'-1'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:19,24 |  |  |
| 21 | `x2d('ffff',1)` | `'-1'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:20 |  |  |
| 22 | `x2d('fffe',2)` | `'-2'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:21,26 |  |  |
| 23 | `x2d('fffe',1)` | `'-2'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:22 |  |  |
| 24 | `x2d('ffff',4)` | `'-1'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:23 |  |  |
| 25 | `x2d('fffe',4)` | `'-2'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:25 |  |  |
| 26 | `x2d('ffff',3)` | `'-1'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:27 |  |  |
| 27 | `x2d('0fff')` | `'4095'` | p.109 derived: 'If n is not specified, hexstring is processed as an unsigned binary number' | brexx:x2d.rexx:28 |  |  |
| 28 | `x2d('0fff',4)` | `'4095'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:29 |  |  |
| 29 | `x2d('0fff',3)` | `'-1'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:30 |  |  |
| 30 | `x2d('07ff')` | `'2047'` | p.109 derived: 'If n is not specified, hexstring is processed as an unsigned binary number' | brexx:x2d.rexx:31 |  |  |
| 31 | `x2d('07ff',4)` | `'2047'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:32 |  |  |
| 32 | `x2d('07ff',3)` | `'2047'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:33 |  |  |
| 33 | `x2d('ff7f',1)` | `'-1'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:34 |  |  |
| 34 | `x2d('ff7f',2)` | `'127'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:35 |  |  |
| 35 | `x2d('ff7f',3)` | `'-129'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:36 |  |  |
| 36 | `x2d('ff7f',4)` | `'-129'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:37 |  |  |
| 37 | `x2d('ff7f',5)` | `'65407'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:38 |  |  |
| 38 | `x2d('ff80',3)` | `'-128'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:41 |  |  |
| 39 | `x2d('ff80',4)` | `'-128'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:42 |  |  |
| 40 | `x2d('ff80',5)` | `'65408'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:43 |  |  |
| 41 | `x2d('ff81',1)` | `'1'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:44 |  |  |
| 42 | `x2d('ff81',2)` | `'-127'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:45 |  |  |
| 43 | `x2d('ff81',3)` | `'-127'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:46 |  |  |
| 44 | `x2d('ff81',4)` | `'-127'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:47 |  |  |
| 45 | `x2d('ff81',5)` | `'65409'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:48 |  |  |
| 46 | `x2d('ffffffffffff',12)` | `'-1'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:49 |  |  |
| 47 | `x2d('a')` | `'10'` | p.109 derived: 'If n is not specified, hexstring is processed as an unsigned binary number' | brexx:x2d.rexx:54 |  |  |
| 48 | `x2d('0f')` | `'15'` | p.109 derived: 'If n is not specified, hexstring is processed as an unsigned binary number' | brexx:x2d.rexx:55 |  |  |
| 49 | `x2d('80')` | `'128'` | p.109 derived: 'If n is not specified, hexstring is processed as an unsigned binary number' | brexx:x2d.rexx:56 |  |  |
| 50 | `x2d('765')` | `'1893'` | p.109 derived: 'If n is not specified, hexstring is processed as an unsigned binary number' | brexx:x2d.rexx:57 |  |  |
| 51 | `x2d(01234)` | `'4660'` | p.10 derived: a constant symbol's value is its characters ('01234') | brexx:x2d.rexx:58 |  |  |
| 52 | `x2d(1E2)` | `'482'` | p.10 derived: constant symbol, value '1E2' (no arithmetic) | brexx:x2d.rexx:59 |  |  |
| 53 | `x2d(baba)` | `'47802'` | p.10 derived: unassigned symbol, value 'BABA' | brexx:x2d.rexx:60 |  |  |
| 54 | `x2d('',0)` | `'0'` | p.109 derived: null string -> 0; 'If n is 0, 0 is always returned' | brexx:x2d.rexx:61 |  |  |
| 55 | `x2d('',12)` | `'0'` | p.109 derived: 'If hexstring is the null string, then 0 is returned' | brexx:x2d.rexx:62 |  |  |
| 56 | `x2d('abc',0)` | `'0'` | p.109 derived: 'If n is 0, 0 is always returned' | brexx:x2d.rexx:63 |  | commented out in BREXX (#192) |
| 57 | `x2d('abc',1)` | `'-4'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:64 |  |  |
| 58 | `x2d('abc',3)` | `'-1348'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:65 |  |  |
| 59 | `x2d('abc',5)` | `'2748'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:66 |  |  |
| 60 | `x2d('abc',12345)` | `'2748'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' | brexx:x2d.rexx:67 |  |  |
| 61 | `x2d(1+3,1+3)` | `'4'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' (arguments 4, 4) | brexx:x2d.rexx:68 |  |  |
| 62 | `x2d(256+12,10+2)` | `'616'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' (arguments 268, 12) | brexx:x2d.rexx:69 |  |  |
| 63 | `d2x(x2d('12345'))` | `'12345'` | p.88/109 derived: round trip (74565) | brexx:x2d.rexx:71 |  |  |
| 64 | `x2d(copies(0,249)\|\|1)` | `'1'` | p.109 derived: implementation maximum -- 'Leading sign characters (0 and F) do not count' | brexx:x2d.rexx:75 |  |  |
| 65 | `x2d(1 + 1E+2)` | `'257'` | p.139 derived: argument 101 | brexx:x2d.rexx:83 |  |  |
| 66 | `x2d('eeeeeeeeeeeeeeeeeeeeeeeee') under NUMERIC DIGITS 31` | `'1183140560213014108063589658350'` | p.109 derived: 'the result must have no more than NUMERIC DIGITS digits' (31 digits; setup: NUMERIC DIGITS 31) | brexx:x2d.rexx:79 |  | commented out in BREXX (#192: wraps at 32 bits) |
| 67 | `x2d('c6 f0'X)` | `'240'` | p.109 example (EBCDIC: the literal is the characters F0) | spec; brexx:x2d.rexx:84 |  | te (EBCDIC only) |
| 68 | `x2d(''X)` | `'0'` | p.9 derived: ''X is the null string; p.109 null -> 0 | brexx:x2d.rexx:53 |  |  |
| 69 | `x2d('12',987654321)` | `'18'` | p.109 derived: 'If n is specified, the given sequence of hexadecimal digits is padded on the left with zeros (not sign-extended), or truncated on the left to n characters ... signed binary number' (n = 987654321) | brexx:x2d.rexx:70 |  | last: a naive implementation pads 987654321 zeros and may die |
| 70 | `x2d(+1E+2)` | `'256'` | p.10/139 derived: prefix + is arithmetic, argument 100 | brexx:x2d.rexx:81 |  | placed last: dies on the host today, see report |
| 71 | `x2d(+.1E2)` | `'16'` | p.10/139 derived: argument 10 | brexx:x2d.rexx:82 |  |  |

ERROR-CASEs (specified outcome is an error; not in the exec, waiting for SIGNAL ON SYNTAX):

| expression | status | spec |
|---|---|---|
| `x2d('eeeeeeeeeeeeeeeeeeeeeeeee') under NUMERIC DIGITS 9` | ERROR-CASE | p.109: 'If the result cannot be expressed as a whole number, an error results' (31 digits) |
| `x2d('FFFFFFFFFF') under NUMERIC DIGITS 9` | ERROR-CASE | p.109: as above (13 digits) |
| `x2d('0G')` | ERROR-CASE | not a hex string; error inferred |

Dropped:

| case | reason |
|---|---|
| brexx:x2d.rexx:50-52 `X2D((1&1\|0=22*33))`, `X2D(ABS(...))`, `X2D(RIGHT(LEFT(REVERSE(321),2),...))` | the expected value rests on operator precedence and other functions, not X2D; belongs to the operators/strings groups |
| brexx:x2d.rexx:72,76 `X2D((00000000000000001+1-0.000000))`, `X2D(ABS(...))` -> 2 | operators group. Note: by p.139 (trailing zeros preserved) the argument is '2.000000', which is not a hex string, so the manual implies an error, not BREXX's 2 |
| brexx:x2d.rexx:73-74 `X2D((99/3+10*126-(33\|\|2)//5-1099))` -> 402 | operators group (argument 192) |
| brexx:x2d.rexx:77 `X2D(ABS(COPIES(0,249)\|\|1))` | tests ABS on a 250-digit number; commented out in BREXX |
| brexx:x2d.rexx:78 `X2D(ABS(RIGHT(...)))` | operators/strings group |

## Dropped BREXX files

| file | reason |
|---|---|
| `b2x.rexx` | B2X is not in SC28-1883-0 (checked: function list of the index, printed p.432, goes BITAND ... X2C, X2D with no B2X; brief lists it as absent) |
| `x2b.rexx` | X2B is not in SC28-1883-0 (same check, printed p.432) |
