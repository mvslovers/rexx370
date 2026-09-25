# tso/ — REXX/370 in the TSO address space

Patched IBM TSO modules from MVS 3.8j and the test driver around them. They are
**not an mbt target**: mbt builds rexx370's own modules, while these are IBM
sources assembled against the MVS/CE macro libraries of the `mvs38src` project.

| File | What it is |
|---|---|
| `IKJEFT01.ASM` / `.orig` | The TMP, patched to call `IKJEFTRX` at start; `.orig` is the IBM source for diffing |
| `IKJEFTRX.ASM` | Ours: loads IRXANCHR/IRXINIT and creates the default environment at logon (`IKJ56942I`) |
| `IKJCT430.ASM` / `.orig` | The EXEC command processor, patched to ask `IKJCT437` before treating a member as CLIST |
| `IKJCT437.ASM` | Ours: decides whether an implicitly invoked member is REXX, and runs it |
| `RXDRV.ASM` | Test driver: calls `IKJCT437` exactly as `IKJCT430` does, from a small load module |
| `TODO_IKJEFT01.md` | Part A (the TMP): build and link recipe, traps, measurements |
| `TODO_IKJCT437.md` | Part B (the classifier): state, measurements, traps |

## Build

```sh
tso/build.sh rxdrv       # RXDRV + IKJCT437 -> build/tso/RXDRV.xmit
tso/build.sh ikjeft01    # patched TMP      -> build/tso/IKJEFT01.xmit
```

Needs `../mvs38src` (or `MVS38SRC=…`) for the pinned `as370` and the macro
libraries. The script fails on any as370 diagnostic, not just on the exit
status, because as370 can write an object and exit 0 at severity 8.

## Test

```sh
python3 tso/rxdrv_test.py install    # WRITES SYS2.LINKLIB (RXDRV only)
python3 tso/rxdrv_test.py run        # batch TMP job, prints the C1..C9 trail
```

`install` backs up the old `RXDRV` and reports if `SYS2.LINKLIB` needed a new
extent. A member there is not loadable through the link list until the next IPL
(IEA703I 106-F, measured 2026-09-25).

The patched `IKJEFT01` itself is installed into `SYS1.LPALIB` and needs an IPL
with CLPA. That stays a manual step, see `TODO_IKJEFT01.md`.

## Status

See `TODO_IKJCT437.md`, "Wo es weitergeht".
