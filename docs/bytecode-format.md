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
| `OP_EXIT_RC` | 0x03 | 1 | pop → RC | Terminate, RC from top of stack |
| `OP_LABEL` | 0x04 | 3 | — | `sym_idx:u16` — label definition point |

### 3.2 Phase 2 — Stack and Variables (WP-BC-02)

| Opcode | Hex | Size | Stack effect | Description |
|--------|-----|------|--------------|-------------|
| `OP_PUSH_LIT` | 0x10 | 3 | → val | `const_idx:u16`; push constant from table |
| `OP_POP` | 0x11 | 2 | pop n → | `n:u8`; discard N slots |
| `OP_DUP` | 0x12 | 1 | val → val val | Duplicate top slot |
| `OP_LOAD` | 0x20 | 3 | → val | `sym_idx:u16`; load variable into slot |
| `OP_STORE` | 0x21 | 3 | val → | `sym_idx:u16`; store top into variable (no pop) |
| `OP_DROP` | 0x22 | 3 | — | `sym_idx:u16`; DROP variable |

**STORE does not pop.** The value remains on the stack for use in
chained assignments or expression context.

### 3.3 Phase 2 — Arithmetic (WP-BC-02)

Binary ops pop two slots, push one result.  Slots carry `type_cache` and
`int_cache` for the integer fast path (`try_arith_fast`).

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_ADD` | 0x30 | 1 | `a + b` |
| `OP_SUB` | 0x31 | 1 | `a - b` |
| `OP_MUL` | 0x32 | 1 | `a * b` |
| `OP_DIV` | 0x33 | 1 | `a / b` (REXX numeric division) |
| `OP_IDIV` | 0x34 | 1 | `a %/% b` (integer divide, `%/%` in REXX is `%`) |
| `OP_MOD` | 0x35 | 1 | `a // b` (remainder) |
| `OP_POW` | 0x36 | 1 | `a ** b` |
| `OP_NEG` | 0x37 | 1 | Unary minus (pop 1, push 1) |

### 3.4 Phase 2 — Comparison (WP-BC-02)

Pop two slots, push "0" or "1".

| Opcode | Hex | Size | REXX operator |
|--------|-----|------|---------------|
| `OP_EQ` | 0x40 | 1 | `a = b` |
| `OP_NE` | 0x41 | 1 | `a \= b` |
| `OP_LT` | 0x42 | 1 | `a < b` |
| `OP_LE` | 0x43 | 1 | `a <= b` |
| `OP_GT` | 0x44 | 1 | `a > b` |
| `OP_GE` | 0x45 | 1 | `a >= b` |
| `OP_SEQ` | 0x46 | 1 | `a == b` (strict) |
| `OP_SNE` | 0x47 | 1 | `a \== b` |
| `OP_SLT` | 0x48 | 1 | `a << b` |
| `OP_SLE` | 0x49 | 1 | `a <<= b` |
| `OP_SGT` | 0x4A | 1 | `a >> b` |
| `OP_SGE` | 0x4B | 1 | `a >>= b` |

### 3.5 Phase 2 — Logical and String (WP-BC-02)

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_AND` | 0x50 | 1 | Logical AND (pops 2, pushes "0"/"1") |
| `OP_OR` | 0x51 | 1 | Logical OR |
| `OP_XOR` | 0x52 | 1 | Logical XOR |
| `OP_NOT` | 0x53 | 1 | Logical NOT (pops 1, pushes "0"/"1") |
| `OP_CONCAT` | 0x60 | 1 | `a \|\| b` — concatenate without separator |
| `OP_BCONCAT` | 0x61 | 1 | `a b` — concatenate with one blank |
| `OP_SAY` | 0x70 | 1 | Output top of stack via IRXINOUT, pop |

### 3.6 Phase 3 — Control Flow (WP-BC-03)

Jump offsets are signed 16-bit, relative to the byte after the instruction.

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_JMP` | 0x71 | 3 | `off:i16` — unconditional jump |
| `OP_JF` | 0x72 | 3 | `off:i16` — jump if top-of-stack false, pop |
| `OP_JT` | 0x73 | 3 | `off:i16` — jump if top-of-stack true, pop |
| `OP_FORINIT` | 0x74 | 1 | Initialize count-loop frame (pops count) |
| `OP_BYINIT` | 0x75 | 3 | `sym_idx:u16` — init TO-BY loop (loop var) |
| `OP_DECFOR` | 0x76 | 3 | `off:i16` — decrement count frame; jump if done |
| `OP_DECINC` | 0x77 | 3 | `off:i16` — step TO-BY frame; jump if done |
| `OP_FOREND` | 0x78 | 1 | Pop top DO frame |
| `OP_NOVALUE` | 0x7F | 3 | `sym_idx:u16` — push variable name (for NOVALUE) |

**DO loop protocol:**

- `DO count`: emit expression for count, `OP_FORINIT`, body, `OP_DECFOR` (with back-edge jump).
- `DO var = start TO limit BY step`: emit start/limit/step stores, `OP_BYINIT`, body, `OP_DECINC`.
- `DO FOREVER`: `OP_JMP` (forward), body, `OP_JMP` (back).
- ITERATE: `OP_JMP` to the `OP_DECFOR`/`OP_DECINC` instruction.
- LEAVE: `OP_FOREND` + `OP_JMP` past the loop.

### 3.7 Phase 4 — CALL/RETURN (WP-BC-04)

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_CALL` | 0x80 | 4 | `sym_idx:u16`, `nargs:u8` — internal label call |
| `OP_RETURN` | 0x81 | 1 | Return from CALL (no value) |
| `OP_RETURNV` | 0x82 | 1 | Return from CALL (value on stack) |
| `OP_CALL_BIF` | 0x83 | 4 | `bif_id:u16`, `nargs:u8` — BIF call |
| `OP_PROC` | 0x7D | 1 | PROCEDURE (isolate variable scope) |
| `OP_EXPOSE` | 0x7E | 3 | `sym_idx:u16` — EXPOSE one variable |
| `OP_EXPOSE_INDIRECT` | 0x7B | 3 | `sym_idx:u16` — EXPOSE via name-variable |

Before `OP_CALL`, args are pushed on the eval stack (arg N first, arg 1
last).  The VM saves them into the call frame and pops them.  On `OP_RETURN`/
`OP_RETURNV` the call frame is popped; with a return value, it lands on the
eval stack of the caller.

`OP_PROC` must be the first instruction in a subroutine that uses
`PROCEDURE [EXPOSE ...]`.

### 3.8 Phase 5 — PARSE Sub-VM (WP-BC-05 PR A)

The PARSE sub-VM operates on a single source string, advancing a scan
pointer according to trigger instructions, and assigning sub-strings to
target variables.

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_PARSE_BEGIN` | 0x90 | 3 | `sym_idx:u16` — PARSE VAR (source = variable) |
| `OP_PARSE_BEGIN_UPPER` | 0x91 | 3 | PARSE UPPER VAR |
| `OP_PARSE_BEGIN_ARG` | 0x92 | 1 | PARSE ARG (source = ARG(1)) |
| `OP_PARSE_BEGIN_ARG_UPPER` | 0x93 | 1 | PARSE UPPER ARG |
| `OP_PARSE_BEGIN_VALUE` | 0x94 | 1 | PARSE VALUE (source = top of stack, popped) |
| `OP_PARSE_BEGIN_VALUE_UPPER` | 0x95 | 1 | PARSE UPPER VALUE |
| `OP_PARSE_PULL_STUB` | 0x96 | 1 | PARSE PULL — stub, raises `IRXBC_ERR_UNSUP` at runtime |
| `OP_PARSE_END` | 0x97 | 1 | End PARSE block |

**Target instructions** (within PARSE block):

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_PVAR` | 0xA0 | 3 | `sym_idx:u16` — assign segment to simple variable |
| `OP_PDOT` | 0xA1 | 1 | Placeholder (`.`) — discard segment |
| `OP_PVAR_STEM` | 0xA2 | 3 | `const_idx:u16` — assign to pre-resolved compound name |

**Trigger instructions** (follow a target, delimit the segment):

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_TR_SPACE` | 0x84 | 1 | Word trigger (one word, leading spaces stripped) |
| `OP_TR_LIT` | 0x85 | 3 | `const_idx:u16` — literal string delimiter |
| `OP_TR_ABS` | 0x86 | 3 | `col:u16` — absolute column (1-based) |
| `OP_TR_REL` | 0x87 | 3 | `off:i16` — relative offset |
| `OP_TR_END` | 0x88 | 1 | Rest of string (end of template) |

Each template item is a target followed by a trigger.  `OP_TR_SPACE` is the
implicit trigger for bare variable names.  The last item in a template always
uses `OP_TR_END`.

### 3.9 Phase 5 — Compound Variables (WP-BC-05 PR C)

Compound variables (e.g. `A.I.J`, `STEM.KEY`) push their tail expressions
onto the eval stack before the compound opcode.

| Opcode | Hex | Size | Description |
|--------|-----|------|-------------|
| `OP_LOAD_CMPD` | 0xB0 | 4 | `stem_idx:u16`, `n_tails:u8` — load compound |
| `OP_STORE_CMPD` | 0xB1 | 4 | Store value (top of stack) into compound |
| `OP_DROP_CMPD` | 0xB2 | 4 | DROP compound variable |
| `OP_NOVALUE_CMPD` | 0xB3 | 4 | Push compound name string (for NOVALUE) |
| `OP_DROP_STEM` | 0xB4 | 3 | `stem_idx:u16` — DROP entire stem |

**Before any compound opcode**, the caller pushes `n_tails` tail values.
The opcode pops the tails, looks up the stem name from the symbol table,
concatenates the resolved tail values (after uppercasing) to form the full
variable name, then calls `vpool_get_buf` / `vpool_set_buf` / `vpool_drop_buf`.

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

## 7. Compiler Limitations (current)

The following constructs cause `IRXBC_ERR_UNSUP` from the compiler:

- `SIGNAL ON condition` / `SIGNAL OFF`
- `TRACE value_expression`
- `ADDRESS environment expression`
- `PARSE PULL` / `PARSE LINEIN` (emits `OP_PARSE_PULL_STUB`, which raises UNSUP at runtime)
- Semicolons as clause separators within a source line
- Variable delimiter `(varname)` in PARSE templates

These limitations are tracked as follow-up items for WP-BC-07+.

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
