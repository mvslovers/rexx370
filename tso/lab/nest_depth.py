"""tso/lab/nest_depth.py - how deep blocks nest under IRXJCL (#296).

    python3 tso/lab/nest_depth.py [STEPLIB.DSN] [do=N,N,...] [ifdo=N,N,...]

IRXEXEC runs on a 64 KB WPOOL and the C stack guard keeps
IRX_STACK_MARGIN (16 KB) below its end, so nested blocks stop with
error 11 before the control stack's 250 entries. This finds where:
one member per depth (n nested 'do 1', or n nested 'if 1 then do'
around 'x = 1'), one IRXJCL step per member, and the condition code
of each step (IEFACTRT): 0 means it ran, 3627 is error 11
(20011 mod 4096). Without STEPLIB the installed modules run.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

# A library of its own: a few hundred nested lines per member fill the
# shared IBMUSER.RXT.EXEC (SE37). Created on first use.
EXEC_DS = "IBMUSER.RXT.NEST"
EXEC_VOL = "WORK01"
DO_DEPTHS = [80, 90, 100, 105, 110, 120, 130]
IFDO_DEPTHS = [50, 60, 65, 70, 75, 80, 90]


def member(kind, depth):
    return f"N{'D' if kind == 'do' else 'I'}{depth:03d}"


def source(kind, depth):
    opener = "do 1" if kind == "do" else "if 1 then do"
    return ("/* REXX */\n" + f"{opener}\n" * depth + "x = 1\n"
            + "end\n" * depth + f"say '{kind} {depth} ok'\n")


def parse_args(argv):
    steplib, do, ifdo = "", DO_DEPTHS, IFDO_DEPTHS
    for a in argv:
        if a.startswith("do="):
            do = [int(x) for x in a[3:].split(",") if x]
        elif a.startswith("ifdo="):
            ifdo = [int(x) for x in a[5:].split(",") if x]
        else:
            steplib = a
    return steplib, do, ifdo


def main():
    if any(a.startswith("-") for a in sys.argv[1:]):
        print(__doc__)
        return 2
    steplib, do, ifdo = parse_args(sys.argv[1:])
    cases = [("do", d) for d in do] + [("ifdo", d) for d in ifdo]
    cfg, c = L.client()
    if not c.dataset_exists(EXEC_DS):
        c.create_dataset(EXEC_DS, "PO", "FB", 80, 3120, ["TRK", 90, 30, 20],
                         volume=EXEC_VOL)
    for kind, d in cases:
        c.write_member(EXEC_DS, member(kind, d), source(kind, d))
    lib = f"//STEPLIB  DD DSN={steplib},DISP=SHR\n" if steplib else ""
    steps = "".join(
        f"//{member(k, d):<8} EXEC PGM=IRXJCL,PARM='{member(k, d)}',\n"
        "//             REGION=4096K,COND=EVEN\n"
        f"{lib}//SYSEXEC  DD DSN={EXEC_DS},DISP=SHR\n"
        "//SYSTSPRT DD SYSOUT=*\n//SYSTSIN  DD DUMMY\n"
        for k, d in cases)
    r = c.submit_jcl(jobcard("NESTDPT", cfg.jes_jobclass, cfg.jes_msgclass,
                             "NEST DEPTH") + "\n" + steps, timeout=600)
    sp = r.spool or ""
    out = Path("build/tso/lab/nest_depth.spool")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(sp)
    print(f"{r.jobname} {r.jobid}  spool: {out}")
    cc = dict(re.findall(
        r"IEF142I \S+ (\S+) - STEP WAS EXECUTED - COND CODE (\d+)", sp))
    cc.update(re.findall(r"IEFACTRT (\S+)\s*/\S+\s*/[^/]*/[^/]*/(\d+)/", sp))
    abend = dict(re.findall(r"IEF450I \S+ (\S+) - ABEND (\S+ \S+)", sp))
    missing = []
    for kind, d in cases:
        m = member(kind, d)
        res = f"ABEND {abend[m]}" if m in abend else f"CC {cc.get(m, '?')}"
        if m not in abend and m not in cc:
            missing.append(m)
        verdict = {"0000": "runs", "00000": "runs",
                   "3627": "error 11", "03627": "error 11"}.get(
                       cc.get(m, ""), "")
        print(f"{kind:<5} {d:>4}  {res:<14} {verdict}")
    if missing:
        print("no condition code for: " + ", ".join(missing))
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
