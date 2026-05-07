#ifndef IRXLOAD_H
#define IRXLOAD_H

/* irxload.h - IRXLOAD Programming Service interface
**
** Ref: SC28-1883-0 §14 (IRXLOAD Programming Service)
** Ref: WP-CPS-07 / TSK-219 / GitHub mvslovers/rexx370#118
*/

#include "irx.h"

/* Function codes (CL8, blank-padded). */
#define IRXLOAD_FC_LOAD "LOAD    "
#define IRXLOAD_FC_FREE "FREE    "

/* Return codes. */
#define IRXLOAD_OK    0     /* success */
#define IRXLOAD_NOMEM 4     /* storage not available; also returned when an \
                             * exec exceeds available environment storage */
#define IRXLOAD_NOTFOUND 8  /* member or DD not found */
#define IRXLOAD_ERROR    20 /* invalid argument / bad eye-catcher */

/* IRXLOAD VLIST follows the z/OS-stage of the spec, not V1.
 * SC28-1883-0 §14 (V1, Dec 1988) defined three parameters
 * (funccode, EXECBLK, INSTBLK). P4 (ENVBLOCK) and P5 (return
 * code) are z/OS additions that became standard in later TSO/E
 * versions. rexx370 implements the 5-parameter z/OS form.
 *
 * Refs: z/OS REXX Reference (current edition) — canonical VLIST
 *       SC28-1883-0 §14 — V1 baseline (3-parameter form)
 */

/* C-core dispatcher — called from asm/irxload.asm BUILDC section.
 * The asm() alias makes the linker emit the symbol "IRXLDISP",
 * which the =V(IRXLDISP) reference in the asm wrapper resolves to.
 *
 * funccode  - pointer to CL8 function code ('LOAD    ' or 'FREE    ')
 * execblk   - EXECBLK identifying the exec (LOAD: required; FREE: ignored)
 * instblk_p - LOAD: output INSTBLK pointer; FREE: pointer to be freed
 * envblk    - owning ENVBLOCK (NULL → default subpool)
 * retval    - output: return code written to caller's P5 slot
 */
int irx_load_dispatch(const char *funccode,
                      struct execblk *execblk,
                      struct instblk **instblk_p,
                      struct envblock *envblk,
                      int *retval) asm("IRXLDISP");

#endif /* IRXLOAD_H */
