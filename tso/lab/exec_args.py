"""tso/lab/exec_args.py - do EXEC's operands reach the exec as on z/OS? (#331)

    python3 tso/lab/exec_args.py [STEPLIB.DSN]

A batch TMP runs ARGSHOW (`parse arg a; say 'n='arg() '<'a'>' length(a)`)
in the implicit and explicit EXEC forms of MIKE-TODO round 7 and compares
each line with what z/OS printed there. Without STEPLIB the installed
modules run; with REXX370.TSO.LINKLIB the patched EXEC (and TMP) from
tso/lmod_link.py testlib.

The round's unquoted case (EXEC '...' a b) is left out: PARS prompts for
it, which a batch TMP cannot answer.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

# Its own library: IBMUSER.RXT.EXEC is full (SE37).
EXEC_DS = "IBMUSER.RXT.NEST"
ARGSHOW = ("/* REXX - ARGSHOW: what PARSE ARG sees (#331) */\n"
           "parse arg a\n"
           "say 'n='arg() '<'a'>' length(a)\n"
           "exit 0\n")
X = f"EXEC '{EXEC_DS}(ARGSHOW)'"
# (command, what z/OS printed in round 7)
CASES = [
    ("%ARGSHOW", "n=0 <> 0"),
    ("%ARGSHOW a", "n=1 <a> 1"),
    ("%ARGSHOW   a   b", "n=1 <a   b> 5"),
    ("%ARGSHOW a b   ", "n=1 <a b> 3"),
    ("%ARGSHOW 'a b'", "n=1 <'a b'> 5"),
    ('%ARGSHOW "a b"', 'n=1 <"a b"> 5'),
    ("%ARGSHOW 'it''s'", "n=1 <'it''s'> 7"),
    ("%ARGSHOW a,b", "n=1 <a,b> 3"),
    ("ARGSHOW a b", "n=1 <a b> 3"),
    (X, "n=0 <> 0"),
    (X + " ''", "n=0 <> 0"),
    (X + " 'a b'", "n=1 <a b> 3"),
    (X + " '  a   b  '", "n=1 <  a   b  > 9"),
    (X + " 'it''s'", "n=1 <it's> 4"),
    (X + " 'a \"b\" c'", 'n=1 <a "b" c> 7'),
    (X + " 'a b' EXEC", "n=1 <a b> 3"),
]


def main():
    if any(a.startswith("-") for a in sys.argv[1:]):
        print(__doc__)
        return 2
    steplib = sys.argv[1] if len(sys.argv) > 1 else ""
    cfg, c = L.client()
    c.write_member(EXEC_DS, "ARGSHOW", ARGSHOW)
    lib = f"//STEPLIB  DD  DSN={steplib},DISP=SHR\n" if steplib else ""
    jcl = (jobcard("EXECARG", cfg.jes_jobclass, cfg.jes_msgclass,
                   "EXEC OPERANDS") + "\n"
           "//TMP      EXEC PGM=IKJEFT01,REGION=4096K\n" + lib +
           f"//SYSEXEC  DD  DSN={EXEC_DS},DISP=SHR\n"
           "//SYSTSPRT DD  SYSOUT=*\n"
           "//SYSTSIN  DD  *\n"
           + "".join(f" {cmd}\n" for cmd, _ in CASES) + "/*\n")
    r = c.submit_jcl(jcl, timeout=300)
    sp = r.spool or ""
    out = Path("build/tso/lab/exec_args.spool")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(sp)
    print(f"{r.jobname} {r.jobid}  spool: {out}")
    abend = re.findall(r"IEF450I .* ABEND (\S+ \S+)", sp)
    if abend:
        print("ABEND:", abend)
        return 1
    prt = sp.split("--- SYSTSPRT ---", 1)
    if len(prt) < 2:
        print("no SYSTSPRT")
        return 1
    said = [ln.rstrip() for ln in prt[1].split("\n--- ", 1)[0].splitlines()
            if ln.startswith("n=")]
    if len(said) != len(CASES):
        print(f"{len(said)} result lines for {len(CASES)} cases:")
        print(prt[1].split("\n--- ", 1)[0])
        return 1
    bad = 0
    for (cmd, want), got in zip(CASES, said):
        ok = got == want
        bad += not ok
        print(f"{'ok ' if ok else 'BAD'}  {cmd:<44} {got}"
              + ("" if ok else f"   (z/OS: {want})"))
    print(f"{len(CASES) - bad} of {len(CASES)} as on z/OS")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
