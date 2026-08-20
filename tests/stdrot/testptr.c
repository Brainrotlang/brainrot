/* tests/stdrot/testptr.c – Minimal STDROT_PTR native bindings, committed
 * as permanent test-only infrastructure.
 *
 * Issue #205 mandates STDROT_PTR as part of the typed native ABI, but no
 * production native in this library actually takes or returns one --
 * without a native that does, STDROT_PTR round-tripping (as a parameter,
 * as a return value, and the pointer_level + 1 convention documented in
 * stdrot_api.h) has no coverage at all. These exist only to be called
 * from .brainrot fixtures under test_cases/.
 *
 * Deliberately NOT under stdrot/: that directory is wildcard-globbed
 * (STDROT_SRCS in the Makefile) straight into the production
 * libstdrot.so, `make install`, and the wasm build. A self-registering
 * native placed there is a real, permanently-shipped Brainrot builtin
 * the moment it compiles, no matter what a comment claims -- there's no
 * such thing as a "spiritually test-only" entry in a self-registering
 * plugin section. This file is instead compiled into a separate,
 * test-only library (tests/libstdrot.so, see the Makefile's
 * TEST_STDROT_LIB rule) that only `make test`/`make valgrind` and CI's
 * test job ever load, via stdrot_load()'s STDROT_LIB_PATH override
 * (stdrot.c) -- the production libstdrot.so and brainrot.wasm never see
 * this file at all. */
#include "stdrot_api.h"

/* poke_int(rizz *p, rizz v): writes v through p. Exercises STDROT_PTR as
 * a parameter type. */
static StdrotValue stdrot_poke_int(StdrotValue *args, int argc)
{
    (void)argc;
    int *p = (int *)args[0].val.ptr;
    *p = args[1].val.i;
    return (StdrotValue){STDROT_NONE, {0}};
}

static const StdrotParam poke_int_params[] = {
    {STDROT_PTR, NULL, 0},
    {STDROT_INT, NULL, 0},
};
STDROT_EXPORT_SIG("poke_int", stdrot_poke_int,
                  ((StdrotParam){STDROT_NONE, NULL, 0}), poke_int_params, 2, 2,
                  false);

/* peek_int(rizz *p) -> rizz: reads *p back. Together with poke_int, this
 * round-trips a value through a native that only ever sees an opaque
 * address -- the STDROT_PTR parameter side of the ABI. */
static StdrotValue stdrot_peek_int(StdrotValue *args, int argc)
{
    (void)argc;
    int *p = (int *)args[0].val.ptr;
    StdrotValue out = {STDROT_INT, {0}};
    out.val.i = *p;
    return out;
}

static const StdrotParam peek_int_params[] = {
    {STDROT_PTR, NULL, 0},
};
STDROT_EXPORT_SIG("peek_int", stdrot_peek_int,
                  ((StdrotParam){STDROT_INT, NULL, 0}), peek_int_params, 1, 1,
                  false);

/* test_ptr_source() -> rizz*: returns the address of a static backing
 * int -- the STDROT_PTR *return* side of the ABI, the other direction
 * from poke_int/peek_int. Deliberately a static rather than malloc()'d,
 * so there's nothing for a test program to leak by never freeing it; the
 * point is exercising pointer_level plumbing, not resource ownership. */
static int test_ptr_backing = 0;

static StdrotValue stdrot_test_ptr_source(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    StdrotValue out = {STDROT_PTR, {0}};
    out.val.ptr = &test_ptr_backing;
    return out;
}

STDROT_EXPORT_SIG("test_ptr_source", stdrot_test_ptr_source,
                  ((StdrotParam){STDROT_PTR, NULL, 0}), NULL, 0, 0, false);
