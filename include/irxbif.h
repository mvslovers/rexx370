/* ------------------------------------------------------------------ */
/*  irxbif.h - REXX/370 Built-in Function Registry                   */
/*                                                                    */
/*  Per-environment dynamic registry for built-in functions. The      */
/*  parser calls irx_bif_find() during function-call dispatch; on     */
/*  match it invokes the handler with argument validation already     */
/*  performed.                                                        */
/*                                                                    */
/*  Registration is one-shot: irxinit() registers all core BIFs       */
/*  (string, misc, arithmetic) after wkbi_bif_registry has been       */
/*  allocated. irxterm() frees the registry. No globals.              */
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
/*  Registry entry                                                    */
/* ================================================================== */

struct irx_bif_entry
{
    char name[IRX_BIF_NAME_MAX]; /* upper-case BIF name (NUL-padded)   */
    int min_args;
    int max_args;
    irx_bif_handler_t handler;
};

/* Opaque — full definition in irx#bif.c */
struct irx_bif_registry;

/* ================================================================== */
/*  Return codes                                                      */
/* ================================================================== */

#define IRX_BIF_OK        0
#define IRX_BIF_NOMEM     20
#define IRX_BIF_NOTFOUND  21
#define IRX_BIF_DUPLICATE 22
#define IRX_BIF_BADARG    23

/* ================================================================== */
/*  Registry API                                                      */
/*                                                                    */
/*  asm() aliases are required because every entry point begins with  */
/*  "irx_bif" — c2asm370 truncates identifiers to 8 characters and    */
/*  they would collide otherwise.                                     */
/* ================================================================== */

/* Allocate a registry via irxstor. Returns IRX_BIF_OK on success. */
int irx_bif_create(struct envblock *env,
                   struct irx_bif_registry **out) asm("IRXBIFCR");

/* Release every node and the registry itself. Safe on NULL. */
void irx_bif_destroy(struct envblock *env,
                     struct irx_bif_registry *reg) asm("IRXBIFDS");

/* Add a single BIF to the registry. Name must be upper-case ASCII,
 * length 1..15. Duplicate names are rejected. */
int irx_bif_register(struct envblock *env,
                     struct irx_bif_registry *reg,
                     const char *name, int min_args, int max_args,
                     irx_bif_handler_t handler) asm("IRXBIFRG");

/* Find a BIF by its upper-case name (length-delimited, not
 * NUL-terminated). Returns NULL if not registered. */
const struct irx_bif_entry *
irx_bif_find(const struct irx_bif_registry *reg,
             const unsigned char *name, size_t len) asm("IRXBIFFN");

/* Bulk-register every entry in a static table. Stops at the first
 * entry whose name field is empty. */
int irx_bif_register_table(struct envblock *env,
                           struct irx_bif_registry *reg,
                           const struct irx_bif_entry *table,
                           int count) asm("IRXBIFRT");

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
