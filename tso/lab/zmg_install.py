#!/usr/bin/env python3
"""tso/lab/zmg_install.py - install the TSO usermod on MVSCE-LAB, step by step.

    python3 tso/usermod.py                     # build/tso/ZMG0002.smp
    python3 tso/lab/zmg_install.py backup      # IKJEFT01/IKJEFT0A, EXEC/EX
    python3 tso/lab/zmg_install.py receive
    python3 tso/lab/zmg_install.py applycheck
    python3 tso/lab/zmg_install.py apply
    python3 tso/lab/zmg_install.py verify

APPLY only, never ACCEPT (see the cover letter in tso/usermod/ZMG0002.mcs).
verify is the real test: a condition code says the job ran, not that the
right bytes landed. It compares the load modules SMP built in SYS1.LPALIB
and SYS1.CMDLIB byte for byte with the ones tso/lmod_link.py testlib put
into REXX370.TSO.LINKLIB, which are the modules the batch TMP tests ran
(JOB01253/JOB01255). Same decks and same link-edit order, so the two must
be identical.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

SYSMOD = "ZMG0002"
STREAM = Path(f"build/tso/{SYSMOD}.smp")
OUT = Path("build/tso/lab")
TESTLIB = "REXX370.TSO.LINKLIB"


def submit(c, cfg, name, body, out):
    jcl = jobcard(name, cfg.jes_jobclass, cfg.jes_msgclass,
                  f"{SYSMOD} INSTALL") + body
    long = [l for l in jcl.splitlines() if l.startswith("//") and len(l) > 71]
    if long:
        raise SystemExit("long cards: %r" % long)
    r = c.submit_jcl(jcl, timeout=900)
    sp = r.spool or ""
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / out).write_text(sp)
    print(f"{r.jobid} {r.status} rc={r.rc}  spool: {OUT / out}")
    return sp


def smp(c, cfg, name, stmt, out, ptfin=None):
    extra = f"//HMASMP.SMPPTFIN DD DSN={ptfin},DISP=SHR\n" if ptfin else ""
    body = f"""
//SMP     EXEC SMPAPP
//HMASMP.LPALIB  DD DSN=SYS1.LPALIB,DISP=SHR
//HMASMP.CMDLIB  DD DSN=SYS1.CMDLIB,DISP=SHR
//HMASMP.AOST4   DD DSN=SYS1.AOST4,DISP=SHR
{extra}//HMASMP.SMPCNTL DD *
 {stmt}
/*
//
"""
    sp = submit(c, cfg, name, body, out)
    for line in sp.splitlines():
        if re.search(r"HMA\d{4}|^ZMG\d{4} |^MOD |IEW\d{3}[1-9]|IEF142I", line):
            print("  ", line.rstrip()[:120])
    return sp


def backup(c, cfg):
    bk = f"{cfg.hlq}.{SYSMOD}.BACKUP"
    if c.dataset_exists(bk):
        raise SystemExit(f"{bk} exists - refusing to overwrite a backup")
    body = f"""
//LPA      EXEC PGM=IEBCOPY,REGION=4096K
//SYSPRINT DD SYSOUT=*
//SYSUT3   DD UNIT=SYSDA,SPACE=(CYL,(1,1))
//SYSUT4   DD UNIT=SYSDA,SPACE=(CYL,(1,1))
//LPA      DD DSN=SYS1.LPALIB,DISP=SHR
//CMD      DD DSN=SYS1.CMDLIB,DISP=SHR
//OUT      DD DSN={bk},DISP=(NEW,CATLG,DELETE),
//            UNIT=SYSDA,SPACE=(TRK,(10,5,5)),
//            DCB=(RECFM=U,BLKSIZE=19069,DSORG=PO)
//SYSIN    DD *
  COPY INDD=LPA,OUTDD=OUT
  SELECT MEMBER=(IKJEFT01,IKJEFT0A)
  COPY INDD=CMD,OUTDD=OUT
  SELECT MEMBER=(EXEC,EX)
/*
//
"""
    sp = submit(c, cfg, "ZMGBKUP", body, "zmg_backup.spool")
    for line in sp.splitlines():
        if "IEB154I" in line or "IEB147I" in line:
            print("  ", line.strip())


def receive(c, cfg):
    ds = f"{cfg.hlq}.{SYSMOD}.SMPPTFIN"
    if c.dataset_exists(ds):
        c.delete_dataset(ds)
    c.create_dataset(ds, "PS", "FB", 80, 3120, ["TRK", 5, 5], "SYSDA")
    c.upload_binary(ds, STREAM.read_bytes())
    smp(c, cfg, "ZMGRECV", f"RECEIVE SELECT({SYSMOD}) .",
        "zmg_receive.spool", ptfin=ds)
    members = [m if isinstance(m, str) else m.get("member")
               for m in c.list_members("SYS1.SMPPTS")]
    print(f"  SMPPTS({SYSMOD}) present: {SYSMOD in members}")


def verify(c, cfg):
    body = f"""
//SMPL    EXEC SMPAPP
//SMPCNTL  DD  *
 LIST CDS SYSMOD({SYSMOD}) .
 LIST CDS MOD(IKJEFT01,IKJEFTRX,IKJCT430,IKJCT437) .
 LIST CDS LMOD(IKJEFT01,EXEC) .
/*
//LPA      EXEC PGM=AMBLIST
//SYSPRINT DD SYSOUT=*
//SYSLIB   DD DSN=SYS1.LPALIB,DISP=SHR
//SYSIN    DD *
 LISTLOAD OUTPUT=MODLIST,MEMBER=IKJEFT01
/*
//CMD      EXEC PGM=AMBLIST
//SYSPRINT DD SYSOUT=*
//SYSLIB   DD DSN=SYS1.CMDLIB,DISP=SHR
//SYSIN    DD *
 LISTLOAD OUTPUT=MODLIST,MEMBER=EXEC
/*
//TST      EXEC PGM=AMBLIST
//SYSPRINT DD SYSOUT=*
//SYSLIB   DD DSN={TESTLIB},DISP=SHR
//SYSIN    DD *
 LISTLOAD OUTPUT=MODLIST,MEMBER=(IKJEFT01,EXEC)
/*
//
"""
    sp = submit(c, cfg, "ZMGVRFY", body, "zmg_verify.spool")
    for line in sp.splitlines():
        if re.search(r"^ZMG\d{4}|STATUS|RMID|LKED CONTROL|^\s+(ORDER|ALIAS|"
                     r"ENTRY|SETCODE|INCLUDE)|SYSTEM LIBRARY", line):
            print("  ", line.rstrip()[:110])
    mods = {}
    for p in sp.split("MEMBER NAME")[1:]:
        mods.setdefault(p.split()[0], []).append(p)
    bad = 0
    for name in ("IKJEFT01", "EXEC"):
        smp_built, tested = mods[name][0], mods[name][-1]
        a, b = L.text_image(smp_built), L.text_image(tested)
        diff = [o for o in set(a) | set(b) if a.get(o) != b.get(o)]
        print(f"  {name}: SMP-built {L.cesd(smp_built)}")
        print(f"  {name}: tested    {L.cesd(tested)}")
        print(f"  {name}: {len(a)} vs {len(b)} bytes, {len(diff)} differ")
        bad += bool(diff) or not a
    print("VERIFY", "FAILED" if bad else "OK")
    return 1 if bad else 0


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    cfg, c = L.client()
    if cmd == "backup":
        backup(c, cfg)
    elif cmd == "receive":
        receive(c, cfg)
    elif cmd == "applycheck":
        smp(c, cfg, "ZMGAPCK", f"APPLY SELECT({SYSMOD}) CHECK .",
            "zmg_applycheck.spool")
    elif cmd == "apply":
        smp(c, cfg, "ZMGAPPLY", f"APPLY SELECT({SYSMOD}) .",
            "zmg_apply.spool")
    elif cmd == "verify":
        return verify(c, cfg)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
