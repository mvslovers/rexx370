# Host Profile — post WP-PERF-02 (2026-05-18)

**Engine commit:** da8fd0d  
**Host:** Linux mvsdev 6.12.73+deb13-amd64 x86_64 (Debian 13)  
**Build:** gcc (Debian 14.2.0-19) — `-pg -O0 -g -std=gnu99`  
**Driver:** `test/host/tstcps_host` with embedded microbench (outer=1000, inner=500)  
**Total wall-clock:** 25.138 s  
**Total CPU (user+sys):** 25.023 s + 0.084 s = 25.107 s  

## Top-10 hotspots

| Rank | Self% | Cum% | Self s | Calls | Function |
|------|-------|------|--------|-------|----------|
| 1 | 11.89 | 11.89 | 0.71 | 304 062 472 | `peek_tok` |
| 2 | 7.54 | 19.43 | 0.45 | 175 523 266 | `tok_is_op_char` |
| 3 | 5.53 | 24.96 | 0.33 | *(no count)* | `_init` ¹ |
| 4 | 4.52 | 29.48 | 0.27 | 115 000 000 | `ebcdic_eq` |
| 5 | 4.36 | 33.84 | 0.26 | 500 000 | `num_div_impl` |
| 6 | 4.02 | 37.86 | 0.24 | 108 014 350 | `irxstor` |
| 7 | 3.02 | 40.88 | 0.18 | 15 003 023 | `upper_bytes` |
| 8 | 2.68 | 43.56 | 0.16 | 2 000 000 | `parse_function_call` |
| 9 | 2.51 | 46.07 | 0.15 | 2 000 000 | `IRXBIFFN` |
| 10 | 2.35 | 48.42 | 0.14 | 13 002 011 | `find_in_bucket` |

¹ `_init` is the ELF constructor section (C runtime / lstring startup), not a REXX
engine function. Its cost is a one-time startup expense and is not an optimisation target.

## Observations

`find_in_bucket` dropped from **#1 at 37.42%** (3.45 s) to **#10 at 2.35%** (0.14 s)
after the vp_primes[] extension to 574 021 buckets.  With 500 k unique compound
variables the table now grows to a capacity where chains are near-length-1, so the
previously dominant chain-walk is almost always a single pointer comparison.  The
call count (13 M) is unchanged — every variable read/write still invokes
`find_in_bucket` — but each call is now trivially fast.

The new profile is well-distributed: the top function (`peek_tok` at 11.89%) is the
next single target worth addressing.  It is called 304 M times because the tokenizer
re-scans the source on every iteration of the DO loop body; a token-stream cache
(WP future) would eliminate this cost class entirely.
