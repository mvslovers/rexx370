# Testing: MVS runs, host cross-compile recipe, historical counts

Moved verbatim from `CLAUDE.md` on 2026-10-19. The everyday commands
(`make test`, `make test-host`, `make test-mvs`) stay in `CLAUDE.md`.

Not re-checked at the move: the hand-written recipe below predates
several source files (`irx#bvm.c`, `irx#bcom.c`, `irx#bctl.c`,
`irx#load.c`, ...), and it passes source lists through shell variables,
which zsh does not word-split. `make test-host` is the maintained path.
The counts are a snapshot from WP-I1c.2; `project.toml` now declares
64 `[[test]]` blocks.

Every work package produces a `test/tst*.c` source file that
doubles as (a) a Linux/gcc cross-compile unit test and (b) an MVS
standalone load module (`TST*` member via `make test`). Tests use a
simple CHECK macro:

```c
#define CHECK(cond, msg) ...  /* see test/tsttokn.c */
```

### MVS build

```bash
make tsttokn     # build one test module (or `make test` for all)
make test-mvs    # build + deploy the test modules to a TESTLIB + run on MVS
# Or invoke a single module by hand after building/deploying:
#   TSO:   CALL 'hlq.LOAD(TSTTOKN)'
#   Batch: // EXEC PGM=TSTTOKN
```

Each test is declared as a `[[test]]` block in `project.toml` (name /
startup / sources — the full Phase 1 + Phase 2 + EXEC + ARIT chain;
TSTTOKN is the one exception, minimal dependencies). mbt v2 generates
the run JCL from that block. Only a few pure-HLASM VLIST-wrapper edge
cases (tinitvl, ttermvl, tistso) also need a hand-written
`test/jcl/<name>.jcl`.

### Linux cross-compile

`make test-host` builds and runs the host-runnable dual tests natively
— the fast inner loop, no MVS round-trip (MVS-only tests such as the
VLIST wrappers are skipped). To compile a single test by hand, the
superset below links every test correctly. The minimum dep set
for a given test is narrower in principle, but `irx#bifs.c` pulls in
`irx#arith.c` and `irx#pars.c`, and `irx#init.c` pulls in `irx#io.c`,
so for practical purposes the superset is the right default.

```bash
LSTRING_INC="-I ../lstring370/include"
LSTRING_SRC="../lstring370/src/lstr#cor.c  ../lstring370/src/lstr#cvt.c \
             ../lstring370/src/lstr#fmt.c  ../lstring370/src/lstr#srch.c \
             ../lstring370/src/lstr#sub.c  ../lstring370/src/lstr#wrd.c \
             ../lstring370/src/lstr#xlt.c"
PHASE1_SRC="src/irx#init.c src/irx#term.c src/irx#stor.c \
            src/irx#anch.c src/irx#env.c  src/irx#uid.c  src/irx#msid.c \
            src/irx#cond.c src/irx#bif.c  src/irx#bifs.c"
PHASE2_SRC="src/irx#io.c   src/irx#lstr.c src/irx#tokn.c \
            src/irx#vpol.c src/irx#pars.c src/irx#ctrl.c"
ALL_SRC="$PHASE1_SRC $PHASE2_SRC src/irx#exec.c src/irx#arith.c $LSTRING_SRC"

run() {
    gcc -I include $LSTRING_INC -Wall -Wextra -std=gnu99 \
        -o /tmp/$1 "test/$1.c" $ALL_SRC && /tmp/$1
}

run tstphas1    # Phase 1 smoke tests                (38/38)
run tsttokn     # Tokenizer (WP-10)                  (70/70)
run tstlstr     # lstring adapter (WP-11b)           (50/50)
run tstvpol     # Variable pool (WP-12)              (47/47)
run tstprsr     # Parser (WP-13)                     (39/39)
run tstsay      # SAY / I/O (WP-14)                  (27/27)
run tstctrl     # Control flow (WP-15)               (62/62)
run tstparse    # PARSE instruction (WP-16)          (74/74)
run tstproc     # PROCEDURE EXPOSE (WP-17)           (53/53)
run tsthelo     # Hello-world end-to-end (WP-18)     (16/16)
run tstanrm     # ECTENVBK anchor TSOFL-conditional  (29/29)
run tstfind     # FINDENVB + CHEKENVB (WP-I1c.2)     (44/44)
run tstarit     # Arithmetic engine (WP-20)          (128/128)
run tstarext    # Direct IRXARITH API (WP-20 + B)    (113/113)
run tstbif      # BIF lookup (WP-21a, #254)          (17/17)
run tstbifs     # All §4 BIFs (WP-21a + WP-21b)      (410/410)
```

Note: `TSTANCH` (the ENVBLOCK anchor smoketest) is linked on MVS as
a standalone load module but is invoked from TSO/Batch — it does not
have a runnable Linux cross-compile entry of its own.

Full matrix: 15 binaries, **1200 tests green** as of WP-I1c.2 close-out.
