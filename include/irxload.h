#ifndef IRXLOAD_H
#define IRXLOAD_H

/* irxload.h - IRXLOAD Programming Service interface
**
** Ref: SC28-1883-0 §14 (IRXLOAD Programming Service)
** Ref: WP-CPS-07 / TSK-219 / GitHub mvslovers/rexx370#116
*/

#include "irx.h"

/* Function codes (CL8, blank-padded). */
#define IRXLOAD_FC_LOAD "LOAD    "
#define IRXLOAD_FC_FREE "FREE    "

/* Return codes. */
#define IRXLOAD_OK       0  /* success */
#define IRXLOAD_NOMEM    4  /* storage not available */
#define IRXLOAD_NOTFOUND 8  /* member or DD not found */
#define IRXLOAD_ERROR    20 /* invalid argument / bad eye-catcher */

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
