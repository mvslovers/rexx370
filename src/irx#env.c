#ifndef __MVS__
#include "irxenv.h"

/* Host-only simulation flag. Set to 1 in test harnesses to exercise
 * Stage 1a / Stage 1b secondary in irx_init_findenvb(). Mirrors the
 * _simulated_ectenvbk pattern in irx#anch.c. Default 0 means all
 * cross-compile tests that do not touch this flag see is_tso() == 0,
 * which matches the MVS batch / non-TSO behaviour they were written for. */
int _simulated_is_tso = 0;

int is_tso(void)
{
    return _simulated_is_tso;
}
#else
/* The real is_tso() lives in asm/istso.asm (CSECT ISTSO). An empty
 * translation unit produces no ESD entries and breaks the NCAL linker;
 * this dummy gives it something to see without affecting behaviour. */
static int _irx_env_dummy = 0;
#endif
