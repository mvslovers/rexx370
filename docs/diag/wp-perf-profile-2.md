# WP-PERF-PROFILE-2 — Fresh Bytecode-VM Hotspot Profile (post OC-12 / OC-09 / OC-ARITH)

**Type:** Measurement / diagnosis only — **NO optimisation code** in this WP.
Pull a fresh profile, compare against the now-stale baseline, prioritise the
next candidate. Every optimisation is a follow-up WP; the build decision is
Mike's + the architecture review.
**Branch:** `feature/wp-perf-profile-2`
**HEAD commit:** `210b90f` — *fix(bytecode): WP-BC-RT03 — de-double quotes in
string-literal expressions (#192)*
**Date:** 2026-06-03
**Supersedes (for hotspot localisation):** `docs/diag/wp-perf-profile.md`
(HEAD `9f02d4b`, #177, 2026-05-29) — that profile predates three optimisation
rounds and no longer reflects where time goes.
**References:** ROADMAP Axis 1 ("Next action: a fresh bytecode-VM profile");
CON-12 (Optimization Candidates).

---

## TL;DR

1. **The three large hotspots from the first profile are confirmed crushed —
   except cluster B, which was deliberately left alone and is now #1.**
   The profile is rooted at `IRXBEXEC` (the bytecode VM dispatch loop, 92.3 %
   cumulative) with `[bc] exec=1 fallback=0` — a pure-bytecode run, zero
   token-walk fallbacks, no `irx_pars_*` / `eval_*` / `kw_*` interpreter symbol
   anywhere in the runtime.

2. **What the three OCs did (machine-independent call counts, identical 10 M-clause
   workload both runs):**
   - **OC-09 (BIF dispatch) — decisive.** `ebcdic_eq` (the linear BIF
     name-compare) went **57,262,112 → 269 calls** (−99.9995 %). `irx_bif_find`
     no longer appears as a call-graph node at all; `bvm_resolve_bif` is called
     1.54 M times but reaches the registry only **9 times** (slot-cached). **Cluster
     C is gone.**
   - **OC-12 / OC-ARITH (numeric) — real but partial.** `num_from_str`
     12,102,041 → 9,220,415 (−24 %); `IRXARICM` 4,940,815 → 4,060,002 (−18 %);
     `num_alloc` ~13 M → 9,910,620 (−24 %). Cluster A shrank but did **not**
     vanish — the compare path still re-parses the operands the integer
     fast-path doesn't cover.
   - **Cluster B (vpool) — untouched, as designed (OC-06 rejected).** `VPOOLGTB`
     12,522,138 → 12,522,138 (identical), `hash_bytes` ~18.3 M → 18.9 M (flat).

3. **New top cost centres (flat self-time, % of the 2.87 s gprof self-total):**
   | rank | cost centre | flat self % | character |
   |---|---|---:|---|
   | 1 | **Cluster B — variable pool / compound resolution** | **~27 %** | memory-access |
   | 2 | **`IRXBEXEC` dispatch-loop self** (single function) | **20.6 %** | CPU (dispatch) |
   | 3 | **Cluster A — numeric re-parse + BCD arithmetic** | **~17.5 %** | mixed CPU+mem |
   | (4) | Value-plumbing — lstring + VM-slot string copies | ~14 % | memory-access |
   | — | Cluster C — BIF dispatch | **~0 %** (gone) | — |
   | — | `_init` (gprof artifact, discount) | 6.6 % | noise |

4. **Recommendation (caveated by the OC-06/OC-09 lesson — share ≠ lever):** the
   single best *candidate* is a **per-bytecode-operand variable-resolution inline
   cache** — but for the **~88 % simple-variable bulk**, **not** OC-06's
   compound tail (which was measured at 2.4–3.2 % and rejected). Memory-access-
   bound → Hercules multiplier applies. **It is an unmeasured ~16 % estimate;
   OC-06 proves such estimates can be ~9× optimistic, so it must be prototyped
   and measured on host *and* MVS cps before any commitment.** Complementary
   #2: VM value copy-elision (the ~14 % plumbing cluster). `IRXBEXEC` dispatch
   (HLASM / threaded = OC-11) is the eventual lever but deferred per CON-12.

---

## 1. Bytecode-path verification (the gate)

The #179 lesson was that `host-profile.sh` had originally profiled the
token-walk path. That fix is in place and verified again here:

- `scripts/host-profile.sh` `ENGINE_SRC` includes `irx#bcom.c`, `irx#bvm.c`,
  `irx#bctl.c` (verified at HEAD `210b90f`).
- `REXX370_BCDEBUG=1` run reports **`[bc] exec=1 fallback=0`**
  (`docs/diag/wp-perf-profile-2-run.log`) — REXXCPS compiles fully to bytecode
  and runs once, zero token-walk fallbacks. `fallback=0` is the hard gate; any
  value > 0 would mean a token-walk/bytecode mixture.
- The flat profile is rooted at `IRXBEXEC` (the VM executor, asm alias of
  `irx_bc_execute`) at **92.3 % cumulative**. No `irx_pars_*` / `eval_expr` /
  `eval_term` / `kw_*` token-walk symbol appears in the runtime. The only
  compiler-side symbols present (`is_kw_barrier` 42 calls, `add_sym`,
  `try_parse_int_cache` 64 calls) run **once at compile time, 0.00 s**.

→ The profile is **100 % bytecode**.

Raw proof artefacts committed alongside this doc:
- `wp-perf-profile-2-run.log` — contains the `[bc] exec=1 fallback=0` line
- `wp-perf-profile-2-gprof.txt` — full gprof flat profile + call graph (1906 lines)
- `wp-perf-profile-2-top10.txt` — extracted top-10
- `wp-perf-profile-2-timing.txt` — driver wall/cpu timing

---

## 2. Workload & measurement caveats

| | |
|---|---|
| Workload | `test/rexxcps.rexx` (REXXCPS 2.2), self-calibrated to **100 × 100 × 1000 = 10 M clauses** — **identical to the #177 baseline run** (confirmed in both `run.log`s) |
| Host | mvsdev.lan (Debian 13, Linux 6.12, gcc 14.2.0), `-O0 -pg -g` |
| Driver | `test/host/tstcps_host.c`, entry `irx_exec_run()` |
| Wall / CPU | wall 9.19 s, `cpu_user` 9.17 s |
| Reported cps | 1,088,532 cps (host) |
| gprof self-time total | **2.87 s** (the base for all flat % below) |

**Read this as hotspot localisation, not a performance statement** (CON-12: the
profile shows WHERE time goes, not HOW FAST).

- **Host cps is not comparable to #177's 1.43 M cps** — different machine
  (#177 = WSL/gcc-13; this = mvsdev.lan VM/gcc-14). Neither is comparable to MVS
  cps. The target metric stays **MVS cps** (System A baseline; see ROADMAP
  performance history). Host cps is only an iteration datapoint.
- **The `cpu_user` 9.17 s vs gprof 2.87 s gap is `-pg` instrumentation
  overhead** (`mcount` bookkeeping over hundreds of millions of instrumented
  calls). The **relative** percentages are valid for ranking; the absolute
  seconds are not a wall-clock budget.
- **Comparison discipline:** #177's headline cluster figures
  (A ~32 %, B ~22 %, C ~10 %) were **cumulative** (call-graph). The new cluster
  figures here are **flat self-time**. Those two metrics are not directly
  subtractable. **All before/after deltas in this doc are therefore anchored on
  call counts**, which are both metric- and host-independent and refer to the
  identical 10 M-clause workload. Where a like-for-like cumulative-to-cumulative
  comparison is possible it is labelled as such.
- **Hercules multiplier (CON-12, validated WP-PERF-03/04, OC-12, OC-ARITH):**
  memory-access-reducing optimisations land ~3–5× over the host expectation on
  MVS. For memory-bound candidates the host % is a **lower bound** on MVS payoff.

---

## 3. Flat profile — top 20 (self-time)

From `docs/diag/wp-perf-profile-2-gprof.txt` (self-total = 2.87 s):

| # | %self | self s | calls | function | subsystem |
|---|------:|-------:|------:|----------|-----------|
| 1 | 20.56 | 0.59 | 1 | `IRXBEXEC` | VM dispatch loop (`irx#bvm.c`) — root, 2.65 s total |
| 2 | 7.84 | 0.23 | 18,872,922 | `bucket_index` | vpool bucket selection (**B**) |
| 3 | 6.62 | 0.19 | — | `_init` | **gprof artifact — discount** |
| 4 | 5.23 | 0.15 | 12,522,138 | `VPOOLGTB` | vpool get (asm alias) (**B**) |
| 5 | 4.88 | 0.14 | 18,872,922 | `hash_bytes` | vpool key hash (**B**) |
| 6 | 4.18 | 0.12 | 9,220,415 | `num_from_str` | BCD parse from string (**A**) |
| 7 | 3.31 | 0.10 | 18,592,760 | `find_in_bucket` | vpool bucket walk (**B**) |
| 8 | 3.14 | 0.09 | 19,852,924 | `Lstrcpy` | lstring copy (**plumbing**) |
| 9 | 2.79 | 0.08 | 46,856,633 | `Lfx` | lstr field access (**plumbing**) |
| 10 | 2.79 | 0.08 | 20,573,074 | `slot_set_buf` | VM slot string set (**plumbing**) |
| 11 | 2.44 | 0.07 | 4,060,002 | `IRXARICM` | arith **compare** (**A**) |
| 12 | 2.09 | 0.06 | 23,142,876 | `irxstor` | storage manager (shared A+plumbing) |
| 13 | 2.09 | 0.06 | 5,460,501 | `rexx_lstr_alloc` | lstr alloc (**plumbing**) |
| 14 | 2.09 | 0.06 | 3,220,004 | `pframe_assign` | PROCEDURE-frame assign (**B**-adjacent) |
| 15 | 1.74 | 0.05 | 28,853,474 | `get_entry` | vpool entry access (**B**) |
| 16 | 1.57 | 0.05 | 9,910,620 | `num_alloc` | BCD scratch alloc (**A**) |
| 17 | 1.57 | 0.05 | 7,511,828 | `slot_set_bool` | VM slot bool set |
| 18 | 1.39 | 0.04 | 30,533,805 | `read_u16` | bytecode operand read |
| 19 | 1.39 | 0.04 | 18,312,760 | `matches_exposed_stem` | PROCEDURE EXPOSE check (**B**) |
| 20 | 1.39 | 0.04 | 8,539,986 | `name_matches_stem` | PROCEDURE EXPOSE check (**B**) |

Notable runners-up: `round_capacity` (1.39 %, 5.18 M), `mag_compare` (1.39 %,
3.36 M), `add_magnitudes` (1.39 %, 420 K), `num_mul` (1.39 %, 270 K), `VPOOLSTB`
(1.05 %, 5.51 M), `IRXARIOP` (1.05 %, 690 K), `num_free` (1.05 %, 9.91 M),
`slot_to_bool` (0.87 %), `num_strip_trailing`/`num_strip_leading` (0.70 % each,
~8.5–9.2 M), `read_i16` (0.70 %, 5.24 M), `get_numeric` (0.70 %, 4.75 M),
`parse_int32_fast` (0.70 %, 1.11 M), `num_format` (0.70 %, 690 K), `resolve_ref`
(0.52 %, 17.75 M), `try_arith_fast` (0.52 %, 1.39 M), `IRXBIFPO` (0.52 %),
`rexx_lstr_dealloc` (0.35 %, 5.46 M).

**`ebcdic_eq`: 0.00 %, 269 calls** (was 8.25 % / 57.3 M). **`irx_bif_find`: not
present** (was a top sub-tree). Cluster C is gone — see §5.

---

## 4. Call-graph structure (cumulative)

Children of the VM root `IRXBEXEC` `[3]` (2.65 s / 92.3 % cumulative), the
largest sub-trees:

| node | %total cum | calls | dominant children |
|------|-----------:|------:|-------------------|
| `VPOOLGTB` (vpool **get**) `[4]` | **21.2** | 12,522,138 | → `bucket_index` 0.25 (hash), `Lstrcpy` 0.11 (copy-out), `find_in_bucket` 0.07, `matches_exposed_stem` 0.03 |
| `IRXARICM` (arith **compare**) `[6]` | **12.6** | 4,060,002 | → `lstr_to_num` 0.19 (7.84 M of 9.22 M parses), `num_free` 0.04, `mag_compare` 0.04 |
| `num_from_str` `[9]` | 8.0 | 9,220,415 | → `num_alloc` 0.06, `num_strip_leading/trailing` 0.04 |
| `IRXARIOP` (arith **op** `+−*/`) `[11]` | ~6.6 | 690,205 | → `num_mul`, `lstr_to_num` |
| `slot_set_buf` `[15]` | 5.5 | 20,573,074 | → `Lfx` 0.08 |
| `pframe_assign` `[10]` | ~7 | 3,220,004 | PROCEDURE-frame variable assignment |
| `bvm_resolve_bif` `[104]` | **0.0** | 1,540,403 | → registry reached **9** times only (slot-cached) |
| `ebcdic_eq` `[144]` | **0.0** | **269** | (was the #2 leaf at 57.3 M) |

Key structural reads:
- **`VPOOLGTB` is the single largest sub-tree** (21.2 % cumulative). Its cost
  decomposes as: hash the composed key (`bucket_index`→`hash_bytes`, ~0.25),
  walk the bucket (`find_in_bucket`, ~0.07), check exposed-stem
  (`matches_exposed_stem`, ~0.03), **then copy the value out** (`Lstrcpy`,
  ~0.11). The copy-out links cluster B to the plumbing cluster — see §6.
- **The compare path still re-parses.** `IRXARICM` drives 7.84 M of the 9.22 M
  `lstr_to_num`/`num_from_str` calls — the integer fast-path catches some
  compares, but the majority of REXXCPS compares still BCD-parse both operands.

---

## 5. What the three OCs did — reconciliation against the new profile

All deltas are **call counts** over the identical 10 M-clause workload.

### OC-09 — BIF direct dispatch → **DECISIVE, cluster C eliminated**
`ebcdic_eq` **57,262,112 → 269** calls. `irx_bif_find` no longer appears as a
call-graph node. `bvm_resolve_bif` (1.54 M calls) reaches the registry only
**9 times** — the slot-cache resolves each BIF site once and reuses the pointer.
The ~37-node linear pointer-chase per call that was cluster C (~10 % cumulative
in #177) is gone. This is the clearest win of the three.

### OC-12 / OC-ARITH — numeric fast-paths → **real but partial; cluster A roughly halved, not gone**
- `num_from_str` 12,102,041 → 9,220,415 (**−24 %**).
- `IRXARICM` 4,940,815 → 4,060,002 (**−18 %**); `num_alloc` ~13 M → 9.91 M (−24 %).
- Like-for-like cumulative (the only fair cumulative-to-cumulative comparison):
  cluster A was ~**32 %** in #177; now `IRXARICM` 12.6 % + `IRXARIOP` ~6.6 % ≈
  **~19 %** cumulative. Down, but not as far as the flat-self ~17.5 % might
  suggest in isolation.
- **Why it's only partial:** `try_arith_fast` (1.39 M) and `parse_int32_fast`
  (1.11 M) fire, but `IRXARICM` still funnels 7.84 M parses. The fast-path
  catches integer-vs-integer cases; REXXCPS compares whose operands are *not*
  integer-cached (compound values, BCD results that cleared their cache) still
  re-parse. The residual is the unfinished part of OC-12 — carrying the parsed
  form *forward* across ops and compounds, not just for constants.

### OC-06 — compound tail-cache → **correctly NOT taken; cluster B untouched and now #1**
`VPOOLGTB` 12,522,138 → 12,522,138 (identical), `hash_bytes` ~18.3 M → 18.9 M
(flat). Cluster B was deliberately left alone (OC-06 was measured at 2.4–3.2 %
and rejected — see §6). With A and C cut, **B is now the largest cluster (~27 %
flat self).** This is the expected outcome, not a regression.

---

## 6. The next candidate — lever analysis (share ≠ lever)

The OC-06/OC-09 lesson governs this section: **a large profile share is
necessary but not sufficient for a worthwhile optimisation.** OC-06 looked like
22 % and delivered 2.4 %; OC-09 looked like 10 % and delivered a 17 % cps jump.
Each candidate below is judged on whether it is a *real lever* (hot-loop,
memory-bound → Hercules multiplier) or just profile mass.

### Candidate 1 (recommended) — per-operand variable-resolution inline cache → **REAL LEVER, but unmeasured**
*Cluster B, ~27 % flat self. Memory-access-bound.*

- **Not OC-06.** OC-06 (compound tail-cache) targets only the ~11.5 % of vpool
  traffic that is compound; ~88 % of cluster B is **simple variables** (`j`,
  `acc`, `i`, `x`, `y` in the REXXCPS inner loop). Caching compound tails leaves
  the bulk untouched — which is exactly why OC-06 measured 2.4–3.2 % and was
  rejected. **Do not re-open OC-06 on the strength of "B is biggest".**
- **The lever:** generalise OC-06's mechanism from compound-only to **all**
  variable references — cache the resolved vpool entry pointer at each bytecode
  reference *operand site*, guarded by a pool generation counter. A hot-loop
  access that hits the cache skips `hash_bytes` + `bucket_index` +
  `find_in_bucket` + the exposed-stem checks (`matches_exposed_stem` +
  `name_matches_stem`).
- **Removable estimate:** the hashable/walkable portion is
  `bucket_index` 7.84 + `hash_bytes` 4.88 + `find_in_bucket` 3.31 +
  `matches_exposed_stem` 1.39 + `name_matches_stem` 1.39 ≈ **~18.8 % flat self**,
  × ~0.88 (simple-var hit fraction) ≈ **~16 % removable on a hit**. The 0.88
  scaling is itself approximate and slightly optimistic — compound accesses hash
  longer composed keys and walk more, so they cost *more* per access than simple
  ones; the simple-var slice of that ~18.8 % is therefore somewhat below 88 %,
  making ~16 % a mild over-estimate. Memory-access-bound → the Hercules
  multiplier makes the MVS payoff likely several× the host figure. It does
  **not** remove the value copy-out (`Lstrcpy`) — see candidate 2.
- **The catch (the lesson, applied):** this ~16 % is an **unmeasured estimate**.
  OC-06's 22 %→2.4 % is the precedent for how wrong a share-based estimate can
  be. The **exposed-stem checks are both what the cache removes and why
  invalidation is hard** — `PROCEDURE EXPOSE` re-binds names across frames, and
  `DROP` / frame entry-exit must invalidate the cache or it returns stale
  pointers (a correctness bug, not just a perf miss). **Must be prototyped and
  measured on host *and* MVS cps, with TRACE-equivalence tests, before any
  commitment.** Best *candidate*, not a certainty.

### Candidate 2 (complementary) — VM value copy-elision → **REAL LEVER, medium risk**
*Plumbing cluster, ~14 % flat self. Memory-access-bound.*

- `slot_set_buf` (20.57 M), `Lstrcpy` (19.85 M — of which `VPOOLGTB` drives
  12.5 M), `Lfx` (46.86 M), `rexx_lstr_alloc` (5.46 M). Every value pushed onto
  the VM stack or fetched from the pool is **copied** as a fresh string buffer.
- **Coupling with candidate 1:** the inline cache removes the hash/walk but
  **not** the copy-out; copy-elision (copy-on-write lstrings, or pointer-view
  slots that borrow the vpool entry's buffer) removes the copy. **Fully draining
  `VPOOLGTB` needs both.** Both are memory-bound → both get the multiplier.
- **Risk:** aliasing/lifetime correctness — a borrowed view must be invalidated
  or copied when its source mutates. Medium risk, high value.

### Candidate 3 (defer) — `IRXBEXEC` dispatch loop → **real cost, but not the next move**
*20.6 % flat self — the biggest single line. CPU-bound (dispatch).*

- **Not a host `-O0` artifact.** `project.toml` `cflags` carries no `-O`, so the
  MVS product also builds at GCC default `-O0` (and c2asm370's `-O1` is buggy —
  root-CLAUDE roadmap #3). The per-opcode fetch-decode-switch + inline slot
  management overhead is **real on-target**.
- **But:** it is CPU/branch-bound, not memory-bound → the Hercules multiplier is
  weak here. The lever is threaded/computed-goto dispatch or an HLASM dispatch
  loop (**OC-11**). Per CON-12 discipline, asm comes *after* the algorithmic
  wins (rewriting an unoptimised loop in asm optimises the wrong thing first).
  **Defer** — re-profile after candidates 1–2, then target whatever is still hot.

### Candidate 4 (lower priority) — cluster A residual → **diminishing returns**
*~17.5 % flat self.* Already attacked twice. The residual `num_from_str`
(9.22 M, 7.84 M of it via `IRXARICM`) is the compare-path operands the integer
fast-path doesn't cover. Extending the slot value-form cache *forward* across
ops and compounds (the unfinished part of OC-12) would cut more, but it is
correctness-sensitive (§9.3 equivalence) and the easy wins are taken. Below
candidates 1–2.

### `_init` (6.6 %) — **not a hotspot.** gprof attributes samples landing in the
ELF `_init` section / unattributable regions here; it is a profiling artifact
and is discounted from all cluster sums above.

---

## 7. Recommendation

**Profile against MVS cps, decide with the architecture review — this doc only
measures and ranks.**

1. **First candidate: per-operand variable-resolution inline cache** for the
   simple-variable bulk of cluster B (the now-#1 cluster, memory-bound, ~16 %
   host estimate, strong multiplier). **Explicitly not OC-06** (compound-only,
   already rejected at 2.4 %). **Prototype + measure on host and MVS first** —
   the ~16 % is unmeasured and OC-06 is the cautionary precedent; the
   `PROCEDURE EXPOSE` / `DROP` / frame invalidation is the real engineering risk.
2. **Complementary: VM value copy-elision** (the ~14 % plumbing cluster) — pairs
   with #1 to fully drain `VPOOLGTB`; both memory-bound.
3. **Defer `IRXBEXEC` dispatch (OC-11 / HLASM)** until the algorithmic wins are
   taken, per CON-12.

No optimisation is implemented in this WP.

---

## 8. Reproduction

```sh
ssh mvsdev.lan
cd repos/rexx370
git fetch origin && git checkout main && git reset --hard origin/main   # HEAD 210b90f
REXX370_BCDEBUG=1 ./scripts/host-profile.sh --source=test/rexxcps.rexx
#   -> build/host-profile/run.log       contains "[bc] exec=1 fallback=0"
#   -> build/host-profile/profile.txt   full gprof flat profile + call graph
#   -> build/host-profile/top-hotspots.txt
```

Prerequisites: `gcc`, `gprof` (binutils), `../lstring370` cloned as a sibling of
`rexx370`. The `ENGINE_SRC` list in `scripts/host-profile.sh` must include
`irx#bcom.c` / `irx#bvm.c` / `irx#bctl.c` (it does at HEAD `210b90f`) — that is
what makes this the bytecode path and not the token-walk.
