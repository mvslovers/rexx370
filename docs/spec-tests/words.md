# Spec tests: words group

Members in `test/spec/`: `DELWORD`, `FIND`, `SUBWORD`, `WORD`, `WORDINDX`, `WORDLEN`, `WORDPOS`, `WORDS`.

Authority: SC28-1883-0 (TSO/E Version 2 REXX Reference, December 1988). Page numbers are printed pages. Blank counts inside the manual examples (DELWORD, SUBWORD, FIND, WORDPOS) were measured on the page images by column-gap width of the monospace font, not taken from the OCR text.

Expected values are shown as REXX literals, so blanks inside the quotes are exact. No case in this group depends on the character set, so no case uses `te`.

General rules used for derived values (p.77, "General notes on the built-in functions"): a null string can be supplied wherever a string is referenced; a length must be a nonnegative whole number; a start character or word must be a positive whole number.

## DELWORD (`DELWORD`), p.87

BREXX source: `brexx370/test/delword.rexx`

Setup before case 5: `s = 'Med lov skal land bygges'` (a line with the literal inline would exceed 72 characters, and a `,,` continuation is mis-parsed by rexx370 today, see the note at the end).

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `delword('Now is the  time',2,2)` | `'Now time'` | p.87 manual example | spec |  | brexx:delword.rexx:1 is this example with the double blank flattened; same result |
| 2 | `delword('Now is the time ',3)` | `'Now is '` | p.87 manual example | spec |  | brexx:delword.rexx:2 identical |
| 3 | `delword('Now is the  time',5)` | `'Now is the  time'` | p.87 manual example | spec |  | double blank must survive |
| 4 | `delword('Now time',5)` | `'Now time'` | p.87 derived: p.87 "If n is greater than the number of words in string, string is returned unchanged" | brexx:delword.rexx:3 |  | brexx changed the string of the manual example; kept as its own case |
| 5 | `delword(s, 3)` | `'Med lov '` | p.87 derived: p.87 "deletes the substring of string that starts at the nth word ... The string deleted includes any blanks following the final word involved"; length omitted = remaining words | brexx:delword.rexx:4 |  | blank before word 3 stays |
| 6 | `delword(s, 1)` | `''` | p.87 derived: p.87 "deletes the substring of string that starts at the nth word ... The string deleted includes any blanks following the final word involved"; length omitted = remaining words | brexx:delword.rexx:5 |  |  |
| 7 | `delword(s, 1,1)` | `'lov skal land bygges'` | p.87 derived: p.87 "deletes the substring of string that starts at the nth word ... The string deleted includes any blanks following the final word involved"; length omitted = remaining words | brexx:delword.rexx:6 |  | trailing blank of word 1 deleted |
| 8 | `delword(s, 2,3)` | `'Med bygges'` | p.87 derived: p.87 "deletes the substring of string that starts at the nth word ... The string deleted includes any blanks following the final word involved"; length omitted = remaining words | brexx:delword.rexx:7 |  |  |
| 9 | `delword(s, 2,10)` | `'Med '` | p.87 derived: p.87 "deletes the substring of string that starts at the nth word ... The string deleted includes any blanks following the final word involved"; length omitted = remaining words | brexx:delword.rexx:8 |  | length beyond last word = rest of string |
| 10 | `delword(s, 3,2)` | `'Med lov bygges'` | p.87 derived: p.87 "deletes the substring of string that starts at the nth word ... The string deleted includes any blanks following the final word involved"; length omitted = remaining words | brexx:delword.rexx:9 |  |  |
| 11 | `delword(' Med lov skal', 1,0)` | `' Med lov skal'` | p.87/77 derived: p.77 general note: "If an argument specifies a length, it must be a nonnegative whole number"; zero words deleted | brexx:delword.rexx:12 |  | leading blank must survive |
| 12 | `delword(' Med lov skal ', 4)` | `' Med lov skal '` | p.87 derived: p.87 "If n is greater than the number of words in string, string is returned unchanged" | brexx:delword.rexx:13 |  |  |
| 13 | `delword('', 1)` | `''` | p.87 derived: p.87 "If n is greater than the number of words in string, string is returned unchanged"; p.77: "Where a string is referenced, a null string can be supplied" | brexx:delword.rexx:14 |  |  |
| 14 | `delword(s, 3,0)` | `'Med lov skal land bygges'` | p.87/77 derived: p.77 general note: "If an argument specifies a length, it must be a nonnegative whole number"; zero words deleted | brexx:delword.rexx:15 |  |  |
| 15 | `delword(s, 10)` | `'Med lov skal land bygges'` | p.87 derived: p.87 "If n is greater than the number of words in string, string is returned unchanged" | brexx:delword.rexx:16 |  |  |
| 16 | `delword(s, 9,9)` | `'Med lov skal land bygges'` | p.87 derived: p.87 "If n is greater than the number of words in string, string is returned unchanged" | brexx:delword.rexx:17 |  |  |
| 17 | `delword(s, 1,0)` | `'Med lov skal land bygges'` | p.87/77 derived: p.77 general note: "If an argument specifies a length, it must be a nonnegative whole number"; zero words deleted | brexx:delword.rexx:18 |  |  |

17 cases: 3 manual examples, 14 derived.

Dropped:

| BREXX case | reason |
|---|---|
| brexx:delword.rexx:10 | identical to brexx:delword.rexx:9 |
| brexx:delword.rexx:11 | identical to brexx:delword.rexx:9 |

ERROR-CASE (specified outcome is error 40, waits for SIGNAL ON SYNTAX; not in the exec):

| expression | status | rule |
|---|---|---|
| `delword('a b',0)` | ERROR-CASE | p.87 "n must be a positive whole number" |

## FIND (`FIND`), p.90

BREXX source: none (no BREXX test for this function).

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `find('now is the time','is the time')` | `'2'` | p.90 manual example | spec |  |  |
| 2 | `find('now is  the time','is    the')` | `'2'` | p.90 manual example | spec |  | blank counts measured on the page image (2 and 4) |
| 3 | `find('now is  the time','is  time ')` | `'0'` | p.90 manual example | spec |  |  |
| 4 | `find('now is the time','')` | `'0'` | p.90 derived: p.90 "Returns 0 if phrase is not found or if there are no words in phrase" | spec |  | null phrase |
| 5 | `find('now is the time','  ')` | `'0'` | p.90 derived: p.90 "Returns 0 if phrase is not found or if there are no words in phrase" | spec |  | phrase of blanks only |

5 cases: 3 manual examples, 2 derived.

## SUBWORD (`SUBWORD`), p.101

BREXX source: `brexx370/test/subword.rexx`

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `subword('Now is the  time',2,2)` | `'is the'` | p.101 manual example | spec |  | brexx:subword.rexx:1 is this example with the double blank flattened |
| 2 | `subword('Now is the  time',3)` | `'the  time'` | p.101 manual example | spec |  | inner double blank must survive |
| 3 | `subword('Now is the  time',5)` | `''` | p.101 manual example | spec |  | brexx:subword.rexx:3 is this example, blanks flattened |
| 4 | `subword('Now is the time',3)` | `'the time'` | p.101 derived: p.101 "The returned string will never have leading or trailing blanks, but will include all blanks between the selected words" | brexx:subword.rexx:2 |  | single-blank variant of example 2 |
| 5 | `subword(' to be or not to be ',5)` | `'to be'` | p.101 derived: p.101 "The returned string will never have leading or trailing blanks, but will include all blanks between the selected words" | brexx:subword.rexx:4 |  | no trailing blank |
| 6 | `subword(' to be or not to be ',6)` | `'be'` | p.101 derived: p.101 "The returned string will never have leading or trailing blanks, but will include all blanks between the selected words" | brexx:subword.rexx:5 |  |  |
| 7 | `subword(' to be or not to be ',7)` | `''` | p.101 derived: p.101 "returns the substring of string that starts at the nth word"; fewer words than n -> nothing to return (null, as in the manual example SUBWORD(...,5) -> '') | brexx:subword.rexx:6 |  |  |
| 8 | `subword(' to be or not to be ',8,7)` | `''` | p.101 derived: p.101 "returns the substring of string that starts at the nth word"; fewer words than n -> nothing to return (null, as in the manual example SUBWORD(...,5) -> '') | brexx:subword.rexx:7 |  |  |
| 9 | `subword(' to be or not to be ',3,2)` | `'or not'` | p.101 derived: p.101 "The returned string will never have leading or trailing blanks, but will include all blanks between the selected words" | brexx:subword.rexx:8 |  |  |
| 10 | `subword(' to be or not to be ',1,2)` | `'to be'` | p.101 derived: p.101 "The returned string will never have leading or trailing blanks, but will include all blanks between the selected words" | brexx:subword.rexx:9 |  | no leading blank |
| 11 | `subword(' to be or not to be ',4,2)` | `'not to'` | p.101 derived: p.101 "The returned string will never have leading or trailing blanks, but will include all blanks between the selected words" | brexx:subword.rexx:10 |  |  |
| 12 | `subword('abc de f', 3)` | `'f'` | p.101 derived: p.101 "The returned string will never have leading or trailing blanks, but will include all blanks between the selected words" | brexx:subword.rexx:11 |  |  |

12 cases: 3 manual examples, 9 derived.

ERROR-CASE (specified outcome is error 40, waits for SIGNAL ON SYNTAX; not in the exec):

| expression | status | rule |
|---|---|---|
| `subword('a b',0)` | ERROR-CASE | p.101 "n must be a positive whole number" |

## WORD (`WORD`), p.106

BREXX source: `brexx370/test/word.rexx`

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `word('Now is the time',3)` | `'the'` | p.107 manual example | spec |  | examples are printed on p.107; brexx:word.rexx:1 identical |
| 2 | `word('Now is the time',5)` | `''` | p.107 manual example | spec |  | brexx:word.rexx:2 identical |
| 3 | `word('This is certainly a test',1)` | `'This'` | p.106 derived: p.106 "returns the nth blank-delimited word in string ... exactly equivalent to SUBWORD(string,n,1)" | brexx:word.rexx:3 |  |  |
| 4 | `word(' This is certainly a test',1)` | `'This'` | p.106 derived: p.106 "returns the nth blank-delimited word in string ... exactly equivalent to SUBWORD(string,n,1)" | brexx:word.rexx:4 |  | no leading blank (SUBWORD rule p.101) |
| 5 | `word('This is certainly a test',2)` | `'is'` | p.106 derived: p.106 "returns the nth blank-delimited word in string ... exactly equivalent to SUBWORD(string,n,1)" | brexx:word.rexx:6 |  |  |
| 6 | `word('This is certainly a test',5)` | `'test'` | p.106 derived: p.106 "returns the nth blank-delimited word in string ... exactly equivalent to SUBWORD(string,n,1)" | brexx:word.rexx:8 |  |  |
| 7 | `word('This is certainly a test ',5)` | `'test'` | p.106 derived: p.106 "returns the nth blank-delimited word in string ... exactly equivalent to SUBWORD(string,n,1)" | brexx:word.rexx:9 |  | no trailing blank |
| 8 | `word('This is certainly a test',6)` | `''` | p.106 derived: p.106 "If there are fewer than n words in string, the null string is returned" | brexx:word.rexx:10 |  |  |
| 9 | `word('',1)` | `''` | p.106 derived: p.106 "If there are fewer than n words in string, the null string is returned"; p.77: "Where a string is referenced, a null string can be supplied" | brexx:word.rexx:11 |  |  |
| 10 | `word('',10)` | `''` | p.106 derived: p.106 "If there are fewer than n words in string, the null string is returned" | brexx:word.rexx:12 |  |  |
| 11 | `word('test ',2)` | `''` | p.106 derived: p.106 "If there are fewer than n words in string, the null string is returned" | brexx:word.rexx:13 |  |  |

11 cases: 2 manual examples, 9 derived.

Dropped:

| BREXX case | reason |
|---|---|
| brexx:word.rexx:5 | identical to brexx:word.rexx:3 |
| brexx:word.rexx:7 | identical to brexx:word.rexx:6 |

ERROR-CASE (specified outcome is error 40, waits for SIGNAL ON SYNTAX; not in the exec):

| expression | status | rule |
|---|---|---|
| `word('a b',0)` | ERROR-CASE | p.106 "n must be a positive whole number" |

## WORDINDEX (`WORDINDX`), p.107

BREXX source: `brexx370/test/wordind.rexx`

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `wordindex('Now is the time',3)` | `'8'` | p.107 manual example | spec |  | brexx:wordind.rexx:1 identical (brexx compared with \=) |
| 2 | `wordindex('Now is the time',6)` | `'0'` | p.107 manual example | spec |  | brexx:wordind.rexx:2 identical (brexx compared with \=) |
| 3 | `wordindex('This is certainly a test',1)` | `'1'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:3 |  |  |
| 4 | `wordindex('  This is certainly a test',1)` | `'3'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:4 |  |  |
| 5 | `wordindex('This   is certainly a test',1)` | `'1'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:5 |  |  |
| 6 | `wordindex('  This   is certainly a test',1)` | `'3'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:6 |  |  |
| 7 | `wordindex('This is certainly a test',2)` | `'6'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:7 |  |  |
| 8 | `wordindex('This   is certainly a test',2)` | `'8'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:8 |  |  |
| 9 | `wordindex('This is   certainly a test',2)` | `'6'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:9 |  |  |
| 10 | `wordindex('This   is   certainly a test',2)` | `'8'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:10 |  |  |
| 11 | `wordindex('This is certainly a test',5)` | `'21'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:11 |  |  |
| 12 | `wordindex('This is certainly a   test',5)` | `'23'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:12 |  |  |
| 13 | `wordindex('This is certainly a test  ',5)` | `'21'` | p.107 derived: p.107 "returns the position of the first character in the nth blank-delimited word in string" (position counts every character of string, leading blanks included) | brexx:wordind.rexx:13 |  |  |
| 14 | `wordindex('This is certainly a test  ',6)` | `'0'` | p.107 derived: p.107 "If there are fewer than n words in the string, 0 is returned" | brexx:wordind.rexx:14 |  |  |
| 15 | `wordindex('This is certainly a test',6)` | `'0'` | p.107 derived: p.107 "If there are fewer than n words in the string, 0 is returned" | brexx:wordind.rexx:15 |  |  |
| 16 | `wordindex('This is certainly a test',7)` | `'0'` | p.107 derived: p.107 "If there are fewer than n words in the string, 0 is returned" | brexx:wordind.rexx:16 |  |  |
| 17 | `wordindex('This is certainly a test  ',7)` | `'0'` | p.107 derived: p.107 "If there are fewer than n words in the string, 0 is returned" | brexx:wordind.rexx:17 |  |  |

17 cases: 2 manual examples, 15 derived.

ERROR-CASE (specified outcome is error 40, waits for SIGNAL ON SYNTAX; not in the exec):

| expression | status | rule |
|---|---|---|
| `wordindex('a b',0)` | ERROR-CASE | p.107 "n must be a positive whole number" |

## WORDLENGTH (`WORDLEN`), p.107

BREXX source: `brexx370/test/wordlen.rexx`

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `wordlength('Now is the time',2)` | `'2'` | p.107 manual example | spec |  | brexx:wordlen.rexx:1 identical (brexx compared with \=) |
| 2 | `wordlength('Now comes the time',2)` | `'5'` | p.107 manual example | spec |  | brexx:wordlen.rexx:2 identical |
| 3 | `wordlength('Now is the time',6)` | `'0'` | p.107 manual example | spec |  | brexx:wordlen.rexx:3 identical |
| 4 | `wordlength('This is certainly a test',1)` | `'4'` | p.107 derived: p.107 "returns the length of the nth blank-delimited word in string" | brexx:wordlen.rexx:4 |  |  |
| 5 | `wordlength('This is certainly a test',2)` | `'2'` | p.107 derived: p.107 "returns the length of the nth blank-delimited word in string" | brexx:wordlen.rexx:5 |  |  |
| 6 | `wordlength('This is certainly a test',5)` | `'4'` | p.107 derived: p.107 "returns the length of the nth blank-delimited word in string" | brexx:wordlen.rexx:6 |  |  |
| 7 | `wordlength('This is certainly a test ',5)` | `'4'` | p.107 derived: p.107 "returns the length of the nth blank-delimited word in string" | brexx:wordlen.rexx:7 |  | trailing blank not counted |
| 8 | `wordlength('This is certainly a test',6)` | `'0'` | p.107 derived: p.107 "If there are fewer than n words in the string, 0 is returned" | brexx:wordlen.rexx:8 |  |  |
| 9 | `wordlength('',1)` | `'0'` | p.107 derived: p.107 "If there are fewer than n words in the string, 0 is returned"; p.77: "Where a string is referenced, a null string can be supplied" | brexx:wordlen.rexx:9 |  |  |
| 10 | `wordlength('',10)` | `'0'` | p.107 derived: p.107 "If there are fewer than n words in the string, 0 is returned" | brexx:wordlen.rexx:10 |  |  |

10 cases: 3 manual examples, 7 derived.

ERROR-CASE (specified outcome is error 40, waits for SIGNAL ON SYNTAX; not in the exec):

| expression | status | rule |
|---|---|---|
| `wordlength('a b',0)` | ERROR-CASE | p.107 "n must be a positive whole number" |

## WORDPOS (`WORDPOS`), p.107

BREXX source: `brexx370/test/wordpos.rexx`

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `wordpos('the','now is the time')` | `'3'` | p.108 manual example | spec |  | examples are printed on p.108; brexx:wordpos.rexx:1 used "Now", same result |
| 2 | `wordpos('The','now is the time')` | `'0'` | p.108 manual example | spec |  | brexx:wordpos.rexx:2 same case |
| 3 | `wordpos('is the','now is the time')` | `'2'` | p.108 manual example | spec |  | brexx:wordpos.rexx:3 same case |
| 4 | `wordpos('is   the','now is the time')` | `'2'` | p.108 manual example | spec |  | brexx:wordpos.rexx:4 is this example with the three blanks flattened (then identical to :3) |
| 5 | `wordpos('is   time ','now is   the time')` | `'0'` | p.108 manual example | spec |  | not in brexx; blank counts measured on the page image |
| 6 | `wordpos('be','To be or not to be')` | `'2'` | p.108 manual example | spec |  | brexx:wordpos.rexx:5 identical |
| 7 | `wordpos('be','To be or not to be',3)` | `'6'` | p.108 manual example | spec |  | brexx:wordpos.rexx:6 identical |
| 8 | `wordpos('This','This is a small test')` | `'1'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:7 |  |  |
| 9 | `wordpos('test','This is a small test')` | `'5'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:8 |  |  |
| 10 | `wordpos('foo','This is a small test')` | `'0'` | p.107 derived: p.107 "Returns 0 if phrase is not found" | brexx:wordpos.rexx:9 |  |  |
| 11 | `wordpos(' This ','This is a small test')` | `'1'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:10 |  | phrase is a sequence of words; surrounding blanks do not belong to a word |
| 12 | `wordpos('This',' This is a small test')` | `'1'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:11 |  |  |
| 13 | `wordpos('This','this is a small This')` | `'5'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:13 |  | case matters ("match exactly") |
| 14 | `wordpos('This','This is a small This')` | `'1'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:14 |  | first occurrence |
| 15 | `wordpos('This','This is a small This', 2)` | `'5'` | p.107 derived: p.107 "specifying start (which must be positive), the word at which to start the search" | brexx:wordpos.rexx:15 |  |  |
| 16 | `wordpos('is a ','This is a small test')` | `'2'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:16 |  |  |
| 17 | `wordpos(' is a ','This is a small test')` | `'2'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:18 |  |  |
| 18 | `wordpos('is a ','This is a small test', 2)` | `'2'` | p.107 derived: p.107 "specifying start (which must be positive), the word at which to start the search" | brexx:wordpos.rexx:19 |  | start at the match itself |
| 19 | `wordpos('is a ','This is a small test',3)` | `'0'` | p.107 derived: p.107 "specifying start (which must be positive), the word at which to start the search"; p.107 "Returns 0 if phrase is not found" | brexx:wordpos.rexx:20 |  |  |
| 20 | `wordpos('is a ','This is a small test',4)` | `'0'` | p.107 derived: p.107 "specifying start (which must be positive), the word at which to start the search"; p.107 "Returns 0 if phrase is not found" | brexx:wordpos.rexx:21 |  |  |
| 21 | `wordpos('test ','This is a small test')` | `'5'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:22 |  |  |
| 22 | `wordpos('test ','This is a small test',5)` | `'5'` | p.107 derived: p.107 "specifying start (which must be positive), the word at which to start the search" | brexx:wordpos.rexx:23 |  |  |
| 23 | `wordpos('test ','This is a small test',6)` | `'0'` | p.107 derived: p.107 "specifying start (which must be positive), the word at which to start the search"; p.107 "Returns 0 if phrase is not found" | brexx:wordpos.rexx:24 |  | start past the last word |
| 24 | `wordpos('test ','This is a small test ')` | `'5'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:25 |  |  |
| 25 | `wordpos(' test','This is a small test ',6)` | `'0'` | p.107 derived: p.107 "specifying start (which must be positive), the word at which to start the search"; p.107 "Returns 0 if phrase is not found" | brexx:wordpos.rexx:26 |  |  |
| 26 | `wordpos('test ','This is a small test ',5)` | `'5'` | p.107 derived: p.107 "specifying start (which must be positive), the word at which to start the search" | brexx:wordpos.rexx:27 |  |  |
| 27 | `wordpos('test ','')` | `'0'` | p.107 derived: p.107 "Returns 0 if phrase is not found"; p.77: "Where a string is referenced, a null string can be supplied" | brexx:wordpos.rexx:31 |  |  |
| 28 | `wordpos(' a ','')` | `'0'` | p.107 derived: p.107 "Returns 0 if phrase is not found" | brexx:wordpos.rexx:36 |  |  |
| 29 | `wordpos(' a ','a')` | `'1'` | p.107 derived: p.107 "searches string for the first occurrence of the sequence of blank-delimited words phrase ... Multiple blanks between words in either phrase or string are treated as a single blank for the comparison, but otherwise the words must match exactly" | brexx:wordpos.rexx:37 |  |  |

29 cases: 7 manual examples, 22 derived.

Dropped:

| BREXX case | reason |
|---|---|
| brexx:wordpos.rexx:12 | identical to brexx:wordpos.rexx:7 |
| brexx:wordpos.rexx:17 | identical to brexx:wordpos.rexx:16 |
| brexx:wordpos.rexx:28 | phrase contains no words (' '): the 1988 WORDPOS text says only "Returns 0 if phrase is not found" and does not settle an empty phrase (FIND p.90 does, see FIND cases 4-5). FIND opens with "WORDPOS is the preferred built-in function for this type of word search. Refer to page 107 ..." -- that points from FIND to WORDPOS but does not carry FIND's "or if there are no words in phrase" rule over to WORDPOS, so it does not settle these cases |
| brexx:wordpos.rexx:29 | phrase contains no words (' ', start 3): the 1988 WORDPOS text says only "Returns 0 if phrase is not found" and does not settle an empty phrase (FIND p.90 does, see FIND cases 4-5). FIND opens with "WORDPOS is the preferred built-in function for this type of word search. Refer to page 107 ..." -- that points from FIND to WORDPOS but does not carry FIND's "or if there are no words in phrase" rule over to WORDPOS, so it does not settle these cases |
| brexx:wordpos.rexx:30 | phrase contains no words ('', start 4): the 1988 WORDPOS text says only "Returns 0 if phrase is not found" and does not settle an empty phrase (FIND p.90 does, see FIND cases 4-5). FIND opens with "WORDPOS is the preferred built-in function for this type of word search. Refer to page 107 ..." -- that points from FIND to WORDPOS but does not carry FIND's "or if there are no words in phrase" rule over to WORDPOS, so it does not settle these cases |
| brexx:wordpos.rexx:32 | phrase contains no words ('' in ''): the 1988 WORDPOS text says only "Returns 0 if phrase is not found" and does not settle an empty phrase (FIND p.90 does, see FIND cases 4-5). FIND opens with "WORDPOS is the preferred built-in function for this type of word search. Refer to page 107 ..." -- that points from FIND to WORDPOS but does not carry FIND's "or if there are no words in phrase" rule over to WORDPOS, so it does not settle these cases |
| brexx:wordpos.rexx:33 | phrase contains no words ('' in ' '): the 1988 WORDPOS text says only "Returns 0 if phrase is not found" and does not settle an empty phrase (FIND p.90 does, see FIND cases 4-5). FIND opens with "WORDPOS is the preferred built-in function for this type of word search. Refer to page 107 ..." -- that points from FIND to WORDPOS but does not carry FIND's "or if there are no words in phrase" rule over to WORDPOS, so it does not settle these cases |
| brexx:wordpos.rexx:34 | phrase contains no words (' ' in ''): the 1988 WORDPOS text says only "Returns 0 if phrase is not found" and does not settle an empty phrase (FIND p.90 does, see FIND cases 4-5). FIND opens with "WORDPOS is the preferred built-in function for this type of word search. Refer to page 107 ..." -- that points from FIND to WORDPOS but does not carry FIND's "or if there are no words in phrase" rule over to WORDPOS, so it does not settle these cases |
| brexx:wordpos.rexx:35 | phrase contains no words (' ' in '', start 3): the 1988 WORDPOS text says only "Returns 0 if phrase is not found" and does not settle an empty phrase (FIND p.90 does, see FIND cases 4-5). FIND opens with "WORDPOS is the preferred built-in function for this type of word search. Refer to page 107 ..." -- that points from FIND to WORDPOS but does not carry FIND's "or if there are no words in phrase" rule over to WORDPOS, so it does not settle these cases |

ERROR-CASE (specified outcome is error 40, waits for SIGNAL ON SYNTAX; not in the exec):

| expression | status | rule |
|---|---|---|
| `wordpos('a','a b',0)` | ERROR-CASE | p.107 "start (which must be positive)" |

## WORDS (`WORDS`), p.108

BREXX source: `brexx370/test/words.rexx`

| # | expression | expected | spec | source | BREXX said | note |
|---|---|---|---|---|---|---|
| 1 | `words('Now is the time')` | `'4'` | p.108 manual example | spec |  | brexx:words.rexx:1 identical (brexx compared with \=) |
| 2 | `words(' ')` | `'0'` | p.108 manual example | spec |  | brexx:words.rexx:2 identical; :10 repeats it |
| 3 | `words('This is certainly a test')` | `'5'` | p.108 derived: p.108 "returns the number of blank-delimited words in string" | brexx:words.rexx:3 |  |  |
| 4 | `words(' This is certainly a test')` | `'5'` | p.108 derived: p.108 "returns the number of blank-delimited words in string" | brexx:words.rexx:4 |  |  |
| 5 | `words('This is certainly a test ')` | `'5'` | p.108 derived: p.108 "returns the number of blank-delimited words in string" | brexx:words.rexx:6 |  |  |
| 6 | `words(' hepp ')` | `'1'` | p.108 derived: p.108 "returns the number of blank-delimited words in string" | brexx:words.rexx:7 |  |  |
| 7 | `words(' hepp hepp ')` | `'2'` | p.108 derived: p.108 "returns the number of blank-delimited words in string" | brexx:words.rexx:8 |  |  |
| 8 | `words('')` | `'0'` | p.108 derived: p.108 "returns the number of blank-delimited words in string"; p.77: "Where a string is referenced, a null string can be supplied" | brexx:words.rexx:9 |  |  |

8 cases: 2 manual examples, 6 derived.

Dropped:

| BREXX case | reason |
|---|---|
| brexx:words.rexx:5 | identical to brexx:words.rexx:3 |
| brexx:words.rexx:10 | identical to brexx:words.rexx:2 (the manual example) |

## Notes

- Continuation (p.12: "The comma is functionally replaced by a blank"): `call t 7, f(x),,` followed by the expected value on the next line must pass three arguments. rexx370 (2026-09-30, both paths) passes four, the third one empty, so the members avoid `,,` continuations and use the setup variable `s` in DELWORD instead.
- Host run 2026-09-30 (bytecode and token-walk alike): all members pass except DELWORD cases 2, 5 and 9. rexx370 drops the blanks in front of the nth word: `delword('Now is the time ',3)` returns `'Now is'`, the manual example says `'Now is '`.
- Ambiguity: WORDPOS with a phrase that contains no words is not settled by the 1988 text (dropped, see WORDPOS).
