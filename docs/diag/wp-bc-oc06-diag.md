# WP-BC-OC06-DIAG — Compound-Cluster Breakdown + Cache-Strategy Diagnosis

**Ticket:** WP-BC-OC06-DIAG (Notion `3733d99387878115a8cfe4ac22229bd5`)
**Type:** **Diagnosis only — NO fix.** Breaks down cluster B and recommends a
cache strategy. Builds no cache. The fix is a separate ticket.
**Branch:** `wp-bc-oc06-diag`
**HEAD commit:** `acfeaa0` — *feat(bytecode): WP-BC-OC09 — BIF direct
dispatch (#181)* (the WP-PERF-PROFILE baseline + OC-09 merged)
**References:** CON-12 (OC-06 hypothesis, cluster B ~22 %, Hercules
multiplier); `docs/diag/wp-perf-profile.md` (PR #179, cluster B finding);
lesson from OC-09/#181 (profile share ≠ hot-loop effect).

---

## Verdict

**OC-06 is not worth building now.** The cache *works* (78 % hit rate), but the
profile that motivated it is mislabelled: the committed "22 % cluster B" is
~88 % **simple-variable** access, and the compound tails OC-06 targets are only
**~11.5 % of all vpool traffic**. On the **same gprof basis that reported the
"22 %"**, the **entire compound-access cost (build + lookup) is only ≈ 3.8 % of
VM self-time** (key-build a mere 1.05 %, §2.2b) — and a cache reclaims only ~78 %
of that. The directly-measured host lever is **~2.4–3.2 % of host CPU** — single
digit either way. Higher-value targets: **OC-12 / cluster A (~32 %)** and the
**simple-variable vpool path** (the real bulk of cluster B). The cache design
below answers FRAGE 2/3 fully and is ready *if* OC-06 is ever taken, but the
recommendation is to **deprioritise it**. (This is the OC-09 lesson a second
time: *profile share ≠ hot-loop effect*.)

A separate, unrelated **correctness bug** surfaced during the work (stem-reset,
§5) and should get its own `bug` ticket regardless of OC-06.

## TL;DR

1. **The committed "22 % cluster B" is the *lookup* half only.** Key-build runs
   *inline* inside the `OP_LOAD_STEM`/`OP_STORE_STEM` switch case, so gprof folds
   it into `IRXBEXEC`'s 17 % self-time. `toupper`/`memcpy`/`memmove` appear
   **nowhere** in the committed flat profile (`wp-perf-profile-gprof.txt`) —
   glibc inlines `toupper`. So the profile never costed key-build separately;
   this WP measures it directly.

2. **FRAGE 1 — lookup dominates ~4 : 1.** Per compound access the cost splits
   **~80 % lookup / ~20 % key-build** (differential wall-clock ablation:
   `T_lookup ≈ 0.370 s`, `T_keybuild ≈ 0.093 s`; byte-op counts agree: 72.3 M vs
   28.9 M). **The escalation gate "time is in key-build, not lookup" is NOT
   triggered** — CON-12's premise (target the lookup) holds. The lookup's
   biggest sub-cost is the **DJB2 hash** (29.7 M bytes) plus **6 function calls
   per access** at -O0; the bucket walk itself is cheap (**1.04 nodes/lookup** —
   the table is well-distributed).

3. **FRAGE 2 — the ticket's "full-key hit rate is low" hypothesis is REFUTED.**
   On the real REXXCPS access trace a **2-entry** full-key cache hits **77.8 %**
   (size-1 already 71.1 %), measured *with correct REXX invalidation*. It
   plateaus at size 2 → a tiny cache suffices (ideal for MVS). Stem-anchor has
   100 % stem-locality but small leverage; tail-resolution repeats 88.7 % but
   targets a different cost.

4. **Recommendation:** a small (2-entry) compound-variable cache keyed on
   `(stem_idx + tail-slot values)`, probed *before* key-build. **High hit rate,
   modest absolute lever** — compound accesses are only **~11.5 % of all vpool
   traffic** (cluster B is **~88 % *simple*-variable** access, a correction to
   the committed profile's "compound resolution" label, §2.4). Ablation-measured
   host lever ≈ **2.4–3.2 % of host CPU**, amplified on MVS for the memory-bound
   part. This is the OC-09 lesson again — *profile share ≠ hot-loop effect*.
   **→ deprioritise OC-06**; OC-12 / cluster A (~32 %) and the simple-var path
   are the higher-value targets. The design below is documented for
   completeness, not as a call to build it now.

5. **Side-finding (needs its own ticket): a real stem-reset correctness bug.**
   `a.1=5; a.=0; say a.1` prints **5** but REXX requires **0** (stem assignment
   must drop all elements). It couples with the cache's invalidation rule #1.

---

## 1. Methodology

All instrumentation lived in a **local diagnostic copy** of the tree
(`/tmp/oc06diag` on the macOS dev box, and an uncommitted local edit on
`mvsdev`); **nothing instrumented was committed**. The repo working tree stayed
pristine on both. Primary host: Apple M2 Pro (arm64, macOS) — deterministic
op-counters + a correctness-preserving wall-clock ablation + an offline trace
analyzer. The macOS box has no gprof, so the **gprof-native cross-check (§2.2b)
was run on `mvsdev`** (Linux x86_64, gcc — the same setup that produced the
committed profile), with the key-build extracted into a `noinline` symbol so
gprof could cost it directly. Three independent methods (byte-ops, ablation,
gprof) agree on the FRAGE-1 split.

Workload: `test/rexxcps.rexx` (REXXCPS 2.2), the same self-calibrated
`100 × 100 × 1000` = 10 M-clause run as WP-PERF-PROFILE, `exec=1 fallback=0`
(100 % bytecode, no token-walk). Baseline host throughput ≈ 0.94–1.04 M cps
(no `-pg`).

Three instruments (full source in Appendix A):

- **Op-counters** (deterministic, exact, reproducible): key-build byte-work in
  the `OP_LOAD_STEM`/`OP_STORE_STEM` handlers; lookup byte-work inside
  `vpool_get_buf`/`vpool_set_buf`, gated on "name contains a dot" so only the
  **compound** path is counted (the `*_buf` functions serve simple vars too).
- **Differential wall-clock ablation** (`-DDIAG_DOUBLE_BUILD` /
  `-DDIAG_DOUBLE_LOOKUP`): redo *one* region's identical work into a throwaway,
  correctness-preserving. The wall delta vs baseline isolates that region's true
  CPU cost without per-call clock overhead and independent of arch.
- **Access trace + offline analyzer** (`analyze.py`, Appendix B): every compound
  vpool op is logged (`<op>\t<key>`, capped at 300 000 records — the pattern is
  periodic, so this is fully representative; L/S ratio matches the full run to
  0.4 %). The analyzer replays the trace through the three cache strategies with
  **correct REXX invalidation** (a stem-default store / DROP evicts that stem's
  cached elements).

---

## 2. FRAGE 1 — where the time goes in cluster B

### 2.1 Raw counters (full REXXCPS run)

2 080 000 compound accesses (1 250 000 LOAD + 830 000 STORE).

| Key-build (inline, **0 function calls**) | total | per access |
|---|---:|---:|
| stem `memcpy` bytes | 16 000 000 | 7.69 |
| tail `toupper` iterations | 11 000 000 | 5.29 |
| `.` separators written | 1 940 000 | 0.93 |
| **key-build byte-ops** | **28 940 000** | **13.9** |

| Lookup (`vpool_*_buf`, compound path) | total | per access |
|---|---:|---:|
| `hash_bytes` calls / bytes | 2 230 017 / 29 690 289 | 14.3 B |
| bucket node-visits (`find_in_bucket`) | 2 169 991 | **1.04** |
| `memcmp` bytes (len-matching nodes) | 28 489 713 | 13.7 |
| `matches_exposed_stem` iterations | **0** | (no EXPOSE) |
| value `Lstrcpy` bytes | 14 147 631 | 6.8 |
| stem-default fallbacks taken | 150 001 | 12 % of loads |
| **lookup byte-ops** | **72 327 633** | **34.8** + 6 calls |

Key-build : lookup = **28.9 M : 72.3 M ≈ 29 % : 71 %** in byte-work. The lookup
also pays ~6 function calls per access (`bucket_index`, `hash_bytes`,
`find_in_bucket`, `matches_exposed_stem`, `resolve_ref`, `Lstrcpy`) that
inline key-build does not — which skews the *time* split further toward lookup.

### 2.2 Wall-clock ablation (the time split)

`cpu_user`, mean of 3 runs each, identical 100 × 100 iterations:

| build | cpu_user (s) | Δ vs baseline | isolates |
|---|---:|---:|---|
| baseline (instrumented) | 9.431 | — | — |
| `+DIAG_DOUBLE_BUILD` | 9.524 | **+0.093** | one key-build pass |
| `+DIAG_DOUBLE_LOOKUP` | 9.801 | **+0.370** | one lookup pass |

**`T_lookup / T_keybuild ≈ 0.370 / 0.093 ≈ 4.0`.** Of the combined
compound-access cost (0.463 s), **lookup ≈ 80 %, key-build ≈ 20 %.** The byte-op
split (71/29) and the time split (80/20) agree in direction; time is more skewed
because of the lookup's per-access call overhead.

### 2.2b gprof-native cross-check (Linux/gcc — same setup as the committed profile)

Re-run on `mvsdev` (Linux x86_64, gcc, `-pg`, `[bc] exec=1 fallback=0`) with the
key-build extracted into a `noinline irx_diag_build_key()` (local, uncommitted)
so gprof attributes it as its own symbol instead of folding it into `IRXBEXEC`
self-time. This puts the key-build into the **same units as the committed
"22 %"** for the first time:

| symbol | self-time | calls | note |
|---|---:|---:|---|
| `IRXBEXEC` | 19.2 % | 1 | VM dispatch root |
| `VPOOLGTB` (cumulative) | 15.9 % | 12 522 138 | **all** gets |
| `VPOOLSTB` (cumulative) | 7.6 % | 5 510 622 | **all** sets |
| **cluster B (vpool total)** | **23.5 %** | — | matches committed ~22 % |
| **`irx_diag_build_key`** | **1.05 %** | **2 080 000** | whole compound key-build (leaf) |

So the **entire compound key-build is 1.05 % of VM self-time**, and the
**compound lookup is ≈ 2.7 % pro-rata** (11.5 % of the 23.5 % cluster B; a little
higher in practice — compound keys are longer and 12 % take a stem-fallback).
gprof-native ratio **lookup : build ≈ 2.6 : 1** — consistent with the byte-op
split (2.5 : 1) and the ablation (4 : 1; more call overhead shows up on
macOS/clang). **Headline: the entire compound-access cost — build *and* lookup —
is only ≈ 3.8 % of VM self-time** on the exact profile basis that reported the
"22 %". The other ~88 % of cluster B is simple-variable access (§2.4).

### 2.3 Verdict for FRAGE 1

- **Lookup dominates (~80 %).** Within it, the **hash** is the single largest
  sub-cost and the **per-access function-call overhead** (6 calls) is the
  memory-access character CON-12 expects the Hercules multiplier to amplify.
  The **bucket walk is already cheap** (1.04 nodes) — the table is well-sized,
  so OC-06 should attack the *hash + call overhead*, not the chain length.
- **Key-build is the minority (~20 %)** but **non-trivial**; the committed gprof
  could not see it (folded into `IRXBEXEC` self-time) — §2.2b now measures it at
  **1.05 % of VM self-time**. A cache that probes *before* building the key
  captures this slice too.
- **Escalation gate NOT triggered.** The time is in the lookup, as CON-12
  assumed — no re-interpretation needed.

### 2.4 Cluster B is mostly *simple*-variable access (profile-label correction)

This bounds the OC-06 lever and corrects the committed profile. Cross-checking
the compound counters against the committed gprof call counts:

| | compound (this WP) | all vpool (committed) | compound share |
|---|---:|---:|---:|
| `get` | 1 250 000 | `VPOOLGTB` 12 522 138 | **10.0 %** |
| `set` | 830 000 | `VPOOLSTB` 5 510 622 | **15.1 %** |
| total | 2 080 000 | 18 032 760 | **11.5 %** |

**Cluster B's ~22 % is ~88 % *simple*-variable access; only ~11.5 % is the
compound tails OC-06 targets.** The committed profile labelled cluster B
"compound resolution" — that is misleading: REXXCPS hammers simple variables
(`j`, `flag`, `acc`, `rc`, `p0…p8`, …) far more than compound tails. A compound
cache can only ever touch that ~11.5 % slice. **Observation (not a scope
change):** if vpool time is ~88 % simple-var, the larger vpool win likely lies
on the *simple-var* path, not compound tails — worth a separate look when
prioritising.

---

## 3. FRAGE 2 — which cache strategy actually hits

Replaying the real trace (300 000 records, 279 808 element accesses; only **17
distinct compound keys** exist in the whole workload: `ACOMPOUND.KEY BEE.1..14`
plus `AVAR.0.2`, `AVAR.1.2`, `AVAR.1.3`).

The hot pattern (verified op-by-op against `rexxcps.rexx:108-148`): each `loop`
iteration touches **the same** `ACOMPOUND.KEY BEE.<loop>` key **8× back-to-back**
(store, load, then load+load+store per inner `j` iter ×2), interleaved only with
simple-var/BIF ops that don't touch a compound cache; then a stem-default
`avar.=…` reset and a handful of `AVAR.*` ops.

### 3.1 Strategy 1 — full-key cache (composed key → entry)

| LRU size | **hit, correct invalidation** | hit, no invalidation* |
|---:|---:|---:|
| 1 | **71.1 %** | 71.1 % |
| 2 | **77.8 %** | 77.8 % |
| 4 | **77.8 %** | 91.2 % |
| 8 | **77.8 %** | 92.3 % |
| 16 | **77.8 %** | 92.3 % |

\* "no invalidation" = a cache that ignores stem-default resets; it happens to
match the **current (buggy)** vpool (§5) — **do not** target this number.

- **The ticket's hypothesis ("low because loop/flag vary") is wrong.** A
  *size-1* last-compound memo already hits **71 %**; the 8×-same-key acompound
  cluster alone yields 7/8 locally.
- **Plateau at size 2 (77.8 %).** Bigger caches do not help under correct
  invalidation, because the per-iteration `avar.=` reset is a *structural* miss
  source, not a capacity one. **→ a 2-entry cache is optimal** — cheap, and
  perfect for the MVS memory budget.
- 77.8 % is the honest, deployable number.

### 3.2 Strategy 2 — stem-anchor cache (cache the stem's hash/bucket)

Stem temporal locality: size-1 = 85.6 %, **size-2 = 100 %** (only two stems ever
occur). **But the leverage is small:** anchoring the stem hash only saves
re-hashing the constant stem prefix — `stem_bytes / keylen = 16.0 M / 28.9 M ≈
55 %` of the hash bytes, i.e. ~16 % of total compound byte-work — and saves
**nothing** on the bucket walk (still a full-key `memcmp`), the value copy, or
the key-build. **Upside: zero invalidation risk** — `hash(stem)` is constant per
`stem_idx` and can be precomputed at *compile* time, then DJB2-rolled over the
tail. A safe, modest incremental win; strictly dominated on leverage by
Strategy 1 but combinable with it.

### 3.3 Strategy 3 — tail-resolution cache (cache resolved tail variables)

| site | tail position | repeat rate |
|---|---|---:|
| `ACOMPOUND.` | 0 (`key1`) | 100.0 % |
| `ACOMPOUND.` | 1 (`loop`) | 87.5 % (7/8) |
| `AVAR.` | 0 (`flag`) | 97.6 % |
| `AVAR.` | 1 (const) | 65.9 % |
| **all positions** | | **88.7 %** |

Tail values are highly repetitive. **But this targets a different cost** — the
simple-var `OP_LOAD_VAR` lookups that *resolve* the tails before
`OP_LOAD_STEM` — not the cluster-B key-build+lookup. It is an indirect,
orthogonal optimisation; not the OC-06 lever.

---

## 4. Cache design (answers FRAGE 2/3 — *if* OC-06 is ever taken)

> Recommendation is to **deprioritise OC-06** (see Verdict). This section
> documents the strategy that *would* work, so the analysis is complete and the
> decision is reversible — not as a call to build it.

**The shape that works: a small (2-entry) compound-variable cache, keyed on
`(stem_idx + tail-slot values)`, probed *before* key-build, returning the
resolved vpool entry pointer.**

### Why this shape (FRAGE 1 ↔ FRAGE 2 are coupled)

A cache keyed on the *composed string* must still **build the key to probe it**
→ it saves only the lookup half (≈80 %). Keying on `(stem_idx + raw tail-slot
values)` lets the probe compare a few short tail strings (≈5.3 B `memcmp`,
cheaper than the 13.9 B build+hash) **before** building → it also skips the
key-build half on a hit. Because key-build is 20 % of the cost and the
probe-compare is cheaper than the build, probe-before-build is the better shape.
The simpler composed-key variant (probe *after* build, saves lookup only) is a
valid first step if the probe-before-build plumbing is awkward.

### Expected lever — high hit rate, modest absolute payoff

Lead with the **directly measured** ablation, not gprof arithmetic (the
gprof cluster-B percentages cover *all* vpool traffic, ~88 % of which is
simple-variable — §2.4 — so crediting the compound cache with them overstates
it ~8×).

- The compound **lookup** is the cleanly-measured `0.370 s` of `9.43 s`
  cpu_user = **3.9 % of host CPU**. Removing it (minus the residual value-copy)
  on ~78 % of accesses ≈ `0.78 × ~0.80 × 0.370 ≈ 0.23 s` = **~2.4 % of host
  CPU**. The probe-before-build form also removes ~78 % of the `0.093 s`
  key-build → **~3.2 % of host CPU** total. **Single-digit percent.**
- **MVS extrapolation (flagged as such):** the removed work (hash over memory +
  per-access call overhead) is memory-access-bound, so per CON-12 /
  WP-PERF-PROFILE the MVS payoff is **×3–5** the host fraction *for that part*.
  Measure on MVS against the 14 280 cps baseline (host cps is only a localiser),
  as OC-09 did — do not assume the host % transfers directly.
- **Residual:** the value `Lstrcpy` (14.1 M B) still runs unless the cache
  returns a *borrowed* value pointer (zero-copy) — an optional extension that
  also dovetails with **OC-12** (carry `type_cache`/`int_cache` forward on the
  cached entry, so a cached compound also skips numeric re-parse).
- **Cap caveat:** correct invalidation limits the hit to 77.8 %. The 91 % is
  only reachable by replicating the stem-reset bug (§5) — not an option.
- **Prioritisation:** good hit rate, modest lever. Against cluster A / OC-12
  (~32 %, correctness-sensitive) this is the smaller-but-safer structural win;
  the §2.4 simple-var observation may outrank both for raw vpool payoff.

### Risk

Medium — entirely in the **invalidation** (§4.1). Capacity/structure are
trivial (2 entries). The measured 77.8 % already *includes* the cost of correct
invalidation, so there is no hidden downside in the number.

---

## 4.1 Invalidation points for the recommended cache

The cache stores `key → entry`. It must invalidate on:

1. **Stem-default assignment** — `stem.=expr` (`OP_STORE_STEM` with a bare-stem
   name ending in `.`, `src/irx#bvm.c:1397`). **Evict every cached entry of that
   stem.** This is the **dominant** event in REXXCPS (one `avar.=` per loop
   iteration) and the reason the hit plateaus at 77.8 %.
2. **`DROP stem.`** — `OP_DROP_STEM` with `tail_cnt==0` → `vpool_drop_stem_all`
   (`src/irx#bvm.c:1429`). Evict the whole stem.
3. **`DROP stem.tail`** — `OP_DROP_STEM` with tails → `vpool_drop_buf`
   (`src/irx#bvm.c:1475`). Evict that one key.
4. **Active-vpool switch** — `CALL` into an internal routine with
   `PROCEDURE [EXPOSE]`, and the matching `RETURN`, change the live pool;
   exposed-stem delegation (`matches_exposed_stem` → parent) likewise redirects
   a stem to the parent pool. Cached entry pointers from one pool are invalid in
   another → **flush the cache, or tag entries with a pool generation and
   compare on probe.** (Not exercised by REXXCPS — its `subroutine` has no
   `PROCEDURE`, so it shares the caller's pool — but mandatory in general.)
5. **Update-in-place is *not* invalidation** — a store to an already-cached key
   updates the entry's value (and `type_cache`/`int_cache`); keep the entry.
6. **Tail-variable reassignment needs *no* explicit invalidation** — e.g.
   `loop=loop+1` produces a *different* composed key / different tail-slot
   values, so the probe misses naturally. (Only a cache keyed on tail-variable
   *names* would need to invalidate here; the value-keyed cache is
   self-invalidating, which is a further argument for that key shape.)

---

## 5. Side-finding — stem-reset correctness bug (separate ticket)

Discovered while validating the simulator's invalidation. **Reproducer:**

```rexx
a.1 = 5
a.  = 0
say 'a.1 =' a.1     /* prints 5 — REXX (SC28-1883-0) requires 0 */
```

Root cause chain:
- `a.=expr` compiles to `OP_STORE_STEM` **only** — no preceding `OP_DROP_STEM`
  (`src/irx#bcom.c:3854`; cf. the `DROP` path at `:3798` which *does* emit it).
- `vpool_set_buf("A.")` sets only the `A.` default holder; it does **not** drop
  existing `A.x` elements (`src/irx#vpol.c:904`).
- `vpool_get("A.1")` finds the element before the default → returns the stale 5.

This is independent of OC-06 but **couples with invalidation rule #1**: the fix
(drop-on-stem-assign) and the cache invalidation must land together, otherwise
the cache would be *more* correct than the underlying pool. **Recommend a
dedicated bug ticket** before/with the OC-06 fix. REXXCPS does not detect it
(it never re-reads a reset element expecting the default), so the perf workload
stays valid.

---

## 6. Reproduction

```sh
# 1. local diagnostic copy (instrumentation NOT committed)
cp -R src include test /tmp/oc06diag/ ; cp -R ../lstring370 /tmp/oc06diag/
#    apply Appendix A patches to the /tmp copies of irx#vpol.c, irx#bvm.c
#    + add diag_oc06.{h,c}
# 2. counters + trace
EXTRA_CFLAGS= ./build.sh /tmp/oc06diag/tstcps_instr
./tstcps_instr --source=test/rexxcps.rexx     # -> counters.txt, trace.tsv
# 3. timing split
EXTRA_CFLAGS="-DDIAG_DOUBLE_BUILD"  ./build.sh /tmp/oc06diag/tstcps_db
EXTRA_CFLAGS="-DDIAG_DOUBLE_LOOKUP" ./build.sh /tmp/oc06diag/tstcps_dl
#    run each 3×, compare cpu_user
# 4. hit-rate analysis
python3 analyze.py /tmp/oc06diag/trace.tsv
# 5. gprof-native cross-check (§2.2b) on a Linux host with gprof:
#    extract the OP_LOAD_STEM/OP_STORE_STEM key-build into a
#    `noinline irx_diag_build_key()` (local, uncommitted), then:
REXX370_BCDEBUG=1 ./scripts/host-profile.sh --source=test/rexxcps.rexx
grep -E "irx_diag_build_key|VPOOLGTB|VPOOLSTB" build/host-profile/profile.txt
```

Appendices A (instrumentation map) and B (`analyze.py`) below make this
reproducible; the instrumentation is diagnostic-only and intentionally **not**
added to `src/`.

---

## 7. What was NOT done (scope guard)

- **No cache implemented** in product code. Instrumentation lived only in the
  local copy and is not committed.
- Hit rates are **measured from the real trace**, not guessed.
- The fix (the cache) and the stem-reset bug fix are **separate tickets**.

---

## Appendix A — instrumentation map (local copy only)

A new `diag_oc06.{h,c}` defines the counters, the capped trace `d_trace()`, and
an `atexit`/destructor dump (`counters.txt`, `trace.tsv`). Insertion points:

| file (local copy) | what was added |
|---|---|
| `src/irx#vpol.c` `hash_bytes` | `if (d_diag_compound) d_lk_hash_bytes += n` |
| `src/irx#vpol.c` `find_in_bucket` | count node-visits + `memcmp` bytes (gated) |
| `src/irx#vpol.c` `matches_exposed_stem` | count loop iterations (gated) |
| `src/irx#vpol.c` `vpool_get_buf` | set `d_diag_compound` on dotted name; `d_trace('L',…)`; count value bytes / stem-fallback; single-exit reset |
| `src/irx#vpol.c` `vpool_set_buf` | same, `d_trace('S',…)`; value bytes |
| `src/irx#vpol.c` `vpool_drop_buf` / `vpool_drop_stem_all` | `d_trace('E'/'D',…)` |
| `src/irx#bvm.c` `OP_LOAD_STEM` / `OP_STORE_STEM` | key-build counters (`stem_bytes`, `tail_bytes = name_pos − stem_len − dots`, `dots`, `keylen`); `#ifdef DIAG_DOUBLE_BUILD` / `DIAG_DOUBLE_LOOKUP` ablation blocks |

The lookup counters are gated on `d_diag_compound` (set only around the dotted
path) so the shared helpers — which also serve simple variables — attribute work
to the compound path exclusively. Single-threaded host, so a plain global flag
is sufficient.

## Appendix B — `analyze.py` (offline hit-rate analyzer)

```python
import sys
from collections import OrderedDict, defaultdict

def stem_of(key):
    i = key.find(".")
    return key[: i + 1] if i >= 0 else key

def load_trace(path):
    recs = []
    for line in open(path):
        line = line.rstrip("\n")
        if line:
            op, _, key = line.partition("\t")
            recs.append((op, key))
    return recs

# Strategy 1: full-key LRU. A stem-default store (key ends '.') or stem drop
# evicts that stem's cached elements (correct REXX); element drop evicts the
# key; element store is update-in-place.
def sim_full_key(recs, size, invalidate_on_stemdefault=True):
    cache = OrderedDict(); hits = 0; accesses = 0
    def evict_stem(stem):
        for k in [k for k in cache if stem_of(k) == stem]:
            del cache[k]
    for op, key in recs:
        if op == "D": evict_stem(stem_of(key)); continue
        if op == "E": cache.pop(key, None); continue
        if op == "S" and key.endswith("."):
            if invalidate_on_stemdefault: evict_stem(stem_of(key))
            cache[key] = True; cache.move_to_end(key)
            while len(cache) > size: cache.popitem(last=False)
            continue
        accesses += 1
        if key in cache: hits += 1; cache.move_to_end(key)
        else:
            cache[key] = True; cache.move_to_end(key)
            while len(cache) > size: cache.popitem(last=False)
    return hits, accesses

# Strategy 2: stem-anchor — temporal locality of the stem only.
def sim_stem_anchor(recs, size):
    cache = OrderedDict(); hits = 0; accesses = 0
    for op, key in recs:
        if op in ("D", "E"): continue
        if op == "S" and key.endswith("."): continue
        accesses += 1; s = stem_of(key)
        if s in cache: hits += 1; cache.move_to_end(s)
        else:
            cache[s] = True; cache.move_to_end(s)
            while len(cache) > size: cache.popitem(last=False)
    return hits, accesses

# Strategy 3: per-(stem,arity,position) tail-value repeat rate.
def sim_tail_resolution(recs):
    stats = defaultdict(lambda: [0, 0]); last = {}
    for op, key in recs:
        if op in ("D", "E"): continue
        if op == "S" and key.endswith("."): continue
        s = stem_of(key); rest = key[len(s):]
        if rest == "": continue
        for pos, tv in enumerate(rest.split(".")):
            sk = (s, rest.count(".") + 1, pos); st = stats[sk]
            st[1] += 1
            if last.get(sk) == tv: st[0] += 1
            last[sk] = tv
    return stats

recs = load_trace(sys.argv[1])
for inval in (True, False):
    for size in (1, 2, 4, 8, 16):
        h, a = sim_full_key(recs, size, inval)
        print(f"full-key inval={inval} size {size:2d}: {100.0*h/a:5.1f}% ({h}/{a})")
for size in (1, 2):
    h, a = sim_stem_anchor(recs, size)
    print(f"stem-anchor size {size}: {100.0*h/a:5.1f}% ({h}/{a})")
tr = tot = 0
for (s, nt, pos), (r, o) in sorted(sim_tail_resolution(recs).items()):
    tr += r; tot += o
    print(f"tail {s} arity {nt} pos {pos}: {100.0*r/o:5.1f}% ({r}/{o})")
print(f"tail ALL: {100.0*tr/tot:5.1f}% ({tr}/{tot})")
```
