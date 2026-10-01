"""tso/lab/jcl_gate.py - thin IRXJCL against the C IRXJCLD (#299).

    python3 tso/lab/jcl_gate.py STEPLIB.DSN

Runs the same PGM= cases twice, once through IRXJCL and once through
IRXJCLD, both from STEPLIB, and prints per case the condition code
(IEFACTRT, the full value, not IEF142I's last four digits), the SYSTSPRT
lines and, per job, the IRX messages that reached the console (WTO).
The C module is the reference: every difference is either a defect of
the thin entry or a place where the C module departed from SC28-1883-0.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

EXEC_DS = "IBMUSER.RXT.EXEC"
EXECV_DS = "IBMUSER.RXT.EXECV"     # VB/255: NEST40B / NEST41B (#294)
MEMBERS = {
    "JGARG": "/* REXX */\nPARSE ARG A\nSAY '['A']'\nEXIT 7\n",
}

# (step suffix, PARM or None, SYSEXEC data set)
CASES = [
    ("ARG", "JGARG hello   world", EXEC_DS),
    ("LOW", "jgarg", EXEC_DS),
    ("LEAD", "  JGARG x", EXEC_DS),
    ("NOMB", "NOSUCHMB", EXEC_DS),
    ("DIV", "RXDIV", EXEC_DS),
    ("EXIT", "RXEXIT", EXEC_DS),
    ("LONG", "TOOLONGNM", EXEC_DS),
    ("NEST40", "NEST40B", EXECV_DS),
    ("NEST41", "NEST41B", EXECV_DS),
    ("NOPRM", None, EXEC_DS),
]


def body(pgm, steplib):
    out = []
    for suffix, parm, sysexec in CASES:
        p = f",PARM='{parm}'" if parm is not None else ""
        out.append(
            f"//{suffix:<8} EXEC PGM={pgm}{p},\n"
            "//             REGION=1024K,COND=EVEN\n"
            f"//STEPLIB  DD DSN={steplib},DISP=SHR\n"
            f"//SYSEXEC  DD DSN={sysexec},DISP=SHR\n"
            "//SYSTSPRT DD SYSOUT=*\n"
            "//SYSTSIN  DD DUMMY\n")
    return "".join(out)


def run(c, cfg, pgm, steplib):
    jcl = jobcard(f"JG{pgm[-4:]}", cfg.jes_jobclass, cfg.jes_msgclass,
                  f"JCL GATE {pgm}") + "\n" + body(pgm, steplib)
    r = c.submit_jcl(jcl, timeout=600)
    sp = r.spool or ""
    out = Path(f"build/tso/lab/jcl_gate_{pgm}.spool")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(sp)
    cc = dict(re.findall(
        r"IEF142I \S+ (\S+) - STEP WAS EXECUTED - COND CODE (\d+)", sp))
    cc.update(re.findall(r"IEFACTRT (\S+)\s*/\S+\s*/[^/]*/[^/]*/(\d+)/", sp))
    abend = dict(re.findall(r"IEF450I \S+ (\S+) - ABEND (\S+ \S+)", sp))
    files = c._json_request(
        "GET", f"/restjobs/jobs/{r.jobname}/{r.jobid}/files")
    prt = {}
    for f in files:
        if f.get("ddname") != "SYSTSPRT":
            continue
        txt = c._request(
            "GET", f"/restjobs/jobs/{r.jobname}/{r.jobid}/files/"
            f"{f.get('id')}/records", accept="text/plain")
        txt = txt.decode("utf-8", "replace")
        prt[f.get("stepname")] = [ln.rstrip() for ln in txt.splitlines()
                                  if ln.strip()]
    msglg = sp.split("--- JESMSGLG ---", 1)[-1].split("\n--- ", 1)[0]
    wtos = [re.sub(r"^.*?(IRX\d{4}\w)", r"\1", ln).rstrip()
            for ln in msglg.splitlines() if re.search(r"IRX\d{4}\w", ln)]
    res = {}
    for suffix, _, _ in CASES:
        res[suffix] = (f"ABEND {abend[suffix]}" if suffix in abend
                       else f"CC {cc.get(suffix, '?')}",
                       prt.get(suffix, []))
    print(f"{pgm}: {r.jobname} {r.jobid}  spool: {out}")
    return res, wtos


def main():
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        print(__doc__)
        return 2
    steplib = sys.argv[1]
    cfg, c = L.client()
    for m, text in MEMBERS.items():
        c.write_member(EXEC_DS, m, text)
    thin, thin_wto = run(c, cfg, "IRXJCL", steplib)
    ref, ref_wto = run(c, cfg, "IRXJCLD", steplib)
    # Two jobs that both failed would compare equal: refuse to compare
    # a case that has no condition code.
    missing = [f"{pgm}/{s}" for pgm, res in (("IRXJCL", thin),
                                              ("IRXJCLD", ref))
               for s, (rc, _) in res.items() if rc == "CC ?"]
    if missing:
        print("no condition code for: " + ", ".join(missing))
        return 2
    diff = 0
    for suffix, parm, _ in CASES:
        a, b = thin[suffix], ref[suffix]
        same = a == b
        diff += not same
        print(f"{'same' if same else 'DIFF':<5} {suffix:<7} "
              f"PARM={parm!r}")
        print(f"      IRXJCL : {a[0]:<12} {a[1]}")
        if not same:
            print(f"      IRXJCLD: {b[0]:<12} {b[1]}")
    same = thin_wto == ref_wto
    diff += not same
    print(f"{'same' if same else 'DIFF':<5} WTOs")
    for w in thin_wto:
        print(f"      IRXJCL : {w}")
    if not same:
        for w in ref_wto:
            print(f"      IRXJCLD: {w}")
    print(f"{diff} difference(s)")
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
