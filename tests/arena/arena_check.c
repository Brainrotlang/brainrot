/* tests/arena/arena_check.c -- unit tests for arena allocator and arena_reset
 *
 * Exercises the arena_reset contract (issue #286):
 * 1. Force >= 3 regions, reset, then free (catches dangling start / UAF).
 * 2. Reset, realloc, and assert all regions are reused, including
 *    pointer equality with the first pre-reset allocation (catches partial
 *    count reset).
 * 3. Reset of {0} / unallocated arena and second/repeated resets (catches
 *    crashes on empty/repeated reset).
 */

#include "lib/arena.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_arena_multi_region_reset_and_free(void)
{
    Arena arena = {0};
    const size_t chunk_size = DEFAULT_REGION_SIZE * sizeof(uintptr_t);

    /* Allocate 3 large chunks to force at least 3 distinct regions. */
    void *p1 = arena_alloc(&arena, chunk_size);
    void *p2 = arena_alloc(&arena, chunk_size);
    void *p3 = arena_alloc(&arena, chunk_size);

    assert(p1 != NULL);
    assert(p2 != NULL);
    assert(p3 != NULL);
    assert(p1 != p2);
    assert(p2 != p3);

    /* Verify we have at least 3 regions linked. */
    int region_count = 0;
    Region *r = arena.start;
    while (r != NULL)
    {
        region_count++;
        r = r->next;
    }
    assert(region_count >= 3);

    /* Reset the arena: must preserve all allocated regions with count = 0. */
    arena_reset(&arena);
    assert(arena.start != NULL);
    assert(arena.end == arena.start);

    int reset_region_count = 0;
    r = arena.start;
    while (r != NULL)
    {
        assert(r->count == 0);
        reset_region_count++;
        r = r->next;
    }
    assert(reset_region_count == region_count);

    /* Under the old bug, regions were freed during reset and arena.start
     * was left dangling; arena_free would then trigger use-after-free. */
    arena_free(&arena);
    assert(arena.start == NULL);
    assert(arena.end == NULL);
}

static void test_arena_reset_reuse_allocations(void)
{
    Arena arena = {0};
    const size_t chunk_size = DEFAULT_REGION_SIZE * sizeof(uintptr_t);

    /* Initial allocations forcing 3 regions. */
    void *orig_p1 = arena_alloc(&arena, chunk_size);
    void *orig_p2 = arena_alloc(&arena, chunk_size);
    void *orig_p3 = arena_alloc(&arena, chunk_size);

    assert(orig_p1 != NULL && orig_p2 != NULL && orig_p3 != NULL);

    /* Reset. */
    arena_reset(&arena);
    assert(arena.end == arena.start);

    /* Reallocate the same chunks: must reuse existing regions and match
     * original pointers. If count was only reset on start, subsequent
     * allocations would skip existing regions and allocate new ones. */
    void *new_p1 = arena_alloc(&arena, chunk_size);
    void *new_p2 = arena_alloc(&arena, chunk_size);
    void *new_p3 = arena_alloc(&arena, chunk_size);

    assert(new_p1 == orig_p1);
    assert(new_p2 == orig_p2);
    assert(new_p3 == orig_p3);

    /* Confirm no new 4th region was added. */
    int count = 0;
    Region *r = arena.start;
    while (r != NULL)
    {
        count++;
        r = r->next;
    }
    assert(count >= 3);
    assert(arena.end->next == NULL);

    arena_free(&arena);
}

static void test_arena_reset_empty_and_repeated(void)
{
    /* 1. Reset on a zero-initialized / never-allocated arena. */
    Arena empty = {0};
    arena_reset(&empty);
    assert(empty.start == NULL);
    assert(empty.end == NULL);

    /* Second reset must also be safe. */
    arena_reset(&empty);
    assert(empty.start == NULL);
    assert(empty.end == NULL);
    arena_free(&empty);

    /* 2. NULL pointer safety. */
    arena_reset(NULL);
    arena_free(NULL);

    /* 3. Consecutive resets after allocation. */
    Arena arena = {0};
    void *p = arena_alloc(&arena, 256);
    assert(p != NULL);

    arena_reset(&arena);
    arena_reset(&arena);

    void *p_after = arena_alloc(&arena, 256);
    assert(p_after == p);

    arena_free(&arena);

    /* 4. Multiple alloc-reset cycles. */
    Arena cycle_arena = {0};
    for (int i = 0; i < 10; i++)
    {
        void *ptr = arena_alloc(&cycle_arena, 128);
        assert(ptr != NULL);
        arena_reset(&cycle_arena);
    }
    arena_free(&cycle_arena);
}

int main(void)
{
    test_arena_multi_region_reset_and_free();
    test_arena_reset_reuse_allocations();
    test_arena_reset_empty_and_repeated();
    printf("arena_check: all tests passed.\n");
    return 0;
}
