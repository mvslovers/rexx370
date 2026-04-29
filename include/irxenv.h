#ifndef IRXENV_H
#define IRXENV_H

#ifdef __MVS__
int is_tso(void) asm("ISTSO");
#else
int is_tso(void);
#endif

#endif /* IRXENV_H */
