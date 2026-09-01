/* tests/old_abi_sim/fake_pre_v2_registry.c -- Round-16 review finding #1.
 *
 * Simulates a libstdrot.so built before STDROT_ABI_VERSION/
 * stdrot_get_api_v2() existed: the pre-PR-205 registry shape was
 * `StdrotEntry { const char *name; StdrotFn fn; }`, discovered through a
 * function named exactly `stdrot_get_api`. This file deliberately does
 * NOT include stdrot_api.h and does NOT define any versioned entrypoint
 * (stdrot_get_api_v2, _v3, ...) -- only the OLD symbol, under the OLD
 * name, with the OLD layout -- so a
 * .so built from it is byte-for-byte what an un-rebuilt pre-ABI-
 * versioning libstdrot.so would export.
 *
 * The point of this fixture is entirely negative: prove stdrot_load()
 * (stdrot.c) detects this via dlsym("stdrot_get_api_v3") failing cleanly
 * and exits with a clear diagnostic, instead of falling back to the old
 * "stdrot_get_api" symbol and reinterpreting this file's actual memory
 * (e.g. an entry's own `name` pointer, "yapping\0", read as if it were
 * one of the new ABI's StdrotEntry POINTER slots) as the current
 * StdrotAPI shape. See STDROT_ABI_VERSION's own comment (stdrot_api.h)
 * for the full reasoning.
 */
#include <stddef.h>

typedef struct
{
    const char *name;
    void *fn;
} OldStdrotEntry;

typedef struct
{
    OldStdrotEntry *functions;
    int count;
} OldStdrotAPI;

static OldStdrotEntry old_entries[] = {
    {"yapping", NULL},
    {"slorp", NULL},
};

/* The OLD symbol name -- deliberately NOT any versioned entrypoint. */
OldStdrotAPI stdrot_get_api(void)
{
    OldStdrotAPI api;
    api.functions = old_entries;
    api.count = (int)(sizeof(old_entries) / sizeof(old_entries[0]));
    return api;
}
