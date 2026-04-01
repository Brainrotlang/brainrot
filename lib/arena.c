#include "arena.h"

/* 
 * @brref Create a new arena with a given size.
 * @param size The size of the arena.
 * @return The new arena.
 */
Region *region_new(size_t size)
{
    size_t total_size = sizeof(Region) + sizeof(uintptr_t) * size;
    Region *region = (Region *)malloc(total_size);
    region->capacity = size;
    region->count = 0;
    region->next = NULL;
    return region;
}

/* 
 * @brref free the region.
 * @param region The region to free.
 */
void region_free(Region *region)
{
    free(region);
}

/* 
 * @brief allocate memory from the arena.
 * @param arena The arena to allocate from.
 * @param size_bytes The size of the memory to allocate.
 * @return The pointer to the allocated memory.
 */
void *arena_alloc(Arena *arena, size_t size_bytes)
{
    if(arena == NULL)
    {
        arena = (Arena*)malloc(sizeof(struct Arena));
        arena->start = NULL;
        arena->end = NULL;
    }
    size_t size = (size_bytes + sizeof(uintptr_t) - 1)/sizeof(uintptr_t);
    if (arena->end == NULL)
    {
        assert(arena->start == NULL);
        size_t capacity = DEFAULT_REGION_SIZE;
        if (size > capacity) capacity = size;
        arena->end = region_new(capacity);
        arena->start = arena->end;
    }

    while (arena->end->count + size > arena->end->capacity && arena->end->next != NULL)
    {
        arena->end = arena->end->next;
    }

    if (arena->end->count + size > arena->end->capacity)
    {
        assert(arena->end->next == NULL);
        size_t cap = DEFAULT_REGION_SIZE;
        if (size > cap) cap = size;
        arena->end->next = region_new(cap);
        arena->end = arena->end->next;
    }

    void *result = &arena->end->data[arena->end->count];
    arena->end->count += size;
    return result;
}


/* 
 * @brief make a copy of a string in the arena.
 * @param arena The arena to allocate from.
 * @param str The string to copy.
 * @return The pointer to the copied string.
 */
String arena_strdup(Arena *arena, String str)
{
    String out;
    out.len = str.len;
    out.data = arena_alloc(arena, out.len + 1);

    memcpy(out.data, str.data, out.len);
    out.data[out.len] = '\0'; // optional

    return out;
}

/* 
 * @brief reset the arena.
 * @param arena The arena to reset.
 */

void arena_reset(Arena *arena)
{
    arena->end = arena->start;
    while (arena->end->next != NULL)
    {
        Region *next = arena->end->next;
        region_free(arena->end);
        arena->end = next;
    }
    arena->end->count = 0;
}

/* 
 * @brief free the arena.
 * @param arena The arena to free.
 */
void arena_free(Arena *arena)
{
    if (arena == NULL) return;  // Prevent NULL pointer dereference

    Region *current = arena->start;
    while (current != NULL)
    {
        Region *next = current->next;
        region_free(current);
        current = next;
    }

    // Clear the arena structure
    arena->start = NULL;
    arena->end = NULL;
}

