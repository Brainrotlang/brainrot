/* mem.h */

#ifndef MEM_H
#define MEM_H

#include "string_value.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

// Maximum allocation size - helps prevent integer overflow
#define MAX_ALLOC_SIZE ((size_t)-1 >> 1)

// Alignment requirement for the platform
#define ALIGNMENT sizeof(void *)

// Magic number to detect buffer overruns and validate pointers
//
// Cast to size_t explicitly: on an ILP32 target (e.g. wasm32, where
// size_t is 32-bit) an uncast 0xDEADBEEFDEADBEEFULL literal is stored
// truncated into `guard` (size_t) but compared against the full 64-bit
// literal, so the comparison promotes `guard` back up via zero-extension
// and never matches — safe_free() would treat every real safe_malloc
// pointer as corrupted and skip freeing it, unconditionally, on any
// platform where size_t is narrower than 64 bits. The cast makes both
// the stored value and the literal used for comparison the same
// (truncated) size_t value on such platforms, and is a no-op on 64-bit
// platforms where size_t already holds the full pattern.
#define MEMORY_GUARD ((size_t)0xDEADBEEFDEADBEEFULL)

typedef struct
{
    size_t guard; // Memory guard to detect corruption
    size_t size;  // Size of allocated block
    char data[];  // Flexible array member for user data
} mem_block_t;

void *handle_malloc_error(size_t size);
size_t align_size(size_t size);
void *safe_malloc(size_t size);
void *safe_malloc_array(size_t nmemb, size_t size);
void safe_free(void **ptr, const char *file, int line, const char *func);
void *safe_memcpy(void *dest, const void *src, size_t n);
String safe_strdup(const String *str);
int is_safe_malloc_ptr(const void *ptr);
void *safe_calloc(size_t count, size_t size);

// Convenience macro for type-safe allocation
#define SAFE_MALLOC(type) ((type *)safe_malloc(sizeof(type)))
#define SAFE_CALLOC(c, type) ((type *)safe_calloc((c), sizeof(type)))
#define SAFE_MALLOC_ARRAY(type, n) ((type *)safe_malloc_array((n), sizeof(type)))
// Convenience macro for safer free usage
#define SAFE_FREE(ptr) safe_free((void **)&(ptr), __FILE__, __LINE__, __func__)

#endif
