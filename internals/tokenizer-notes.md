# REXX Tokenizer Design Notes

Reference material for WP-10 (TSK-113) implementation.

Derived from:
- SC28-1883-0: TSO/E V2 REXX Reference, Chapter 2 (Structure and Syntax)
- Josep Maria Blasco: "A Tokenizer for Rexx and ooRexx", RexxLA 2024
- Practical experience from BREXX/370 `nextsymb.c`

---

## 1. The fundamental REXX tokenizer trap: no reserved words

REXX has **zero reserved words**. Every keyword (DO, IF, WHILE, SAY,
CALL, SIGNAL, PARSE, etc.) is a plain symbol and can also be used
as a variable name. This is legal REXX:

```rexx
while = 4
do while = 1 to (while) while (while < 7)
  say while
end while
```

**Consequence for the tokenizer:** The tokenizer must classify `DO`,
`WHILE`, `SAY`, `END` etc. as `TOK_SYMBOL` — the same type as any
variable name. Keyword recognition is **entirely the parser's job**
(WP-13), based on clause position and context. The tokenizer has no
concept of "keyword tokens".

If you create token types like `TOK_DO`, `TOK_IF`, `TOK_SAY` — you
have made a mistake. There is only `TOK_SYMBOL`.

---

## 2. Whitespace is NOT a token, but it IS an operator

In most languages, whitespace between tokens is insignificant filler.
In REXX, a blank between two terms is the **concatenation operator**:

```rexx
x = 'hello' 'world'    /* Result: 'hello world'  (blank concatenation) */
x = 'hello'||'world'   /* Result: 'helloworld'   (explicit concat)     */
x = 'hello''world'     /* Result: 'helloworld'   (abuttal concat)      */
```

**Consequence for the tokenizer:** Do NOT emit whitespace/blank tokens.
The parser will determine concatenation from the adjacency of terms
in the token stream (two consecutive value-producing tokens with no
operator between them = blank concatenation). The tokenizer simply
skips whitespace between tokens.

---

## 3. Multi-character operators are a parser concern

REXX allows whitespace and even comments between the characters that
form a multi-character operator:

```rexx
a = b | /* a comment */ | c      /* This is || (concatenation)  */
a = b * /* power */ * 2           /* This is ** (exponentiation) */
a = b / /* integer div */ / c     /* This is // (remainder)      */
```

The IBM spec defines these composite operators: `**`, `//`, `||`,
`>=`, `<=`, `\=`, `==`, `\==`, `>=`, `<=`, `>>`, `<<`, `>>=`,
`<<=`, `\>>`, `\<<`, `&&`.

**Consequence for the tokenizer:** Emit each operator CHARACTER as
its own token. `||` becomes two `TOK_OPERATOR` tokens, each with
value `|`. The parser combines adjacent operator tokens into
composite operators. This is simpler, handles the whitespace-in-
operator edge case automatically, and matches IBM's spec which
defines operators at the syntactic (parser) level, not the lexical
(tokenizer) level.

The only exception: comparison operators that start with `\` or `¬`
(the NOT character). `\=` should be emitted as `TOK_NOT` followed by
`TOK_COMPARISON('=')`, letting the parser combine them.

---

## 4. Symbols are unusual

A REXX "symbol" encompasses what most languages split into several
categories:

| REXX concept | Examples | Notes |
|---|---|---|
| Simple variable | `x`, `name`, `i` | Starts with A-Z or a-z or !?_@ |
| Compound variable | `stem.i.j`, `a.b.c` | Contains dots |
| Stem | `stem.` | Ends with dot |
| Constant symbol | `25AB`, `3.14` | Starts with digit or dot |
| Number | `42`, `1E5`, `3.14` | Subset of constant symbol |

All of these are `TOK_SYMBOL` in the tokenizer. The distinction
between variable symbols and constant symbols matters for the parser
(variable lookup vs. literal value), but the tokenizer just sees a
symbol.

**Symbol characters** (SC28-1883-0, Chapter 2): A symbol starts with
`A-Z a-z ! ? _ @` or `0-9 .` and continues with any of those plus
digits. On EBCDIC, use `isalnum()` plus the special characters.

**Compound symbols with dots:** `stem.i.j` is ONE symbol token. The
tokenizer does not split at dots within a symbol. The variable pool
(WP-12) handles the dot-delimited tail resolution.

---

## 5. Numbers vs. symbols

Numbers in REXX are a subset of symbols: `42`, `3.14`, `1E5`,
`1e-2`. The tokenizer can optionally classify these as `TOK_NUMBER`
for convenience, but this is a **hint** — the parser and expression
evaluator ultimately decide if something is a number based on the
REXX number format rules (which depend on NUMERIC DIGITS).

A pragmatic approach: if the token starts with a digit or `.digit`,
classify as `TOK_NUMBER`. Everything else starting with alpha or
special is `TOK_SYMBOL`. The expression evaluator will re-check
numeric validity at runtime anyway.

---

## 6. String literals

REXX has two quote characters: `'` and `"`. A string can use either:

```rexx
x = 'hello'
x = "hello"
```

**Doubling for escape:** A quote inside a string of the same type is
doubled:

```rexx
x = 'it''s'        /* Value: it's  */
x = "say ""hi"""   /* Value: say "hi" */
```

**Hex strings:** A string immediately followed by `x` or `X`:

```rexx
x = 'FF'x          /* One byte, value X'FF' */
x = 'C1 C2 C3'x    /* Three bytes           */
```

Only hex digits and spaces are allowed inside. Spaces are ignored.
Must have an even number of hex digits (after removing spaces).

**Binary strings:** A string immediately followed by `b` or `B`:

```rexx
x = '11110000'b    /* One byte, value X'F0' */
x = '1111 0000'b   /* Same, spaces allowed  */
```

Only `0`, `1`, and spaces. Groups of 4 bits (after removing spaces,
must be multiple of 4).

**Tokenizer responsibility:** Recognize the suffix character (`x`/`X`
or `b`/`B`) immediately after the closing quote (no whitespace
between). Emit as `TOK_HEXSTRING` or `TOK_BINSTRING`. The token
value should be the raw content between quotes (without suffix), and
the suffix should be recorded in flags or a separate field.

---

## 7. Comments

REXX comments are `/* ... */` and can be **nested**:

```rexx
/* This is /* a nested */ comment */
say 'hello'
```

Track `comment_depth` as a counter. Increment on `/*`, decrement on
`*/`. Only exit comment mode when depth reaches 0.

**Line counting:** Newlines inside comments must still be counted
for accurate line numbers in error messages, TRACE output, and
PARSE SOURCE / SOURCELINE().

**REXX identifier:** The first line of a REXX exec conventionally
starts with `/* REXX */` or a comment containing the word `REXX`.
The tokenizer does not need to check this — it is the Exec Load
Routine's job (WP in Phase 4). The tokenizer just processes whatever
source text it receives.

---

## 8. Clause boundaries

A clause ends at:
- A semicolon (`;`) — explicit clause end
- End of line — implicit clause end (unless continuation)
- End of source

**Continuation:** A comma at the end of a clause (possibly followed
by whitespace or comments, then newline) means the clause continues
on the next line:

```rexx
say 'hello',      /* continues */
    'world'
```

The tokenizer should:
1. Emit `TOK_COMMA` for the comma
2. NOT emit `TOK_EOC` for the line end
3. Continue tokenizing the next line as part of the same clause

**Semicolons in comments or strings** do not end clauses.

**Label colons:** A symbol followed by `:` is a label. The colon
is effectively a clause terminator. The tokenizer can emit the colon
as `TOK_SEMICOLON` (it acts as one) or as a separate token type.
Simplest: emit it as `TOK_SEMICOLON` — the parser recognizes labels
from the pattern SYMBOL + SEMICOLON at clause start.

---

## 9. Operator characters

Single-character operators to recognize:

| Character | Token type |
|---|---|
| `+` `-` `*` `/` `%` | `TOK_OPERATOR` |
| `=` `>` `<` | `TOK_COMPARISON` |
| `&` `\|` | `TOK_LOGICAL` |
| `\` `¬` | `TOK_NOT` |
| `(` | `TOK_LPAREN` |
| `)` | `TOK_RPAREN` |
| `,` | `TOK_COMMA` |
| `;` | `TOK_SEMICOLON` |

Note: `|` is `TOK_LOGICAL`, not `TOK_CONCAT`. The parser forms `||`
from two adjacent `TOK_LOGICAL('|')` tokens.

**EBCDIC:** The NOT character has two representations: `\` (backslash,
X'E0' in EBCDIC 037) and `¬` (logical not, X'5F' in EBCDIC 037).
Both must be recognized. In ASCII cross-compile, `¬` may not be
available — handle with `#ifdef` or accept only `\`.

---

## 10. Summary: what the tokenizer does and does NOT do

**Does:**
- Breaks source into tokens (TOK_SYMBOL, TOK_STRING, TOK_NUMBER,
  TOK_HEXSTRING, TOK_BINSTRING, TOK_OPERATOR, TOK_COMPARISON,
  TOK_LOGICAL, TOK_NOT, TOK_CONCAT, TOK_LPAREN, TOK_RPAREN,
  TOK_COMMA, TOK_SEMICOLON, TOK_EOC, TOK_EOF)
- Skips whitespace (does not emit blank tokens)
- Strips comments (tracks nesting, preserves line count)
- Handles continuation (comma before line end)
- Records line/column for every token
- Reports errors with line/column (unclosed string, unclosed comment)

**Does NOT:**
- Recognize keywords (no TOK_DO, TOK_IF, TOK_SAY — all are TOK_SYMBOL)
- Form composite operators (no TOK_POWEROP for `**` — two TOK_OPERATOR)
- Determine if a symbol is a variable or a label
- Evaluate hex/binary string contents
- Check REXX identifier in first line
- Access or modify irx_wkblk_int (pure: source in → tokens out)
