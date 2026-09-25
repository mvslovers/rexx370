import sys, re
sys.path.insert(0, "mbt/scripts"); sys.path.insert(0, "tso")
from mbt.jcl import jobcard
import lmod_link as L
cfg, c = L.client()
target = sys.argv[1]            # TEST or LPALIB
H = cfg.hlq + ".TSOREP"
if target == "TEST":
    lmod = f"{H}.RELINK"
    if c.dataset_exists(lmod): c.delete_dataset(lmod)
    lmoddd = (f"//SYSLMOD  DD DSN={lmod},DISP=(NEW,CATLG,DELETE),\n"
              "//            UNIT=SYSDA,SPACE=(TRK,(10,5,5)),\n"
              "//            DCB=(RECFM=U,BLKSIZE=19069,DSORG=PO)")
else:
    lmod = "SYS1.LPALIB"
    lmoddd = "//SYSLMOD  DD DSN=SYS1.LPALIB,DISP=SHR"
jcl = jobcard("TSORELNK", cfg.jes_jobclass, cfg.jes_msgclass, "IKJEFT01 FROM AOST4") + f"""
//LKED     EXEC PGM=IEWL,REGION=4096K,
//            PARM='NCAL,LIST,XREF,LET,RENT,REUS'
//SYSPRINT DD SYSOUT=*
//SYSUT1   DD UNIT=SYSDA,SPACE=(CYL,(1,1))
//AOST4    DD DSN=SYS1.AOST4,DISP=SHR
{lmoddd}
//SYSLIN   DD *
  INCLUDE AOST4(IKJEFT01,IKJEFT06,IKJEFTSC)
  ORDER IKJEFT01(P),IKJEFT06
  ALIAS IKJEFT0A
  ENTRY IKJEFT01
  SETCODE AC(1)
  NAME IKJEFT01(R)
/*
//AMB      EXEC PGM=AMBLIST,COND=(4,LT,LKED)
//SYSPRINT DD SYSOUT=*
//SYSLIB   DD DSN={lmod},DISP=SHR
//SYSIN    DD *
 LISTLOAD OUTPUT=MODLIST,MEMBER=(IKJEFT01,IKJEFT0A)
/*
//
"""
long = [l for l in jcl.splitlines() if l.startswith("//") and len(l) > 71]
if long: raise SystemExit("long cards: %r" % long)
r = c.submit_jcl(jcl, timeout=300)
sp = r.spool or ""
open(sys.argv[2], "w").write(sp)
print(r.jobid, r.status, r.rc)
for line in sp.splitlines():
    if re.search(r"IEF142I|IEF272I|IEW\d{3}[1-9]", line): print("  ", line.strip()[:100])
parts = [p for p in sp.split("MEMBER NAME")[1:]]
for p in parts:
    head = p.split("\n",1)[0].split()
    img = L.text_image(p)
    ec = bytes(img.get(o,0x40) for o in range(4,0x48)).decode("cp037","replace")
    ssi = re.search(r"MODULE SSI:\s+(\S+)", p); apf = re.search(r"APFCODE\s+(\S+)", p)
    print(head[0], "EP", head[-1], "SSI", ssi and ssi.group(1), "AC", apf and apf.group(1),
          L.cesd(p), len(img), "|", ec)
