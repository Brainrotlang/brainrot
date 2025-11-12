/* hm.h */

#ifndef HM_H
#define HM_H

#include <stdbool.h>
#include <stddef.h>
#include "../ast.h"

#define INIT_CAPACITY 64
#define LOAD_FACTOR 0.75

typedef struct
{
    void *key;
    void *value;
    size_t key_size;
    size_t value_size;
} HashMapNode;

typedef struct
{
    HashMapNode **nodes;
    size_t size;
    size_t capacity;
} HashMap;

size_t fnv1a_hash(const void *key, size_t len);
bool key_equal(const void *a, const void *b, size_t len);
HashMap *hm_new();
void hm_resize(HashMap *hm);
void dump(HashMap *hm);
void *hm_get(HashMap *hm, const void *key, size_t key_size);
void hm_put(HashMap *hm, const void *key, size_t key_size, void *value, size_t value_size);
void hm_free(HashMap *hm);
void hm_free_shallow(HashMap *hm);

#endif
