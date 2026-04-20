# TSK-148 — c2asm370 -O0 vs -O1 Parity Run

Empirical verification that the rexx370 test surface produces identical
observable behaviour at c2asm370's `-O0` and `-O1` optimization levels
on Hercules MVS 3.8j. PR #38 (Phase D conversion BIFs) and PR #39
(Phase E reflection BIFs) are the primary scope; the full test suite
is included as a free-bonus check.

**Verdict: 0 codegen issues, 0 rexx370 defects, all tests clean at
both optimization levels.**

## Method

Build infrastructure: `mbt/scripts/mvsasm.py` hard-codes `c2asm370 -S
-O1` on the cross-compile command line. To swap to `-O0` without
patching mbt, append `-O0` to `[build].cflags` in `project.toml`; gcc
3.2.3 honours last-`-O`-wins, so the line becomes effectively
`c2asm370 -S -O1 ... -O0 ...` and produces `-O0` output. Verified
empirically by diffing one generated `.s` file before and after:

| File | -O1 size | -O0 size | byte-identical? |
|---|---|---|---|
| `src/irx#bif.s` | 1070 lines | 1319 lines | no — `-O0` is 23% larger |

A second cross-check: rebuilding with `-O0` removed produced a `.s`
file byte-identical to the original `-O1` reference — confirming the
revert was clean and no stale state leaked between phases.

Stamp invalidation: `make clean` removes `.mbt/stamps/` and the
generated `.s` files in `src/`, so each phase started from a true
clean slate. Stamps are keyed on source-file hash, not on the
compile command, so without `make clean` between phases mbt would
silently reuse the previous phase's `.s` files. Verified by
inspecting `mbt/mk/targets.mk` and `mbt/scripts/mvsasm.py`.

Each phase ran end-to-end:

1. `make clean`
2. `make build` — cross-compile + ASM + NCAL link, all 34 modules RC=0.
3. `make link` — final IEWL for 17 test load modules. (IRXJCL fails
   with RC=8 / IEW0342 in both phases — IRXJCL CSECT not yet
   implemented, see project.toml comment; out of scope.)
4. `zowe jobs submit local-file test/mvs/tstall.jcl --wait-for-output`,
   capturing the full spool to `/tmp/jobout-O{0,1}.json`.
5. Per-test parsing via `parse_tstall.py`, grouping by the
   `--- Phase X: ... ---` headers in TBIFS / BBIFS SYSPRINT.

## Results — step level

All 32 test steps pass at both optimization levels with identical
counts. The skipped counts in BPHAS1 and BANRM are the four/seven
TSO-only assertions gated in PR #50.

| Step | -O0 pass | -O1 pass | -O0 fail | -O1 fail | -O0 skip | -O1 skip |
|---|---:|---:|---:|---:|---:|---:|
| BANCH  | 1   | 1   | 0 | 0 | 0 | 0 |
| BANRM  | 17  | 17  | 0 | 0 | 7 | 7 |
| BAREXT | 113 | 113 | 0 | 0 | 0 | 0 |
| BARIT  | 128 | 128 | 0 | 0 | 0 | 0 |
| BBIF   | 29  | 29  | 0 | 0 | 0 | 0 |
| BBIFS  | 417 | 417 | 0 | 0 | 0 | 0 |
| BCTRL  | 62  | 62  | 0 | 0 | 0 | 0 |
| BHELO  | 16  | 16  | 0 | 0 | 0 | 0 |
| BLSTR  | 50  | 50  | 0 | 0 | 0 | 0 |
| BPARSE | 74  | 74  | 0 | 0 | 0 | 0 |
| BPHAS1 | 34  | 34  | 0 | 0 | 4 | 4 |
| BPROC  | 53  | 53  | 0 | 0 | 0 | 0 |
| BPRSR  | 39  | 39  | 0 | 0 | 0 | 0 |
| BSAY   | 27  | 27  | 0 | 0 | 0 | 0 |
| BTOKN  | 70  | 70  | 0 | 0 | 0 | 0 |
| BVPOL  | 47  | 47  | 0 | 0 | 0 | 0 |
| TANCH  | 1   | 1   | 0 | 0 | 0 | 0 |
| TANRM  | 24  | 24  | 0 | 0 | 0 | 0 |
| TAREXT | 113 | 113 | 0 | 0 | 0 | 0 |
| TARIT  | 128 | 128 | 0 | 0 | 0 | 0 |
| TBIF   | 29  | 29  | 0 | 0 | 0 | 0 |
| TBIFS  | 417 | 417 | 0 | 0 | 0 | 0 |
| TCTRL  | 62  | 62  | 0 | 0 | 0 | 0 |
| THELO  | 16  | 16  | 0 | 0 | 0 | 0 |
| TLSTR  | 50  | 50  | 0 | 0 | 0 | 0 |
| TPARSE | 74  | 74  | 0 | 0 | 0 | 0 |
| TPHAS1 | 38  | 38  | 0 | 0 | 0 | 0 |
| TPROC  | 53  | 53  | 0 | 0 | 0 | 0 |
| TPRSR  | 39  | 39  | 0 | 0 | 0 | 0 |
| TSAY   | 27  | 27  | 0 | 0 | 0 | 0 |
| TTOKN  | 70  | 70  | 0 | 0 | 0 | 0 |
| TVPOL  | 47  | 47  | 0 | 0 | 0 | 0 |

## Results — TBIFS / BBIFS phase breakdown

Phase D (PR #38 — conversion BIFs) and Phase E (PR #39 — reflection
BIFs) are the primary parity targets. Counts identical at both
optimization levels in both TSO and batch modes:

| Phase group | TBIFS -O0 | TBIFS -O1 | BBIFS -O0 | BBIFS -O1 |
|---|---:|---:|---:|---:|
| Phase B: substring & position                    | 19 | 19 | 19 | 19 |
| Phase C: word operations                         | 13 | 13 | 13 | 13 |
| Phase C: numeric BIFs                            | 42 | 42 | 42 | 42 |
| Phase C: boundary and edge cases                 | 39 | 39 | 39 | 39 |
| Phase D: padding & formatting                    | 18 | 18 | 18 | 18 |
| **Phase D: C2D / X2D**                           | **39** | **39** | **39** | **39** |
| **Phase D: D2C / D2X**                           | **65** | **65** | **65** | **65** |
| **Phase D: error paths**                         | **18** | **18** | **18** | **18** |
| Phase E: insert / delete / overlay               | 12 | 12 | 12 | 12 |
| **Phase E: DATATYPE**                            | **28** | **28** | **28** | **28** |
| **Phase E: SYMBOL**                              | **9**  | **9**  | **9**  | **9**  |
| **Phase E: DIGITS / FUZZ / FORM**                | **7**  | **7**  | **7**  | **7**  |
| **Phase E: error paths**                         | **2**  | **2**  | **2**  | **2**  |
| Phase F: translate / verify / compare / abbrev   | 22 | 22 | 22 | 22 |
| Phase F: ERRORTEXT                               | 14 | 14 | 14 | 14 |
| Phase F: USERID                                  | 1  | 1  | 1  | 1  |
| Phase F: EXTERNALS + LINESIZE stubs              | 2  | 2  | 2  | 2  |
| Phase F: VALUE                                   | 15 | 15 | 15 | 15 |
| Phase F: SOURCELINE                              | 52 | 52 | 52 | 52 |

All zero failures everywhere.

**Phase D total: 140 pass per step. Phase E total: 46 pass per step.**

## Hotspot spot-check

Per the should-have brief, eyeballed the `-O1`-generated assembler for
the four Phase-D hotspots called out in the original ticket:

- **`bcd_mul256_add`** (`src/irx#bifs.s` lines 4635–4727). Outer loop
  bounded by the byte-count field, BCD multiply via `MH =H'256'` plus
  modulo-10 via `SRDA` + `DR`, MEMMOVE call for buffer extension uses
  the standard prologue/epilogue calling convention. Carry chain is
  preserved across the digit propagation. No reordered stores, no
  dead code, no missing instructions.
- **`bcd_to_bytes_lsb`** (lines 4730–5008). Walks bytes LSB-first,
  packs nibbles per the documented algorithm. Single-pass, no
  reload-after-store hazards visible.
- **`twos_complement_bytes`** (lines 5009–5068). Two-phase: invert
  every byte (`X 2,=F'-1'` + `STC`), then LSB-first carry propagation.
  Final `NI 0(3),15` masks the high nibble to preserve the BCD
  upper-nibble convention. Tight, no surprises.
- **`hex_val`** / **`hex_char`** (lines 3988–4105). Standard
  table-lookup style, EBCDIC-correct character ranges.

No "worth watching" items surfaced.

Phase E (reflection BIFs, PR #39) hotspots — `bif_datatype`,
`bif_symbol`, `bif_digits` / `bif_fuzz` / `bif_form` — were not
spot-checked individually because the empirical pass counts (46/46
at both levels) provide direct evidence of behavioural parity.

## Build infra changes (PR-internal)

`-O0` produced significantly larger object code than `-O1` and hit
several MVS dataset capacity limits during the parity build that
`-O1` had never tripped. The bumps were applied to `project.toml`
and verified to handle both optimization levels. Same pattern as
PR #50's LOAD enlargement.

| Dataset | Before | After | Why |
|---|---|---|---|
| OBJECT | TRK 30,10 / DIRBLK 30  | TRK 200,50 / DIRBLK 60  | -O0 SYSPUNCH .o files larger; SE37 at module #21 |
| NCALIB | TRK 30,10 / DIRBLK 30  | TRK 300,100 / DIRBLK 60 | -O0 NCAL modules larger; SE37 at LNK24 |
| LOAD   | TRK 600,100 / DIRBLK 60 | (unchanged from PR #50) | already sufficient at -O0 |

These are permanent infrastructure improvements that benefit any future
build at any optimization level — not -O0-specific workarounds.

## Conclusions

- **No c2asm370 codegen issues found.** Every PASS at -O0 is also a
  PASS at -O1, and vice versa. The 1180 host-side cross-compile tests
  plus the 1187 MVS tests (after PR #52's regression additions) all
  stay green across both optimization levels.
- **No latent rexx370 defects surfaced** (i.e. no fails-at-both that
  the cross-compile happened to miss). PR #52's SOURCELINE finding
  remains the only post-test-port rexx370 defect to date.
- **PR #38 and PR #39 ratified empirically** at both optimization
  levels. The Phase D conversion BIFs and Phase E reflection BIFs
  produce identical observable behaviour at -O0 and -O1.

## Closure note

Per the ticket: closure of TSK-148 itself is an architect call. This
report documents the empirical evidence; the disposition (close as
"clean", leave open for future re-checks, etc.) belongs to Mike.

## Files

- `/tmp/jobout-O0.json` — raw -O0 spool (456 KB)
- `/tmp/jobout-O1.json` — raw -O1 spool (457 KB)
- `/tmp/parity-report.txt` — parser output (157 lines)
- `/tmp/irx-bif-O0.s` / `/tmp/irx-bif-O1.s` — sample .s pair for
  the optimization-took-effect cross-check
