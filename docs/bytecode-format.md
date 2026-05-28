# REXX/370 Bytecode Format Reference

This document describes the EXECBLK container layout, the complete opcode
table, and the VM evaluation model as of WP-BC-06.

See `docs/architecture.md §15` for the design rationale.
Implementation files: `include/irxbops.h`, `include/irxexbl.h`,
`include/irxbvm.h`, `src/irx#bcom.c`, `src/irx#bvm.c`.

---

## 1. EXECBLK Container Layout

```
struct irx_bc_execblk {
    char          magic[4];         /* "RX37" eye-catcher           */
    uint16_t      version;          /* format version = 1           */
    uint16_t      flags;            /* reserved, zero               */
    uint32_t      const_count;      /* number of constant entries   */
    uint32_t      symbol_count;     /* number of symbol entries     */
    uint32_t      code_length;      /* bytecode length in bytes     */
    uint32_t      entry_offset;     /* offset of entry point in     */
                                    /* bytecode (usually 0)         */
    uint32_t      trace_map_offset; /* offset to trace map; 0=none  */
    /* followed by: Constants Table, Symbol Table, Bytecode */
};
```

Immediately after the header:

```
+----------------------------------+
| Constants Table                  |  const_count × 64 bytes
|  entry[0]: len(1) + data(63)     |
|  entry[1]: len(1) + data(63)     |
|  ...                             |
+----------------------------------+
| Symbol Table                     |  symbol_count × 64 bytes
|  entry[0]: len(1) + upper(63)    |  (uppercased variable names)
|  ...                             |
+----------------------------------+
| Bytecode                         |  code_length bytes
|  ...                             |
+----------------------------------+
| Trace Map (if present)           |  currently unused
+----------------------------------+
```

Constants: source literals (strings and numbers) exactly as written.
Symbols: variable names and internal label names, uppercased.

Each entry is exactly `IRXBC_ENTRY_SIZE = 64` bytes.  The first byte is
the data length (0–63); the remaining 63 bytes hold the data (`IRXBC_STR_MAX = 63`).
Strings longer than 63 bytes are not representable and cause a compile error.

### Access macros (`include/irxexbl.h`)

```c
IRXBC_CONST_TBL(bc)   /* char *    — start of Constants Table */
IRXBC_SYM_TBL(bc)     /* char *    — start of Symbol Table    */
IRXBC_CODE(bc)         /* unsigned char * — start of bytecode  */
IRXBC_ENTRY(bc)        /* unsigned char * — entry point        */
IRXBC_TOTAL(nc,ns,cl)  /* size_t   — total allocation size     */
```

---

## 2. Opcode Encoding

All opcodes are 1-byte values in the range 0x00–0xFF.  Operands follow
the opcode byte and are encoded little-endian:

- `u8`  — unsigned 8-bit (1 byte)
- `u16` — unsigned 16-bit little-endian (2 bytes)
- `i16` — signed 16-bit little-endian (2 bytes), relative jump offset

Jump offsets are relative to the byte **after the complete instruction**
(i.e. after the operand bytes).

The `OP_SIZE(op)` macro in `irxbops.h` returns the total byte size
(opcode + operands) for opcode `op`.

---

## 3. Complete Opcode Table

### 3.1 Phase 1 — Basics (WP-BC-01)

| Opcode | Hex | Size | Stack effect | Description |
|--------|-----|------|--------------|-------------|
| `OP_NOP` | 0x00 | 1 | — | No operation |
| `OP_EXIT` | 0x01 | 1 | — | Terminate, `*rc_out = 0` |
| `OP_NEWCLAUSE` | 0x02 | 1 | — | Clause boundary (TRACE hook) |
| `OP_EXIT_RC` | 0x03 | 1 | pop → RC | Pop TOS, convert to int, terminate |

### 3.2 Phase 2 — Stack and Variables (WP-BC-02)

| Opcode | Hex | Size | Stack effect | Description |
|--------|-----|------|--------------|-------------|
| `OP_PUSH_LIT` | 0x10 | 3 | → val | `const_idx:u16`; push constant from table |
| `OP_PUSH_TMP` | 0x11 | 1 | — | Reserved |
| `OP_POP` | 0x12 | 2 | pop n → | `n:u8`; discard N slots |
| `OP_DUP` | 0x13 | 1 | val → val val | Duplicate top slot |
| `OP_LOAD` | 0x20 | 3 | → val | `sym_idx:u16`; load variable into slot |
| `OP_STORE` | 0x21 | 3 | val → | `sym_idx:u16`; pop TOS and store into variable |
| `OP_DROP` | 0x22 | 3 | — | `sym_idx:u16`; DROP variable |

### 3.3 Phase 2 — Arithmetic (WP-BC-02)

Binary ops pop two slots, push one result.  Slots carry `type_cache` and
`int_cache` for the integer fast path (`try_arith_fast`).

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_ADD` | 0x30 | 1 | `a + b` |
| `OP_SUB` | 0x31 | 1 | `a - b` |
| `OP_MUL` | 0x32 | 1 | `a * b` |
| `OP_DIV` | 0x33 | 1 | `a / b` (REXX numeric division) |
| `OP_IDIV` | 0x34 | 1 | `a % b` (integer divide, truncate-to-zero) |
| `OP_MOD` | 0x35 | 1 | `a // b` (remainder) |
| `OP_POW` | 0x36 | 1 | `a ** b` |
| `OP_NEG` | 0x37 | 1 | Unary minus (pop 1, push 1) |

### 3.4 Phase 2 — Comparison (WP-BC-02)

Pop two slots, push "0" or "1".

| Opcode | Hex | Size | REXX operator |
|--------|-----|------|---------------|
| `OP_EQ` | 0x40 | 1 | `a = b` (fuzzy equal) |
| `OP_NE` | 0x41 | 1 | `a \= b` |
| `OP_LT` | 0x42 | 1 | `a < b` |
| `OP_LE` | 0x43 | 1 | `a <= b` |
| `OP_GT` | 0x44 | 1 | `a > b` |
| `OP_GE` | 0x45 | 1 | `a >= b` |
| `OP_DEQ` | 0x46 | 1 | `a == b` (strict equal) |
| `OP_DNE` | 0x47 | 1 | `a \== b` |
| `OP_DLT` | 0x48 | 1 | `a << b` |
| `OP_DLE` | 0x49 | 1 | `a <<= b` |
| `OP_DGT` | 0x4A | 1 | `a >> b` |
| `OP_DGE` | 0x4B | 1 | `a >>= b` |

### 3.5 Phase 2 — Logical and String (WP-BC-02)

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_AND` | 0x50 | 1 | `a & b` — logical AND (pops 2, pushes "0"/"1") |
| `OP_OR` | 0x51 | 1 | `a \| b` — logical OR |
| `OP_XOR` | 0x52 | 1 | `a && b` — logical XOR |
| `OP_NOT` | 0x53 | 1 | `\a` — logical NOT (pops 1, pushes "0"/"1") |
| `OP_CONCAT` | 0x60 | 1 | `a \|\| b` — concatenate without separator |
| `OP_BCONCAT` | 0x61 | 1 | `a b` — concatenate with one blank |
| `OP_SAY` | 0x70 | 1 | Pop TOS, output via IRXINOUT |

### 3.6 Phase 3 — Control Flow (WP-BC-03)

Jump offsets are signed 16-bit, relative to the byte after the instruction.

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_JMP` | 0x04 | 3 | `off:i16` — unconditional jump |
| `OP_JF` | 0x05 | 3 | `off:i16` — jump if top-of-stack false, pop |
| `OP_JT` | 0x06 | 3 | `off:i16` — jump if top-of-stack true, pop |
| `OP_TOINT` | 0x71 | 1 | Coerce TOS to integer string |
| `OP_FORINIT` | 0x72 | 2 | `n:u8` — pop count → frame[n]; push bool (count>0) |
| `OP_BYINIT` | 0x73 | 2 | `n:u8` — reserved |
| `OP_DECFOR` | 0x74 | 4 | `n:u8` + `off:i16` — decrement frame[n]; jump-if-done |
| `OP_DOTEST` | 0x75 | 1 | Reserved (WHILE/UNTIL via JF) |
| `OP_ITERATE` | 0x76 | 3 | `off:i16` — jump to iterate point |
| `OP_LEAVE` | 0x77 | 3 | `off:i16` — jump to loop end |

**DO count loop protocol:**

- Push count expression, `OP_FORINIT n` (pops count, pushes bool), `OP_JF exit`.
- Body, `OP_DECFOR n exit_off` (decrements; if done jumps forward to exit).
- `OP_JMP` back to top of body.
- ITERATE: `OP_ITERATE` (offset to `OP_DECFOR`).
- LEAVE: `OP_LEAVE` (offset to after the loop).
- `DO FOREVER`: `OP_JMP` back to top of body unconditionally.

### 3.7 Phase 4 — CALL/RETURN (WP-BC-04)

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_LABEL` | 0x78 | 3 | `sym_idx:u16` — call target definition (no-op at runtime) |
| `OP_CALL` | 0x79 | 4 | `sym_idx:u16`, `nargs:u8` — CALL statement |
| `OP_CALL_BIF` | 0x7A | 4 | `sym_idx:u16`, `nargs:u8` — expression BIF call (pushes result) |
| `OP_RETURN` | 0x7B | 1 | Return from CALL (no value) |
| `OP_RETURNV` | 0x7C | 1 | Pop TOS, store as RESULT, return from CALL |

Before `OP_CALL`/`OP_CALL_BIF`, args are pushed on the eval stack (arg N first,
arg 1 last).  The VM saves them into the call frame and pops them.  On
`OP_RETURN`/`OP_RETURNV` the call frame is popped; with a return value it lands
on the eval stack of the caller.

### 3.8 Phase 5 — PARSE Sub-VM (WP-BC-05 PR A)

The PARSE sub-VM operates on a single source string.  The source is pushed on
the eval stack before `OP_PARSE_BEGIN`, which pops it.  The `flags:u8` operand
encodes the parse variant: bit 0 = UPPER.  Template items are pairs of target +
trigger.

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_PARSE_BEGIN` | 0x80 | 2 | `flags:u8` — start PARSE block; pop source from stack |
| `OP_PARSE_END` | 0x81 | 1 | End PARSE block, free source |
| `OP_PVAR` | 0x82 | 3 | `sym_idx:u16` — assign segment to simple variable |
| `OP_PDOT` | 0x83 | 1 | Dot placeholder — discard segment |
| `OP_TR_SPACE` | 0x84 | 1 | Trigger: one word (leading whitespace stripped) |
| `OP_TR_LIT` | 0x85 | 3 | `lit_idx:u16` — trigger: literal string delimiter |
| `OP_TR_ABS` | 0x86 | 3 | `col:u16` — trigger: absolute column (1-based) |
| `OP_TR_REL` | 0x87 | 3 | `off:i16` — trigger: relative offset |
| `OP_TR_END` | 0x88 | 1 | Trigger: rest of string (last item) |
| `OP_PUSH_SOURCE` | 0x89 | 1 | Push PARSE SOURCE string |
| `OP_PUSH_NUMERIC` | 0x8A | 1 | Push PARSE NUMERIC string |

Each template item is a target (`OP_PVAR`, `OP_PDOT`) followed immediately by a
trigger.  `OP_TR_SPACE` is the implicit trigger for bare variable names.  The
last item in a template always ends with `OP_TR_END`.

Variable delimiters `(varname)` in templates are rejected with
`IRXBC_ERR_UNSUP`.

### 3.9 Phase 5 — PROCEDURE EXPOSE (WP-BC-05 PR B)

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_PROC` | 0x8B | 2 | `nexposed:u8` — PROCEDURE: isolate variable scope |
| `OP_EXPOSE` | 0x8C | 3 | `sym_idx:u16` — EXPOSE one named variable |
| `OP_EXPOSE_INDIRECT` | 0x8D | 3 | `sym_idx:u16` — EXPOSE via name-variable |

`OP_PROC` is emitted immediately after the function label.  `nexposed` is the
count of following `OP_EXPOSE`/`OP_EXPOSE_INDIRECT` instructions (informational).

### 3.10 Phase 5 — Compound Variables (WP-BC-05 PR C)

Compound variables (e.g. `A.I.J`) push `tail_count` tail expressions onto the
eval stack before the compound opcode.

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_LOAD_STEM` | 0x8E | 4 | `stem_sym:u16`, `tail_count:u8` — load compound var |
| `OP_STORE_STEM` | 0x8F | 4 | `stem_sym:u16`, `tail_count:u8` — store compound var |
| `OP_DROP_STEM` | 0x90 | 4 | `stem_sym:u16`, `tail_count:u8` — DROP; `tail_count=0` drops all |
| `OP_PVAR_STEM` | 0x91 | 4 | `stem_sym:u16`, `tail_count:u8` — PARSE target: compound var |
| `OP_PULL_FROM_QUEUE` | 0x92 | 1 | Push next external queue line (WP-33b stub; raises `IRXBC_ERR_UNSUP`) |

Before any stem opcode, the caller pushes `tail_count` tail values (constants
via `OP_PUSH_LIT`, variables via `OP_LOAD`).  The opcode pops them, uppercases
them, concatenates with the stem name to form the full variable name, then calls
`vpool_get_buf` / `vpool_set_buf` / `vpool_drop_buf`.

---

## 4. Evaluation Stack

- Depth: 256 slots (`IRXBC_STACK_DEPTH`).
- Each slot: `struct bc_stack_slot` = `PLstr str` + `int32_t type_cache` + `int32_t int_cache`.
- `str` points into a parallel `Lstr` array allocated at VM init.
- SP points to the **next free** slot.  Push: `stack[sp++]`.  Pop: `stack[--sp]`.
- `type_cache == IRXBC_STACK_LINTEGER (1)` means `int_cache` holds the parsed integer.

---

## 5. VM Resource Limits

| Resource | Limit | Configured by |
|----------|-------|---------------|
| Eval stack depth | 256 | `IRXBC_STACK_DEPTH` |
| DO loop nesting | 16 | `IRXBC_DO_DEPTH` |
| CALL depth | 16 | `IRXBC_CALL_DEPTH` |
| ARG() max arguments | `IRX_MAX_ARGS` | `include/irxfunc.h` |
| Constant/symbol name length | 63 bytes | `IRXBC_STR_MAX` |

---

## 6. Constant Type-Cache (OC-07, WP-BC-06)

At VM init, `irx_bc_execute()` allocates two `int32_t[n_consts]` arrays:

```
const_type_cache[i]  — IRXBC_STACK_LINTEGER or 0
const_int_cache[i]   — parsed integer value for const i
```

Each constant is pre-parsed using the same digit-scan logic as
`try_parse_int_cache()`.  On `OP_PUSH_LIT`, instead of calling the parse
function at runtime, the VM reads the pre-computed values directly:

```c
stack[sp].type_cache = const_type_cache[idx];
stack[sp].int_cache  = const_int_cache[idx];
```

This eliminates repeated digit-parsing of constants such as loop bounds
(`1`, `14`, `100`), comparison values (`0`, `5`), and BIF arguments.

---

### 3.11 SIGNAL (WP-BC-07 PR A)

Unconditional `SIGNAL label` and `SIGNAL VALUE expr`.  Both opcodes clear the
eval stack, unwind all active call frames (restoring isolated variable scopes
created by `OP_PROC`), close any active PARSE frame, set `SIGL=0` in the work
block, and jump to the target label.

Labels must be defined somewhere in the bytecode stream via `OP_LABEL`.  The
VM pre-scans for `OP_LABEL` at startup (same scan as for `OP_CALL` targets)
and builds `label_pc[sym_idx]`.  No change to the EXECBLK header is required.

SIGL line-number tracking is not yet implemented (requires trace-map support in
a later WP); `wkbi_sigl` is set to 0.

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_SIGNAL` | 0x93 | 3 | `sym_idx:u16` — jump to label (compile-time name) |
| `OP_SIGNAL_VALUE` | 0x94 | 1 | Pop label-name string, uppercase, resolve, jump |
| `OP_SIGNAL_ON` | 0x95 | 4 | `cond:u8`, `sym_idx:u16` — enable condition trap (WP-BC-07 PR B) |
| `OP_SIGNAL_OFF` | 0x96 | 2 | `cond:u8` — disable condition trap (WP-BC-07 PR B) |

`OP_SIGNAL_ON` and `OP_SIGNAL_OFF` are defined and the compiler parses `SIGNAL
ON`/`SIGNAL OFF` syntax, but the compiler returns `IRXBC_ERR_UNSUP` for these
forms in PR A, falling back to the token-walk interpreter.  VM handlers for
these opcodes are deferred to WP-BC-07 PR B.

---

## 7. Compiler Limitations (current)

The following constructs cause `IRXBC_ERR_UNSUP` from the compiler:

- `SIGNAL ON condition` / `SIGNAL OFF condition` (deferred to WP-BC-07 PR B)
- `TRACE value_expression`
- `ADDRESS environment expression`
- `PARSE PULL` / `PARSE LINEIN` (`OP_PULL_FROM_QUEUE` stub raises `IRXBC_ERR_UNSUP` at runtime)
- Semicolons as clause separators within a source line
- Variable delimiter `(varname)` in PARSE templates

These limitations are tracked as follow-up items for WP-BC-07 PR B and WP-BC-08+.

---

## 8. Error Codes

| Constant | Value | Meaning |
|----------|-------|---------|
| `IRXBC_OK` | 0 | Success |
| `IRXBC_ERR_STOR` | 20 | `irxstor` allocation failed |
| `IRXBC_ERR_TOKN` | 21 | Tokenizer returned an error |
| `IRXBC_ERR_UNSUP` | 22 | Unsupported construct (compile-time) |
| `IRXBC_ERR_OPCODE` | 23 | Unknown opcode (VM runtime) |
| `IRXBC_ERR_ARITH` | 24 | Arithmetic error (type, divide-by-zero) |
| `IRXBC_ERR_STACK` | 25 | Stack underflow or overflow |
| `IRXBC_ERR_PATCH` | 26 | Too many forward-jump patches |
| `IRXBC_ERR_LOOP` | 27 | DO nesting too deep |
| `IRXBC_ERR_IO` | 28 | I/O routine call failed |
| `IRXBC_ERR_STRTOOLONG` | 29 | Literal/symbol exceeds `IRXBC_STR_MAX` |
| `IRXBC_ERR_CALL` | 30 | CALL stack overflow |
| `IRXBC_ERR_PARSE_COMPOUND` | 31 | Compound variable target in PARSE template |
