#include "mem.h"

/**
 * @brief Retrieves the memory block header from a user pointer
 *
 * Given a pointer to the user data portion of an allocation made by safe_malloc,
 * calculates and returns a pointer to the associated mem_block_t header.
 *
 * @param ptr Pointer to examine, must have been returned by safe_malloc
 * @return mem_block_t* Pointer to the block header, or NULL if ptr is NULL
 *
 * @warning This function performs no validation - the caller must ensure ptr
 *          was actually allocated by safe_malloc
 */
static inline mem_block_t *get_block_ptr(const void *ptr)
{
    return ptr ? ((mem_block_t *)ptr - 1) : NULL;
}

/**
 * @brief Aligns a size value to the platform's memory alignment requirement
 *
 * Rounds up the given size to the next multiple of ALIGNMENT, with overflow checking.
 *
 * @param size Size value to align
 * @return size_t Aligned size value, or 0 if alignment would cause overflow
 *
 * @note The alignment value is platform-dependent and defined by ALIGNMENT macro
 */
size_t align_size(size_t size)
{
    // Check for overflow before alignment
    if (size > MAX_ALLOC_SIZE - (ALIGNMENT - 1))
    {
        return 0; // Indicate overflow
    }
    return (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}

/**
 * @brief Handles memory allocation failures with error reporting
 *
 * Reports allocation failures to stderr with size and error information,
 * sets errno to ENOMEM, and returns NULL.
 *
 * @param size The size of the failed allocation attempt
 * @return void* Always returns NULL
 *
 * @note Sets errno to ENOMEM
 */
void *handle_malloc_error(size_t size)
{
    fprintf(stderr, "Memory allocation failed - Size: %zu, Error: %s\n",
            size, strerror(errno));
    errno = ENOMEM;
    return NULL;
}

/**
 * @brief Safely allocates and zero-initializes memory with overflow checking
 *
 * Allocates and zero-initializes memory for an array with the following safety features:
 * - Overflow checking on element count and size
 * - Memory alignment
 * - Zero initialization
 * - Size tracking
 * - Guard pattern for corruption detection
 *
 * @param count Number of elements to allocate
 * @param size Size of each element in bytes
 * @return void* Pointer to allocated and zero-initialized memory, or NULL if:
 *         - count or size is 0
 *         - count * size > MAX_ALLOC_SIZE
 *         - alignment would cause overflow
 *         - system is out of memory
 *
 * @note Sets errno on failure
 */
void *safe_calloc(size_t count, size_t size)
{
    if (count == 0 || size == 0)
    {
        return NULL;
    }

    // Check for overflow in multiplication
    if (count > MAX_ALLOC_SIZE / size)
    {
        return handle_malloc_error(count * size);
    }

    size_t total_size = count * size;
    if (total_size > MAX_ALLOC_SIZE)
    {
        return handle_malloc_error(total_size);
    }

    size_t aligned_size = align_size(total_size);
    if (aligned_size == 0 || aligned_size > MAX_ALLOC_SIZE)
    {
        return handle_malloc_error(total_size);
    }

    // Calculate total size needed including metadata
    size_t block_size = sizeof(mem_block_t) + aligned_size;
    if (block_size < total_size)
    { // Check for overflow
        return handle_malloc_error(total_size);
    }

    mem_block_t *block = malloc(block_size);
    if (block == NULL)
    {
        return handle_malloc_error(total_size);
    }

    block->guard = MEMORY_GUARD;
    block->size = aligned_size;
    memset(block->data, 0, aligned_size);

    return block->data;
}


/**
 * @brief Safely allocates memory with overflow checking and zero initialization
 *
 * Allocates memory with the following safety features:
 * - Overflow checking on size calculations
 * - Memory alignment
 * - Zero initialization
 * - Size tracking
 * - Guard pattern for corruption detection
 *
 * @param size Number of bytes to allocate
 * @return void* Pointer to allocated memory, or NULL if:
 *         - size is 0
 *         - size > MAX_ALLOC_SIZE
 *         - alignment would cause overflow
 *         - system is out of memory
 *
 * @note Sets errno on failure
 */
void *safe_malloc(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }

    if (size > MAX_ALLOC_SIZE)
    {
        return handle_malloc_error(size);
    }

    size_t aligned_size = align_size(size);
    if (aligned_size == 0 || aligned_size > MAX_ALLOC_SIZE)
    {
        return handle_malloc_error(size);
    }

    // Calculate total size needed including metadata
    size_t total_size = sizeof(mem_block_t) + aligned_size;
    if (total_size < size)
    { // Check for overflow
        return handle_malloc_error(size);
    }

    mem_block_t *block = malloc(total_size);
    if (block == NULL)
    {
        return handle_malloc_error(size);
    }

    block->guard = MEMORY_GUARD;
    block->size = aligned_size;
    memset(block->data, 0, aligned_size);

    return block->data;
}

/**
 * @brief Safely allocates an array with overflow checking
 *
 * Allocates memory for an array of elements, checking for multiplication
 * overflow in the size calculation.
 *
 * @param nmemb Number of elements to allocate
 * @param size Size of each element
 * @return void* Pointer to allocated array, or NULL if:
 *         - nmemb * size would overflow
 *         - allocation fails for any reason
 *
 * @note Equivalent to safe_malloc(nmemb * size) but with overflow check
 */
void *safe_malloc_array(size_t nmemb, size_t size)
{
    // Check multiplication overflow
    if (nmemb > 0 && size > MAX_ALLOC_SIZE / nmemb)
    {
        return handle_malloc_error(nmemb * size);
    }
    return safe_malloc(nmemb * size);
}

/**
 * @brief Validates if a pointer was allocated by safe_malloc
 *
 * Checks if a pointer appears to have been allocated by safe_malloc by
 * verifying the presence and validity of the guard pattern.
 *
 * @param ptr Pointer to validate
 * @return int 1 if pointer appears valid, 0 otherwise
 *
 * @warning A matching guard pattern does not guarantee the pointer is valid,
 *          but a non-matching pattern guarantees it is invalid
 */
int is_safe_malloc_ptr(const void *ptr)
{
    if (!ptr)
        return 0;
    const mem_block_t *block = get_block_ptr(ptr);
    return block->guard == MEMORY_GUARD;
}

/**
 * @brief Safely frees memory allocated by safe_malloc
 *
 * Frees memory with additional safety features:
 * - Validates the pointer was allocated by safe_malloc
 * - Wipes memory contents before freeing
 * - Sets pointer to NULL after freeing
 * - Handles NULL pointers safely
 *
 * @param ptr Address of pointer to free. Must not be NULL.
 *            Points to memory allocated by safe_malloc.
 *
 * @note If ptr or *ptr is NULL, function returns without action
 * @note If pointer appears invalid, prints warning and returns without freeing
 */
void safe_free(void **ptr, const char* file, int line, const char* func)
{
    if (!ptr || !*ptr)
    {
        return;
    }

    mem_block_t *block = get_block_ptr(*ptr);
    if (!block || block->guard != MEMORY_GUARD)
    {
        fprintf(stderr, "Warning: Attempt to free invalid/corrupted pointer, %s, %d, %s\n",
                file, line, func);
        return;
    }

    // Clear user data
    memset(block->data, 0, block->size);
    // Clear metadata
    block->guard = 0;
    block->size = 0;

    free(block);
    *ptr = NULL;
}

/**
 * @brief Safely copies memory between buffers with extensive validation
 *
 * Copies memory with the following safety checks:
 * - NULL pointer validation
 * - Size limit validation
 * - Destination buffer size validation
 * - Buffer overlap detection
 * - Safe_malloc pointer validation
 *
 * @param dest Destination buffer (must be from safe_malloc)
 * @param src Source buffer
 * @param n Number of bytes to copy
 * @return void* Pointer to destination buffer, or NULL if:
 *         - dest or src is NULL
 *         - n > MAX_ALLOC_SIZE
 *         - dest not from safe_malloc
 *         - dest buffer too small
 *         - buffers overlap
 *
 * @note Sets errno on failure
 */
void *safe_memcpy(void *dest, const void *src, size_t n)
{
    if (!dest || !src)
    {
        errno = EINVAL;
        return NULL;
    }

    if (n == 0)
    {
        return dest;
    }

    if (n > MAX_ALLOC_SIZE)
    {
        errno = ERANGE;
        return NULL;
    }

    // Verify dest is from safe_malloc
    if (!is_safe_malloc_ptr(dest))
    {
        errno = EINVAL;
        return NULL;
    }

    const mem_block_t *block = get_block_ptr(dest);
    if (block->size < n)
    {
        errno = ERANGE;
        return NULL;
    }

    // Check for buffer overlap
    if ((src < dest && (const char *)src + n > dest) ||
        (dest < src && (char *)dest + n > src))
    {
        errno = EINVAL;
        return NULL;
    }

    return memcpy(dest, src, n);
}

/**
 * @brief Safely duplicates a string with extensive validation
 *
 * Creates a new string in safe_malloc'd memory with the following features:
 * - NULL pointer checking
 * - Proper size calculation with overflow detection
 * - Secure string copying
 * - Zero initialization
 *
 * @param str String to duplicate
 * @return char* Pointer to new string, or NULL if:
 *         - str is NULL
 *         - memory allocation fails
 *         - string length exceeds MAX_ALLOC_SIZE
 *
 * @note Sets errno on failure
 * @note Returned string must be freed with safe_free
 */
char *safe_strdup(const char *str)
{
    if (!str)
    {
        errno = EINVAL;
        return NULL;
    }

    size_t len = strlen(str) + 1;
    char *new_str = safe_malloc(len);
    if (new_str)
    {
        memcpy(new_str, str, len);
    }
    return new_str;
}


