# tso/ — REXX/370 in the TSO address space

Patched IBM TSO modules from MVS 3.8j and the test driver around them. They are
**not an mbt target**: mbt builds rexx370's own modules, while these are IBM
sources assembled against the MVS/CE macro libraries of the `mvs38src` project.

| File | What it is |
|---|---|
| `IKJEFT01.ASM` | The TMP, patched to call `IKJEFTRX` at start (the IBM source is `mvs38src/src/IKJEFT01.ASM`) |
| `IKJEFTRX.ASM` | Ours: loads IRXANCHR/IRXINIT and creates the default environment at logon (`IKJ56942I`) |
| `IKJCT430.ASM` | The EXEC command processor, patched to ask `IKJCT437` before treating a member as CLIST (the IBM source is `mvs38src/src/IKJCT430.ASM`) |
| `IKJCT437.ASM` | Ours: decides whether an implicitly invoked member is REXX, and runs it |
| `RXDRV.ASM` | Test driver: calls `IKJCT437` exactly as `IKJCT430` does, from a small load module |
| `lmod_link.py` | Links decks on MVS against the INSTALLED load module, the way SMP does: `reference` proves the IBM source reproduces it, `testlib` puts the patched modules into `REXX370.TSO.LINKLIB` |
| `lab/exec_test.py` | The EXEC language rules as a 27-case table: patched modules via STEPLIB, the IBM TMP, or what SMP installed |
| `usermod/ZMG0002.mcs`, `usermod.py` | The SMP4 usermod: MCS with cover letter, JCLIN and four `++MOD` decks, built into `build/tso/ZMG0002.smp` |
| `lab/zmg_install.py` | Install step by step (backup, receive, applycheck, apply, verify; restore for a rebuilt test level) |
| `lab/testlib_put.py` | Put rexx370 modules into the APF test library |
| `lab/tsofg.py` | Drive one TSO foreground session through s3270 and log every screen (SAY on a terminal cannot be checked in batch) |
| `lab/rep_*.py` | The MVSCE-LAB repair of 2026-09-25 (TK5 level for IKJEFT01/06/SC), kept for the record |
| `TODO_IKJEFT01.md` | Part A (the TMP): build and link recipe, traps, measurements |
| `TODO_IKJCT437.md` | Part B (the classifier): state, measurements, traps |

## Build

```sh
tso/build.sh rxdrv       # RXDRV + IKJCT437 -> build/tso/RXDRV.xmit
tso/build.sh ikjeft01    # decks IKJEFT01.o, IKJEFTRX.o (+ IKJEFT01.orig.o)
tso/build.sh exec        # decks IKJCT430.o, IKJCT437.o (+ IKJCT430.orig.o)
```

The TSO load modules are not linked here. They ship as an SMP usermod of
object decks, and SMP link-edits those into the installed load module, so the
service already on it survives (KB `MVS-SMP-0004`).

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

```sh
python3 tso/lmod_link.py reference exec|ikjeft01   # IBM source == installed?
python3 tso/lmod_link.py testlib   exec|ikjeft01   # patched -> REXX370.TSO.LINKLIB
python3 tso/lab/exec_test.py new-tmp               # patched TMP + patched EXEC
python3 tso/lab/exec_test.py drop-tmp              # then: IBM TMP + patched EXEC
python3 tso/lab/exec_test.py ibm-tmp
```

`REXX370.TSO.LINKLIB` is APF-authorized and not in the link list. A batch TMP
with it as STEPLIB takes `IKJEFT01` and `EXEC` from there, ahead of the LPA and
`SYS1.CMDLIB`. Nobody else is affected.

## Status

See `TODO_IKJCT437.md`, "Wo es weitergeht".
