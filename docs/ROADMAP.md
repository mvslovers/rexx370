# REXX/370 — Roadmap

Forward-looking development plan. This is the **single source of truth** for
"where are we and what's next". The detailed per-WP definitions live in
`docs/workpackages.md` (historical, Phase 1-3) and in the Notion Issues & Tasks
database; this file orders the open work into strategic axes and is kept current
as phases complete.

Last updated: 2026-07-02

---

## Where we are

The interpreter is **functionally complete enough to run real REXX** (REXXCPS,
Mike Cowlishaw's benchmark, runs end-to-end on MVS via `PGM=IRXJCL,PARM='REXXCPS'`).
Two execution paths exist:

- **Bytecode VM** — the primary path, default-on. Compiles REXX to bytecode and
  executes it. This is where all current work happens.
- **Token-walk interpreter** — frozen (see CON-18). Kept only as (1) an
  equivalence-test reference and (2) the UNSUP fallback for constructs the
  bytecode compiler does not yet handle.

**Two existential goals drive everything:**

1. **Approach BREXX/370 performance.** BREXX runs REXXCPS at ~89,878 cps on
   MVS/CE (System A). We are at ~20,899 cps (System A, post OC-ARITH) =
   **factor ~4.3× slower**, down from 6.3× at the start of the bytecode phase.
2. **Decommission the token-walk path.** Bytecode as the sole execution path.
   Requires the bytecode VM to be (a) functionally complete and (b) broadly
   correctness-verified. Tracked in CON-20.

### Performance history (System A — Docker/WSL, the "work" machine)

| Milestone | cps (System A) | factor to BREXX |
|---|---|---|
| Bytecode baseline | 14,280 | 6.3× |
| + OC-09 (BIF dispatch) | 17,274 | 5.2× |
| + OC-ARITH (integer op fast-path) | 20,899 | 4.3× |
| **BREXX/370 target** | **89,878** | 1.0× |

> **Measurement discipline:** Two MVS systems exist — System A (Docker/WSL,
> notebook) and System B (Docker/Proxmox-VM, home). Identical code gives
> different cps (A ≈ 1.4× B). Always compare before/after on the *same* system,
> and never accept a cps number without a verified `[bc] exec=1 fallback=0` line.
> Details in CON-12.

---

## The five axes

### Axis 1 — Performance (toward BREXX)

The three large, clearly-measurable hotspots from the first profile are done
(OC-12 numeric compare, OC-09 BIF dispatch, OC-ARITH integer op).

**The re-profile is done** (`docs/diag/wp-perf-profile-2.md`, HEAD `210b90f`,
2026-06-03). Verdict: OC-09 was decisive (BIF name-compare `ebcdic_eq`
57.3 M → 269 calls — cluster C gone); OC-12/OC-ARITH cut numeric ~24 % (cluster A
roughly halved, not gone); cluster B (variable pool) was untouched as designed
and is **now the #1 cluster (~27 % flat self)**.

Next candidates, ranked by the new profile (share ≠ lever — see the doc):
- **Variable-resolution (pointer) cache** (top recommendation) — cache the
  resolved vpool entry pointer at each bytecode reference *operand site*, for the
  ~88 % **simple-variable** bulk of cluster B. **Invalidation frequency now
  measured** (`docs/diag/wp-perf-varcache-diag.md`, 2026-06-03): the cache is a
  **real lever, not OC-06-redux** — writes do **not** invalidate it (`set`
  updates in place; resize re-links without moving entries), so only `DROP`/
  stem-drop/`PROCEDURE` invalidate → **44.8 reads/invalidation**. Hit rate splits
  by kind: **simple vars (~90 % of reads) ~99.9 %, clean** (the lever); compounds
  lower (a per-operand cache can't cache the entry pointer — must rebuild the
  composed key). Removes up to the ~18.8 % vpool-lookup self-time
  (simple-var-dominated **upper bound**), memory-bound (multiplier).
  **Explicitly NOT OC-06** (compound-only, measured 2.4–3.2 % and rejected) and
  NOT a write-invalidated value cache (that variant *is* dead, 2.06 reads/inval).
  Remaining gate: prototype + MVS-cps measurement (CON-12 discipline). Scope the
  first cut to simple variables; build checklist (free/scope/stem-shadow
  invalidation, compound key-rebuild) in the diag doc.
- **VM value copy-elision** (complementary) — the ~14 % lstring/VM-slot
  string-copy cluster (`slot_set_buf`/`Lstrcpy`/`Lfx`); pairs with the inline
  cache to fully drain `VPOOLGTB`. Memory-bound.
- **OC-11** — HLASM / threaded dispatch for `IRXBEXEC` (the 20.6 % dispatch-loop
  self-time; real on-target, the product also builds `-O0`). CPU-bound → weak
  multiplier; the eventual lever, but per CON-12 discipline *after* the
  algorithmic wins above.
- **OC-02** — packed-decimal backend (only if decimal arithmetic ever proves to
  be a real hotspot; cluster A residual is diminishing returns and partly a
  benchmark artifact).

Rejected/done: OC-06 (compound cache — measured, ~2.4-3.2%, not worth it),
OC-07 (done via WP-BC-06), OC-08 (folds into OC-12), OC-10 (obsolete — bytecode
resolves loops to jump offsets).

**Key heuristic — the Hercules multiplier:** memory-access-reducing
optimizations deliver 3-5× their host-measured effect on MVS (validated across
WP-PERF-03/04, OC-12, OC-ARITH). Weight memory-bound candidates higher than
pure-CPU ones. The host profile is an *iteration tool*; MVS cps is the target
metric.

### Axis 2 — Decommission / Correctness (toward token-walk removal)

The gate to removing the token-walk fallback. See CON-20 for the full schedule.
The cleanest way to scope this: **every `BC_FAIL_UNSUP` trigger in the bytecode
compiler is a decommission gate** — each is a construct that still forces a
whole-program fallback to token-walk.

**Closed gates** (constructs that no longer force a token-walk fallback):
- ~~**WP-BC-NUMERIC**~~ — `NUMERIC DIGITS/FUZZ/FORM` statement (PR #188, 2026-06-03).
  Writes `wkbi_digits/fuzz/form` in the VM; the arith engine and the OC-ARITH/OC-12
  fast-path gates read them automatically. The gates correctly disable the integer
  fast-path at DIGITS≠9 / FUZZ>0 and must not be removed.

Open items:
- **WP-BC-INT** — bytecode INTERPRET (runtime compilation). Deferred (low
  priority): INTERPRET is absent from BOTH the bytecode and the token-walk path,
  so it is NOT a decommission blocker — the token-walk fallback offers nothing
  here that would be lost. WP-23 (token-walk INTERPRET) is won't-do; INTERPRET
  goes straight to bytecode, tested against the spec / bc_only (no token-walk
  reference will ever exist). Design is captured in the WP-BC-INT ticket
  (CON-17 §8.4: separate EXECBLK per call, recursive compile→execute→free,
  reentrant via heap-allocated bcom_ctx).
- ~~**WP-BC-RT03**~~ — quote de-doubling for string literals (`'p''q''r'` →
  `p'q'r`). **Done** (PR #192, 2026-06-03): `bc_exp8`'s `TOK_STRING` branch now runs
  `bpse_dedouble` when `TOKF_QUOTE_DBL` is set, mirroring the PARSE-template
  path; one fix covers both quote kinds (`'` and `"`). This was a silent
  wrong-output divergence, not a `BC_FAIL_UNSUP` fallback gate. Token-walk (the
  correct reference here) is unchanged per CON-18; equivalence proven in
  `test/tstbdq.c`.
- **WP-CPS-09a-FU** — SIGNAL/CALL condition-trap *activation* (the parser-only
  baseline is done via #131; the runtime trap machinery — NOVALUE hook,
  condition dispatcher, SIGL/RC/CONDITION updates, CALL ON/OFF — is open).
- **Token-walk's own defects** — discovered while building bytecode equivalence
  tests. The token-walk is *not* a gold standard; it is wrong in several places
  where the bytecode is right (the true reference is SC28-1883-0):
  - **WP-TW-DECDO** — `do j=1.1 to 2.2 by 1.1` steps wrong in token-walk.
  - **WP-TW-STEMRESET** — bare-stem assignment doesn't clear tails (bytecode
    fixed via #186; token-walk still has the bug). Academic until/unless
    token-walk is ever used as a path again.
- **Broader stress-testing** — REXXCPS is one program. More programs (INTERPRET-
  heavy, string-heavy, PARSE-heavy) must be bytecode-verified before the fallback
  can be removed.

### Axis 3 — Spec completeness (full SC28-1883-0 / z/OS conformance)

Features beyond the REXXCPS subset. These do not block decommission but are
needed for a *complete* REXX.

- **WP-CPS-06b** — IRXEXEC full spec (EVALBLOCK result capture, multi-argument,
  auto-init env, syntax-error RCs 20001-20099, ABEND detection). The REXXCPS-MVP
  built only the subroutine-call path.
- **WP-CPS-08b** — IRXJCL full spec (sequential-file mode, full EVDATA, step-RC
  bit-masking). Depends on WP-CPS-06b.
- **WP-CPS-08c** — IRXJCL HLASM rewrite (post-MVP).
- **BIF integration** (deferred from WP-21b — these depend on subsystems that
  are partly unbuilt):
  - **TRACE()** — integrate with the trace system
  - **CONDITION()** — integrate with the condition-trap system (needs
    WP-CPS-09a-FU)
  - **ADDRESS()** — integrate with host command environments
  - **QUEUED()** — integrate with the data stack (needs a data stack)
  - **EXTERNALS()** — replace stub with real implementation (needs data stack)
- **IRXARITH refinements** — `num_div_impl` helper refactor, `num_format` maxbuf
  parameter, binary exponentiation for `num_power` (O(log n)), result-exponent
  overflow check per §9.4.5, NOMEM propagation from `irx_arith_compare`, expanded
  edge-case test coverage.
- **Other BIF/language gaps** — VALUE mode 3 (selector argument), LINESIZE real
  implementation (TSO terminal width via WP-33), SOURCELINE MVS verification,
  PARSE VERSION language-level date (TSK-128), IRXBIF hash-based registry lookup,
  IRXBIFS off-stack arrays + BIF_OMITTED_OK flag.

### Axis 4 — Environment / Anchor (status UNCLEAR — needs inventory)

> **2026-07-02 — IRXTERM-from-C-host crash RESOLVED.** The long-standing
> "IRXTERM crashes when called from a crent370 C host" report (consumer:
> httprexx) was root-caused to an as370 assembler pitfall in the caller-side
> shims — RS-format `LM R0,R12,20(,R13)` silently assembles with BASE=0 and
> restores registers from PSA low core. Fixed in `test/trxcall.asm`,
> `test/trxldc.asm` and httprexx `asm/htrxterm.asm`; a second latent bug
> (`asm/istso.asm` EXTRACT S328 on non-zeroed parameter list) was fixed on
> the way. Full analysis: `docs/irxterm-c-host-crash.md`. IRXINIT/IRXEXEC/
> IRXTERM themselves were exonerated. The related **#204** (`env_get_safe`
> S0C4 when IRXINIT is reached via LOAD+BALR from a foreign C host) is now
> fixed: `env_get_safe()` gates `getenv()` on a CLIBCRT being registered for
> the current TCB — a silent replay of `@@CRTGET`'s lookup — rather than a
> bare non-NULL-PPA check. Verified on MVS: TREXXVL case 0d (IRXINIT via
> `__load`+BALR) returns rc=0 with no S0C4, and TSTFLIP (`setenv`/`getenv`
> on a legitimate runtime) stays green.

> ⚠️ **The state of this axis is not currently known.** The core
> IRXINIT/IRXTERM/IRXANCHR/parameter-module work (WP-I1a-d) was marked done
> during the pre-REXXCPS push, but a family of **refinement and verification
> sub-tasks** accumulated afterward and their status was never confirmed against
> the current code. **First action for this axis: an inventory/diagnosis round**
> (same discipline as the task cleanup) to establish what is actually open vs.
> already handled by the REXXCPS work.

Candidates to verify (not a committed work list until inventoried):
- **WP-I1c.5 / WP-I1c.5+** — HLASM entry-point wrappers; IRXINIT/IRXTERM RC=0/4
  differentiation (TCB-aware).
- **WP-I1a.0 / .3 / .4** — ECT-anchor-API refactor (`irxanchr.h` → `ectanchr.h`);
  IRXANCHR slot-management API; IRXTMPW TMP wrapper.
- **Q-tickets** — Q-ECT-01 (ECT semantics under ISPF splits), Q-TERM-01 (IRXTERM
  call convention + slot behaviour), Q-PERSIST-01 (IRXANCHR persistence).
- **is_tso() migration** — `is_tso()` HLASM wrapper (EXTRACT-based); abolish
  `anch_tso()` in favour of `is_tso()`.
- **FINDENVB self-healing cache + cross-subtask discovery.**
- **USERID non-TSO batch** — ACEE/JCT walk replacing the MVSUSER literal fallback.
- **Research** — IRXINT vs IRXINIT engine/API split; replaceable-routine
  load-module strategy (aliases vs own modules).
- **WP-33-TSO** — TSO variant of the I/O replaceable routine (TPUT/TGET, SVC 93).

### Axis 5 — Infrastructure

- **MBT issue #33** — first-class test support in the build tool. The practical
  driver: test programs are re-uploaded on every MVS build, and the upload time
  is the main friction in the build→measure loop. Phase 1 ("tests out of
  `make build`") would shrink uploads the most. See the test/build cleanup CON.
- **Test & build cleanup** — `docs/workpackages.md` and the CLAUDE.md status
  block are stale; the test suite (~34 test modules vs 26 product modules) could
  be tiered. CI ratchet: `clang-format --dry-run --Werror` on every PR.

---

## Document status register

| Document | Status | Note |
|---|---|---|
| CON-12 (Optimization Candidates) | current | Performance axis source of truth; pruned 2026-06-03 |
| CON-20 (Correctness/Decommission) | current | Axis 2 source of truth |
| CON-18 (Token-Walk Freeze) | current | Freeze decision; token-walk-has-own-bugs noted in CON-20 |
| CON-16 (REXXCPS Roadmap) | **closed** | Goal achieved 2026-05-17; superseded by this file |
| CON-1 (Architecture v0.1.6) | **review needed** | Predates bytecode phase; may carry stale assumptions |
| `docs/workpackages.md` | **stale** | Frozen at Phase 3; historical reference only |

---

## How this maps to the task backlog

The Notion Issues & Tasks database holds the per-task detail. After the
2026-06-03 cleanup, the open REXX tasks correspond to the axes above. The old
WP-I epic series (WP-I, WP-I1a-d, WP-I2/I3/I4) is closed — that work migrated
into the WP-CPS-* series during the REXXCPS pivot.

**Recurring rules** (also in CLAUDE.md): a new test is a `[[test]]` block in
`project.toml` (`test/tst*.c`); mbt generates its run JCL, and only pure-HLASM
VLIST-wrapper edge cases (tinitvl, ttermvl, tistso) also need a hand-written
`test/jcl/<name>.jcl`. Commits carry no AI/Co-Authored-By references (project
policy). Diagnose before fix; measure before optimize; verify against the
spec, not against the token-walk.
