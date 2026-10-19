# CLAUDE.md — rexx370 Project Instructions

Extends the ecosystem root `CLAUDE.md` (platform constraints, mbt, SMP,
git rules); this file holds only what is specific to rexx370.

## What is this project?

rexx370 is a from-scratch REXX language interpreter for MVS 3.8j,
interface-compatible with IBM TSO/E Version 2 REXX (SC28-1883-0).
It is **not** a port of BREXX/370 — it is a clean reimplementation
with reentrancy as a core design constraint. (Its product name is still
open; it is not "REXX/370", which is IBM's.)

The primary specification is the IBM manual SC28-1883-0 (TSO Extensions
Version 2 REXX Reference, December 1988). When in doubt about REXX
language behaviour, semantics, or control block layouts, consult this
manual. It is available at https://vtda.org (search for SC28-1883).

## Read when relevant

| Before you … | read |
|---|---|
| pick up any work, or close/change a work package | `internals/ROADMAP.md` (single source of truth, see below) |
| change interpreter structure, ENVBLOCK/anchor, control blocks | `internals/architecture.md` (also the Notion design page: https://www.notion.so/3283d9938787811ba3f4d3308b254cad) |
| touch the bytecode compiler or VM | `internals/bytecode-format.md` |
| write or reformat C code | `internals/code-style.md` |
| look for which source file holds what | `internals/source-files.md` (partial; `git ls-files src` is current) |
| compile a test by hand, or need the old per-test counts | `internals/testing.md` |
| implement a BIF or an MVS integration | `internals/brexx-reference.md` |
| measure or quote performance | `internals/profiling.md`, `internals/ROADMAP.md` performance history |
| work on the TSO integration (IKJEFT01, EXEC, `ZMG0002`) | `tso/README.md`, `tso/TODO_IKJCT437.md`, `tso/TODO_IKJEFT01.md` |

## Architecture

Key points:
- **ENVBLOCK** is the anchor for each Language Processor Environment
- **ECTENVBK** (offset +30 in the TSO ECT) anchors the current ENVBLOCK
  under IBM's TSOFL-conditional contract (CON-14, TSK-194/195): IRXINIT
  with TSOFL=1 overwrites the slot unconditionally, TSOFL=0 never touches
  it. IRXTERM of a TSOFL=1 env rolls the slot back to the most recent
  TSO-attached predecessor in IRXANCHR (or NULL), and only while the slot
  still points to the terminating env. The slot can be occupied before
  a program starts, e.g. by the env a TMP registers at logon, so
  do not assume NULL under TSO. In batch (no ECT reachable) the slot is
  never written and IRXINIT still succeeds locally. See
  `internals/architecture.md` §6.1 and `include/irxanchr.h`
- **IRXEXTE** (Vector of External Entry Points) holds all replaceable
  routine pointers — SAY, PULL, I/O, Host Command, etc.
- **irx_wkblk_int** (our internal Work Block) holds all per-environment
  interpreter runtime state (variables, stack, trace, numeric settings)
- All state is per-environment. Zero globals. This is non-negotiable.

## The cardinal rule: NO GLOBAL STATE

This is the single most important constraint. Every prior REXX
interpreter for MVS 3.8j (including BREXX/370) failed at reentrancy
because of global variables. We do not repeat that mistake.

**Forbidden:**
- `static` mutable variables in any source file
- `extern` mutable variables
- File-scope variables that hold interpreter state
- Global function pointers

**Required:**
- All mutable state in `irx_wkblk_int` or sub-structures thereof
- All functions receive their context via parameter (typically
  `struct envblock *` from which `irx_wkblk_int` is reachable
  via `envblock->envblock_userfield`)
- Multiple concurrent REXX environments must be able to coexist
  in the same address space without interfering

**The one exception:** The cross-compile test harness (`test/tst*.c`)
may define `void *_simulated_ectenvbk` as a global to simulate the
MVS TCB. This is test-only, never in production code.

## Build system

- **Toolchain:** cc370 (C → S/370 object, `-std=gnu99`), libc370 from
  the cc370 sysroot (version pinned in `project.toml` `[toolchain]`)
- **Build tool:** mbt 2 — reads `project.toml`, driven by `make`
- **Dependency:** lstring370 (staged in `.mbt/deps`; the host build
  compiles it from `../lstring370/src`)
- **Target:** MVS 3.8j, AMODE 24, RMODE 24

```bash
make                               # full build (all modules)
make irxjcl                        # specific module (lowercase alias)
```

Note: `#` in filenames must be quoted in shell commands.

## File naming convention

Source files: `src/irx#module.c` — the `#` separates prefix from
submodule name and maps to a valid PDS member name on MVS.
New source files follow the same pattern: `src/irx#xxxx.c` where
`xxxx` is a 4-character identifier. Member names must be ≤ 8 chars.

Headers: `include/name.h` — plain names, no prefix convention.

Tests: `test/tstname.c` — lowercase, no `#`, stem ≤ 8 chars
(PDS member `TSTNAME`). Each test is both a Linux/gcc unit test and
an MVS standalone load module (entry via `@@CRT0`, `main()` returns
RC=0/1).

## Testing

Every work package produces a `test/tst*.c` that runs both on the host
and as an MVS load module. Each test is declared as a `[[test]]` block
in `project.toml`; mbt v2 generates the run JCL from that block. Only a
few pure-assembler VLIST-wrapper edge cases also need a hand-written
`test/jcl/<name>.jcl`.

```bash
make test-host   # build + run the host-runnable tests natively (fast inner loop)
make tsttokn     # build one test module (or `make test` for all)
make test-mvs    # build + deploy the test modules to a TESTLIB + run on MVS
```

Registering a new test as a `[[test]]` block is part of the change, not
a follow-up. Details, the hand-compile recipe and the historical counts:
`internals/testing.md`.

## Memory management

All memory allocation goes through `irxstor()` (defined in
`src/irx#stor.c`). Never call `malloc`/`calloc`/`free` directly
from any module other than `irx#stor.c` itself.

```c
void *ptr = NULL;
int rc = irxstor(RXSMGET, size, &ptr, envblock);  /* allocate */
/* ... use ptr ... */
irxstor(RXSMFRE, size, &ptr, envblock);            /* free     */
```

On MVS `irxstor` issues GETMAIN/FREEMAIN itself, by inline assembler,
for every subpool including 0 (#298), with libc370's 8-byte block prefix;
that layout must not change (see the header of `src/irx#stor.c`). On the
host it uses `calloc`/`free`.

## Error handling pattern

Use the ALLOC/cleanup pattern established in `src/irx#init.c`:

```c
#define ALLOC(ptr, size, envblk) \
    do { \
        void *_tmp = NULL; \
        int _rc = irxstor(RXSMGET, (int)(size), &_tmp, (envblk)); \
        if (_rc != 0) goto cleanup; \
        (ptr) = _tmp; \
    } while (0)

int some_function(struct envblock *envblk)
{
    struct foo *a = NULL;
    struct bar *b = NULL;

    ALLOC(a, sizeof(struct foo), envblk);
    ALLOC(b, sizeof(struct bar), envblk);

    /* ... success path ... */
    return 0;

cleanup:
    if (b) { void *p = b; irxstor(RXSMFRE, 0, &p, envblk); }
    if (a) { void *p = a; irxstor(RXSMFRE, 0, &p, envblk); }
    return 20;
}
```

## Control block discipline

- Every control block starts with a 4 or 8 byte eye-catcher
- Always validate eye-catchers before accessing fields
- **Never** modify the layout of structs in `include/irx.h` — these
  are the IBM-compatible interface. Extensions go in `irxwkblk.h`
  or new internal headers.
- Use `memcpy()` for eye-catcher initialization, not string assignment
  (eye-catchers are `unsigned char[]`, not `char*`)

## Platform and EBCDIC

**Platform detection:** cc370 defines `__MVS__`. Use `#ifdef __MVS__`
for MVS-specific code paths. The `#else` path is for cross-compile
testing on Linux/gcc.

- Use `ctype.h` functions (`isalpha`, `isdigit`, `isalnum`), which
  libc370 provides in EBCDIC-correct form
- For cross-compile testing, ASCII is fine — the logic should be
  character-set-neutral using ctype.h

## Code style

C99 / gnu99 throughout `src/`, `include/` and `test/`. The full style
guide is [`internals/code-style.md`](internals/code-style.md);
`.clang-format` and `.clang-tidy` at the repo root encode the
mechanically enforceable parts. In short: Allman braces, braces even on
one-line bodies (and by hand around non-trivial `case` bodies), 4-space
indent, `snake_case`, `ALL_CAPS` macros and enum values, `_t` typedefs,
`FILENAME_H` guards without leading underscores, no magic numbers,
declarations where first used, no bare `{}` blocks just to declare.

### Tooling workflow

**clang-tidy during development.** Run while iterating to catch
issues early. Warnings are advisory — they surface magic numbers,
const-correctness, multi-level pointer conversions, and similar
concerns. Fix what's in your diff; pre-existing warnings are out
of scope for the current commit unless the task explicitly includes
them.

```sh
clang-tidy --config-file=.clang-tidy src/irx#*.c -- \
    -Iinclude -I../lstring370/include -std=gnu99
```

**clang-format at the end of each commit, only on files you changed.**
Run it once as the very last step, immediately before `git add`.
Pass the specific files you touched — do not sweep the whole tree
from a feature branch (that belongs in a dedicated `chore: clang-format
sweep` commit).

```sh
clang-format --style=file:.clang-format -i <files you changed>
```

The codebase is convergent with `.clang-format`. Any diff produced by
`clang-format` on a file you didn't semantically change is a red flag:
re-check that you're using `--style=file:.clang-format` and that your
local clang-format isn't picking up a different config.

### Comments and documentation

- All code comments in **English**
- German only for user-facing documentation (manual, if any)

## Work packages & roadmap

**`internals/ROADMAP.md` is the single source of truth for current status and what's
next.** Read it first. It orders all open work into five axes (Performance,
Decommission/Correctness, Spec-Completeness, Environment/Anchor, Infrastructure)
and tracks which design docs (CONs) are current vs. stale.

**ROADMAP is part of the deliverable, not a separate chore.** If a PR closes or
materially changes a work package that ROADMAP names, the *same PR* must update
the ROADMAP entry — move a finished item to its axis's "Closed gates"/"Recently
shipped" note (Decommission axis) or remove it (other axes), and refresh the
"Last updated" date. The truth lives in the repo, so a PR that ships the code but
leaves ROADMAP saying the work is still open has left the source of truth wrong.
Same class of always-do as the test-registration rule (a `[[test]]` block in
`project.toml`).

`internals/workpackages.md` holds the historical Phase 1-3 per-WP definitions and is
**frozen/stale** — do not treat its status table as current.

**The old System A / System B measuring machines no longer exist.** Every cps
figure predating 2026-08-15 (the 20.9k System A baseline, the 4.3× factor)
was taken on hardware that is gone and is *not* comparable to a new
measurement. Re-baseline on the current system before quoting a factor.

## SMP and the TSO integration

The SMP rules are the root's (`~/repos/mvs/.workspace/context/smp.md`).
Proposed first level: **`TRXX100`** for 1.0.0, no `delete`.

**The TSO integration is not part of the FMID.** The patches to IBM's TMP
(IKJEFT01 + IKJEFTRX) and EXEC (IKJCT430 + IKJCT437) live in `tso/` and ship as
the usermod `ZMG0002` (`tso/usermod/`, FMID EBB1102). State and tests:
`tso/TODO_IKJCT437.md`, `tso/lab/exec_test.py`.
