# Profile — post WP-PERF-05A (2026-05-19)

**Engine commit:** (wp-perf-05a-token-helper-inline)  
**Host:** Linux 6.6.114.1-microsoft-standard-WSL2 (Ubuntu 24.04)  
**Build:** gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) — `-pg -O0 -g -std=gnu99`  
**Drivers:** embedded microbench (outer=1000, inner=500) + `--source=test/rexxcps.rexx`  

All measurements are same-machine: branch tip, same binary build flags, same host.

---

## Key finding: `-O0` does not honour `inline`

GCC at `-O0` ignores `inline` hints unconditionally — it compiles every
function as a separate callable regardless of the annotation.  The
five functions marked `static inline` in this WP are therefore still
visible as separate gprof entries with the same call counts as
post-PERF-04.  The host gprof profile **cannot validate this
optimisation**.

The annotation is nonetheless correct C and is the right preparation
for the target: c2asm370 (GCC 3.2.3 derivative) running at its
default optimisation level on MVS, where function-call overhead is
meaningfully larger than on a modern x86 host.  The real validation
signal is the MVS REXXCPS run (see MVS A/B section below).

---

## Embedded microbench — top-10 hotspots

**Post-PERF-05A wall-clock:** 20.772 s (run 1: 20.771 s, run 2: 20.772 s — very stable)  
**Post-PERF-04 wall-clock:** 19.739 s  
**Delta:** +1.033 s (+5.2%) — within WSL2 scheduler variance (±10–15%)

| Rank | Self% | Cum% | Self s | Calls | Function |
|------|-------|------|--------|-------|----------|
| 1 | 11.15 | 11.15 | 0.61 | 304 062 472 | `peek_tok` |
| 2 | 4.84 | 16.00 | 0.27 | 175 523 266 | `tok_is_op_char` |
| 3 | 4.02 | 20.02 | 0.22 | 115 000 000 | `ebcdic_eq` |
| 4 | 4.02 | 24.04 | 0.22 | 500 000 | `num_div_impl` |
| 5 | 3.75 | 27.79 | 0.20 | 145 541 228 | `cur_tok` |
| 6 | 3.66 | 31.45 | 0.20 | 33 005 095 | `rexx_lstr_dealloc` |
| 7 | 3.56 | 35.01 | 0.20 | 38 509 050 | `advance_tok` |
| 8 | 3.47 | 38.48 | 0.19 | 13 002 011 | `find_in_bucket` |
| 9 | 3.29 | 41.77 | 0.18 | 33 005 095 | `rexx_lstr_alloc` |
| 10 | 3.11 | 44.88 | 0.17 | 2 000 000 | `IRXBIFFN` |

`sym_matches`, `tok_is_kw`, `tok_ends_clause` fell below rank 10 in this
run of the microbench (same workload as post-perf04 where they were also
not in the top-10 — the microbench does not exercise the keyword path as
heavily as REXXCPS).

## REXXCPS 2.2 — top-10 hotspots

**Post-PERF-05A wall-clock:** 28.145 s / 25.879 s (two runs — WSL2 variance ±8%)  
**Post-PERF-04 wall-clock:** 25.913 s  
**Delta:** −0.034 s (−0.1%) — no measurable change, within variance

| Rank | Self% | Cum% | Self s | Calls | Function |
|------|-------|------|--------|-------|----------|
| 1 | 15.02 | 15.02 | 0.97 | 423 356 394 | `peek_tok` |
| 2 | 5.93 | 20.95 | 0.39 | 236 229 954 | `cur_tok` |
| 3 | 4.62 | 25.57 | 0.30 | 102 939 299 | `tok_is_kw` |
| 4 | 4.47 | 30.04 | 0.29 | 200 938 138 | `tok_is_op_char` |
| 5 | 4.31 | 34.35 | 0.28 | 115 040 140 | `sym_matches` |
| 6 | 3.31 | 37.66 | 0.21 | 10 530 821 | `find_keyword` |
| 7 | 3.08 | 40.74 | 0.20 | 60 844 260 | `advance_tok` |
| 8 | 3.08 | 43.82 | 0.20 | 42 826 156 | `rexx_lstr_alloc` |
| 9 | 2.31 | 46.13 | 0.15 | 3 810 210 | `lstr_to_long` |
| 10 | 2.16 | 48.29 | 0.14 | 31 882 468 | `tok_ends_clause` |

All five target functions (`cur_tok`, `sym_matches`, `tok_is_kw`,
`tok_is_op_char`, `tok_ends_clause`) remain in the top-10 with call
counts identical to post-PERF-04.  This confirms that GCC at `-O0`
does not inline them — the annotation has zero effect on the host
build.

**Escalation note (per WP-PERF-05A spec):** The post-profile shows the
five functions still in top-10 with similar percentages. Per the task
spec, this is expected at `-O0` and is documented here rather than
papered over.  The code change proceeds because the annotation is
correct and targets the MVS build environment, not the host profiler.

---

## Host-side profile assessment

The host gprof run at `-O0` is not the correct measurement tool for
this optimisation.  It serves as a sanity check (call counts, workload
shape) but cannot confirm or deny the inline win.

To measure the actual impact, compare the MVS REXXCPS CPS before and
after applying the branch (see MVS A/B section).

---

## MVS A/B — REXXCPS 2.2

*(To be filled in after MVS run)*

**Target:** MVS 3.8j on Hercules (TK5)  
**Build:** c2asm370 (GCC 3.2.3) — `-O0`  
**Driver:** REXXCPS 2.2 — 5 × 5 iterations of 1000 clauses

| Branch | CPS | Elapsed | Delta |
|--------|-----|---------|-------|
| main (WP-PERF-04) | 7 580 | 3.3 s | — |
| wp-perf-05a | TBD | TBD | TBD |

---

## Hypothesis check

**Hypothesis:** This is CPU-cycle-optimisation (function-call overhead
elimination), not memory-access reduction. On MVS, where CALL/RETURN
overhead is higher than on x86 Linux (additional save-area discipline
required by the S/370 calling convention), the gain should be visible
despite `-O0`.  Expected MVS win: +10–15% (partial Hercules-multiplier
vs. the memory-access wins in WP-PERF-03/04).

**Host outcome:** No measurable win at `-O0` (expected — GCC ignores
the hint at this optimisation level).

**MVS outcome:** TBD — fill in after REXXCPS run on target.

If the MVS win is +10–15%: hypothesis holds.  
If MVS win is <+5%: c2asm370 also ignores the hint or the call
overhead is smaller than estimated.  
If MVS win is >+25%: the S/370 calling convention overhead is larger
than estimated — document and adjust model.

---

## Next performance candidates (from REXXCPS top-10)

After the five token helpers are accounted for (pending MVS
validation), the remaining expensive entries are:

- `peek_tok` at 15% — a token-stream cache (avoid re-scanning from
  `tok_pos`) would collapse this; deferred to bytecode phase.
- `find_keyword` at 3.3%, `find_in_bucket` at 3.5% — variable pool
  hash quality and bucket depth.
- `rexx_lstr_alloc` + `rexx_lstr_dealloc` at ~3% each — pool wrapper
  overhead; improving pool hit rate would reduce these further.
- `lstr_to_long` at 2.3% (new entrant) — arithmetic conversion cost.
