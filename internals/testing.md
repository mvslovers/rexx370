# Testing: MVS runs, host runs, historical counts

Moved from `CLAUDE.md` on 2026-10-19. The everyday commands
(`make test`, `make test-host`, `make test-mvs`) stay in `CLAUDE.md`.

The counts at the end are a snapshot from WP-I1c.2; `project.toml` now
declares 64 `[[test]]` blocks.

Every work package produces a `test/tst*.c` source file that
doubles as (a) a host-compiled unit test and (b) an MVS
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

### Host run

```bash
make test-host                       # build + run every host-runnable test
make test-host ARGS="--only TSTTOKN" # just one (repeat --only for more)
```

`make test-host` (mbt's `mk/mbt.mk`) compiles each `[[test]]` whose
sources are portable C with the host compiler and runs it natively,
gating on the exit code — the fast inner loop, no MVS round-trip.
Tests carrying assembler sources (the VLIST wrappers, for instance) or
marked `host = false` are skipped there; `make test-mvs` still runs
them.

The hand-written gcc recipe that used to stand here was removed on
2026-10-19: its source lists predated several files (`irx#bvm.c`,
`irx#bcom.c`, `irx#bctl.c`, `irx#load.c`, ...) and it passed them
through shell variables, which zsh does not word-split.

### Historical counts

Recorded at the WP-I1c.2 close-out as "15 binaries, 1200 tests green";
the rows below, kept as recorded, are 16 tests adding up to 1217.

| Test | Covers | Checks |
|---|---|---|
| tstphas1 | Phase 1 smoke tests | 38 |
| tsttokn | Tokenizer (WP-10) | 70 |
| tstlstr | lstring adapter (WP-11b) | 50 |
| tstvpol | Variable pool (WP-12) | 47 |
| tstprsr | Parser (WP-13) | 39 |
| tstsay | SAY / I/O (WP-14) | 27 |
| tstctrl | Control flow (WP-15) | 62 |
| tstparse | PARSE instruction (WP-16) | 74 |
| tstproc | PROCEDURE EXPOSE (WP-17) | 53 |
| tsthelo | Hello-world end-to-end (WP-18) | 16 |
| tstanrm | ECTENVBK anchor TSOFL-conditional | 29 |
| tstfind | FINDENVB + CHEKENVB (WP-I1c.2) | 44 |
| tstarit | Arithmetic engine (WP-20) | 128 |
| tstarext | Direct IRXARITH API (WP-20 + B) | 113 |
| tstbif | BIF lookup (WP-21a, #254) | 17 |
| tstbifs | All §4 BIFs (WP-21a + WP-21b) | 410 |

`TSTANCH` (the ENVBLOCK anchor smoketest) is linked on MVS as a
standalone load module and invoked from TSO/Batch; it had no host run
of its own.
