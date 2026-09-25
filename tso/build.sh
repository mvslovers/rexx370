#!/usr/bin/env bash
# tso/build.sh - build the TSO integration modules (not an mbt target).
#
#   tso/build.sh rxdrv      RXDRV + IKJCT437  -> build/tso/RXDRV.xmit
#   tso/build.sh ikjeft01   decks for LMOD IKJEFT01: IKJEFT01.o, IKJEFTRX.o
#   tso/build.sh exec       decks for LMOD EXEC:     IKJCT430.o, IKJCT437.o
#
# The TSO load modules are NOT linked here. They ship as an SMP usermod of
# object decks, and SMP link-edits those into the INSTALLED load module on
# MVS, which keeps the service already on it (ZP60014, UY16532, UZ82014,
# ...). Each target also assembles the unpatched IBM source from mvs38src
# as <name>.orig.o: tso/lmod_link.py links it the same way into a test
# library and proves it reproduces the installed module.
#
# The IBM modules are assembled with the pinned as370 and the macro
# libraries of the mvs38src project; see tso/TODO_IKJEFT01.md for why
# each of these options is what it is. Override MVS38SRC if that repo
# does not live next to this one.
#
# as370 can write an object and exit 0 at severity 8, so the listing's
# diagnostics are checked as well, not just the exit status.

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
MVS38SRC="${MVS38SRC:-$repo/../mvs38src}"
out="$repo/build/tso"
mkdir -p "$out"

as370_bin="$MVS38SRC/work/src-states/bin/as370-main"
macros=(
  tk5-recon pls-header
  mvsce-2.1.4-dlib/AMACLIB mvsce-2.1.4-dlib/AMODGEN
  mvsce-2.1.4-dlib/AGENLIB mvsce-2.1.4-dlib/ATSOMAC
  mvsce-2.1.4-dlib/ATCAMMAC mvsce-2.1.4-dlib/APVTMACS
  tape mirror erep-set amaclib-live
)
incs=()
for m in "${macros[@]}"; do
  incs+=(-I "$MVS38SRC/work/macros/$m")
done

asm() { # asm <source> <object>
  local log="$2.log"
  if ! ASMDATE=09/07/26 ASMTIME=12.00 "$as370_bin" "${incs[@]}" \
       -o "$2" "$1" >"$log" 2>&1; then
    cat "$log"; echo "as370 failed: $1" >&2; exit 1
  fi
  # A clean as370 run prints nothing; any " ERROR:" line or the
  # "Statements Flagged" summary means the object is not trustworthy.
  if grep -E -q ' ERROR:|WARNING:|Statements? Flagged' "$log"; then
    cat "$log"; echo "as370 diagnostics: $1" >&2; exit 1
  fi
}

case "${1:-}" in
  rxdrv)
    asm "$here/IKJCT437.ASM" "$out/IKJCT437.o"
    asm "$here/RXDRV.ASM" "$out/RXDRV.o"
    # --norent --noreus: RXDRV keeps its save area in the CSECT, and a RENT
    # module from an APF library lands in key 0 (S0C4 reason 004).
    ld370 -o "$out/RXDRV.lm" --name RXDRV --entry RXDRV --blocksize 19069 \
          --norent --noreus "$out/RXDRV.o" "$out/IKJCT437.o" -iebcopy
    # ld370 appends .xmit itself when -xmit is given.
    ld370 --pack RXDRV="$out/RXDRV.lm.iebcopy" -o "$out/RXDRV" \
          --blocksize 19069 --norent --noreus -xmit
    echo "built $out/RXDRV.xmit"
    ;;
  ikjeft01)
    asm "$MVS38SRC/src/IKJEFT01.ASM" "$out/IKJEFT01.orig.o"
    asm "$here/IKJEFT01.ASM" "$out/IKJEFT01.o"
    asm "$here/IKJEFTRX.ASM" "$out/IKJEFTRX.o"
    echo "built $out/IKJEFT01.orig.o $out/IKJEFT01.o $out/IKJEFTRX.o"
    ;;
  exec)
    asm "$MVS38SRC/src/IKJCT430.ASM" "$out/IKJCT430.orig.o"
    asm "$here/IKJCT430.ASM" "$out/IKJCT430.o"
    asm "$here/IKJCT437.ASM" "$out/IKJCT437.o"
    echo "built $out/IKJCT430.orig.o $out/IKJCT430.o $out/IKJCT437.o"
    ;;
  *)
    echo "usage: $0 rxdrv|ikjeft01|exec" >&2; exit 2
    ;;
esac
