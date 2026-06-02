# WP-PERF-PROFILE — Bytecode-VM Hotspot Profile + OC Prioritisation

**Ticket:** TSK-264 (Notion 3733d993878781 3d96ebfa20177f5caf)
**Type:** Measurement only — **NO optimisation code** in this WP. Fix the
measurement infrastructure, pull a profile, prioritise the OC candidates.
Every optimisation is a follow-up WP.
**Branch:** `wp-perf-profile`
**HEAD commit:** `9f02d4b` — *feat(bytecode): WP-BC-RT02 — abuttal vs blank
concatenation (#177)*
**References:** CON-12 (Optimization Candidates, OC-06..OC-11); CON-12 REXXCPS
Performance Baseline.

---

## TL;DR

1. **Blocker fixed.** `scripts/host-profile.sh` did not list the bytecode
   modules in `ENGINE_SRC`; it ended at `irx#arith.c`. Added `irx#bcom.c`,
   `irx#bvm.c`, `irx#bctl.c`. The build links cleanly and now profiles the
   **bytecode** path.
2. **Bytecode path proven, not token-walk.** With `REXX370_BCDEBUG=1` the run
   reports **`[bc] exec=1 fallback=0`** — REXXCPS compiles fully to bytecode and
   runs once, zero token-walk fallbacks. The gprof flat profile is rooted at
   `IRXBEXEC` (the VM dispatch loop, 96 % cumulative). **No** `irx_pars_*` /
   token-walk interpreter symbol appears in the runtime; the only compiler-side
   symbols (`is_kw_barrier`, `add_sym`, `try_parse_int_cache`) are in
   `irx#bcom.c`/`irx#bvm.c`, run once (42–64 calls, 0.00 s).
3. **The hotspots have shifted from the 2026-05-11 token-walk hypotheses.** The
   three dominant cost centres in the bytecode VM are:
   - **A — Numeric value re-parsing + BCD arithmetic (~32 % cumulative).**
     Operands flow through the VM as character strings and are re-parsed to BCD
     on every compare/op. `num_from_str` = 12.1 M calls.
   - **B — Variable-pool / compound resolution (~22 % cumulative).** Composed-key
     hashing + bucket walk.
   - **C — BIF dispatch via linear linked-list scan (~10 % cumulative).**
     `irx_bif_find` walks the ~52-entry registry node-by-node; 57.3 M
     name-compares.
4. **Priority recommendation (Hercules-multiplier-weighted):**
   **OC-09 first** (BIF dispatch — cheapest, lowest-risk, memory-access-bound),
   **OC-06 second** (compound tail-cache — biggest clean memory-access cluster),
   then a **new candidate OC-12** (numeric comparison fast-path / slot
   value-form cache — biggest aggregate, but correctness-sensitive). OC-07 is
   already satisfied by WP-BC-06 for the literal subset; OC-10 is obsolete in
   the bytecode VM; OC-08 is modest and folds into OC-12; OC-11 (HLASM) is an
   eventual lever *after* the algorithmic wins.

---

## 1. The fix to `scripts/host-profile.sh`

Three modules added to `ENGINE_SRC` (the only change to the script):

```diff
             ${REPO_ROOT}/src/irx#ctrl.c \
             ${REPO_ROOT}/src/irx#exec.c \
-            ${REPO_ROOT}/src/irx#arith.c"
+            ${REPO_ROOT}/src/irx#arith.c \
+            ${REPO_ROOT}/src/irx#bcom.c \
+            ${REPO_ROOT}/src/irx#bvm.c  \
+            ${REPO_ROOT}/src/irx#bctl.c"
```

`irx#bcom.c` = bytecode compiler, `irx#bvm.c` = bytecode VM, `irx#bctl.c` =
bytecode control-flow. No further modules were needed — the build links with
**no** undefined-symbol errors (the escalation case "missing symbols beyond the
three modules" did not occur).

---

## 2. Bytecode-path verification (the gate)

The bytecode VM is default-on (`wkbi_use_bytecode = 1`, `src/irx#init.c:268`;
opt-out via `REXX370_BYTECODE=0`). The exec pipeline takes the bytecode path and
only falls back to token-walk on an `IRXBC_ERR_UNSUP` compile result
(`src/irx#exec.c:343-394`). The fallback counter is exposed via
`REXX370_BCDEBUG=1` (`src/irx#term.c:170-184`).

```
$ REXX370_BCDEBUG=1 ./scripts/host-profile.sh --source=test/rexxcps.rexx
...
[bc] exec=1 fallback=0          <-- gate: 1 bytecode execution, 0 fallbacks
```

`fallback=0` is the hard gate: any value > 0 would mean REXXCPS hit an
unsupported construct and the profile would be a token-walk/bytecode mixture.
It is 0 → the profile is **100 % bytecode**. Corroborated by the flat profile:
`IRXBEXEC` (the VM executor, asm alias of `irx_bc_execute`) is the root at
1.92 s / 96 % cumulative; the token-walk interpreter is entirely absent.

Raw proof artefacts committed alongside this doc:
- `docs/diag/wp-perf-profile-run.log` — contains the `[bc] exec=1 fallback=0` line
- `docs/diag/wp-perf-profile-gprof.txt` — full gprof flat profile + call graph
- `docs/diag/wp-perf-profile-top10.txt` — extracted top-10
- `docs/diag/wp-perf-profile-timing.txt` — driver wall/cpu timing

---

## 3. Workload & measurement caveats

| | |
|---|---|
| Workload | `test/rexxcps.rexx` (REXXCPS 2.2), self-calibrated to `count=100 × averaging=100 × 1000` clauses = **10 M clauses** |
| Host | WSL2 (Linux 6.6), gcc 13.3.0, `-O0 -pg -g` (canonical profiling build) |
| Driver | `test/host/tstcps_host.c`, entry `irx_exec_run()` |
| Wall / CPU | wall 6.99 s, `cpu_user` 6.985 s |
| Reported cps | 1,431,706 cps (host) |
| gprof-attributed self-time | **~2.0 s** |

**Read this only as hotspot localisation, not a performance statement**
(CON-12 guiding principle "the profile shows WHERE time goes, not HOW FAST").

- **Host cps ≠ MVS cps** (CON-12: factor ~112×). The 1.43 M host cps figure is an
  iteration datapoint on *this* machine; the target metric stays MVS cps against
  the 14,280 baseline. It is not comparable to the mvsdev.lan baseline either
  (different host).
- **The `cpu_user` 6.99 s vs gprof ~2.0 s gap is `-pg` instrumentation overhead.**
  With 57 M+ instrumented calls, `mcount` bookkeeping dominates the unattributed
  ~5 s. The **relative** percentages below are valid for ranking; the absolute
  seconds are not a wall-clock budget.
- **Hercules multiplier (CON-12, validated WP-PERF-03/04):** memory-access-reducing
  optimisations land ~3–5× over the host expectation on MVS. So for the
  memory-access-bound candidates below, the host % is a **lower bound** on the
  MVS payoff; for pure CPU savers it is roughly the ceiling.

---

## 4. Top-10 hotspots (flat profile, self-time)

From `docs/diag/wp-perf-profile-top10.txt`:

| # | %self | self s | calls | function | subsystem |
|---|------:|-------:|------:|----------|-----------|
| 1 | 17.00 | 0.34 | 1 | `IRXBEXEC` | VM dispatch loop (`irx#bvm.c`) — root, 1.92 s total |
| 2 | 8.25 | 0.17 | 57,262,112 | `ebcdic_eq` | **BIF registry name-compare** (`irx#bif.c:72`) |
| 3 | 7.50 | 0.15 | 12,102,041 | `num_from_str` | BCD parse from string (`irx#arith.c:192`) |
| 4 | 6.50 | 0.13 | 18,322,925 | `hash_bytes` | vpool key hash |
| 5 | 4.50 | 0.09 | 18,322,761 | `find_in_bucket` | vpool bucket walk |
| 6 | 4.25 | 0.09 | 30,393,868 | `get_entry` | vpool entry access |
| 7 | 4.00 | 0.08 | 13,352,246 | `num_free` | BCD scratch free |
| 8 | 4.00 | 0.08 | 270,000 | `num_mul` | BCD multiply |
| 9 | 3.75 | 0.07 | 12,522,138 | `VPOOLGTB` | vpool get (asm alias) |
| 10 | 2.50 | 0.05 | 31,706,132 | `irxstor` | storage manager (alloc/free) |

Notable runners-up: `IRXARICM` (arith compare, 4.94 M), `Lfx` (lstr field
access, 46.6 M), `IRXBIFFN` (BIF dispatch, 1.54 M), `num_format` (1.25 M),
`slot_set_buf` (20 M), `matches_exposed_stem` (18.2 M), `mag_compare`,
`bucket_index` (18.3 M), `num_alloc` (13 M), `num_strip_trailing` (12.9 M),
`lstr_to_num` (12.1 M), `rexx_lstr_alloc`/`rexx_lstr_dealloc` (4.6 M each),
`num_to_lstr`, `read_u16` (30.5 M), `Lstrcpy` (19.6 M).

---

## 5. Top call chains (cumulative, call graph)

Children of the VM root `IRXBEXEC` `[3]` (1.92 s / 96 % cumulative), sorted by
cumulative cost:

| node | %total | cum s | calls | dominant child |
|------|-------:|------:|------:|----------------|
| `IRXARICM` (arith **compare**) `[4]` | 18.5 | 0.37 | 4,940,815 | → `lstr_to_num` 0.19 children |
| `VPOOLGTB` (vpool **get**) `[5]` | 15.9 | 0.31 | 12,522,138 | → `find_in_bucket` 0.06, `matches_exposed_stem` 0.02 |
| `IRXARIOP` (arith **op** `+ - * /`) `[6]` | 14.0 | 0.29 | 1,250,205 | → `num_mul` 0.08, `lstr_to_num` 0.05 |
| `lstr_to_num` `[7]` | 13.2 | 0.27 | 12,102,041 | → `num_from_str` 0.15 self + 0.09 |
| `num_from_str` `[8]` | 11.9 | 0.24 | 12,102,041 | (leaf BCD parse) |
| `IRXBIFFN` (BIF dispatch) `[9]` | 10.2 | 0.21 | 1,540,403 | → `ebcdic_eq` 0.17 |
| `ebcdic_eq` `[10]` | 8.2 | 0.17 | 57,262,112 | 100 % from `IRXBIFFN`/`irx_bif_find` |
| `VPOOLSTB` (vpool **set**) `[13]` | 6.0 | 0.12 | 5,510,622 | → `find_in_bucket` 0.03 |

### Cost clusters (the three themes)

**A — Numeric re-parse + BCD arithmetic (~32 % cumulative).**
`IRXARICM` (18.5 %) and `IRXARIOP` (14.0 %) both funnel into
`lstr_to_num` → `num_from_str` (13.2 % / 11.9 %, **12.1 M calls**). REXXCPS does
`if j>acompound.key1.loop`, `if j<5`, `if 17<length(j)-1`, `acc=acc+i`,
`x=(i*3+7)/2`, `y=x**2`, `avar.1.2*1.1`, the `select … when` chains. Every one of
those re-parses its operands **from their string form** into a fresh BCD number
(`num_alloc` 13 M / `num_free` 13 M, feeding `irxstor` 31.7 M calls). Note: an
integer fast-path *exists* (`try_arith_fast`, 1.39 M calls, ~0 s) but the
**compare** path (`IRXARICM`, the single largest sub-tree) does **not** use it —
9.6 M of the 12.1 M `num_from_str` calls originate in `IRXARICM`.

**B — Variable pool / compound resolution (~22 % cumulative).**
`VPOOLGTB` (15.9 %) + `VPOOLSTB` (6.0 %). The leaves are `hash_bytes`
(6.5 %, 18.3 M), `find_in_bucket` (4.5 %, 18.3 M), `get_entry` (4.25 %, 30.4 M),
`matches_exposed_stem` (1.75 %, 18.2 M), `bucket_index` (1.5 %). REXXCPS hammers
two-level compounds (`acompound.key1.loop`, `avar.flag.2`) in the inner loop;
each access hashes the composed key and walks the bucket.

**C — BIF dispatch linear scan (~10 % cumulative).**
`IRXBIFFN` (10.2 %) is almost entirely `ebcdic_eq` (8.2 %).
`irx_bif_find` (`src/irx#bif.c:206-226`) is a **linear linked-list scan**
(`reg->head` → `node->next`) byte-comparing every BIF name. **57.3 M
node-visits / 1.54 M finds ≈ 37 nodes walked per BIF call** — the registry holds
~52 BIFs and the hot ones (`substr`, `pos`, `length`, `words`, `word`) sit deep
in the list. This is a pointer-chase across ~52 cache lines per call.

---

## 6. OC-06..OC-11 reconciliation against the profile

Every verdict cites a profile observation. "Confirmed" means the profile shows
the targeted code as a real hotspot **now, on the bytecode VM** (the OC
hypotheses were derived from the 2026-05-11 *token-walk* analysis).

### OC-06 — Compound-Variable Tail-Caching → **CONFIRMED (strong)**
*Module: `irx#vpol.c`, `irx#pars.c`. Character: memory-access-reducing.*
Cluster B is the #2 cost centre (~22 % cumulative). `hash_bytes` (6.5 %, 18.3 M),
`find_in_bucket` (4.5 %), `matches_exposed_stem` (1.75 %), `bucket_index` (1.5 %)
are exactly the composed-key-hash + bucket-walk that OC-06 proposes to cache.
Memory-access-reducing → Hercules multiplier applies → host 22 % is a lower
bound on MVS. The risk OC-06 itself names (invalidation on
`DROP`/`PROCEDURE EXPOSE`/frame boundary) is real and warrants the generation-
counter + TRACE-equivalence mitigation. **High-value.**

### OC-07 — Numeric-Type-Caching at the Token → **already satisfied (WP-BC-06), re-framed**
*Module: `irx#tokn.c`, `irx#pars.c`. Originally: cache parsed form of literals.*
The literal-caching OC-07 proposed is **already implemented** by WP-BC-06: the VM
pre-computes `const_type_cache[]` / `const_int_cache[]` per constant and reads
them on `OP_PUSH_LIT` (`src/irx#bvm.c:955`, `docs/bytecode-format.md:304-324`,
`docs/architecture.md:874`). The profile confirms literals are **not** the cost:
`num_from_str` is reached only via `lstr_to_num` ← `IRXARICM`/`IRXARIOP`, never
from the `OP_PUSH_LIT` handler — direct evidence the constant cache works at push
time. The 12.1 M `num_from_str` calls come from `IRXARICM`/`IRXARIOP` operating
on **variable / computed values** (`j`, `acc`, `acompound.key1.loop`), not source
literals. The precise verdict: **OC-07 is implemented but bypassed on the compare
path** — the comparison opcodes re-parse anyway (see OC-12). OC-07's mechanism is
done; the residual `num_from_str` hotspot is a different problem (value-form not
carried forward) → folded into **OC-12**. Not a standalone candidate any more.

### OC-08 — lstring370 Small-Buffer-Pool → **CONFIRMED but modest**
*Module: `irx#lstr.c`. Character: memory-access-reducing.*
`rexx_lstr_alloc` (1.25 %, 4.6 M) + `rexx_lstr_dealloc` (1.25 %, 4.6 M) ≈ 2.5 %.
Real, but smaller than the hypothesis implied. The **larger** allocation churn is
`num_alloc` (1.5 %, 13 M) + `num_free` (4.0 %, 13 M) ≈ 5.5 % — those are **BCD
scratch numbers**, not lstrings, and they are a *consequence* of cluster A's
re-parsing. A pool would help, but the churn is better killed at the source
(OC-12). **Lower priority; folds into OC-12.**

### OC-09 — BIF Direct-Dispatch → **CONFIRMED, underestimated, re-framed as memory-access**
*Module: `irx#pars.c` (originally). Originally framed CPU, 3-8 %.*
Cluster C: `IRXBIFFN` → `ebcdic_eq` linear linked-list scan, 57.3 M node-visits,
~37 per find, **8.2 %+ cumulative** — already above the OC-09 estimate on the
host. Critically, it is a **pointer-chase** (`node->next` 57 M times across ~52
cache lines), i.e. **memory-access-bound**, so the Hercules multiplier amplifies
it on MVS well beyond the host 8 %. The registry is **static after init**
(`irx#bif.c`), so the fix is near-zero-risk. The bytecode-context mechanism
shifts from OC-09's "cache fn-pointer in the token" (no token exists at VM
runtime) to either: **(a) resolve the BIF index at bytecode-compile time and bake
it into the `OP_CALL_BIF` operand** (preferred — turns the 57 M-visit scan into a
single array index), or **(b) replace the linked list with a sorted/perfect-hash
table**. **Highest-confidence, lowest-risk win.**

### OC-10 — DO-Loop Token-Position Caching → **OBSOLETE (eliminated by bytecode)**
*Module: `irx#ctrl.c`, `irx#pars.c`. Character: CPU.*
No `END`→`DO` backward-scan / loop-back function appears anywhere in the hot
path. The bytecode compiler resolves loop structure to jump offsets at compile
time; the runtime cost is just `read_u16`/`read_i16` operand reads (~1 %, 30.5 M /
5.2 M calls, trivial). The bytecode VM **already solved** what OC-10 proposed for
the token-walk interpreter. **Drop from the bytecode roadmap** (token-walk-only
concern).

### OC-11 — HLASM Hot-Path Module → **CONFIRMED as eventual lever; retarget; defer**
*Module: strategically chosen hot functions. Character: CPU.*
OC-11's *guessed* top-3 partially match the measured top-3:
- `vpool_lookup` → `VPOOLGTB`/`find_in_bucket`/`hash_bytes` ✓ (cluster B)
- `irx_arith_compare` → `IRXARICM` ✓✓ (largest single sub-tree)
- `lstr_alloc`/`free` → only ~2.5 %; the real churn is `num_alloc`/`num_free` ✗
The measured HLASM candidates are `num_from_str`/`IRXARICM`, `hash_bytes`/
`find_in_bucket`, and `ebcdic_eq`. **But** per CON-12 discipline, asm comes
*after* the algorithmic wins — rewriting `ebcdic_eq` (a linear scan) in HLASM
would optimise a bad algorithm. **Defer until OC-09 + OC-06 are taken**, then
re-profile and target whatever is still hot.

---

## 7. Dominant hotspot **not** covered by any OC → new candidate **OC-12**

This is the most valuable finding of the profile.

The single largest sub-tree is **`IRXARICM` — numeric comparison — at 18.5 %
cumulative (4.94 M calls)**, and its cost is dominated by re-parsing operands
through `lstr_to_num` → `num_from_str` (9.6 M of the 12.1 M total parses). No OC
covers this cleanly, and the source confirms the gap precisely:
- **The arithmetic op path already uses the cache** — `OP_ADD/SUB/MUL`
  (`src/irx#bvm.c:1408-1415`) call `try_arith_fast`, which short-circuits to C
  integer math when both slots carry `IRXBC_STACK_LINTEGER` `int_cache`
  (`src/irx#bvm.c:382-452`). `OP_DIV`/`OP_POW` are excluded (they must round).
- **The comparison opcodes bypass the cache entirely** — `OP_EQ/NE/LT/LE/GT/GE`
  (`src/irx#bvm.c:1454-1474`) call `irx_arith_compare` directly on the slot
  `.str` (string) fields, with **no `int_cache` check first**. Every comparison
  re-parses both operands to BCD. This is the 9.6 M-call majority of cluster A.
- **Computed values lose their cached form** — the BCD result paths clear
  `type_cache` (`src/irx#bvm.c:1428`, `:1450`), so a value produced by a BCD op
  is re-parsed by its next consumer even on the arithmetic path.
- **OC-01** (integer fast-path in IRXARITH) explicitly scoped itself to
  `+ - *` and **excluded comparisons**. **OC-07** (via WP-BC-06) caches only
  constant literals. Neither covers the compare-path re-parse.

**Proposed OC-12 — Numeric comparison fast-path / slot value-form caching.**
Carry the parsed numeric form (the WP-BC-06 `type_cache`/`int_cache` slot fields)
**forward across operations** for computed and variable values — not just for
constants — and/or add an integer fast-path inside the comparison opcode
(compare two integer-valued slots as C `long`s, no BCD round-trip). This would
eliminate the bulk of the 12.1 M `num_from_str` calls **and** the 13 M
`num_alloc`/`num_free` churn (and the downstream `irxstor` traffic, 31.7 M
calls) that OC-08 only nibbles at.

Character: **mixed** — the BCD digit-walk it removes is CPU, but the
`num_alloc`/`num_free`/`irxstor` churn it removes is memory-access (multiplier
applies). Highest **aggregate** payoff of any candidate.
Risk: **correctness-sensitive** — must stay observationally equivalent per
SC28-1883-0 §9.3 (signed zero, rounding boundaries, NUMERIC DIGITS changes mid-
exec need a generation counter). Same risk class OC-01 documents.

---

## 8. Prioritised recommendation

Ranked by (profile weight) × (memory-access character → Hercules multiplier) ×
(inverse implementation risk). The CON-12 directive — weight
memory-access-reducing candidates above pure CPU savers — is applied as the
tie-breaker, not as the only axis: a cheap, provably-safe, memory-access-bound
win outranks a large but correctness-sensitive one.

| Rank | Candidate | Profile weight (host) | Character | Risk | Why this order |
|------|-----------|----------------------|-----------|------|----------------|
| **1** | **OC-09** BIF dispatch | ~10 % (`ebcdic_eq` 8.2 %) | memory-access (pointer-chase) | **very low** (static registry) | Cheapest, highest-confidence; multiplier amplifies on MVS; compile-time index resolution is mechanical |
| **2** | **OC-06** Compound tail-cache | ~22 % (cluster B) | memory-access | medium (invalidation) | Biggest *clean* memory-access cluster; strong multiplier; needs generation-counter + TRACE-equivalence tests |
| **3** | **OC-12** (new) Numeric compare fast-path / slot value-form cache | ~32 % (cluster A) | mixed CPU+mem | higher (§9.3 equivalence) | Biggest aggregate, but correctness-sensitive — take after the two structural wins; subsumes OC-08 |
| — | OC-07 | n/a | — | — | Already satisfied for literals (WP-BC-06); remainder folded into OC-12 |
| — | OC-08 | ~2.5 % | memory-access | low | Modest; the real alloc churn (BCD scratch) is better killed by OC-12 |
| — | OC-10 | ~0 % | — | — | Obsolete — bytecode resolves loops to jump offsets at compile time |
| — | OC-11 | (top-3 = A/B/C) | CPU | high (dual maintenance) | Eventual lever; only *after* the algorithmic wins, then re-profile |

**Recommended first follow-up WP: OC-09.** It is the lowest-risk, highest-
confidence change (the BIF registry is static post-init, so resolving the index
at bytecode-compile time into the `OP_CALL_BIF` operand cannot change semantics),
it is memory-access-bound (so the Hercules multiplier works in our favour on the
real target), and it removes 57 M pointer-chasing node-visits for what should be
a single array index. Measure the result on MVS against the 14,280 cps baseline
(CON-12: MVS is the target metric, not host cps) before moving to OC-06.

---

## 9. Reproduction

```sh
git checkout wp-perf-profile        # HEAD 9f02d4b + the ENGINE_SRC fix
REXX370_BCDEBUG=1 ./scripts/host-profile.sh --source=test/rexxcps.rexx
#   -> build/host-profile/run.log       contains "[bc] exec=1 fallback=0"
#   -> build/host-profile/profile.txt   full gprof flat profile + call graph
#   -> build/host-profile/top-hotspots.txt
```

Prerequisites: `gcc`, `gprof` (binutils), and `../lstring370` cloned as a
sibling of `rexx370`.
