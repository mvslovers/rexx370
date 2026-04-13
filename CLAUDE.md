# CLAUDE.md — REXX/370 Project Instructions

## What is this project?

REXX/370 is a from-scratch REXX language interpreter for MVS 3.8j,
interface-compatible with IBM TSO/E Version 2 REXX (SC28-1883-0).
It is **not** a port of BREXX/370 — it is a clean reimplementation
with reentrancy as a core design constraint.

The primary specification is the IBM manual SC28-1883-0 (TSO Extensions
Version 2 REXX Reference, December 1988). When in doubt about REXX
language behaviour, semantics, or control block layouts, consult this
manual. It is available at https://vtda.org (search for SC28-1883).

## Architecture

The architecture design document is at:
https://www.notion.so/3283d9938787811ba3f4d3308b254cad

Key points:
- **ENVBLOCK** is the anchor for each Language Processor Environment
- **TCB → TCBUSER → RAB → ENVBLOCK** chain manages environments per task
- **IRXEXTE** (Vector of External Entry Points) holds all replaceable
  routine pointers — SAY, PULL, I/O, Host Command, etc.
- **irx_wkblk_int** (our internal Work Block) holds all per-environment
  interpreter runtime state (variables, stack, trace, numeric settings)
- All state is per-environment. Zero globals. This is non-negotiable.

## Build system

- **Compiler:** c2asm370 (C → HLASM cross-compiler for MVS 3.8j)
- **C Library:** crent370 (reentrant C runtime, provides malloc/free,
  stdio, string.h, plus MVS-specific APIs)
- **Build tool:** mbt — reads `project.toml`
- **Target:** MVS 3.8j, AMODE 24, RMODE 24

```bash
mbt build                          # full build
mbt build --target irxjcl          # specific module
```

Cross-compile for local testing (Linux/gcc):
```bash
gcc -I./include -Wall -Wextra -std=gnu99 -o test/test_FOO \
    test/test_FOO.c \
    'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \
    'src/irx#rab.c' 'src/irx#uid.c' 'src/irx#msid.c'
```

Note: `#` in filenames must be quoted in shell commands.

## File naming convention

Source files: `src/irx#module.c` — the `#` separates prefix from
submodule name and maps to a valid PDS member name on MVS.

Headers: `include/name.h` — plain names, no prefix convention.

Tests: `test/test_name.c`

| File | Member | Purpose |
|------|--------|---------|
| `src/irx#init.c` | IRX#INIT | IRXINIT — environment initialization |
| `src/irx#term.c` | IRX#TERM | IRXTERM — environment termination |
| `src/irx#stor.c` | IRX#STOR | Storage management replaceable routine |
| `src/irx#rab.c` | IRX#RAB | RAB chain management |
| `src/irx#uid.c` | IRX#UID | User ID replaceable routine |
| `src/irx#msid.c` | IRX#MSID | Message ID replaceable routine |

New source files follow the same pattern: `src/irx#xxxx.c` where
`xxxx` is a 4-character identifier. Member names must be ≤ 8 chars.

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

**The one exception:** The cross-compile test harness (`test/test_*.c`)
may define `void *_simulated_tcbuser` as a global to simulate the
MVS TCB. This is test-only, never in production code.

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

On MVS with a specific subpool configured in PARMBLOCK, `irxstor`
uses `getmain()`/`freemain()` from `<clibos.h>`. Otherwise it uses
the crent370 heap (`calloc`/`free`).

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

## crent370 API cheat sheet

| Header | Provides | When needed |
|--------|----------|-------------|
| `<stdlib.h>` | malloc, calloc, free, atoi | Always (via irxstor) |
| `<stdio.h>` | printf, fprintf, sprintf | Testing, debug |
| `<string.h>` | memcpy, memset, memcmp, strlen | Always |
| `<clibos.h>` | getmain, freemain, BLDL, LOAD, LINK | Subpool alloc, module loading |
| `<clibwto.h>` | wtof() | Operator messages (IRXnnnnI) |
| `<clibstae.h>` | ESTAE wrappers | Phase 6+ recovery |
| `<clibcrt.h>` | C runtime area access | Runtime introspection |
| `<clibthrd.h>` | Thread primitives | Future multi-thread |
| `<osio.h>` | OPEN/CLOSE/GET/PUT DCB | Phase 4 EXECIO |
| `<osdcb.h>` | DCB structure definitions | Phase 4 EXECIO |
| `<clibcib.h>` | Console interface blocks | If STC console needed |

**Platform detection:** c2asm370 defines `__MVS__`. Use `#ifdef __MVS__`
for MVS-specific code paths. The `#else` path is for cross-compile
testing on Linux/gcc.

## EBCDIC

MVS 3.8j uses EBCDIC (Code Page 037/1047). When processing REXX
source code or comparing characters:

- Do NOT hardcode ASCII values (e.g., `c >= 0x41 && c <= 0x5A`)
- Use `ctype.h` functions (`isalpha`, `isdigit`, `isalnum`) which
  crent370 provides in EBCDIC-correct form
- Character literals like `'A'` or `'0'` are compiled correctly by
  c2asm370 (they produce EBCDIC values)
- For cross-compile testing, ASCII is fine — the logic should be
  character-set-neutral using ctype.h

## Testing

Every work package produces a `test/test_*.c` file that runs on
Linux/gcc without MVS. Tests use a simple CHECK macro:

```c
#define CHECK(cond, msg) ...  /* see test/test_phase1.c */
```

Run Phase 1 test:
```bash
gcc -I./include -Wall -Wextra -std=gnu99 -o test/test_phase1 \
    test/test_phase1.c \
    'src/irx#init.c' 'src/irx#term.c' 'src/irx#stor.c' \
    'src/irx#rab.c' 'src/irx#uid.c' 'src/irx#msid.c'
./test/test_phase1
```

Expected: `38/38 passed`

## Work packages

See `doc/workpackages.md` for the full list of work packages.
Each WP is self-contained with inputs, outputs, constraints,
and acceptance criteria.

Current status: Phase 1 (WP-01 through WP-05) is complete.
Next: Phase 2 starting with WP-10 (Tokenizer).

## Knowledge sources

BREXX/370 (https://github.com/mvslovers/brexx370) is a knowledge
source for REXX BIF implementations and MVS integration patterns.
Do **not** copy code verbatim — reimplemented clean with reentrant
design. Useful files to reference:

| BREXX file | What to learn from it |
|---|---|
| `inc/irx.h` | IBM control block layouts (already extracted) |
| `lstring/*.c` | REXX string operation algorithms |
| `src/nextsymb.c` | Token classification logic |
| `src/interpre.c` | Opcode dispatch patterns (but avoid its globals) |
| `src/variable.c` | Hash table and compound variable resolution |
| `src/rxmvs.c` | MVS BIF implementations (LISTDSI, SYSDSN, etc.) |
| `src/builtin.c` | Built-in function dispatch table |

## Comments and documentation

- All code comments in **English**
- German only for user-facing documentation (manual, if any)
