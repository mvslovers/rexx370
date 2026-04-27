# IRXPROBE — z/OS ECTENVBK behaviour probe (Phase α)

Research tooling for issue [#74](https://github.com/mvslovers/rexx370/issues/74).
Captures authoritative IBM TSO/E REXX behaviour against ECTENVBK and the
IRXANCHR registry on z/OS, so CON-3 Open Questions Q-INIT-1..4, Q-TERM-1
and Q-EXEC-1 can be answered byte-by-byte.

This directory contains everything needed to build, install, and run the
probe on z/OS. The layout:

```
test/zos/
├── asm/irxprobe.asm    HLASM source for the IRXPROBE load module
├── jcl/uplprob.jcl     allocate REXX370.{SOURCE,LOAD,REXX} on z/OS
├── jcl/asmprob.jcl     assemble + link IRXPROBE
├── rexx/probed.rex     Discovery driver (case D)
├── rexx/proba1.rex     Action drivers, one per case
├── rexx/proba2.rex
├── rexx/proba3.rex
├── rexx/proba4.rex
├── rexx/proba5.rex     two-step (capture env addr between steps)
├── rexx/proba6.rex     two-step (risk: may destabilise session)
└── rexx/proba7.rex     IRXEXEC stub — full linkage is a follow-up
```

## Status

- **Source-side AC-1..AC-3 deliverable.** The module assembles, links, and
  the drivers run the cases as specified in the issue.
- **AC-4..AC-7 require z/OS execution by Mike.** This README is the run
  recipe.
- **AC-1 caveat.** The HLASM has not been assembled on a real HLASM
  toolchain in this branch; expect at most a small round of assembly
  fix-ups on first contact. They should be local — addressing-mode
  diagnostics, V-CON resolution against `IRXINIT` / `IRXTERM` (LPA), or
  `SYS1.MODGEN` SYSLIB substitution.
- **A7 caveat.** The `EXEC` subcommand is a stub. Full IRXEXEC linkage
  needs an `EXECBLK` and `EVALBLOCK` and is in scope for a follow-up
  ticket once the simpler cases have produced a reference log.

## One-time setup

1. **Allocate the three datasets.**

   Edit `jcl/uplprob.jcl` if your HLQ is not `&SYSUID..REXX370.*`, then
   submit it. RC=0 leaves you with:

   - `&SYSUID..REXX370.SOURCE` — FB/80 PDS for HLASM source
   - `&SYSUID..REXX370.LOAD`   — load library for `IRXPROBE`
   - `&SYSUID..REXX370.REXX`   — FB/80 PDS for REXX execs

2. **Upload the source.**

   Push `asm/irxprobe.asm` into `&SYSUID..REXX370.SOURCE(IRXPROBE)`.
   Use whichever transfer you prefer:

   ```
   zowe files upload file-to-data-set test/zos/asm/irxprobe.asm \
       "&USER..REXX370.SOURCE(IRXPROBE)"
   ```

3. **Upload the eight REXX drivers.**

   ```
   for d in d a1 a2 a3 a4 a5 a6 a7; do
     mem=PROBE${d^^}
     zowe files upload file-to-data-set test/zos/rexx/probe${d}.rex \
         "&USER..REXX370.REXX(${mem})"
   done
   ```

   Member name mapping:

   | Local file        | PDS member |
   |-------------------|-----------|
   | `probed.rex`      | `PROBED`  |
   | `proba1.rex`      | `PROBEA1` |
   | `proba2.rex`      | `PROBEA2` |
   | `proba3.rex`      | `PROBEA3` |
   | `proba4.rex`      | `PROBEA4` |
   | `proba5.rex`      | `PROBEA5` |
   | `proba6.rex`      | `PROBEA6` |
   | `proba7.rex`      | `PROBEA7` |

4. **Assemble and link.**

   Submit `jcl/asmprob.jcl`. Verify RC=0 on both the `ASM` and `LKED`
   steps. The load module lands in `&SYSUID..REXX370.LOAD(IRXPROBE)`.

   If assembly fails:
   - `IRXINIT` / `IRXTERM` external references unresolved → confirm
     `SYS1.CSSLIB` is in `LKED.SYSLIB` (and that the system has TSO/E
     installed in LPA — it should on any modern z/OS).
   - Other minor source-level fix-ups → patch in place and re-submit;
     fold the fix back into `asm/irxprobe.asm` as you go and open a
     follow-up commit on the issue branch.

## Running the eight test cases

**One LOGON session per case.** State isolation between cases is
non-negotiable. After each case, log off, then log on fresh for the next.

For each session, capture the terminal log via your 3270 emulator
(x3270: `Trace Data Stream`; Vista TN3270: session log; Tom Brennan
TN3270: `File → Capture`). The captured log is the per-case artefact;
concatenated, the eight logs become the **master log**.

The `===CASE-…===` markers emitted by the REXX drivers and the
`== marker ==` lines emitted by `IRXPROBE MARK` are the structural
boundaries the master log uses. They make a `diff` between the z/OS
master log and a future MVS 3.8j run mechanically meaningful.

### Run order

| # | Case | LOGON | Driver invocation | Notes |
|---|------|-------|-------------------|-------|
| 1 | D    | fresh | `EX 'hlq.REXX(PROBED)'`                                     | Read-only baseline. Do this first; it produces the addresses you'll quote in A2 and A6. |
| 2 | A1   | fresh | `EX 'hlq.REXX(PROBEA1)'`                                    | First IRXINIT after logon. |
| 3 | A2   | fresh | step 1: `EX 'hlq.REXX(PROBEA2)'` (gives address); step 2: `EX 'hlq.REXX(PROBEA2)' 'xxxxxxxx'` | Pass the TMP-default ECTENVBK from the step-1 DUMP as the prev address. Same LOGON. |
| 4 | A3   | fresh | `EX 'hlq.REXX(PROBEA3)'`                                    | Non-TSO env in TSO session. |
| 5 | A4   | fresh | `EX 'hlq.REXX(PROBEA4)'`                                    | Two IRXINIT calls back-to-back. |
| 6 | A5   | fresh | step 1: `EX 'hlq.REXX(PROBEA5)'`; step 2: `EX 'hlq.REXX(PROBEA5)' 'xxxxxxxx'` | After step 1, copy the `new envblock` value into the step-2 argument. Same LOGON. |
| 7 | A6   | fresh | step 1: `EX 'hlq.REXX(PROBEA6)'`; step 2: `EX 'hlq.REXX(PROBEA6)' 'xxxxxxxx'` | **Risk:** may destabilise the session. Run last. After step 1, copy the `ECTENVBK` value. |
| 8 | A7   | fresh | step 1 / step 2 — but EXEC is currently a stub | Run only after the IRXEXEC follow-up lands. The driver is included so the case anchor exists. |

### What "pass the address" means in steps 2 of A2 / A5 / A6 / A7

The drivers print every observed pointer in 8-digit hex on a clearly
labelled line, e.g.

```
  ECTENVBK             = 1A234560
```

or, after an `INIT`, e.g.

```
  new envblock         = 1A2C5680
```

Read the value off the terminal log, type it (eight hex digits, no
prefix, no quotes inside the apostrophes) on the step-2 invocation:

```
EX 'hlq.REXX(PROBEA5)' '1A2C5680'
```

That keeps the manual loop cheap without requiring `IRXEXCOM` plumbing
in the HLASM module. (Adding `IRXEXCOM` SHV writes is a worthwhile
follow-up but deliberately out of scope for the v1 probe.)

## Building the master log

Once all eight cases have produced terminal-log files (`probe-d.log`,
`probe-a1.log`, …, `probe-a7.log`), concatenate them:

```
cat probe-d.log probe-a1.log probe-a2.log probe-a3.log \
    probe-a4.log probe-a5.log probe-a6.log probe-a7.log \
    > IRXPROBE.MASTER.LOG
```

Attach `IRXPROBE.MASTER.LOG` to CON-3 and add the reference table
(AC-5, AC-6).

## Updating CON-3 (AC-6)

For each Open Question, fill in the IBM-observed behaviour:

| Question  | Source case | Observed |
|-----------|-------------|----------|
| Q-INIT-1  | A1          | _from log_ |
| Q-INIT-2  | A2          | _from log_ |
| Q-INIT-3  | A3          | _from log_ |
| Q-INIT-4  | A4          | _from log_ |
| Q-TERM-1  | A5          | _from log_ |
| Q-EXEC-1  | A7 (later)  | _follow-up_ |

Where the IBM behaviour diverges from the rexx370 conservative
assumption, open a follow-up ticket per AC-7.

## Phase β

Phase β (porting the probe to MVS 3.8j and diffing the master logs) is
explicitly out of scope here. It needs (1) the `EXEC` subcommand fully
implemented and (2) a port of `irxprobe.asm` to IFOX00-compatible
assembler. Track it as a separate ticket once Phase α is in CON-3.
