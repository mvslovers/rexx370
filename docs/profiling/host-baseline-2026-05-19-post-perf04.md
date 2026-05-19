# Profile — post WP-PERF-04 (2026-05-19)

**Engine commit:** (wp-perf-04-allocator-pool)  
**Host:** Linux 6.6.114.1-microsoft-standard-WSL2 (Ubuntu 24.04)  
**Build:** gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) — `-pg -O0 -g -std=gnu99`  
**Drivers:** embedded microbench (outer=1000, inner=500) + `--source=test/rexxcps.rexx`  

All measurements are same-machine A/B: `main` tip (5d2f321) vs branch tip,
same binary build flags, same host.

---

## Embedded microbench — top-10 hotspots

**Post-PERF-04 wall-clock:** 19.739 s  
**Baseline (main) wall-clock:** 21.036 s  
**Delta:** −1.297 s (−6.2%)

| Rank | Self% | Cum% | Self s | Calls | Function |
|------|-------|------|--------|-------|----------|
| 1 | 14.21 | 14.21 | 0.79 | 304 062 472 | `peek_tok` |
| 2 | 6.29 | 20.50 | 0.35 | 13 002 011 | `find_in_bucket` |
| 3 | 6.29 | 26.79 | 0.35 | 500 000 | `num_div_impl` |
| 4 | 3.96 | 30.75 | 0.22 | 2 000 000 | `parse_function_call` |
| 5 | 3.60 | 34.35 | 0.20 | 175 523 266 | `tok_is_op_char` |
| 6 | 3.60 | 37.95 | 0.20 | 145 541 228 | `cur_tok` |
| 7 | 3.24 | 41.19 | 0.22 | 115 000 000 | `ebcdic_eq` |
| 8 | 3.06 | 44.25 | 0.17 | 33 005 095 | `rexx_lstr_alloc` |
| 9 | 2.70 | 46.95 | 0.15 | 33 005 095 | `rexx_lstr_dealloc` |
| 10 | 2.52 | 49.47 | 0.14 | 6 000 000 | `num_from_str` |

## REXXCPS 2.2 — top-10 hotspots

**Post-PERF-04 wall-clock:** 25.913 s  
**Baseline (main) wall-clock:** 27.381 s  
**Delta:** −1.468 s (−5.4%)

| Rank | Self% | Cum% | Self s | Calls | Function |
|------|-------|------|--------|-------|----------|
| 1 | 15.46 | 15.46 | 0.98 | 423 356 394 | `peek_tok` |
| 2 | 3.61 | 19.07 | 0.23 | 42 826 156 | `rexx_lstr_alloc` |
| 3 | 3.30 | 22.37 | 0.21 | 236 229 954 | `cur_tok` |
| 4 | 3.14 | 25.51 | 0.20 | 42 826 156 | `rexx_lstr_dealloc` |
| 5 | 2.83 | 28.34 | 0.18 | 10 530 821 | `find_keyword` |
| 6 | 2.75 | 31.09 | 0.17 | 115 040 140 | `sym_matches` |
| 7 | 2.67 | 33.76 | 0.17 | 31 882 468 | `tok_ends_clause` |
| 8 | 2.67 | 36.43 | 0.17 | 13 881 194 | `hash_bytes` |
| 9 | 2.67 | 39.10 | 0.17 | 1 390 203 | `parse_function_call` |
| 10 | 2.51 | 41.61 | 0.16 | 60 844 260 | `advance_tok` |

`irxstor` dropped from rank 6 (3.91%) to rank ~20 (0.78%); not visible in the top-10.

Note: REXXCPS 2.2 uses `TIME('R')` internally to report CPS. The host-side
`TIME()` BIF does not return a valid elapsed time on this non-MVS platform
(returns an overflow value), so the in-script CPS figure is invalid. The
wall-clock delta above is the reliable measure.

---

## Storage-path A/B — REXXCPS workload

| Function | main self s | post-04 self s | Δ s | Δ% |
|----------|------------|----------------|-----|-----|
| `irxstor` | 0.25 (3.91%) | 0.05 (0.78%) | −0.20 | −80% |
| `Lfree` | 0.20 (3.05%) | 0.12 (1.88%) | −0.08 | −40% |
| `Lfx` | 0.15 (2.35%) | 0.11 (1.73%) | −0.04 | −27% |
| `rexx_lstr_alloc` | 0.07 (1.17%) | 0.23 (3.61%) | +0.16 | pool wrapper |
| `rexx_lstr_dealloc` | 0.08 (1.25%) | 0.20 (3.14%) | +0.12 | pool wrapper |
| `rexx_lstr_alloc_raw` | — | 0.00 (0.00%) | — | pool-miss only |
| `rexx_lstr_dealloc_raw` | — | 0.01 (0.16%) | — | pool-miss only |
| `POOL_BUCKET_FOR` macro | — | — (inlined) | — | macro expansion |

### Pool effectiveness — REXXCPS

| Metric | Value |
|--------|-------|
| `rexx_lstr_alloc` calls | 42 826 156 |
| `rexx_lstr_alloc_raw` calls (pool miss) | 1 400 390 |
| Pool hit rate (alloc) | **96.7%** |
| `irxstor` calls — main | 103 934 570 |
| `irxstor` calls — post-04 | 21 083 038 |
| `irxstor` call reduction | **−80%** |

### Pool effectiveness — embedded microbench

| Metric | Value |
|--------|-------|
| `rexx_lstr_alloc` calls | 33 005 095 |
| `rexx_lstr_alloc_raw` calls (pool miss) | 2 500 074 |
| Pool hit rate (alloc) | **92.4%** |
| `irxstor` calls — main | 104 010 338 |
| `irxstor` calls — post-04 | 43 000 296 |
| `irxstor` call reduction | **−59%** |

---

## POOL_BUCKET_FOR macro

The original implementation used a `static int pool_bucket_for(int size)`
function with a 4-iteration linear scan.  At `-O0` this was a non-inlined
call at every alloc and dealloc site (66 M calls per microbench run),
consuming all savings.

Replacing it with a `#define POOL_BUCKET_FOR(size_, bkt_)` macro that
expands the 4-case switch directly into each call site eliminates the
function-call overhead on both the host (`-O0`) and MVS (c2asm370), without
relying on `__attribute__((always_inline))` which is not portable across all
c2asm370 builds.

---

## Observations

**`irxstor` drops from 3.91% to 0.78% under REXXCPS** — removed from the
top-10. The 80% reduction in call count (103 M → 21 M) reflects the REXXCPS
workload's tighter string reuse patterns (higher pool hit rate 96.7% vs
92.4% in the microbench).

**`rexx_lstr_alloc` and `rexx_lstr_dealloc` enter the top-10** at 3.61%
and 3.14%. This is expected: the pool check (wkbi deref + switch + count
check) now has non-trivial cost that was previously absorbed by the more
expensive `irxstor` call. The net storage-path time is lower than before.

**Wall-clock −5.4% REXXCPS, −6.2% microbench** on WSL2 at `-O0`. At `-O0`
on a host with fast libc malloc, the pool wrapper overhead is proportionally
larger than on the MVS target where each irxstor call is a GETMAIN/FREEMAIN
SVC (supervisor call overhead). See the MVS A/B section for the on-target
result (+32.7% CPS).

**WSL2 scheduler variance** — the WSL2 host shows ±10–15% run-to-run
variance. The same-machine A/B controls for hardware speed; the structural
metrics (irxstor call count, pool hit rate) are the reliable measures.

---

## MVS A/B — REXXCPS 2.2

**Target:** MVS 3.8j on Hercules (TK5)  
**Build:** c2asm370 (GCC 3.2.3) — `-O0`  
**Driver:** REXXCPS 2.2 — 5 × 5 iterations of 1000 clauses

| Branch | CPS | Elapsed | Delta |
|--------|-----|---------|-------|
| main (WP-PERF-03) | 5 710 | 4.4 s | — |
| wp-perf-04 | 7 580 | 3.3 s | **+32.7%** |

The larger gain on MVS vs. WSL2 (32.7% vs. 5.4%) is expected: on MVS
each `irxstor` call is a GETMAIN/FREEMAIN SVC. Supervisor call overhead
is orders of magnitude higher than a `libc malloc`, so the 80% reduction
in `irxstor` calls from the pool carries far greater weight on the target
platform.

---

## Next performance candidates

`find_in_bucket` (variable pool collision walk) moved to rank 2 at 6.29%
in the microbench. With `irxstor` cleared from the top-5, vpool lookup is
now the dominant allocation-related cost.

`num_div_impl` at rank 3 (6.29%, 500 k calls) has high per-call cost
(0.70 µs). Division is the slowest path in the arithmetic engine.

`peek_tok` remains rank 1 at 14–15% — awaiting a token-stream cache WP
that would collapse per-token re-scan overhead entirely.
