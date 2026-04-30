#ifndef IRXENV_H
#define IRXENV_H

#ifdef __MVS__
int is_tso(void) asm("ISTSO");
#else
int is_tso(void);
#endif

/* TSO private storage subpool — IRXTSPRM sets SUBPOOL=78 so that the
 * ENVBLOCK allocated under TSO/IKJEFT01 survives across subtask transitions.
 * Batch uses subpool 0 (system default). */
#define PARMBLOCK_SUBPOOL_TSO 78

#endif /* IRXENV_H */
