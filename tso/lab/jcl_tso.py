"""tso/lab/jcl_tso.py - IRXJCL called from TSO, in a batch TMP (#299).

    python3 tso/lab/jcl_tso.py STEPLIB.DSN

The TMP (with ZMG0002) owns a TSO environment, so a CALL of IRXJCL
borrows it instead of making one: no IRXTERM at the end, and messages
take the I/O routine (IRXIOTSO, PUTLINE to SYSTSPRT) instead of WTO.
That is the route jcl_gate.py, all batch, never reaches. A CLIST does
the CALL and writes &LASTCC after it; prints SYSTSPRT and the IRX lines
of the console log.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

EXEC_DS = "IBMUSER.RXT.EXEC"
# Explicit EXEC: line 1 decides, and without a REXX comment it is a
# CLIST (from SYSEXEC an implicit exec would be REXX).
CLIST = ("PROC 1 MEM\n"
         "CALL '<<LIB>>(IRXJCL)' '&MEM'\n"
         "WRITE JGCL &MEM LASTCC=&LASTCC\n")
CASES = ["JGARG", "NOSUCHMB", "RXDIV"]


def main():
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        print(__doc__)
        return 2
    lib = sys.argv[1]
    cfg, c = L.client()
    c.write_member(EXEC_DS, "JGCL", CLIST.replace("<<LIB>>", lib))
    jcl = (jobcard("JGTSO", cfg.jes_jobclass, cfg.jes_msgclass,
                   "IRXJCL FROM TSO") + "\n"
           "//TMP      EXEC PGM=IKJEFT01,REGION=4096K\n"
           f"//STEPLIB  DD  DSN={lib},DISP=SHR\n"
           f"//SYSEXEC  DD  DSN={EXEC_DS},DISP=SHR\n"
           "//SYSTSPRT DD  SYSOUT=*\n"
           "//SYSTSIN  DD  *\n"
           + "".join(f" EXEC '{EXEC_DS}(JGCL)' '{m}'\n"
                     for m in CASES) + "/*\n")
    r = c.submit_jcl(jcl, timeout=300)
    sp = r.spool or ""
    out = Path("build/tso/lab/jcl_tso.spool")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(sp)
    print(f"{r.jobname} {r.jobid}  spool: {out}")
    abend = re.findall(r"IEF450I .* ABEND (\S+ \S+)", sp)
    print("abend:", abend or "none")
    prt = sp.split("--- SYSTSPRT ---", 1)
    print("--- SYSTSPRT")
    print(prt[1].split("\n--- ", 1)[0].rstrip() if len(prt) > 1
          else "(none)")
    msglg = sp.split("--- JESMSGLG ---", 1)[-1].split("\n--- ", 1)[0]
    print("--- console IRX lines")
    for ln in msglg.splitlines():
        if re.search(r"IRX\d{4}\w|IEA\d{3}", ln):
            print(ln.rstrip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
