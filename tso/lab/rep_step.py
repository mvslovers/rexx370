#!/usr/bin/env python3
"""MVSCE-LAB TSO repair, step 1 - run from the rexx370 repo root.

    python3 <this> applycheck | apply | acceptcheck | accept | alloc | apf

(The IKJEFT01 relink into SYS1.LPALIB is rep_relink.py LPALIB.)
Every subcommand saves the spool next to this script and prints what has to
be checked; a condition code alone proves nothing.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, "mbt/scripts")
sys.path.insert(0, "tso")
from mbt.jcl import jobcard  # noqa: E402
import lmod_link as L  # noqa: E402

HERE = Path("build/tso/lab")
APPLY_IDS = "UY43678,UZ42826"
ACCEPT_IDS = "UY43678,UZ42826,UY16532"
NEWLIB = "REXX370.TSO.LINKLIB"
NEWVOL = "MVS000"


def submit(c, cfg, name, body, out):
    jcl = jobcard(name, cfg.jes_jobclass, cfg.jes_msgclass, "TSO REPAIR") + body
    long = [l for l in jcl.splitlines() if l.startswith("//") and len(l) > 71]
    if long:
        raise SystemExit("long cards: %r" % long)
    r = c.submit_jcl(jcl, timeout=900)
    sp = r.spool or ""
    (HERE / out).write_text(sp)
    print(f"{r.jobid} {r.status} rc={r.rc}  spool: {HERE / out}")
    return sp


def smp(c, cfg, name, stmt, out):
    body = f"""
//SMP     EXEC SMPAPP
//HMASMP.LPALIB  DD DSN=SYS1.LPALIB,DISP=SHR
//HMASMP.AOST4   DD DSN=SYS1.AOST4,DISP=SHR
//HMASMP.SMPCNTL DD *
 {stmt}
/*
//
"""
    sp = submit(c, cfg, name, body, out)
    for line in sp.splitlines():
        if re.search(r"HMA\d{4}|^U[YZ]\d{5} |^MOD |IEW\d{4}|IEF142I", line):
            print("  ", line.rstrip()[:120])
    return sp


def lpalib_verify(c, cfg):
    body = """
//AMB      EXEC PGM=AMBLIST
//SYSPRINT DD SYSOUT=*
//SYSLIB   DD DSN=SYS1.LPALIB,DISP=SHR
//SYSIN    DD *
 LISTLOAD OUTPUT=MODLIST,MEMBER=(IKJEFT01,IKJEFT0A,IKJEFT02,IKJEFT04)
 LISTLOAD OUTPUT=MODLIST,MEMBER=(IKJEFT07)
/*
//
"""
    sp = submit(c, cfg, "TSOREPVF", body, "rep_verify.spool")
    for p in sp.split("MEMBER NAME")[1:]:
        img = L.text_image(p)
        print(p.split()[0], L.cesd(p), len(img))
    print("check: IKJEFTSC now 0xED0 (UY43678) in IKJEFT01;"
          " IKJEFT06 at the UZ42826 level in all four LMODs")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    cfg, c = L.client()
    if cmd == "applycheck":
        smp(c, cfg, "TSOAPCK", f"APPLY SELECT({APPLY_IDS}) CHECK .",
            "rep_applycheck.spool")
    elif cmd == "apply":
        smp(c, cfg, "TSOAPPLY", f"APPLY SELECT({APPLY_IDS}) .",
            "rep_apply.spool")
        print("check in the spool: HMA2380-style LMOD messages, and the"
              " LKDPRINT INCLUDE statements (target LMOD or AOST4?)")
        lpalib_verify(c, cfg)
    elif cmd == "acceptcheck":
        smp(c, cfg, "TSOACCK", f"ACCEPT SELECT({ACCEPT_IDS}) CHECK .",
            "rep_acceptcheck.spool")
    elif cmd == "accept":
        smp(c, cfg, "TSOACC", f"ACCEPT SELECT({ACCEPT_IDS}) .",
            "rep_accept.spool")
    elif cmd == "alloc":
        body = f"""
//ALLOC    EXEC PGM=IEFBR14
//LIB      DD DSN={NEWLIB},DISP=(NEW,CATLG,DELETE),
//            UNIT=SYSDA,VOL=SER={NEWVOL},SPACE=(TRK,(30,15,20)),
//            DCB=(RECFM=U,BLKSIZE=19069,DSORG=PO)
//
"""
        submit(c, cfg, "TSOALLOC", body, "rep_alloc.spool")
        print([x for x in c.list_datasets(NEWLIB)])
    elif cmd == "apf":
        old = c.read_member("SYS1.PARMLIB", "IEAAPF00")
        lines = old.splitlines()
        if any(NEWLIB in l for l in lines):
            raise SystemExit("IEAAPF00 already names " + NEWLIB)
        vol = [x for x in c.list_datasets(NEWLIB)
               if x.get("dsname") == NEWLIB][0]["vol"]
        last = lines[-1]
        # the last entry carries no comma; give it one after the volser
        m = re.match(r"^( \S+ \S+)(\s.*)$", last[:72])
        lines[-1] = (m.group(1) + "," + m.group(2)[1:]).ljust(72) + last[72:80]
        seq = int(last[72:80]) + 10
        entry = f" {NEWLIB} {vol}".ljust(30) + "REXX370 TSO TEST LIBRARY"
        lines.append(entry.ljust(72) + f"{seq:08d}")
        new = "\n".join(lines) + "\n"
        print(new)
        c.write_member("SYS1.PARMLIB", "IEAAPF00", new)
        print("re-read:")
        print(c.read_member("SYS1.PARMLIB", "IEAAPF00"))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
