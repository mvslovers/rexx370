# Spec tests: strings group (SC28-1883-0)

Conformance cases for the string built-in functions of chapter 4 of
SC28-1883-0 (TSO/E Version 2 REXX Reference, December 1988). Members live
in `test/spec/`. The manual is the only authority for expected values;
BREXX expectations (brexx370 `test/*.rexx`, CMS-370-BREXX, Unlicense)
were a starting point and are listed where they differ.

Columns: `spec` gives the printed page and whether the value is a manual
**example** or **derived** from a quoted rule. `source` is `spec` for a
manual example (with the matching BREXX case, if any) or
`brexx:<file>:<n>`. Expected values are REXX literals as written in the
exec. General argument rules are on p.77.

## Split members

A hex string (`'C1'x`) currently ends a rexx370 exec on the host at the
first one evaluated (measured: `run_rc=20`, no further output, both the
bytecode and the token-walk path). A hex literal in the *expected* value
is evaluated before `t`/`te` runs, so `te` does not protect it. Every case
with a hex literal therefore lives in its own member, so the hex-free
cases still run:

- `TRANSLAT` (hex-free) and `TRANSLHX` (hex strings), both TRANSLATE p.104
- `XRANGE` (hex-free) and `XRANGEHX` (hex strings), both p.108

Harness note: no line ends in `,,` (an argument separator followed by a
continuation comma passes an extra empty argument in rexx370); long
expected values are assigned to `want` on a setup line instead.

## BREXX files covered

| file | used for | dropped |
|---|---|---|
| abbrev.rexx | ABBREV |  |
| center.rexx | CENTER |  |
| compare.rexx | COMPARE |  |
| copies.rexx | COPIES |  |
| delstr.rexx | DELSTR |  |
| insert.rexx | INSERT | 12, 13 (not settled) |
| lastpos.rexx | LASTPOS, POS (case 19) | 11, 12, 16 (not settled), 13 (duplicate) |
| left.rexx | LEFT |  |
| length.rexx | LENGTH | 3 (duplicate) |
| overlay.rexx | OVERLAY | 3 (duplicate) |
| pos.rexx | POS | 10, 12 (not settled) |
| reverse.rexx | REVERSE |  |
| right.rexx | RIGHT |  |
| space.rexx | SPACE | 9, 11 (duplicates) |
| strip.rexx | STRIP |  |
| substr.rexx | SUBSTR |  |
| verify.rexx | VERIFY | 5 (not settled) |
| xrange.rexx | XRANGE, XRANGEHX |  |
| transla.rexx | TRANSLAT, TRANSLHX |  |
| blanks.rexx | - | SPACE line (not settled, see SPACE); WORDS/WORD/WORDPOS/comparison/DATATYPE/X2C/PARSE lines belong to other groups |
| changes.rexx | - | all: CHANGESTR is not in SC28-1883-0 |
| countst.rexx | - | all: COUNTSTR is not in SC28-1883-0 |
| soundex.rexx | - | all: SOUNDEX is a BREXX extension, not in SC28-1883-0 |

## ABBREV

14 cases: 6 manual examples, 8 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `abbrev('Print','Pri')` | `'1'` | p.78 example | spec; =brexx:abbrev:1 |  |  |
| 2 | `abbrev('PRINT','Pri')` | `'0'` | p.78 example | spec; =brexx:abbrev:2 |  |  |
| 3 | `abbrev('PRINT','PRI',4)` | `'0'` | p.78 example | spec; =brexx:abbrev:3 |  |  |
| 4 | `abbrev('PRINT','PRY')` | `'0'` | p.78 example | spec; =brexx:abbrev:4 |  |  |
| 5 | `abbrev('PRINT','')` | `'1'` | p.78 example | spec; =brexx:abbrev:5 |  |  |
| 6 | `abbrev('PRINT','',1)` | `'0'` | p.78 example | spec; =brexx:abbrev:6 |  |  |
| 7 | `abbrev('information','info',4)` | `'1'` | p.78 derived: "info equal to leading chars of information and length of info not less than length" | brexx:abbrev:7 |  |  |
| 8 | `abbrev('information','',0)` | `'1'` | p.78 derived: Note: "a null string will always match if a length of 0 is used" | brexx:abbrev:8 |  |  |
| 9 | `abbrev('information','Info',4)` | `'0'` | p.78 derived: comparison is exact; case differs | brexx:abbrev:9 |  |  |
| 10 | `abbrev('information','info',5)` | `'0'` | p.78 derived: length(info)=4 < length 5 | brexx:abbrev:10 |  |  |
| 11 | `abbrev('information','info ')` | `'0'` | p.78 derived: 5-char info: its trailing blank does not equal "r", the 5th char of information | brexx:abbrev:11 |  |  |
| 12 | `abbrev('information','info',3)` | `'1'` | p.78 derived: 4 >= 3 and leading chars match | brexx:abbrev:12 |  |  |
| 13 | `abbrev('info','information',3)` | `'0'` | p.78 derived: info longer than information cannot equal its leading chars | brexx:abbrev:13 |  |  |
| 14 | `abbrev('info','info',5)` | `'0'` | p.78 derived: length(info)=4 < 5 | brexx:abbrev:14 |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `abbrev('PRINT','P',-1)` | p.78: length "must be a nonnegative whole number" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (14/14).

## CENTER (CENTRE/CENTER)

24 cases: 4 manual examples, 20 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `center(abc,7)` | `'  ABC  '` | p.81 example | spec; =brexx:center:2 |  |  |
| 2 | `center(abc,8,'-')` | `'--ABC---'` | p.81 example | spec; =brexx:center:3 |  |  |
| 3 | `centre('The blue sky',8)` | `'e blue s'` | p.81 example | spec; =brexx:center:15 |  |  |
| 4 | `centre('The blue sky',7)` | `'e blue '` | p.81 example | spec; =brexx:center:16 |  |  |
| 5 | `centre(abc,7)` | `'  ABC  '` | p.81 derived: Note "can be called either CENTRE or CENTER" | brexx:center:1 |  | abc is an unassigned symbol, value ABC |
| 6 | `center('The blue sky',8)` | `'e blue s'` | p.81 derived: manual example with the CENTER spelling (Note: either name) | brexx:center:4 |  |  |
| 7 | `center('The blue sky',7)` | `'e blue '` | p.81 derived: manual example with the CENTER spelling (Note: either name) | brexx:center:5 |  |  |
| 8 | `center('****',8,'-')` | `'--****--'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:6 |  |  |
| 9 | `center('****',7,'-')` | `'-****--'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:7 |  |  |
| 10 | `center('*****',8,'-')` | `'-*****--'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:8 |  |  |
| 11 | `center('*****',7,'-')` | `'-*****-'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:9 |  |  |
| 12 | `center('12345678',4,'-')` | `'3456'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:10 |  |  |
| 13 | `center('12345678',5,'-')` | `'23456'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:11 |  |  |
| 14 | `center('1234567',4,'-')` | `'2345'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:12 |  |  |
| 15 | `center('1234567',5,'-')` | `'23456'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:13 |  |  |
| 16 | `centre(abc,8,'-')` | `'--ABC---'` | p.81 derived: manual example with the CENTRE spelling (Note: either name) | brexx:center:14 |  |  |
| 17 | `centre('****',8,'-')` | `'--****--'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:17 |  |  |
| 18 | `centre('****',7,'-')` | `'-****--'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:18 |  |  |
| 19 | `centre('*****',8,'-')` | `'-*****--'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:19 |  |  |
| 20 | `centre('*****',7,'-')` | `'-*****-'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:20 |  |  |
| 21 | `centre('12345678',4,'-')` | `'3456'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:21 |  |  |
| 22 | `centre('12345678',5,'-')` | `'23456'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:22 |  |  |
| 23 | `centre('1234567',4,'-')` | `'2345'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:23 |  |  |
| 24 | `centre('1234567',5,'-')` | `'23456'` | p.81 derived: odd count -> "right-hand end loses or gains one more character than the left-hand end" | brexx:center:24 |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `center('abc',7,'--')` | p.77: "a pad character ... must be exactly one character long" |
| `center('abc',-1)` | p.77: a length "must be a nonnegative whole number" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (24/24).

## COMPARE

11 cases: 6 manual examples, 5 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `compare('abc','abc')` | `'0'` | p.82 example | spec; =brexx:compare:1 |  |  |
| 2 | `compare('abc','ak')` | `'2'` | p.82 example | spec; =brexx:compare:2 |  |  |
| 3 | `compare('ab ','ab')` | `'0'` | p.82 example | spec; =brexx:compare:3 |  |  |
| 4 | `compare('ab ','ab',' ')` | `'0'` | p.82 example | spec; =brexx:compare:4 |  |  |
| 5 | `compare('ab ','ab','x')` | `'3'` | p.82 example | spec; =brexx:compare:5 |  |  |
| 6 | `compare('ab-- ','ab','-')` | `'5'` | p.82 example | spec; =brexx:compare:6 |  |  |
| 7 | `compare('foo','bar')` | `'1'` | p.82 derived: "position of the first character that does not match; shorter string padded on the right with pad (default blank)" | brexx:compare:7 |  |  |
| 8 | `compare('foo','foo')` | `'0'` | p.82 derived: "position of the first character that does not match; shorter string padded on the right with pad (default blank)" | brexx:compare:8 |  |  |
| 9 | `compare(' ','')` | `'0'` | p.82 derived: "position of the first character that does not match; shorter string padded on the right with pad (default blank)" | brexx:compare:9 |  |  |
| 10 | `compare('foo','f','o')` | `'0'` | p.82 derived: "position of the first character that does not match; shorter string padded on the right with pad (default blank)" | brexx:compare:10 |  |  |
| 11 | `compare('foobar','foobag')` | `'6'` | p.82 derived: "position of the first character that does not match; shorter string padded on the right with pad (default blank)" | brexx:compare:11 |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `compare('a','b','xy')` | p.77: pad must be exactly one character |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (11/11).

## COPIES

7 cases: 2 manual examples, 5 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `copies('abc',3)` | `'abcabcabc'` | p.83 example | spec; =brexx:copies:1 |  |  |
| 2 | `copies('abc',0)` | `''` | p.83 example | spec; =brexx:copies:2 |  |  |
| 3 | `copies('foo',3)` | `'foofoofoo'` | p.83 derived: "returns n concatenated copies of string" | brexx:copies:3 |  |  |
| 4 | `copies('x',10)` | `'xxxxxxxxxx'` | p.83 derived: "returns n concatenated copies of string" | brexx:copies:4 |  |  |
| 5 | `copies('',50)` | `''` | p.83 derived: "returns n concatenated copies of string" | brexx:copies:5 |  |  |
| 6 | `copies('',0)` | `''` | p.83 derived: "returns n concatenated copies of string" | brexx:copies:6 |  |  |
| 7 | `copies('foobar',0)` | `''` | p.83 derived: "returns n concatenated copies of string" | brexx:copies:7 |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `copies('abc',-1)` | p.83: n "must be a nonnegative whole number" |
| `copies('abc',1.5)` | same |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (7/7).

## DELSTR

12 cases: 3 manual examples, 9 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `delstr('abcd',3)` | `'ab'` | p.87 example | spec; =brexx:delstr:1 |  |  |
| 2 | `delstr('abcde',3,2)` | `'abe'` | p.87 example | spec; =brexx:delstr:2 |  |  |
| 3 | `delstr('abcde',6)` | `'abcde'` | p.87 example | spec; =brexx:delstr:3 |  |  |
| 4 | `delstr('Med lov skal land bygges',6)` | `'Med l'` | p.87 derived: "deletes the substring that begins at the nth character, and is of length length; if length not specified, the rest is deleted" | brexx:delstr:4 |  |  |
| 5 | `delstr('Med lov skal land bygges',6,10)` | `'Med lnd bygges'` | p.87 derived: "deletes the substring that begins at the nth character, and is of length length; if length not specified, the rest is deleted" | brexx:delstr:5 |  |  |
| 6 | `delstr('Med lov skal land bygges',1)` | `''` | p.87 derived: "deletes the substring that begins at the nth character, and is of length length; if length not specified, the rest is deleted" | brexx:delstr:6 |  |  |
| 7 | `delstr('Med lov skal',30)` | `'Med lov skal'` | p.87 derived: "if n is greater than the length of string, the string is returned unchanged" | brexx:delstr:7 |  |  |
| 8 | `delstr('Med lov skal',8,8)` | `'Med lov'` | p.87 derived: "deletes the substring that begins at the nth character, and is of length length; if length not specified, the rest is deleted" | brexx:delstr:8 |  | length reaches past the end |
| 9 | `delstr('Med lov skal',12)` | `'Med lov ska'` | p.87 derived: "deletes the substring that begins at the nth character, and is of length length; if length not specified, the rest is deleted" | brexx:delstr:9 |  | n = length(string): last char deleted |
| 10 | `delstr('Med lov skal',13)` | `'Med lov skal'` | p.87 derived: "if n is greater than the length of string, the string is returned unchanged" | brexx:delstr:10 |  |  |
| 11 | `delstr('Med lov skal',14)` | `'Med lov skal'` | p.87 derived: "if n is greater than the length of string, the string is returned unchanged" | brexx:delstr:11 |  |  |
| 12 | `delstr('',30)` | `''` | p.87 derived: "if n is greater than the length of string, the string is returned unchanged" | brexx:delstr:12 |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `delstr('abc',0)` | p.87: n "must be a positive whole number" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (12/12).

## INDEX

5 cases: 5 manual examples, 0 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `index('abcdef','cd')` | `'3'` | p.92 example | spec |  |  |
| 2 | `index('abcdef','xd')` | `'0'` | p.92 example | spec |  |  |
| 3 | `index('abcdef','bc',3)` | `'0'` | p.92 example | spec |  |  |
| 4 | `index('abcabc','bc',3)` | `'5'` | p.92 example | spec |  |  |
| 5 | `index('abcabc','bc',6)` | `'0'` | p.92 example | spec |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `index('abc','b',0)` | p.92: start "must be a positive whole number" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (5/5).

## INSERT

11 cases: 5 manual examples, 6 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `insert(' ','abcdef',3)` | `'abc def'` | p.92 example | spec; =brexx:insert:1 |  |  |
| 2 | `insert('123','abc',5,6)` | `'abc  123   '` | p.92 example | spec; =brexx:insert:2 |  |  |
| 3 | `insert('123','abc',5,6,'+')` | `'abc++123+++'` | p.92 example | spec; =brexx:insert:3 |  |  |
| 4 | `insert('123','abc')` | `'123abc'` | p.92 example | spec; =brexx:insert:4 |  |  |
| 5 | `insert('123','abc',,5,'-')` | `'123--abc'` | p.92 example | spec; =brexx:insert:5 |  |  |
| 6 | `insert('abc','def')` | `'abcdef'` | p.92 derived: "inserts new, padded to length length, after the nth character; default n is 0; if n > length(target), padding is added there also" | brexx:insert:6 |  | default length = length(new): default inferred from the manual's examples (INSERT('123','abc') -> '123abc') |
| 7 | `insert('abc','def',2)` | `'deabcf'` | p.92 derived: "inserts new, padded to length length, after the nth character; default n is 0; if n > length(target), padding is added there also" | brexx:insert:7 |  | default length = length(new): default inferred from the manual's examples (INSERT('123','abc') -> '123abc') |
| 8 | `insert('abc','def',3)` | `'defabc'` | p.92 derived: "inserts new, padded to length length, after the nth character; default n is 0; if n > length(target), padding is added there also" | brexx:insert:8 |  | default length = length(new): default inferred from the manual's examples (INSERT('123','abc') -> '123abc') |
| 9 | `insert('abc','def',5)` | `'def  abc'` | p.92 derived: "inserts new, padded to length length, after the nth character; default n is 0; if n > length(target), padding is added there also" | brexx:insert:9 |  | default length = length(new): default inferred from the manual's examples (INSERT('123','abc') -> '123abc') |
| 10 | `insert('abc','def',5,,'*')` | `'def**abc'` | p.92 derived: "inserts new, padded to length length, after the nth character; default n is 0; if n > length(target), padding is added there also" | brexx:insert:10 |  | default length = length(new): default inferred from the manual's examples (INSERT('123','abc') -> '123abc') |
| 11 | `insert('abc','def',5,4,'*')` | `'def**abc*'` | p.92 derived: "inserts new, padded to length length, after the nth character; default n is 0; if n > length(target), padding is added there also" | brexx:insert:11 |  |  |

Dropped:

| source | expression | BREXX said | reason |
|---|---|---|---|
| brexx:insert:12 | `insert('abc','def',,0)` | `'def'` | p.92 says only that new is "padded to length length"; truncation of new to a shorter length is not stated in the 1988 manual. Not settled. |
| brexx:insert:13 | `insert('abc','def',2,1)` | `'deaf'` | same: needs new truncated to length 1, which p.92 does not state. |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `insert('a','b',-1)` | p.92: n "must be a nonnegative whole number" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (11/11).

## JUSTIFY

8 cases: 4 manual examples, 4 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `justify('The blue sky',14)` | `'The  blue  sky'` | p.93 example | spec |  |  |
| 2 | `justify('The blue sky',8)` | `'The blue'` | p.93 example | spec |  |  |
| 3 | `justify('The blue sky',9)` | `'The  blue'` | p.93 example | spec |  |  |
| 4 | `justify('The blue sky',9,'+')` | `'The++blue'` | p.93 example | spec |  |  |
| 5 | `justify('The blue sky',13)` | `'The  blue sky'` | p.93 derived: "extra pad characters are added evenly from left to right" | spec rule |  | 12 -> 13: one extra pad goes to the left gap |
| 6 | `justify('  The   blue  ',10)` | `'The   blue'` | p.93 derived: string "first normalized as though SPACE(string)" | spec rule |  | normalized "The blue" (8), two extra blanks in the single gap |
| 7 | `justify('The blue sky',12,'+')` | `'The+blue+sky'` | p.93 derived: "blanks between words are replaced with the pad character" | spec rule |  | length equals normalized width, no extra pads |
| 8 | `justify('The blue sky',0)` | `''` | p.93 derived: truncated on the right to length 0 | spec rule |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `justify('a b',-1)` | p.93: "length must be nonnegative" |

Host run 2026-09-30 (bytecode VM; token-walk same): FAIL, cases 3, 4.

## LASTPOS

14 cases: 3 manual examples, 11 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `lastpos(' ','abc def ghi')` | `'8'` | p.93 example | spec; =brexx:lastpos:1 |  |  |
| 2 | `lastpos(' ','abcdefghi')` | `'0'` | p.93 example | spec; =brexx:lastpos:2 |  |  |
| 3 | `lastpos(' ','abc def ghi',7)` | `'4'` | p.93 example | spec; =brexx:lastpos:3 |  |  |
| 4 | `lastpos('b','abc abc')` | `'6'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:4 |  |  |
| 5 | `lastpos('b','abc abc',5)` | `'2'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:5 |  | 1-char needle, start 5 |
| 6 | `lastpos('b','abc abc',6)` | `'6'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:6 |  | 1-char needle at start |
| 7 | `lastpos('b','abc abc',7)` | `'6'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:7 |  |  |
| 8 | `lastpos('x','abc abc')` | `'0'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:8 |  | not found |
| 9 | `lastpos('b','abc abc',20)` | `'6'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:9 |  | start > length |
| 10 | `lastpos('b','')` | `'0'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:10 |  | null haystack: not found |
| 11 | `lastpos('bc','abc abc')` | `'6'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:14 |  |  |
| 12 | `lastpos('bc ','abc abc',20)` | `'2'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:15 |  | start > length |
| 13 | `lastpos('abc','abc abc')` | `'5'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:17 |  |  |
| 14 | `lastpos('abc','abc abc',7)` | `'5'` | p.93 derived: "position of the last occurrence"; "start ... defaults to LENGTH(string) if larger than that value" | brexx:lastpos:18 |  | start = length: match ending at start |

Dropped:

| source | expression | BREXX said | reason |
|---|---|---|---|
| brexx:lastpos:11 | `lastpos('','c')` | `0` | null needle: p.93 does not say what a null needle yields. Not settled. |
| brexx:lastpos:12 | `lastpos('','')` | `0` | null needle, as above. |
| brexx:lastpos:13 | `lastpos('b','abc abc',20)` | `6` | duplicate of brexx:lastpos:9. |
| brexx:lastpos:16 | `lastpos('abc','abc abc',6)` | `1` | multi-char needle with start inside a match: p.93 does not settle whether the needle must end at or before start (-> 1) or may begin at or before start (-> 5); the only start example has a 1-char needle. |
| brexx:lastpos:19 | `pos('abc','abcdefabccdabcd',4)` | `7` | a POS call; moved to member POS (case 16). |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `lastpos('a','abc',0)` | p.93: start "must be a positive whole number" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (14/14).

## LEFT

9 cases: 3 manual examples, 6 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `left('abc d',8)` | `'abc d   '` | p.94 example | spec; =brexx:left:1 |  |  |
| 2 | `left('abc d',8,'.')` | `'abc d...'` | p.94 example | spec; =brexx:left:2 |  |  |
| 3 | `left('abc  def',7)` | `'abc  de'` | p.94 example | spec; =brexx:left:3 |  |  |
| 4 | `left('foobar',1)` | `'f'` | p.94 derived: "padded with pad characters (or truncated) on the right" | brexx:left:4 |  |  |
| 5 | `left('foobar',0)` | `''` | p.94 derived: "padded with pad characters (or truncated) on the right" | brexx:left:5 |  |  |
| 6 | `left('foobar',6)` | `'foobar'` | p.94 derived: "padded with pad characters (or truncated) on the right" | brexx:left:6 |  |  |
| 7 | `left('foobar',8)` | `'foobar  '` | p.94 derived: "padded with pad characters (or truncated) on the right" | brexx:left:7 |  |  |
| 8 | `left('foobar',8,'*')` | `'foobar**'` | p.94 derived: "padded with pad characters (or truncated) on the right" | brexx:left:8 |  |  |
| 9 | `left('foobar',1,'*')` | `'f'` | p.94 derived: "padded with pad characters (or truncated) on the right" | brexx:left:9 |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `left('abc',-1)` | p.94: "length must be nonnegative" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (9/9).

## LENGTH

6 cases: 3 manual examples, 3 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `length('abcdefgh')` | `'8'` | p.94 example | spec; =brexx:length:1 |  |  |
| 2 | `length('abc defg')` | `'8'` | p.94 example | spec |  |  |
| 3 | `length('')` | `'0'` | p.94 example | spec; =brexx:length:2,3 |  |  |
| 4 | `length('a')` | `'1'` | p.94 derived: "returns the length of string" | brexx:length:4 |  |  |
| 5 | `length('abc')` | `'3'` | p.94 derived: "returns the length of string" | brexx:length:5 |  |  |
| 6 | `length('abcdefghij')` | `'10'` | p.94 derived: "returns the length of string" | brexx:length:6 |  |  |

Dropped:

| source | expression | BREXX said | reason |
|---|---|---|---|
| brexx:length:3 | `length('')` | `0` | duplicate of brexx:length:2 (= manual example, case 3). |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (6/6).

## OVERLAY

17 cases: 5 manual examples, 12 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `overlay(' ','abcdef',3)` | `'ab def'` | p.96 example | spec; =brexx:overlay:2 |  |  |
| 2 | `overlay('.','abcdef',3,2)` | `'ab. ef'` | p.96 example | spec; =brexx:overlay:1 |  |  |
| 3 | `overlay('qq','abcd')` | `'qqcd'` | p.96 example | spec; =brexx:overlay:4 |  |  |
| 4 | `overlay('qq','abcd',4)` | `'abcqq'` | p.96 example | spec; =brexx:overlay:5 |  |  |
| 5 | `overlay('123','abc',5,6,'+')` | `'abc+123+++'` | p.96 example | spec; =brexx:overlay:6 |  |  |
| 6 | `overlay('foo','abcdefghi',3,4,'*')` | `'abfoo*ghi'` | p.96 derived: "overlays target starting at the nth character with new, padded or truncated to length length" | brexx:overlay:7 |  |  |
| 7 | `overlay('foo','abcdefghi',3,2,'*')` | `'abfoefghi'` | p.96 derived: "overlays target starting at the nth character with new, padded or truncated to length length" | brexx:overlay:8 |  | truncated to 2 |
| 8 | `overlay('foo','abcdefghi',3,4,)` | `'abfoo ghi'` | p.96 derived: "overlays target starting at the nth character with new, padded or truncated to length length" | brexx:overlay:9 |  | trailing empty pad arg = default blank |
| 9 | `overlay('foo','abcdefghi',3)` | `'abfoofghi'` | p.96 derived: default length = length(new), inferred from the examples (the manual states no default) | brexx:overlay:10 |  | inferred default |
| 10 | `overlay('foo','abcdefghi',,4,'*')` | `'foo*efghi'` | p.96 derived: "default value for n is 1" | brexx:overlay:11 |  |  |
| 11 | `overlay('foo','abcdefghi',9,4,'*')` | `'abcdefghfoo*'` | p.96 derived: "overlays target starting at the nth character with new, padded or truncated to length length" | brexx:overlay:12 |  | overlay extends past end of target |
| 12 | `overlay('foo','abcdefghi',10,4,'*')` | `'abcdefghifoo*'` | p.96 derived: "if n is greater than the length of target, padding is added before the new string" | brexx:overlay:13 |  | n = length+1: no padding needed |
| 13 | `overlay('foo','abcdefghi',11,4,'*')` | `'abcdefghi*foo*'` | p.96 derived: "if n is greater than the length of target, padding is added before the new string" | brexx:overlay:14 |  | pad char used for the gap |
| 14 | `overlay('','abcdefghi',3)` | `'abcdefghi'` | p.96 derived: default length = length(new), inferred from the examples (the manual states no default) | brexx:overlay:15 |  | inferred default (0) |
| 15 | `overlay('foo','',3)` | `'  foo'` | p.96 derived: "if n is greater than the length of target, padding is added before the new string"; default length inferred | brexx:overlay:16 |  |  |
| 16 | `overlay('','',3,4,'*')` | `'******'` | p.96 derived: "if n is greater than the length of target, padding is added before the new string" | brexx:overlay:17 |  | 2 gap pads + 4 pads for null new |
| 17 | `overlay('','')` | `''` | p.96 derived: n default 1, length default inferred 0 | brexx:overlay:18 |  |  |

Dropped:

| source | expression | BREXX said | reason |
|---|---|---|---|
| brexx:overlay:3 | `overlay('.','abcdef',3,2)` | `'ab. ef'` | duplicate of brexx:overlay:1. |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `overlay('a','abc',0)` | p.96: n "must be a positive whole number" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (17/17).

## POS

16 cases: 4 manual examples, 12 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `pos('day','Saturday')` | `'6'` | p.96 example | spec; =brexx:pos:1 |  |  |
| 2 | `pos('x','abc def ghi')` | `'0'` | p.96 example | spec; =brexx:pos:2 |  |  |
| 3 | `pos(' ','abc def ghi')` | `'4'` | p.96 example | spec; =brexx:pos:3 |  |  |
| 4 | `pos(' ','abc def ghi',5)` | `'8'` | p.96 example | spec; =brexx:pos:4 |  |  |
| 5 | `pos('foo','a foo foo b')` | `'3'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:5 |  |  |
| 6 | `pos('foo','a foo foo',3)` | `'3'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:6 |  | match at start |
| 7 | `pos('foo','a foo foo',4)` | `'7'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:7 |  |  |
| 8 | `pos('foo','a foo foo b',30)` | `'0'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:8 |  | start > length |
| 9 | `pos('foo','a foo foo b',1)` | `'3'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:9 |  |  |
| 10 | `pos('foo','')` | `'0'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:11 |  | null haystack |
| 11 | `pos('b','a')` | `'0'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:13 |  |  |
| 12 | `pos('b','b')` | `'1'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:14 |  |  |
| 13 | `pos('b','abc')` | `'2'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:15 |  |  |
| 14 | `pos('b','def')` | `'0'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:16 |  |  |
| 15 | `pos('foo','foo foo b')` | `'1'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:pos:17 |  |  |
| 16 | `pos('abc','abcdefabccdabcd',4)` | `'7'` | p.96 derived: "position of one string, needle, in another, haystack; if not found, 0 is returned"; start = "point at which to start the search" | brexx:lastpos:19 |  | misfiled in lastpos.rexx (it is a POS call) |

Dropped:

| source | expression | BREXX said | reason |
|---|---|---|---|
| brexx:pos:10 | `pos('','a foo foo b')` | `0` | null needle: p.96 does not say what a null needle yields. Not settled. |
| brexx:pos:12 | `pos('','')` | `0` | null needle, as above. |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `pos('a','abc',0)` | p.96: start "must be a positive whole number" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (16/16).

## REVERSE

8 cases: 2 manual examples, 6 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `reverse('ABc.')` | `'.cBA'` | p.98 example | spec; =brexx:reverse:1 |  |  |
| 2 | `reverse('XYZ ')` | `' ZYX'` | p.98 example | spec; =brexx:reverse:2 |  |  |
| 3 | `reverse('Tranquility')` | `'ytiliuqnarT'` | p.98 derived: "string, swapped end for end" | brexx:reverse:3 |  |  |
| 4 | `reverse('foobar')` | `'raboof'` | p.98 derived: "string, swapped end for end" | brexx:reverse:4 |  |  |
| 5 | `reverse('')` | `''` | p.98 derived: "string, swapped end for end" | brexx:reverse:5 |  |  |
| 6 | `reverse('fubar')` | `'rabuf'` | p.98 derived: "string, swapped end for end" | brexx:reverse:6 |  |  |
| 7 | `reverse('f')` | `'f'` | p.98 derived: "string, swapped end for end" | brexx:reverse:7 |  |  |
| 8 | `reverse(' foobar ')` | `' raboof '` | p.98 derived: "string, swapped end for end" | brexx:reverse:8 |  |  |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (8/8).

## RIGHT

10 cases: 3 manual examples, 7 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `right('abc  d',8)` | `'  abc  d'` | p.98 example | spec; =brexx:right:1 |  |  |
| 2 | `right('abc def',5)` | `'c def'` | p.98 example | spec; =brexx:right:2 |  |  |
| 3 | `right('12',5,'0')` | `'00012'` | p.98 example | spec; =brexx:right:3 |  |  |
| 4 | `right('',4)` | `'    '` | p.98 derived: "rightmost length characters ... padded with pad characters (or truncated) on the left" | brexx:right:4 |  |  |
| 5 | `right('foobar',0)` | `''` | p.98 derived: "rightmost length characters ... padded with pad characters (or truncated) on the left" | brexx:right:5 |  |  |
| 6 | `right('foobar',3)` | `'bar'` | p.98 derived: "rightmost length characters ... padded with pad characters (or truncated) on the left" | brexx:right:6 |  |  |
| 7 | `right('foobar',6)` | `'foobar'` | p.98 derived: "rightmost length characters ... padded with pad characters (or truncated) on the left" | brexx:right:7 |  |  |
| 8 | `right('foobar',8)` | `'  foobar'` | p.98 derived: "rightmost length characters ... padded with pad characters (or truncated) on the left" | brexx:right:8 |  |  |
| 9 | `right('foobar',8,'*')` | `'**foobar'` | p.98 derived: "rightmost length characters ... padded with pad characters (or truncated) on the left" | brexx:right:9 |  |  |
| 10 | `right('foobar',4,'*')` | `'obar'` | p.98 derived: "rightmost length characters ... padded with pad characters (or truncated) on the left" | brexx:right:10 |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `right('abc',-1)` | p.98: "length must be nonnegative" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (10/10).

## SPACE

14 cases: 5 manual examples, 9 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `space('abc  def  ')` | `'abc def'` | p.99 example | spec; =brexx:space:1 |  |  |
| 2 | `space('  abc def',3)` | `'abc   def'` | p.99 example | spec; =brexx:space:2 |  |  |
| 3 | `space('abc  def  ',1)` | `'abc def'` | p.99 example | spec; =brexx:space:3 |  |  |
| 4 | `space('abc  def  ',0)` | `'abcdef'` | p.99 example | spec; =brexx:space:4 |  |  |
| 5 | `space('abc  def  ',2,'+')` | `'abc++def'` | p.99 example | spec; =brexx:space:5 |  |  |
| 6 | `space(' foo ')` | `'foo'` | p.99 derived: "n pad characters between each word"; "leading and trailing blanks are always removed"; default n 1, pad blank | brexx:space:6 |  |  |
| 7 | `space(' foo')` | `'foo'` | p.99 derived: "n pad characters between each word"; "leading and trailing blanks are always removed"; default n 1, pad blank | brexx:space:7 |  |  |
| 8 | `space('foo ')` | `'foo'` | p.99 derived: "n pad characters between each word"; "leading and trailing blanks are always removed"; default n 1, pad blank | brexx:space:8 |  |  |
| 9 | `space(' foo bar ')` | `'foo bar'` | p.99 derived: "n pad characters between each word"; "leading and trailing blanks are always removed"; default n 1, pad blank | brexx:space:10 |  |  |
| 10 | `space(' foo bar ',2)` | `'foo  bar'` | p.99 derived: "n pad characters between each word"; "leading and trailing blanks are always removed"; default n 1, pad blank | brexx:space:12 |  |  |
| 11 | `space(' foo bar ',,'-')` | `'foo-bar'` | p.99 derived: "n pad characters between each word"; "leading and trailing blanks are always removed"; default n 1, pad blank | brexx:space:13 |  | n omitted |
| 12 | `space(' foo bar ',2,'-')` | `'foo--bar'` | p.99 derived: "n pad characters between each word"; "leading and trailing blanks are always removed"; default n 1, pad blank | brexx:space:14 |  |  |
| 13 | `space(' f-- b-- ',2,'-')` | `'f----b--'` | p.99 derived: "n pad characters between each word"; "leading and trailing blanks are always removed"; default n 1, pad blank | brexx:space:15 |  | pad "-" in words is data, not a separator |
| 14 | `space(' f o o b a r ',0)` | `'foobar'` | p.99 derived: "n pad characters between each word"; "leading and trailing blanks are always removed"; default n 1, pad blank | brexx:space:16 |  | n=0: "all blanks are removed" |

Dropped:

| source | expression | BREXX said | reason |
|---|---|---|---|
| brexx:space:9 | `space(' foo ')` | `'foo'` | identical to brexx:space:6. |
| brexx:space:11 | `space(' foo bar ')` | `'foo bar'` | identical to brexx:space:10. |
| brexx:blanks:4 | `space(' a'\|\|'05'x\|\|'b  c ')` | `'a'\|\|'05'x\|\|'b c'` | chapter 2 of the 1988 manual never defines which characters are blanks (no statement that X'05' is not one). Not settled by SC28-1883-0. |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `space('a b',-1)` | p.99: "The n must be nonnegative" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (14/14).

## STRIP

12 cases: 5 manual examples, 7 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `strip('  ab c  ')` | `'ab c'` | p.100 example | spec; =brexx:strip:1 |  |  |
| 2 | `strip('  ab c  ','L')` | `'ab c  '` | p.100 example | spec; =brexx:strip:2 |  |  |
| 3 | `strip('  ab c  ','t')` | `'  ab c'` | p.100 example | spec; =brexx:strip:3 |  |  |
| 4 | `strip('12.7000',,0)` | `'12.7'` | p.100 example | spec; =brexx:strip:4 |  |  |
| 5 | `strip('0012.700',,0)` | `'12.7'` | p.100 example | spec |  | manual's literal is '0012.700' |
| 6 | `strip('0012.7000',,0)` | `'12.7'` | p.100 derived: variant of the manual STRIP example | brexx:strip:5 |  | BREXX used '0012.7000' |
| 7 | `strip(' foo bar ')` | `'foo bar'` | p.100 derived: Both (default)/Leading/Trailing; "char specifies the character to be removed" | brexx:strip:6 |  |  |
| 8 | `strip(' foo bar ','L')` | `'foo bar '` | p.100 derived: Both (default)/Leading/Trailing; "char specifies the character to be removed" | brexx:strip:7 |  |  |
| 9 | `strip(' foo bar ','T')` | `' foo bar'` | p.100 derived: Both (default)/Leading/Trailing; "char specifies the character to be removed" | brexx:strip:8 |  |  |
| 10 | `strip(' foo bar ','B')` | `'foo bar'` | p.100 derived: Both (default)/Leading/Trailing; "char specifies the character to be removed" | brexx:strip:9 |  |  |
| 11 | `strip(' foo bar ','B','*')` | `' foo bar '` | p.100 derived: Both (default)/Leading/Trailing; "char specifies the character to be removed" | brexx:strip:10 |  | no leading/trailing "*" |
| 12 | `strip(' foo bar',,'r')` | `' foo ba'` | p.100 derived: Both (default)/Leading/Trailing; "char specifies the character to be removed" | brexx:strip:11 |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `strip(' a ',,'xy')` | p.100: "char must be exactly one character long" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (12/12).

## SUBSTR

9 cases: 3 manual examples, 6 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `substr('abc',2)` | `'bc'` | p.100 example | spec; =brexx:substr:1 |  |  |
| 2 | `substr('abc',2,4)` | `'bc  '` | p.100 example | spec; =brexx:substr:2 |  |  |
| 3 | `substr('abc',2,6,'.')` | `'bc....'` | p.100 example | spec; =brexx:substr:3 |  |  |
| 4 | `substr('foobar',2,3)` | `'oob'` | p.100 derived: "substring that begins at the nth character, of length length, padded with pad if necessary" | brexx:substr:4 |  |  |
| 5 | `substr('foobar',3)` | `'obar'` | p.100 derived: "substring that begins at the nth character, of length length, padded with pad if necessary" | brexx:substr:5 |  | length omitted: rest of string |
| 6 | `substr('foobar',3,6)` | `'obar  '` | p.100 derived: "substring that begins at the nth character, of length length, padded with pad if necessary" | brexx:substr:6 |  |  |
| 7 | `substr('foobar',3,6,'*')` | `'obar**'` | p.100 derived: "substring that begins at the nth character, of length length, padded with pad if necessary" | brexx:substr:7 |  |  |
| 8 | `substr('foobar',6,3)` | `'r  '` | p.100 derived: "substring that begins at the nth character, of length length, padded with pad if necessary" | brexx:substr:8 |  |  |
| 9 | `substr('foobar',8,3)` | `'   '` | p.100 derived: "substring that begins at the nth character, of length length, padded with pad if necessary" | brexx:substr:9 |  | n beyond end: all pad |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `substr('abc',0)` | p.100: n "must be a positive whole number" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (9/9).

## TRANSLAT (TRANSLATE)

12 cases: 5 manual examples, 7 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `translate('abcdef')` | `'ABCDEF'` | p.104 example | spec; =brexx:transla:1 |  |  |
| 2 | `translate('abbc','&','b')` | `'a&&c'` | p.104 example | spec; =brexx:transla:2 |  |  |
| 3 | `translate('abcdef','12','ec')` | `'ab2d1f'` | p.104 example | spec; =brexx:transla:3 |  |  |
| 4 | `translate('abcdef','12','abcd','.')` | `'12..ef'` | p.104 example | spec; =brexx:transla:4 |  |  |
| 5 | `translate('4123','abcd','1234')` | `'dabc'` | p.104 example | spec; =brexx:transla:5 |  |  |
| 6 | `translate('Foo Bar')` | `'FOO BAR'` | p.104 derived: "if neither translate table is given, string is simply translated to uppercase" | brexx:transla:6 |  |  |
| 7 | `translate('Foo Bar',,'')` | `'Foo Bar'` | p.104 derived: tablei given as null string (p.77: "a null string can be supplied"), so no character is in the input table | brexx:transla:7 |  | reading: a null argument counts as "given" |
| 8 | `translate('Foo Bar','',)` | `'       '` | p.104 derived: "output table defaults to the null string and is padded with pad or truncated"; tablei default XRANGE('00'x,'FF'x) | brexx:transla:8 |  | tableo null + default pad blank: every char -> blank |
| 9 | `translate('Foo Bar','',,'*')` | `'*******'` | p.104 derived: "output table defaults to the null string and is padded with pad or truncated"; tablei default XRANGE('00'x,'FF'x) | brexx:transla:9 |  | as 8 with pad "*" |
| 10 | `translate('','klasjdf','woieruw')` | `''` | p.104 derived: null string in, null out | brexx:transla:10 |  |  |
| 11 | `translate('foobar','abcdef','fedcba')` | `'aooefr'` | p.104 derived: character mapped by position in tablei | brexx:transla:11 |  |  |
| 12 | `translate('aba','xyz','aab')` | `'xzx'` | p.104 derived: "the first occurrence of a character in the input table is the one that is used if there are duplicates" | spec rule |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `translate('abc','x','a','xy')` | p.77: pad must be exactly one character |

Host run 2026-09-30 (bytecode VM; token-walk same): FAIL, cases 7, 8, 9.

## TRANSLHX (TRANSLATE, hex strings)

2 cases: 0 manual examples, 2 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `translate('Foo Bar',xrange('01'x,'ff'x))` | `'Gpp'\|\|'41'x\|\|'Cb'\|\|'9A'x` | p.104 derived: "output table defaults to the null string and is padded with pad or truncated"; tablei default XRANGE('00'x,'FF'x); EBCDIC: each code maps to code+1, FF to blank | brexx:transla:12 |  | `te` (EBCDIC only). EBCDIC F->G, o->p, blank->41, B->C, a->b, r->9A |
| 2 | `translate('A'\|\|'00'x,'x','00'x)` | `'Ax'` | p.104 derived: character mapped by position in tablei | spec rule |  | character-set independent |

Host run 2026-09-30 (bytecode VM; token-walk same): died before case 1 (`run_rc=20`, first hex literal).

## VERIFY

17 cases: 7 manual examples, 10 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `verify('123','1234567890')` | `'0'` | p.106 example | spec; =brexx:verify:1 |  |  |
| 2 | `verify('1Z3','1234567890')` | `'2'` | p.106 example | spec; =brexx:verify:2 |  |  |
| 3 | `verify('AB4T','1234567890')` | `'1'` | p.106 example | spec |  |  |
| 4 | `verify('AB4T','1234567890','M')` | `'3'` | p.106 example | spec; =brexx:verify:3 |  |  |
| 5 | `verify('AB4T','1234567890','N')` | `'1'` | p.106 example | spec |  |  |
| 6 | `verify('1P3Q4','1234567890',,3)` | `'4'` | p.106 example | spec; =brexx:verify:4 |  |  |
| 7 | `verify('AB3CD5','1234567890','M',4)` | `'6'` | p.106 example | spec; =brexx:verify:6 |  |  |
| 8 | `verify('foobar','barfo','N',1)` | `'0'` | p.106 derived: "position of the first character in string that is not also in reference" (Nomatch) / "that is in reference" (Match) | brexx:verify:7 |  | option quoted (BREXX: unquoted symbol N) |
| 9 | `verify('foobar','barfo','M',1)` | `'1'` | p.106 derived: "position of the first character in string that is not also in reference" (Nomatch) / "that is in reference" (Match) | brexx:verify:8 |  | option quoted (BREXX: unquoted symbol M) |
| 10 | `verify('','barfo')` | `'0'` | p.106 derived: "if string is null, the function returns 0, regardless of the value of the third argument" | brexx:verify:9 |  |  |
| 11 | `verify('foobar','')` | `'1'` | p.106 derived: "if reference is null and option Nomatch specified, or left to default, the function will return 1" | brexx:verify:10 |  |  |
| 12 | `verify('foobar','barf','N',3)` | `'3'` | p.106 derived: "position of the first character in string that is not also in reference" (Nomatch) / "that is in reference" (Match) | brexx:verify:11 |  |  |
| 13 | `verify('foobar','barf','N',4)` | `'0'` | p.106 derived: "position of the first character in string that is not also in reference" (Nomatch) / "that is in reference" (Match) | brexx:verify:12 |  |  |
| 14 | `verify('','')` | `'0'` | p.106 derived: string null -> 0 | brexx:verify:13 |  |  |
| 15 | `verify('foobar','','M')` | `'0'` | p.106 derived: "if reference is null and option Match is specified, the function will return 0" | spec rule |  |  |
| 16 | `verify('abc','abc',,4)` | `'0'` | p.106 derived: "if start is greater than LENGTH(string), 0 is returned" | spec rule |  |  |
| 17 | `verify('AB4T','1234567890','match')` | `'3'` | p.106 derived: "only the first character of option is significant and it can be in upper or lower case" | spec rule |  |  |

Dropped:

| source | expression | BREXX said | reason |
|---|---|---|---|
| brexx:verify:5 | `verify('ABCDE','',,3)` | `3` | two readings: p.106 literally ("If reference is null and option Nomatch specified, or left to default, the function will return 1") gives 1; the search-from-start rule (first character at or after start not in reference) gives 3, BREXX's value. The sentence may not have start in mind. Not settled; the start=1 case (case 11) covers the sentence unambiguously. |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `verify('abc','a',,0)` | p.106: start "must be a positive whole number" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (17/17).

## XRANGE

3 cases: 1 manual examples, 2 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `xrange('a','f')` | `'abcdef'` | p.108 example | spec; =brexx:xrange:1 |  | t, not te: a..f is 81..86 (EBCDIC) and 61..66 (ASCII), contiguous in both |
| 2 | `xrange('a','a')` | `'a'` | p.108 derived: "all one-byte codes between and including start and end" | brexx:xrange:5 |  |  |
| 3 | `length(xrange())` | `'256'` | p.108 derived: default start 00, end FF -> 256 codes | spec rule |  |  |

ERROR-CASE (syntax error 40 expected; wait for SIGNAL ON SYNTAX):

| expression | rule |
|---|---|
| `xrange('ab')` | p.108: "start and end must be single characters" |

Host run 2026-09-30 (bytecode VM; token-walk same): PASS (3/3).

## XRANGEHX (XRANGE, hex strings)

6 cases: 4 manual examples, 2 derived.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `xrange('03'x,'07'x)` | `'0304050607'x` | p.108 example | spec; =brexx:xrange:2 |  |  |
| 2 | `xrange(,'04'x)` | `'0001020304'x` | p.108 example | spec |  |  |
| 3 | `xrange('i','j')` | `'898A8B8C8D8E8F9091'x` | p.108 example (EBCDIC) | spec |  | `te` (EBCDIC only). |
| 4 | `xrange('FE'x,'02'x)` | `'FEFF000102'x` | p.108 example | spec; =brexx:xrange:3 |  |  |
| 5 | `xrange('7D'x,'83'x)` | `'7D7E7F80818283'x` | p.108 derived: all codes between and including start and end | brexx:xrange:4 |  |  |
| 6 | `xrange('f','r')` | `'fghi'\|\|'8A8B8C8D8E8F90'x\|\|'jklmnopqr'` | p.108 derived: EBCDIC codes 86..99 include the gap 8A..90 | brexx:xrange:6 |  | `te` (EBCDIC only). |

Host run 2026-09-30 (bytecode VM; token-walk same): died before case 1 (`run_rc=20`, first hex literal).

## Host failures (rexx370 defects, expectations unchanged)

- JUSTIFY 3, 4 (manual examples): got `The blue ` / `The+blue+` --
  inferred from the output: the trailing blank left by truncation is not
  removed before the pads are added.
- TRANSLAT 7, 8, 9: a table given as a null string is treated as
  omitted, so the string is uppercased (`FOO BAR`).
- TRANSLHX, XRANGEHX: die at the first hex literal (known defect).

Totals: 247 cases, 88 manual examples, 159 derived.
