#!/usr/bin/env python3
"""tso/lab/exec_test.py - implicit EXEC through the patched modules in
REXX370.TSO.LINKLIB, batch TMP with that library as STEPLIB.

    python3 tso/lab/exec_test.py new-tmp     # patched TMP + patched EXEC
    python3 tso/lab/exec_test.py ibm-tmp     # IBM TMP (LPA) + patched EXEC
    python3 tso/lab/exec_test.py drop-tmp    # remove IKJEFT01/IKJEFT0A from
                                             # the test library for ibm-tmp

The STEPLIB supplies PGM=IKJEFT01 and the EXEC command processor ahead of
the LPA and SYS1.CMDLIB. For ibm-tmp the library must not hold IKJEFT01 /
IKJEFT0A (remove them first; tso/lmod_link.py testlib ikjeft01 puts them
back). The members exercised live in IBMUSER.RXT.EXEC / .PROC:

    %RXA   SYSEXEC, no comment         -> REXX
    RXB    SYSPROC, /* REXX */ line 1   -> REXX (implicit without %)
    %RXC   SYSPROC, CLIST               -> CLIST
    %RXD   SYSPROC, CLIST, a comment in line 1 without REXX -> CLIST
           (the UY16532 comment path of IKJCT430, which carried two
           fixed displacements; see tso/TODO_IKJCT437.md)
    %RXZZ  nowhere                      -> not found
    EXEC 'IBMUSER.RXT.PROC(RXC)'        -> explicit path, CLIST, no hook
    TIME                                -> the TMP is still alive afterwards
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

LIB = "REXX370.TSO.LINKLIB"
BODY = f"""
//TMP      EXEC PGM=IKJEFT01,REGION=4096K
//STEPLIB  DD  DSN={LIB},DISP=SHR
//SYSEXEC  DD  DSN=IBMUSER.RXT.EXEC,DISP=SHR
//SYSPROC  DD  DSN=IBMUSER.RXT.PROC,DISP=SHR
//SYSTSPRT DD  SYSOUT=*
//SYSPRINT DD  SYSOUT=*
//SYSUDUMP DD  SYSOUT=*
//SYSTSIN  DD  *
 %RXA
 RXB
 %RXC
 %RXD
 %RXZZ
 EXEC 'IBMUSER.RXT.PROC(RXC)'
 TIME
/*
//
"""

REXX_A = "HELLO FROM SYSEXEC RXA"
REXX_B = "HELLO FROM SYSPROC RXB"
CLIST_C = "HELLO FROM CLIST RXC"
CLIST_D = "HELLO FROM CLIST RXD"
RXD_TEXT = "/* A PLAIN CLIST, COMMENT IN LINE 1 */\nWRITE HELLO FROM CLIST RXD\n"


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
    print([m if isinstance(m, str) else m.get("member")
           for m in c.list_members(LIB)])
    return 0


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "drop-tmp":
        cfg, c = L.client()
        return drop_tmp(c, cfg)
    if mode not in ("new-tmp", "ibm-tmp"):
        print(__doc__)
        return 2
    cfg, c = L.client()
    members = [m if isinstance(m, str) else m.get("member")
               for m in c.list_members(LIB)]
    has_tmp = "IKJEFT01" in members
    if (mode == "new-tmp") != has_tmp:
        print(f"{LIB} holds {members}: wrong state for {mode}")
        return 2
    if "RXD" not in [m if isinstance(m, str) else m.get("member")
                     for m in c.list_members("IBMUSER.RXT.PROC")]:
        c.write_member("IBMUSER.RXT.PROC", "RXD", RXD_TEXT)
    jcl = jobcard("RXEXECT", cfg.jes_jobclass, cfg.jes_msgclass,
                  "EXEC TEST") + BODY
    r = c.submit_jcl(jcl, timeout=300)
    sp = r.spool or ""
    out = Path("build/tso/lab") / f"exec_test_{mode}.spool"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(sp)
    print(f"{r.jobid} {r.status} rc={r.rc}  spool: {out}")
    joblog = sp.split("--- JESYSMSG ---")[0]
    tsprt = sp.split("--- SYSTSPRT ---", 1)[1].split("\n--- ", 1)[0] \
        if "--- SYSTSPRT ---" in sp else ""
    print("----- SYSTSPRT -----")
    print(tsprt.rstrip())
    print("--------------------")
    for l in joblog.splitlines():
        if re.search(r"IKJ569|IEA995I|IEF450I|ABEND|IEA703I", l):
            print("  joblog:", l.strip()[:100])

    env = "IKJ56942I" in joblog
    checks = [
        ("REXX env at logon", env == (mode == "new-tmp")),
        ("no abend (job log and TMP-caught IKJ56641I)",
         r.status != "ABEND" and "IEF450I" not in joblog
         and "IKJ56641I" not in tsprt),
        ("CLIST RXD with comment in line 1 ran as CLIST", CLIST_D in tsprt),
        ("CLIST RXC ran twice (implicit + explicit)",
         tsprt.count(CLIST_C) == 2),
        ("TIME ran afterwards", "IKJ56650I" in tsprt),
    ]
    if mode == "new-tmp":
        checks += [("RXA ran as REXX", REXX_A in tsprt),
                   ("RXB ran as REXX", REXX_B in tsprt)]
    else:
        checks += [("RXA not run as REXX (no env)", REXX_A not in tsprt),
                   ("RXB not run as REXX (no env)", REXX_B not in tsprt)]
    bad = 0
    for name, ok in checks:
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        bad += not ok
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
