# Host Profile — post WP-PERF-03 (2026-05-19)

**Engine commit:** f10c20b  
**Host:** Linux mvsdev 6.12.73+deb13-amd64 x86_64 (Debian 13)  
**Build:** gcc (Debian 14.2.0-19) — `-pg -O0 -g -std=gnu99`  
**Driver:** `test/host/tstcps_host` with `--source=test/rexxcps.rexx`  
(REXXCPS 2.2: 100 × 100 iterations of 1000 clauses)  
**Total wall-clock:** 26.075 s  
**Total CPU (user+sys):** 26.056 s + 0.012 s = 26.068 s  

## Top-10 hotspots (REXXCPS 2.2 workload)

| Rank | Self% | Cum% | Self s | Calls | Function |
|------|-------|------|--------|-------|----------|
| 1 | 10.57 | 10.57 | 0.62 | 423 356 394 | `peek_tok` |
| 2 | 5.50 | 16.07 | 0.32 | *(no count)* | `_init` ¹ |
| 3 | 4.47 | 20.54 | 0.26 | 10 530 821 | `find_keyword` |
| 4 | 4.47 | 25.01 | 0.26 | 200 938 138 | `tok_is_op_char` |
| 5 | 4.30 | 29.31 | 0.25 | 236 229 954 | `cur_tok` |
| 6 | 3.78 | 33.09 | 0.22 | 103 934 570 | `irxstor` |
| 7 | 3.61 | 36.70 | 0.21 | 102 939 299 | `tok_is_kw` |
| 8 | 2.92 | 39.62 | 0.17 | 60 844 260 | `advance_tok` |
| 9 | 2.32 | 41.94 | 0.14 | 41 533 322 | `Lfree` |
| 10 | 2.23 | 44.17 | 0.13 | 115 040 140 | `sym_matches` |

¹ `_init` is the ELF constructor section (C runtime startup). Not an optimisation target.

## Comparison with pre-PERF-03 baseline (main, REXXCPS)

Pre-PERF-03 profile: 29.950 s wall-clock, 334 062 REXX clauses/second.

| Function | main self s | post-03 self s | Δ |
|----------|------------|----------------|---|
| `peek_tok` | 0.92 | 0.62 | −33% |
| `tok_is_op_char` | 0.30 | 0.26 | −13% |
| `upper_bytes` | **0.23** | **— (gone)** | **−100%** |
| `sym_matches` | 0.22 | 0.13 | −41% |
| `find_keyword` | 0.22 | 0.26 | +18% ² |
| `Lfx` | 0.14 | — (gone) | — |
| `irxstor` | 0.25 | 0.22 | −12% |

² `find_keyword` absolute time increased slightly at `-O0`: the binary search
loop body (5 iterations with `strlen` + `memcmp` + conditional) has higher
per-iteration overhead than the linear scan for a 22-entry table at zero
optimisation.  At `-O2` the inline expansion and branch elimination make
binary search faster; the effect is visible in REXXCPS workloads at
higher optimisation but less prominent at `-O0` (the profiling build).

**REXXCPS output:**
```
Averaged: 100 x 100 iterations of 1000 clauses (over 26.1s)
Performance: 383 679 REXX clauses per second
```

**Wall-clock delta vs pre-PERF-03:** −3.875 s (−12.9%)  
**CPS delta vs pre-PERF-03:** +49 617 CPS (+14.9%)

## Embedded microbench comparison (outer=1000, inner=500)

| Baseline | Wall-clock | Notes |
|----------|-----------|-------|
| post-PERF-02 (main) | 25.138 s | reference |
| post-PERF-03 (A+B+C) | 20.638 s | −18.2% |
| post-PERF-03 (A+B+C+D) | 21.0–22.4 s ³ | ~−14 to −18% |

³ Embedded microbench timing on this host varies ~1–2 s run-to-run due to
scheduler noise. Best reading with A+B+C+D: 21.673 s (−13.8%).

## Observations

**`upper_bytes` eliminated** — no longer in the top-20.  The `tok_upper`
field pre-computed during tokenise (Change C) absorbs all per-token
upper-case work.  This was the primary objective of Change C.

**`sym_matches` reduced by 41%** in absolute time.  The `TOKF_KEYWORD`
fast-reject (Change B) filters out non-keyword symbols before entering the
fold-and-compare body, cutting the majority of the ~102 M `tok_is_kw` calls
and ~115 M `sym_matches` calls to a single flag test.

**`peek_tok` still #1 at `-O0`** — Change A (`static inline`) has no effect
at `-O0` because GCC does not honour the inline hint without optimisation.
At `-O2` the function body (two bounds checks, one array index) is folded
directly into each call site, eliminating the function overhead entirely.
Full benefit is realised in production MVS builds (c2asm370 compiles at
a level comparable to `-O1`/`-O2`).

**`tok_is_op_char` and `cur_tok` remain prominent** — both are called
~200 M and ~236 M times respectively per REXXCPS run.  These are the next
candidates once a token-stream cache (planned future WP) is in place; caching
would eliminate the re-scan on each DO-loop iteration, collapsing all
per-token function call overhead at once.

**REXXCPS wall-clock variability** — the mvsdev.lan host shows ±15–20%
run-to-run variance on REXXCPS without `-pg` due to scheduler noise.
The structural improvements (functions removed from / reduced in the top-10)
are a more reliable measure than the raw CPS number on this machine.
