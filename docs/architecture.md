# REXX/370 Architecture Design

**Project:** REXX/370 — Interface-compatible REXX interpreter for MVS 3.8j
**Compatibility:** TSO/E Version 2 REXX (SC28-1883-0, December 1988)
**Target platform:** MVS 3.8j / Hercules / MVS/CE
**Version:** 0.2.0-DRAFT
**Date:** 20 April 2026

---

# 1. Summary

## 1.1 Project goal

Implementation of a REXX language interpreter for MVS 3.8j that is **interface-compatible** with the TSO/E Version 2 REXX specification:

- Identical control block layouts (ENVBLOCK, PARMBLOCK, EXECBLK, INSTBLK, EVALBLOCK, SHVBLOCK)
- Compatible programming service entry points (IRXxxxx routines)
- Compatible replaceable routine interfaces (Exec Load, I/O, Host Command, Data Stack, Storage, User ID, Message ID)
- Compatible exit routine interfaces (Init/Term, Exec Init/Term, IRXEXEC exit, Attention Handling)
- Full SAA Procedures Language compliance

## 1.2 Scope and constraints

**MVS 3.8j reality:**

- No TSO/E V2 — we have MVS/TSO (TSO Release ~4–5)
- No ISPF (unless separately installed)
- No MVS/ESA features (no 31-bit, no data spaces)
- 24-bit addressing only (AMODE 24, RMODE 24)
- EBCDIC code page 037/1047

| Feature | Status | Note |
|---|---|---|
| REXX language (SAA CPI subset) | Full | Chapters 2–9 |
| Built-in functions (chapter 4) | Full | All SAA functions |
| TSO/E External Functions | Partial | Adapted to MVS 3.8j |
| REXX commands (chapter 10) | Full | EXECIO, MAKEBUF, etc. |
| Programming Services (chapter 12) | Full | IRXJCL–IRXRLT |
| Language Processor Environments | Full | Chapter 14 |
| Replaceable Routines (chapter 16) | Full | 7 routines |
| Host Cmd Environments | TSO+MVS+LINK | No ATTACH on 3.8j |
| ISPEXEC / ISREDIT | Deferred | Requires ISPF |
| Debug commands | Full | TS, TE, HI, HT, RT |
| DBCS support | Deferred | Appendix B |

## 1.3 Implementation language

**Primary:** C — for the interpreter core, tokenizer, parser, evaluator, and all phase-2+ modules.
**Secondary:** HLASM — for control block DSECTs, SVC interfaces, and platform-specific entry points where direct system service access is required.

---

# 2. Architecture overview

```text
┌──────────────────────────────────────────────────────────┐
│                   REXX/370 Interpreter                   │
├──────────────────────────────────────────────────────────┤
│  IRXINIT / IRXEXEC / IRXTERM (API layer)                 │
├──────────────────────────────────────────────────────────┤
│  Language Processor Environment (LPE)                    │
│  ENVBLOCK · PARMBLOCK · Work Block Extension             │
│  REXX Vector of External Entry Points                    │
├──────────────────────────────────────────────────────────┤
│  Interpreter core                                        │
│  Tokenizer · Parser · Evaluator                          │
│  Variable Pool · Stack Manager · Arithmetic engine       │
├──────────────────────────────────────────────────────────┤
│  Service layer                                           │
│  IRXEXCOM · IRXSUBCM · IRXIC · IRXRLT                    │
│  Built-in Functions · TSO/E External Functions           │
├──────────────────────────────────────────────────────────┤
│  Replaceable Routines                                    │
│  Exec Load · I/O · Host Cmd · Stack · Storage            │
│  User ID · Message ID                                    │
├──────────────────────────────────────────────────────────┤
│  MVS 3.8j system interface                               │
│  GETMAIN/FREEMAIN · OPEN/CLOSE/GET/PUT · BLDL/LOAD       │
│  LINK · WTO/WTOR · STIMER · TGET/TPUT · ENQ/DEQ          │
└──────────────────────────────────────────────────────────┘
```

---

# 3. Control block definitions

All control blocks must be byte-exact with the TSO/E V2 REXX layouts.

## 3.1 Environment Block (ENVBLOCK)

Anchor for a Language Processor Environment. Ref: SC28-1883-0 chapter 14, p. 323; cross-verified against SC28-1883-4 (Aug 1991) and [z/OS 2.5 online](https://www.ibm.com/docs/en/zos/2.5.0?topic=environment-format-block-envblock).

Offsets 0..303 are byte-exact with all IBM editions from SC28-1883-0 (Dec 1988) through z/OS 2.5. Bytes 304..319 are reserved in every documented IBM edition and have remained so for 37+ years; rexx370 keeps them as private reserved space for future use. Total length stays at 320 bytes, matching `ENVBLOCK_LENGTH`.

| Offset | Length | Field | Description |
|---|---|---|---|
| +00 | 8 | `ENVBLOCK_ID` | Eye-catcher: 'ENVBLOCK' (character) |
| +08 | 4 | `ENVBLOCK_VERSION` | Version: '0100' (character) |
| +0C | 4 | `ENVBLOCK_LENGTH` | Block length (= 320) |
| +10 | 4 | `ENVBLOCK_PARMBLOCK` | Pointer to PARMBLOCK |
| +14 | 4 | `ENVBLOCK_USERFIELD` | User field (caller-supplied to IRXINIT) |
| +18 | 4 | `ENVBLOCK_WORKBLOK_EXT` | Pointer to Work Block Extension |
| +1C | 4 | `ENVBLOCK_IRXEXTE` | Pointer to REXX Vector of External Entry Points |
| +20 | 4 | `ENVBLOCK_ERROR_CALL` | Address of routine that issued the first error |
| +24 | 4 | — | Reserved |
| +28 | 8 | `ENVBLOCK_ERROR_MSGID` | Message ID of first error (character) |
| +30 | 80 | `ENVBLOCK_PRIMARY_ERROR_MESSAGE` | Primary error message text |
| +80 | 160 | `ENVBLOCK_ALTERNATE_ERROR_MESSAGE` | Alternate error message text |
| +120 | 4 | `ENVBLOCK_COMPGMTB` | Compiler programming table (= 0 in rexx370) |
| +124 | 4 | `ENVBLOCK_ATTNROUT_PARMPTR` | Attention handler control block (= 0 in rexx370 Phase 1) |
| +128 | 4 | `ENVBLOCK_ECTPTR` | Back-pointer to the ECT this env is anchored in; populated for TSO envs, 0 otherwise |
| +12C | 4 | `ENVBLOCK_INFO_FLAGS` | Status bits. Bit 0 (TERMA_CLEANUP) = abnormal termination in progress (set by IRXTERMA). Bits 1..31 reserved |
| +130 | 16 | — | rexx370-private, reserved for future use. Lives in the IBM-reserved range 304..319 that has been undocumented in every edition from SC28-1883-0 (Dec 1988) through z/OS 2.5 (2024) |

```hlasm
ENVBLOCK DSECT
ENVBID     DS    CL8          Eye-catcher 'ENVBLOCK'
ENVBVER    DS    CL4          Version '0100'
ENVBLEN    DS    F            Length of ENVBLOCK (=320)
ENVBPARM   DS    A            -> PARMBLOCK
ENVBUSER   DS    A            User field (caller-supplied)
ENVBWKEX   DS    A            -> Work Block Extension
ENVBIRXE   DS    A            -> REXX Vector of External Entry Points
ENVBECAL   DS    A            Error call routine address
           DS    F            Reserved
ENVBEMID   DS    CL8          Error message ID
ENVBEMSG   DS    CL80         Primary error message
ENVBAMSG   DS    CL160        Alternate error message
ENVBCPGM   DS    A            Compiler programming table (=0 in rexx370)
ENVBATTN   DS    A            Attention handler CB (=0 in rexx370)
ENVBECTP   DS    A            -> ECT (TSO envs only, 0 otherwise)
ENVBINFO   DS    F            INFO_FLAGS; bit 0 = TERMA_CLEANUP
*          rexx370 private; IBM bytes 304..319 are reserved in every
*          SC28-1883 edition from Dec 1988 through z/OS 2.5.
           DS    CL16         Reserved for rexx370 future use
ENVBSIZE   EQU   *-ENVBLOCK   Total size = 320
```

## 3.2 Parameter Block (PARMBLOCK)

Parameters of the Language Processor Environment. Ref: chapter 14, p. 324.

| Offset | Length | Field | Description |
|---|---|---|---|
| +00 | 4 | `PARM_ID` | Eye-catcher: 'PARM' |
| +04 | 4 | `PARM_LENGTH` | Block length |
| +08 | 4 | `PARM_VERSION` | Version number |
| +0C | 4 | `PARM_FLAGS` | Flags |
| +10 | 4 | `PARM_MASKS` | Masks |
| +14 | 8 | `PARM_MODNAMET` | Module Name Table name |
| +1C | 8 | `PARM_HOSTENV` | Default host command env |
| +24 | 8 | `PARM_DSNLOAD` | Load DD-name |
| +2C | 4 | `PARM_FUNCPKG` | Pointer to Function Package Table |
| +30 | 4 | `PARM_HOSTCMD` | Pointer to Host Cmd Env Table |
| +34 | 4 | `PARM_MODN_PTR` | Pointer to Module Name Table |
| +38 | 4 | `PARM_NUMENVS` | Max. environments |

## 3.3 Exec Block (EXECBLK)

Identification of the exec to be executed. Ref: chapter 12, p. 220.

| Offset | Length | Field | Description |
|---|---|---|---|
| +00 | 8 | `EXECBLK_APTS` | Eye-catcher: 'IRXEXECB' |
| +08 | 4 | `EXECBLK_LENGTH` | Length |
| +10 | 8 | `EXECBLK_MEMBER` | Member name |
| +18 | 8 | `EXECBLK_DDNAME` | DD-name |
| +20 | 8 | `EXECBLK_SUBCOM` | Initial host cmd env |
| +28 | 4 | `EXECBLK_DSNPTR` | Pointer to DSN |
| +2C | 4 | `EXECBLK_DSNLEN` | DSN length |

## 3.4 In-Storage Control Block (INSTBLK)

REXX exec as in-storage block. Ref: chapter 12, p. 222.

| Offset | Length | Field | Description |
|---|---|---|---|
| +00 | 8 | `INSTBLK_ACRONYM` | Eye-catcher: 'IRXINSTB' |
| +08 | 4 | `INSTBLK_LENGTH` | Header length (=128) |
| +10 | 4 | `INSTBLK_STESSION` | Pointer to exec image |
| +14 | 4 | `INSTBLK_STESSION_LEN` | Length of exec image |
| +18 | 8 | `INSTBLK_MEMBER` | Member name |
| +20 | 8 | `INSTBLK_DDNAME` | DD-name |
| +70 | 4 | `INSTBLK_NUMREC` | Number of records |
| +74 | 4 | `INSTBLK_RECORD_TBL` | Pointer to record table |

## 3.5 Evaluation Block (EVALBLOCK)

Result return from IRXEXEC. Ref: chapter 12, p. 225.

| Offset | Length | Field | Description |
|---|---|---|---|
| +00 | 4 | `EVALBLOCK_EVPAD1` | Reserved |
| +04 | 4 | `EVALBLOCK_EVSIZE` | Total size (doublewords) |
| +08 | 4 | `EVALBLOCK_EVLEN` | Length of data in EVDATA |
| +0C | 4 | `EVALBLOCK_EVPAD2` | Reserved |
| +10 | var | `EVALBLOCK_EVDATA` | Result data |

> **Note:** EVLEN = X'80000000' means "no result".

## 3.6 Shared Variable Block (SHVBLOCK)

Variable access via IRXEXCOM. Ref: chapter 12, p. 241.

| Offset | Length | Field | Description |
|---|---|---|---|
| +00 | 4 | `SHVNEXT` | Next SHVBLOCK (or 0) |
| +04 | 4 | `SHVUSER` | User field |
| +08 | 4 | `SHVCODE` | Function code |
| +0C | 1 | `SHVRET` | Return code flags |
| +10 | 4 | `SHVNAMA` | Pointer to variable name |
| +14 | 4 | `SHVNAML` | Name buffer length |
| +18 | 4 | `SHVVALA` | Pointer to value buffer |
| +1C | 4 | `SHVVALL` | Value buffer length |
| +20 | 4 | `SHVNAMELEN` | Actual name length |
| +24 | 4 | `SHVVALELEN` | Actual value length |

**SHVCODE function codes:** S=Set, F=Fetch, D=Drop, s=SymSet, f=SymFetch, d=SymDrop, N=Next, P=Private.

## 3.7 Work Block Extension

Runtime data of the environment. Ref: chapter 14, p. 326.

| Offset | Field | Description |
|---|---|---|
| +00 | `WKBK_ID` | Eye-catcher |
| +08 | `WKBK_ENVBLOCK` | Back-pointer to ENVBLOCK |
| +0C | `WKBK_DATASTACK` | Data stack anchor |
| +10 | `WKBK_TRACEFLG` | TRACE setting |
| +14 | `WKBK_NUMDIGITS` | NUMERIC DIGITS |
| +18 | `WKBK_NUMFUZZ` | NUMERIC FUZZ |
| +1C | `WKBK_NUMFORM` | NUMERIC FORM |
| +28 | `WKBK_VARPOOL` | Variable pool |

## 3.8 REXX Vector of External Entry Points

Function pointer table for all replaceable routines. Ref: chapter 14, p. 328.

| Offset | Field | Target |
|---|---|---|
| +00 | `IRXEXECV_EXEC` | IRXEXEC |
| +04 | `IRXEXECV_EXCOM` | IRXEXCOM |
| +08 | `IRXEXECV_SAY` | SAY routine |
| +0C | `IRXEXECV_TRACE` | TRACE routine |
| +10 | `IRXEXECV_PULL` | PULL routine |
| +14 | `IRXEXECV_LOAD` | Exec Load |
| +18 | `IRXEXECV_IO` | I/O routine |
| +1C | `IRXEXECV_HOSTCMD` | Host Cmd routine |
| +20 | `IRXEXECV_STACK` | Data Stack |
| +24 | `IRXEXECV_STORAGE` | Storage Mgmt |
| +28 | `IRXEXECV_USERID` | User ID |
| +2C | `IRXEXECV_MSGID` | Message ID |
| +30 | `IRXEXECV_SUBCOM` | IRXSUBCM |
| +34 | `IRXEXECV_IC` | IRXIC |
| +38 | `IRXEXECV_RLT` | IRXRLT |
| +3C | `IRXEXECV_INIT` | IRXINIT |
| +40 | `IRXEXECV_TERM` | IRXTERM |

---

# 4. Programming Services (IRXxxxx)

## 4.1 IRXJCL — batch execution

Parses EXEC PARM, initializes non-TSO environment via IRXINIT, calls IRXEXEC, cleans up with IRXTERM.

```jcl
//REXX     EXEC PGM=IRXJCL,PARM='exec_name parm1 parm2'
//SYSEXEC  DD   DSN=MY.REXX.EXEC,DISP=SHR
//SYSTSPRT DD   SYSOUT=*
//SYSTSIN  DD   DUMMY
```

## 4.2 IRXEXEC — execute an exec

Main interface. Parameters: EXECBLK_PTR, ARGLIST_PTR, FLAGS, INSTBLK_PTR, CPPL_PTR, EVALBLK_PTR, WKAREA_PTR, USERFIELD_PTR, ENVBLOCK_PTR.

**Return codes:** 0=OK, 4=RC>=1, 20=not found, 28=env not found, 32=invalid plist, -3=host cmd not found.

## 4.3 IRXEXCOM — variable access

Walks the SHVBLOCK chain, performs Set/Fetch/Drop/Next on the variable pool.

## 4.4 IRXSUBCM — subcommand table

ADD, DELETE, QUERY, UPDATE for host command environments.

## 4.5 IRXIC — trace and execution control

Controls TRACE and execution status. Used by TS/TE/HI/HT/RT.

## 4.6 IRXRLT — retrieve result

Functions: GETRLTE, GETRL, GETBLOCK.

---

# 5. Replaceable Routines

## 5.1 Exec Load Routine

1. BLDL from SYSEXEC/SYSPROC
2. Read via BSAM/QSAM
3. Build INSTBLK record table
4. Free with FREEMAIN

**Search order:** SYSEXEC → SYSPROC (with REXX identifier) → ALTLIB (future).

## 5.2 I/O Routine

Functions: RXFWRITE (SAY), RXFREAD (PULL), RXFREADP (stack+terminal), RXFTWRITE (trace), RXFWRITERR, RXFOPEN, RXFCLOSE, RXFREAD_DS, RXFWRITE_DS.

MVS 3.8j: TGET/TPUT (TSO) or WTO/WTOR (batch). EXECIO via QSAM.

## 5.3 Host Command Environment Routine

| Environment | Implementation |
|---|---|
| TSO | Build CPPL, IKJPARS |
| MVS | REXX commands + exec invocation |
| LINK | SVC 6 |
| ATTACH | SVC 42 (restricted) |

See chapter 8 for host command integration details.

## 5.4 Data Stack Routine

PUSH/QUEUE/PULL, MAKEBUF/DROPBUF, NEWSTACK/DELSTACK, QBUF/QELEM/QSTACK. Nested doubly-linked lists.

## 5.5 Storage Management

GETMAIN R / FREEMAIN R, subpool 0.

## 5.6 User ID

TSO: PSCBUSER. Non-TSO: ACEE/JCT.

## 5.7 Message ID

Standard prefix: 'IRX'.

---

# 6. Language Processor Environments

## 6.1 Environment anchor (ECTENVBK)

Each REXX Language Processor Environment is anchored in the TSO Environment Control Table (ECT) at offset +30 (`ECTENVBK`) — not in TCBUSER. The ECT lies in user-accessible TSO work storage and is problem-state-writable; TCBUSER would require APF authorization and offers no behavioural advantage on MVS 3.8j. A separate anchor control block (RAB) is not required and not used.

### Read-mostly discipline

rexx370 follows a **read-mostly** discipline for `ECTENVBK`: IRXINIT writes the slot only when it is NULL (no other REXX has claimed it); subsequent IRXINIT calls return the new ENVBLOCK pointer to the caller without touching the anchor. IRXTERM clears the slot only if it still points at the terminating ENVBLOCK; otherwise it leaves the anchor alone.

The motivation is **coexistence with other REXX implementations on the same task**, not default-environment protection. MVS 3.8j ships without IBM REXX, so there is no automatic default environment for rexx370 to protect. ECTENVBK can be in exactly three real states on this platform:

- (a) **NULL** — nobody has taken the slot yet; safe for us to claim.
- (b) **Non-NULL, pointing at a BREXX environment** currently active on this task — BREXX would crash if we overwrote its anchor.
- (c) **Non-NULL, pointing at an earlier rexx370 environment we set ourselves** — we already hold that pointer through the IRXINIT return value.

"Only write when `ECTENVBK == 0`" is the correct rule in all three cases. See CON-1 §6.1 and §14.2 for the full rationale, and SC28-1883-0 §15 for the caller-managed pointer-passing contract for reentrant environments.

### Cold-path walk

To discover the ECT (and whether we're in a TSO context at all), rexx370 walks the standard TSO control-block chain. Offsets are from IBM macros (SYS1.MACLIB / Data Areas manuals), not from SC28-1883:

```text
PSA  + PSAAOLD  (0x224) -> ASCB
ASCB + ASCBASXB (0x06C) -> ASXB
ASXB + ASXBLWA  (0x014) -> LWA
LWA  + LWAPECT  (0x020) -> ECT
ECT  + ECTENVBK (0x030) -> ENVBLOCK (current anchor)
```

This walk is empirically validated by BREXX/370 (in production on Hercules MVS 3.8j since 2019) and re-verified for rexx370 in PR #45 across TSO-foreground, TSO-background, and pure-batch scenarios. In batch any link (typically `ASXBLWA`) can be NULL — the walk returns NULL, and IRXINIT reduces to allocating a local ENVBLOCK returned by reference.

### Hot path and `ENVBLOCK_ECTPTR`

Once initialized, the ENVBLOCK pointer is passed as an explicit parameter (register 0 in the IBM ABI, `p->envblock` in rexx370's C-internal convention) to every service routine, replaceable routine, and BIF. The cold-path walk runs at most once per IRXINIT. `ENVBLOCK_ECTPTR` (offset +296) is populated during IRXINIT so routines holding an ENVBLOCK pointer can reach the ECT without re-walking PSA — useful for IRXUID and future ACEE-based replaceable routines.

### Non-TSO environments

For batch jobs started by JES2 (future Phase 5 IRXJCL), no persistent anchor exists. The ENVBLOCK is created locally by IRXJCL; `ECTENVBK` and `ENVBLOCK_ECTPTR` stay 0. The pointer is passed by reference through all IRXxxxx service calls as parameter — SC28-1883-0-compliant, since every IRXxxxx signature includes an ENVBLOCK pointer argument.

See `include/irxanchr.h` and `src/irx#anch.c` for the anchor API, and CON-1 §3.1 / §6.1 for the spec-level definition.

## 6.2 Environment types

| Type | TSO-integrated | Address space |
|---|---|---|
| TSO/E integrated | Yes (TSOFL=1) | TSO foreground/background |
| Non-TSO/E | No (TSOFL=0) | Batch, started tasks |
| ISPF integrated | Yes + ISPF flags | ISPF environment (future) |

### Type detection

IRXINIT determines the type via a two-tier strategy:

1. **Explicit caller override.** If the caller passes a non-NULL PARMBLOCK with the flags field set, those flags are authoritative.
2. **Auto-detection fallback.** With a NULL PARMBLOCK, IRXINIT detects from the runtime context:
   - **TSO / non-TSO:** `anch_tso()` tests `CLIBPPA.ppaflag & (PPAFLAG_TSOFG | PPAFLAG_TSOBG)` via `__ppaget()`. `PPAFLAG_TSOFG` marks TSO foreground (interactive READY prompt), `PPAFLAG_TSOBG` marks TSO background (batch job driving IKJEFT01 / IRXJCL). Pure batch (`EXEC PGM=...` directly, no TSO TMP) leaves both clear. The structurally equivalent check is `anch_walk() != NULL` — if the cold-path walk succeeds an ECT exists; in pure batch `ASXBLWA` is NULL and the walk returns NULL. Both indicators yield the same truth value on MVS 3.8j; the anchor library uses the walk as its gate because it directly reflects the structural fact we care about.
   - **ISPF (future):** `BLDL` for `ISPQRY`; if found, `CALL ISPQRY` and check return code. Phase 1 is TSO-only; ISPF detection is documented here and implemented when ISPF support enters scope.

**Empirical finding (TSK-3463 Phase C, 20 April 2026, PR #45):** the similarly-named bits in `CLIBCRT.crtflag` (the per-task runtime struct) carry identical field names but are never populated by crent370 startup — TSO detection lives at the process level (CLIBPPA), not per-task (CLIBCRT). The three scenarios validated on Hercules MVS 3.8j: TSO-foreground `ppaflag=0xC0` (TSOFG+TSOBG set, walk non-NULL), TSO-background `ppaflag=0x40` (TSOBG only, walk non-NULL), pure batch `ppaflag=0x00` (both clear, walk NULL).

## 6.3 Initialization (IRXINIT)

1. Parameter validation
2. Allocate ENVBLOCK via storage management routine
3. Allocate and fill PARMBLOCK (from parameters module or in-storage parameter list)
4. Allocate Work Block Extension
5. Build REXX Vector of External Entry Points (load Module Name Table, resolve each replaceable routine via BLDL/LOAD, store addresses in the vector)
6. Initialize Host Command Environment Table (default entries: TSO, MVS, LINK, ATTACH)
7. Initialize Function Package Table
8. Anchor initialization (see §6.1): for TSO environments, populate `ENVBLOCK_ECTPTR` (+296) with the ECT address obtained from the cold-path walk. If `ECTENVBK` is currently 0, also write the new ENVBLOCK's address to `ECTENVBK`. If `ECTENVBK` is non-zero, leave it alone — the caller is responsible for tracking the new pointer explicitly per the SC28 reentrant-env contract. For non-TSO environments this step is a no-op.
9. Call initialization exit (if defined)
10. Return ENVBLOCK pointer to caller

## 6.4 Termination (IRXTERM)

Mirror of §6.3:

1. Call termination exit (if defined) — Phase 6
2. Anchor cleanup (see §6.1): for TSO environments, if `ECTENVBK` currently points at this ENVBLOCK, clear it to 0. If the terminating ENVBLOCK is not the one in `ECTENVBK` — the typical case for explicit `IRXINIT`/`IRXTERM` from caller code, where the new ENVBLOCK never occupied the anchor slot — `ECTENVBK` is left unchanged. For non-TSO environments this step is a no-op.
3. Free Function Package Table, Host Command Environment Table, REXX Vector of External Entry Points
4. Free Work Block Extension
5. Free PARMBLOCK
6. Free ENVBLOCK

---

# 7. Interpreter core

## 7.1 Execution model

Load exec → tokenize (single pass) → execute clause by clause → return result via EVALBLOCK.

## 7.2 Variable pool

Hash table for O(1) access. Compound variables resolved via derived names. PROCEDURE/EXPOSE for scoping.

## 7.3 Arithmetic engine (IRXARITH)

Module IRXARITH implements the full REXX arithmetic per SC28-1883-0 §9. All arithmetic operations and all non-strict numeric comparisons go exclusively through this engine — the parser/evaluator calls it, but knows neither the number representation nor the rounding rules itself.

### 7.3.1 Number representation

Character BCD: one digit per byte (`'0'`–`'9'`), sign and exponent as separate fields in the number struct. Software-implemented in C. Consequence: arbitrary precision (memory-limited only), identical behavior on Linux cross-compile and Hercules/MVS, hardware packed decimal instructions are not used.

### 7.3.2 NUMERIC settings

- **DIGITS** — default 9, maximum 1,000. Values > 1,000 → SYNTAX condition. The spec maximum would be 999,999,999; the practical cap covers all realistic use cases.
- **FUZZ** — default 0. Affects non-strict comparisons (`=`, `\=`, `<`, `>`, `<=`, `>=`), but not `==` or `\==`. Must be smaller than DIGITS.
- **FORM** — default SCIENTIFIC, alternative ENGINEERING. Affects only the string representation of numbers in exponential notation, not the internal arithmetic.

All three values live in the Work Block Extension (`wkbk_numdigits`, `wkbk_numfuzz`, `wkbk_numform`) and are read by IRXARITH from the wkblk on each operation.

### 7.3.3 Interfaces

- `irx_arith_op(a, b, opcode, result, wk)` — all binary and unary arithmetic operations (ADD, SUB, MUL, DIV, INTDIV, MOD, POWER, NEGATE, PLUS) through a single entry point with opcode dispatch.
- `irx_arith_compare(a, b, wk, result)` — non-strict comparisons with FUZZ handling. Strict comparisons (`==`, `\==`) stay in the parser as exact string compare.
- `irx_number_to_lstr(n, out, wk)` — central number-to-string conversion. Chooses internally between fixed-point and exponential notation per §9.4 and applies FORM when exponential.

Internal format helpers are built with an explicit parameter profile so that the later `FORMAT()` BIF (WP-21/22) can reuse them directly.

### 7.3.4 Error handling

Errors are reported in a `wkbi_last_condition` slot in the Work Block Extension (code, subcode, description, valid). Error codes are collected as constants in `include/irxcond.h` per SC28-1883-0:

- **26.11** — Numeric overflow/underflow (exponent after operation > 999,999,999)
- **41.1** — Bad arithmetic conversion (non-numeric operand)
- **42.3** — Arithmetic overflow; divide by zero

Per SC28-1883-0 §9.4.5, a deliberate asymmetry applies: exponent overflow → SYNTAX 26.11, exponent underflow → result = 0 (no error).

The parser propagates errors upward and terminates the exec with the corresponding RC. The full condition trap infrastructure (SIGNAL ON / CALL ON, CONDITION BIF) will be built in WP-61 on top of this reporting mechanism.

### 7.3.5 Optimizations

All operations use the BCD path exclusively — no fast paths for integer or IEEE float. Potential optimizations (integer fast path, packed decimal for DIGITS ≤ 31) are tracked in the Optimization Candidates catalog (Notion) and will be pursued only based on profiling data.

### 7.3.6 Spec references

SC28-1883-0 §9 (Numbers and Arithmetic), §7.5.4 (NUMERIC DIGITS), §7.5.5 (NUMERIC FUZZ), §7.5.6 (NUMERIC FORM), §9.4.5 (Exponent overflow), §9.6 (String comparison). Implementation: WP-20.

## 7.4 Parsing engine

PARSE sources: ARG, PULL, EXTERNAL, SOURCE, VALUE, VAR, VERSION, NUMERIC.

---

# 8. Host command integration

## 8.1 TSO host command environment

On MVS 3.8j (with MVS/TSO), we must interface with TSO command processors. This requires building a CPPL (Command Processor Parameter List) with pointers to CBUF, UPT, PSCB, and ECT. The command string is passed as CPPL; the return code is set in the special variable RC.

## 8.2 MVS host command environment

The MVS environment handles REXX commands (EXECIO, MAKEBUF, etc.) and exec invocations. Return code -3 is returned when the command is not found.

## 8.3 LINK host command environment

Parses the command string, builds the parameter list, executes SVC 6 (LINK) to the module. System abends are converted into negative decimal numbers, user abends into positive decimal numbers, and both are placed in RC.

---

# 9. REXX commands (chapter 10)

## 9.1 EXECIO

The most complex REXX command. Performs sequential I/O on MVS datasets. Syntax: `EXECIO lines DISKR/DISKW/DISKRU ddname [linenum] [(options]`. Implementation on MVS 3.8j via QSAM or BSAM, with data transfer between dataset and the REXX data stack or STEM variables.

## 9.2 Stack commands

MAKEBUF, DROPBUF, NEWSTACK, DELSTACK, QBUF, QELEM, QSTACK — all implemented via the Data Stack replaceable routine.

## 9.3 Debug commands

| Command | Description |
|---|---|
| TS | Trace Start — enable interactive tracing |
| TE | Trace End — disable interactive tracing |
| HI | Halt Interpretation — set HALT condition |
| HT | Halt Typing — suppress terminal output |
| RT | Resume Typing — resume terminal output |

These are implemented as immediate commands — they can be entered at the terminal while a REXX exec is running (via attention handling).

---

# 10. Built-in functions

## 10.1 SAA CPI functions (mandatory)

All SAA Procedures Language functions must be implemented: ABBREV, ABS, ADDRESS, ARG, BITAND, BITOR, BITXOR, CENTER/CENTRE, COMPARE, CONDITION, COPIES, C2D, C2X, DATATYPE, DATE, DELSTR, DELWORD, DIGITS, D2C, D2X, ERRORTEXT, EXTERNALS, FIND, FORM, FORMAT, FUZZ, INDEX, INSERT, JUSTIFY, LASTPOS, LEFT, LENGTH, LINESIZE, MAX, MIN, OVERLAY, POS, QUEUED, RANDOM, REVERSE, RIGHT, SIGN, SOURCELINE, SPACE, STRIP, SUBSTR, SUBWORD, SYMBOL, TIME, TRACE, TRANSLATE, TRUNC, USERID, VALUE, VERIFY, WORD, WORDINDEX, WORDLENGTH, WORDPOS, WORDS, XRANGE, X2C, X2D.

## 10.2 TSO/E External Functions

| Function | MVS 3.8j status | Note |
|---|---|---|
| LISTDSI | Partial | DSCB readable from VTOC, no SMS |
| MSG | Full | Control TSO message display |
| OUTTRAP | Full | Capture command output into stem |
| PROMPT | Full | Control prompting mode |
| STORAGE | Full | Read/write virtual storage (APF!) |
| SYSDSN | Full | Check dataset existence via LOCATE/OBTAIN |
| SYSVAR | Partial | Many SYSVAR variables are TSO/E V2-specific |

## 10.3 Function search order

1. Internal functions (labels within the exec) — skipped if the function name is quoted
2. Built-in functions
3. Function packages: (a) user package, (b) local package, (c) system package
4. External functions: (a) SYSEXEC DD, (b) SYSPROC DD (with REXX identifier check), (c) load library (BLDL)

---

# 11. Conditions and condition traps

| Condition | Trigger |
|---|---|
| ERROR | Host command returns non-zero RC |
| FAILURE | Host command fails (RC < 0 or abend) |
| HALT | External halt request (HI command, attention) |
| NOVALUE | Reference to an uninitialized variable (SIGNAL ON NOVALUE) |
| SYNTAX | Language syntax error during execution |

**SIGNAL ON:** transfers control to a label (like GOTO). Any active DO/SELECT structures are terminated.

**CALL ON:** calls a label as a subroutine. Surrounding structures remain intact.

The condition reporting infrastructure (wkbi_last_condition slot, error codes in include/irxcond.h) is established as part of WP-20 (see section 7.3.4). The full trap handler mechanism comes in WP-61.

---

# 12. Module structure (~200 KB total)

| Module | Description | Phase |
|---|---|---|
| IRXJCL | Batch entry | 5 |
| IRXINIT | Env init | 1 |
| IRXTERM | Env term | 1 |
| IRXEXEC | Main interpreter (40K) | 2 |
| IRXEXCOM | Variable access | 5 |
| IRXSUBCM | Subcmd table | 5 |
| IRXIC | Trace control | 5 |
| IRXRLT | Result retrieval | 5 |
| IRXLOAD | Exec load | 1 |
| IRXIO | I/O | 2 |
| IRXHCMD | Host command | 4 |
| IRXSTK | Data stack | 4 |
| IRXSTOR | Storage mgmt | 1 |
| IRX#ANCH | ECTENVBK anchor (read-mostly) | 1 |
| IRXUID | User ID | 1 |
| IRXMSGID | Message ID | 1 |
| IRXTOKN | Tokenizer | 2 |
| IRXPARS | Parser/evaluator (20K) | 2 |
| IRXARITH | Arithmetic (12K) | 3 |
| IRX#COND | Condition reporting (shared) | 3 |
| IRX#BIF | BIF registry infrastructure | 3 |
| IRX#BIFS | Built-in functions — string + numeric/conversion/reflection/environment (~30K) | 3 |
| IRXEFN | TSO/E ext fns | 7 |
| IRXCMD | REXX commands | 4 |
| IRXMSG | Messages | 7 |

---

# 13. Implementation milestones

## Phase 1: foundation

- [x] Control block DSECTs
- [x] IRXINIT / IRXTERM
- [x] Storage management
- [x] Environment anchor management (ECTENVBK, read-mostly discipline)

## Phase 2: interpreter core

- [x] Tokenizer + parser + evaluator
- [x] Variable pool, PARSE
- [x] DO/IF/SELECT, CALL/RETURN, EXIT, SIGNAL
- [x] PROCEDURE (EXPOSE)
- [x] "Hello World" runs end-to-end

## Phase 3: arithmetic and functions

- [ ] Arbitrary-precision arithmetic
- [ ] All built-in functions
- [ ] INTERPRET

## Phase 4: I/O and commands

- [ ] EXECIO, data stack
- [ ] Host command environments

## Phase 5: Programming Services

- [ ] IRXEXEC, IRXJCL, IRXEXCOM, IRXSUBCM, IRXRLT, IRXIC

## Phase 6: customizing and exits

- [ ] Replaceable routine installation
- [ ] Module Name / Function Package tables
- [ ] Exit routines

## Phase 7: polish

- [ ] TSO/E External Functions
- [ ] Condition traps, TRACE, debug
- [ ] Compliance testsuite

---

# 14. Design decisions

## 14.1 Open

1. **Storage subpool:** Subpool 0 vs. dedicated (78). STORAGE() function requires APF.
2. **Exec caching:** LRU cache per environment recommended.
3. **REXX identifier:** First record `/*...REXX...*/` → REXX exec.
4. **Reentrancy:** All modules RENT. Working storage only via GETMAIN.

## 14.2 Resolved

- **C as implementation language (Phase 2+):** Confirmed by completed Phase 2 (16 April 2026). The entire interpreter chain is implemented in C. Decision confirmed as part of the WP-20 discussion (point B1). The original option "Phase 1–2 HLASM only, evaluate from Phase 3" was already not taken in Phase 1.
- **24-bit memory handling for arithmetic:** Through the `NUMERIC DIGITS` cap of 1,000 (see section 7.3), the arithmetic engine's memory footprint stays in the kilobyte range even with multiple concurrent intermediate results. Overlay not required. Decided as part of the WP-20 discussion (point B2).
- **Environment anchor on MVS 3.8j — read-mostly ECTENVBK (20 April 2026).** rexx370 anchors the REXX environment in the TSO ECT (`ECTENVBK` slot, IKJECT offset `0x30`), not in TCBUSER. The write discipline is read-mostly: `ECTENVBK` is set at most once (when the slot is 0) and never overwritten thereafter by rexx370. Subsequent explicit IRXINIT calls return an ENVBLOCK pointer without touching the anchor; IRXTERM clears `ECTENVBK` only when it still points at the terminating ENVBLOCK. Motivation is coexistence with BREXX (which shares the same slot on MVS 3.8j), not default-environment protection — MVS 3.8j ships without IBM REXX, so the only way `ECTENVBK` is ever non-zero is because BREXX or an earlier rexx370 put it there, and in both cases "do not overwrite" is the correct rule. ENVBLOCK offsets 0..303 are byte-exact with SC28-1883-0, SC28-1883-4, and z/OS 2.5; the +304..+319 range stays fully reserved. Problem-state-writable; follows the BREXX/370 anchor pattern in production on Hercules since 2019. Fully implemented and verified as of 20 April 2026: PR #45 shipped Phase A/B (push/pop baseline); the read-mostly switchover followed; PR #46 (commit d868b46) added `test/test_anchor_readmostly.c` covering (a) empty-slot baseline, (b) BREXX-simulated non-NULL slot — read-mostly correctly does not overwrite, (c) own-env stacking — second IRXINIT does not disturb the first anchor. MVS smoketests via TSTANCH remain green in all three TSO/batch scenarios. See §3.1 and §6.1.
- **Environment type detection — `ppaflag` primary, cold-path walk as structural proxy (20 April 2026).** `anch_tso()` tests `CLIBPPA.ppaflag & (PPAFLAG_TSOFG | PPAFLAG_TSOBG)` via `__ppaget()`. The structurally equivalent check is `anch_walk() != NULL`; the anchor library uses the walk as its gate. Empirical finding: the similarly-named bits in `CLIBCRT.crtflag` (per-task runtime struct) are never populated by crent370 startup and must not be used — TSO detection lives at the process level (CLIBPPA), not per-task. Validated in PR #45 across three scenarios (TSO foreground, TSO background, pure batch). See §6.2.

## 14.3 Design principles (emergent)

**Reference interpreter behaviour is authoritative; the spec is supporting evidence.** When SC28-1883-0 and the deployed TSO/E V2 REXX implementation diverge, the reference interpreter wins. rexx370's compatibility target is byte-exact interface compatibility with the IBM implementation on MVS, not with a deductive reading of the spec.

This principle emerged during WP-21b Phase C review. Both reviewers argued from SC28-1883-0 Appendix E that `MAX(,1)` (omitted operand) and `MAX('', 1)` (empty-string operand) should raise distinct SYNTAX conditions (40.1 and 41.1 respectively). Empirical test against TSO/E REXX on MVS showed both forms produce `IRX0040I` (SYNTAX 40, "Incorrect call to routine"). The deductive reading was wrong; the current behaviour (both → 40.1) is byte-compatible and correct.

**Operational consequence:** for contested edge cases, verify against the reference interpreter before filing as a defect. This is especially relevant for phases still ahead — EXECIO return codes, host-command integration, STEM semantics — where spec and implementation are known to diverge in subtle ways.

---

# 15. Test strategy

| Category | Description | Estimated count |
|---|---|---|
| Language conformance | SAA CPI compliance tests | 500+ |
| Built-in functions | Each function, edge cases | 300+ |
| Control blocks | Layout verification | 50+ |
| Service interfaces | IRXEXEC, IRXEXCOM, etc. | 100+ |
| EXECIO | I/O operations | 80+ |
| Data stack | Stack operations | 50+ |
| Host commands | TSO, MVS, LINK environments | 50+ |
| Stress tests | Large programs, deep nesting | 30+ |
| Compatibility | IBM REXX testsuites (if available) | varies |

Once the interpreter reaches a basic functional level, REXX testsuites can be written in REXX itself. The IBM REXX Validation Suite (if available via CBT Tape) would be the gold standard for conformance tests.

---

# 16. References

- SC28-1883-0: TSO/E V2 REXX Reference (Dec. 1988) — **primary specification**
- SC28-1882: TSO/E V2 REXX User's Guide
- SC26-4358: SAA CPI Procedures Language Reference
- GC28-1871: TSO/E V2 Programming Guide
- GC28-0683: MVS/370 Data Management Services Guide
- GC28-0684: MVS/370 System Programming Library: Supervisor
- Mike Cowlishaw: "The REXX Language" (2nd edition)
