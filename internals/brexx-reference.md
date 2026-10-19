# BREXX/370 as a knowledge source

Moved verbatim from `CLAUDE.md` on 2026-10-19.

BREXX/370 (https://github.com/mvslovers/brexx370) is a knowledge
source for REXX BIF implementations and MVS integration patterns.
Do **not** copy code verbatim — reimplemented clean with reentrant
design. Useful files to reference:

| BREXX file | What to learn from it |
|---|---|
| `inc/irx.h` | IBM control block layouts (already extracted) |
| `lstring/*.c` | REXX string operation algorithms |
| `src/nextsymb.c` | Token classification logic |
| `src/interpre.c` | Opcode dispatch patterns (but avoid its globals) |
| `src/variable.c` | Hash table and compound variable resolution |
| `src/rxmvs.c` | MVS BIF implementations (LISTDSI, SYSDSN, etc.) |
| `src/builtin.c` | Built-in function dispatch table |
