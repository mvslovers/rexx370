# ld370 `--pack` — SIGABRT at ≥7 members (single 256-byte PDS directory block overflow)

**Status:** **RESOLVED** — fixed in cc370 `ld370` (commit `c914122`, on `main`).
Rebuild/reinstall the toolchain (`make -C ../cc370 install`); `make deploy` then
packs all 12 members. The analysis below was correct (single fixed directory
block overflow); the fix emits as many 256-byte directory blocks as needed.

---

## Resolution (cc370 team, 2026-06-23)

Confirmed exactly as analysed: the unload/xmit writer built the whole PDS
directory in one fixed `dir[256]` with no bounds check, so the 7th entry
(`6*36 + 2 + 12 > 256`) overflowed the stack buffer → SIGABRT.

**Fix:** emit the directory as a **sequence of 256-byte blocks** — 7 name-sorted
entries per non-last block (`2 + 7*36 = 254`), the last block holding the
remaining entries + the FF end-of-directory terminator (so ≤6, or 0 when the
member count is a multiple of 7). Each block is its own
`count(0,0,0,KL=8,DL=256) + key + 256B` record, `key` = the high member name in
the block (`FF*8` on the last). Verified against a real multi-block IEBCOPY
UNLOAD (the CBT571 `XFASM` oracle: 7/7/2-entry blocks, keys `IFOX06`/`IFOX51`/FF,
all dir records at MBBCCHHR 0). `emit_xmit` frames each directory block as its
own RECFM=VS logical record (IEBCOPY reads `SYSUT1` one VS record at a time), the
EOD marker riding with the last block so the ≤6-member image stays byte-identical
to the previously validated one. `file370` and the host reload simulator now walk
all directory blocks.

**Validated on real MVS:** 8 members (2 directory blocks) install (`IEB154I` ×8)
and both `M1` (block 0) and `M8` (block 1, the spillover member) run with their
own return code — so a member in the **second** directory block is found and
loadable. Host regression packs 7 and 20 members.

---

## Original report (analysis that led to the fix)

## Symptom

`ld370 --pack` aborts with **SIGABRT (exit 134 / rc=-6)**, no diagnostic, when
packing **7 or more members**. 6 members succeed.

```
[mbt] Deploy target: IBMUSER.REXX370.V1R0M0D.LINKLIB
[mbt] Modules (12): IRXANCHR, IRXPARMS, IRXTSPRM, IRXISPRM, IRXTMPW, IRXINIT,
      IRXTERM, IRXLOAD, IRXEXEC, IRXDBG, IRXHELO, IRXJCL
[mbt] ERROR: ld370 --pack failed (rc=-6)
```

## Isolation (count-based, not size/format)

| members | result |
|---------|--------|
| 6 (incl. the 3 large ~200–300 KB services together) | exit 0 |
| **7** (even 7 *tiny* data modules) | **exit 134 (SIGABRT)** |
| 8, … 12 | exit 134 |

- Independent of member **size**: 6 large modules pack fine; 7 tiny ones abort.
- Independent of output **format**: reproduces with both `-xmit` and `-iebcopy`.
- Not the input arrays: `packspec[MAXOBJ]` / `struct umember m[MAXOBJ]` with
  `MAXOBJ 1024` (ld370.c:110,1294,1339) are nowhere near 7.

## Minimal reproducer

```sh
# from rexx370 after `make` (build/*.iebcopy exist); any 7 members:
ld370 --pack build/IRXANCHR.iebcopy build/IRXPARMS.iebcopy build/IRXTSPRM.iebcopy \
             build/IRXISPRM.iebcopy build/IRXTMPW.iebcopy build/IRXDBG.iebcopy \
             build/IRXLOAD.iebcopy -o /tmp/x -iebcopy ; echo $?      # -> 134
# drop any one (6 members)                                          -> 0
```

## Root cause (analysis)

The IEBCOPY unload / xmit writer emits a **single 256-byte PDS directory block**
(ld370.c:549 — `[dir record] count(KL=8,DL=256) + key FF*8 + 256B dir block`).
Each PDS2 directory entry is name(8) + TTR(3) + indicator(1) + 24 B user-data
= **36 B**; with the 2-byte used-count and the 12-byte end-of-directory marker,
**6 entries fit in 256 B and the 7th overflows the block** → buffer overflow →
abort. Member count above what one directory block holds is never split across
additional directory blocks.

Same class as the `rld[]`/`ld[]` overflow just fixed: a fixed-capacity buffer
written without a bounds check / without growth.

## Suggested fix

Emit as many 256-byte directory blocks as the member count requires (PDS
directories are a sequence of directory-block records, name-sorted across
blocks), or bounds-check and grow. A regression packing ≥7 members (and ideally
≥ one full block boundary, e.g. 7 and 20) would cover it.

## Impact / workaround

Blocks `make deploy` for any project with ≥7 load modules (REXX/370 has 12).
No clean host-side workaround for a single LINKLIB (the whole point of `--pack`
is one container); splitting into ≤6-member images would produce multiple
libraries. Fix needed in ld370.

---

*Found during the REXX/370 mbt-v2 migration, immediately after the rld/ld
overflow fix. See `toolchain-ld370-ld-symbol-resolution.md` for the first bug.*
