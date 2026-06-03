# WP-PERF-VARCACHE — Variable-Cache Invalidation Frequency (pre-build diagnosis)

**Type:** Measurement / diagnosis only — **NO cache implementation, NO product
code**. Instrument a throwaway copy, count variable-pool events over a REXXCPS
run, decide whether the per-operand variable-resolution cache (recommended in
`docs/diag/wp-perf-profile-2.md`) is a real lever or is neutralised by
invalidation. The build decision is Mike's + the architecture review.
**Branch:** `feature/wp-perf-varcache-diag`
**HEAD commit:** `cbcfa30` — *docs(perf): fresh bytecode VM profile after
OC-12/OC-09/OC-ARITH (#193)*
**Date:** 2026-06-03
**References:** `docs/diag/wp-perf-profile-2.md` (candidate 1 = the var cache);
CON-12 (the OC-06/OC-09 lesson — profile share ≠ hot-loop lever).

---

## TL;DR

**The candidate is a real lever — the strongest in the profile — and is NOT
neutralised by invalidation. The task's founding premise is wrong for this
codebase.**

1. The premise to test was: *"a per-operand variable cache must invalidate on
   every assignment, DROP, frame switch, … else it returns stale pointers."*
   For REXXCPS the dominant event is **assignment** (`set` = 90.8% of all
   invalidation candidates). If assignment invalidated, the cache would be
   OC-06-redux (dead).
2. **But assignment does NOT invalidate a *pointer* cache in this codebase.**
   `vpool_set_buf`'s update path does `Lstrcpy(&tgt->value, value)` **in place
   on the same entry object** (`src/irx#vpol.c:942`) — the entry *address* is
   stable across writes, so a cached entry pointer stays valid and reads the new
   value live. And bucket resize **re-links** the existing entry objects without
   moving them (`maybe_resize`, `src/irx#vpol.c:267-285`) — pointers survive
   resize too. A pointer cache is invalidated **only** by entry *frees*
   (`DROP`/stem-drop) and scope switches (`PROCEDURE`).
3. **Measured over a pinned 10×10 REXXCPS run** (100 executions of the
   1000-clause body, `[bc] exec=1 fallback=0`):

   | Cache model | what invalidates | reads / invalidation | per-entry hit rate |
   |---|---|---:|---:|
   | Coarse **value** cache (write bumps a global gen) | every set + drop + dropstem + frame switch | **2.06** | — |
   | Frame-aware **value** cache | every set + drop + dropstem | 2.16 | **77.3 %** |
   | **Pointer cache (coarse gen)** | drop + dropstem + scope only | **44.8** | — |
   | **Pointer cache (per-entry)** | a freed/re-created entry only | (≈45.7) | **97.8 %** † |

   † aggregate sim figure — splits into **simple vars ~99.9 % (clean, the lever)**
   vs **compounds lower / sim-overstated** (point 4 + §5.2).

4. **Verdict:** A *write-invalidated* cache is dead (2.06 reads/inval — what one
   would naively build, and what the task feared). A **pointer/resolution
   cache** — the design `wp-perf-profile-2.md` actually proposed — sails past the
   ≥10 bar at **44.8 reads/invalidation**, because writes (90.8 % of events) are
   non-events for it. The hit rate splits by variable kind, and the headline must
   not be flattened:
   - **Simple variables (~90 % of reads): ~99.9 % hit, clean.** The operand site
     maps to a fixed entry; the cache removes the *whole* lookup. `j` =
     99.996 % (one cold miss in the entire run). **This is the lever.**
   - **Compound variables (~10 %: `acompound.*` 5.6 % + `avar.*`): lower, and the
     sim overstates them** — see §5.2. A per-operand cache cannot cache the entry
     pointer for a compound (the tail is re-derived from runtime values each
     reference); it can skip the hash+bucket-walk but must still rebuild the
     composed key. And `avar.*` is genuinely churned (stem-drop every iteration).
   So the aggregate 97.8 % per-entry figure is a **simple-var-dominated upper
   bound**. The cache removes the vpool lookup (`hash`+`bucket`+`bucket-walk`+
   exposed-stem ≈ **18.8 %** of host self-time, profile-2); the **cleanly
   removable** part is the simple-var share of that (compound lookups cost more
   per-op — longer keys — so they are over-weighted in the 18.8 %, and only
   partly removable). Net: a strong, **memory-access-bound** win (pointer-chase +
   string compare → Hercules multiplier on MVS), best stated as "~18 % is the
   upper bound; the MVS-cps prototype pins the real figure." **Build it.** It does
   *not* remove the value copy-out (`Lstrcpy`) — that is candidate 2 (value
   copy-elision), complementary.

---

## 1. Method

Throwaway instrumentation in a **local copy** of the engine (never committed to
the product; the tree was `git checkout`-restored after measuring). Counters
were added at the variable-pool entry points and the bytecode frame
push/pop/scope-switch sites:

- `vpool_get_buf` → `reads` (the hot read; asm alias `VPOOLGTB`)
- `vpool_set_buf` → `set`
- `vpool_drop_buf` → `drop`
- `vpool_drop_stem_all` → `dropstem`
- `vpool_expose_var`/`_stem` → `expose`
- `OP_CALL`/`OP_CALL_BIF` frame push → `call`; `OP_RETURN`/`OP_RETURNV`
  pop → `return`; `PROCEDURE` vpool switch (`cf->prev_vpool = vpool`) → `proc`

**Two per-entry cache models were simulated on the same reads** (counting only —
no pointers are ever reused, no behaviour changes):

- **VALUE cache** — a read hits iff the entry was not *written* since this
  entry was last read. `wver` bumps on **every** `set` (create + in-place
  update). This is what an invalidate-on-assignment cache would achieve.
- **POINTER cache** — a read hits iff the entry was not *re-created* since this
  entry was last read. `pwver` bumps **only on entry create**. In-place updates
  keep the entry address valid → a cached pointer is **not** stale, so writes do
  not count as misses. This is the candidate from `wp-perf-profile-2.md`.

> **The per-entry sim is an *optimistic upper bound* on a real per-operand
> cache, in three ways:**
> (a) **Compounds (the big one):** the sim keys on the *fully-resolved* entry,
> as if resolution were free. A real per-operand cache **cannot** cache the entry
> pointer for a compound (`acompound.key1.loop`) — REXX re-derives the tail from
> the runtime values of `key1`/`loop` on every reference, so the same operand
> site targets a *different* entry when `loop` changes. A correct compound cache
> must rebuild the composed key (it may skip the hash+bucket-walk, but not the
> key-build + tail-variable reads). So the sim's compound hit rate is achievable
> only for the lookup tail, not the whole resolution. **Simple variables do not
> have this problem** — their operand→entry mapping is fixed.
> (b) a variable read from K bytecode sites is one warm slot in the sim but K
> independently-warming slots in reality (extra cold misses — negligible at
> steady state);
> (c) the sim keys on the entry, so it implicitly assumes the
> resize-pointer-stability that `maybe_resize` in fact provides (which it does).
> Caveat (a) is why the headline is split simple vs compound (§4); (b)/(c) are
> minor. The simple-var result — the lever — is *not* optimistic and is ~99.9 %.

**Workload:** `test/rexxcps.rexx` pinned to `count=10`, `averaging=10`,
`do trial=1 to 1` (no re-calibration) → **100 executions of the 1000-clause
body**, confirmed by `call=1400` (14 `call subroutine` per body × 100).
`[bc] exec=1 fallback=0` — pure bytecode, no token-walk. Host: mvsdev.lan
(Debian 13, gcc 14.2.0), built via `scripts/host-profile.sh`. Counts are
**deterministic and host-independent** (re-running reproduces them exactly).

---

## 2. Raw event counts (one pinned 10×10 run)

```
[vcd] reads=125458 notfound=0 set=55184 drop=1400 dropstem=1400 expose=0
      call=1400 ret=1400 proc=0
[vcd] VALUE  : sel_hit=96995  sel_miss=28463 total=125458
              j_hit=21000 j_miss=4200  acmp_hit=4200 acmp_miss=2800
[vcd] POINTER: p_hit=122714  p_miss=2744  total=125458
              pj_hit=25199 pj_miss=1     pacmp_hit=6986 pacmp_miss=14
```

| Event | Count | per body | note |
|---|---:|---:|---|
| `reads` (`vpool_get_buf`) | 125,458 | ~1255 | all resolve (`notfound=0`) |
| `set` (`vpool_set_buf`) | 55,184 | ~552 | **90.8 % of all events; in-place on existing entries** |
| `drop` (`vpool_drop_buf`) | 1,400 | 14 | `RESULT` dropped on each `return` |
| `dropstem` (`vpool_drop_stem_all`) | 1,400 | 14 | the `avar.=` bare-stem assignment |
| `expose` | 0 | — | REXXCPS uses no `PROCEDURE EXPOSE` |
| `call` / `return` | 1,400 / 1,400 | 14 / 14 | frame push / pop |
| **`proc`** (isolated-scope switch) | **0** | — | **the subroutine has no `PROCEDURE` → shared pool** |

---

## 3. Reads per invalidation — by model

| Model | invalidating events | total | reads / inval |
|---|---|---:|---:|
| Coarse **value** (write bumps a single global generation) | `set+drop+dropstem+expose+call+return` | 60,784 | **2.06** |
| Frame-aware **value** (calls free — shared scope) | `set+drop+dropstem+expose` | 57,984 | 2.16 |
| **Coarse pointer** (only free/scope bumps the generation) | `drop+dropstem+proc` | **2,800** | **44.8** |

**Reading these:**

- **The naive coarse *value* cache is dead (2.06).** Sets fire every ~2 reads;
  any whole-cache generation that bumps on writes cold-restarts constantly. This
  is the OC-06-redux outcome — *if* writes invalidated.
- **Frame-awareness alone does not save the value model (2.16).** Frame switches
  are only 2,800 / 60,784 = **4.6 %** of coarse invalidations (and free anyway —
  `proc=0`, the calls are shared-scope); **sets are 90.8 %**. So the value
  model's problem is writes, not frames.
- **The pointer cache changes the denominator entirely.** Writes don't
  invalidate it, so the only events that do are entry frees + scope switches =
  **2,800**, giving **44.8 reads/invalidation — above the ≥10 build bar.** Even
  the *simplest* correct pointer cache (one pool-generation counter bumped only
  on `DROP`/stem-drop/`PROCEDURE`/resize — all rare) clears the bar.

---

## 4. Per-entry hit rates (the realised lever)

| Cache model | hit | miss | hit rate | reads / miss |
|---|---:|---:|---:|---:|
| VALUE (write invalidates) | 96,995 | 28,463 | **77.3 %** | 4.41 |
| **POINTER (write does not invalidate)** | 122,714 | 2,744 | **97.8 %** | **45.7** |

**Validation against hand-computed expectations** (the sim is only trustworthy
if these match):

- **`j`** — read ~6×/iteration, written once/iteration by the loop control.
  - VALUE: 21,000 hit / 4,200 miss = **83.3 %** (expected ~80 % — write evicts
    once per iteration). ✓
  - POINTER: 25,199 hit / **1 miss** = **99.996 %** — `j` is created once in the
    whole run and never freed, so exactly **one** cold miss. ✓ (textbook)
- **`acompound.key1.loop`** — read-modify-written every iteration.
  - VALUE: 4,200 hit / 2,800 miss = **60 %** (expected ~50 % — RMW evicts). ✓
  - POINTER: 6,986 hit / **14 miss** = **99.8 %** at the *entry* level (14
    distinct tails `loop=1..14`, each created once, never freed). **But this is
    an optimistic bound, not an achievable per-operand hit rate** (caveat (a)
    above): the cache cannot cache the entry pointer for a compound — the operand
    re-derives the tail from `key1`/`loop` each reference, so it must rebuild the
    composed key (reading `key1` and `loop`) and can skip only the hash+walk. The
    99.8 % measures "the target entry is stable", which bounds the *lookup-tail*
    saving — it does **not** mean compound resolution is 99.8 % free.

The pointer-cache misses (2,744 total) are almost entirely **`avar.`** churn: the
`avar.=` bare-stem assignment does `dropstem`+`set` every loop iteration, so the
`AVAR.` default entry is freed and re-created 1,400 times, cold-missing on its
next read each time. The hot scalars (`j`, `loop`, `flag`, `key1`) and the
`acompound.*` tails are never dropped → ~100 % hit.

---

## 5. Why the task's premise does not hold (the crux)

### 5.1 Assignment does not invalidate a pointer cache

The task assumed *assignment must invalidate the cache, "sonst stale Pointer."*
That is true for a cache of the **value** (or of a copied value buffer). It is
**false for a cache of the resolved entry pointer in this implementation:**

- `vpool_set_buf` update path (`src/irx#vpol.c:935-950`) writes the new value
  **in place** into the existing entry's `value` field (`Lstrcpy(&tgt->value,
  value)`); it does not free or replace the entry. **The entry address is
  invariant across writes**, so a cached pointer reads the updated value
  correctly.
- `maybe_resize` (`src/irx#vpol.c:267-285`) re-links the existing entry objects
  into a freshly allocated **bucket pointer array**; it frees the old bucket
  array, never the entry structs. **Entry addresses are invariant across
  resize.** (Confirmed by reading the resize loop, not inferred.)

So the only operations that invalidate a pointer cache are those that **free an
entry** (`vpool_drop_buf`, `vpool_drop_stem_all`) or **change the resolution
scope** (`PROCEDURE`, `EXPOSE`). In REXXCPS those total 2,800 vs 55,184 writes —
the feared invalidator is a non-event.

### 5.2 Where the premise *partly* holds — compounds

The pointer-cache argument above is airtight for **simple variables**: operand
site → fixed entry, so a cached pointer is correct until the entry is freed.

It does **not** transfer to **compound** references (`acompound.key1.loop`). REXX
re-derives the tail from the *runtime values* of the tail symbols on every
reference, so the same operand site targets a *different* entry when `key1` or
`loop` changes — caching a fixed entry pointer there would return stale data (a
correctness bug). A correct compound cache must read the tail variables and
rebuild the composed key; it can then skip the final hash+bucket-walk, but **not**
the key-build. So the per-entry sim's 99.8 % for `acompound` (§4) measures only
that the *target entry is stable*, which bounds the lookup-tail saving — it
over-credits compounds as if their whole resolution were cacheable. This is why
the headline is split (simple ~99.9 % clean vs compound lower) and why ~18 % is
an upper bound, not a point estimate. The simple-variable bulk (~90 % of reads)
carries the lever regardless.

---

## 6. Recommendation

**Build the per-operand pointer/resolution cache (candidate 1). It is a real,
strong, memory-bound lever and is NOT neutralised by invalidation — the opposite
of OC-06.** Specifics, with the OC-06/OC-09 *measure-before-commit* discipline:

1. **Cache the resolved entry pointer at each simple-variable operand site.** On
   a hit, skip `hash_bytes`+`bucket_index`+`find_in_bucket`+exposed-stem and read
   the value live from the entry. This is the clean ~99.9 %-hit win (~90 % of
   reads). Profile-2 flat-self for the lookup work (all gets) = `bucket_index`
   7.84 + `hash_bytes` 4.88 + `find_in_bucket` 3.31 + `matches_exposed_stem` 1.39
   + `name_matches_stem` 1.39 = **~18.8 %**. **Treat ~18 % as an upper bound, not
   a point estimate:** it is dominated by the simple-var share (cleanly
   removable), while the compound share of that 18.8 % is (i) over-weighted
   (compound keys are longer → more hashing per op) and (ii) only *partly*
   removable (a compound cache must still rebuild the composed key from the tail
   vars; see below). The lever is **memory-access-bound** (pointer-chase + string
   compare), so the Hercules multiplier applies on MVS — but the MVS-cps
   prototype (step 4) is what pins the real number.
   - **Compounds** (`acompound.*`) need a different mechanism — cache keyed on the
     *composed key*, skipping only the final hash+bucket-walk, since the operand
     re-derives the tail each reference. Lower payoff; design it second, or scope
     the first cut to simple variables only (where the bulk of the win is).
2. **Even the simplest correct design works:** a single pool-generation counter
   bumped on `DROP` / stem-drop / `PROCEDURE` / `EXPOSE` / resize (all rare —
   2,800/run, none on the write path). Coarse → 44.8 reads/inval. A per-operand
   slot scheme reaches ~98 %. Start coarse; it already clears the bar.
3. **Invalidation correctness checklist** (the real risks, all cheap because
   rare): (a) entry free — `DROP`, stem-drop; (b) scope switch — `PROCEDURE`,
   `EXPOSE` delegation; (c) **stem-default → specific shadowing** — when a
   specific tail (e.g. `avar.1.2`) is first created, operands that cached the
   stem-default (`avar.`) pointer must re-resolve; (d) resize is **safe**
   (entries don't move) — but bumping the pool-generation on resize anyway is
   free insurance.
4. **Measure on MVS cps before committing** (CON-12: host % is an iteration
   tool, MVS cps is the target metric). The 97.8 % is host-measured for REXXCPS
   specifically; `DROP`-heavy / `PROCEDURE EXPOSE`-heavy / `INTERPRET` workloads
   invalidate more.

**Complementary, not either/or:** the pointer cache leaves the value copy-out
(`Lstrcpy`, profile-2) in place — that is exactly what **candidate 2 (value
copy-elision)** targets. Together they drain `VPOOLGTB` (the single largest
sub-tree in profile-2); the pointer cache removes the lookup, copy-elision
removes the copy.

**What NOT to build:** a write-invalidated (value-form / value-buffer) cache —
that is the 2.06-reads/inval dead end the first sim measured.

---

## 7. Reproduction

```sh
ssh mvsdev.lan
cd repos/rexx370   # HEAD cbcfa30
# In a throwaway copy of the tree, add counters to vpool_get_buf/set_buf/
# drop_buf/drop_stem_all/expose_* (src/irx#vpol.c) and to OP_CALL/OP_RETURN/
# PROCEDURE (src/irx#bvm.c); simulate two per-entry caches (VALUE: bump a
# write-version on every set; POINTER: bump a create-version only on entry
# create) and tally hit/miss per read.  Pin rexxcps.rexx to count=10,
# averaging=10, do trial=1 to 1.  Build + run:
REXX370_BCDEBUG=1 ./scripts/host-profile.sh --source=test/rexxcps.rexx --summary
#   run.log    -> "[bc] exec=1 fallback=0"   (bytecode gate)
#   timing.txt -> the "[vcd] ..." counter lines
git checkout -- .   # restore the product tree (instrumentation is throwaway)
```

Counts are deterministic; the absolute numbers above reproduce exactly. The
instrumentation is not committed — only this diagnosis document is.
