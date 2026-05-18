# Host Profile Baseline — initial (2026-05-18)

**Engine commit:** 93884db  
**Host:** Linux mvsdev 6.12.73+deb13-amd64 x86_64 (Debian 13)  
**Build:** gcc (Debian 14.2.0-19) — `-pg -O0 -g -std=gnu99`  
**Driver:** `test/host/tstcps_host` with embedded microbench (outer=1000, inner=500)  
**Total wall-clock:** 27.841 s  
**Total CPU (user+sys):** 27.709 s + 0.128 s = 27.837 s  

## Top-10 hotspots

| Rank | Self% | Cum% | Self s | Calls | Function |
|------|-------|------|--------|-------|----------|
| 1 | 37.42 | 37.42 | 3.45 | 13 002 011 | `find_in_bucket` |
| 2 | 8.89 | 46.31 | 0.82 | 304 062 472 | `peek_tok` |
| 3 | 3.80 | 50.11 | 0.35 | 500 000 | `num_div_impl` |
| 4 | 3.04 | 53.15 | 0.28 | 175 523 266 | `tok_is_op_char` |
| 5 | 2.60 | 55.75 | 0.24 | 115 000 000 | `ebcdic_eq` |
| 6 | 2.39 | 58.14 | 0.22 | *(no count)* | `_init` ¹ |
| 7 | 2.28 | 60.42 | 0.21 | 108 014 342 | `irxstor` |
| 8 | 2.17 | 62.59 | 0.20 | 145 541 228 | `cur_tok` |
| 9 | 1.74 | 64.33 | 0.16 | 42 008 067 | `Lfx` |
| 10 | 1.74 | 66.07 | 0.16 | 2 000 000 | `parse_function_call` |

¹ `_init` is the ELF constructor section (C runtime / lstring startup), not a REXX
engine function. Its 2.39% is a one-time cost amortised across the run; it is not
an optimisation target.

## Initial observations

`find_in_bucket` at **37.42%** (13M calls, one per variable read/write) dominates
the profile and is the primary target for CON-12 variable-pool optimisation.
`peek_tok` at **8.89%** (304M calls) indicates that the tokenizer re-scans the
source on every pass through the DO loop body; an AST or token-stream cache would
eliminate this cost class entirely.
