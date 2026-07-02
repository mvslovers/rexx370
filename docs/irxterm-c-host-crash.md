# IRXTERM crashes when invoked from a crent370 C-runtime host — RESOLVED

**Status:** RESOLVED — root cause found, fixed, full test matrix green.
**Date opened:** 2026-07-02 · **Date resolved:** 2026-07-02
**Related:** mvslovers/rexx370#204 (separate `env_get_safe` foreign-PPA bug,
fixed on the `issue-204-foreign-ppa-getenv` branch — pending `make test-mvs`),
mvslovers/httprexx (the consumer that hit this).

---

## TL;DR — root cause

**The bug was never in IRXTERM, the C runtime, or the LINK/BALR machinery.
It was a single assembler-syntax pitfall in the caller-side shim
(`test/trxcall.asm` / httprexx `asm/htrxterm.asm`):**

```asm
         LM    R0,R12,20(,R13)    restore R0-R12        <-- BROKEN
         LM    R0,R12,20(R13)     restore R0-R12        <-- correct
```

`LM`/`STM` are RS-format instructions: the operand syntax is `D2(B2)`.
The RX-style `D2(,B2)` form (empty index) is **silently assembled by as370
with BASE = 0**:

```
LM 0,12,20(,13)   ->  98 0C 0014      base 0, i.e. LM from ABSOLUTE 0x14
LM 0,12,20(13)    ->  98 0C D014      base R13 — intended
```

So the shim's epilog restored **R0–R12 from PSA low core 0x14–0x48** (CVT
pointer, old PSWs, CSWs) instead of from the caller's save area. The adjacent
`L R14,12(,R13)` is RX-format (has an index field) and therefore assembled
correctly — which is why every dump showed **R13/R14 valid but R0–R12
"system-flavored garbage"**: those "garbage" words were literally the old
PSWs of our own task (`078D0000 xxxxxxxx` pairs) and nucleus addresses from
low core. The garbage varied with interrupt timing, hence the wandering
wild-branch targets and the rotating abend codes (S0C1/S0C2/S0C4/S0C6).

### Why every earlier hypothesis mis-fit

- **"Only when called from a C host"** — an artifact. Only the C-host path
  goes through the buggy shim (`trx_call`/`HRXCALL`); the pure-asm tests
  (`TTERMVL`, `TLNKTERM`) and the VLIST wrappers use the correct `D(B)`
  syntax (`LM R1,R12,24(R13)`), so they never failed.
- **"The caller's C-DSA frame is corrupted during the IRXTERM call"** —
  refuted by instrumentation: word-by-word diffs of the caller frame
  (192 bytes), the shim workarea, a 28 KB module window, and a 2M-iteration
  SVC-free spin probe all showed **zero unexpected writes**. Storage was
  never corrupted; the broken LM simply never read it.
- The exec, IRXTERM's teardown, `__load`, the LINK-vs-BALR asymmetry and
  subpool-0 collisions were all exonerated (see Evidence log).

## The fix

- `test/trxcall.asm` — epilog `LM R0,R12,20(R13)` (was `20(,R13)`), plus a
  warning comment. The file is back to the plain production shim shape.
- `test/trxldc.asm` — same fix in `TRXCALLV`.
- **httprexx `asm/htrxterm.asm`** — same fix applied (uncommitted, in the
  httprexx working tree). httprexx can re-enable IRXTERM and stop leaking
  the LPE.
- Repo-wide scan for further RS-format `D(,B)` operands (LM/STM/CS/CDS/
  shifts/ICM/…) found **no other instance** in rexx370, httprexx, or
  libc370.

## Second bug found & fixed on the way: `asm/istso.asm` S328

While verifying, TREXXVL's batch leg failed deterministically in its 5th
IRXINIT with **S328 inside SVC 40 (EXTRACT)**: `istso.asm` issued
`EXTRACT …,MF=(E,WEXTLST)` with the parameter list in a **freshly GETMAINed,
non-zeroed workarea**. The E-form stores the answer-area address and the
FIELDS bytes but leaves the TCB slot (list+4) untouched; residual garbage
there is interpreted as a TCB address → S328. Worked by luck whenever the
GETMAINed storage happened to be zero. Fixed by `XC`-clearing the parameter
list and answer area before the EXTRACT. (The "issues SVC 9" comment was
also wrong — EXTRACT is SVC 40.)

## Verification (2026-07-02, MVS-CE `mvsdev.lan`)

- `TREXXVL` — httprexx-shaped end-to-end (LINK-IRXINIT → LINK-IRXEXEC →
  `__load`+BALR-IRXTERM with the production shim), cases 0c + 1–8, each with
  a real IRXTERM: **batch + TSO CC 0** (JOB 899, 913).
- `TINITVL`, `TTERMVL`, `TLNKTERM`, `TEXECVL`, `TISTSO`: **CC 0**.
- Full suite: 55 tests × batch+TSO — **0 failures**
  (JOBs 903/905/909/911/913), host suite 2474 assertions PASS.
- Negative proof: re-introducing the `D(,B)` spelling reproduces the
  original crash signature immediately (JOB 890); the fixed spelling with
  the identical LINKLIB is green (JOB 899).

## Follow-ups

1. **as370 (cc370 repo):** `D(,B)` on RS-format instructions should be an
   assembly ERROR (IFOX00 rejects it) or be assembled as `D(B)` — silently
   emitting base 0 is a landmine. File an issue with the 2-line reproducer
   above.
2. **httprexx:** commit the `htrxterm.asm` fix, remove the "IRXTERM
   temporarily DISABLED" workaround (`src/httprexx.c` ~L331), retest.
3. **#204** (`env_get_safe` foreign-PPA on the IRXINIT-BALR path) is **fixed**:
   `env_get_safe()` now gates `getenv()` on a CLIBCRT being registered for the
   current TCB (a silent replay of `@@CRTGET`'s lookup) instead of merely
   checking for a non-NULL PPA. `test/trxldc.asm` (`TRXCALLV`) now drives
   TREXXVL case 0d, the on-target repro (pending `make test-mvs`).
4. **mbt:** `submit_jcl` polls max 120 s — a full 110-step runner exceeds it
   ("FAIL NO RC" with an empty spool). Consider a configurable timeout.
   The `//SYSUDUMP DD SYSOUT=*` added to test steps proved valuable — keep.

## Evidence log (job numbers, chronological)

- JOB 862–876: caller-frame diff, module-window diff, workarea probes —
  every storage region byte-stable across the IRXTERM call while the
  restore still produced garbage; post-mortems show low-core PSW pairs in
  R0–R12 with R13/R14 intact.
- JOB 868: pure-asm TTERMVL/TLNKTERM green against the same LINKLIB.
- JOB 892: 2M-iteration SVC-free spin after the IRXTERM BALR — zero writes
  to the caller frame ("TRXHIT NONE"), run green with a CSECT-sourced
  register restore.
- JOB 894: production sequence probed step-by-step — `WDPREV` correct,
  frame intact after FREEMAIN; only the `LM` differed.
- Host proof: `as370` object code of `LM 0,12,20(,13)` = `980C0014`
  (base 0) vs `LM 0,12,20(13)` = `980CD014`.
- JOB 896: istso S328 traceback (IRXIINIT → ISTSO → EXTRACT SVC).
- JOB 899/903/905/909/911/913: full green matrix after both fixes.
