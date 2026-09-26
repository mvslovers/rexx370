#!/usr/bin/env python3
"""tso/lab/exec_test.py - the TSO EXEC language rules, case by case.

    python3 tso/lab/exec_test.py new-tmp     # patched TMP + patched EXEC
    python3 tso/lab/exec_test.py ibm-tmp     # IBM TMP (LPA) + patched EXEC
    python3 tso/lab/exec_test.py installed   # no STEPLIB: what SMP installed
    python3 tso/lab/exec_test.py drop-tmp    # remove IKJEFT01/IKJEFT0A from
                                             # the test library for ibm-tmp

new-tmp and ibm-tmp run a batch TMP with STEPLIB=REXX370.TSO.LINKLIB, which
supplies PGM=IKJEFT01 and the EXEC command processor ahead of the LPA and
SYS1.CMDLIB. For ibm-tmp the library must hold the IBM IKJEFT01: once the
usermod is in the LPA, a library without one gets the patched TMP from
there. ibm-tmp puts an IBM TMP in: tso/lmod_link.py reference ikjeft01
links the unpatched IBM IKJEFT01 against the installed module (the
IKJEFTRX CSECT it carries along is dead code, nothing calls it), and
that is copied over. tso/lmod_link.py testlib ikjeft01 puts ours back.

The rules (measured on z/OS, docs/REXX_TSO_INTEGRATION.md):

  implicit %name / name   SYSPROC member: REXX only with a comment
                          containing REXX in line 1; every other DD
                          (SYSEXEC): REXX, no check
  explicit EXEC 'ds(m)'   REXX only with that comment, whatever library
  explicit ... EXEC       REXX, forced; E, EX, EXE all abbreviate it
  EXEC name EXEC          unqualified name gets .EXEC, not .CLIST

Each case names what its output must contain with a REXX environment and
without one (the IBM TMP: everything falls back to CLIST). The test data
sets are IBMUSER.RXT.EXEC (SYSEXEC) and IBMUSER.RXT.PROC (SYSPROC); the
members this test needs beyond the old ones are created when missing.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

LIB = "REXX370.TSO.LINKLIB"
EXEC_DS = "IBMUSER.RXT.EXEC"
PROC_DS = "IBMUSER.RXT.PROC"

MEMBERS = {
    (PROC_DS, "RXD"): "/* A PLAIN CLIST, COMMENT IN LINE 1 */\n"
                      "WRITE HELLO FROM CLIST RXD\n",
    (EXEC_DS, "CLST"): "WRITE HELLO FROM CLIST CLST\n",
    (PROC_DS, "NOCMT"): "SAY 'HELLO FROM NOCMT'\n",
}

A = "HELLO FROM SYSEXEC RXA"
B = "HELLO FROM SYSPROC RXB"
C = "HELLO FROM CLIST RXC"
D = "HELLO FROM CLIST RXD"
CLST = "HELLO FROM CLIST CLST"
NOCMT = "HELLO FROM NOCMT"
SAYNF = "COMMAND SAY NOT FOUND"

# (command, with a REXX environment, with the IBM TMP)
# An expectation is a text the command's output must contain; a leading
# '!' means it must NOT contain it.
CASES = [
    # A batch TMP has no prefix; unqualified names need one (IBMUSER).
    ("PROFILE PREFIX(IBMUSER)", "", ""),
    ("%RXA", A, "COMMAND RXA NOT FOUND"),
    ("RXB", B, SAYNF),
    ("%RXC", C, C),
    ("%RXD", D, D),
    ("%RXD", D, D),                        # two comment CLISTs: #239
    ("%RXA", A, "COMMAND RXA NOT FOUND"),  # ... REXX still works
    ("%CLST", "!" + CLST, "COMMAND CLST NOT FOUND"),
    ("%NOCMT", SAYNF, SAYNF),
    ("%RXZZ", "COMMAND RXZZ NOT FOUND", "COMMAND RXZZ NOT FOUND"),
    (f"EXEC '{EXEC_DS}(RXA)'", SAYNF, SAYNF),
    (f"EXEC '{EXEC_DS}(RXA)' EX", A, SAYNF),
    (f"EXEC '{EXEC_DS}(RXA)' E", A, SAYNF),
    (f"EXEC '{EXEC_DS}(RXA)' EXEC", A, SAYNF),
    (f"EXEC '{PROC_DS}(RXB)'", B, SAYNF),
    (f"EXEC '{EXEC_DS}(CLST)'", CLST, CLST),
    (f"EXEC '{PROC_DS}(NOCMT)'", SAYNF, SAYNF),
    (f"EXEC '{PROC_DS}(NOCMT)' EXEC", NOCMT, SAYNF),
    ("EXEC RXT(RXA) EXEC", A, SAYNF),
    # without the keyword: .CLIST (3.8's message shows the name before
    # DAIR adds the prefix; the EXEC case above proves the prefix is used)
    ("EXEC RXT(RXC)", "RXT.CLIST NOT IN CATALOG",
     "RXT.CLIST NOT IN CATALOG"),
    (f"EXEC '{PROC_DS}(RXC)'", C, C),
    # BREXX/370 writes its own context into ECTENVBK and leaves it there,
    # pointing at storage it has freed (brexx370 asm/rxinit.hlasm UPDENV,
    # asm/rxterm.hlasm). REXX must still find the TMP's environment.
    ("BREXX", "", ""),
    ("%RXA", A, "COMMAND RXA NOT FOUND"),
    ("BREXX RXA", "", ""),
    ("%RXA", A, "COMMAND RXA NOT FOUND"),
    ("TIME", "IKJ56650I", "IKJ56650I"),
]

BODY = f"""
//TMP      EXEC PGM=IKJEFT01,REGION=4096K
<<STEPLIB>>//SYSEXEC  DD  DSN={EXEC_DS},DISP=SHR
//SYSPROC  DD  DSN={PROC_DS},DISP=SHR
//SYSTSPRT DD  SYSOUT=*
//SYSPRINT DD  SYSOUT=*
//SYSUDUMP DD  SYSOUT=*
//SYSTSIN  DD  *
""" + "".join(f" {cmd}\n" for cmd, _, _ in CASES) + "/*\n//\n"


def members(c, ds):
    return [m if isinstance(m, str) else m.get("member")
            for m in c.list_members(ds)]


def drop_tmp(c, cfg):
    vol = [x for x in c.list_datasets(LIB) if x.get("dsname") == LIB][0]
    jcl = jobcard("RXDROPT", cfg.jes_jobclass, cfg.jes_msgclass,
                  "DROP TEST TMP") + f"""
//SCR      EXEC PGM=IEHPROGM
//SYSPRINT DD SYSOUT=*
//DD1      DD UNIT={vol["dev"]},VOL=SER={vol["vol"]},DISP=OLD
//SYSIN    DD *
  SCRATCH DSNAME={LIB},VOL={vol["dev"]}={vol["vol"]},MEMBER=IKJEFT0A
  SCRATCH DSNAME={LIB},VOL={vol["dev"]}={vol["vol"]},MEMBER=IKJEFT01
/*
//
"""
    r = c.submit_jcl(jcl, timeout=300)
    print(f"{r.jobid} {r.status} rc={r.rc}")
    print(members(c, LIB))
    return 0


def ibm_tmp_in(c, cfg):
    """Put an IBM TMP into the test library, so the STEPLIB TMP creates
    no REXX environment: link the unpatched IKJEFT01 like SMP would
    (lmod_link reference) and copy the result over."""
    # The comparison inside reference() differs by design once the usermod
    # is installed (the installed IKJEFT01 is then ours); only the linked
    # module in RXLMOD.LOADREF is wanted here.
    L.reference("ikjeft01")
    if not c.dataset_exists(f"{cfg.hlq}.RXLMOD.LOADREF"):
        raise SystemExit("IBM TMP link failed - not running ibm-tmp")
    jcl = jobcard("RXIBMTMP", cfg.jes_jobclass, cfg.jes_msgclass,
                  "IBM TMP IN") + f"""
//COPY     EXEC PGM=IEBCOPY,REGION=4096K
//SYSPRINT DD SYSOUT=*
//IN       DD DSN={cfg.hlq}.RXLMOD.LOADREF,DISP=SHR
//OUT      DD DSN={LIB},DISP=SHR
//SYSIN    DD *
  COPY INDD=IN,OUTDD=OUT
  SELECT MEMBER=((IKJEFT01,,R),(IKJEFT0A,,R))
/*
//
"""
    r = c.submit_jcl(jcl, timeout=300)
    copied = [l.split()[1] for l in (r.spool or "").splitlines()
              if "IEB154I" in l]
    print(f"{r.jobid} IBM TMP into {LIB}: {copied}")
    if sorted(copied) != ["IKJEFT01", "IKJEFT0A"]:
        raise SystemExit("IBM TMP not copied - not running ibm-tmp")


def split_output(tsprt):
    """Cut SYSTSPRT into the output of each command. The TMP echoes each
    input line; everything up to the next echo belongs to it."""
    lines = tsprt.splitlines()
    segs, pos = [], 0
    for i, (cmd, _, _) in enumerate(CASES):
        start = None
        for j in range(pos, len(lines)):
            if lines[j].strip() == cmd:
                start = j + 1
                break
        if start is None:
            segs.append(None)
            continue
        nxt = CASES[i + 1][0] if i + 1 < len(CASES) else None
        end = len(lines)
        for j in range(start, len(lines)):
            if nxt is not None and lines[j].strip() == nxt:
                end = j
                break
        segs.append("\n".join(lines[start:end]))
        pos = end
    return segs


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "drop-tmp":
        cfg, c = L.client()
        return drop_tmp(c, cfg)
    if mode not in ("new-tmp", "ibm-tmp", "installed"):
        print(__doc__)
        return 2
    cfg, c = L.client()
    if mode == "ibm-tmp":
        ibm_tmp_in(c, cfg)
    elif mode == "new-tmp" and "IKJEFT01" not in members(c, LIB):
        print(f"{LIB} holds {members(c, LIB)}: no test TMP for {mode}")
        return 2
    for (ds, mem), text in MEMBERS.items():
        if mem not in members(c, ds):
            c.write_member(ds, mem, text)
    jcl = jobcard("RXEXECT", cfg.jes_jobclass, cfg.jes_msgclass,
                  "EXEC TEST") + BODY.replace(
        "<<STEPLIB>>", "" if mode == "installed"
        else f"//STEPLIB  DD  DSN={LIB},DISP=SHR\n")
    r = c.submit_jcl(jcl, timeout=300)
    sp = r.spool or ""
    out = Path("build/tso/lab") / f"exec_test_{mode}.spool"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(sp)
    print(f"{r.jobid} {r.status} rc={r.rc}  spool: {out}")
    joblog = sp.split("--- JESYSMSG ---")[0]
    tsprt = sp.split("--- SYSTSPRT ---", 1)[1].split("\n--- ", 1)[0] \
        if "--- SYSTSPRT ---" in sp else ""
    for line in joblog.splitlines():
        if re.search(r"IEA995I|IEF450I|ABEND|IEA703I", line):
            print("  joblog:", line.strip()[:100])

    bad = 0
    abend = (r.status == "ABEND" or "IEF450I" in joblog
             or "IKJ56641I" in tsprt)
    print(f"  {'FAIL' if abend else 'PASS'}  no abend "
          "(job log and TMP-caught IKJ56641I)")
    bad += abend
    col = 1 if mode != "ibm-tmp" else 2
    for case, seg in zip(CASES, split_output(tsprt)):
        want = case[col]
        if seg is None:
            ok, got = False, "(command not reached)"
        elif want.startswith("!"):
            ok, got = want[1:] not in seg, seg
        else:
            ok, got = want in seg, seg
        print(f"  {'PASS' if ok else 'FAIL'}  {case[0]:<34} "
              f"{'not ' + want[1:] if want.startswith('!') else want}")
        if not ok:
            for g in got.splitlines()[:4]:
                print(f"          | {g.rstrip()[:90]}")
        bad += not ok
    print(f"{len(CASES) + 1 - bad}/{len(CASES) + 1} passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
