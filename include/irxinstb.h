#ifndef IRXINSTB_H
#define IRXINSTB_H

/* irxinstb.h - INSTBLK compile-time assertions
**
** The authoritative INSTBLK layout is in include/irx.h (struct instblk).
** This header adds static size/layout checks that catch any drift.
**
** Ref: CON-1 §3.4 (INSTBLK byte-exact layout)
** Ref: WP-CPS-07 / TSK-219 / GitHub mvslovers/rexx370#116
*/

#include "irx.h"

/* INSTBLK header must be exactly 128 bytes on MVS (pointer = 4 bytes).
 * Assertion is MVS-only: on 64-bit hosts the struct is larger due to
 * pointer widening and different struct padding. */
#ifdef __MVS__
typedef char irxinstb_hdrlen_ok[(sizeof(struct instblk) == INSTBLK_HDRLEN) ? 1 : -1];
#endif

#endif /* IRXINSTB_H */
