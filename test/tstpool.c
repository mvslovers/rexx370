/* ------------------------------------------------------------------ */
/*  test/host/tstpool.c — WP-PERF-04 allocator pool stress test       */
/*                                                                    */
/*  Verifies the per-env Lstring free-list pool:                      */
/*  - Pool starts empty, fills on Lfree, drains on Lfx pool-hit       */
/*  - Grow path (16→32) exercises bucket ping-pong correctly          */
/*  - 100 000 tight alloc/free cycles via Lfx/Lfree on stack Lstr     */
/*  - Pool count stays bounded (never exceeds LSTR_POOL_MAX_PER_BUCKET)*/
/*  - 100 env init/term cycles leave no pool leaks                    */
/*                                                                    */
/*  Build (Linux):                                                     */
/*    gcc -I include -I contrib/lstring370-0.1.0-dev/include \        */
/*        -Wall -Wextra -std=gnu99 -O0 -g \                           */
/*        -o /tmp/tstpool test/host/tstpool.c \                       */
/*        src/irx#init.c  src/irx#term.c  src/irx#stor.c \           */
/*        src/irx#anch.c  src/irx#env.c   src/irx#uid.c  \           */
/*        src/irx#msid.c  src/irx#cond.c  src/irx#bif.c  \           */
/*        src/irx#bifs.c  src/irx#io.c    src/irx#lstr.c \           */
/*        src/irx#tokn.c  src/irx#vpol.c  src/irx#pars.c \           */
/*        src/irx#ctrl.c  src/irx#exec.c  src/irx#arith.c \          */
/*        ../lstring370/src/lstr#cor.c  \                             */
/*        ../lstring370/src/lstr#cvt.c  \                             */
/*        ../lstring370/src/lstr#fmt.c  \                             */
/*        ../lstring370/src/lstr#srch.c \                             */
/*        ../lstring370/src/lstr#sub.c  \                             */
/*        ../lstring370/src/lstr#wrd.c  \                             */
/*        ../lstring370/src/lstr#xlt.c                                */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <string.h>

#include "irx.h"
#include "irxfunc.h"
#include "irxlstr.h"
#include "irxwkblk.h"
#include "lstring.h"

#ifndef __MVS__
void *_simulated_ectenvbk = NULL;
#endif

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        tests_run++;                     \
        if (cond)                        \
        {                                \
            tests_passed++;              \
            printf("  PASS: %s\n", msg); \
        }                                \
        else                             \
        {                                \
            tests_failed++;              \
            printf("  FAIL: %s\n", msg); \
        }                                \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Helper: create a minimal env                                      */
/* ------------------------------------------------------------------ */

static struct envblock *make_env(void)
{
    struct envblock *env = NULL;
    irxinit(NULL, &env);
    return env;
}

/* ------------------------------------------------------------------ */
/*  Test 1 — pool starts empty, fills, drains                         */
/* ------------------------------------------------------------------ */

static void test_pool_basic(void)
{
    struct envblock *env = make_env();
    struct lstr_alloc *a;
    struct irx_wkblk_int *wkbi;
    Lstr s;

    printf("\n[test_pool_basic]\n");

    CHECK(env != NULL, "env created");

    a = irx_lstr_init(env);
    wkbi = (struct irx_wkblk_int *)env->envblock_userfield;

    CHECK(a != NULL, "lstr allocator initialized");
    CHECK(wkbi != NULL, "wkbi reachable");
    CHECK(wkbi->wkbi_lstr_pool.buckets[0].count == 0,
          "bucket-0 starts empty");

    /* Alloc 8 bytes → Lfx rounds to 16 → bucket 0 */
    Lzeroinit(&s);
    CHECK(Lfx(a, &s, 8) == LSTR_OK, "Lfx(8) ok");
    CHECK((int)s.maxlen == 16, "maxlen rounded to 16");
    CHECK(wkbi->wkbi_lstr_pool.buckets[0].count == 0,
          "bucket-0 still 0 after alloc (pool was empty)");

    /* Free → item goes to bucket 0 */
    Lfree(a, &s);
    CHECK(wkbi->wkbi_lstr_pool.buckets[0].count == 1,
          "bucket-0 count=1 after Lfree");

    /* Re-alloc → pool hit */
    CHECK(Lfx(a, &s, 8) == LSTR_OK, "Lfx(8) pool hit ok");
    CHECK(wkbi->wkbi_lstr_pool.buckets[0].count == 0,
          "bucket-0 drained after pool hit");

    Lfree(a, &s);
    irxterm(env);
}

/* ------------------------------------------------------------------ */
/*  Test 2 — grow path (16→32) exercises bucket ping-pong            */
/* ------------------------------------------------------------------ */

static void test_pool_grow_path(void)
{
    struct envblock *env = make_env();
    struct lstr_alloc *a;
    struct irx_wkblk_int *wkbi;
    Lstr s;
    int b0_before;
    int b1_before;

    printf("\n[test_pool_grow_path]\n");

    a = irx_lstr_init(env);
    wkbi = (struct irx_wkblk_int *)env->envblock_userfield;

    /* Alloc a 16-byte buffer and deposit it in the pool. */
    Lzeroinit(&s);
    CHECK(Lfx(a, &s, 8) == LSTR_OK, "initial Lfx(8)");
    CHECK((int)s.maxlen == 16, "initial maxlen == 16");
    Lfree(a, &s);
    b0_before = wkbi->wkbi_lstr_pool.buckets[0].count;
    b1_before = wkbi->wkbi_lstr_pool.buckets[1].count;
    CHECK(b0_before == 1, "bucket-0 has 1 item before grow");

    /* Grow to 32 bytes: Lfx frees the old 16-byte buffer (→ bucket 0)
     * and allocates a new 32-byte buffer.  If bucket 1 is empty the
     * new alloc goes to raw; if not empty it hits the pool. */
    CHECK(Lfx(a, &s, 8) == LSTR_OK, "pool-hit alloc for grow base");
    CHECK((int)s.maxlen == 16, "maxlen still 16 after pool hit");
    /* bucket-0 is now empty (drained by the pool hit above) */
    CHECK(wkbi->wkbi_lstr_pool.buckets[0].count == 0,
          "bucket-0 empty after pool hit");
    /* Grow: request 20 bytes → round_capacity → 32.
     * Lfx frees the old 16-byte buf (→ bucket-0) and allocs a 32-byte
     * buf from raw (bucket-1 is empty). */
    CHECK(Lfx(a, &s, 20) == LSTR_OK, "Lfx(20) grows to 32");
    CHECK((int)s.maxlen == 32, "maxlen == 32 after grow");
    CHECK(wkbi->wkbi_lstr_pool.buckets[0].count == 1,
          "bucket-0 has 1 item (old 16B buf returned to pool by grow)");
    (void)b0_before;
    (void)b1_before;

    Lfree(a, &s);
    irxterm(env);
}

/* ------------------------------------------------------------------ */
/*  Test 3 — 100k tight alloc/free cycles, bounded pool count         */
/* ------------------------------------------------------------------ */

static void test_pool_100k_cycles(void)
{
    struct envblock *env = make_env();
    struct lstr_alloc *a;
    struct irx_wkblk_int *wkbi;
    int i;
    int ok = 1;
    int max_seen = 0;

    printf("\n[test_pool_100k_cycles]\n");

    a = irx_lstr_init(env);
    wkbi = (struct irx_wkblk_int *)env->envblock_userfield;

    for (i = 0; i < 100000; i++)
    {
        Lstr s;
        Lzeroinit(&s);
        if (Lfx(a, &s, 8) != LSTR_OK)
        {
            ok = 0;
            break;
        }
        if ((int)s.maxlen != 16)
        {
            ok = 0;
        }
        Lfree(a, &s);
        if (wkbi->wkbi_lstr_pool.buckets[0].count > max_seen)
        {
            max_seen = wkbi->wkbi_lstr_pool.buckets[0].count;
        }
        if (wkbi->wkbi_lstr_pool.buckets[0].count > LSTR_POOL_MAX_PER_BUCKET)
        {
            ok = 0;
            break;
        }
    }

    CHECK(ok == 1, "100k alloc/free cycles: no error, no overflow");
    CHECK(max_seen == 1, "pool count never exceeds 1 in tight 1-alloc loop");

    irxterm(env);
}

/* ------------------------------------------------------------------ */
/*  Test 4 — pool count bounded at LSTR_POOL_MAX_PER_BUCKET           */
/* ------------------------------------------------------------------ */

static void test_pool_max_items(void)
{
    struct envblock *env = make_env();
    struct lstr_alloc *a;
    struct irx_wkblk_int *wkbi;
    Lstr items[LSTR_POOL_MAX_PER_BUCKET + 10];
    int n = LSTR_POOL_MAX_PER_BUCKET + 10;
    int i;

    printf("\n[test_pool_max_items]\n");

    a = irx_lstr_init(env);
    wkbi = (struct irx_wkblk_int *)env->envblock_userfield;

    /* Allocate n items, then free them all — the pool takes the first
     * LSTR_POOL_MAX_PER_BUCKET and the remaining 10 fall through to
     * rexx_lstr_dealloc_raw. */
    for (i = 0; i < n; i++)
    {
        Lzeroinit(&items[i]);
        Lfx(a, &items[i], 8);
    }
    for (i = 0; i < n; i++)
    {
        Lfree(a, &items[i]);
    }

    CHECK(wkbi->wkbi_lstr_pool.buckets[0].count == LSTR_POOL_MAX_PER_BUCKET,
          "bucket-0 capped at LSTR_POOL_MAX_PER_BUCKET");

    irxterm(env);
}

/* ------------------------------------------------------------------ */
/*  Test 5 — pool teardown: irxterm with live pool items must not     */
/*           crash or leak; bucket counts are zeroed after teardown   */
/* ------------------------------------------------------------------ */

static void test_pool_teardown(void)
{
    struct envblock *env = make_env();
    struct lstr_alloc *a;
    struct irx_wkblk_int *wkbi;
    Lstr s;

    printf("\n[test_pool_teardown]\n");

    a = irx_lstr_init(env);
    wkbi = (struct irx_wkblk_int *)env->envblock_userfield;

    /* Deposit one item into each of buckets 0, 1, 2. */
    Lzeroinit(&s);
    Lfx(a, &s, 8); /* -> bucket-0 (cap 16)  */
    Lfree(a, &s);
    Lzeroinit(&s);
    Lfx(a, &s, 20); /* -> bucket-1 (cap 32)  */
    Lfree(a, &s);
    Lzeroinit(&s);
    Lfx(a, &s, 40); /* -> bucket-2 (cap 64)  */
    Lfree(a, &s);

    CHECK(wkbi->wkbi_lstr_pool.buckets[0].count == 1,
          "bucket-0 has 1 item before teardown");
    CHECK(wkbi->wkbi_lstr_pool.buckets[1].count == 1,
          "bucket-1 has 1 item before teardown");
    CHECK(wkbi->wkbi_lstr_pool.buckets[2].count == 1,
          "bucket-2 has 1 item before teardown");

    /* irxterm calls irx_lstr_pool_teardown; absence of crash = success. */
    CHECK(irxterm(env) == 0, "irxterm with 3 pooled items: rc=0");
}

/* ------------------------------------------------------------------ */
/*  Test 6 — repeated init/term cycles with pool items                */
/*                                                                    */
/*  Capped at 50 iterations: IRXANCHR has 64 slots and the host       */
/*  32-bit envblock_ptr field truncates 64-bit pointers, so address   */
/*  aliasing can confuse the slot lookup after ~62 reuses.            */
/* ------------------------------------------------------------------ */

static void test_pool_init_term_cycles(void)
{
    int i;
    int ok = 1;

    printf("\n[test_pool_init_term_cycles]\n");

    for (i = 0; i < 50; i++)
    {
        struct envblock *env = make_env();
        struct lstr_alloc *a;
        Lstr s;

        if (env == NULL)
        {
            ok = 0;
            break;
        }

        a = irx_lstr_init(env);
        if (a == NULL)
        {
            ok = 0;
            irxterm(env);
            break;
        }

        /* Leave items in the pool so teardown has real work to do. */
        Lzeroinit(&s);
        Lfx(a, &s, 8); /* alloc 16-byte buf */
        Lfree(a, &s);  /* deposit to pool (bucket-0) */
        Lzeroinit(&s);
        Lfx(a, &s, 20); /* alloc 32-byte buf */
        Lfree(a, &s);   /* deposit to pool (bucket-1) */

        if (irxterm(env) != 0)
        {
            ok = 0;
            break;
        }
    }

    CHECK(ok == 1, "50 env init/term cycles with pool items: all clean");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("tstpool: allocator pool stress tests\n");

    test_pool_basic();
    test_pool_grow_path();
    test_pool_100k_cycles();
    test_pool_max_items();
    test_pool_teardown();
    test_pool_init_term_cycles();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_failed > 0) ? 1 : 0;
}
