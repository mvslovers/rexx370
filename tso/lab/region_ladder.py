#!/usr/bin/env python3
"""tso/lab/region_ladder.py - how much REGION IRXJCL and %exec need (#258).

    python3 tso/lab/region_ladder.py              # installed modules
    python3 tso/lab/region_ladder.py STEPLIB.DSN  # modules from a library

One job, one step per REGION, every step COND=EVEN so a failing step does
not hide the next. The same exec runs through PGM=IRXJCL and through a
batch TMP (PGM=IKJEFT01, %RXA via the TSO integration). Per step it prints
the condition code or abend, the SYSTSPRT record count from the jobs API
and VIRT from IEF374I, then the lines that explain a failure (IEA70x,
IKJ56500I/IKJ56641I, the @@CRT1 stack message, a libc370 getmain WTO).

A step that ends with neither output nor a message is the defect #258 is
about, so it is flagged SILENT. Text in a dynamically allocated SYSOUT
(the C runtime's SYSTERM, e.g. libc370#254 at 640K) does not count: nobody
reads it, and the spool API often cannot either.

The exec is IBMUSER.RXT.EXEC(RXA), one SAY, as in JOB01403 and JOB01424;
it is created when missing. Read "recs": SYSTSPRT holds the SAY line for
IRXJCL, and the echoed command plus output for the TMP.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

EXEC_DS = "IBMUSER.RXT.EXEC"
EXEC_MEM = "RXA"
EXEC_TEXT = "SAY 'HELLO FROM SYSEXEC RXA -- NO COMMENT NEEDED'\n"
SAY_TEXT = "HELLO FROM SYSEXEC RXA"

IRXJCL_REGIONS = [512, 640, 768, 896, 1024]
TMP_REGIONS = [256, 384, 512, 640, 768]

# Lines that name a cause; a failing step without any of them is SILENT.
EXPLAIN = re.compile(r"IEA70\d|IKJ56500I|IKJ56641I|@@CRT1|getmain request"
                     r"|SYSIN DD not defined|IEC\d{3}I")


def steps():
    return ([(f"J{r}", "IRXJCL", r) for r in IRXJCL_REGIONS]
            + [(f"T{r}", "TMP", r) for r in TMP_REGIONS])


def body(steplib):
    lib = f"//STEPLIB  DD DSN={steplib},DISP=SHR\n" if steplib else ""
    out = []
    for name, kind, region in steps():
        if kind == "IRXJCL":
            out.append(
                f"//{name:<8} EXEC PGM=IRXJCL,PARM='{EXEC_MEM}',"
                f"REGION={region}K,COND=EVEN\n{lib}"
                f"//SYSEXEC  DD DSN={EXEC_DS},DISP=SHR\n"
                "//SYSTSPRT DD SYSOUT=*\n"
                "//SYSTSIN  DD DUMMY\n")
        else:
            out.append(
                f"//{name:<8} EXEC PGM=IKJEFT01,REGION={region}K,COND=EVEN\n"
                f"{lib}"
                f"//SYSEXEC  DD DSN={EXEC_DS},DISP=SHR\n"
                "//SYSTSPRT DD SYSOUT=*\n"
                "//SYSTSIN  DD *\n"
                f" %{EXEC_MEM}\n"
                "/*\n")
    return "".join(out)


def step_lines(spool):
    """Explaining lines per step, from JESYSMSG: each step's block starts
    at its IEF236I, and messages without a step name (@@CRT1, a getmain
    WTO) sit inside it."""
    sysmsg = spool.split("--- JESYSMSG ---", 1)[-1].split("\n--- ", 1)[0]
    per, cur = {}, None
    for line in sysmsg.splitlines():
        m = re.match(r"IEF236I ALLOC\. FOR \S+ (\S+)", line.strip())
        if m:
            cur = m.group(1)
        elif cur and EXPLAIN.search(line):
            per.setdefault(cur, []).append(line.strip())
    return per


def main():
    steplib = sys.argv[1] if len(sys.argv) > 1 else ""
    if steplib.startswith("-"):
        print(__doc__)
        return 2
    cfg, c = L.client()
    if EXEC_MEM not in [m if isinstance(m, str) else m.get("member")
                        for m in c.list_members(EXEC_DS)]:
        c.write_member(EXEC_DS, EXEC_MEM, EXEC_TEXT)
    jcl = jobcard("RGNLADR", cfg.jes_jobclass, cfg.jes_msgclass,
                  "REGION LADDER") + "\n" + body(steplib)
    r = c.submit_jcl(jcl, timeout=600)
    sp = r.spool or ""
    out = Path("build/tso/lab/region_ladder.spool")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(sp)
    print(f"{r.jobname} {r.jobid}  spool: {out}")

    files = c._json_request(
        "GET", f"/restjobs/jobs/{r.jobname}/{r.jobid}/files")
    recs = {f.get("stepname"): f.get("record-count")
            for f in files if f.get("ddname") == "SYSTSPRT"}
    cc = dict(re.findall(
        r"IEF142I \S+ (\S+) - STEP WAS EXECUTED - COND CODE (\d+)", sp))
    abend = dict(re.findall(r"IEF450I \S+ (\S+) - ABEND (\S+ \S+)", sp))
    virt = dict(re.findall(r"IEF374I STEP /(\S+)\s*/ STOP .*VIRT\s+(\d+K)",
                           sp))
    # SYSTSPRT text per step, in step order (the spool keeps that order
    # for the steps that wrote one).
    texts = sp.split("--- SYSTSPRT ---")[1:]
    written = [n for n, _, _ in steps() if recs.get(n) is not None]
    said = {n: SAY_TEXT in t.split("\n--- ", 1)[0]
            for n, t in zip(written, texts)}

    explained = step_lines(sp)
    # The TMP echoes its failure into SYSTSPRT, not into JESYSMSG.
    for n, t in zip(written, texts):
        seg = t.split("\n--- ", 1)[0]
        explained.setdefault(n, []).extend(
            ln.strip() for ln in seg.splitlines() if EXPLAIN.search(ln))

    print(f"{'step':<6} {'kind':<6} {'region':>6}  {'result':<14} "
          f"{'recs':>4} {'virt':>5}  verdict")
    for name, kind, region in steps():
        res = f"ABEND {abend[name]}" if name in abend \
            else f"CC {cc.get(name, '?')}"
        ok = said.get(name, False)
        why = explained.get(name, [])
        verdict = "ok" if ok else ("fails, explained" if why else "SILENT")
        print(f"{name:<6} {kind:<6} {region:>5}K  {res:<14} "
              f"{str(recs.get(name, '-')):>4} {virt.get(name, '-'):>5}  "
              f"{verdict}")
        for w in ([] if ok else why[:2]):
            print(f"         | {w[:100]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
