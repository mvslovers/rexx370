#!/usr/bin/env python3
"""tso/rxdrv_test.py - install RXDRV on MVS and run the IKJCT437 trail.

Run from the repository root (it uses mbt's mvsMF client and .env):

    tso/build.sh rxdrv
    python3 tso/rxdrv_test.py install   # RXDRV -> SYS2.LINKLIB
    python3 tso/rxdrv_test.py run       # batch TMP job, prints the trail

install WRITES SYS2.LINKLIB. RXDRV has to live in an APF library: the
batch TMP step names SYS2.LINKLIB as STEPLIB, and a non-APF STEPLIB would
de-authorize the step, so the patched IKJEFT01 ends S047. The old member
is copied to BACKUP first. The data set's extent count is compared before
and after: a copy that needs a NEW extent is not visible through the link
list until the next IPL (IEA703I 106-F), so that is reported loudly.

run submits the RXCT437 job: RXDRV is invoked as a TSO COMMAND (a CALL
would pass a PARM list, not a CPPL) for four members:

    RXA   in SYSEXEC              -> REXX, no content check
    RXB   in SYSPROC, /* REXX */  -> REXX
    RXC   in SYSPROC, CLIST       -> not REXX
    RXZZ  nowhere                 -> not REXX

It prints the SYSTSPRT, where an exec's SAY lands through IRXIOTSO. The
C1..C9 probe WTOs were removed from IKJCT437 on 2026-09-25, so the job-log
trail stays empty; tso/lab/exec_test.py tests the real EXEC path instead.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
from mbt.config import MbtConfig  # noqa: E402
from mbt.jcl import jobcard  # noqa: E402
from mbt.mvsmf import MvsMFClient  # noqa: E402

LINKLIB = "SYS2.LINKLIB"
XMIT = Path("build/tso/RXDRV.xmit")

RUN_JCL = """
//TMP      EXEC PGM=IKJEFT01,REGION=4096K
//STEPLIB  DD  DSN=SYS2.LINKLIB,DISP=SHR
//SYSEXEC  DD  DSN=IBMUSER.RXT.EXEC,DISP=SHR
//SYSPROC  DD  DSN=IBMUSER.RXT.PROC,DISP=SHR
//SYSTSPRT DD  SYSOUT=*
//SYSPRINT DD  SYSOUT=*
//SYSUDUMP DD  SYSOUT=*
//SYSTSIN  DD  *
 RXDRV RXA
 RXDRV RXB
 RXDRV RXC
 RXDRV RXZZ
/*
//
"""


def client():
    cfg = MbtConfig("project.toml")
    return cfg, MvsMFClient(host=cfg.mvs_host, port=cfg.mvs_port,
                            user=cfg.mvs_user, password=cfg.mvs_pass)


def extents(c):
    d = c.list_datasets(LINKLIB)[0]
    return int(d["extx"]), d["used"]


def install():
    cfg, c = client()
    hlq = cfg.hlq
    stage, temp, backup = (f"{hlq}.RXDRV.XMIT", f"{hlq}.RXDRV.TEMP",
                           f"{hlq}.RXDRV.BACKUP")
    data = XMIT.read_bytes()
    ext_before, used_before = extents(c)
    print(f"{LINKLIB}: {ext_before} extent(s), {used_before}% used")

    for dsn in (stage, temp):
        if c.dataset_exists(dsn):
            c.delete_dataset(dsn)
    c.create_dataset(stage, "PS", "FB", 80, 3120, ["TRK", 10, 5], "SYSDA")
    c.upload_binary(stage, data)

    # JCL cards end at column 71: one keyword group per line.
    bk_disp = ("DISP=OLD" if c.dataset_exists(backup) else
               "DISP=(NEW,CATLG,DELETE),"
               "\n//            UNIT=SYSDA,SPACE=(TRK,(5,5,5)),"
               "\n//            DCB=(RECFM=U,BLKSIZE=19069,DSORG=PO)")
    jcl = jobcard("RXDRVINS", cfg.jes_jobclass, cfg.jes_msgclass,
                  "RXDRV INSTALL") + f"""
//RECV     EXEC PGM=IKJEFT01,REGION=4096K
//SYSTSPRT DD SYSOUT=*
//SYSTSIN  DD *
 RECEIVE INDSN('{stage}') DATASET('{temp}')
/*
//BACKUP   EXEC PGM=IEBCOPY,REGION=4096K,COND=(0,NE,RECV)
//SYSPRINT DD SYSOUT=*
//IN       DD DSN={LINKLIB},DISP=SHR
//OUT      DD DSN={backup},{bk_disp}
//SYSIN    DD *
  COPY INDD=IN,OUTDD=OUT
  SELECT MEMBER=((RXDRV,,R))
/*
//COPY     EXEC PGM=IEBCOPY,REGION=4096K,
//            COND=((0,NE,RECV),(0,NE,BACKUP))
//SYSPRINT DD SYSOUT=*
//IN       DD DSN={temp},DISP=SHR
//OUT      DD DSN={LINKLIB},DISP=SHR
//SYSIN    DD *
  COPY INDD=IN,OUTDD=OUT
  SELECT MEMBER=((RXDRV,,R))
/*
//
"""
    long = [l for l in jcl.splitlines() if l.startswith("//") and len(l) > 71]
    if long:
        print("JCL card(s) past column 71:", *long, sep="\n  ")
        return 1
    r = c.submit_jcl(jcl, timeout=300)
    print(f"job {r.jobname} {r.jobid}: {r.status} rc={r.rc}")
    for line in (r.spool or "").splitlines():
        if any(k in line for k in ("IEF142I", "IEF272I", "IEB154I", "IEB1",
                                   "RECEIVE", "Receive")):
            print("  ", line.strip()[:100])

    ext_after, used_after = extents(c)
    print(f"{LINKLIB}: {ext_after} extent(s), {used_after}% used")
    if ext_after != ext_before:
        print(f"!!! {LINKLIB} grew from {ext_before} to {ext_after} extents. "
              "Members in the new extent cannot be loaded through the link "
              "list until the next IPL (IEA703I 106-F).")
        return 1
    return 0 if r.rc == 0 else 1


def run():
    cfg, c = client()
    jcl = jobcard("RXCT437", cfg.jes_jobclass, cfg.jes_msgclass,
                  "IKJCT437 TRAIL") + RUN_JCL
    r = c.submit_jcl(jcl, timeout=300)
    print(f"job {r.jobname} {r.jobid}: {r.status} rc={r.rc}\n")
    spool = r.spool or ""
    # The probe WTOs arrive as '+C1 ENTERED' in the job log; the dump
    # has '+C0 SMCT ...' lines, so match the probe shape, not "+C".
    probe = re.compile(r"JOB +\d+ +\+C\d ")
    events = ("IKJ569", "IEA995I", "IEF450I", "IEA703I", "__CRTGET")
    print("===== job log: the C1..C9 trail =====")
    for line in spool.split("--- JESYSMSG ---")[0].splitlines():
        if probe.search(line) or any(e in line for e in events):
            print("  ", line.strip()[:100])
    print("\n===== SYSTSPRT =====")
    marker = "--- SYSTSPRT ---"
    if marker in spool:
        tail = spool.split(marker, 1)[1]
        print(tail.split("\n--- ", 1)[0])
    return 0 if r.status != "ABEND" else 1


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "install":
        sys.exit(install())
    if cmd == "run":
        sys.exit(run())
    print(__doc__)
    sys.exit(2)
