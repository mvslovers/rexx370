/* ------------------------------------------------------------------ */
/*  test_vpool.c - WP-12 variable pool unit tests                     */
/*                                                                    */
/*  Cross-compile build (Linux/gcc):                                  */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -o test/test_vpool \                */
/*        test/test_vpool.c 'src/irx#vpol.c' \                         */
/*        ../lstring370/src/'lstr#cor.c'                              */
/*                                                                    */
/*  The tests use the default lstring370 allocator directly (no       */
/*  irxstor bridge), so they run without needing the full rexx370    */
/*  environment setup. Allocator injection is verified end-to-end    */
/*  via a tracking wrapper that counts alloc/dealloc calls.          */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "lstring.h"
#include "lstralloc.h"
#include "irxvpool.h"

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) \
    do { \
        tests_run++; \
        if (cond) { \
            tests_passed++; \
            printf("  PASS: %s\n", msg); \
        } else { \
            tests_failed++; \
            printf("  FAIL: %s\n", msg); \
        } \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Helpers: manipulate Lstr via the library                          */
/* ------------------------------------------------------------------ */

static void set_lstr(struct lstr_alloc *a, Lstr *s, const char *cstr)
{
    Lscpy(a, s, cstr);
}

static int lstr_eq_cstr(const Lstr *s, const char *cstr)
{
    size_t n = strlen(cstr);
    if (s->len != n) return 0;
    return memcmp(s->pstr, cstr, n) == 0;
}

/* ------------------------------------------------------------------ */
/*  Tracking allocator (wraps the default malloc/free allocator so    */
/*  we can verify that nothing leaks and nothing slips past the      */
/*  injected callbacks).                                              */
/* ------------------------------------------------------------------ */

struct track {
    long alloc_calls;
    long dealloc_calls;
    long bytes_live;
};

static void *track_alloc(size_t size, void *ctx)
{
    struct track *t = (struct track *)ctx;
    void *p = malloc(size);
    if (p != NULL) {
        t->alloc_calls++;
        t->bytes_live += (long)size;
    }
    return p;
}

static void track_dealloc(void *ptr, size_t size, void *ctx)
{
    struct track *t = (struct track *)ctx;
    if (ptr != NULL) {
        t->dealloc_calls++;
        t->bytes_live -= (long)size;
        free(ptr);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: simple set/get/drop                                         */
/* ------------------------------------------------------------------ */

static void test_basic_set_get_drop(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool  *pool;
    Lstr name, value, out;

    printf("\n--- Test: set/get/drop for simple variables ---\n");

    pool = vpool_create(a, NULL);
    CHECK(pool != NULL, "vpool_create returns non-NULL");

    Lzeroinit(&name); Lzeroinit(&value); Lzeroinit(&out);
    set_lstr(a, &name, "X");
    set_lstr(a, &value, "42");

    CHECK(vpool_set(pool, &name, &value) == VPOOL_OK,
          "vpool_set X = '42'");
    CHECK(vpool_exists(pool, &name) == 1, "vpool_exists(X) == 1");
    CHECK(vpool_get(pool, &name, &out) == VPOOL_OK,
          "vpool_get(X) returns OK");
    CHECK(lstr_eq_cstr(&out, "42"), "retrieved value == '42'");

    /* Overwrite */
    set_lstr(a, &value, "hello");
    vpool_set(pool, &name, &value);
    vpool_get(pool, &name, &out);
    CHECK(lstr_eq_cstr(&out, "hello"), "overwrite works");

    /* Drop */
    CHECK(vpool_drop(pool, &name) == VPOOL_OK, "vpool_drop(X)");
    CHECK(vpool_get(pool, &name, &out) == VPOOL_NOT_FOUND,
          "vpool_get(X) after drop -> NOT_FOUND");
    CHECK(vpool_exists(pool, &name) == 0,
          "vpool_exists(X) after drop == 0");

    Lfree(a, &name); Lfree(a, &value); Lfree(a, &out);
    vpool_destroy(pool);
}

/* ------------------------------------------------------------------ */
/*  Test: stem-default lookup                                         */
/* ------------------------------------------------------------------ */

static void test_stem_default(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool  *pool;
    Lstr name, value, out;

    printf("\n--- Test: stem-default lookup ---\n");

    pool = vpool_create(a, NULL);
    Lzeroinit(&name); Lzeroinit(&value); Lzeroinit(&out);

    /* Set STEM. = 'default' */
    set_lstr(a, &name, "STEM.");
    set_lstr(a, &value, "default");
    vpool_set(pool, &name, &value);

    /* STEM.X not set -> returns 'default' */
    set_lstr(a, &name, "STEM.X");
    CHECK(vpool_get(pool, &name, &out) == VPOOL_OK,
          "vpool_get(STEM.X) falls back to stem");
    CHECK(lstr_eq_cstr(&out, "default"),
          "stem default value retrieved");

    /* Set STEM.Y = 'explicit' -> direct hit, no fallback */
    set_lstr(a, &name, "STEM.Y");
    set_lstr(a, &value, "explicit");
    vpool_set(pool, &name, &value);
    vpool_get(pool, &name, &out);
    CHECK(lstr_eq_cstr(&out, "explicit"),
          "explicit value beats stem default");

    /* Fetching STEM.X still returns default */
    set_lstr(a, &name, "STEM.X");
    vpool_get(pool, &name, &out);
    CHECK(lstr_eq_cstr(&out, "default"),
          "other tail still uses stem default");

    /* Non-compound variable does not fall back to anything */
    set_lstr(a, &name, "OTHER");
    CHECK(vpool_get(pool, &name, &out) == VPOOL_NOT_FOUND,
          "non-compound missing var -> NOT_FOUND");

    Lfree(a, &name); Lfree(a, &value); Lfree(a, &out);
    vpool_destroy(pool);
}

/* ------------------------------------------------------------------ */
/*  Test: dynamic resize with 5000 entries                            */
/* ------------------------------------------------------------------ */

static void test_resize(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool  *pool;
    Lstr name, value, out;
    int i;
    int all_ok;

    printf("\n--- Test: dynamic resize with 5000 entries ---\n");

    pool = vpool_create(a, NULL);
    Lzeroinit(&name); Lzeroinit(&value); Lzeroinit(&out);

    CHECK(pool->bucket_count == 67, "initial bucket count is 67");

    all_ok = 1;
    for (i = 0; i < 5000; i++) {
        char buf[16];
        sprintf(buf, "V%d", i);
        set_lstr(a, &name, buf);
        sprintf(buf, "val%d", i);
        set_lstr(a, &value, buf);
        if (vpool_set(pool, &name, &value) != VPOOL_OK) {
            all_ok = 0; break;
        }
    }
    CHECK(all_ok, "5000 inserts succeeded");
    CHECK(pool->entry_count == 5000, "entry_count == 5000");
    CHECK(pool->bucket_count > 67,
          "table resized past initial 67 buckets");

    /* Spot-check a few random keys survived the resizes. */
    set_lstr(a, &name, "V0");
    vpool_get(pool, &name, &out);
    CHECK(lstr_eq_cstr(&out, "val0"), "V0 survives resizes");
    set_lstr(a, &name, "V2499");
    vpool_get(pool, &name, &out);
    CHECK(lstr_eq_cstr(&out, "val2499"), "V2499 survives resizes");
    set_lstr(a, &name, "V4999");
    vpool_get(pool, &name, &out);
    CHECK(lstr_eq_cstr(&out, "val4999"), "V4999 survives resizes");

    Lfree(a, &name); Lfree(a, &value); Lfree(a, &out);
    vpool_destroy(pool);
}

/* ------------------------------------------------------------------ */
/*  Test: 10000 variables, load factor below 4                        */
/* ------------------------------------------------------------------ */

static void test_large_load(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool  *pool;
    Lstr name, value;
    int i;

    printf("\n--- Test: 10000 variables, load factor below 4 ---\n");

    pool = vpool_create(a, NULL);
    Lzeroinit(&name); Lzeroinit(&value);

    for (i = 0; i < 10000; i++) {
        char buf[16];
        sprintf(buf, "VAR%d", i);
        set_lstr(a, &name, buf);
        sprintf(buf, "%d", i);
        set_lstr(a, &value, buf);
        vpool_set(pool, &name, &value);
    }
    CHECK(pool->entry_count == 10000, "all 10000 entries present");
    {
        double load = (double)pool->entry_count / (double)pool->bucket_count;
        printf("         load factor = %.2f (buckets=%d)\n",
               load, pool->bucket_count);
        CHECK(load < 4.0, "load factor stays below 4");
    }

    Lfree(a, &name); Lfree(a, &value);
    vpool_destroy(pool);
}

/* ------------------------------------------------------------------ */
/*  Test: scope isolation                                             */
/* ------------------------------------------------------------------ */

static void test_scope_isolation(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool  *parent;
    struct irx_vpool  *child;
    Lstr name, value, out;

    printf("\n--- Test: PROCEDURE scope isolation ---\n");

    parent = vpool_create(a, NULL);
    child  = vpool_create(a, parent);

    Lzeroinit(&name); Lzeroinit(&value); Lzeroinit(&out);

    set_lstr(a, &name, "PARENT_VAR");
    set_lstr(a, &value, "parent_val");
    vpool_set(parent, &name, &value);

    /* Child does not see parent's locals. */
    CHECK(vpool_get(child, &name, &out) == VPOOL_NOT_FOUND,
          "child does not see parent's PARENT_VAR");
    CHECK(vpool_exists(child, &name) == 0,
          "vpool_exists(child, PARENT_VAR) == 0");

    /* Parent still sees its own. */
    CHECK(vpool_get(parent, &name, &out) == VPOOL_OK,
          "parent still sees PARENT_VAR");

    Lfree(a, &name); Lfree(a, &value); Lfree(a, &out);
    vpool_destroy(child);
    vpool_destroy(parent);
}

/* ------------------------------------------------------------------ */
/*  Test: EXPOSE single variable                                      */
/* ------------------------------------------------------------------ */

static void test_expose_var(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool  *parent;
    struct irx_vpool  *child;
    Lstr name, value, out;

    printf("\n--- Test: EXPOSE single variable ---\n");

    parent = vpool_create(a, NULL);
    child  = vpool_create(a, parent);

    Lzeroinit(&name); Lzeroinit(&value); Lzeroinit(&out);

    /* Parent already has Y = 'parent_y' */
    set_lstr(a, &name, "Y");
    set_lstr(a, &value, "parent_y");
    vpool_set(parent, &name, &value);

    /* Child exposes Y */
    CHECK(vpool_expose_var(child, &name) == VPOOL_OK,
          "vpool_expose_var(child, Y)");

    /* Child sees the parent's value. */
    CHECK(vpool_get(child, &name, &out) == VPOOL_OK,
          "child can read exposed Y");
    CHECK(lstr_eq_cstr(&out, "parent_y"),
          "child reads parent's value");

    /* Child writes; parent sees the new value. */
    set_lstr(a, &value, "child_wrote");
    vpool_set(child, &name, &value);
    vpool_get(parent, &name, &out);
    CHECK(lstr_eq_cstr(&out, "child_wrote"),
          "parent sees child's write through EXPOSE");

    /* Drop in child drops in parent too. */
    CHECK(vpool_drop(child, &name) == VPOOL_OK,
          "vpool_drop(child, Y)");
    CHECK(vpool_exists(parent, &name) == 0,
          "parent's Y is gone after child's drop");

    /* EXPOSE a variable the parent does not have yet:
     * creates an unset placeholder in the parent. */
    set_lstr(a, &name, "Z");
    vpool_expose_var(child, &name);
    CHECK(vpool_exists(child, &name) == 0,
          "Z still unset via placeholder");

    /* Setting Z in child creates it in parent. */
    set_lstr(a, &value, "via_child");
    vpool_set(child, &name, &value);
    CHECK(vpool_exists(parent, &name) == 1,
          "parent now has Z via EXPOSE");
    vpool_get(parent, &name, &out);
    CHECK(lstr_eq_cstr(&out, "via_child"),
          "parent's Z has the child-written value");

    Lfree(a, &name); Lfree(a, &value); Lfree(a, &out);
    vpool_destroy(child);
    vpool_destroy(parent);
}

/* ------------------------------------------------------------------ */
/*  Test: EXPOSE stem                                                 */
/* ------------------------------------------------------------------ */

static void test_expose_stem(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool  *parent;
    struct irx_vpool  *child;
    Lstr name, value, out;

    printf("\n--- Test: EXPOSE stem ---\n");

    parent = vpool_create(a, NULL);
    child  = vpool_create(a, parent);

    Lzeroinit(&name); Lzeroinit(&value); Lzeroinit(&out);

    /* Expose STEM. on child */
    set_lstr(a, &name, "STEM.");
    vpool_expose_stem(child, &name);

    /* Set STEM.FOO in child -> ends up in parent */
    set_lstr(a, &name, "STEM.FOO");
    set_lstr(a, &value, "bar");
    vpool_set(child, &name, &value);

    CHECK(vpool_exists(parent, &name) == 1,
          "STEM.FOO created in parent via stem EXPOSE");
    vpool_get(parent, &name, &out);
    CHECK(lstr_eq_cstr(&out, "bar"), "parent STEM.FOO == 'bar'");

    /* Child reads through the same path. */
    vpool_get(child, &name, &out);
    CHECK(lstr_eq_cstr(&out, "bar"),
          "child reads STEM.FOO from parent");

    /* Drop via child drops in parent. */
    vpool_drop(child, &name);
    CHECK(vpool_exists(parent, &name) == 0,
          "drop propagates to parent through stem EXPOSE");

    Lfree(a, &name); Lfree(a, &value); Lfree(a, &out);
    vpool_destroy(child);
    vpool_destroy(parent);
}

/* ------------------------------------------------------------------ */
/*  Test: iteration without duplicates                                */
/* ------------------------------------------------------------------ */

static void test_iteration(void)
{
    struct lstr_alloc *a = lstr_default_alloc();
    struct irx_vpool  *pool;
    Lstr name, value;
    Lstr out_name, out_value;
    int  seen[10];
    int  rc;
    int  count;
    int  i;

    printf("\n--- Test: vpool_next iteration ---\n");

    pool = vpool_create(a, NULL);
    Lzeroinit(&name); Lzeroinit(&value);
    Lzeroinit(&out_name); Lzeroinit(&out_value);

    for (i = 0; i < 10; i++) {
        char buf[8];
        sprintf(buf, "V%d", i);
        set_lstr(a, &name, buf);
        sprintf(buf, "%d", i);
        set_lstr(a, &value, buf);
        vpool_set(pool, &name, &value);
        seen[i] = 0;
    }

    vpool_next_reset(pool);
    count = 0;
    while ((rc = vpool_next(pool, &out_name, &out_value)) == VPOOL_OK) {
        /* out_name is "V<i>" -> index from "<i>" suffix */
        int idx = atoi((const char *)out_name.pstr + 1);
        if (idx >= 0 && idx < 10 && !seen[idx]) seen[idx] = 1;
        count++;
        if (count > 20) break;   /* safety cap */
    }
    CHECK(rc == VPOOL_LAST, "iteration ends with VPOOL_LAST");
    CHECK(count == 10, "iteration visited 10 entries");
    {
        int all_seen = 1;
        for (i = 0; i < 10; i++) if (!seen[i]) { all_seen = 0; break; }
        CHECK(all_seen, "every entry visited exactly once");
    }

    /* Empty pool: NOT_FOUND on first call. */
    {
        struct irx_vpool *empty = vpool_create(a, NULL);
        vpool_next_reset(empty);
        CHECK(vpool_next(empty, &out_name, &out_value) == VPOOL_NOT_FOUND,
              "empty pool -> NOT_FOUND");
        vpool_destroy(empty);
    }

    Lfree(a, &name);     Lfree(a, &value);
    Lfree(a, &out_name); Lfree(a, &out_value);
    vpool_destroy(pool);
}

/* ------------------------------------------------------------------ */
/*  Test: allocator injection routes everything through callbacks     */
/* ------------------------------------------------------------------ */

static void test_allocator_injection(void)
{
    struct track       st;
    struct lstr_alloc  tracking;
    struct irx_vpool  *pool;
    Lstr name, value, out;
    int i;

    printf("\n--- Test: allocator injection (no leaks) ---\n");

    st.alloc_calls = 0; st.dealloc_calls = 0; st.bytes_live = 0;
    tracking.alloc   = track_alloc;
    tracking.dealloc = track_dealloc;
    tracking.ctx     = &st;

    pool = vpool_create(&tracking, NULL);
    CHECK(pool != NULL, "vpool_create with injected allocator");
    CHECK(st.bytes_live > 0, "allocator received alloc calls");

    Lzeroinit(&name); Lzeroinit(&value); Lzeroinit(&out);

    for (i = 0; i < 50; i++) {
        char buf[16];
        sprintf(buf, "K%d", i);
        set_lstr(&tracking, &name, buf);
        sprintf(buf, "v%d", i);
        set_lstr(&tracking, &value, buf);
        vpool_set(pool, &name, &value);
    }
    for (i = 0; i < 50; i++) {
        char buf[16];
        sprintf(buf, "K%d", i);
        set_lstr(&tracking, &name, buf);
        vpool_drop(pool, &name);
    }

    Lfree(&tracking, &name);
    Lfree(&tracking, &value);
    Lfree(&tracking, &out);
    vpool_destroy(pool);

    CHECK(st.bytes_live == 0,
          "all bytes released (no leaks via injected allocator)");
    CHECK(st.alloc_calls == st.dealloc_calls,
          "alloc call count == dealloc call count");
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== REXX/370 WP-12 Variable Pool Tests ===\n");

    test_basic_set_get_drop();
    test_stem_default();
    test_resize();
    test_large_load();
    test_scope_isolation();
    test_expose_var();
    test_expose_stem();
    test_iteration();
    test_allocator_injection();

    printf("\n=== Results: %d/%d passed",
           tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
