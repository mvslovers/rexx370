/* ------------------------------------------------------------------ */
/*  irxvpol.c - REXX/370 Variable Pool implementation                 */
/*                                                                    */
/*  Chained hash table with dynamic resize to a next prime on load    */
/*  factor > VP_MAX_LOAD. PROCEDURE EXPOSE uses pointer sharing via   */
/*  vpool_entry->exposed_ref into the parent pool. Stem-EXPOSE        */
/*  registers a stem name on the child pool; any access to names     */
/*  matching that stem is delegated to the parent pool (recursively  */
/*  through the parent's own exposed stems).                          */
/*                                                                    */
/*  All memory goes through the injected lstring370 allocator. No    */
/*  statics, no globals - pools are fully self-contained.            */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stddef.h>
#include <string.h>

#include "lstring.h"
#include "lstralloc.h"
#include "irxvpool.h"

/* ------------------------------------------------------------------ */
/*  Capacity management                                               */
/* ------------------------------------------------------------------ */

static const int vp_primes[] = {
    67, 137, 277, 557, 1117, 2237, 4483, 8963
};
#define VP_PRIME_COUNT  8
#define VP_INITIAL      67
#define VP_MAX_LOAD     4    /* entries per bucket before resize */

static int next_prime_after(int current)
{
    int i;
    for (i = 0; i < VP_PRIME_COUNT; i++) {
        if (vp_primes[i] > current) return vp_primes[i];
    }
    return current;  /* already at or above the largest listed prime */
}

/* djb2 hash (Dan Bernstein) over the raw bytes of the name. */
static unsigned long hash_bytes(const unsigned char *p, size_t n)
{
    unsigned long h = 5381UL;
    size_t i;
    for (i = 0; i < n; i++) {
        h = ((h << 5) + h) + p[i];
    }
    return h;
}

/* ------------------------------------------------------------------ */
/*  Allocation helpers                                                */
/* ------------------------------------------------------------------ */

static void *vp_alloc_raw(struct lstr_alloc *a, size_t size)
{
    if (a == NULL) return NULL;
    return (*a->alloc)(size, a->ctx);
}

static void vp_free_raw(struct lstr_alloc *a, void *ptr, size_t size)
{
    if (a == NULL || ptr == NULL) return;
    (*a->dealloc)(ptr, size, a->ctx);
}

static void vp_entry_init(struct vpool_entry *e)
{
    e->next         = NULL;
    Lzeroinit(&e->name);
    Lzeroinit(&e->value);
    e->flags        = 0;
    e->exposed_ref  = NULL;
}

static struct vpool_entry *vp_entry_new(struct lstr_alloc *a)
{
    struct vpool_entry *e;
    e = (struct vpool_entry *)vp_alloc_raw(a, sizeof(struct vpool_entry));
    if (e != NULL) vp_entry_init(e);
    return e;
}

static void vp_entry_free(struct lstr_alloc *a, struct vpool_entry *e)
{
    if (e == NULL) return;
    Lfree(a, &e->name);
    Lfree(a, &e->value);
    vp_free_raw(a, e, sizeof(struct vpool_entry));
}

/* Allocate a fresh bucket array cleared to zero. */
static struct vpool_entry **vp_alloc_buckets(struct lstr_alloc *a,
                                             int count)
{
    struct vpool_entry **buckets;
    size_t bytes = (size_t)count * sizeof(struct vpool_entry *);
    buckets = (struct vpool_entry **)vp_alloc_raw(a, bytes);
    if (buckets != NULL) memset(buckets, 0, bytes);
    return buckets;
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                         */
/* ------------------------------------------------------------------ */

struct irx_vpool *vpool_create(struct lstr_alloc *a,
                               struct irx_vpool *parent)
{
    struct irx_vpool *pool;

    if (a == NULL) return NULL;

    pool = (struct irx_vpool *)vp_alloc_raw(a, sizeof(struct irx_vpool));
    if (pool == NULL) return NULL;

    memset(pool, 0, sizeof(*pool));
    memcpy(pool->vp_id, VPOOL_ID, VPOOL_ID_LEN);
    pool->alloc        = a;
    pool->parent       = parent;
    pool->bucket_count = VP_INITIAL;
    pool->buckets      = vp_alloc_buckets(a, VP_INITIAL);
    if (pool->buckets == NULL) {
        vp_free_raw(a, pool, sizeof(*pool));
        return NULL;
    }
    return pool;
}

static void vp_free_exposed_stems(struct irx_vpool *pool)
{
    int i;
    if (pool->exposed_stems == NULL) return;
    for (i = 0; i < pool->exposed_stem_count; i++) {
        Lfree(pool->alloc, &pool->exposed_stems[i]);
    }
    vp_free_raw(pool->alloc, pool->exposed_stems,
                (size_t)pool->exposed_stem_cap * sizeof(Lstr));
    pool->exposed_stems      = NULL;
    pool->exposed_stem_count = 0;
    pool->exposed_stem_cap   = 0;
}

void vpool_destroy(struct irx_vpool *pool)
{
    int i;
    struct lstr_alloc *a;

    if (pool == NULL) return;
    if (memcmp(pool->vp_id, VPOOL_ID, VPOOL_ID_LEN) != 0) return;

    a = pool->alloc;

    for (i = 0; i < pool->bucket_count; i++) {
        struct vpool_entry *e = pool->buckets[i];
        while (e != NULL) {
            struct vpool_entry *next = e->next;
            vp_entry_free(a, e);
            e = next;
        }
    }
    vp_free_raw(a, pool->buckets,
                (size_t)pool->bucket_count *
                sizeof(struct vpool_entry *));

    vp_free_exposed_stems(pool);

    memset(pool->vp_id, 0, VPOOL_ID_LEN);
    vp_free_raw(a, pool, sizeof(*pool));
}

/* ------------------------------------------------------------------ */
/*  Bucket helpers                                                    */
/* ------------------------------------------------------------------ */

static int bucket_index(const struct irx_vpool *pool, const PLstr name)
{
    unsigned long h = hash_bytes(name->pstr, name->len);
    return (int)(h % (unsigned long)pool->bucket_count);
}

static struct vpool_entry *find_in_bucket(struct vpool_entry *head,
                                          const PLstr name)
{
    struct vpool_entry *e;
    for (e = head; e != NULL; e = e->next) {
        if (e->name.len == name->len &&
            (name->len == 0 ||
             memcmp(e->name.pstr, name->pstr, name->len) == 0)) {
            return e;
        }
    }
    return NULL;
}

/* Resize the bucket array to the next prime if the load factor
 * exceeds VP_MAX_LOAD and we are not already at the largest prime. */
static int maybe_resize(struct irx_vpool *pool)
{
    int new_count;
    struct vpool_entry **new_buckets;
    int i;

    if (pool->entry_count <= pool->bucket_count * VP_MAX_LOAD) {
        return VPOOL_OK;
    }
    new_count = next_prime_after(pool->bucket_count);
    if (new_count == pool->bucket_count) return VPOOL_OK;  /* at max */

    new_buckets = vp_alloc_buckets(pool->alloc, new_count);
    if (new_buckets == NULL) return VPOOL_NOMEM;

    for (i = 0; i < pool->bucket_count; i++) {
        struct vpool_entry *e = pool->buckets[i];
        while (e != NULL) {
            struct vpool_entry *next = e->next;
            unsigned long h = hash_bytes(e->name.pstr, e->name.len);
            int idx = (int)(h % (unsigned long)new_count);
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }

    vp_free_raw(pool->alloc, pool->buckets,
                (size_t)pool->bucket_count *
                sizeof(struct vpool_entry *));
    pool->buckets      = new_buckets;
    pool->bucket_count = new_count;

    /* Iteration cursor is invalidated by resize. */
    pool->next_started = 0;
    pool->next_bucket  = 0;
    pool->next_entry   = NULL;

    return VPOOL_OK;
}

/* ------------------------------------------------------------------ */
/*  Stem helpers                                                      */
/* ------------------------------------------------------------------ */

static int name_matches_stem(const PLstr name, const PLstr stem)
{
    /* The stem name includes its trailing dot. A compound name
     * "STEM.X" matches "STEM.", and so does the bare default
     * "STEM." itself. */
    if (stem->len == 0 || name->len < stem->len) return 0;
    return memcmp(name->pstr, stem->pstr, stem->len) == 0;
}

static int matches_exposed_stem(const struct irx_vpool *pool,
                                const PLstr name)
{
    int i;
    if (pool->exposed_stem_count == 0) return 0;
    for (i = 0; i < pool->exposed_stem_count; i++) {
        if (name_matches_stem(name, &pool->exposed_stems[i])) return 1;
    }
    return 0;
}

/* Locate the first dot in `name`. Returns the 0-based offset of the
 * dot, or -1 if there is no dot. */
static int first_dot(const PLstr name)
{
    size_t i;
    for (i = 0; i < name->len; i++) {
        if (name->pstr[i] == '.') return (int)i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Entry insertion                                                   */
/* ------------------------------------------------------------------ */

/* Insert a freshly created entry at the head of its bucket. Also
 * bumps entry_count and triggers resize if needed. Caller is
 * responsible for populating the entry's name and value buffers. */
static int link_entry(struct irx_vpool *pool, struct vpool_entry *e)
{
    int idx = bucket_index(pool, &e->name);
    e->next = pool->buckets[idx];
    pool->buckets[idx] = e;
    pool->entry_count++;
    return maybe_resize(pool);
}

/* Unlink an entry from its bucket chain. Caller provides the
 * already-computed bucket index. */
static void unlink_entry(struct irx_vpool *pool, int idx,
                         struct vpool_entry *target)
{
    struct vpool_entry *prev = NULL;
    struct vpool_entry *cur  = pool->buckets[idx];
    while (cur != NULL) {
        if (cur == target) {
            if (prev == NULL) pool->buckets[idx] = cur->next;
            else prev->next = cur->next;
            pool->entry_count--;
            return;
        }
        prev = cur;
        cur  = cur->next;
    }
}

/* ------------------------------------------------------------------ */
/*  Core operations                                                   */
/* ------------------------------------------------------------------ */

/* Follow an exposed_ref to the real storage entry. */
static struct vpool_entry *resolve_ref(struct vpool_entry *e)
{
    while (e != NULL && (e->flags & VPOOL_EXPOSED_REF) &&
           e->exposed_ref != NULL) {
        e = e->exposed_ref;
    }
    return e;
}

int vpool_set(struct irx_vpool *pool, const PLstr name, const PLstr value)
{
    int idx;
    struct vpool_entry *e;
    int rc;

    if (pool == NULL || name == NULL || value == NULL) return VPOOL_BADARG;
    if (memcmp(pool->vp_id, VPOOL_ID, VPOOL_ID_LEN) != 0) return VPOOL_BADARG;

    /* Stem-EXPOSE delegation. */
    if (matches_exposed_stem(pool, name) && pool->parent != NULL) {
        return vpool_set(pool->parent, name, value);
    }

    idx = bucket_index(pool, name);
    e = find_in_bucket(pool->buckets[idx], name);

    if (e != NULL) {
        struct vpool_entry *tgt = resolve_ref(e);
        if (tgt == NULL) return VPOOL_BADARG;
        rc = Lstrcpy(pool->alloc, &tgt->value, value);
        if (rc != LSTR_OK) return VPOOL_NOMEM;
        tgt->flags &= ~VPOOL_UNSET;
        return VPOOL_OK;
    }

    /* Create a new local entry. */
    e = vp_entry_new(pool->alloc);
    if (e == NULL) return VPOOL_NOMEM;

    rc = Lstrcpy(pool->alloc, &e->name, name);
    if (rc != LSTR_OK) { vp_entry_free(pool->alloc, e); return VPOOL_NOMEM; }
    rc = Lstrcpy(pool->alloc, &e->value, value);
    if (rc != LSTR_OK) { vp_entry_free(pool->alloc, e); return VPOOL_NOMEM; }

    return link_entry(pool, e);
}

int vpool_get(struct irx_vpool *pool, const PLstr name, PLstr value)
{
    int idx;
    struct vpool_entry *e;
    int rc;

    if (pool == NULL || name == NULL || value == NULL) return VPOOL_BADARG;

    /* Stem-EXPOSE delegation. */
    if (matches_exposed_stem(pool, name) && pool->parent != NULL) {
        return vpool_get(pool->parent, name, value);
    }

    idx = bucket_index(pool, name);
    e = find_in_bucket(pool->buckets[idx], name);

    if (e != NULL) {
        struct vpool_entry *tgt = resolve_ref(e);
        if (tgt != NULL && !(tgt->flags & VPOOL_UNSET)) {
            rc = Lstrcpy(pool->alloc, value, &tgt->value);
            return (rc == LSTR_OK) ? VPOOL_OK : VPOOL_NOMEM;
        }
    }

    /* Stem default: if `name` is compound (contains a dot before the
     * last character), look up "STEM." as the fallback. The stem key
     * includes the first dot. */
    {
        int dot = first_dot(name);
        if (dot >= 0) {
            Lstr stem_key;
            int  stem_idx;
            struct vpool_entry *stem_e;

            stem_key.pstr   = name->pstr;
            stem_key.len    = (size_t)(dot + 1);
            stem_key.maxlen = stem_key.len;
            stem_key.type   = LSTRING_TY;

            /* Don't fall into infinite recursion: only fall back on a
             * true compound, i.e. there is at least one character
             * after the dot (otherwise the caller already asked for
             * the stem default). */
            if (name->len > stem_key.len) {
                stem_idx = bucket_index(pool, &stem_key);
                stem_e   = find_in_bucket(pool->buckets[stem_idx],
                                          &stem_key);
                if (stem_e != NULL) {
                    struct vpool_entry *tgt = resolve_ref(stem_e);
                    if (tgt != NULL && !(tgt->flags & VPOOL_UNSET)) {
                        rc = Lstrcpy(pool->alloc, value, &tgt->value);
                        return (rc == LSTR_OK) ? VPOOL_OK : VPOOL_NOMEM;
                    }
                }
            }
        }
    }

    return VPOOL_NOT_FOUND;
}

int vpool_drop(struct irx_vpool *pool, const PLstr name)
{
    int idx;
    struct vpool_entry *e;

    if (pool == NULL || name == NULL) return VPOOL_BADARG;

    if (matches_exposed_stem(pool, name) && pool->parent != NULL) {
        return vpool_drop(pool->parent, name);
    }

    idx = bucket_index(pool, name);
    e = find_in_bucket(pool->buckets[idx], name);
    if (e == NULL) return VPOOL_NOT_FOUND;

    if ((e->flags & VPOOL_EXPOSED_REF) && pool->parent != NULL) {
        /* Drop the backing entry in the parent, then remove our ref. */
        vpool_drop(pool->parent, name);
    }

    unlink_entry(pool, idx, e);
    vp_entry_free(pool->alloc, e);
    return VPOOL_OK;
}

int vpool_exists(struct irx_vpool *pool, const PLstr name)
{
    int idx;
    struct vpool_entry *e;

    if (pool == NULL || name == NULL) return 0;

    if (matches_exposed_stem(pool, name) && pool->parent != NULL) {
        return vpool_exists(pool->parent, name);
    }

    idx = bucket_index(pool, name);
    e = find_in_bucket(pool->buckets[idx], name);
    if (e == NULL) return 0;

    {
        struct vpool_entry *tgt = resolve_ref(e);
        if (tgt == NULL) return 0;
        return (tgt->flags & VPOOL_UNSET) ? 0 : 1;
    }
}

/* ------------------------------------------------------------------ */
/*  EXPOSE                                                            */
/* ------------------------------------------------------------------ */

int vpool_expose_var(struct irx_vpool *pool, const PLstr name)
{
    struct vpool_entry *parent_e;
    struct vpool_entry *child_e;
    int rc;
    int pidx;

    if (pool == NULL || name == NULL) return VPOOL_BADARG;
    if (pool->parent == NULL) return VPOOL_OK;   /* top-level scope */

    /* If an entry with this name already exists locally (e.g. the
     * caller called EXPOSE twice), drop it and re-create as a ref. */
    {
        int idx = bucket_index(pool, name);
        struct vpool_entry *existing =
            find_in_bucket(pool->buckets[idx], name);
        if (existing != NULL) {
            unlink_entry(pool, idx, existing);
            vp_entry_free(pool->alloc, existing);
        }
    }

    /* Look up or create a placeholder entry in the parent. */
    pidx = bucket_index(pool->parent, name);
    parent_e = find_in_bucket(pool->parent->buckets[pidx], name);
    if (parent_e == NULL) {
        parent_e = vp_entry_new(pool->alloc);
        if (parent_e == NULL) return VPOOL_NOMEM;
        rc = Lstrcpy(pool->alloc, &parent_e->name, name);
        if (rc != LSTR_OK) {
            vp_entry_free(pool->alloc, parent_e);
            return VPOOL_NOMEM;
        }
        parent_e->flags |= VPOOL_UNSET;
        rc = link_entry(pool->parent, parent_e);
        if (rc != VPOOL_OK) {
            /* link_entry already did entry_count--; undo and free. */
            unlink_entry(pool->parent,
                         bucket_index(pool->parent, name), parent_e);
            vp_entry_free(pool->alloc, parent_e);
            return rc;
        }
    }

    /* Install a ref entry in the child. */
    child_e = vp_entry_new(pool->alloc);
    if (child_e == NULL) return VPOOL_NOMEM;
    rc = Lstrcpy(pool->alloc, &child_e->name, name);
    if (rc != LSTR_OK) {
        vp_entry_free(pool->alloc, child_e);
        return VPOOL_NOMEM;
    }
    child_e->flags      |= VPOOL_EXPOSED_REF;
    child_e->exposed_ref = parent_e;

    return link_entry(pool, child_e);
}

int vpool_expose_stem(struct irx_vpool *pool, const PLstr stem_name)
{
    int rc;

    if (pool == NULL || stem_name == NULL) return VPOOL_BADARG;
    if (pool->parent == NULL) return VPOOL_OK;

    /* Grow the exposed_stems array as needed. */
    if (pool->exposed_stem_count == pool->exposed_stem_cap) {
        int new_cap = pool->exposed_stem_cap == 0
                      ? 4 : pool->exposed_stem_cap * 2;
        Lstr *new_arr = (Lstr *)vp_alloc_raw(pool->alloc,
                        (size_t)new_cap * sizeof(Lstr));
        if (new_arr == NULL) return VPOOL_NOMEM;
        memset(new_arr, 0, (size_t)new_cap * sizeof(Lstr));
        if (pool->exposed_stems != NULL) {
            memcpy(new_arr, pool->exposed_stems,
                   (size_t)pool->exposed_stem_count * sizeof(Lstr));
            vp_free_raw(pool->alloc, pool->exposed_stems,
                        (size_t)pool->exposed_stem_cap * sizeof(Lstr));
        }
        pool->exposed_stems    = new_arr;
        pool->exposed_stem_cap = new_cap;
    }

    {
        Lstr *slot = &pool->exposed_stems[pool->exposed_stem_count];
        Lzeroinit(slot);
        rc = Lstrcpy(pool->alloc, slot, stem_name);
        if (rc != LSTR_OK) return VPOOL_NOMEM;
        pool->exposed_stem_count++;
    }
    return VPOOL_OK;
}

/* ------------------------------------------------------------------ */
/*  Iteration                                                         */
/* ------------------------------------------------------------------ */

void vpool_next_reset(struct irx_vpool *pool)
{
    if (pool == NULL) return;
    pool->next_started = 0;
    pool->next_bucket  = 0;
    pool->next_entry   = NULL;
}

int vpool_next(struct irx_vpool *pool, PLstr name, PLstr value)
{
    struct vpool_entry *e;
    struct vpool_entry *tgt;
    int rc;

    if (pool == NULL || name == NULL || value == NULL) return VPOOL_BADARG;

    if (!pool->next_started) {
        pool->next_bucket  = 0;
        pool->next_entry   = NULL;
        pool->next_started = 1;
    } else if (pool->next_entry != NULL) {
        pool->next_entry = pool->next_entry->next;
        if (pool->next_entry == NULL) {
            /* Finished this bucket chain; scan forward. */
            pool->next_bucket++;
        }
    }

    /* Find the next live entry, skipping empty buckets. */
    while (pool->next_entry == NULL &&
           pool->next_bucket < pool->bucket_count) {
        pool->next_entry = pool->buckets[pool->next_bucket];
        if (pool->next_entry == NULL) pool->next_bucket++;
    }

    if (pool->next_entry == NULL) {
        if (pool->entry_count == 0) return VPOOL_NOT_FOUND;
        return VPOOL_LAST;
    }

    e   = pool->next_entry;
    tgt = resolve_ref(e);
    if (tgt == NULL) return VPOOL_BADARG;

    rc = Lstrcpy(pool->alloc, name, &e->name);
    if (rc != LSTR_OK) return VPOOL_NOMEM;
    rc = Lstrcpy(pool->alloc, value, &tgt->value);
    if (rc != LSTR_OK) return VPOOL_NOMEM;

    /* Pre-advance so the caller can detect the final entry: if the
     * advance lands us at the end, next call returns VPOOL_LAST. */
    return VPOOL_OK;
}
