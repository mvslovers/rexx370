# REXX/370 Work Packages

Work package definitions for agent-driven implementation.
Each WP is self-contained with inputs, outputs, acceptance criteria.

Reference: [Architecture Design v0.1.0](https://www.notion.so/3283d9938787811ba3f4d3308b254cad)

---

## Status

| WP | Title | Phase | Status |
|----|-------|-------|--------|
| WP-01 | Project skeleton + headers | 1 | DONE |
| WP-02 | IRXSTOR, IRXUID, IRXMSGID | 1 | DONE |
| WP-03 | RAB management | 1 | DONE |
| WP-04 | IRXINIT + IRXTERM | 1 | DONE |
| WP-05 | Phase 1 smoke test | 1 | DONE (38/38) |
| WP-10 | Tokenizer (IRX#TOKN) | 2 | DONE (70/70) — PR #2 |
| WP-11b | LString adapter (IRX#LSTR) | 2 | DONE (50/50) — PR #4 |
| WP-12 | Variable pool (IRX#VPOL) | 2 | DONE (47/47) — PR #6 |
| WP-13 | Parser + evaluator (IRX#PARS) | 2 | DONE (38/38) — PR #8 |
| WP-14 | SAY + basic I/O routine (IRX#IO) | 2 | DONE (27/27) — PR #10 |
| WP-15 | DO/IF/SELECT/CALL/RETURN/EXIT/SIGNAL | 2 | DONE (62/62) — PR #12 |
| WP-16 | PARSE instruction | 2 | OPEN |
| WP-17 | PROCEDURE EXPOSE | 2 | OPEN |
| WP-18 | Hello World end-to-end | 2 | OPEN |
| WP-20 | Arithmetic engine (IRXARITH) | 3 | OPEN |
| WP-21 | Built-in string functions | 3 | OPEN |
| WP-22 | Built-in misc functions | 3 | OPEN |
| WP-23 | INTERPRET instruction | 3 | OPEN |
| WP-30 | EXECIO command | 4 | OPEN |
| WP-31 | Data stack (IRXSTK) | 4 | OPEN |
| WP-32 | Host command envs (TSO/MVS/LINK) | 4 | OPEN |
| WP-40 | IRXEXEC full implementation | 5 | OPEN |
| WP-41 | IRXJCL batch entry | 5 | OPEN |
| WP-42 | IRXEXCOM variable sharing | 5 | OPEN |
| WP-43 | IRXSUBCM subcommand table | 5 | OPEN |
| WP-50 | Replaceable routine install | 6 | OPEN |
| WP-51 | Exit routines | 6 | OPEN |
| WP-60 | TSO/E external functions | 7 | OPEN |
| WP-61 | Condition traps | 7 | OPEN |
| WP-62 | TRACE + debug commands | 7 | OPEN |
| WP-63 | Compliance testsuite | 7 | OPEN |

---

## WP-10: Tokenizer (IRXTOKN)

**Phase:** 2 — Interpreter Core
**Priority:** First WP of Phase 2 — everything depends on this
**Estimated LOC:** ~800

### Context

The tokenizer converts REXX source text into a stream of tokens.
It runs once per exec load (single-pass). The token stream is stored
in the `irx_wkblk_int.wkbi_tokens` pointer.

### Inputs

- Architecture Design v0.1.0, Section 7.2 (Token types table)
- SC28-1883-0, Chapter 2 (Structure and Syntax) — defines valid tokens
- `inc/irxwkblk.h` — where token stream is anchored
- Knowledge source: `brexx370/src/nextsymb.c` (704 LOC) — reference
  for token classification logic, BUT must be reimplemented without
  global state (all state in a tokenizer context struct)

### Outputs

- `inc/irxtokn.h` — Token type definitions, tokenizer context struct,
  function prototypes
- `src/irxtokn.c` — Tokenizer implementation
- `test/test_tokenizer.c` — Unit tests

### Token Types (from Architecture Design)

```
01  TOK_SYMBOL       FRED, A.B.C, X12
02  TOK_STRING       'hello', "world"
03  TOK_NUMBER       42, 3.14, 1E5
04  TOK_HEXSTRING    'FF'x
05  TOK_OPERATOR     + - * / // % **
06  TOK_COMPARISON   = == > < >= etc.
07  TOK_LOGICAL      & | &&
08  TOK_NOT          \ ¬
09  TOK_CONCAT       ||
0A  TOK_LPAREN       (
0B  TOK_RPAREN       )
0C  TOK_COMMA        ,
0D  TOK_SEMICOLON    ;
10  TOK_EOC          End of Clause
11  TOK_EOF          End of File
```

### Token Structure

Each token needs: type (byte), flags (byte), line number (int),
column (short), pointer to token text in source, length of token text.
Tokens should be stored in a contiguous array (not linked list) for
cache efficiency on the interpreter hot path.

### Constraints

- **Reentrant**: All state in a context struct, no globals
- **EBCDIC**: Source is EBCDIC on MVS 3.8j. Use character classification
  that works for both EBCDIC (MVS) and ASCII (cross-compile testing)
- **Comments**: `/* ... */` are stripped, but their line counts must
  be preserved for PARSE SOURCE / SOURCELINE / TRACE
- **Continuation**: comma at end of clause continues to next line
- **String delimiters**: both `'...'` and `"..."`, with doubling for
  escape (`'it''s'` = `it's`)
- **REXX identifier**: first record `/*...REXX...*/` identifies as REXX

### Acceptance Criteria

1. Tokenizes `say 'Hello World'` into: SYMBOL("SAY"), STRING("Hello World"), EOC, EOF
2. Handles compound symbols: `stem.i.j` as single SYMBOL token
3. Handles hex strings: `'FF'x` as TOK_HEXSTRING
4. Handles all operators from the spec
5. Strips comments, preserves line numbers
6. Handles continuation (trailing comma)
7. EBCDIC-safe character classification
8. All state in context struct (no globals, no statics)
9. Stress test: tokenize 1000-line exec without errors

---

## WP-11: LString Library Port

**Phase:** 2 — Interpreter Core
**Priority:** Parallel with WP-10
**Estimated LOC:** ~2000 (subset of brexx370's 6000)

### Context

REXX strings are typeless — they carry their value, length, maximum
allocated length, and an optional cached numeric type. The LString
library provides all string operations that built-in functions and
the interpreter need.

### Inputs

- `brexx370/inc/lstring.h` — Lstr structure definition, macros
- `brexx370/lstring/*.c` — 40+ string operation implementations
- `brexx370/inc/ldefs.h` — Type definitions

### Outputs

- `inc/lstring.h` — Lstr structure, macros, prototypes
- `src/lstring.c` — Core string operations
- `test/test_lstring.c` — Unit tests

### What to Port (Phase 2 subset)

Core operations needed for the interpreter:
- `Lfx()` — allocate/grow string buffer
- `Lscpy()`, `Lstrcpy()` — copy from C string / Lstr
- `Lcat()`, `Lstrcat()` — concatenate
- `Llen()` — length
- `Lsubstr()` — substring extraction
- `Lindex()`, `Lpos()` — search
- `Lverify()` — verify
- `Lupper()` — translate to uppercase
- `L2str()` — ensure string representation
- `_Lisnum()` — check if string is a valid number
- `Lprint()` — output (for SAY)

String functions for BIFs (can be added incrementally):
- LEFT, RIGHT, SUBSTR, COPIES, REVERSE, STRIP, SPACE
- WORD, WORDS, SUBWORD, WORDINDEX, WORDLENGTH, WORDPOS
- POS, LASTPOS, INDEX
- CENTER/CENTRE, INSERT, OVERLAY, DELSTR, DELWORD
- TRANSLATE, CHANGESTR, COUNTSTR
- C2D, C2X, D2C, D2X, X2C, X2D, B2X, X2B
- COMPARE, ABBREV, VERIFY, DATATYPE

### Constraints

- **Reentrant**: The Lstr structure is self-contained (no globals).
  The only global in brexx370 is the `Lerror` callback — this must
  be replaced with a per-environment error handler from `irx_wkblk_int`.
- **Memory**: All allocation must go through `irxstor()` (not direct
  malloc). The Lfx/LPMALLOC macros need to be adapted.
- **EBCDIC**: String comparison must work correctly on EBCDIC.
  brexx370 already handles this.

### Acceptance Criteria

1. Lstr allocate/copy/concatenate works correctly
2. All WORD-family functions produce correct results
3. Numeric detection (`_Lisnum`) handles REXX number format
4. Memory: all allocations go through irxstor
5. No global state (Lerror callback via context)
6. Unit tests for each ported function

---

## WP-12: Variable Pool

**Phase:** 2 — Interpreter Core
**Priority:** After WP-11 (needs LString)
**Estimated LOC:** ~600

### Context

The Variable Pool stores all REXX variables for an exec. It uses a
hash table for O(1) average lookup. Each exec invocation has its own
pool; PROCEDURE creates a new pool with an optional EXPOSE list.
Compound variables (stems) are resolved by deriving the tail value.

### Inputs

- Architecture Design v0.1.0, Section 7.3
- SC28-1883-0, Chapter 3 (Variables)
- `irxwkblk.h` — `wkbi_varpool` pointer
- Knowledge source: `brexx370/src/variable.c` (1217 LOC)
- Knowledge source: `brexx370/src/bintree.c` (753 LOC)

### Outputs

- `inc/irxvpool.h` — Variable pool structures, prototypes
- `src/irxvpool.c` — Variable pool implementation
- `test/test_vpool.c` — Unit tests

### Operations

- `vpool_create(parent, expose_list)` — Create new variable pool
- `vpool_destroy(pool)` — Free a variable pool
- `vpool_set(pool, name, value)` — Set a variable
- `vpool_get(pool, name, value)` — Get a variable (NOVALUE if unset)
- `vpool_drop(pool, name)` — Drop a variable
- `vpool_next(pool, cursor)` — Iterate (for SHVNEXTV)
- `vpool_exists(pool, name)` — Check existence (for SYMBOL())

### Constraints

- Hash table with chaining (not open addressing) for predictable perf
- Compound variable resolution: `stem.i.j` →
  1. Resolve value of `i`, resolve value of `j`
  2. Derive name: `STEM.` + value_of_i + `.` + value_of_j
  3. Look up derived name; if not found, return STEM. default value
- EXPOSE: When PROCEDURE EXPOSE lists a variable, the new pool's
  entry for that variable points to the parent pool's entry
- NOVALUE: If SIGNAL ON NOVALUE is active and a variable is accessed
  that has never been set, raise the NOVALUE condition
- All state in the pool structure, no globals

### Acceptance Criteria

1. Set/Get/Drop for simple variables
2. Compound variable resolution (stem.1, stem.i where i='FOO')
3. PROCEDURE creates isolated scope
4. EXPOSE correctly shares named variables with parent
5. NOVALUE detection
6. Iteration (NEXT) visits all variables exactly once
7. Hash table handles 10000 variables without degradation
8. No global state

---

## WP-13: Parser + Expression Evaluator (IRXPARS)

**Phase:** 2 — Interpreter Core
**Priority:** After WP-10 + WP-12
**Estimated LOC:** ~1500

### Context

The parser processes the token stream from WP-10 and executes
REXX clauses. REXX is interpreted — there is no separate AST or
bytecode compilation step (unlike brexx370 which compiles to bytecode).
Each clause is classified and dispatched: assignment, keyword
instruction, label, command, or null clause.

Expression evaluation implements REXX operator precedence:
prefix (\ + -), **, * / // %, + -, || (abuttal) blank,
comparison operators, & (AND), | && (OR XOR).

### Inputs

- Token stream from WP-10
- Variable pool from WP-12
- LString library from WP-11
- SC28-1883-0, Chapter 2 (Structure/Syntax), Chapter 6 (Numbers)
- Knowledge source: `brexx370/src/interpre.c` (1887 LOC),
  `brexx370/src/compile.c` (1951 LOC), `brexx370/src/expr.c` (439 LOC)

### Outputs

- `inc/irxpars.h` — Parser/evaluator context, prototypes
- `src/irxpars.c` — Parser + expression evaluator
- `test/test_parser.c` — Unit tests

### Design Decision: Direct Interpretation vs Bytecode

The architecture document says "tokenize in one pass, execute clause
by clause." This means direct interpretation of the token stream,
NOT compilation to bytecode like brexx370 does. This is simpler,
closer to IBM's original implementation, and avoids the complex
compile-state globals that made brexx370 non-reentrant.

For performance, frequently-executed DO loops can cache the token
position for fast re-entry without re-scanning.

### Constraints

- All state in parser context struct (no globals)
- Expression evaluation must handle: string comparison, numeric
  comparison, strict comparison (==, \==), concatenation (||, blank,
  abuttal), all arithmetic operators
- Function calls in expressions: `func(arg1, arg2)` — check internal
  labels first, then BIFs, then external
- Operator precedence must match SC28-1883-0 exactly

### Acceptance Criteria

1. `x = 2 + 3` evaluates to '5' (assignment + arithmetic)
2. `x = 'hello' 'world'` evaluates to 'hello world' (blank concat)
3. `x = a || b` evaluates correctly (explicit concat)
4. Correct operator precedence: `2 + 3 * 4` = '14'
5. String comparison: `'abc' = 'ABC'` is true (REXX is case-insensitive
   for comparison by default)
6. Strict comparison: `'abc' == 'ABC'` is false
7. Function calls in expressions: `length('hello')` = '5'
8. Nested parentheses: `(2 + 3) * (4 + 5)` = '45'
9. No global state

---

## WP-14: SAY + Basic I/O Routine (IRXIO)

**Phase:** 2 — Interpreter Core
**Priority:** After WP-13
**Estimated LOC:** ~300

### Context

The I/O Replaceable Routine handles all interpreter I/O. For Phase 2,
we need RXFWRITE (SAY) and RXFWRITERR (error messages). PULL and
dataset I/O come in Phase 4.

This is also the key embeddability hook: by replacing the I/O routine,
HTTPD can redirect SAY to HTTP response, ISPF to TPUT, etc.

### Inputs

- Architecture Design v0.1.0, Section 5.2
- `irxwkblk.h` — I/O function codes (RXFWRITE etc.)
- `irx.h` — IRXEXTE entry points (irxsay, io_routine)

### Outputs

- `src/irxio.c` — Default I/O routine (IRXINOUT)
- Test: SAY 'Hello World' produces output

### Implementation

```c
int irxinout(int function, char *data, int length,
             struct envblock *envblock)
{
    switch (function) {
    case RXFWRITE:    /* SAY */
        /* MVS: TPUT (TSO) or WTO (batch) */
        /* Cross: printf */
        break;
    case RXFWRITERR:  /* Error message */
        /* MVS: TPUT or WTO */
        /* Cross: fprintf(stderr) */
        break;
    /* Phase 4: RXFREAD, RXFREADP, RXFOPEN, RXFCLOSE, RXFREAD_DS, RXFWRITE_DS */
    }
}
```

### Acceptance Criteria

1. `SAY 'Hello World'` outputs "Hello World" followed by newline
2. Error messages go to stderr/SYSTSPRT
3. I/O routine is wired through IRXEXTE (replaceable)
4. No global state

---

## WP-18: Hello World End-to-End

**Phase:** 2 — Milestone
**Priority:** After WP-10 through WP-17
**Estimated LOC:** ~200 (glue + test)

### Context

This is the Phase 2 milestone: execute a real REXX exec end-to-end.

### Test Exec

```rexx
/* REXX */
say 'Hello World from REXX/370!'
x = 2 + 3
say 'The answer is' x
exit 0
```

### What Must Work

1. IRXINIT creates environment
2. Source loaded (from INSTBLK for testing)
3. Tokenizer produces token stream
4. Parser executes SAY (dispatches to I/O routine)
5. Parser evaluates `2 + 3`, assigns to `x`
6. Parser evaluates `'The answer is' x` (blank concat + variable ref)
7. EXIT sets return code
8. IRXTERM cleans up

### Acceptance Criteria

1. Output: `Hello World from REXX/370!` and `The answer is 5`
2. Return code: 0
3. No memory leaks (all GETMAIN balanced by FREEMAIN)
4. No global state accessed at any point

---

## Agent Instructions

### Build Infrastructure

**Compiler:** c2asm370 (C to HLASM cross-compiler)
**C Library:** crent370 (reentrant C runtime for MVS 3.8j)
**Build Tool:** mbt (mvslovers build tool) — see `project.toml`
**Target:** MVS 3.8j, AMODE 24, RMODE 24, reentrant (RENT)

### mbt Build

```bash
mbt build           # compile + link all modules
mbt build --target irxjcl   # build specific module
```

Cross-compile test (Linux/gcc):
```bash
# Standard dependency sets (expand as needed):
LSTRING_INC="-I contrib/lstring370-0.1.0-dev/include"
LSTRING_SRC="../lstring370/src/lstr#cor.c"
PHASE1="src/irx#init.c src/irx#term.c src/irx#stor.c src/irx#rab.c src/irx#uid.c src/irx#msid.c"
PHASE2="src/irx#io.c src/irx#lstr.c src/irx#tokn.c src/irx#vpol.c src/irx#pars.c src/irx#ctrl.c"

gcc -I include $LSTRING_INC -Wall -Wextra -std=gnu99 \
    -o test/test_NAME test/test_NAME.c \
    $PHASE1 $PHASE2 src/NEW_MODULE.c "$LSTRING_SRC"
```

### File Naming Convention

Source files use `prefix#module.c` (e.g., `irx#init.c`).
The `#` maps to a valid PDS member name character on MVS.
Header files use plain names in `include/` directory.

| Source File | PDS Member | Purpose |
|---|---|---|
| `src/irx#init.c` | IRX#INIT | IRXINIT implementation |
| `src/irx#term.c` | IRX#TERM | IRXTERM implementation |
| `src/irx#stor.c` | IRX#STOR | Storage management |
| `src/irx#rab.c` | IRX#RAB | RAB chain management |
| `src/irx#uid.c` | IRX#UID | User ID routine |
| `src/irx#msid.c` | IRX#MSID | Message ID routine |
| `src/irx#tokn.c` | IRX#TOKN | Tokenizer (WP-10) |
| `src/irx#lstr.c` | IRX#LSTR | lstring370 adapter (WP-11b) |
| `src/irx#vpol.c` | IRX#VPOL | Variable pool (WP-12) |
| `src/irx#pars.c` | IRX#PARS | Parser/Evaluator (WP-13) |
| `src/irx#io.c`   | IRX#IO   | Default I/O routine IRXINOUT (WP-14) |
| `src/irx#ctrl.c` | IRX#CTRL | Control flow: DO/IF/SELECT/CALL/SIGNAL (WP-15) |

### crent370 APIs

**Memory:** Use `calloc()`/`free()` from `<stdlib.h>` for regular
heap storage. Use `getmain(size, subpool)`/`freemain(addr, size, subpool)`
from `<clibos.h>` for specific subpool allocation.
In REXX/370, all allocation goes through `irxstor()` which wraps
the above — never call calloc/getmain directly from other modules.

**Console output:** `wtof()` from `<clibwto.h>` — formatted WTO.
Use for operator messages (IRX0001I etc.)

**Thread management:** `<clibthrd.h>` and `<clibthdi.h>` — not needed
for Phase 1-2, but relevant for future multi-threaded embedding.

**ESTAE recovery:** `<clibstae.h>` — Phase 6+ for ESTAE/abend recovery.

**OS services:** `<clibos.h>` — getmain, freemain, BLDL, LOAD, LINK,
OPEN/CLOSE, GET/PUT, TGET/TPUT, WTO/WTOR wrappers.

**Dataset I/O:** `<osio.h>`, `<osdcb.h>` — DCB/BSAM/QSAM.
Needed for Phase 4 (EXECIO).

### General Rules

1. **No globals.** Every piece of mutable state lives in a struct
   passed as parameter. If you're tempted to write `static` or
   `extern` for mutable data, put it in `irx_wkblk_int` or a
   sub-struct pointed to by it.

2. **Memory via irxstor.** Never call calloc/free directly outside
   of `irx#stor.c`. Always `irxstor(RXSMGET, ...)` /
   `irxstor(RXSMFRE, ...)`.

3. **Eye-catchers.** Every control block starts with an eye-catcher.
   Always validate eye-catchers before accessing block fields.

4. **IBM compatibility.** Never modify the layout of structs defined
   in `irx.h`. Our extensions go in `irxwkblk.h` or new headers.

5. **EBCDIC awareness.** Use character classification functions that
   work on both ASCII and EBCDIC. Don't hardcode ASCII values.

6. **Error paths.** Every allocation must have a corresponding
   deallocation on the error path. Use the ALLOC/cleanup pattern
   from irx#init.c.

7. **Testing.** Every WP produces a test file. Tests must be
   runnable on the cross-compile platform (Linux/gcc) without MVS.

8. **Comments in english.** All code comments and documentation in
   English. German only for user-facing documentation (manual etc.)

### Reference Material

- IBM TSO/E V2 REXX Reference: SC28-1883-0
  Available at: https://vtda.org (search for SC28-1883)
- Architecture Design v0.1.0: Notion page CON-1
  https://www.notion.so/3283d9938787811ba3f4d3308b254cad
- BREXX/370 source: https://github.com/mvslovers/brexx370
  (knowledge source for REXX BIF logic — reimplemented clean)
- crent370: https://github.com/mvslovers/crent370
- mbt build tool: https://github.com/mvslovers/mbt
