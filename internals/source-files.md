# Source file map

Moved verbatim from `CLAUDE.md` on 2026-10-19. The table is not
exhaustive: `git ls-files src` is the current list (for example
`irx#bvm.c`, `irx#bcom.c`, `irx#bctl.c`, `irx#load.c`, `irx#jcl.c`
are not in it).

| File | Member | Purpose |
|------|--------|---------|
| `src/irx#init.c` | IRX#INIT | IRXINIT — environment initialization |
| `src/irx#term.c` | IRX#TERM | IRXTERM — environment termination |
| `src/irx#stor.c` | IRX#STOR | Storage management replaceable routine; GETMAIN/FREEMAIN by inline assembler (#298) |
| `src/irx#svc.c`  | IRX#SVC  | LOAD/DELETE by inline assembler, no libc370 (#298) |
| `src/irx#envt.c` | IRX#ENVT | REXX370_BYTECODE / REXX370_BCDEBUG for C hosts (IRXJCL, tests); IRXINIT reads no env vars (#298) |
| `src/irx#anch.c` | IRX#ANCH | ECTENVBK anchor (push/pop discipline) |
| `src/irx#uid.c` | IRX#UID | User ID replaceable routine, load module IRXUID (#255) |
| `src/irx#msid.c` | IRX#MSID | Message ID replaceable routine, load module IRXMSGID (#255) |
| `src/irx#tokn.c` | IRX#TOKN | Tokenizer (WP-10) |
| `src/irx#lstr.c` | IRX#LSTR | lstring370 adapter (WP-11b) |
| `src/irx#vpol.c` | IRX#VPOL | Variable pool (WP-12) |
| `src/irx#pars.c` | IRX#PARS | Parser + expression evaluator (WP-13) |
| `src/irx#io.c`   | IRX#IO   | Default I/O routine, load module IRXINOUT; writes OUTDD through `asm/irxqsam.asm` (WP-14, #255, #302) |
| `src/irx#qsmh.c` | IRX#QSMH | Host stand-in for `asm/irxqsam.asm` (`[host] replace`) |
| `src/irx#tsio.c` | IRX#TSIO | TSO I/O routine IRXIOTSO, writes via PUTLINE (WP-33-TSO) |
| `src/irx#putl.c` | IRX#PUTL | Host stand-in for `asm/putlin.asm` (captures PUTLINE calls) |
| `src/irx#ldqs.c` | IRX#LDQS | IRXLOAD's member reader: stdio, needs a C runtime (batch) |
| `src/irx#ldbp.c` | IRX#LDBP | IRXLDTSO's member reader: BPAM via `asm/irxbpam.asm`, no C runtime (#230) |
| `src/irx#ctrl.c` | IRX#CTRL | Control flow: DO/IF/SELECT/CALL/SIGNAL (WP-15) |
| `src/irx#exec.c` | IRX#EXEC | End-to-end execution pipeline irx_exec_run() (WP-18) |
| `src/irx#cond.c` | IRX#COND | Condition raise helper (irx_cond_raise, WP-21a) |
| `src/irx#bif.c`  | IRX#BIF  | BIF argument-validation helpers (WP-21a); lookup is `irx_bif_find_local()` in IRX#BIFS |
| `src/irx#bifs.c` | IRX#BIFS | All §4 BIFs — string (WP-21a) + numeric, conversion, reflection, environment (WP-21b) |

