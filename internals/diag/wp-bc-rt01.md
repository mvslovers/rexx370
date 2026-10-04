# WP-BC-RT01 — REXXCPS Runtime Divergence (RC24 / ARITH) — Diagnosis

**Ticket:** TSK-261 (Notion 3733d993878781a6b412d2c780a050dc)
**Type:** Diagnosis only — NO product fix (the fix is a separate ticket).
**Diagnosis branch:** `wp-bc-rt01-diagnose`
**HEAD commit:** `b0fcb68` — *feat(bytecode): WP-BC-CONTCOMMA — trailing-comma line continuation (#175)*

---

## TL;DR

REXXCPS aborts under the bytecode VM with **RC24 = IRXBC_ERR_ARITH** because the
bytecode **compiler** mis-compiles **abuttal concatenation** (two adjacent value
terms with *no* blank between them, e.g. `'a'v'b'` or `1.0''loop`).

- **Root cause (single defect):** `src/irx#bcom.c:2238`, function `bc_exp3`.
  For an implicit concatenation (no `||` operator) the compiler **unconditionally**
  emits `OP_BCONCAT` (0x61, "insert one blank"). It never emits `OP_CONCAT`
  (0x60, direct/no-blank) for true abuttal. The token-walk parser gets this right
  via `toks_adjacent()` (`src/irx#pars.c:161`, used in `parse_concat` at
  `src/irx#pars.c:4582`); the compiler simply omits that check.
- **VM error site that fires:** `src/irx#bvm.c:1425` (the `OP_ADD/SUB/MUL/...`
  case). `irx_arith_op` returns `IRXPARS_SYNTAX` (20) because one operand is the
  blank-poisoned string `"1.0  1"`, which `lstr_to_num` rejects. The VM maps that
  to `IRXBC_ERR_ARITH` (24). Lines 2000/2131 (BIF→ARITH) and 1447 (OP_NEG) do
  **not** fire.
- **Multiple independent triggers? No.** One compiler defect; it fires wherever
  REXXCPS uses abuttal. The four constructs named as suspects in the ticket are
  **red herrings** — proven below.

---

## Methodology

Host-side A/B comparison, no MVS needed. Built `test/host/tstcps_host.c`
(`--source=<file>`) with the full engine + bytecode sources and ran every repro
twice:

- `REXX370_BYTECODE=0` → **token-walk** (the correct reference)
- default → **bytecode** (the one that aborts)

`IRXBC_ERR_ARITH` value confirmed = **24** (`include/irxbops.h:378`).

The VM error site and operands were pinned with gdb (breaking at the four
`IRXBC_ERR_ARITH` sites; `irx#bvm.c:1425` is the one that fires). The faulty
operand bytes were read directly from the `Lstr` slots. The wrong opcode was
confirmed at compile time with gdb breakpoints on the two emit sites.

> Note: tracing arithmetic by *calling the inferior `printf`* mid-run perturbs
> execution and reports a misleading "last op". The reliable result comes from a
> conditional breakpoint (`arc != 0`) with no inferior call. Reported here are the
> reliable values.

---

## The four ticket suspects are red herrings

Each tested in isolation, A/B. **None throws RC24**; all complete RC=0 under both
engines:

| # | Construct (snippet)                 | Token-walk | Bytecode | Diverges? |
|---|-------------------------------------|------------|----------|-----------|
| 1 | `do j=1.1 to 2.2 by 1.1`            | RC=0       | RC=0     | value/format only, no RC24 |
| 2 | `count=(1%total + 1) * count`       | RC=0       | RC=0     | leading-space only, no RC24 |
| 3 | `avar.1.2=avar.1.2*1.1` (decimal)   | RC=0       | RC=0     | leading-space only, no RC24 |
| 4 | `flag=5+99.7` (decimal add in WHEN) | RC=0       | RC=0     | no |

The "leading space" they showed (`count= 300`) was the visible tip of the *actual*
defect (abuttal mis-compiled in `say 'count='...` style expressions), not an
arithmetic bug.

---

## Root-cause demonstration (clean, no crash) — `b1`

```rexx
v='X'
say 'a'v'b'
say '['v']'
```

| | Output |
|---|---|
| Token-walk (correct) | `aXb` <br> `[X]` |
| Bytecode (wrong)     | `a X b` <br> `[ X ]` |

Both RC=0. `'a'v'b'` is pure abuttal (three adjacent terms, no blanks, no `||`).
Token-walk concatenates with no blank → `aXb`. Bytecode inserts a blank at each
junction → `a X b`. This is the whole bug, with no arithmetic and no stems.

`b2` (`say a b`, two vars separated by a real blank) is identical under both
engines (`X Y`) — **blank concatenation is fine; only abuttal is broken.**

### Codegen proof (gdb, compile time)

For `say 'a'v'b'` the compiler hits the emit sites as:

```
EMIT OP_BCONCAT (0x61, blank-concat) at bcom.c:2238
EMIT OP_BCONCAT (0x61, blank-concat) at bcom.c:2238
```

`OP_CONCAT` (0x60) at `bcom.c:2229` is **never** emitted. Two abuttal junctions →
two `OP_BCONCAT` → two inserted blanks.

---

## Minimal RC24 repro — `min2`

This is the minimal snippet that **runs correctly under token-walk and aborts with
RC24 under bytecode**, mirroring the exact REXXCPS chain (lines 126 + 131/138):

```rexx
loop=1
avar.=1.0''loop
say 'avar.1.2=['avar.1.2']'
avar.1.2=avar.1.2*1.1
say 'result=['avar.1.2']'
```

| | Output | RC |
|---|---|---|
| Token-walk (correct) | `avar.1.2=[1.01]` <br> `result=[1.111]` | 0 |
| Bytecode (wrong)     | `avar.1.2=[ 1.0  1 ]` <br> *(aborts)* | **24** |

### Why "1.0  1"

`avar.=1.0''loop` sets the stem default to the abuttal `1.0` ⋅ `''` ⋅ `loop`
(three adjacent terms):

- Token-walk: `"1.0"` + `""` + `"1"` = **`"1.01"`** — a valid number.
- Bytecode (blank at every junction): `"1.0"` + `" "` + `""` + `" "` + `"1"` =
  **`"1.0  1"`** (two blanks) — *not* a valid number.

Then `avar.1.2` (unset → returns the stem default `"1.0  1"`) is used in
`avar.1.2*1.1`. `irx_arith_op` calls `lstr_to_num("1.0  1")`, which fails →
`IRXPARS_SYNTAX` → `IRXBC_ERR_ARITH` (24) at `irx#bvm.c:1425`.

In the real program the same thing happens at `rexxcps.rexx:126` feeding
`:131`/`:138`; that is the abort the ticket observed (header printed, then RC24
before the report).

### Repro caveat (why the stem form)

`x=1.0''loop` with a *simple* variable fails under **both** engines (a separate,
common-mode `''`-immediately-after-a-numeric-literal quirk — **not** a divergence,
proven by A/B, out of scope here). The **stem** form `avar.=1.0''loop` runs cleanly
under token-walk, which is why `min2` uses it. Anyone reducing to the simple-var
form will see both engines fail and wrongly conclude the repro is broken.

---

## Acceptance criteria

1. **Minimal repro** — `min2` above (token-walk correct, bytecode RC24). ✅
2. **Exact VM site + hypothesis** — `irx#bvm.c:1425` (OP_MUL → `irx_arith_op` →
   `IRXPARS_SYNTAX` on the blank-poisoned operand). Root cause is upstream in the
   compiler at `irx#bcom.c:2238`. ✅
3. **Multiple independent triggers?** — **No.** One compiler defect at
   `bcom.c:2238`, firing at every abuttal site. The four ticket suspects are red
   herrings (table above). Any *other* runtime divergence that may surface *after*
   this fix is explicitly out of scope per the ticket. ✅
4. **Fix-ticket proposal** — below. ✅
5. **No product code change in this ticket.** ✅ (No repro test committed — it
   would fail RED under default bytecode today; it should land *with* the fix.)

---

## Proposal for the fix ticket

**Title (suggested):** `WP-BC-RT01-FIX: bytecode compiler must distinguish abuttal
from blank concatenation`

**What & where:** In `src/irx#bcom.c`, `bc_exp3` (the implicit-concat branch,
currently lines ~2231-2239):

```c
else if (is_value_starter(ctx, 0))
{
    bc_exp4(ctx);
    emit_byte(ctx, OP_BCONCAT);   /* BUG: always blank */
}
```

Capture the last token of the left operand (the token at `ctx->pos - 1`) *before*
`bc_exp4` consumes the next term, then choose the opcode by source adjacency,
mirroring the token-walk reference:

- adjacent in source (no blank gap, same line) → `OP_CONCAT` (0x60, abuttal)
- otherwise → `OP_BCONCAT` (0x61, blank concat)

The adjacency test already exists in the token-walk side: `toks_adjacent()` /
`tok_source_end_col()` (`src/irx#pars.c:161` / `:141`), used by `parse_concat`
(`src/irx#pars.c:4582`). The token struct carries everything needed
(`tok_col`, `tok_line`, `tok_length`, `tok_type` — `include/irxtokn.h:65`).
Hex/bin/quoted strings need the same column adjustment `tok_source_end_col`
already applies.

**Side note (free cleanup):** the header comment on `OP_BCONCAT`
(`include/irxbops.h:117`) reads *"abuttal with one blank"* — that conflates abuttal
(no blank) with blank concatenation. Suggest re-wording to *"blank concatenation
(one blank)"* to avoid perpetuating the confusion that likely seeded this bug.

**Regression test (land with the fix):** add `min2` and `b1` as a host/MVS test
(e.g. `test/host` + a `TST*` member registered in `tstall.jcl` + `project.toml`).
They go green the moment the fix lands; committing them now would be red CI.
