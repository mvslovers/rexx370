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

#ifndef __IRXVPOOL_H__
#define __IRXVPOOL_H__

#include "lstring.h"
#include "lstralloc.h"

/* ================================================================== */
/*  Return codes                                                      */
/* ================================================================== */

#define VPOOL_OK          0   /* success                              */
#define VPOOL_NOT_FOUND   1   /* variable does not exist              */
#define VPOOL_LAST        2   /* last variable returned (for NEXT)    */
#define VPOOL_NOMEM      20   /* allocator failed                     */
#define VPOOL_BADARG     21   /* invalid argument                     */

/* ================================================================== */
/*  Entry flags                                                       */
/* ================================================================== */

#define VPOOL_DROPPED     0x01  /* entry is tombstoned after DROP    */
#define VPOOL_EXPOSED_REF 0x02  /* entry is a ref into parent pool   */
#define VPOOL_UNSET       0x04  /* placeholder created by EXPOSE     */

/* ================================================================== */
/*  Entry                                                             */
/* ================================================================== */

struct vpool_entry {
    struct vpool_entry *next;          /* chain within bucket          */
    Lstr                name;          /* variable name (bytes as-is) */
    Lstr                value;         /* variable value              */
    int                 flags;         /* VPOOL_DROPPED / _EXPOSED_REF */
    struct vpool_entry *exposed_ref;   /* -> parent entry if exposed  */
};

/* ================================================================== */
/*  Pool                                                              */
/* ================================================================== */

#define VPOOL_ID      "VPOL"
#define VPOOL_ID_LEN  4

struct irx_vpool {
    unsigned char        vp_id[VPOOL_ID_LEN];  /* eye-catcher 'VPOL'   */
    struct vpool_entry **buckets;              /* bucket array         */
    int                  bucket_count;         /* current bucket count */
    int                  entry_count;          /* live entries (incl.
                                                * exposed refs)        */
    struct irx_vpool    *parent;               /* -> parent scope      */
    Lstr                *exposed_stems;        /* array of stem names  */
    int                  exposed_stem_count;
    int                  exposed_stem_cap;
    struct lstr_alloc   *alloc;                /* injected allocator   */

    /* NEXT cursor. Do not mutate the pool while iterating. */
    int                  next_bucket;
    struct vpool_entry  *next_entry;
    int                  next_started;
};

/* ================================================================== */
/*  Public API                                                        */
/*                                                                    */
/*  asm() aliases are required for vpool_exists / vpool_expose_var /  */
/*  vpool_expose_stem / vpool_next / vpool_next_reset because their   */
/*  first 8 C characters collide under c2asm370's 8-char truncation.  */
/* ================================================================== */

/* Lifecycle */
struct irx_vpool *vpool_create (struct lstr_alloc *a,
                                struct irx_vpool *parent);
void              vpool_destroy(struct irx_vpool *pool);

/* Core operations. `name` is used as-is (no uppercasing). The
 * parser is responsible for producing the canonical name. */
int vpool_set   (struct irx_vpool *pool,
                 const PLstr name, const PLstr value);
int vpool_get   (struct irx_vpool *pool,
                 const PLstr name, PLstr value);
int vpool_drop  (struct irx_vpool *pool, const PLstr name);
int vpool_exists(struct irx_vpool *pool,
                 const PLstr name)                      asm("VPOOLEXI");

/* EXPOSE registration. Should be called on the child pool before
 * any set/get operations. `name` for expose_stem must include the
 * trailing dot (e.g. "STEM."). */
int vpool_expose_var (struct irx_vpool *pool,
                      const PLstr name)                 asm("VPOOLXPV");
int vpool_expose_stem(struct irx_vpool *pool,
                      const PLstr stem_name)            asm("VPOOLXPS");

/* Iteration. Call vpool_next_reset() before the first vpool_next()
 * to rewind the cursor. vpool_next() returns VPOOL_OK with the
 * current entry's name and value copied out, or VPOOL_LAST after the
 * last entry has been returned, or VPOOL_NOT_FOUND if the pool is
 * empty. `name` and `value` are grown via the pool's allocator. */
int  vpool_next      (struct irx_vpool *pool,
                      PLstr name, PLstr value)          asm("VPOOLNXT");
void vpool_next_reset(struct irx_vpool *pool)           asm("VPOOLNRS");

#endif /* __IRXVPOOL_H__ */
