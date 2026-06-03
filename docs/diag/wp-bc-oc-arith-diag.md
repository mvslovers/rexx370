# WP-BC-OC-ARITH-DIAG — Cluster A Arithmetic-Op Breakdown + Fast-Path Diagnosis

**Ticket:** WP-BC-OC-ARITH-DIAG
**Type:** **Diagnosis only — NO fix.** Measures the arithmetic-*op* half of
cluster A (the part OC-12 did not touch) and recommends whether a fix is
worth building. Builds nothing in product code. The fix is a separate ticket.
**Branch:** `wp-bc-oc-arith-diag`
**HEAD commit:** `5fc0239` — *docs(diag): WP-BC-OC06-DIAG — OC-06
deprioritised (#182)* (WP-PERF-PROFILE baseline + OC-09 + OC-12 merged).
**References:** CON-12 (cluster A ~32 %, OC-01 integer fast-path,
Hercules multiplier); `docs/diag/wp-perf-profile.md` (PR #179, cluster A
finding); `docs/diag/wp-bc-oc06-diag.md` (PR #182, methodology template:
local-copy instrumentation, three-way convergence, profile-share ≠
hot-loop-effect lesson). OC-12 = the int-cache compare fast-path (#180).

---

## Verdict

**A small, low-risk integer fix is worth building; a decimal fast-path is
not.** Cluster A splits into a *compare* half (IRXARICM — OC-12's domain) and
an *op* half (IRXARIOP — this WP). On the **post-OC-12** main the op half is
still **~16 % of VM self-time**, so the "hot-loop arith is smaller than the
committed share" trap is **not** triggered — it is a genuine 16 %.

Inside the op half the OC-01 integer fast-path (`try_arith_fast`) **hits only
10.1 %** of the time. The 89.9 % that fall to BCD split **44.8 % fixable
(integer-valued operands that were simply never int-cached) / 55.2 %
irreducible (a decimal/exponent operand is present — an integer fast-path can
never cover these)**. A directly-ablated fix that routes the fixable ops to
the fast path reclaims **6.2 % of host CPU** (output byte-identical), and its
removed work is partly memory-bound (alloc/free churn) so the MVS payoff is
amplified. The irreducible 55.2 % (≈ 9.8 % of host CPU) is decimal arithmetic
that only a complex, correctness-sensitive packed-decimal path could touch —
and it is partly a REXXCPS artifact.

**Recommendation: build the op-path integer fix** (the op-path analog of
OC-12 — same trusted mechanism, same risk class). **Decline / deprioritise a
decimal fast-path.** This is a *bounded yes*, not an OC-06-style negative and
not an unqualified win.

**Why this clears the bar when OC-06 (also single-digit) did not.** OC-06 was
deprioritised at **2.4–3.2 % host** with **medium risk** — its entire risk
budget was cache *invalidation* (DROP / PROCEDURE EXPOSE / frame boundary).
This fix is **~2× the lever (6.2 %) at strictly lower risk**: it has **no
invalidation surface** (plain int32 behind the existing overflow guards, no
cached state to keep coherent), and it **completes the OC-12 mechanism already
shipped in #180** rather than adding a new subsystem. Higher payoff, lower
risk, no new surface — the opposite of OC-06's profile.

## TL;DR

1. **The fast-path barely fires (FRAGE 1).** `try_arith_fast` is called
   1 390 401× and succeeds **140 200× = 10.08 %**. The successes are *all*
   `OP_ADD`, and they are exactly the `do loop=1 to 14` integer counter
   increment (`loop=loop+1`, 14 × 10 000 units = 140 000). **Every explicit
   arithmetic statement in the program falls to BCD** — the fast-path's only
   beneficiary in REXXCPS is the integer DO-loop counter. DIV/IDIV/MOD/POW/NEG
   ≈ 0, so "fast-eligible" and "all-arith" hit rates are both 10.08 %.

2. **The 1 250 201 BCD fallbacks are 45 % fixable / 55 % irreducible
   (FRAGE 2).** Each `(op, operand-class)` pair maps 1:1 to a source
   construct:

   | fallback | count | class | construct |
   |---|---:|---|---|
   | ADD uncached-int + cached-int | 280 000 | **fixable** | `acompound.key1.loop+1` |
   | SUB uncached-int − cached-int | 280 000 | **fixable** | `length(j)-1` |
   | ADD cached-int + decimal | 140 000 | irreducible | `flag=5+99.7` |
   | ADD decimal + decimal | 280 198 | irreducible | `do j=1.1 to 2.2 by 1.1` step |
   | MUL exponent/decimal × decimal | 270 000 | irreducible | `avar.1.2=avar.1.2*1.1` |

   **Fixable = 560 000 (44.8 %)**; **irreducible = 690 201 (55.2 %)**;
   bigint/non-numeric = 0. "Fixable" = both operands are plain integers of
   ≤ 9 digits (the `try_parse_int_cache` window) that simply were not tagged
   `LINTEGER`, because the value came from a BIF (`substr`, `length`) or from
   a previous BCD result (which clears `type_cache`, `irx#bvm.c:1527`).

3. **Time split is three-way consistent (FRAGE 3).** gprof: op half
   (`IRXARIOP`) = **16.0 % cumulative**. Ablation (route fixable → fast):
   cpu_user **3.097 s → 2.904 s = −6.2 %**. Op-counts weighted by gprof
   per-op self-time: fixable ≈ 39 % / irreducible ≈ 61 % of the op half.
   These agree: 6.2 % ÷ 16 % ≈ 39 %. The **fast-path's own success cost is
   negligible** (`i32toa` 0.00 s / 140 200 calls; its `slot_set_buf` share
   140 200 ⁄ 20 013 074) — the task's third outcome ("fast-path hits but
   i32toa/slot_set_buf dominates") is **ruled out**.

4. **Recommendation:** build the op-path integer fix — **parse an uncached
   but integer-valued operand on demand inside `try_arith_fast`** (the exact
   ablation; simplest, catches both fixable constructs), or equivalently
   carry `int_cache` forward across `irx_arith_op` results and numeric-BIF
   results. ~6.2 % host; the removed work is *mixed* — the digit-walk is
   CPU-bound (≈ host ceiling) but the `num_alloc`/`num_free`/`irxstor` churn
   is memory-bound (Hercules ×3–5), so the **memory part** lands above the
   host figure on MVS. **Decline the decimal fast-path** for the irreducible
   9.8 %: high effort + high correctness risk (NUMERIC DIGITS / rounding /
   exponent form) and partly benchmark-specific.

5. **Neither escalation condition is met.** The op half is genuinely ~16 %
   (not smaller than the committed share — *not* the OC-09/OC-06 trap a third
   time), and it is *not* overwhelmingly irreducible decimal — a worthwhile
   integer fix exists.

---

## 1. Methodology

All instrumentation lived in a **local diagnostic copy** (`/tmp/ocaridiag`);
**nothing was committed** and the repo working tree stayed pristine
throughout (`git status` clean but for the untracked `.claude/`). Host: WSL2
(Linux 6.6, gcc 13.3.0), the same box and `-O0` build as WP-PERF-PROFILE /
WP-BC-OC06-DIAG.

Workload: `test/rexxcps.rexx` (REXXCPS 2.2), `count=100 × averaging=100`,
≈ 10 M clauses, **`[bc] exec=1 fallback=0`** (100 % bytecode, zero
token-walk — the methodology depends on this and it was re-verified on every
run). Deterministic: no RNG, so op-counters are exact and reproducible.

Three independent instruments (full map in Appendix A):

- **Op-counters** (deterministic) inside `try_arith_fast` and the
  `OP_ADD…OP_POW` dispatch: per-op dispatch / fast-attempt / fast-success
  counts, and — on every guard-fail — a classification of operand `a` and
  `b` into {cached-int, uncached-int (≤ 9 dig), bigint (> 9 dig),
  decimal, exponent, other}, plus the full `(op, a-class, b-class)`
  histogram. The classifier mirrors `try_parse_int_cache` exactly so
  "uncached-int" means *"this operand would be fixable by the existing cache
  mechanism."*
- **gprof** (`-pg`, the committed `scripts/host-profile.sh`): `IRXARIOP`
  cumulative and the per-leaf self-times (`num_addsub`, `num_mul`,
  `num_from_str`, `num_to_lstr`, `num_alloc`/`num_free`, `i32toa`).
  `IRXARIOP`/`try_arith_fast`/`i32toa` are already distinct symbols — no
  noinline extraction was needed (unlike OC-06-DIAG's key-build).
- **Correctness-preserving ablation** (`-DDIAG_SIMFIX`, a clean no-counter
  build): make `try_arith_fast` accept an uncached but integer-valued operand
  by parsing it on demand (same ≤ 9-digit window). This *simulates the fix*,
  so its cpu_user delta is the directly-measured achievable win. Output was
  byte-identical to baseline except the program's own self-reported cps line.

---

## 2. FRAGE 1 — does `try_arith_fast` fire in the hot loop?

Raw counters (full REXXCPS run, `arith-counters.txt`):

```
op     dispatch    fastTry     fastOK    ovfBail      ->BCD
ADD      840400     840400     140200          0     700200
SUB      280001     280001          0          0     280001
MUL      270000     270000          0          0     270000
DIV           4          0          0          0          0
IDIV          0          0          0          0          0
MOD/POW       0          0          0          0          0
ALL     1390405    1390401     140200          0    1250201
OP_NEG dispatches: 0
```

- **fast-eligible hit rate = 10.08 % (140 200 / 1 390 401).**
- **all-arith hit rate = 10.08 %** — DIV (4) / IDIV / MOD / POW / NEG are
  ≈ 0, so the structurally-ineligible ops dilute nothing.
- **0 overflow/div-zero bailouts** — every fast-eligible call that passed the
  `LINTEGER` guard also succeeded; the 89.9 % miss is *entirely* the guard
  itself (an operand is not `LINTEGER`), never the overflow guards.
- **All 140 200 successes are `OP_ADD`.** A controlled `DO var=expr TO expr
  [BY step]` compiles its increment to `load var; load step; OP_ADD; store
  var` (verified at `src/irx#bcom.c:3003-3007`, `BCTL_DO_TO`), so the counter
  bump runs through `try_arith_fast`. The 140 200 = the `do loop=1 to 14`
  integer increment (14 × 10 000 = 140 000) + ~200 from the `do i=1 to
  averaging` counters. **No explicit arithmetic statement in the source ever
  takes the fast path** — only the integer DO-counter does.

Cross-checks against the committed gprof (`build/host-profile/profile.txt`):
`i32toa` is called **140 200×** (it runs only on fast success,
`irx#bvm.c:511`) — equals our success counter. `IRXARIOP` (`irx_arith_op`,
the BCD entry) is called **1 250 205×** ≈ our 1 250 201 fallbacks + 4 DIV.
Independent confirmation of the 10 % hit rate.

---

## 3. FRAGE 2 — why the fallbacks miss (decimal vs uncached-integer)

`(op, a-class, b-class)` histogram over the 1 250 201 fallbacks:

```
ADD   a=uncached-int  b=cached-int :  280000     <- acompound.key1.loop + 1
SUB   a=uncached-int  b=cached-int :  280000     <- length(j) - 1
ADD   a=cached-int    b=decimal    :  140000     <- flag = 5 + 99.7
ADD   a=decimal       b=decimal    :  280198     <- do j=1.1 to 2.2 by 1.1
MUL   a=exponent      b=decimal    :  269758  )
MUL   a=decimal       b=decimal    :     191  }  <- avar.1.2 = avar.1.2 * 1.1
MUL   a=uncached/bigint b=decimal  :      51  )
```

**FRAGE 2 roll-up:**

| class | count | share | reachable by integer fast-path? |
|---|---:|---:|---|
| **fixable** (both int ≤ 9 dig, uncached) | **560 000** | **44.8 %** | **yes** |
| irreducible (a/b decimal or exponent) | 690 201 | 55.2 % | no |
| bigint (> 9 dig) / non-numeric | 0 | 0 % | — |

### Why the fixable operands are not cached

- `acompound.key1.loop` is `substr(1234"5678",6,2)` = `"78"` — an integer
  *string* produced by a string BIF, which stores it with `type_cache = 0`.
  The subsequent `+1` is BCD, and the BCD result path **clears `type_cache`**
  (`irx#bvm.c:1527`), so it stays uncached on the next `+1` too. Both `+1`
  per `j`-iteration miss.
- `length(j)` returns `"3"` — an integer from a numeric BIF, again
  `type_cache = 0`. Every `length(j)-1` misses.

Both are *integer-valued*; nothing about REXX semantics forces them to BCD.
They miss purely because the cached numeric form is **not carried forward**
from BIF results / BCD results — the same gap OC-12 closed on the *compare*
path (#180 carried `int_cache` into the compare opcode but left the op path
and the BIF/BCD producers untouched).

### Why the irreducible operands cannot be helped

`5+99.7`, the `do j=1.1 … by 1.1` step, and `avar.1.2*1.1` all carry a
decimal (or, for the growing `avar.1.2` accumulator, an exponent-form)
operand. `try_parse_int_cache` rejects them by construction; an int32
fast-path cannot represent `99.7` or `1.5E+10`. These are BCD *by nature*.

---

## 4. FRAGE 3 — time split (three-way)

### 4.1 gprof (leg 1)

`IRXARIOP` cumulative = **16.0 %** of VM self-time (0.26 s of the 1.62 s
gprof basis), self-time 0.00 s. Its children:

| child | calls | gprof time | note |
|---|---:|---:|---|
| `num_addsub` | 980 201 | 0.08 s | ADD/SUB BCD |
| `num_mul` | 270 000 | 0.06 s | MUL BCD (the decimal accumulator) |
| `lstr_to_num`→`num_from_str` | 2 500 410 | ~0.05 s | operand re-parse (2 per op) |
| `num_to_lstr` | 1 250 205 | ~0.05 s | result re-format |
| `num_alloc`/`num_free` | 3.75 M (op share) | ~0.02 s | BCD scratch churn |

Fast-path success cost is **negligible**: `i32toa` 0.00 s / 140 200 calls;
`slot_set_buf` from `try_arith_fast` is 140 200 ⁄ 20 013 074 of a 0.06 s
symbol ≈ 0.0004 s. → the "hits but i32toa/slot_set_buf dominates" outcome is
**ruled out**; when the fast path fires it is essentially free.

### 4.2 Ablation (leg 2 — the achievable win, directly measured)

`-DDIAG_SIMFIX` (parse uncached-integer operands on demand; decimals fail the
parse and fall through unchanged). 6 interleaved runs, cpu_user:

| build | mean cpu_user | Δ vs baseline |
|---|---:|---:|
| baseline | 3.097 s | — |
| `+DIAG_SIMFIX` | 2.904 s | **−0.194 s = −6.2 %** |

Output diff showed **only** the program's self-reported cps/calibration lines
(3.08 M → 3.24 M cps — expected, it measures itself). REXXCPS **self-validates
its own arithmetic** via the `if … say 'Failed1'..'FailedT2'` guards on every
hot-loop construct; the clean diff means **none of those guards fired under
the ablation** — the workload itself confirms the fast-path results match BCD,
which is stronger than byte-identity. So routing the **560 000 fixable** ops
to the fast path is worth **6.2 % of host CPU**, observationally equivalent.
The ablation *includes* the cost of the on-demand parse attempt on the
690 201 irreducible ops (they bail at the first `.`), and still nets +6.2 % —
that added cost is in the noise.

### 4.3 Op-count weighting (leg 3) and convergence

Weighting the per-op gprof self-times by the FRAGE-2 counts gives, *without
reference to the ablation*, fixable ≈ 39 % / irreducible ≈ 61 % of the op
half (560 k cheaper addsub ops + their parse/format/alloc share, vs 420 k
decimal addsub + the 270 k decimal/exponent MUL, ~2.7× costlier per op).

The convergence is that **the op half measures 16 % on two independent
bases**:

- **gprof** (self-time basis): `IRXARIOP` cumulative = **16.0 %**;
- **op-count + ablation** (cpu_user basis): fixable is independently 39 % of
  the op half (op-count weighting) *and* independently 0.194 s (ablation);
  0.194 s ÷ 0.39 ≈ 0.497 s = **16.0 % of the 3.097 s cpu_user**.

Two methods, two bases, same 16 % — that agreement is what licenses reading
the irreducible part as a subtraction: **fixable = 6.2 % of host CPU
(reclaimable, directly ablated); irreducible = 16 % − 6.2 % ≈ 9.8 % of host
CPU** (decimal, not reachable by an integer fast-path). The 9.8 % is a
cross-checked subtraction, *not* a direct measurement — it holds only because
the two 16 %s coincide.

### 4.4 Character & Hercules multiplier

The fixable win removes both **CPU** work (the `num_from_str` digit-walk and
`num_addsub`) and **memory-bound** work (`num_alloc`/`num_free`/`irxstor`
churn — 2 scratch BCD numbers allocated+freed per fallback). Per CON-12 /
WP-PERF-PROFILE the memory part lands ×3–5 on MVS, so **host 6.2 % is a lower
bound on the MVS payoff**. Measure on MVS against the 14 280 cps baseline
(host cps is only a localiser), as OC-09/OC-12 did.

---

## 5. Recommendation

**Build the op-path integer fix (separate ticket). Decline the decimal
fast-path.**

### 5.1 Do: integer op-path fast-path (the op-path analog of OC-12)

Two equivalent shapes; **(a) is recommended** (it is exactly the verified
ablation):

- **(a) Parse-on-demand in `try_arith_fast`** — when an operand is not
  `LINTEGER`, attempt a plain-integer parse of its `.str` (the
  `try_parse_int_cache` ≤ 9-digit window); on success use it, else fall to
  BCD as today. *Simplest*, touches one function, catches **both** fixable
  constructs (including the first `acompound+1` and every `length(j)-1`),
  and is byte-equivalent in this run.
- **(b) Carry `int_cache` forward** across `irx_arith_op` integer results and
  numeric-BIF results. Avoids the re-parse but touches more sites (the BCD
  result path at `irx#bvm.c:1527`, the BIF return path, possibly vpool).
  Higher coverage of repeated chains, more surface area.

**Payoff:** ~6.2 % host CPU; more on MVS (memory-bound component). **Risk:
low** — same risk class as the accepted OC-12 (#180): plain int32 arithmetic
behind the existing overflow guards, output verified observationally
equivalent (SC28-1883-0 normalises `"007"+0 → "7"`, which the fast path
already produces via `i32toa`). The fuzz/DIGITS reasoning OC-12 documented at
`irx#bvm.c:1569-1593` applies unchanged (parse does not round; bytecode
forces token-walk fallback under NUMERIC FUZZ).

### 5.2 Don't: decimal fast-path for the irreducible 9.8 %

A packed-decimal / OC-02-style fast path is the only thing that could touch
the irreducible 690 201 ops, but:

- **High effort + high correctness risk** — decimal add/mul with NUMERIC
  DIGITS, rounding boundaries, signed zero, exponent normalisation per
  SC28-1883-0 §9.3. Far beyond the int32 guard envelope.
- **It would not even cover the biggest irreducible cost** — the
  `avar.1.2*1.1` accumulator grows into **exponent form** (269 758 of the
  270 000 MULs have an exponent operand); a packed-*integer* path misses it.
- **Partly a benchmark artifact** — the decimal DO-loop step
  `do j=1.1 to 2.2 by 1.1` (280 k ADDs) and the ×1.1 accumulator are
  REXXCPS's deliberate decimal stress. Real-world REXX leans on **integer**
  loops/counters/indexing, which already take the fast path. So in typical
  code the *fixable* share is likely **higher** than REXXCPS's 45 %, and the
  decimal path's value is **lower** than its 9.8 % here suggests.

### 5.3 Escalation check (both conditions NOT met)

- *"Op-path smaller than the committed cluster-A share"* — **no**: the op half
  is a genuine 16 % post-OC-12 (the OC-09/OC-06 profile-share trap does not
  recur here).
- *"Overwhelmingly irreducible decimal, no worthwhile fast-path"* — **no**:
  44.8 % of fallbacks are fixable integers worth 6.2 % host (more on MVS).

So this is a **bounded yes** on the integer fix, **no** on the decimal path —
distinct from OC-06's clean negative and from an unqualified win.

---

## 6. Reproduction

```sh
git checkout wp-bc-oc-arith-diag      # HEAD 5fc0239

# 1. ceiling + leaf self-times (committed pipeline, no instrumentation)
REXX370_BCDEBUG=1 ./scripts/host-profile.sh --source=test/rexxcps.rexx
grep -E "IRXARIOP|i32toa|try_arith_fast|num_addsub|num_mul" \
     build/host-profile/profile.txt

# 2. FRAGE 1/2 counters + FRAGE 3 ablation (local copy, NOT committed)
#    Appendix A lists the exact edits to a /tmp copy of src/irx#bvm.c.
#    counters:  build instrumented bvm  -> run -> arith-counters.txt
#    ablation:  build clean baseline and -DDIAG_SIMFIX, 6 interleaved runs,
#               compare cpu_user (driver stderr).
```

Prerequisites: `gcc`, `gprof`, `../lstring370` as a sibling clone.

---

## 7. What was NOT done (scope guard)

- **No product-code change.** All instrumentation/ablation lived in
  `/tmp/ocaridiag`; the repo tree stayed clean. The fix is a separate ticket.
- Hit rate, operand classes, and the time split are **measured**, not guessed
  — every fallback count maps 1:1 to a named source construct (§3).
- Three independent methods (op-counters, gprof, ablation) converge on the
  16 % / 6.2 % / 9.8 % split.

---

## Appendix A — instrumentation map (local copy only)

A `/tmp` copy of `src/irx#bvm.c`; everything else compiled from the real
tree.

| location | added (counters build) |
|---|---|
| after includes | `d_classify()` (mirrors `try_parse_int_cache`), per-op counter arrays, `__attribute__((destructor))` dump → `arith-counters.txt` |
| `try_arith_fast` entry | `d_fast_attempt[op]++` |
| `try_arith_fast` guard-fail | classify `a`,`b`; `d_fail`, `d_faila`, `d_failb`, `d_pair`++ |
| `try_arith_fast` success | `d_fast_success[op]++` (overflow bail derived = attempt − fail − success) |
| `OP_ADD…OP_POW` dispatch | `d_disp[op]++` |
| `OP_NEG` | `d_neg++` |

| location | added (ablation build, `-DDIAG_SIMFIX`) |
|---|---|
| before `try_arith_fast` | `d_simfix_int()` — int value if cached or `.str` is a plain integer ≤ 9 dig |
| `try_arith_fast` guard | replace the `LINTEGER` check with `d_simfix_int(a)&&d_simfix_int(b)` |

The counter and ablation paths are mutually exclusive builds; the ablation
build carries no counters so its cpu_user is clean.
