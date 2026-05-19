# Host Profile — post WP-PERF-04 (2026-05-19)

**Engine commit:** (wp-perf-04-allocator-pool)  
**Host:** Linux 6.6.114.1-microsoft-standard-WSL2 (Ubuntu 24.04)  
**Build:** gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) — `-pg -O0 -g -std=gnu99`  
**Driver:** `test/host/tstcps_host` with embedded microbench (outer=1000, inner=500)  
**Total wall-clock:** 19.739 s  
**Total CPU (user+sys):** 19.632 s + 0.096 s = 19.728 s  

## Top-10 hotspots (embedded microbench)

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

## A/B comparison — same machine, same driver invocation

The pre-PERF-04 baseline was run immediately before this profile on the same
host (WSL2, gcc 13.3.0) from the `main` branch tip (commit 5d2f321).

| Function | main self s | post-04 self s | Δ s | Δ% |
|----------|------------|----------------|-----|-----|
| `irxstor` | 0.23 (4.54%) | 0.11 (1.98%) | −0.12 | −52% |
| `Lfx` | 0.16 (3.23%) | 0.08 (1.44%) | −0.08 | −50% |
| `Lfree` | 0.10 (2.12%) | 0.04 (0.72%) | −0.06 | −60% |
| `rexx_lstr_alloc` | 0.06 (1.21%) | 0.17 (3.06%) | +0.11 | pool wrapper |
| `rexx_lstr_dealloc` | 0.05 (1.01%) | 0.15 (2.70%) | +0.10 | pool wrapper |
| `rexx_lstr_alloc_raw` | — | 0.01 (0.09%) | — | pool-miss only |
| `rexx_lstr_dealloc_raw` | — | 0.01 (0.18%) | — | pool-miss only |
| `pool_bucket_for` | — | — (inlined) | — | `always_inline` |

**Baseline wall-clock:** 21.036 s  
**Post-PERF-04 wall-clock:** 19.739 s  
**Delta:** −1.297 s (−6.2%)

### Pool effectiveness

| Metric | Value |
|--------|-------|
| `rexx_lstr_alloc` calls | 33 005 095 |
| `rexx_lstr_alloc_raw` calls (pool miss) | 2 500 074 |
| Pool hit rate (alloc) | **92.4%** |
| `irxstor` calls pre-PERF-04 | 104 010 338 |
| `irxstor` calls post-PERF-04 | 43 000 296 |
| `irxstor` call reduction | **−59%** |

### `pool_bucket_for` inlining

The initial implementation used a 4-iteration linear scan (`for` loop).
At `-O0` this was a non-inlined function call at each of the 66 M hot
sites, consuming ~1.2% self-time and eating the irxstor savings.
Replacing the loop with a `switch` and adding
`__attribute__((always_inline))` forces the compiler to expand the
4-case dispatch inline, reducing the total storage-path overhead and
restoring the expected wall-clock delta.

## Observations

**`irxstor` drops from 4.54% to 1.98%** — the primary goal of WP-PERF-04.
The pool intercepts 92.4% of allocations, reducing irxstor calls from
104 M to 43 M. The 43 M remaining calls are non-lstring storage operations
(vpool buckets, arithmetic temporaries, BIF env allocations).

**`Lfx` and `Lfree` also drop substantially** (−50% and −60% in self-time)
because both functions spend most of their time in the allocator callback;
pool hits return without calling irxstor.

**New wrapper cost is visible but bounded** — `rexx_lstr_alloc` (pool check
+ possible pool-hit return) and `rexx_lstr_dealloc` (pool check + possible
pool deposit) now appear in the top-10 at 3.06% and 2.70% respectively. The
pool_bucket_for switch adds ~4 comparisons per call, inlined via
`__attribute__((always_inline))` to avoid function-call overhead.

**Wall-clock −6.2% on WSL2 at `-O0`** — this is the lower bound of the
expected improvement range. At `-O0` on a host with fast libc malloc, the
pool wrapper overhead is proportionally larger than on the MVS target where
each irxstor call translates to a GETMAIN/FREEMAIN SVC (expensive supervisor
call) while the pool lookup is just a few memory reads. MVS measurement is
AC9 (requires hardware access).

**Host profiling limitation** — the WSL2 scheduler introduces ±10–15% run-to-run
variance in absolute wall-clock times. The same-machine A/B comparison
controls for hardware speed but not for OS scheduler noise. Structural
metrics (irxstor call count −59%, pool hit rate 92.4%) are the reliable
measures; wall-clock delta is indicative.

## Next performance candidates

`find_in_bucket` (variable pool hash collision walk) rose to rank 2 at 6.29%,
up from a lower rank before. With irxstor cleared from the top-5,
vpool lookup is now the dominant allocation-related cost. The 13 M calls
indicate significant hash collision depth in the 1000×500 workload.

`num_div_impl` at rank 3 (6.29%, 500 k calls) is the arithmetic engine
division path. The high per-call cost (0.70 µs) suggests a future
WP-PERF-06 candidate once vpool is addressed.
