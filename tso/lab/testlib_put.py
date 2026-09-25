#!/usr/bin/env python3
"""tso/lab/testlib_put.py - put rexx370 load modules into REXX370.TSO.LINKLIB.

    python3 tso/lab/testlib_put.py put IRXEXEC [IRXINIT ...]
    python3 tso/lab/testlib_put.py drop EXEC EX
    python3 tso/lab/testlib_put.py list

A batch TMP with STEPLIB=REXX370.TSO.LINKLIB takes these ahead of the link
list, so a fix can be tested without replacing anything in SYS2.LINKLIB (whose
link-list extents are fixed until the next IPL). The modules come from the
mbt build (build/<NAME>.iebcopy); each is packed with ld370 -xmit, received
into a scratch PDS and copied in with IEBCOPY replace.
"""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

LIB = "REXX370.TSO.LINKLIB"


def members(c):
    return [m if isinstance(m, str) else m.get("member")
            for m in c.list_members(LIB)]


def put(c, cfg, names):
    hlq = cfg.hlq
    out = Path("build/tso/lab")
    out.mkdir(parents=True, exist_ok=True)
    packs = []
    for n in names:
        src = Path(f"build/{n}.iebcopy")
        if not src.exists():
            raise SystemExit(f"{src} missing - run make {n.lower()} first")
        packs.append(f"{n}={src}")
    subprocess.run(["ld370", "--pack", *packs, "-o", str(out / "TESTPUT"),
                    "-xmit"], check=True)
    data = (out / "TESTPUT.xmit").read_bytes()
    stage, temp = f"{hlq}.TESTPUT.XMIT", f"{hlq}.TESTPUT.TEMP"
    for ds in (stage, temp):
        if c.dataset_exists(ds):
            c.delete_dataset(ds)
    c.create_dataset(stage, "PS", "FB", 80, 3120, ["TRK", 30, 15], "SYSDA")
    c.upload_binary(stage, data)
    sel = ",".join(f"({n},,R)" for n in names)
    jcl = jobcard("TESTPUT", cfg.jes_jobclass, cfg.jes_msgclass,
                  "TESTLIB PUT") + f"""
//RECV     EXEC PGM=IKJEFT01,REGION=4096K
//SYSTSPRT DD SYSOUT=*
//SYSTSIN  DD *
 RECEIVE INDSN('{stage}') DATASET('{temp}')
/*
//COPY     EXEC PGM=IEBCOPY,REGION=4096K,COND=(0,NE,RECV)
//SYSPRINT DD SYSOUT=*
//IN       DD DSN={temp},DISP=SHR
//OUT      DD DSN={LIB},DISP=SHR
//SYSIN    DD *
  COPY INDD=IN,OUTDD=OUT
  SELECT MEMBER=({sel})
/*
//
"""
    r = c.submit_jcl(jcl, timeout=300)
    sp = r.spool or ""
    print(f"{r.jobid} {r.status} rc={r.rc}")
    for line in sp.splitlines():
        if "IEB154I" in line or "IEF142I" in line or "IEF272I" in line:
            print("  ", line.strip()[:100])
    print(members(c))


def drop(c, cfg, names):
    vol = [x for x in c.list_datasets(LIB) if x.get("dsname") == LIB][0]
    scr = "\n".join(f"  SCRATCH DSNAME={LIB},VOL={vol['dev']}={vol['vol']},"
                    f"MEMBER={n}" for n in names)
    jcl = jobcard("TESTDROP", cfg.jes_jobclass, cfg.jes_msgclass,
                  "TESTLIB DROP") + f"""
//SCR      EXEC PGM=IEHPROGM
//SYSPRINT DD SYSOUT=*
//DD1      DD UNIT={vol['dev']},VOL=SER={vol['vol']},DISP=OLD
//SYSIN    DD *
{scr}
/*
//
"""
    r = c.submit_jcl(jcl, timeout=300)
    print(f"{r.jobid} {r.status} rc={r.rc}")
    print(members(c))


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("put", "drop", "list"):
        print(__doc__)
        return 2
    cfg, c = L.client()
    if sys.argv[1] == "put":
        put(c, cfg, [n.upper() for n in sys.argv[2:]])
    elif sys.argv[1] == "drop":
        drop(c, cfg, [n.upper() for n in sys.argv[2:]])
    else:
        print(members(c))
    return 0


if __name__ == "__main__":
    sys.exit(main())
