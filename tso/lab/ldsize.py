#!/usr/bin/env python3
"""tso/lab/ldsize.py - where a load module's bytes come from (#258).

    python3 tso/lab/ldsize.py MAP [MAP ...]       summary per module
    python3 tso/lab/ldsize.py --top 20 MAP ...     plus the largest sections
    python3 tso/lab/ldsize.py --link IRXEXEC ...   link the modules first

Reads the text load map that `ld370 --map FILE` writes (cc370#573) and sums
the section lengths by where they came from: this project's own objects,
and each autocall archive (lstring370, libc370). mbt writes the maps as
build/<NAME>.map once mvslovers/mbt#131 lands; until then --link relinks
the named modules with the command `make VERBOSE=1` prints, adding --map,
and writes build/lab/<NAME>.map (the module itself goes to /dev/null).
"""
import collections
import re
import subprocess
import sys
from pathlib import Path

SECTION = re.compile(r"^(\S*)\s+(SD|PC)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+(.*)$")
MAPDIR = Path("build/lab")


def origin(source):
    """Group key: the archive's file name, or 'own' for an object."""
    m = re.match(r"(.*)\((.*)\)", source.split()[0])
    return Path(m.group(1)).name if m else "own"


def read_map(path):
    head = Path(path).read_text().splitlines()
    name = Path(path).stem  # the header names the -o file, /dev/null here
    rows = []
    for line in head:
        m = SECTION.match(line)
        if m:
            rows.append((m.group(5).split()[0], origin(m.group(5)),
                         int(m.group(4), 16)))
    return name, rows


def link(module):
    """Relink one module with --map; return the map path."""
    out = subprocess.run(["make", "VERBOSE=1", "-B", module.lower()],
                         capture_output=True, text=True).stdout
    cmd = next((ln for ln in out.splitlines() if ln.startswith("ld370 ")),
               None)
    if cmd is None:
        sys.exit(f"ldsize: no ld370 command for {module}")
    MAPDIR.mkdir(parents=True, exist_ok=True)
    mapfile = MAPDIR / f"{module}.map"
    args = [a for a in cmd.split() if a != "-iebcopy"]
    i = args.index("-o")
    args[i + 1] = "/dev/null"
    subprocess.run(args[:1] + ["--map", str(mapfile)] + args[1:], check=True)
    return mapfile


def main(argv):
    top, relink, files = 0, False, []
    it = iter(argv)
    for a in it:
        if a == "--top":
            top = int(next(it))
        elif a == "--link":
            relink = True
        elif a.startswith("-"):
            print(__doc__)
            return 2
        else:
            files.append(a)
    if not files:
        print(__doc__)
        return 2
    if relink:
        files = [link(m) for m in files]

    print(f"{'module':<9} {'total':>7} {'own':>7} {'lstring370':>10} "
          f"{'libc370':>8}")
    for f in files:
        name, rows = read_map(f)
        by = collections.Counter()
        for _, grp, length in rows:
            by[grp] += length
        other = sum(v for k, v in by.items()
                    if k not in ("own", "lstring370.a", "libc.a"))
        print(f"{name:<9} {sum(by.values()):>7} {by['own']:>7} "
              f"{by['lstring370.a']:>10} {by['libc.a']:>8}"
              + (f"  other {other}" if other else ""))
        for src, grp, length in sorted(rows, key=lambda r: -r[2])[:top]:
            print(f"          {length:>7}  {src}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
