# Host Profiling — WP-PERF-01

This document describes how to profile the REXX/370 engine on a Linux
host using gprof, as established by WP-PERF-01.

---

## Prerequisites

| Tool | Package (Debian/Ubuntu) | Notes |
|------|------------------------|-------|
| `gcc` | `build-essential` | GNU C compiler. Apple clang on macOS is **not** supported — clang's `-pg` does not produce `gmon.out` on macOS. |
| `gprof` | `binutils` | GNU profiler. Part of binutils on every Linux distro. |
| `../lstring370` | — | Parallel clone of [mvslovers/lstring370](https://github.com/mvslovers/lstring370). Must be at `../lstring370` relative to the `rexx370` repo root. |

Install on Debian/Ubuntu:

```sh
sudo apt-get install build-essential binutils
```

Clone lstring370 as a sibling:

```sh
# from the parent directory of your rexx370 clone:
git clone https://github.com/mvslovers/lstring370
# result: ../lstring370/
```

---

## Quick start

```sh
./scripts/host-profile.sh
```

This runs the full pipeline:

1. Preflight checks (gcc, gprof, ../lstring370)
2. Build `test/host/tstcps_host` with `-pg -O0 -g -std=gnu99`
3. Run the driver (embedded microbench); capture stdout → `build/host-profile/run.log`, stderr timing → `build/host-profile/timing.txt`
4. Run `gprof` → `build/host-profile/profile.txt`
5. Extract top-10 → `build/host-profile/top-hotspots.txt`
6. Print summary to stdout (wall time, CPU time, top-3 hotspots)

All output lands in `build/host-profile/` which is gitignored.

### Flags

| Flag | Effect |
|------|--------|
| *(none)* | Full run with embedded microbench |
| `--source=PATH` | Run `PATH` as the REXX source instead of the embedded bench |
| `--summary` | Skip full `profile.txt`; still produce `top-hotspots.txt` and summary line |
| `--clean` | Remove `build/host-profile/` and exit |

### Compiler override

```sh
CC=gcc-14 GPROF=gprof ./scripts/host-profile.sh
```

---

## Interpreting the output

### `top-hotspots.txt`

Tab-separated: `rank | self% | cumulative% | self-seconds | calls | function`

- **`self%`** — percentage of total profiling time spent *inside* this function,
  excluding time in callees. This is the most useful column for identifying
  bottlenecks.
- **`cumulative%`** — percentage of time in this function *and* all functions it
  calls. High cumulative% on a top-level function (e.g. `irx_exec_run`) is
  expected and not diagnostic.
- **`calls`** — total call count over the run. High call count × non-trivial
  self% is the classic signal for per-call optimisation.
- **`function`** — C function name as seen by the linker. Inlined functions are
  not visible because we compile with `-O0`.

### Why `-O0` and not `-O2`?

At `-O2`, the compiler inlines small functions, vectorises loops, and hoists
subexpressions. gprof attributes time to the surviving (non-inlined) caller,
making it impossible to tell which callee was hot. `-O0` gives exact per-function
attribution at the cost of inflated absolute timings — use **relative percentages**,
not absolute seconds, when comparing results across machines or builds.

### Reading the call-graph section (`profile.txt`)

After the flat profile, gprof emits a call-graph showing caller → callee
relationships and the percentage of time each arc represents. This is useful for
tracing *why* a function is called frequently, but the flat profile is sufficient
for initial hotspot identification.

---

## Custom workloads

To profile against a specific REXX program:

```sh
./scripts/host-profile.sh --source=/path/to/program.rexx
```

The driver passes `--source=PATH` through to `tstcps_host`, which reads the file
and invokes the engine on its contents. SAY output is captured to `run.log` as
usual.

---

## Baseline snapshots

Committed snapshots live in `docs/profiling/`. Naming convention:

```
docs/profiling/host-baseline-YYYY-MM-DD-context.md
```

`context` is a short label that distinguishes the run (e.g. `initial`,
`after-vpool-opt`, `after-bif-cache`). Each snapshot captures:

- Engine commit hash
- Build flags
- Driver / workload description
- Total timing
- Top-10 hotspots table

See `docs/profiling/host-baseline-2026-05-18-initial.md` for the first snapshot.

To commit a new baseline after an optimisation:

```sh
./scripts/host-profile.sh
# copy top-hotspots.txt into a new docs/profiling/host-baseline-YYYY-MM-DD-label.md
# add brief observations
```

---

## Known limitations

- **Absolute times are not comparable across machines.** Always compare
  `self%` percentages, not raw seconds.
- **`-O0` inflates time for trivially inlinable helpers.** A function that
  would be inlined at `-O2` shows up as its own entry at `-O0`. This is a
  feature for profiling — you can see the call — but inflated call counts
  for small wrappers are expected.
- **macOS is not supported** for this profiling pipeline. Apple's toolchain
  dropped working gprof support. Run on a Linux host.
- **gprof uses statistical sampling** (typically 100 Hz). Functions with
  very short runtimes may have zero or noisy samples. The embedded microbench
  is sized to run 10–20 s to give enough samples across all hot paths.
- **Host timings do not reflect MVS performance.** The profiling identifies
  algorithmic bottlenecks (O(n²) operations, excessive allocation, redundant
  parsing) that apply on both platforms. Absolute speeds differ significantly
  due to architecture, cache sizes, and EBCDIC vs ASCII.
