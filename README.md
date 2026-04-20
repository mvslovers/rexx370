# REXX/370

**REXX Interpreter for MVS 3.8j — Interface-compatible with IBM TSO/E Version 2 REXX**

## Overview

REXX/370 is a from-scratch implementation of a REXX language interpreter for MVS 3.8j,
designed to be interface-compatible with the TSO/E Version 2 REXX specification
(SC28-1883-0, December 1988).

### Design Goals

- **IBM Interface Compatibility** — Identical control block layouts (ENVBLOCK, PARMBLOCK,
  EXECBLK, INSTBLK, EVALBLOCK, SHVBLOCK), compatible IRXxxxx programming services,
  compatible replaceable routine interfaces
- **Reentrant** — All modules RENT, all working storage via GETMAIN, no static data
  modification. Multiple concurrent REXX environments in the same address space.
- **Embeddable** — Usable from HTTPD (CGI), ISPF, UFSD, Batch, STCs via
  replaceable I/O routines (SAY/PULL hooks)
- **SAA Procedures Language Compliance** — Full REXX language as defined by SAA CPI

### Non-Goals (for now)

- DBCS support (Appendix B — deferred)
- ISPEXEC / ISREDIT integration (requires ISPF)
- 31-bit addressing (MVS 3.8j is 24-bit only)

## Project Structure

```
rexx370/
  inc/            C headers
    irx.h           IBM-compatible control block definitions
    irxanchor.h     ECTENVBK anchor API (read-mostly: claim when NULL,
                    clear only when still holding)
    irxwkblk.h      Internal Work Block (per-environment interpreter state)
    irxfunc.h       Service function prototypes
  src/            C source (CRENT370)
    irxstor.c       Storage Management Replaceable Routine
    irxinit.c       IRXINIT - Environment Initialization
    irxterm.c       IRXTERM - Environment Termination
    irx#anch.c      ECTENVBK anchor: push on IRXINIT, pop on IRXTERM
    irxuid.c        User ID Replaceable Routine
    irxmsgid.c      Message ID Replaceable Routine
  mac/            HLASM macros and DSECTs
    irxenvb.mac     ENVBLOCK DSECT
    irxparm.mac     PARMBLOCK DSECT
  test/           Test programs
    test_phase1.c   Phase 1 smoke test
  doc/            Documentation
    workpackages.md Work package descriptions for implementation
  jcl/            Sample JCL
  build/          Build artifacts
  project.toml    Build configuration
```

## Build

Requires: c2asm370, CRENT370, HLASM (for macros)

```
mbt build
```

## Architecture

See: [REXX/370 Architecture Design v0.1.0](doc/) and the
[Notion Concept Page](https://www.notion.so/3283d9938787811ba3f4d3308b254cad)

## References

- SC28-1883-0: TSO/E V2 REXX Reference (December 1988) — Primary specification
- SC28-1882: TSO/E V2 REXX User's Guide
- SC26-4358: SAA CPI Procedures Language Reference
- GC28-0684: MVS/370 System Programming Library: Supervisor

## License

(c) 2026 mvslovers
