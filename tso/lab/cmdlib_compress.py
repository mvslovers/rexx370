#!/usr/bin/env python3
"""tso/lab/cmdlib_compress.py - back up and compress SYS1.CMDLIB in place.

    python3 tso/lab/cmdlib_compress.py state
    python3 tso/lab/cmdlib_compress.py backup  BKUPDSN
    python3 tso/lab/cmdlib_compress.py compress
    python3 tso/lab/cmdlib_compress.py restore BKUPDSN   # only if needed
    python3 tso/lab/cmdlib_compress.py vtoc      # IEHLIST FORMAT, read only

backup unloads the whole library with IEBCOPY into a new sequential data
set. That reads every member once, so it is also the check that none is
damaged; the member count must match the directory. compress runs IEBCOPY
with INDD = OUTDD on the live library. DISP=SHR: the link list holds the
library shared, so DISP=OLD would wait for ever. The compress moves members
within the existing extents and releases none, so the extent table the
link list took at IPL stays valid (a NEW extent is what gives 106-F).
Nobody should run TSO commands while it runs.

AN IPL MUST FOLLOW A COMPRESS.  The extents stay, but the members move, and
MVS 3.8 keeps the directory entries of the modules named in the resident
BLDL list (SYS1.PARMLIB(IEABLD00): ALLOC, LOGON, LOGOFF, TEST, ... -- they
live in SYS1.CMDLIB) from IPL on.  Moved, those fail with IEA703I 106-F:
the logon CLIST's ALLOC fails (no SYSEXEC), LOGOFF fails and the session
cannot end.  Measured on MVSCE-LAB 2026-09-27, compress JOB01355.
"""
import re
import sys

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

LIB = "SYS1.CMDLIB"
WORK = """//SYSUT3   DD  UNIT=SYSDA,SPACE=(CYL,(5,5))
//SYSUT4   DD  UNIT=SYSDA,SPACE=(CYL,(5,5))
"""


def state(c):
    r = [x for x in c.list_datasets(LIB) if x.get("dsname") == LIB][0]
    m = [x if isinstance(x, str) else x.get("member")
         for x in c.list_members(LIB)]
    print(f"{LIB}: vol {r.get('vol')} extents {r.get('extx')} "
          f"used {r.get('used')}% of {r.get('sizex')} {r.get('spacu')}, "
          f"{len(m)} members")
    return len(m)


def run(c, cfg, name, body):
    jcl = jobcard(name, cfg.jes_jobclass, cfg.jes_msgclass, name) + body
    r = c.submit_jcl(jcl, timeout=900)
    sp = r.spool or ""
    open(f"build/tso/lab/{name.lower()}.spool", "w").write(sp)
    print(f"{r.jobid} {r.status} rc={r.rc}  spool build/tso/lab/"
          f"{name.lower()}.spool")
    for line in sp.splitlines():
        if re.search(r"IEB1(?!67I|54I|52I)\d\dI|IEF142I|IEF272I|ABEND|"
                     r"IEC\d+I|IEB\d+E", line):
            print("  ", line.strip()[:110])
    return r, sp


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in (
            "state", "backup", "compress", "restore", "vtoc"):
        print(__doc__)
        return 2
    cfg, c = L.client()
    op = sys.argv[1]
    if op == "state":
        state(c)
        return 0
    n = state(c)
    if op == "vtoc":
        run(c, cfg, "CMDVTOC", f"""
//LIST     EXEC PGM=IEHLIST
//SYSPRINT DD  SYSOUT=*
//VOL      DD  UNIT=3350,VOL=SER=MVSRES,DISP=OLD
//SYSIN    DD  *
  LISTVTOC FORMAT,VOL=3350=MVSRES,DSNAME={LIB}
/*
//
""")
        return 0
    if op == "backup":
        bk = sys.argv[2]
        r, sp = run(c, cfg, "CMDBKUP", f"""
//BKUP     EXEC PGM=IEBCOPY,REGION=4096K
//SYSPRINT DD  SYSOUT=*
//IN       DD  DSN={LIB},DISP=SHR
//OUT      DD  DSN={bk},DISP=(NEW,CATLG,DELETE),
//             UNIT=SYSDA,SPACE=(CYL,(100,20),RLSE)
{WORK}//SYSIN    DD  *
  COPY INDD=IN,OUTDD=OUT
/*
//
""")
        got = len(re.findall(r"IEB154I\s+\S+\s+HAS BEEN SUCCESSFULLY", sp)) \
            or len(re.findall(r"IEB167I", sp))
        print(f"members unloaded: {got} (directory: {n})")
        return 0 if r.rc == 0 else 1
    if op == "compress":
        r, sp = run(c, cfg, "CMDCOMP", f"""
//COMP     EXEC PGM=IEBCOPY,REGION=4096K
//SYSPRINT DD  SYSOUT=*
//LIB      DD  DSN={LIB},DISP=SHR
{WORK}//SYSIN    DD  *
  COPY INDD=LIB,OUTDD=LIB
/*
//
""")
        state(c)
        return 0 if r.rc == 0 else 1
    if op == "restore":
        bk = sys.argv[2]
        r, sp = run(c, cfg, "CMDREST", f"""
//REST     EXEC PGM=IEBCOPY,REGION=4096K
//SYSPRINT DD  SYSOUT=*
//IN       DD  DSN={bk},DISP=SHR
//OUT      DD  DSN={LIB},DISP=SHR
{WORK}//SYSIN    DD  *
  COPY INDD=((IN,R)),OUTDD=OUT
/*
//
""")
        state(c)
        return 0 if r.rc == 0 else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
