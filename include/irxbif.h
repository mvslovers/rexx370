/* ------------------------------------------------------------------ */
/*  irxbif.h - REXX/370 Built-in Function lookup and arg validation   */
/*                                                                    */
/*  Built-in functions live in a static, read-only table compiled     */
/*  into every module that runs REXX (irx#bifs.c). The parser and     */
/*  the bytecode VM resolve a name with irx_bif_find_local(), which   */
/*  always returns a handler linked into the running module.          */
/*                                                                    */
/*  There is no per-environment registry any more: its handler        */
/*  pointers pointed into IRXINIT's copy of the BIFs and IRXEXEC      */
/*  wild-branched into them (#200), and building it was the only      */
/*  reason IRXINIT linked the interpreter (#254).                     */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXBIF_H
#define IRXBIF_H

#include "irx.h"
#include "lstring.h"

struct irx_parser; /* forward decl */

/* ================================================================== */
/*  Handler signature                                                 */
/* ================================================================== */

/* Maximum length of a BIF name (SAA REXX allows up to 250 chars in   */
/* symbols, but real BIFs are well under 16).                         */
#define IRX_BIF_NAME_MAX 16

/* Return codes match IRXPARS_* so the parser can propagate directly. */
typedef int (*irx_bif_handler_t)(struct irx_parser *p,
                                 int argc, PLstr *argv, PLstr result);

/* ================================================================== */
/*  Table entry                                                       */
/* ================================================================== */

struct irx_bif_entry
{
    char name[IRX_BIF_NAME_MAX]; /* upper-case BIF name (NUL-padded)   */
    int min_args;
    int max_args;
    irx_bif_handler_t handler;
};

/* ================================================================== */
/*  Lookup                                                            */
/*                                                                    */
/*  asm() aliases are required because every entry point begins with  */
/*  "irx_bif" — c2asm370 truncates identifiers to 8 characters and    */
/*  they would collide otherwise.                                     */
/* ================================================================== */

/* Resolve a BIF by its upper-case name (length-delimited, not
 * NUL-terminated) to the handler linked into THIS module. Covers the
 * static core-BIF table + ARG. NULL if not a known BIF. */
const struct irx_bif_entry *
irx_bif_find_local(const unsigned char *name, size_t len) asm("IRXBIFFL");

/* ================================================================== */
/*  Argument-validation helpers                                       */
/*                                                                    */
/*  Each helper raises the appropriate SYNTAX 40.x condition via      */
/*  irx_cond_raise() on failure and returns a non-zero code that the  */
/*  handler should propagate. A zero return means "validated; use     */
/*  *out".                                                            */
/* ================================================================== */

/* Require argv[idx] to be present (argc > idx && argv[idx] non-NULL). */
int irx_bif_require_arg(struct irx_parser *p, int argc, PLstr *argv,
                        int idx, const char *bif_name) asm("IRXBIFRA");

/* Parse argv[idx] as a non-negative whole number into *out. */
int irx_bif_whole_nonneg(struct irx_parser *p, PLstr *argv,
                         int idx, const char *bif_name,
                         long *out) asm("IRXBIFNN");

/* Parse argv[idx] as a strictly-positive whole number into *out. */
int irx_bif_whole_positive(struct irx_parser *p, PLstr *argv,
                           int idx, const char *bif_name,
                           long *out) asm("IRXBIFPO");

/* Parse optional argv[idx] as a non-negative whole number.
 * If omitted (argc <= idx or empty string), *out is set to default_val
 * and 0 is returned. */
int irx_bif_opt_whole(struct irx_parser *p, int argc, PLstr *argv,
                      int idx, const char *bif_name,
                      long default_val, long *out) asm("IRXBIFOW");

/* Validate argv[idx] is exactly one character. If omitted, *out is
 * set to default_char. */
int irx_bif_opt_char(struct irx_parser *p, int argc, PLstr *argv,
                     int idx, const char *bif_name,
                     char default_char, char *out) asm("IRXBIFOC");

/* Validate argv[idx] is exactly one character taken from the allowed
 * set (EBCDIC-safe). The allowed set is an upper-case ASCII string of
 * legal option letters. If omitted, *out = default_opt. The returned
 * character is always upper-case. */
int irx_bif_opt_option(struct irx_parser *p, int argc, PLstr *argv,
                       int idx, const char *bif_name,
                       const char *allowed, char default_opt,
                       char *out) asm("IRXBIFOP");

#endif /* IRXBIF_H */
