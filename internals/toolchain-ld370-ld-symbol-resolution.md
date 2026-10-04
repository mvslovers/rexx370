# ld370 / ar370 — LD-symbol resolution failure (REXX/370 mbt v2 host build)

**Status:** **RESOLVED** — fixed in cc370 `ld370` (commit `6e4dbef`, on `main`).
Rebuild/reinstall the toolchain (`make -C ../cc370 install`) to pick it up. The
analysis in this report was correct that it is an ld370 LD-handling defect; the
mechanism turned out to be a **buffer overflow**, not a resolution-logic bug.

---

## Root cause & fix (cc370 team, 2026-06-23)

**A fixed-size array overflow in `ld370`'s object reader, not an LD-resolution
logic bug.** `parse_object` recorded an object's RLD items into a fixed
`o->rld[512]` and its LD entries into `o->ld[64]` with **no bounds check on the
write**. In `struct obj`, `rld[]` is laid out immediately before `ld[]`, and in
an object deck the **RLD cards follow the ESD cards** — so an object with **>512
RLD items** overflowed `rld[]` straight into the already-recorded `ld[]` entries,
silently corrupting its exported LD symbols (e.g. `IRXBEXEC` → `????EXEC`,
owner 1 → 13). A later cross-object reference to such a symbol then came back
*unresolved*.

This is exactly why the pattern looked irregular: it depends on **how far past
512 a given object's RLD list runs** and **which `ld[]` slots the spill lands
on** — not on any property of the symbol. The affected objects are precisely the
large C cores with >512 RLD items:

| object | RLD bytes | >512 items? |
|--------|-----------|-------------|
| irx#pars | 3116 | yes → IRXPAR* lost |
| irx#bifs | 3668 | yes → IRXBIFAL/IRXPTOPT lost |
| irx#bcom | 2724 | yes → IRXBCOMP/IRXBCUTX lost |
| irx#bvm | 2436 | yes → IRXBEXEC lost |
| irx#arith, irx#vpol, … | <2048 | no → resolve fine (matches "IRXARICM resolves") |

The v1 IFOX00+IEWL path has no such limit, so it bound everything. The ld370
test corpus had never linked an object with >512 RLD items (crent370 `t1` + the
small fixtures), so the overflow was latent — the same class as the previously
fixed text-buffer overflow.

**Fix:** `rld` and `ld` are now `malloc`'d and grown on demand (`grow_arr`), like
the object text buffer. A regression in `ld370/tests/run.sh` links a
600-`A`-con (>512 RLD) object that exports an LD symbol and references it from a
second object — without `--allow-unresolved`, so a clobbered symbol fails the
link.

**Verified on these exact objects:** the `IRXINIT` link goes from **11
unresolved** (`IRXPARBA IRXBCOMP IRXBEXEC IRXPARIN IRXPARRN IRXPARCL IRXBIFAL
IRXPAREV …`) to **all IRX\* resolved**; the only symbol left is `@@CRT0` (the C
startfile, not on the manual link line). `ISTSO` and `MAIN` remain rexx370-side
items as noted under *Secondary findings* below.

---

## Original report (analysis that led to the fix)

**Reporter context:** REXX/370 builds cleanly under the v1 MVS toolchain
(IFOX00 assemble + IEWL link, all final links RC=0). The same C/asm sources,
compiled by the same `cc370` but assembled with `as370` and linked with
`ld370` on the host, fail with "unresolved external reference(s)" for a
specific set of cross-module function symbols.

---

## TL;DR

1. `cc370` exports `asm("NAME")`-aliased C functions as **LD (label-definition)
   records inside a single private/blank CSECT** per object (one PC CSECT per
   `.c`, the exported entry points as LD entries pointing into it). `as370`
   emits these LD records correctly (verified with `file370 -v`).

2. Linking the objects as **loose `.o` on the ld370 command line leaves these
   LD symbols unresolved** — even in a minimal two-object link where the
   defining object is present.

3. Building an **`ar370` `.a` archive and autocalling it resolves most of them**
   (11 → 5 unresolved for IRXINIT). So ld370 resolves references to LD symbols
   via the archive symbol index, but not from loose `.o`.

4. **5 symbols stay unresolved even via the archive, and even with `--include`
   (force-include) of their defining member** — although the member's text is
   physically in the module. This is the core bug.

5. The v1 path (IFOX00 + IEWL) resolves **all** of these (final links RC=0), so
   this is a host-toolchain regression. REXX/370 has **no runtime symbol
   resolution** (no dynamic LOAD of these entry points, no IRXEXTE-vector
   dispatch for them) — they are direct intra-program calls that *must* resolve
   at link time.

---

## Environment

| Tool | Version |
|------|---------|
| cc370 | V1.0 - Jun 23 2026 |
| as370 | V1.0 - Jun 23 2026 |
| ld370 | V1.0 (man page: "host-native MVS linker (IEWL replacement) … byte-identical to IEWL") |
| file370 | V1.0 |
| ar370 | (same toolchain build) |

Host: macOS (arm64). Project: `mvslovers/rexx370`, branch
`feature/mbt-v2-migration`. Build: `-std=gnu99 -O1`.

---

## The cc370 object model

`cc370 -S` emits **one blank/private CSECT per translation unit**; every
function is a `PDPPRLG` macro block. Exported (non-static, `asm()`-aliased)
functions are `PDPPRLG …,ENTRY=YES` → an `ENTRY` directive → an **LD record**.
Internal functions are `ENTRY=NO`.

```
         CSECT                          <- blank/private (PC) section
@@F3     PDPPRLG CINDEX=0,...,ENTRY=NO  <- internal
...
IRXPARBA PDPPRLG CINDEX=12,...,ENTRY=YES <- exported -> LD record
```

`as370`'s macro library (`<exedir>/../macros`, libc370) differs from IFOX00's
(SYS1.MACLIB + CRENT370.MACLIB on MVS). The `PDPPRLG` expansion that produces
the `ENTRY`/ESD could in principle diverge between the two assemblers — worth
ruling out (see "Where to look", item 3). Empirically `as370` *does* emit the
LD records (below), so this is not obviously the cause.

`file370 build/irx#pars.o`:
```
OS/360 object deck -- 1 section(s) (first (private)), 32 extern ref(s),
66396B text, 56 RLD card(s); 5 LD entr(y/ies)
    ESD    1  (blank)   PC  addr=000000  len=010554
    ESD  --   IRXPARBA  LD  addr=000B80
    ESD  --   IRXPARIN  LD  addr=00FE24
    ESD  --   IRXPARCL  LD  addr=00FF18
    ESD  --   IRXPAREV  LD  addr=010074
    ESD  --   IRXPARRN  LD  addr=010348
```

---

## Symptom and affected symbols

Linking the production load module `IRXINIT` (asm entry wrapper +
~20 C cores) as loose `.o`:

```
ld370: unresolved external reference(s):
    ISTSO IRXBIFAL IRXBCUTX IRXPTOPT IRXPARBA IRXPAREV
    IRXBCOMP IRXBEXEC IRXPARIN IRXPARRN IRXPARCL
ld370: 11 unresolved external(s) -- the module would S0C4 at runtime
```

All ten `IRX*` symbols are C functions exported via `asm()` aliases and defined
(as LD) in objects that **are on the link line**:

| Symbol | C function | defined (LD) in | object text |
|--------|-----------|-----------------|-------------|
| IRXPARIN | irx_pars_init | irx#pars.o | 66 396 B |
| IRXPARRN | irx_pars_run | irx#pars.o | |
| IRXPARCL | irx_pars_cleanup | irx#pars.o | |
| IRXPARBA | irx_pars_… | irx#pars.o | |
| IRXPAREV | irx_pars_…eval | irx#pars.o | |
| IRXBCOMP | irx_bc_compile | irx#bcom.o | 40 890 B |
| IRXBCUTX | irx_bc_unsup_text | irx#bcom.o | |
| IRXBEXEC | irx_bc_execute | irx#bvm.o | 27 629 B |
| IRXBIFAL | irx_bif_…register | irx#bifs.o | 37 727 B |
| IRXPTOPT | irx_p…_opt | irx#bifs.o | |

(`ISTSO` is a **separate, non-bug issue** — see "Secondary findings".)

---

## Evidence

### 1. v1 (IFOX00 + IEWL) resolves everything

`zowe jobs list jobs --prefix MBTLK*` against the v1 reference build:
**every final-link job is `CC 0000`** (MBTLKIRX = services, MBTLKTST = tests,
MBTLKIST). Assembler jobs (MBTASM*) are CC 0004 (normal). Since there is no
runtime resolution, the v1 final link *does* bind these symbols.

### 2. as370 emits the LD records correctly

See the `file370` dump above — `IRXPARRN` et al. are present as LD records with
valid addresses. So the object carries the definitions.

### 3. Loose `.o` → 11 unresolved; `.a` archive → 5; the rest stay unresolved

| Link form | unresolved |
|-----------|-----------|
| loose `.o` (current mbt v2) | 11 (ISTSO + 10) |
| `ar370` `.a` + autocall | **5** |
| `.a` listed 3× | 5 (no change) |
| `.a` + `--include irx#pars --include irx#bcom --include irx#bifs` | 5 (no change) |

```sh
ar370 rc /tmp/rexcore.a build/irx#*.o build/istso.o     # 25 members, 92 symbols
ld370 -e IRXINIT build/irxinit.o /tmp/rexcore.a \
      .mbt/deps/lstring370/lib/lstring370.a -lc -iebcopy -o /tmp/out
# -> 5 unresolved: IRXBIFAL IRXPTOPT IRXPARBA IRXBCUTX IRXPAREV
```

**Key observation:** `IRXPARIN/RN/CL` (in irx#pars.o) resolve via the archive,
but `IRXPARBA/IRXPAREV` (also in irx#pars.o) do **not** — even though pulling
`IRXPARIN` brings the whole `irx#pars.o` member into the module. So ld370
registers only a **subset of a member's LD entries**, and `--include`-ing the
member does not register the rest.

### 4. Object-intrinsic, not cumulative / not ordering / not address-of

Minimal two-object links:

```sh
# FAILS: IRXBEXEC unresolved (exec.o references it, bvm.o defines it as LD)
ld370 --allow-unresolved -e @@CRT0 build/irx#exec.o build/irx#bvm.o \
      .mbt/deps/lstring370/lib/lstring370.a -lc -iebcopy -o /tmp/e1

# WORKS: IRXARICM resolves (pars.o references it, arith.o defines it as LD)
ld370 --allow-unresolved -e @@CRT0 build/irx#pars.o build/irx#arith.o \
      .mbt/deps/lstring370/lib/lstring370.a -lc -iebcopy -o /tmp/e2
```

- `IRXBEXEC` is a **direct call** (`irx#exec.c:379: irx_bc_execute(...)`), not a
  vector/address-of reference. It still fails.
- Reordering the objects (problem objects first) does not change the result.

### 5. Resolved-vs-unresolved pattern (via the `.a` archive)

| object | LD symbol | addr in obj | resolves? | also ER (self-ref)? |
|--------|-----------|-------------|-----------|---------------------|
| irx#pars.o | IRXPARBA | 0x000B80 | **no** | no |
| | IRXPARIN | 0x00FE24 | yes | no |
| | IRXPARCL | 0x00FF18 | yes | no |
| | IRXPAREV | 0x010074 | **no** | yes |
| | IRXPARRN | 0x010348 | yes | no |
| irx#bcom.o | IRXBCUTX | 0x009CB4 | **no** | no |
| | IRXBCOMP | 0x009D60 | yes | no |
| irx#bvm.o | IRXBEXEC | 0x001654 | yes | no |
| irx#bifs.o | IRXPTOPT | 0x008B2C | **no** | yes |
| | IRXBIFAL | 0x0094FC | **no** | no |

No single clean rule fits: the lowest-addressed LD of pars/bcom/bifs fails, but
bvm's single LD resolves; bifs has *both* LDs failing; two of the five failures
are also self-referenced (ER+LD in the same object), three are not. This points
at an ld370/ar370 LD-registration defect rather than a property of the source.

---

## Secondary findings (not the toolchain bug)

- **ISTSO** is a real named CSECT (`SD`, len 0x8C) in `asm/istso.asm`, called
  directly via `int is_tso(void) asm("ISTSO")` (`include/irxenv.h`). It is
  unresolved in IRXINIT simply because the object is not on IRXINIT's link line
  (it was a standalone `[[module]]`). Fix is on the rexx370 side: link
  `asm/istso.asm` into the modules that call `is_tso()`. Not a toolchain issue.
- **MAIN** appears in one test module's unresolved set; origin still to be
  pinned down on the rexx370 side.

---

## Hypotheses / where to look

1. **ld370 cross-`.o` LD resolution.** References to LD (entry) symbols are not
   resolved from loose `.o` inputs, only via the ar370 archive symbol index.
   Compare how ld370 builds its global symbol table from a directly-included
   object's CESD/ESD vs from an archive member.

2. **ld370/ar370 partial LD registration.** When a member is brought in (autocall
   *or* `--include`), only some of its LD entries enter the resolvable symbol
   table. `irx#pars.o`: IRXPARIN/RN/CL register, IRXPARBA/IRXPAREV do not, from
   the *same* member. Check the LD-record loop in ld370's member loader and the
   ar370 index builder (note: all five missing symbols *are* present in the
   ar370 index, so the gap is on the consume/registration side, not the index).

3. **as370 vs IFOX00 ESD divergence.** as370 uses host libc370 macros; IFOX00
   uses MVS macros. Download the IFOX00-produced object for `irx#pars` from the
   v1 OBJECT PDS and `file370 -v` it; compare its ESD (record types/order) with
   `as370`'s. If they differ, the fault is in as370 / the macro library; if they
   are equivalent, the fault is in ld370.

4. **Decisive isolation:** link the *IFOX00-produced* object with **ld370**
   (and/or the *as370*-produced object with IEWL). Whichever linker fails on a
   known-good object names the culprit.

---

## Reproduction (from the rexx370 repo, branch feature/mbt-v2-migration)

```sh
make deps && make            # builds build/*.o and the loose-.o load modules
                             # IRXINIT etc. report 11 unresolved externals

# loose .o vs archive:
ar370 rc /tmp/rexcore.a build/irx#*.o build/istso.o
ld370 -e IRXINIT build/irxinit.o /tmp/rexcore.a \
      .mbt/deps/lstring370/lib/lstring370.a -lc -iebcopy -o /tmp/irxinit
#   loose .o: 11 unresolved ; archive: 5 unresolved

# inspect an object's exported LD records:
file370 -v build/irx#pars.o | grep ' LD '
```

---

*Generated during the rexx370 mbt-v2 migration investigation. The migration
structure (project.toml v2, 58 modules) is complete and builds; it is blocked
on this link-time symbol-resolution issue.*
