/* ------------------------------------------------------------------ */
/*  irxvpool.h - REXX/370 Variable Pool                               */
/*                                                                    */
/*  Per-scope name -> value map that stores all REXX variables for    */
/*  an exec. Chained hash table with dynamic resize, PROCEDURE        */
/*  EXPOSE pointer sharing, stem-default lookup. All memory goes      */
/*  through the injected lstring370 allocator (WP-11b).               */
/*                                                                    */
/*  Design:                                                           */
/*   - The pool is a pure name -> value map. Compound-tail            */
/*     resolution (stem.i.j -> STEM.FOO.3) is the parser's job;       */
/*     the pool receives the fully derived name.                      */
/*   - Stem-default lookup (STEM. fallback) IS the pool's job.        */
/*   - PROCEDURE EXPOSE uses pointer sharing: the child entry         */
/*     carries an `exposed_ref` back into the parent's entry; all     */
/*     reads and writes route through that link.                      */
/*   - Stem-EXPOSE registers the stem name on the child pool;         */
/*     any access to that stem delegates to the parent.               */
/*   - NOVALUE handling is the interpreter's job - the pool simply    */
/*     returns VPOOL_NOT_FOUND.                                       */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 3 (Variables)                           */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXVPOOL_H
#define IRXVPOOL_H

#include <stdint.h>

#include "lstralloc.h"
#include "lstring.h"

/* ================================================================== */
/*  Return codes                                                      */
/* ================================================================== */

#define VPOOL_OK        0  /* success                              */
#define VPOOL_NOT_FOUND 1  /* variable does not exist              */
#define VPOOL_LAST      2  /* last variable returned (for NEXT)    */
#define VPOOL_NOMEM     20 /* allocator failed                     */
#define VPOOL_BADARG    21 /* invalid argument                     */

/* ================================================================== */
/*  Entry flags                                                       */
/* ================================================================== */

#define VPOOL_DROPPED     0x01 /* entry is tombstoned after DROP    */
#define VPOOL_EXPOSED_REF 0x02 /* entry is a ref into parent pool   */
#define VPOOL_UNSET       0x04 /* placeholder created by EXPOSE     */

/* ================================================================== */
/*  Entry                                                             */
/* ================================================================== */

struct vpool_entry
{
    struct vpool_entry *next;        /* chain within bucket          */
    Lstr name;                       /* variable name (bytes as-is) */
    Lstr value;                      /* variable value              */
    int flags;                       /* VPOOL_DROPPED / _EXPOSED_REF */
    struct vpool_entry *exposed_ref; /* -> parent entry if exposed  */
    int32_t type_cache;              /* 0=unknown, IRXBC_STACK_LINTEGER=1        */
    int32_t int_cache;               /* integer value when type_cache==1         */
};

/* ================================================================== */
/*  Pool                                                              */
/* ================================================================== */

#define VPOOL_ID     "VPOL"
#define VPOOL_ID_LEN 4

struct irx_vpool
{
    unsigned char vp_id[VPOOL_ID_LEN]; /* eye-catcher 'VPOL'   */
    struct vpool_entry **buckets;      /* bucket array         */
    int bucket_count;                  /* current bucket count */
    int entry_count;                   /* live entries (incl.
                                        * exposed refs)        */
    struct irx_vpool *parent;          /* -> parent scope      */
    Lstr *exposed_stems;               /* array of stem names  */
    int exposed_stem_count;
    int exposed_stem_cap;
    struct lstr_alloc *alloc; /* injected allocator   */

    /* Bumped whenever a cached entry pointer could go stale: entry
     * free (DROP / stem-drop), EXPOSE, resize. NOT bumped on the
     * assignment update path - vpool_set_buf writes in place on the
     * same entry object, so a cached pointer reads the new value
     * live. See vpool_get_cached(). Wraps at 2^32; the events are
     * rare enough (thousands per run) that this is unreachable. */
    unsigned long generation;

    /* NEXT cursor. Do not mutate the pool while iterating. */
    int next_bucket;
    struct vpool_entry *next_entry;
    int next_started;
};

/* ================================================================== */
/*  Resolution cache                                                  */
/* ================================================================== */

/* One cache slot per bytecode operand site (per symbol-table index).
 * Caches the bucket entry a simple-variable name resolved to, so a
 * repeat read skips hash + bucket index + bucket walk.
 *
 * The cached pointer is the PRE-resolve bucket entry, not the target
 * resolve_ref() lands on: its lifetime is then tied to exactly the
 * pool whose generation is checked here, which makes a hit no more
 * dangling-prone than the uncached path. resolve_ref() runs on every
 * hit - a flag test plus at most one hop.
 *
 * A slot is valid only while `pool` and `gen` both still match. The
 * owner must additionally zero its slots whenever the active pool
 * changes: vpool_destroy() + vpool_create() can return the same
 * address, and a fresh pool starts at generation 0, so the pair could
 * otherwise match a dead pool by coincidence. */
struct vpool_cache_slot
{
    struct irx_vpool *pool;    /* pool the entry was resolved in   */
    unsigned long gen;         /* pool->generation at resolve time */
    struct vpool_entry *entry; /* bucket entry, before resolve_ref */
};

/* ================================================================== */
/*  Public API                                                        */
/*                                                                    */
/*  asm() aliases are required for vpool_exists / vpool_expose_var /  */
/*  vpool_expose_stem / vpool_next / vpool_next_reset because their   */
/*  first 8 C characters collide under c2asm370's 8-char truncation.  */
/* ================================================================== */

/* Lifecycle */
struct irx_vpool *vpool_create(struct lstr_alloc *a,
                               struct irx_vpool *parent);
void vpool_destroy(struct irx_vpool *pool);

/* Core operations. `name` is used as-is (no uppercasing). The
 * parser is responsible for producing the canonical name. */
int vpool_set(struct irx_vpool *pool,
              const PLstr name, const PLstr value);
int vpool_get(struct irx_vpool *pool,
              const PLstr name, PLstr value);
int vpool_drop(struct irx_vpool *pool, const PLstr name);
int vpool_exists(struct irx_vpool *pool,
                 const PLstr name) asm("VPOOLEXI");

/* EXPOSE registration. Should be called on the child pool before
 * any set/get operations. `name` for expose_stem must include the
 * trailing dot (e.g. "STEM."). */
int vpool_expose_var(struct irx_vpool *pool,
                     const PLstr name) asm("VPOOLXPV");
int vpool_expose_stem(struct irx_vpool *pool,
                      const PLstr stem_name) asm("VPOOLXPS");

/* Iteration. Call vpool_next_reset() before the first vpool_next()
 * to rewind the cursor. vpool_next() returns VPOOL_OK with the
 * current entry's name and value copied out, or VPOOL_LAST after the
 * last entry has been returned, or VPOOL_NOT_FOUND if the pool is
 * empty. `name` and `value` are grown via the pool's allocator. */
int vpool_next(struct irx_vpool *pool,
               PLstr name, PLstr value) asm("VPOOLNXT");
void vpool_next_reset(struct irx_vpool *pool) asm("VPOOLNRS");

/* Buffer-pointer variants — bypass temporary Lstr allocation.
 * The name is passed as (data, len) directly; no irxstor call is made
 * for the lookup key.  type_cache/int_cache propagate the bytecode VM
 * integer fast-path through variable load/store round-trips. */
int vpool_get_buf(struct irx_vpool *pool,
                  const char *name_data, int name_len,
                  PLstr value,
                  int32_t *type_cache_out,
                  int32_t *int_cache_out) asm("VPOOLGTB");

/* As vpool_get_buf(), but consults `slot` first and fills it on a
 * miss. Falls straight through to the uncached path - without ever
 * touching the slot - for any name containing a dot, so compound
 * lookups, the stem-default fallback and exposed-stem delegation
 * keep today's behaviour exactly.
 *
 * That dotless restriction is what makes skipping matches_exposed_stem()
 * on a hit correct: every caller of vpool_expose_stem() admits only
 * names ending in '.', and name_matches_stem() compares the stem's
 * full length including that dot, so a dotless name can never match
 * an exposed stem. */
int vpool_get_cached(struct irx_vpool *pool,
                     const char *name_data, int name_len,
                     PLstr value,
                     int32_t *type_cache_out,
                     int32_t *int_cache_out,
                     struct vpool_cache_slot *slot) asm("VPOOLGTC");

int vpool_set_buf(struct irx_vpool *pool,
                  const char *name_data, int name_len,
                  const PLstr value,
                  int32_t type_cache,
                  int32_t int_cache) asm("VPOOLSTB");

int vpool_drop_buf(struct irx_vpool *pool,
                   const char *name_data,
                   int name_len) asm("VPOOLDRB");

/* Drop ALL pool entries whose name begins with stem_data (which must
 * include the trailing dot, e.g. "STEM.").  Used by DROP STEM.
 * Returns VPOOL_OK on success, VPOOL_BADARG on invalid arguments. */
int vpool_drop_stem_all(struct irx_vpool *pool,
                        const char *stem_data,
                        int stem_len) asm("VPOOLDSA");

#endif /* IRXVPOOL_H */
