# SC28-1883-0 conformance: language group

Members in `test/spec/`: PARSE1-PARSE4, ARG, ARGOPT, COMPOPS, COMPNOT,
COMPSLSH, COMPCHN, PREFIX, EVALORD, CONCAT, CONCATX, SYMBOL, VALUE, DATE,
DATEO, DATEC, TIME, TRACE, ERRORTXT, ADDRESS, SOURCELN.

The only authority for an expected value is SC28-1883-0 (TSO/E Version 2
REXX Reference, December 1988). Pages are printed page numbers (PDF page =
printed + 15). `spec` says whether the value is a manual **example** or
**derived** from a rule (quoted briefly). `source` is `spec` for cases
taken from the manual (example or rule) and `brexx:<file>:<n>` for cases
that come from a brexx370 test, `n` being the number that test gives the
case. `BREXX said` is filled only where the BREXX expectation differs.

## Splits

A runtime or compile error ends the whole exec in rexx370 today, so cases
that stop it were moved into members of their own:

| member | split from | reason |
|---|---|---|
| ARGOPT | ARG | `ARG(1,'Exists')` (option word longer than one letter) is rejected (#280) and ends the exec (run_rc 24); before #262 was fixed, that error inside a routine aborted rexx370 (exit 134) |
| COMPNOT | COMPOPS | `\<<` / `\>>` are misparsed; in COMPOPS they stopped the whole exec (run_rc 20) |
| COMPSLSH | COMPOPS | `/=` / `/==` are rejected at compile time (run_rc 20) |
| COMPCHN | COMPOPS | chained comparisons (`2=2=2`), known error 20 |
| CONCATX | CONCAT | `'!'xxx'!'` is lexed as a hex string (run_rc 21) |
| DATEO, DATEC | DATE | `DATE('O')` and `DATE('C')` end the exec (run_rc 24) |

Inside a member, a case known to stop rexx370 was put last where that was
possible (PARSE4: the data-stack cases, ERRORTXT: `ERRORTEXT(0)`,
`ERRORTEXT(99)`, `ERRORTEXT(' 16 ')`, ADDRESS: `ADDRESS ('M'||'VS')`,
SOURCELN: `SOURCELINE(' 1 ')`). Cases after the stop are not reached.

## Harness notes (not cases)

- Never end a line with `,,` inside a `call t` line: the last comma is a
  continuation and, per p.12, is replaced by a blank; rexx370 instead
  passes an extra empty argument. The language case itself is ARG 32-33.
- Hex literals are avoided (they switch rexx370 to the token-walk path):
  BREXX cases with `'00'x` etc. use `X2C('00')`. `D2C(0)` is not used:
  rexx370 returns a null string for it (strings group).
- DATE and TIME cases compare only formats and relations. Each check is a
  single expression, because only within one expression are all DATE/TIME
  calls guaranteed to use one time stamp (p.86, p.103).
- SOURCELN's expected values (line count, line 1, the marker line number)
  were generated from the member itself; editing the member means
  regenerating them.
- Both interpreter paths were run: bytecode VM (default) and token-walk
  (`REXX370_BYTECODE=0`). In token-walk, every internal *function* call
  ends the exec with run_rc 24 (known), which explains the early stops
  there.

### PARSE1 (31 cases: 11 manual examples, 20 derived)

Host run, bytecode VM: ran to the end; FAIL: 6, 9, 10.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; FAIL: 6, 9, 10.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `parse value 'This is a sentence.' with v1 v2 v3` → `v1` | `'This'` | p.131 example | spec |  |  |
| 2 | `v2` | `'is'` | p.131 example | spec |  |  |
| 3 | `v3` | `'a sentence.'` | p.131 example | spec |  |  |
| 4 | `parse value 'This is   a sentence.' with v1 v2 v3` → `v1` | `'This'` | p.131 example | spec |  | 6: last variable keeps its leading blanks (known rexx370 defect) |
| 5 | `v2` | `'is'` | p.131 example | spec |  | 6: last variable keeps its leading blanks (known rexx370 defect) |
| 6 | `v3` | `'  a sentence.'` | p.131 example | spec |  | 6: last variable keeps its leading blanks (known rexx370 defect) |
| 7 | `parse value 'a b  ' with v1 v2` → `v2` | `'b  '` | p.131 derived: "the last variable could have both leading and trailing blanks" | spec |  |  |
| 8 | `parse value '  a  b' with v1 v2` → `v1` | `'a'` | p.131, 134-135 derived: leading blanks skipped; only the blank delimiting the previous word is removed, the last variable keeps the rest | spec |  |  |
| 9 | `v2` | `' b'` | p.131, 134-135 derived: leading blanks skipped; only the blank delimiting the previous word is removed, the last variable keeps the rest | spec |  |  |
| 10 | `parse value '  x  ' with v1` → `v1` | `'  x  '` | p.133 derived: "single variable name ... assigned the entire input string" | spec |  |  |
| 11 | `v2 = 'old'; v3 = 'old'; parse value 'a' with v1 v2 v3` → `v1` | `'a'` | p.131 derived: "the unused variables will be set to null" | spec |  |  |
| 12 | `v2` | `''` | p.131 derived: "the unused variables will be set to null" | spec |  |  |
| 13 | `v3` | `''` | p.131 derived: "the unused variables will be set to null" | spec |  |  |
| 14 | `s = 'This is  the data which, I think,  is scanned.'; parse var s w1 w2 w3 rest` → `w1` | `'This'` | p.134-135 derived: word parsing; "only the blank that delimits the previous word is removed" | spec |  | example string of p.134 |
| 15 | `w2` | `'is'` | p.134-135 derived: word parsing; "only the blank that delimits the previous word is removed" | spec |  | example string of p.134 |
| 16 | `w3` | `'the'` | p.134-135 derived: word parsing; "only the blank that delimits the previous word is removed" | spec |  | example string of p.134 |
| 17 | `rest` | `'data which, I think,  is scanned.'` | p.134-135 derived: word parsing; "only the blank that delimits the previous word is removed" | spec |  | example string of p.134 |
| 18 | `parse var s . . . word4 .` → `word4` | `'data'` | p.136 example | spec |  | brexx:parse.rexx:25 is the same case on its "text" string |
| 19 | `parse upper value 'To be, or not' with u` → `u` | `'TO BE, OR NOT'` | p.50 derived: UPPER translates the data first | spec |  |  |
| 20 | `string = 'one two three'; parse var string word1 string` → `word1` | `'one'` | p.52 example | spec |  |  |
| 21 | `string` | `'two three'` | p.52 example | spec |  |  |
| 22 | `parse upper var string word1 string` → `word1` | `'TWO'` | p.52 example | spec |  |  |
| 23 | `string` | `'THREE'` | p.52 example | spec |  |  |
| 24 | `parse value 'x' with st.` → `st.abc` | `'x'` | p.21 derived: note 1: "a stem used in a parsing template ... sets an entire collection" | spec |  |  |
| 25 | `st.7` | `'x'` | p.21 derived: note 1: "a stem used in a parsing template ... sets an entire collection" | spec |  |  |
| 26 | `parse value '' with ?centertag ?outputl83 ?outputp83 Abbrev. abstract` → `?centertag` | `''` | p.131 derived: unused variables set to null | brexx:parse.rexx:12 |  |  |
| 27 | `?outputl83` | `''` | p.131 derived: unused variables set to null | brexx:parse.rexx:12 |  |  |
| 28 | `?outputp83` | `''` | p.131 derived: unused variables set to null | brexx:parse.rexx:12 |  |  |
| 29 | `Abbrev.A` | `''` | p.131 derived: unused variables set to null | brexx:parse.rexx:12 |  |  |
| 30 | `Abbrev.42` | `''` | p.131 derived: unused variables set to null | brexx:parse.rexx:12 |  |  |
| 31 | `abstract` | `''` | p.131 derived: unused variables set to null | brexx:parse.rexx:12 |  |  |

### PARSE2 (60 cases: 29 manual examples, 31 derived)

Host run, bytecode VM: ran to the end; FAIL: 2, 14, 15, 17, 18, 32, 33, 36, 37, 41, 42, 43, 44, 47, 53, 54, 55, 56, 60.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; FAIL: 2, 14, 15, 17, 18, 36, 53, 54, 55, 56, 60.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `parse value 'To be, or not to be?' with w1 ',' w2` → `w1` | `'To be'` | p.132 example | spec |  |  |
| 2 | `w2` | `' or not to be?'` | p.132 example | spec |  |  |
| 3 | `parse value 'To be, or not to be?' with w1 ',' w2 w3 w4` → `w1` | `'To be'` | p.132 example | spec |  |  |
| 4 | `w2` | `'or'` | p.132 example | spec |  |  |
| 5 | `w3` | `'not'` | p.132 example | spec |  |  |
| 6 | `w4` | `'to be?'` | p.132 example | spec |  |  |
| 7 | `parse value 'To be' with w1 ',' w2` → `w1` | `'To be'` | p.132 derived: no match: the pattern matches the end, variables to the right null | spec |  |  |
| 8 | `w2` | `''` | p.132 derived: no match: the pattern matches the end, variables to the right null | spec |  |  |
| 9 | `comma = ','; parse value 'To be, or not to be?' with w1 (comma) w2 w3 w4` → `w1` | `'To be'` | p.132 example (variable pattern) | spec |  |  |
| 10 | `w2` | `'or'` | p.132 example (variable pattern) | spec |  |  |
| 11 | `w3` | `'not'` | p.132 example (variable pattern) | spec |  |  |
| 12 | `w4` | `'to be?'` | p.132 example (variable pattern) | spec |  |  |
| 13 | `s = 'This is  the data which, I think,  is scanned.'; parse var s w1 ',' w2 ',' rest` → `w1` | `'This is  the data which'` | p.134 example | spec |  | brexx:parse.rexx:22 has the same case on "text" instead of "data" |
| 14 | `w2` | `' I think'` | p.134 example | spec |  | brexx:parse.rexx:22 has the same case on "text" instead of "data" |
| 15 | `rest` | `'  is scanned.'` | p.134 example | spec |  | brexx:parse.rexx:22 has the same case on "text" instead of "data" |
| 16 | `parse var s w1 ',' w2 ',' w3 ',' rest` → `w1` | `'This is  the data which'` | p.134 example | spec |  | = brexx:parse.rexx:23 |
| 17 | `w2` | `' I think'` | p.134 example | spec |  | = brexx:parse.rexx:23 |
| 18 | `w3` | `'  is scanned.'` | p.134 example | spec |  | = brexx:parse.rexx:23 |
| 19 | `rest` | `''` | p.134 example | spec |  | = brexx:parse.rexx:23 |
| 20 | `parse var s w1 w2 w3 rest ','` → `w1` | `'This'` | p.135 example | spec |  | = brexx:parse.rexx:24 |
| 21 | `w2` | `'is'` | p.135 example | spec |  | = brexx:parse.rexx:24 |
| 22 | `w3` | `'the'` | p.135 example | spec |  | = brexx:parse.rexx:24 |
| 23 | `rest` | `'data which'` | p.135 example | spec |  | = brexx:parse.rexx:24 |
| 24 | `parse var s w1 ' ' w2 ' ' w3 ' ' rest ','` → `w1` | `'This'` | p.135 example | spec |  | = brexx:parse.rexx:25 |
| 25 | `w2` | `'is'` | p.135 example | spec |  | = brexx:parse.rexx:25 |
| 26 | `w3` | `''` | p.135 example | spec |  | = brexx:parse.rexx:25 |
| 27 | `rest` | `'the data which'` | p.135 example | spec |  | = brexx:parse.rexx:25 |
| 28 | `input = 'L/look for/1 10'; parse var input verb 2 delim +1 string (delim) rest` → `verb` | `'L'` | p.135 example | spec |  | = brexx:parse.rexx:30 |
| 29 | `delim` | `'/'` | p.135 example | spec |  | = brexx:parse.rexx:30 |
| 30 | `string` | `'look for'` | p.135 example | spec |  | = brexx:parse.rexx:30 |
| 31 | `rest` | `'1 10'` | p.135 example | spec |  | = brexx:parse.rexx:30 |
| 32 | `parse value 'abc' with p1 '' p2` → `p1` | `'abc'` | p.134 derived: "a null pattern ... match[es] the end of the data" | spec |  |  |
| 33 | `p2` | `''` | p.134 derived: "a null pattern ... match[es] the end of the data" | spec |  |  |
| 34 | `parse value '(inval1 inval2) outval' with '(' in1 in2 ')' out` → `in1` | `'inval1'` | p.131, 134 derived: last variable keeps its leading blank | brexx:parse.rexx:1 |  |  |
| 35 | `in2` | `'inval2'` | p.131, 134 derived: last variable keeps its leading blank | brexx:parse.rexx:1 |  |  |
| 36 | `out` | `' outval'` | p.131, 134 derived: last variable keeps its leading blank | brexx:parse.rexx:1 |  |  |
| 37 | `parse value '()' with '(' inner1 inner2 ')'` → `inner1` | `''` | p.131 derived: unused variables null | brexx:parse.rexx:2 |  |  |
| 38 | `inner2` | `''` | p.131 derived: unused variables null | brexx:parse.rexx:2 |  |  |
| 39 | `parse value 'UNIX COMMAND ./xxx.r brexx /bin/bash' with a b c d e './' f g h i j` → `a` | `'UNIX'` | p.131-134 derived: words per section between patterns | brexx:parse.rexx:3 |  |  |
| 40 | `b` | `'COMMAND'` | p.131-134 derived: words per section between patterns | brexx:parse.rexx:3 |  |  |
| 41 | `c d e` | `'  '` | p.131-134 derived: words per section between patterns | brexx:parse.rexx:3 |  |  |
| 42 | `f` | `'xxx.r'` | p.131-134 derived: words per section between patterns | brexx:parse.rexx:3 |  |  |
| 43 | `g` | `'brexx'` | p.131-134 derived: words per section between patterns | brexx:parse.rexx:3 |  |  |
| 44 | `h` | `'/bin/bash'` | p.131-134 derived: words per section between patterns | brexx:parse.rexx:3 |  |  |
| 45 | `i \|\| j` | `''` | p.131-134 derived: words per section between patterns | brexx:parse.rexx:3 |  |  |
| 46 | `parse value '11/09/1959' with Day '/' Month '/' Year` → `Day Month Year` | `'11 09 1959'` | p.134 derived: literal patterns | brexx:parse.rexx:8 |  |  |
| 47 | `parse value '11/09/1959' with Day . '/' Month . '/' Year .` → `Day Month Year` | `'11 09 1959'` | p.134 derived: literal patterns | brexx:parse.rexx:8 |  |  |
| 48 | `parse value '12345.678' with Integer '.' Decimal` → `Integer` | `'12345'` | p.134 derived: literal patterns | brexx:parse.rexx:9 |  |  |
| 49 | `Decimal` | `'678'` | p.134 derived: literal patterns | brexx:parse.rexx:9 |  |  |
| 50 | `parse value '<DL>,</DL>,<P><DT>,,<DD>,,' with  list.!dl.0.1 ',' list.!dl.0.2 ',' list.!dl.0.3 ',' list.!dl.0.4 ',' list.!dl.0.5 ',' list.!dl.0.6 ','` → `list.!dl.0.1` | `'<DL>'` | p.133, 134 derived: compound targets are ordinary variables | brexx:parse.rexx:15 |  |  |
| 51 | `list.!dl.0.2` | `'</DL>'` | p.133, 134 derived: compound targets are ordinary variables | brexx:parse.rexx:15 |  |  |
| 52 | `list.!dl.0.3` | `'<P><DT>'` | p.133, 134 derived: compound targets are ordinary variables | brexx:parse.rexx:15 |  |  |
| 53 | `list.!dl.0.4` | `''` | p.133, 134 derived: compound targets are ordinary variables | brexx:parse.rexx:15 |  |  |
| 54 | `list.!dl.0.5` | `'<DD>'` | p.133, 134 derived: compound targets are ordinary variables | brexx:parse.rexx:15 |  |  |
| 55 | `list.!dl.0.6` | `''` | p.133, 134 derived: compound targets are ordinary variables | brexx:parse.rexx:15 |  |  |
| 56 | `sl = '/'; v = x2c('15')sl\|\|x2c('09')sl\|\|x2c('00')sl; v = v\|\|x2c('01')sl\|\|x2c('FE')sl\|\|x2c('FF'); parse var v nl '/' tab '/' x00 '/' x01 '/' xfe '/' xff` → `c2x(nl\|\|tab\|\|x00\|\|x01\|\|xfe\|\|xff)` | `'1509000001FEFF'` | p.134 derived: literal patterns; data is any characters | brexx:parse.rexx:17 |  | hex literals of BREXX replaced by X2C (hex strings fall back to token-walk today) |
| 57 | `x00 = x2c('00'); v = 'before' \|\| x00 \|\| 'between' \|\| x00 \|\| 'after'; parse var v tagnest1 (x00) tntag (x00) tagnest2` → `tagnest1` | `'before'` | p.135 derived: variable pattern | brexx:parse.rexx:18 |  | X2C instead of hex literal |
| 58 | `tntag` | `'between'` | p.135 derived: variable pattern | brexx:parse.rexx:18 |  | X2C instead of hex literal |
| 59 | `tagnest2` | `'after'` | p.135 derived: variable pattern | brexx:parse.rexx:18 |  | X2C instead of hex literal |
| 60 | `v = x2c('15') x2c('09') x2c('00'); parse var v nl tab x00` → `c2x(nl) c2x(tab) c2x(x00)` | `'15 09 00'` | p.131 derived: only blanks delimit words | brexx:parse.rexx:16 |  | BREXX #115 case; X2C instead of hex literals |

### PARSE3 (45 cases: 21 manual examples, 24 derived)

Host run, bytecode VM: ran to the end; FAIL: 13, 16, 18, 19, 23, 25, 27, 29, 30, 31, 32, 35, 36, 37, 42, 44.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; FAIL: 13, 16, 18, 19, 23, 25, 27, 29, 30, 31, 32, 35, 36, 37.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `f = 'Flying pigs have wings'; parse var f x1 5 x2` → `x1` | `'Flyi'` | p.132 example | spec |  |  |
| 2 | `x2` | `'ng pigs have wings'` | p.132 example | spec |  |  |
| 3 | `parse var f x1 5 x2 10 x3` → `x1` | `'Flyi'` | p.132 example | spec |  |  |
| 4 | `x2` | `'ng pi'` | p.132 example | spec |  |  |
| 5 | `x3` | `'gs have wings'` | p.132 example | spec |  |  |
| 6 | `parse var f x1 5 x2 +5 x3` → `x1` | `'Flyi'` | p.132 example | spec |  |  |
| 7 | `x2` | `'ng pi'` | p.132 example | spec |  |  |
| 8 | `x3` | `'gs have wings'` | p.132 example | spec |  |  |
| 9 | `s = 'This is  the data which, I think,  is scanned.'; parse var s s1 10 s2 20 s3` → `s1` | `'This is  '` | p.136 example | spec |  | = brexx:parse.rexx:26 |
| 10 | `s2` | `'the data w'` | p.136 example | spec |  | = brexx:parse.rexx:26 |
| 11 | `s3` | `'hich, I think,  is scanned.'` | p.136 example | spec |  | = brexx:parse.rexx:26 |
| 12 | `a = '123456789'; parse var a 3 w1 +3 w2 3 w3` → `w1` | `'345'` | p.136 example | spec |  | = brexx:parse.rexx:27 |
| 13 | `w2` | `'6789'` | p.136 example | spec |  | = brexx:parse.rexx:27 |
| 14 | `w3` | `'3456789'` | p.136 example | spec |  | = brexx:parse.rexx:27 |
| 15 | `parse var a 3 w1 +3 w2 -3 w3` → `w1` | `'345'` | p.137 example | spec |  |  |
| 16 | `w2` | `'6789'` | p.137 example | spec |  |  |
| 17 | `w3` | `'3456789'` | p.137 example | spec |  |  |
| 18 | `x = 'hi mom!'; parse var x 1 w1 1 w2 1 w3` → `w1` | `'hi mom!'` | p.137 example (parse var x 1 w1 1 w2 1 w3) | spec |  | data from brexx:parse.rexx:28 |
| 19 | `w2` | `'hi mom!'` | p.137 example (parse var x 1 w1 1 w2 1 w3) | spec |  | data from brexx:parse.rexx:28 |
| 20 | `w3` | `'hi mom!'` | p.137 example (parse var x 1 w1 1 w2 1 w3) | spec |  | data from brexx:parse.rexx:28 |
| 21 | `parse value 'abc' with p1 10 p2` → `p1` | `'abc'` | p.137 derived: column beyond the data = end of data, no padding | spec |  |  |
| 22 | `p2` | `''` | p.137 derived: column beyond the data = end of data, no padding | spec |  |  |
| 23 | `parse value 'abcdef' with 3 p1 -5 p2` → `p1` | `'cdef'` | p.137 derived: backing up: left variable gets the rest; column left of 1 = column 1 | spec |  |  |
| 24 | `p2` | `'abcdef'` | p.137 derived: backing up: left variable gets the rest; column left of 1 = column 1 | spec |  |  |
| 25 | `parse value 'ab,cd' with ',' -1 c1 +1` → `c1` | `'b'` | p.137 derived: ',' -1 x +1: character before the comma or end of string | spec |  |  |
| 26 | `parse value 'abcd' with ',' -1 c1 +1` → `c1` | `'d'` | p.137 derived: ',' -1 x +1: character before the comma or end of string | spec |  |  |
| 27 | `opts = 'word1 prword2 word3'; parse upper value ' 'opts with ' PR' +1 prword ' '` → `prword` | `'PRWORD2'` | p.137 example | spec |  |  |
| 28 | `opts = 'word1 word3'; parse upper value ' 'opts with ' PR' +1 prword ' '` → `prword` | `''` | p.137 derived: "null if no such word exists" | spec |  |  |
| 29 | `opts = 'word1 prword2 word3'; parse upper value ' 'opts with ' PR' +1 prword .` → `prword` | `'PRWORD2'` | p.137 derived: as case 27 with a placeholder | brexx:parse.rexx:29 |  |  |
| 30 | `a = 'This is the data which, I think, is scanned.'; parse var a 'which' +5 y` → `y` | `', I think, is scanned.'` | p.136-138 derived: "last matched position" of a literal is its first character; a literal before a signed pattern is not removed | spec |  | value derived, not the printed one: manual prints y = ", I think is scanned" (case 1, no period) and ", I think is scanned." (case 2): the comma after "think" of its own data string is missing; value derived from the data. **Ambiguous print** |
| 31 | `parse var a 'which' x +5 y` → `x` | `'which'` | p.136-138 derived: "last matched position" of a literal is its first character; a literal before a signed pattern is not removed | spec |  | value derived, not the printed one: manual prints y = ", I think is scanned" (case 1, no period) and ", I think is scanned." (case 2): the comma after "think" of its own data string is missing; value derived from the data. **Ambiguous print** |
| 32 | `y` | `', I think, is scanned.'` | p.136-138 derived: "last matched position" of a literal is its first character; a literal before a signed pattern is not removed | spec |  | value derived, not the printed one: manual prints y = ", I think is scanned" (case 1, no period) and ", I think is scanned." (case 2): the comma after "think" of its own data string is missing; value derived from the data. **Ambiguous print** |
| 33 | `parse value '19760601' with Year +4 Month +2 Day` → `Year Month Day` | `'1976 06 01'` | p.136 derived: relative patterns | brexx:parse.rexx:8 |  |  |
| 34 | `parse value '12345' with Part1 +3 Part2` → `Part1 Part2` | `'123 45'` | p.136 derived: relative patterns | brexx:parse.rexx:10 |  |  |
| 35 | `parse value 1 with true 1 ?b2hreq 1 ?config. 1 ?cs.` → `true ?b2hreq` | `'1 1'` | p.137 derived: backing up to 1, stems as targets | brexx:parse.rexx:13 |  |  |
| 36 | `key = 'FOO'` → `?config.azerTy ?config.key ?cs.RaP ?cs.666` | `'1 1 1 1'` | p.137 derived: backing up to 1, stems as targets | brexx:parse.rexx:13 |  |  |
| 37 | `parse value 0 with false 1 ?!!gml 1 ?addressflag 1 ?annot` → `false ?!!gml ?addressflag ?annot` | `'0 0 0 0'` | p.137 derived: backing up to 1 | brexx:parse.rexx:14 |  |  |
| 38 | `input = '20260930 16:54:22'; parse var input  ymd hms 1 yyyy 5 mm 7 dd . 1 . hh ':' mi ':' ss .` → `ymd '/' hms` | `'20260930 / 16:54:22'` | p.137 derived: multiple backups | brexx:parse.rexx:11 |  | fixed data instead of DATE()/TIME() |
| 39 | `yyyy mm dd` | `'2026 09 30'` | p.137 derived: multiple backups | brexx:parse.rexx:11 |  | fixed data instead of DATE()/TIME() |
| 40 | `hh mi ss` | `'16 54 22'` | p.137 derived: multiple backups | brexx:parse.rexx:11 |  | fixed data instead of DATE()/TIME() |
| 41 | `parse value '/middle1 middle2/after' with delimiter +1 middle1 middle2 middle3 (delimiter) after` → `delimiter` | `'/'` | p.135-136 derived: relative + variable pattern | brexx:parse.rexx:20 |  |  |
| 42 | `middle1 middle2` | `'middle1 middle2'` | p.135-136 derived: relative + variable pattern | brexx:parse.rexx:20 |  |  |
| 43 | `middle3` | `''` | p.135-136 derived: relative + variable pattern | brexx:parse.rexx:20 |  |  |
| 44 | `after` | `'after'` | p.135-136 derived: relative + variable pattern | brexx:parse.rexx:20 |  |  |
| 45 | `parse value date('S') with century +2 .` → `century` | `left(date('S'),2)` | p.136 derived: relative pattern | brexx:parse.rexx:7 |  | compares with LEFT(DATE('S'),2), no fixed century |

### PARSE4 (27 cases: 3 manual examples, 24 derived)

Host run, bytecode VM: died after case 22 (run_rc 20); FAIL: 10, 17, 18.  
Token-walk (`REXX370_BYTECODE=0`): died after case 22 (run_rc 20); FAIL: 10, 17, 18.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `call fred 'This is the first string', 2` → `first` | `'This is the first string'` | p.133 example | spec |  |  |
| 2 | `second` | `'2'` | p.133 example | spec |  |  |
| 3 | `call m3 'alpha beta  gamma', 'second', 7` → `word1` | `'alpha'` | p.138 derived: comma moves to the next string; missing strings give null | spec |  | template of p.138 |
| 4 | `string1` | `'beta  gamma'` | p.138 derived: comma moves to the next string; missing strings give null | spec |  | template of p.138 |
| 5 | `string2` | `'second'` | p.138 derived: comma moves to the next string; missing strings give null | spec |  | template of p.138 |
| 6 | `num` | `'7'` | p.138 derived: comma moves to the next string; missing strings give null | spec |  | template of p.138 |
| 7 | `call m3 'alpha'` → `word1 '/' string1` | `'alpha / '` | p.138 derived: comma moves to the next string; missing strings give null | spec |  | template of p.138 |
| 8 | `string2 \|\| num` | `''` | p.138 derived: comma moves to the next string; missing strings give null | spec |  | template of p.138 |
| 9 | `parse value 'x y' with p1, p2` → `p1` | `'x y'` | p.138 derived: "only one string ... variables that follow a comma pattern are set to null" | spec |  |  |
| 10 | `p2` | `''` | p.138 derived: "only one string ... variables that follow a comma pattern are set to null" | spec |  |  |
| 11 | `call math 25, 2` → `n` | `'25'` | p.133 derived: PARSE ARG with comma | brexx:parse.rexx:4 |  |  |
| 12 | `precision` | `'2'` | p.133 derived: PARSE ARG with comma | brexx:parse.rexx:4 |  |  |
| 13 | `call up 'Easy Rider'` → `u1 u2` | `'EASY RIDER'` | p.50 derived: UPPER | spec |  |  |
| 14 | `l1 l2` | `'Easy Rider'` | p.50 derived: UPPER | spec |  |  |
| 15 | `parse numeric var1` → `var1` | `'9 0 SCIENTIFIC'` | p.51 example | spec |  |  |
| 16 | `numeric digits 5; numeric fuzz 1; numeric form engineering; parse numeric var1` → `var1` | `'5 1 ENGINEERING'` | p.51 derived: "in the order DIGITS FUZZ FORM" | spec |  |  |
| 17 | `numeric digits 9; numeric fuzz 0; numeric form scientific; parse source src` → `word(src,1)` | `'TSO'` | p.51-52 derived: token 1 is "TSO", nine tokens | spec |  | 18 inferred: token 9 is the PARSETOK user token (p.277), which defaults to blank; if it is blank WORDS() may count 8 |
| 18 | `words(src)` | `'9'` | p.51-52 derived: token 1 is "TSO", nine tokens | spec |  | 18 inferred: token 9 is the PARSETOK user token (p.277), which defaults to blank; if it is blank WORDS() may count 8 |
| 19 | `parse version ver` → `word(ver,1)` | `'REXX370'` | p.52 derived: five words, first "REXX370" | spec |  |  |
| 20 | `words(ver)` | `'5'` | p.52 derived: five words, first "REXX370" | spec |  |  |
| 21 | `x = 'My dog has fleas'; parse var x with y` → `with` | `'My'` | p.52 derived: WITH is a subkeyword only in PARSE VALUE | brexx:parse.rexx:19b |  |  |
| 22 | `y` | `'dog has fleas'` | p.52 derived: WITH is a subkeyword only in PARSE VALUE | brexx:parse.rexx:19b |  |  |
| 23 | `drop with; queue 'gone'; queue 'next'; parse pull; parse pull q1` → `q1` | `'next'` | p.50 derived: no template: PARSE PULL still removes a line | spec |  |  |
| 24 | `queue 'one two  three'; parse pull q1 q2` → `q1` | `'one'` | p.51, 131 derived: PARSE PULL parses the next queue line | spec |  |  |
| 25 | `q2` | `'two  three'` | p.51, 131 derived: PARSE PULL parses the next queue line | spec |  |  |
| 26 | `queue 'Up'; parse upper pull q1` → `q1` | `'UP'` | p.51, 131 derived: PARSE PULL parses the next queue line | spec |  |  |
| 27 | `queue 'one two'; parse pull with y` → `with '/' y` | `'one / two'` | p.52 derived: WITH as a target in PARSE PULL | brexx:parse.rexx:19b |  |  |

### ARG (33 cases: 19 manual examples, 14 derived)

Host run, bytecode VM: ran to the end; FAIL: 32, 33.  
Token-walk (`REXX370_BYTECODE=0`): died after case 19 (run_rc 24); no FAIL.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `call name` → `r.1` | `'0'` | p.79 example ("Call name;") | spec |  | = brexx:arg.rexx:1-5 |
| 2 | `r.2` | `''` | p.79 example ("Call name;") | spec |  | = brexx:arg.rexx:1-5 |
| 3 | `r.3` | `''` | p.79 example ("Call name;") | spec |  | = brexx:arg.rexx:1-5 |
| 4 | `r.4` | `'0'` | p.79 example ("Call name;") | spec |  | = brexx:arg.rexx:1-5 |
| 5 | `r.5` | `'1'` | p.79 example ("Call name;") | spec |  | = brexx:arg.rexx:1-5 |
| 6 | `call namex 'a',,'b'` → `r.1` | `'3'` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 7 | `r.2` | `'a'` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 8 | `r.3` | `''` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 9 | `r.4` | `'b'` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 10 | `r.5` | `''` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 11 | `r.6` | `''` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 12 | `r.7` | `'1'` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 13 | `r.8` | `'0'` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 14 | `r.9` | `'1'` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 15 | `r.10` | `'0'` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 16 | `r.11` | `'1'` | p.79 example ("Call name 'a',,'b';") | spec |  | 11: ARG(99), rule "for n>=4" |
| 17 | `call namex 1,,2` → `r.1 r.2 '/'r.3'/' r.4` | `'3 1 // 2'` | p.79 derived: as the example | brexx:arg.rexx:6-15 |  | = brexx:arg.rexx:21-30 |
| 18 | `r.5 r.6` | `' '` | p.79 derived: as the example | brexx:arg.rexx:6-15 |  | = brexx:arg.rexx:21-30 |
| 19 | `r.7 r.8 r.9 r.10 r.11` | `'1 0 1 0 1'` | p.79 derived: as the example | brexx:arg.rexx:6-15 |  | = brexx:arg.rexx:21-30 |
| 20 | `fna('a',,'b')` | `'3 a  b 1 0 1'` | p.79, 71 derived: same rules through a function call | spec |  |  |
| 21 | `fna()` | `'0    0 0 1'` | p.79, 71 derived: same rules through a function call | spec |  |  |
| 22 | `x = 'old'; call chg x` → `result` | `'old'` | p.33 derived: "expressions are evaluated ... and form the argument string(s)" | brexx:argval.rexx:1-6 |  |  |
| 23 | `x` | `'new'` | p.33 derived: "expressions are evaluated ... and form the argument string(s)" | brexx:argval.rexx:1-6 |  |  |
| 24 | `x = 'old'` → `chg(x)` | `'old'` | p.33 derived: "expressions are evaluated ... and form the argument string(s)" | brexx:argval.rexx:1-6 |  |  |
| 25 | `x = 'old'; y = 'yold'` → `chg2(x, y)` | `'old yold'` | p.33 derived: "expressions are evaluated ... and form the argument string(s)" | brexx:argval.rexx:1-6 |  |  |
| 26 | `x = 'old'` → `chg3(x,,x)` | `'old 0 old'` | p.33 derived: "expressions are evaluated ... and form the argument string(s)" | brexx:argval.rexx:1-6 |  |  |
| 27 | `x = 'old'` → `chg4(x)` | `'old'` | p.33 derived: "expressions are evaluated ... and form the argument string(s)" | brexx:argval.rexx:1-6 |  |  |
| 28 | `call easy 'Easy Rider'` → `adjective` | `'EASY'` | p.30 example | spec |  |  |
| 29 | `noun` | `'RIDER'` | p.30 example | spec |  |  |
| 30 | `fred('data X',1,5)` | `'DATA X/1/5'` | p.30 example (FRED('data X',1,5)) | spec |  |  |
| 31 | `call twice 'a b'` → `result` | `'A B\|a b'` | p.30 derived: "ARG ... can be executed as often as desired" | spec |  |  |
| 32 | `call cnt 1, 2` → `result` | `'2'` | p.12 derived: "the comma is functionally replaced by a blank" | spec |  | coordinator note: rexx370 passes an extra empty argument for a line ending ",," |
| 33 | `call cnt 1 , 2` → `result` | `'2'` | p.12 derived: "the comma is functionally replaced by a blank" | spec |  | coordinator note: rexx370 passes an extra empty argument for a line ending ",," |

### ARGOPT (2 cases: 0 manual examples, 2 derived)

Host run, bytecode VM: died before case 1 (run_rc 24, #280); no FAIL. Before the #262 fix: aborted without @@RESULT (exit 134, stdout lost).  
Token-walk (`REXX370_BYTECODE=0`): died before case 1 (run_rc 24); no FAIL.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `opt('x',,'y')` | `'1 1 0 1 0'` | p.79 derived: "only the capitalized letter is significant"; a null string is explicitly specified | spec |  | split from ARG: ARG(1,'Exists') ends rexx370's exec (#280) |
| 2 | `opt('')` | `'1 1 0 1 1'` | p.79 derived: "only the capitalized letter is significant"; a null string is explicitly specified | spec |  | split from ARG: ARG(1,'Exists') ends rexx370's exec (#280) |

### COMPOPS (65 cases: 14 manual examples, 51 derived)

Host run, bytecode VM: ran to the end; FAIL: 2, 12, 13, 14, 15, 17, 24, 34, 39, 55, 56.  
Token-walk (`REXX370_BYTECODE=0`): died after case 5 (run_rc 20); no FAIL.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `a = '3'` → `(a+1)>7` | `'0'` | p.17 example | spec |  |  |
| 2 | `' ' = ''` | `'1'` | p.17 example | spec |  |  |
| 3 | `' ' == ''` | `'0'` | p.17 example | spec |  |  |
| 4 | `' ' \== ''` | `'1'` | p.17 example | spec |  |  |
| 5 | `(a+1)*3=12` | `'1'` | p.17 example | spec |  |  |
| 6 | `'abc' << 'abd'` | `'1'` | p.17 example | spec |  |  |
| 7 | `'077' >> '11'` | `'0'` | p.17 example | spec |  |  |
| 8 | `'abc' >> 'ab'` | `'1'` | p.17 example | spec |  |  |
| 9 | `'ab ' << 'abd'` | `'1'` | p.17 example | spec |  |  |
| 10 | `'000000' >> '0E0000'` | `'1'` | p.17 example | spec |  | strict character comparison: 1 only in EBCDIC (F0 > C5); in ASCII 0 (30 < 45); `te` (EBCDIC) |
| 11 | `'000000' > '0E0000'` | `'0'` | p.17 derived: note: ">" compares 0E0000 and 000000 numerically | spec |  |  |
| 12 | `'abc  ' = 'abc'` | `'1'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 13 | `'abc' = 'abc  '` | `'1'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 14 | `'  abc  ' = 'abc'` | `'1'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 15 | `'abc  ' \= 'abc'` | `'0'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 16 | `'abc  ' == 'abc'` | `'0'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 17 | `'abc ' > 'abc'` | `'0'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 18 | `'abc ' >= 'abc'` | `'1'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 19 | `'abc ' < 'abd'` | `'1'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 20 | `'ab  c' > 'ab'` | `'1'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 21 | `'a b' = 'a  b'` | `'0'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 22 | `'ab x' < 'abcx'` | `'1'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 23 | `'ab' < 'ab  c'` | `'1'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 24 | `left('END',80) = 'END'` | `'1'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 25 | `'1 ' = 1` | `'1'` | p.14 derived: non-strict: leading and trailing blanks ignored, shorter padded | brexx:equal.rexx:1-15 |  |  |
| 26 | `'1.0' = 1` | `'1'` | p.14, 145 derived: both terms numeric: numeric comparison | spec |  |  |
| 27 | `'1.0' == 1` | `'0'` | p.14, 145 derived: both terms numeric: numeric comparison | spec |  |  |
| 28 | `' 12 ' = '12.00'` | `'1'` | p.14, 145 derived: both terms numeric: numeric comparison | spec |  |  |
| 29 | `'1e2' = 100` | `'1'` | p.14, 145 derived: both terms numeric: numeric comparison | spec |  |  |
| 30 | `'10' > '9'` | `'1'` | p.14, 145 derived: both terms numeric: numeric comparison | spec |  |  |
| 31 | `'10' >> '9'` | `'0'` | p.14, 145 derived: both terms numeric: numeric comparison | spec |  |  |
| 32 | `'-1' < '0'` | `'1'` | p.14, 145 derived: both terms numeric: numeric comparison | spec |  |  |
| 33 | `'+5' \= '5'` | `'0'` | p.14, 145 derived: both terms numeric: numeric comparison | spec |  |  |
| 34 | `'a' = 'a '` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 35 | `'a' \= 'b'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 36 | `'b' > 'a'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 37 | `'a' < 'b'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 38 | `'a' >< 'b'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 39 | `'a' <> 'a '` | `'0'` | p.14-15 derived: operator table | spec |  |  |
| 40 | `'a' >= 'a'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 41 | `'a' <= 'b'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 42 | `'a' \< 'b'` | `'0'` | p.14-15 derived: operator table | spec |  |  |
| 43 | `'a' \> 'b'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 44 | `'b ' >> 'b'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 45 | `'b' << 'b '` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 46 | `'b' >>= 'b'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 47 | `'a' <<= 'b'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 48 | `' a' \== 'a'` | `'1'` | p.14-15 derived: operator table | spec |  |  |
| 49 | `'a' < 'A'` | `'1'` | p.14 derived: EBCDIC: lowercase before uppercase, digits after letters | spec |  | `te` |
| 50 | `'Z' < '0'` | `'1'` | p.14 derived: EBCDIC: lowercase before uppercase, digits after letters | spec |  | `te` |
| 51 | `'z' < '9'` | `'1'` | p.14 derived: EBCDIC: lowercase before uppercase, digits after letters | spec |  | `te` |
| 52 | `'a' << 'A'` | `'1'` | p.14 derived: EBCDIC: lowercase before uppercase, digits after letters | spec |  | `te` |
| 53 | `numeric digits 5; numeric fuzz 0` → `4.9999 = 5` | `'0'` | p.145 example (NUMERIC DIGITS 5, FUZZ 0 / 1) | spec |  | may overlap with the numerics group |
| 54 | `4.9999 < 5` | `'1'` | p.145 example (NUMERIC DIGITS 5, FUZZ 0 / 1) | spec |  | may overlap with the numerics group |
| 55 | `numeric fuzz 1` → `4.9999 = 5` | `'1'` | p.145 example (NUMERIC DIGITS 5, FUZZ 0 / 1) | spec |  | may overlap with the numerics group |
| 56 | `4.9999 < 5` | `'0'` | p.145 example (NUMERIC DIGITS 5, FUZZ 0 / 1) | spec |  | may overlap with the numerics group |
| 57 | `numeric fuzz 0; numeric digits 9` → `1 & 1` | `'1'` | p.15 derived: logical operators | spec |  |  |
| 58 | `1 & 0` | `'0'` | p.15 derived: logical operators | spec |  |  |
| 59 | `0 \| 1` | `'1'` | p.15 derived: logical operators | spec |  |  |
| 60 | `0 \| 0` | `'0'` | p.15 derived: logical operators | spec |  |  |
| 61 | `1 && 1` | `'0'` | p.15 derived: logical operators | spec |  |  |
| 62 | `1 && 0` | `'1'` | p.15 derived: logical operators | spec |  |  |
| 63 | `\1` | `'0'` | p.15 derived: logical operators | spec |  |  |
| 64 | `\0` | `'1'` | p.15 derived: logical operators | spec |  |  |
| 65 | `(1 = 1) & ('a' < 'b')` | `'1'` | p.15 derived: logical operators | spec |  |  |

### COMPNOT (5 cases: 0 manual examples, 5 derived)

Host run, bytecode VM: ran to the end; FAIL: 1, 2, 3, 4, 5.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; FAIL: 1, 2, 3, 4, 5.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `'b' \<< 'a'` | `'1'` | p.15 derived: \<< strictly NOT less than, \>> strictly NOT greater than | spec |  | split from COMPOPS: rexx370 misparses these operators |
| 2 | `'b' \>> 'a'` | `'0'` | p.15 derived: \<< strictly NOT less than, \>> strictly NOT greater than | spec |  | split from COMPOPS: rexx370 misparses these operators |
| 3 | `'a' \<< 'a '` | `'0'` | p.15 derived: \<< strictly NOT less than, \>> strictly NOT greater than | spec |  | split from COMPOPS: rexx370 misparses these operators |
| 4 | `'a ' \>> 'a'` | `'0'` | p.15 derived: \<< strictly NOT less than, \>> strictly NOT greater than | spec |  | split from COMPOPS: rexx370 misparses these operators |
| 5 | `'10' \<< '9'` | `'0'` | p.15 derived: \<< strictly NOT less than, \>> strictly NOT greater than | spec |  | split from COMPOPS: rexx370 misparses these operators |

### COMPSLSH (5 cases: 0 manual examples, 5 derived)

Host run, bytecode VM: died before case 1 (run_rc 20); no FAIL.  
Token-walk (`REXX370_BYTECODE=0`): died before case 1 (run_rc 20); no FAIL.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `'a' /= 'b'` | `'1'` | p.15-16 derived: /= and /== are listed as not-equal operators | spec |  | split: rexx370 rejects /= at compile time (run_rc 20) |
| 2 | `'a' /= 'a '` | `'0'` | p.15-16 derived: /= and /== are listed as not-equal operators | spec |  | split: rexx370 rejects /= at compile time (run_rc 20) |
| 3 | `'a' /== 'a '` | `'1'` | p.15-16 derived: /= and /== are listed as not-equal operators | spec |  | split: rexx370 rejects /= at compile time (run_rc 20) |
| 4 | `'a' /== 'a'` | `'0'` | p.15-16 derived: /= and /== are listed as not-equal operators | spec |  | split: rexx370 rejects /= at compile time (run_rc 20) |
| 5 | `1 /= '1.0'` | `'0'` | p.15-16 derived: /= and /== are listed as not-equal operators | spec |  | split: rexx370 rejects /= at compile time (run_rc 20) |

### COMPCHN (6 cases: 0 manual examples, 6 derived)

Host run, bytecode VM: died after case 4 (run_rc 20); FAIL: 1, 2, 3, 4.  
Token-walk (`REXX370_BYTECODE=0`): died after case 4 (run_rc 20); FAIL: 1, 2, 3, 4.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `2=2=2` | `'0'` | p.16 derived: left-to-right evaluation; equal precedence is not reordered | brexx:opchain.rexx:1-5 |  | known rexx370 defect (error 20) |
| 2 | `1=1=1` | `'1'` | p.16 derived: left-to-right evaluation; equal precedence is not reordered | brexx:opchain.rexx:1-5 |  | known rexx370 defect (error 20) |
| 3 | `3>2>1` | `'0'` | p.16 derived: left-to-right evaluation; equal precedence is not reordered | brexx:opchain.rexx:1-5 |  | known rexx370 defect (error 20) |
| 4 | `'a'=='a'==1` | `'1'` | p.16 derived: left-to-right evaluation; equal precedence is not reordered | brexx:opchain.rexx:1-5 |  | known rexx370 defect (error 20) |
| 5 | `a = 2=2=2` → `a` | `'0'` | p.16 derived: left-to-right evaluation; equal precedence is not reordered | brexx:opchain.rexx:1-5 |  | known rexx370 defect (error 20) |
| 6 | `1<2<3` | `'1'` | p.16 derived: as above | spec |  |  |

### PREFIX (20 cases: 1 manual examples, 19 derived)

Host run, bytecode VM: ran to the end; FAIL: 6, 7, 11.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; FAIL: 1, 6, 7, 11.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `b = 0; a = 1; x = -\b` → `x` | `'-1'` | p.14, 16 derived: prefix operators, left to right | brexx:prefix.rexx |  | INTERPRET removed |
| 2 | `x = +-+-\0` → `x` | `'1'` | p.14, 16 derived: prefix operators, left to right | brexx:prefix.rexx |  | INTERPRET removed |
| 3 | `x = \\1` → `x` | `'1'` | p.14, 16 derived: prefix operators, left to right | brexx:prefix.rexx |  | INTERPRET removed |
| 4 | `x = -(-3)` → `x` | `'3'` | p.14, 16 derived: prefix operators, left to right | brexx:prefix.rexx |  | INTERPRET removed |
| 5 | `x = - -a` → `x` | `'1'` | p.14, 16 derived: prefix operators, left to right | brexx:prefix.rexx |  | INTERPRET removed |
| 6 | `x = +'1E+2'` → `x` | `'100'` | p.14, 139-142 derived: "+term" is "0+term", result formatted as a number | brexx:prefix.rexx |  |  |
| 7 | `x = +.1E2` → `x` | `'10'` | p.14, 139-142 derived: "+term" is "0+term", result formatted as a number | brexx:prefix.rexx |  |  |
| 8 | `x = +5` → `x` | `'5'` | p.14, 139-142 derived: "+term" is "0+term", result formatted as a number | brexx:prefix.rexx |  |  |
| 9 | `x = -2**2` → `x` | `'4'` | p.16 derived: prefix minus binds tighter than ** | brexx:prefix.rexx |  |  |
| 10 | `x = 3 - -a` → `x` | `'4'` | p.16 derived: prefix minus binds tighter than ** | brexx:prefix.rexx |  |  |
| 11 | `v = '1.50'; x = -v` → `x` | `'-1.50'` | p.142 derived: "If either number is zero, the other number ... is used as the result (with sign adjustment)"; trailing zeros kept (p.139, 141) | brexx:prefix.rexx | -1.5 |  |
| 12 | `v` | `'1.50'` | p.142 derived: the operand is not changed | brexx:prefix.rexx |  |  |
| 13 | `x = --a` → `x` | `'1'` | p.11, 16 derived: "--" is two prefix operators; 1988 REXX has no line comments | brexx:prefix.rexx |  | BREXX comment says "--" starts a line comment in BREXX |
| 14 | `x = -' 5 '` → `x` | `'-5'` | p.14, 139-142 derived: prefix arithmetic, zero is "0" | spec |  |  |
| 15 | `x = +'1.50'` → `x` | `'1.50'` | p.14, 139-142 derived: prefix arithmetic, zero is "0" | spec |  |  |
| 16 | `x = -'0.00'` → `x` | `'0'` | p.14, 139-142 derived: prefix arithmetic, zero is "0" | spec |  |  |
| 17 | `x = \\0` → `x` | `'0'` | p.14, 139-142 derived: prefix arithmetic, zero is "0" | spec |  |  |
| 18 | `x = -3**2` → `x` | `'9'` | p.16 example | spec |  |  |
| 19 | `x = -a + 5` → `x` | `'4'` | p.16 derived: prefix before arithmetic | spec |  |  |
| 20 | `x = \0 + 1` → `x` | `'2'` | p.16 derived: prefix before arithmetic | spec |  |  |

### EVALORD (33 cases: 6 manual examples, 27 derived)

Host run, bytecode VM: ran to the end; FAIL: 8.  
Token-walk (`REXX370_BYTECODE=0`): died after case 20 (run_rc 24); FAIL: 8.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `3+2*5` | `'13'` | p.16 example | spec |  |  |
| 2 | `-3**2` | `'9'` | p.16 example | spec |  |  |
| 3 | `a = 3` → `a+5` | `'8'` | p.17 example | spec |  |  |
| 4 | `a-4*2` | `'-5'` | p.17 example | spec |  |  |
| 5 | `a/2` | `'1.5'` | p.17 example | spec |  |  |
| 6 | `0.5**2` | `'0.25'` | p.17 example | spec |  |  |
| 7 | `2*3**2` | `'18'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 8 | `2**3**2` | `'64'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 9 | `10-2-3` | `'5'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 10 | `12/2/3` | `'2'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 11 | `7//2*3` | `'3'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 12 | `1 \|\| 2+3` | `'15'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 13 | `2+3 \|\| 4` | `'54'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 14 | `'a' 'b' = 'a b'` | `'1'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 15 | `1 = 1 & 2 = 2` | `'1'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 16 | `1 \| 0 & 0` | `'1'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 17 | `0 & 0 \| 1` | `'1'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 18 | `1 && 1 & 0` | `'1'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 19 | `1 \| 1 && 1` | `'0'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 20 | `1 + 1 \|\| 1` | `'21'` | p.16 derived: precedence table; equal precedence left to right | spec |  | 8: 2**3**2 left to right |
| 21 | `$ = ''; do j = 0 to 7; $ = $ fff(j); end` → `$` | `' 12 12 12 12 12 12 12 12'` | p.16 derived: "individual terms are evaluated from left to right ... as soon as they are encountered" | brexx:evalord.rexx:1-2 |  |  |
| 22 | `j` | `'8'` | p.16 derived: "individual terms are evaluated from left to right ... as soon as they are encountered" | brexx:evalord.rexx:1-2 |  |  |
| 23 | `a = 1; b = 2` → `a + (b * setab())` | `'3'` | p.16 derived: as above | brexx:evalord.rexx:3-4 |  |  |
| 24 | `x = 1` → `x + setx()` | `'1'` | p.16 derived: as above | brexx:evalord.rexx:3-4 |  |  |
| 25 | `x = 1` → `args(x, setx())` | `'1 0'` | p.33, 71 derived: arguments evaluated in turn from left to right | brexx:evalord.rexx:5-6 |  |  |
| 26 | `x = 1; call args x, setx()` → `result` | `'1 0'` | p.33, 71 derived: arguments evaluated in turn from left to right | brexx:evalord.rexx:5-6 |  |  |
| 27 | `twice(3) + twice(4)` | `'14'` | p.16 derived: terms left to right | brexx:evalord.rexx:9 |  |  |
| 28 | `log = ''` → `args(lg('a'), lg('b')) log` | `'a b ab'` | p.71 derived: arguments evaluated in turn from left to right | spec |  |  |
| 29 | `cnt = 0; x = 0 & bump()` → `cnt` | `'1'` | p.13 derived: "Expressions are always wholly evaluated" | spec |  |  |
| 30 | `x = 1 \| bump()` → `cnt` | `'2'` | p.13 derived: "Expressions are always wholly evaluated" | spec |  |  |
| 31 | `x = 'a'; x = x \|\| setxs()` → `x` | `'ab'` | p.16 derived: terms left to right | brexx:evalord.rexx:10-12 |  |  |
| 32 | `s = ''; do i = 1 to 3; s = s \|\| i \|\| one(); end` → `s i` | `'112131 4'` | p.16 derived: terms left to right | brexx:evalord.rexx:10-12 |  |  |
| 33 | `n = 0; do k = 1 to 5 by 2; n = n + one(); end` → `n k` | `'3 7'` | p.16 derived: terms left to right | brexx:evalord.rexx:10-12 |  |  |

### CONCAT (26 cases: 4 manual examples, 22 derived)

Host run, bytecode VM: ran to the end; no FAIL.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; no FAIL.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `fred = 37.4` → `Fred"%"` | `'37.4%'` | p.14 example | spec |  |  |
| 2 | `drop today is it xxx; day = 'Monday'` → `Today is Day` | `'TODAY IS Monday'` | p.17 example | spec |  |  |
| 3 | `'If it is' day` | `'If it is Monday'` | p.17 example | spec |  |  |
| 4 | `b = 2` → `'REPEAT'   B + 3` | `'REPEAT 5'` | p.11 derived: the example clause 'REPEAT'  B + 3 keeps one blank | spec |  |  |
| 5 | `'a'     'b'` | `'a b'` | p.9, 11 derived: multiple blanks become one; blanks next to operators removed | spec |  |  |
| 6 | `'a' \|\| 'b'` | `'ab'` | p.9, 11 derived: multiple blanks become one; blanks next to operators removed | spec |  |  |
| 7 | `'a'\|\|'b'` | `'ab'` | p.9, 11 derived: multiple blanks become one; blanks next to operators removed | spec |  |  |
| 8 | `('a') 'b'` | `'a b'` | p.11 derived: a blank outside a parenthesis stays unless next to another special character | spec |  |  |
| 9 | `('a')('b')` | `'ab'` | p.11 derived: a blank outside a parenthesis stays unless next to another special character | spec |  |  |
| 10 | `('a') ('b')` | `'a b'` | p.11 derived: a blank outside a parenthesis stays unless next to another special character | spec |  |  |
| 11 | `'a' ('b')` | `'a b'` | p.11 derived: a blank outside a parenthesis stays unless next to another special character | spec |  |  |
| 12 | `(('a'))` | `'a'` | p.11 derived: a blank outside a parenthesis stays unless next to another special character | spec |  |  |
| 13 | `x = 'You can use a comma' 'to continue this clause.'` → `x` | `'You can use a comma to continue this clause.'` | p.12 example | spec |  |  |
| 14 | `v = 'x'` → `v'y'` | `'xy'` | p.13 derived: abuttal of symbol and string | spec |  |  |
| 15 | `'y'v` | `'yx'` | p.13 derived: abuttal of symbol and string | spec |  |  |
| 16 | `v\|\|v v` | `'xx x'` | p.13 derived: abuttal of symbol and string | spec |  |  |
| 17 | `1.50 'x'` | `'1.50 x'` | p.19 derived: constant symbol value = its characters | spec |  |  |
| 18 | `007 \|\| ''` | `'007'` | p.19 derived: constant symbol value = its characters | spec |  |  |
| 19 | `'' ''` | `' '` | p.13 derived: null strings | spec |  |  |
| 20 | `'' \|\| ''` | `''` | p.13 derived: null strings | spec |  |  |
| 21 | `length('' 'a' '')` | `'3'` | p.13 derived: null strings | spec |  |  |
| 22 | `x = 'blah'.` → `x` | `'blah.'` | p.10, 19 derived: "." is a constant symbol | brexx:dotlit.rexx:1-5 |  | INTERPRET removed |
| 23 | `x = 'a' .` → `x` | `'a .'` | p.10, 19 derived: "." is a constant symbol | brexx:dotlit.rexx:1-5 |  | INTERPRET removed |
| 24 | `x = .` → `x` | `'.'` | p.10, 19 derived: "." is a constant symbol | brexx:dotlit.rexx:1-5 |  | INTERPRET removed |
| 25 | `x = . 'b'` → `x` | `'. b'` | p.10, 19 derived: "." is a constant symbol | brexx:dotlit.rexx:1-5 |  | INTERPRET removed |
| 26 | `x = 'a'\|\|.` → `x` | `'a.'` | p.10, 19 derived: "." is a constant symbol | brexx:dotlit.rexx:1-5 |  | INTERPRET removed |

### CONCATX (2 cases: 1 manual examples, 1 derived)

Host run, bytecode VM: died before case 1 (run_rc 21); no FAIL.  
Token-walk (`REXX370_BYTECODE=0`): died before case 1 (run_rc 20); no FAIL.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `drop xxx x1` → `'!'xxx'!'` | `'!XXX!'` | p.17 example | spec |  | split: rexx370 lexes '!'xxx as a hex string (run_rc 21) |
| 2 | `'a'x1` | `'aX1'` | p.9 derived: "the X cannot be part of a longer symbol" | spec |  |  |

### SYMBOL (30 cases: 13 manual examples, 17 derived)

Host run, bytecode VM: ran to the end; FAIL: 2, 4, 12, 19, 20, 22, 27, 29, 30.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; FAIL: 2, 4, 12, 19, 20, 22, 29, 30.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `drop A.3; j = 3` → `symbol('J')` | `'VAR'` | p.101 example | spec |  | = brexx:symbol.rexx:1-5 |
| 2 | `symbol(J)` | `'LIT'` | p.101 example | spec |  | = brexx:symbol.rexx:1-5 |
| 3 | `symbol('a.j')` | `'LIT'` | p.101 example | spec |  | = brexx:symbol.rexx:1-5 |
| 4 | `symbol(2)` | `'LIT'` | p.101 example | spec |  | = brexx:symbol.rexx:1-5 |
| 5 | `symbol('*')` | `'BAD'` | p.101 example | spec |  | = brexx:symbol.rexx:1-5 |
| 6 | `alpha = 'foobar'; beta = 'foobar'; gamma.foobar = 'foobar'; omega = 'FOOBAR'` → `symbol('HEPP')` | `'LIT'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 7 | `symbol('ALPHA')` | `'VAR'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 8 | `symbol('Un*x')` | `'BAD'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 9 | `symbol('gamma.delta')` | `'LIT'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 10 | `symbol('gamma.FOOBAR')` | `'VAR'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 11 | `symbol('gamma.alpha')` | `'LIT'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 12 | `symbol('gamma.omega')` | `'VAR'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 13 | `symbol('Un*x.gamma')` | `'BAD'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 14 | `symbol('!!')` | `'LIT'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 15 | `symbol('')` | `'BAD'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 16 | `symbol(x2c('00'))` | `'BAD'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 17 | `symbol('foo-bar')` | `'BAD'` | p.101, 10, 19-20 derived: BAD = not a valid symbol, VAR = assigned, else LIT; compound substitution | brexx:symbol.rexx:6-17 |  | setup by assignment instead of PARSE; 16 uses X2C('00') for '00'x |
| 18 | `symbol('@#$.!?_')` | `'LIT'` | p.10, 19, 101 derived: symbol characters; constant symbols are LIT | spec |  |  |
| 19 | `symbol('12e5')` | `'LIT'` | p.10, 19, 101 derived: symbol characters; constant symbols are LIT | spec |  |  |
| 20 | `symbol('17.3E-12')` | `'LIT'` | p.10, 19, 101 derived: symbol characters; constant symbols are LIT | spec |  |  |
| 21 | `symbol('a b')` | `'BAD'` | p.10, 19, 101 derived: symbol characters; constant symbols are LIT | spec |  |  |
| 22 | `x = 12e5` → `x` | `'12E5'` | p.18-19 example (constant/simple symbols; value = the symbol in uppercase) | spec |  |  |
| 23 | `x = 3D` → `x` | `'3D'` | p.18-19 example (constant/simple symbols; value = the symbol in uppercase) | spec |  |  |
| 24 | `x = .12345` → `x` | `'.12345'` | p.18-19 example (constant/simple symbols; value = the symbol in uppercase) | spec |  |  |
| 25 | `drop freda; fred = freda` → `fred` | `'FREDA'` | p.18-19 example (constant/simple symbols; value = the symbol in uppercase) | spec |  |  |
| 26 | `Whatagoodidea?` | `'WHATAGOODIDEA?'` | p.18-19 example (constant/simple symbols; value = the symbol in uppercase) | spec |  |  |
| 27 | `drop a b c fred; drop a. c. x.; a=3; b=4; c='Fred'; a.b='Fred'; a.fred=5; a.c='Bill'; c.c=a.fred; x.a.b='Annie'; v = a b c a.a a.b a.c c.a a.fred x.a.4` → `v` | `'3 4 Fred A.3 Fred Bill C.3 5 Annie'` | p.20 example | spec |  | FRED dropped first (case 25 assigned it), as in the manual setup |
| 28 | `hole. = 'empty'; hole.9 = 'full'` → `hole.1 hole.mouse hole.9` | `'empty empty full'` | p.21 example | spec |  |  |
| 29 | `symbol('hole.xyz')` | `'VAR'` | p.21 derived: a stem assignment gives all possible compound variables a value | spec |  |  |
| 30 | `total. = 0; null = ''; total.null = total.null + 5` → `total. total.null` | `'0 5'` | p.21 example | spec |  |  |

### VALUE (23 cases: 4 manual examples, 19 derived)

Host run, bytecode VM: ran to the end; FAIL: 15, 17, 18.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; FAIL: 15, 17, 18.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `drop A3; A33 = 7; J = 3; fred = 'J'` → `value('fred')` | `'J'` | p.105 example | spec |  |  |
| 2 | `value(fred)` | `'3'` | p.105 example | spec |  |  |
| 3 | `value('a'j)` | `'A3'` | p.105 example | spec |  |  |
| 4 | `value('a'j\|\|j)` | `'7'` | p.105 example | spec |  |  |
| 5 | `value('j')` | `j` | p.105 derived: note: fred=VALUE('j') is identical to fred=j | spec |  |  |
| 6 | `drop A3; A33 = 7; K = 3; fred = 'K'; list.5 = '?'` → `value('a'k)` | `'A3'` | p.105 derived: as the examples | brexx:value.rexx:1-4 |  |  |
| 7 | `value('a'k\|\|k)` | `'7'` | p.105 derived: as the examples | brexx:value.rexx:1-4 |  |  |
| 8 | `value('fred')` | `'K'` | p.105 derived: as the examples | brexx:value.rexx:1-4 |  |  |
| 9 | `value(fred)` | `'3'` | p.105 derived: as the examples | brexx:value.rexx:1-4 |  |  |
| 10 | `value('LIST.'k)` | `'LIST.3'` | p.105, 20 derived: LIST.3 is not assigned, its value is its derived name | brexx:value.rexx:7 | ? | BREXX set K=3 and LIST.5, so LIST.3 is looked up |
| 11 | `drop a b c; drop x.; x.a = 'asdf'; x.b = 'foo'; x.c = 'A'; a = 'B'; b = 'C'; c = 'A'; xyzzy = 'foo'` → `value('a')` | `'B'` | p.105 derived: uppercase + compound substitution | brexx:value.rexx:8-17 |  |  |
| 12 | `value(a)` | `'C'` | p.105 derived: uppercase + compound substitution | brexx:value.rexx:8-17 |  |  |
| 13 | `value(c)` | `'B'` | p.105 derived: uppercase + compound substitution | brexx:value.rexx:8-17 |  |  |
| 14 | `value('c')` | `'A'` | p.105 derived: uppercase + compound substitution | brexx:value.rexx:8-17 |  |  |
| 15 | `value('x.A')` | `'foo'` | p.105 derived: uppercase + compound substitution | brexx:value.rexx:8-17 |  |  |
| 16 | `value(x.B)` | `'B'` | p.105 derived: uppercase + compound substitution | brexx:value.rexx:8-17 |  |  |
| 17 | `value('x.B')` | `'A'` | p.105 derived: uppercase + compound substitution | brexx:value.rexx:8-17 |  |  |
| 18 | `value('x.'\|\|a)` | `'A'` | p.105 derived: uppercase + compound substitution | brexx:value.rexx:8-17 |  |  |
| 19 | `value(value(x.b))` | `'C'` | p.105 derived: uppercase + compound substitution | brexx:value.rexx:8-17 |  |  |
| 20 | `value('xyzzy')` | `'foo'` | p.105 derived: uppercase + compound substitution | brexx:value.rexx:8-17 |  |  |
| 21 | `drop never` → `value('never')` | `'NEVER'` | p.18, 105 derived: uninitialized symbol | spec |  |  |
| 22 | `value('x.never')` | `'X.NEVER'` | p.18, 105 derived: uninitialized symbol | spec |  |  |
| 23 | `k = 5` → `value('LIST.'k)` | `'?'` | p.105 derived: compound substitution | spec |  | the LIST.5 case BREXX meant |

### DATE (18 cases: 0 manual examples, 18 derived)

Host run, bytecode VM: ran to the end; FAIL: 13, 14.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; FAIL: 13, 14.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `ok = length(date('S')) = 8 & datatype(date('S'),'W')` → `ok` | `'1'` | p.86 derived: Sorted yyyymmdd | spec |  |  |
| 2 | `ok = date() == date('N')` → `ok` | `'1'` | p.85-86 derived: default = Normal "dd mon yyyy", no leading zero, month = first 3 letters of Month | spec |  |  |
| 3 | `ok = words(date()) = 3` → `ok` | `'1'` | p.85-86 derived: default = Normal "dd mon yyyy", no leading zero, month = first 3 letters of Month | spec |  |  |
| 4 | `ok = word(date('N'),1) == substr(date('S'),7,2) + 0` → `ok` | `'1'` | p.85-86 derived: default = Normal "dd mon yyyy", no leading zero, month = first 3 letters of Month | spec |  |  |
| 5 | `ok = word(date('N'),2) == left(date('M'),3)` → `ok` | `'1'` | p.85-86 derived: default = Normal "dd mon yyyy", no leading zero, month = first 3 letters of Month | spec |  |  |
| 6 | `ok = word(date('N'),3) == left(date('S'),4)` → `ok` | `'1'` | p.85-86 derived: default = Normal "dd mon yyyy", no leading zero, month = first 3 letters of Month | spec |  |  |
| 7 | `ok = date('E') == substr(date('S'),7,2)'/'substr(date('S'),5,2)'/'\|\| substr(date('S'),3,2)` → `ok` | `'1'` | p.86 derived: European dd/mm/yy, Usa mm/dd/yy | spec |  |  |
| 8 | `ok = date('U') == substr(date('S'),5,2)'/'substr(date('S'),7,2)'/'\|\| substr(date('S'),3,2)` → `ok` | `'1'` | p.86 derived: European dd/mm/yy, Usa mm/dd/yy | spec |  |  |
| 9 | `ok = length(date('J')) = 5 & left(date('J'),2) == substr(date('S'),3,2)` → `ok` | `'1'` | p.86 derived: Julian yyddd, Days ddd (no leading zeros) | spec |  |  |
| 10 | `ok = right(date('J'),3) == right(date('D'),3,'0')` → `ok` | `'1'` | p.86 derived: Julian yyddd, Days ddd (no leading zeros) | spec |  |  |
| 11 | `ok = datatype(date('D'),'W') & left(date('D'),1) \== '0'` → `ok` | `'1'` | p.86 derived: Julian yyddd, Days ddd (no leading zeros) | spec |  |  |
| 12 | `m = 'January February March April May June July August September' 'October November December'; ok = date('M') == word(m, substr(date('S'),5,2))` → `ok` | `'1'` | p.86 derived: Month: full English name | spec |  |  |
| 13 | `ok = date('B') - date('D') + 1 ==  (left(date('S'),4)-1)*365 + (left(date('S'),4)-1)%4 -  (left(date('S'),4)-1)%100 + (left(date('S'),4)-1)%400` → `ok` | `'1'` | p.85-86 derived: Basedate: complete days since 1 Jan 0001, Gregorian (note p.86) | spec |  | formula checked against the manual example 27 Aug 1988: B=725975, D=240 |
| 14 | `w = 'Monday Tuesday Wednesday Thursday Friday Saturday Sunday'; ok = date('W') == word(w, date('B')//7 + 1)` → `ok` | `'1'` | p.85 derived: "DATE(B)//7 ... 0 is Monday" | spec |  |  |
| 15 | `ok = datatype(date('B'),'W') & left(date('B'),1) \== '0'` → `ok` | `'1'` | p.85 derived: "DATE(B)//7 ... 0 is Monday" | spec |  |  |
| 16 | `ok = date('Sorted') == date('S') & date('Weekday') == date('W')` → `ok` | `'1'` | p.85 derived: "only the capitalized letter is needed" | spec |  |  |
| 17 | `ok = date('Normal') == date() & date('Basedate') == date('B')` → `ok` | `'1'` | p.85 derived: "only the capitalized letter is needed" | spec |  |  |
| 18 | `ok = date('s') == date('S') & date('w') == date('W')` → `ok` | `'1'` | p.77 derived: a suboption letter "can be in upper- or lowercase" | spec |  |  |

### DATEO (2 cases: 0 manual examples, 2 derived)

Host run, bytecode VM: died before case 1 (run_rc 24); no FAIL.  
Token-walk (`REXX370_BYTECODE=0`): died before case 1 (run_rc 20); no FAIL.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `ok = date('O') == substr(date('S'),3,2)'/'substr(date('S'),5,2)'/'\|\| substr(date('S'),7,2)` → `ok` | `'1'` | p.86 derived: Ordered yy/mm/dd | spec |  | split: DATE('O') ends rexx370 (run_rc 24) |
| 2 | `ok = date('Ordered') == date('O')` → `ok` | `'1'` | p.86 derived: Ordered yy/mm/dd | spec |  | split: DATE('O') ends rexx370 (run_rc 24) |

### DATEC (3 cases: 0 manual examples, 3 derived)

Host run, bytecode VM: died before case 1 (run_rc 24); no FAIL.  
Token-walk (`REXX370_BYTECODE=0`): died before case 1 (run_rc 20); no FAIL.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `ok = date('B') - date('C') + 1 ==  (left(date('S'),2)*100-1)*365 + (left(date('S'),2)*100-1)%4 -  (left(date('S'),2)*100-1)%100 + (left(date('S'),2)*100-1)%400` → `ok` | `'1'` | p.86 derived: Century: days incl. today since 1 Jan of the last year divisible by 100 | spec |  | split: DATE('C') ends rexx370 (run_rc 24) |
| 2 | `ok = datatype(date('C'),'W') & left(date('C'),1) \== '0'` → `ok` | `'1'` | p.86 derived: Century: days incl. today since 1 Jan of the last year divisible by 100 | spec |  | split: DATE('C') ends rexx370 (run_rc 24) |
| 3 | `ok = date('C') >= date('D') & date('Century') == date('C')` → `ok` | `'1'` | p.86 derived: Century: days incl. today since 1 Jan of the last year divisible by 100 | spec |  | split: DATE('C') ends rexx370 (run_rc 24) |

### TIME (15 cases: 1 manual examples, 14 derived)

Host run, bytecode VM: ran to the end; FAIL: 1, 11.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; FAIL: 1.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `time('E')` | `'0'` | p.103 example (first call returns 0) | spec |  |  |
| 2 | `ok = length(time()) = 8 & time() == time('N')` → `ok` | `'1'` | p.102 derived: default hh:mm:ss = Normal | spec |  |  |
| 3 | `ok = substr(time(),3,1) \|\| substr(time(),6,1) == '::'` → `ok` | `'1'` | p.102 derived: default hh:mm:ss = Normal | spec |  |  |
| 4 | `ok = time('H') == left(time('N'),2) + 0` → `ok` | `'1'` | p.102 derived: the relations the examples show (1014 = 54 + 60*16) | spec |  |  |
| 5 | `ok = time('M') == left(time('N'),2) * 60 + substr(time('N'),4,2)` → `ok` | `'1'` | p.102 derived: the relations the examples show (1014 = 54 + 60*16) | spec |  |  |
| 6 | `ok = time('S') == left(time('N'),2) * 3600 + substr(time('N'),4,2) * 60 + substr(time('N'),7,2)` → `ok` | `'1'` | p.102 derived: the relations the examples show (1014 = 54 + 60*16) | spec |  |  |
| 7 | `ok = length(time('L')) = 15 & left(time('L'),8) == time('N')` → `ok` | `'1'` | p.102 derived: Long hh:mm:ss.uuuuuu | spec |  |  |
| 8 | `ok = substr(time('L'),9,1) == '.' & datatype(right(time('L'),6),'W')` → `ok` | `'1'` | p.102 derived: Long hh:mm:ss.uuuuuu | spec |  |  |
| 9 | `h = '12 1 2 3 4 5 6 7 8 9 10 11'; ok = time('C') == word(h, left(time('N'),2)//12 + 1)':' \|\| substr(time('N'),4,2) \|\| substr('ampm', 1+2*(left(time('N'),2)>11),2)` → `ok` | `'1'` | p.102 derived: Civil: hour 1-12 without leading zero, am/pm, minutes truncated | spec |  |  |
| 10 | `e = time('E')` → `datatype(e,'N') & e >= 0` | `'1'` | p.102-103 derived: Elapsed/Reset: a number; one expression = one time stamp | spec |  | 11 fails only sometimes (the clock is not frozen per expression) |
| 11 | `ok = time('E') == time('E')` → `ok` | `'1'` | p.102-103 derived: Elapsed/Reset: a number; one expression = one time stamp | spec |  | 11 fails only sometimes (the clock is not frozen per expression) |
| 12 | `r = time('R')` → `datatype(r,'N') & r >= 0` | `'1'` | p.102-103 derived: Elapsed/Reset: a number; one expression = one time stamp | spec |  | 11 fails only sometimes (the clock is not frozen per expression) |
| 13 | `ok = time('Hours') == time('H') & time('Normal') == time()` → `ok` | `'1'` | p.102 derived: "only the capitalized letter is needed" | spec |  |  |
| 14 | `ok = time('Minutes') == time('M') & time('Seconds') == time('S')` → `ok` | `'1'` | p.102 derived: "only the capitalized letter is needed" | spec |  |  |
| 15 | `ok = time('h') == time('H') & time('n') == time('N')` → `ok` | `'1'` | p.77 derived: a suboption letter "can be in upper- or lowercase" | spec |  |  |

### TRACE (16 cases: 0 manual examples, 16 derived)

Host run, bytecode VM: ran to the end; FAIL: 10, 16.  
Token-walk (`REXX370_BYTECODE=0`): ran to the end; FAIL: 10, 16.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `trace()` | `'N'` | p.65 derived: Normal "is the default setting" | brexx:trace.rexx:1 |  |  |
| 2 | `trace('O')` | `'N'` | p.103 derived: returns the trace actions in effect, then sets | brexx:trace.rexx:2 |  |  |
| 3 | `trace()` | `'O'` | p.103, 65 derived: as above | spec |  | after 7 the setting is restored with TRACE('Normal') unchecked |
| 4 | `trace('N')` | `'O'` | p.103, 65 derived: as above | spec |  | after 7 the setting is restored with TRACE('Normal') unchecked |
| 5 | `trace('Error')` | `'N'` | p.103, 65 derived: as above | spec |  | after 7 the setting is restored with TRACE('Normal') unchecked |
| 6 | `trace()` | `'E'` | p.103, 65 derived: as above | spec |  | after 7 the setting is restored with TRACE('Normal') unchecked |
| 7 | `trace('F')` | `'E'` | p.103, 65 derived: as above | spec |  | after 7 the setting is restored with TRACE('Normal') unchecked |
| 8 | `x = trace('Normal')` → `trace()` | `'N'` | p.103, 65 derived: as above | spec |  | after 7 the setting is restored with TRACE('Normal') unchecked |
| 9 | `trace off` → `trace()` | `'O'` | p.64-66 derived: TRACE instruction; no option restores N (tip 1, p.66) | spec |  |  |
| 10 | `trace` → `trace()` | `'N'` | p.64-66 derived: TRACE instruction; no option restores N (tip 1, p.66) | spec |  |  |
| 11 | `trace value 'E'` → `trace()` | `'E'` | p.64-66 derived: TRACE instruction; no option restores N (tip 1, p.66) | spec |  |  |
| 12 | `trace Normal` → `trace()` | `'N'` | p.64-66 derived: TRACE instruction; no option restores N (tip 1, p.66) | spec |  |  |
| 13 | `trace('e')` | `'N'` | p.77 derived: a suboption letter "can be in upper- or lowercase" | spec |  |  |
| 14 | `trace()` | `'E'` | p.77 derived: a suboption letter "can be in upper- or lowercase" | spec |  |  |
| 15 | `x = trace('N'); call sub` → `result` | `'O'` | p.34, 67 derived: "Trace actions are automatically saved across subroutine and function calls" | spec |  |  |
| 16 | `trace()` | `'N'` | p.34, 67 derived: "Trace actions are automatically saved across subroutine and function calls" | spec |  |  |

### ERRORTXT (51 cases: 2 manual examples, 49 derived)

Host run, bytecode VM: died after case 48 (run_rc 24); no FAIL.  
Token-walk (`REXX370_BYTECODE=0`): died after case 48 (run_rc 20); no FAIL.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `errortext(16)` | `'Label not found'` | p.89 example | spec |  | 1 = brexx:errorte.rexx:1 |
| 2 | `errortext(60)` | `''` | p.89 example | spec |  | 1 = brexx:errorte.rexx:1 |
| 3 | `errortext(3)` | `'Program is unreadable'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 4 | `errortext(4)` | `'Program interrupted'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 5 | `errortext(5)` | `'Machine storage exhausted'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 6 | `errortext(7)` | `'WHEN or OTHERWISE expected'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 7 | `errortext(8)` | `'Unexpected THEN or ELSE'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 8 | `errortext(9)` | `'Unexpected WHEN or OTHERWISE'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 9 | `errortext(10)` | `'Unexpected or unmatched END'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 10 | `errortext(11)` | `'Control stack full'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 11 | `errortext(12)` | `'Clause > 500 characters'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 12 | `errortext(13)` | `'Invalid character in data'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 13 | `errortext(14)` | `'Incomplete DO/SELECT/IF'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 14 | `errortext(15)` | `'Invalid hex constant'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 15 | `errortext(17)` | `'Unexpected PROCEDURE'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 16 | `errortext(18)` | `'THEN expected'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 17 | `errortext(19)` | `'String or symbol expected'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 18 | `errortext(20)` | `'Symbol expected'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 19 | `errortext(21)` | `'Invalid data on end of clause'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 20 | `errortext(22)` | `'Invalid character string'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 21 | `errortext(23)` | `'Invalid SBCS/DBCS mixed string'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 22 | `errortext(24)` | `'Invalid TRACE request'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 23 | `errortext(25)` | `'Invalid sub-keyword found'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 24 | `errortext(26)` | `'Invalid whole number'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 25 | `errortext(27)` | `'Invalid DO syntax'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 26 | `errortext(28)` | `'Invalid LEAVE or ITERATE'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 27 | `errortext(29)` | `'Environment name too long'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 28 | `errortext(30)` | `'Name or string > 250 characters'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 29 | `errortext(32)` | `'Invalid use of stem'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 30 | `errortext(33)` | `'Invalid expression result'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 31 | `errortext(34)` | `'Logical value not 0 or 1'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 32 | `errortext(35)` | `'Invalid expression'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 33 | `errortext(38)` | `'Invalid template or pattern'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 34 | `errortext(39)` | `'Evaluation stack overflow'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 35 | `errortext(40)` | `'Incorrect call to routine'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 36 | `errortext(41)` | `'Bad arithmetic conversion'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 37 | `errortext(42)` | `'Arithmetic overflow/underflow'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 38 | `errortext(43)` | `'Routine not found'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 39 | `errortext(44)` | `'Function did not return data'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 40 | `errortext(45)` | `'No data specified on function RETURN'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 41 | `errortext(48)` | `'Failure in system service'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 42 | `errortext(49)` | `'Interpreter failure'` | p.395-403 derived: the message text after "line nn:"; mapping inferred from the ERRORTEXT(16) example | spec |  | 10 = brexx:errorte.rexx:3 (errortext(10)), 40 = :4 |
| 43 | `errortext(1)` | `''` | p.89, 395 derived: in range 0-99 but not a defined error number (3-45, 48, 49) | spec |  | errortext(1) = brexx:errorte.rexx:5, errortext(90) = :2 |
| 44 | `errortext(2)` | `''` | p.89, 395 derived: in range 0-99 but not a defined error number (3-45, 48, 49) | spec |  | errortext(1) = brexx:errorte.rexx:5, errortext(90) = :2 |
| 45 | `errortext(46)` | `''` | p.89, 395 derived: in range 0-99 but not a defined error number (3-45, 48, 49) | spec |  | errortext(1) = brexx:errorte.rexx:5, errortext(90) = :2 |
| 46 | `errortext(47)` | `''` | p.89, 395 derived: in range 0-99 but not a defined error number (3-45, 48, 49) | spec |  | errortext(1) = brexx:errorte.rexx:5, errortext(90) = :2 |
| 47 | `errortext(50)` | `''` | p.89, 395 derived: in range 0-99 but not a defined error number (3-45, 48, 49) | spec |  | errortext(1) = brexx:errorte.rexx:5, errortext(90) = :2 |
| 48 | `errortext(90)` | `''` | p.89, 395 derived: in range 0-99 but not a defined error number (3-45, 48, 49) | spec |  | errortext(1) = brexx:errorte.rexx:5, errortext(90) = :2 |
| 49 | `errortext(0)` | `''` | p.89 derived: range 0-99 | spec |  | rexx370 ends at case 49 (run_rc 24) |
| 50 | `errortext(99)` | `''` | p.89 derived: range 0-99 | spec |  | rexx370 ends at case 49 (run_rc 24) |
| 51 | `errortext(' 16 ')` | `'Label not found'` | p.11, 89 derived: a number may have leading/trailing blanks | spec |  |  |

### ADDRESS (12 cases: 0 manual examples, 12 derived)

Host run, bytecode VM: died after case 11 (run_rc 20); FAIL: 3, 7, 8, 9, 10.  
Token-walk (`REXX370_BYTECODE=0`): died after case 11 (run_rc 20); FAIL: 3, 7, 8, 9, 10.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `a0 = address()` → `wordpos(a0, 'TSO MVS') > 0` | `'1'` | p.22, 24 derived: initial environment TSO (TSO/E) or MVS (non-TSO/E) | brexx:address.rexx:1 |  |  |
| 2 | `address LINK` → `address()` | `'LINK'` | p.28 derived: lasting change; no argument switches back | spec |  |  |
| 3 | `address` → `address()` | `a0` | p.28 derived: lasting change; no argument switches back | spec |  |  |
| 4 | `address` → `address()` | `'LINK'` | p.28 derived: lasting change; no argument switches back | spec |  |  |
| 5 | `address value 'AT'\|\|'TACH'` → `address()` | `'ATTACH'` | p.28 derived: VALUE form | spec |  |  |
| 6 | `address 'TSO'` → `address()` | `'TSO'` | p.28 derived: environment is a literal string or a symbol taken as a constant | spec |  |  |
| 7 | `address link` → `address()` | `'LINK'` | p.28 derived: environment is a literal string or a symbol taken as a constant | spec |  |  |
| 8 | `call sub` → `result` | `'LINK ATTACH'` | p.29, 34 derived: ADDRESS settings saved across routine calls | spec |  |  |
| 9 | `address()` | `'LINK'` | p.29, 34 derived: ADDRESS settings saved across routine calls | spec |  |  |
| 10 | `address` → `address()` | `'TSO'` | p.29, 34 derived: ADDRESS settings saved across routine calls | spec |  |  |
| 11 | `address value a0` → `address()` | `a0` | p.29, 34 derived: ADDRESS settings saved across routine calls | spec |  |  |
| 12 | `address ('M'\|\|'VS')` → `address()` | `'MVS'` | p.28 derived: "VALUE may be omitted" when expression1 starts with a special character | spec |  | manual example ADDRESS ('ENVIR'\|\|number); rexx370 ends here (run_rc 20) |

### SOURCELN (5 cases: 0 manual examples, 5 derived)

Host run, bytecode VM: died after case 4 (run_rc 24); no FAIL.  
Token-walk (`REXX370_BYTECODE=0`): died after case 4 (run_rc 20); no FAIL.

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `sourceline()` | `'40'` | p.99 derived: SOURCELINE() = number of the final line; SOURCELINE(n) = line n | spec |  | expected values generated from the file itself; 2-4 compare STRIP(...,'T'): on MVS the member is FB 80 and p.99 does not settle whether the record padding belongs to the line |
| 2 | `l1 = '/* REXX -- SOURCELINE conformance, SC28-1883-0 p.99'; l1 = l1 \|\| copies(' ', 68 - length(l1)) \|\| '*/'` → `strip(sourceline(1),'T')` | `l1` | p.99 derived: SOURCELINE() = number of the final line; SOURCELINE(n) = line n | spec |  | expected values generated from the file itself; 2-4 compare STRIP(...,'T'): on MVS the member is FB 80 and p.99 does not settle whether the record padding belongs to the line |
| 3 | `mk = 1; mkl = 'mk = 1 /* this is the marker line */'` → `strip(sourceline(14),'T')` | `mkl` | p.99 derived: SOURCELINE() = number of the final line; SOURCELINE(n) = line n | spec |  | expected values generated from the file itself; 2-4 compare STRIP(...,'T'): on MVS the member is FB 80 and p.99 does not settle whether the record padding belongs to the line |
| 4 | `strip(sourceline(sourceline()),'T')` | `'return'` | p.99 derived: the final line | spec |  |  |
| 5 | `sourceline(' 1 ')` | `sourceline(1)` | p.99, 11 derived: n is a whole number, blanks allowed | spec |  | rexx370 ends here (run_rc 24) |

## ERROR-CASEs (specified outcome is an error; wait for SIGNAL ON SYNTAX)

| member | expression | specified outcome | spec | source |
|---|---|---|---|---|
| PREFIX | `+'abc'` | error 41, bad arithmetic conversion | p.14, 402 | brexx:prefix.rexx (last case) |
| PREFIX | `\2` | error 34, logical value not 0 or 1 | p.15, 401 | spec |
| COMPOPS | `2 & 1` | error 34 | p.15, 401 | spec |
| ARG | `arg(0)`, `arg(1.5)` | error: "n must be a positive whole number" (40 inferred, p.79 gives no number) | p.79 | spec |
| ARG | `arg(1,'X')` | error: option not Exists/Omitted (40 inferred) | p.79 | spec |
| VALUE | `value('a b')` | error: "name must be a valid REXX symbol, or an error results" (the number is not given; 40 inferred) | p.105 | spec |
| ERRORTXT | `errortext(100)`, `errortext(-1)` | error: "any other value is an error" (40 inferred) | p.89 | spec |
| SOURCELN | `sourceline(0)`, `sourceline(sourceline()+1)` | error: n positive, not beyond the final line (40 inferred) | p.99 | spec |
| TRACE | `trace('X')` | error 24, invalid TRACE request | p.103, 399 | spec |
| TRACE | `trace(5)` | error: "option cannot be a number" (number not given) | p.103 | spec |
| DATE, TIME | `date('X')`, `time('X')` | error 40 (option not in the list; inferred) | p.85, 102 | spec |
| PARSE | `parse value 'x' w` (no WITH) | error 38 | p.401 | spec |
| ADDRESS | `address ABCDEFGHI` | error 29, environment name too long | p.400 | spec |

## NEEDS-INTERPRET

| source | cases | why |
|---|---|---|
| brexx:notsign.rexx:1-6 | `(1 X'5F'= 2)`, `(1 X'5F'== 1)`, `X'5F'(1 = 1)`, `X'5F'datatype('x','N')`, `(2 X'5F'> 1)`, `(1 X'5F'< 2)` | the EBCDIC not sign (p.11, 15) cannot be written in the ASCII-only source; the test builds it with X2C and runs it through INTERPRET. The backslash forms are covered in COMPOPS/PREFIX |
| brexx:interpr.rexx:1-7 | CALL under INTERPRET, `interpret 'return 4'`, `interpret 'return tc5a()'`, nested INTERPRET, `interpret 'return'` in a subroutine, RETURN of a PROCEDURE variable, top-level `interpret 'return zero()'` | tests INTERPRET itself |

## Dropped BREXX cases

| file | cases | reason |
|---|---|---|
| parse.rexx | 5 | `PARSE SOURCE . calltype .` = COMMAND depends on how the exec is invoked, not on the manual |
| parse.rexx | 6, 31-36 | already left out of the BREXX file (CMS/UNIX PARSE SOURCE, PARSE EXTERNAL at a console) |
| parse.rexx | 19 | disabled block in BREXX (CMS: WITH invalid outside PARSE VALUE); its TSO/E form 19b is PARSE4 21-22, 27 |
| parse.rexx | 26 (second block, TRL2 POS 2) | `s1 =10 s2 =20 s3`: the `=n` positional form is not in SC28-1883-0 (p.136 knows unsigned and signed numbers only) |
| arg.rexx | 16-20, 21-30 | the same checks as 1-5 and 6-15 through other routines; covered by ARG 1-19 |
| evalord.rexx | 7, 8 (`x + value('x', 5)`) | VALUE with a second argument is not in SC28-1883-0 (p.105: `VALUE(name)`) |
| value.rexx | 5, 6, 18-23 | two-argument VALUE (set a new value), not in SC28-1883-0 |
| date.rexx | 1-9 | print concrete dates, no expectation; replaced by relation cases (DATE) |
| time.rexx | 1-9 | print concrete times, no expectation; replaced by relation cases (TIME) |
| trace.rexx | 3 (commented out, `trace('?A')`) | interactive debug |
| errorms.rexx | all | prints every ERRORTEXT 0-90, no expectation; the texts are ERRORTXT 3-42 |
| address.rexx | 1 | prints ADDRESS() only; replaced by ADDRESS 1 (TSO or MVS) |
| arrays.rexx | all | BREXX extensions (ICREATE, ISET, ISORT, BITARRAY, SFCREATE, ...) |
| addrlink.rexx | all | ADDRESS LINKPGM/LINKMVS are not in SC28-1883-0 (p.22-25: MVS, LINK, ATTACH, TSO, ISPEXEC, ISREDIT) and the cases call a load module |
| estae.rexx | all | STORAGE() (TSO/E environment function, out of scope) to force an abend; BREXX ESTAE behaviour |
| mtt.rexx | all | MTT/MTTX, BREXX extension |
| nosmf.rexx | all | PUTSMF removal, BREXX extension |
| raccheck.rexx | all | RACCHECK extension, SYSVAR out of scope |
| tcp132.rexx | all | TCP functions, BREXX extension |
| updps.rexx | all | ALLOCATE and stream I/O, not in SC28-1883-0 |
| gtterm.rexx | all | SYSVAR (out of scope), TERMINAL() extension |
| uninit.rexx | all | ISEARCH/ISEARCHNN/LLSEARCH, BREXX extensions |
| logic132.rexx | all | LOCATE, E2A/A2E extensions; `DATE('S', v, 'T')` (DATE with conversion arguments) is not in SC28-1883-0 |
| fbpad.rexx | all | LINEIN, READ(), EXECIO with ALLOCATE: stream I/O / extensions |
| lnoutps.rexx, linein.rexx, lineout.rexx, lines.rexx, charin.rexx, charout.rexx, chars.rexx | all | stream I/O, not in SC28-1883-0 |

## Cases where the manual contradicts BREXX

| member | case | manual | BREXX |
|---|---|---|---|
| PREFIX | 11 `-v`, v = '1.50' | `-1.50`: prefix minus is `0-v`, "the other number ... is used as the result", trailing zeros kept (p.142, 139) | `-1.5` |
| VALUE | 10 `value('LIST.'k)`, K=3, LIST.5='?' | `LIST.3` (LIST.3 is unassigned) | `?` |
| PARSE3 | BREXX 26 second block | `=10` is not a 1988 pattern | expected it to work |
| EVALORD, VALUE | 2-argument VALUE | not in the 1988 manual | used |

## Ambiguities found in the manual

- p.138, CASE 1/2: the printed results `", I think is scanned"` and
  `", I think is scanned."` drop the comma after "think" that the data
  string `'This is the data which, I think, is scanned.'` has (and CASE 1
  also drops the period). PARSE3 30-32 expect the value derived from the
  data string: `', I think, is scanned.'`.
- p.89 / Appendix A: ERRORTEXT is said to return "the error message", but
  the manual shows only one example (`ERRORTEXT(16)` = `Label not found`).
  That the result is the Appendix A text after "Error running execname,
  line nn:" is inferred from that one example. The capitalization of the
  Appendix headings is taken as printed (bold headings). Messages 6, 31,
  36, 37 contain typographic quotes in print and are not cases.
- p.17 `'000000' >> '0E0000'` -> 1 holds only in EBCDIC (F0 > C5); in
  ASCII the strict comparison gives 0, so COMPOPS 10 is a `te` case. The
  0 of the host run is the correct ASCII answer, not a defect.
- p.11: the rule for a blank between two parentheses (`('a') ('b')`) is
  one sentence with a nested exception; CONCAT 10 reads it as "blank kept".
- DATE('J') is "yyddd" without saying whether ddd has leading zeros;
  DATE 10 assumes it does (5 characters), since the format shows 5 places
  and Days is explicitly "no leading zeros" while Julian is not.
- TIME('E') after the first call: "no leading zeros" is not settled for
  values below 1 (`.5` or `0.5`); TIME 10/12 check only that it is a
  number >= 0.
- The TRACE function does not say which letter it reports after
  `TRACE('F')`: Failure "is the same as the Normal option" (p.65), so
  `F` or `N` are both possible. The two derived cases that checked this
  (old TRACE 8 and 9) were dropped as UNSUPPORTED.
