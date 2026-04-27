# IRXPROBE — z/OS ECTENVBK behaviour probe (Phase α)

Research tooling for issue [#74](https://github.com/mvslovers/rexx370/issues/74).
Captures authoritative IBM TSO/E REXX behaviour against ECTENVBK and the
IRXANCHR registry on z/OS, so CON-3 Open Questions Q-INIT-1..4, Q-TERM-1
and Q-EXEC-1 can be answered byte-by-byte.

```
test/zos/
├── asm/irxprobe.asm    HLASM source for the IRXPROBE load module
├── jcl/uplprob.jcl     allocate REXX370.{SOURCE,LOAD,REXX} on z/OS
├── jcl/asmprob.jcl     assemble + link IRXPROBE
└── rexx/proba*.rex     legacy REXX drivers (optional, see appendix)
```

## Status

- **AC-1..AC-3 done.** IRXPROBE assembles cleanly, links cleanly,
  dispatches all eight subcommands. Verified on z/OS HLASM R6.0 and
  IEWL.
- **AC-4 done.** All Phase α cases (Discovery + A1..A6) ran on z/OS
  and produced clean SYSPRINT-DD output. A7 deferred (IRXEXEC stub
  — full linkage in a follow-up).
- **AC-5..AC-7 are Mike's:** concatenate the per-session captures
  into the master log, attach to CON-3, fill in the reference
  table, open follow-up tickets for any IBM-vs-rexx370 divergence.

## Subcommands

`IRXPROBE` accepts a single subcommand or a `;`-separated sequence
on one invocation. All segments share state (most notably `LASTENV`,
which TERM_LAST consumes), so multi-step cases like A5 can run as
one CALL on one subtask.

| Subcommand          | Action |
|---------------------|--------|
| `DUMP`              | Read-only ECTENVBK + IRXANCHR + ENVBLOCK + PARMBLOCK |
| `INIT [module]`     | IRXINIT INITENVB; default parm-module `IRXTSPRM` |
| `INITP hex`         | IRXINIT INITENVB with R0 = hex caller-prev env |
| `INITNT [module]`   | IRXINIT INITENVB; default parm-module `IRXPARMS` |
| `TERM hex`          | IRXTERM on the explicit hex env address |
| `TERM_LAST`         | IRXTERM on the env from the most recent INIT* in this CALL |
| `EXEC hex`          | Stub (NOT_IMPLEMENTED) — A7 follow-up |
| `MARK text`         | Emit a `-- text` separator line |

`PARMODE` defaults are picked so `INIT` and `INITNT` work without
any argument; pass an explicit module name (e.g. `INIT IRXISPRM`)
to override.

## One-time setup

1. **Allocate the datasets.**

   Edit `jcl/uplprob.jcl` if your HLQ is not `&SYSUID..REXX370.*`,
   then submit it. RC=0 leaves you with:

   - `&SYSUID..REXX370.SOURCE` — FB/80 PDS for HLASM source
   - `&SYSUID..REXX370.LOAD`   — load library for `IRXPROBE`
   - `&SYSUID..REXX370.REXX`   — FB/80 PDS for REXX execs (only needed if you use the legacy REXX drivers)

2. **Upload the source.**

   Push `asm/irxprobe.asm` into `&SYSUID..REXX370.SOURCE(IRXPROBE)`.

   ```
   zowe files upload file-to-data-set test/zos/asm/irxprobe.asm \
       "&USER..REXX370.SOURCE(IRXPROBE)"
   ```

3. **Assemble and link.**

   Submit `jcl/asmprob.jcl`. Verify RC=0 on both the `ASM` and
   `LKED` steps. The load module lands in
   `&SYSUID..REXX370.LOAD(IRXPROBE)`.

   `IRXINIT` and `IRXTERM` are loaded at runtime via `LOAD EP=`,
   so the link step has no external references to resolve and no
   `SYS1.CSSLIB` SYSLIB DD is required.

## Running the eight test cases

**One LOGON session per case.** State isolation between cases is
non-negotiable. After each case, log off, then log on fresh for
the next. Capture the terminal log via your 3270 emulator
(x3270 `Trace Data Stream`, Vista TN3270 session log, Tom Brennan
`File → Capture`, etc.). The captured log is the per-case
artefact; concatenated, the eight logs become the **master log**.

Each segment in the output starts with a `== <SUBCMD> ==` marker.
Those markers and the structured key-value lines (`ECTENVBK = ...`,
`new envblock = ...`, `IRXTERM RC = ...`) are what makes a `diff`
between the z/OS master log and a future MVS 3.8j run mechanically
meaningful.

### Run order

| # | Case | LOGON | Invocation | Notes |
|---|------|-------|------------|-------|
| 1 | D    | fresh | `CALL 'hlq.LOAD(IRXPROBE)' 'DUMP'`                                     | Read-only baseline. Captures the TMP-default ECTENVBK you'll quote in A2 and A6. |
| 2 | A1   | fresh | `CALL 'hlq.LOAD(IRXPROBE)' 'DUMP;INIT;DUMP'`                           | Q-INIT-1: first IRXINIT after LOGON. |
| 3 | A2   | fresh | step 1: `CALL ... 'DUMP'`<br>step 2: `CALL ... 'INITP xxxxxxxx;DUMP'`  | Q-INIT-2. Read TMP-default ECTENVBK from step 1, paste into step 2 (same LOGON). |
| 4 | A3   | fresh | `CALL 'hlq.LOAD(IRXPROBE)' 'DUMP;INITNT;DUMP'`                         | Q-INIT-3: TSOFL=0 in TSO session. |
| 5 | A4   | fresh | `CALL 'hlq.LOAD(IRXPROBE)' 'DUMP;INIT;DUMP;INIT;DUMP'`                 | Q-INIT-4: two IRXINIT calls back-to-back. |
| 6 | A5   | fresh | `CALL 'hlq.LOAD(IRXPROBE)' 'DUMP;INIT;DUMP;TERM_LAST;DUMP'`            | **Q-TERM-1.** `LASTENV` carries the new env from INIT to TERM_LAST inside one CALL. |
| 7 | A6   | fresh | step 1: `CALL ... 'DUMP'`<br>step 2: `CALL ... 'TERM xxxxxxxx;DUMP'`   | LIFO/anchor-protect probe. **Risk:** may destabilise the session — run last. |
| 8 | A7   | fresh | (deferred — IRXEXEC stub)                                              | Run only after the IRXEXEC follow-up lands. |

In A2 and A6, the address pasted into step 2 is the 8-digit hex
value the step-1 DUMP printed on the line labelled
`ECTENVBK = ...`. No prefix, no quotes inside the apostrophes:

```
CALL 'hlq.LOAD(IRXPROBE)' 'INITP 1A2C5680;DUMP'
```

## Building the master log

```
cat probe-d.log probe-a1.log probe-a2.log probe-a3.log \
    probe-a4.log probe-a5.log probe-a6.log probe-a7.log \
    > IRXPROBE.MASTER.LOG
```

Attach `IRXPROBE.MASTER.LOG` to CON-3 (AC-5).

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

Phase β (porting the probe to MVS 3.8j and diffing the master logs)
is explicitly out of scope here. It needs (1) the `EXEC` subcommand
fully implemented and (2) a port of `irxprobe.asm` to IFOX00-
compatible assembler. Track it as a separate ticket once Phase α is
in CON-3.

## Appendix: legacy REXX drivers

Eight REXX execs (`probed.rex`, `proba1.rex` .. `proba7.rex`) were
the original Phase α scaffolding before sequenced subcommands were
implemented. They still work — running them gives the same
per-segment output one CALL at a time — but the `;`-sequenced direct
CALLs above are the **canonical** Phase α invocations now and
should be preferred for the master-log captures.

To use the REXX drivers:

```
zowe files upload file-to-data-set test/zos/rexx/probed.rex \
    "&USER..REXX370.REXX(PROBED)"
# repeat for proba1..proba7 -> PROBEA1..PROBEA7

EX 'hlq.REXX(PROBED)'
```

The two-step drivers (`PROBEA2`, `PROBEA5`, `PROBEA6`, `PROBEA7`)
were a workaround for state not surviving between TSO CALL
invocations — the `;`-sequenced form solves that without manual
address-copy between steps.
