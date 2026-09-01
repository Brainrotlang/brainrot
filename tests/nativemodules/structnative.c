/* tests/nativemodules/structnative.c -- by-value struct arguments across
 * the native ABI (STDROT_STRUCT, issue #208 Phase 5 Road B), built into
 * tests/nativemodules/structnative.so by the Makefile's nativemodules
 * pattern rule.
 *
 * This is the fixture that proves the premise the whole generated-binding
 * road rests on: that a Brainrot `gang` and the equivalent C struct have
 * the SAME byte layout, so a native can memcpy the bytes it receives
 * straight into a real C type and read correct field values out. Nothing
 * here reimplements Brainrot's layout -- the structs below are ordinary C
 * declarations, so the _Static_asserts are a real C compiler's opinion,
 * and the values the Brainrot side reads back are only correct if
 * compute_struct_layout() (ast.c) agreed with it.
 *
 * Vec2 is the shape that matters in practice (raylib's Vector2 exactly);
 * Mixed is deliberately the padded, offset-sensitive shape already used
 * as ground truth in tests/abi/struct_layout_abi_check.c and
 * test_cases/struct_layout_padding.brainrot, so a layout regression shows
 * up here as a wrong VALUE rather than only as a wrong size.
 */
#include "stdrot_api.h"
#include <stddef.h>
#include <string.h>

/* gang Vec2 { chad x; chad y; }; */
typedef struct
{
    float x;
    float y;
} Vec2;
_Static_assert(sizeof(Vec2) == 8, "Vec2 size");
_Static_assert(offsetof(Vec2, y) == 4, "Vec2.y offset");

/* gang Mixed { cap flag; rizz n; gigachad d; }; -- same shape as struct
 * Mixed in tests/abi/struct_layout_abi_check.c. */
typedef struct
{
    _Bool flag;
    int n;
    double d;
} Mixed;
_Static_assert(sizeof(Mixed) == 16, "Mixed size");
_Static_assert(offsetof(Mixed, n) == 4, "Mixed.n offset");
_Static_assert(offsetof(Mixed, d) == 8, "Mixed.d offset");

/* Every native below checks .val.blob.size against its own sizeof before
 * copying, and returns a distinctive out-of-band sentinel on mismatch
 * rather than reading whatever it was handed. This is the check
 * STDROT_STRUCT's contract (stdrot_api.h) tells a binding to make and the
 * one a generator would emit mechanically: the host promises the blob is
 * that struct's C-ABI image, and a native that memcpy's without checking
 * turns a host-side layout bug into an out-of-bounds read inside someone
 * else's library. */
#define SIZE_MISMATCH_SENTINEL (-1.0f)

static StdrotValue native_vec2_len2(StdrotValue *args, int argc)
{
    (void)argc;
    if (args[0].val.blob.size != sizeof(Vec2))
    {
        return (StdrotValue){.type = STDROT_FLOAT,
                             .val = {.f = SIZE_MISMATCH_SENTINEL}};
    }
    Vec2 v;
    memcpy(&v, args[0].val.blob.data, sizeof(v));
    return (StdrotValue){.type = STDROT_FLOAT,
                         .val = {.f = v.x * v.x + v.y * v.y}};
}

/* Writes through its own argument copy, then reports what it wrote. The
 * point is entirely on the Brainrot side: the caller's variable must be
 * UNCHANGED afterward. If .val.blob.data ever went back to aliasing the
 * live variable's storage instead of the adapter-owned copy
 * execute_native_call() makes, this call would silently rewrite the
 * caller's struct and the fixture's read-back would change. */
static StdrotValue native_vec2_scribble(StdrotValue *args, int argc)
{
    (void)argc;
    if (args[0].val.blob.size != sizeof(Vec2))
    {
        return (StdrotValue){.type = STDROT_FLOAT,
                             .val = {.f = SIZE_MISMATCH_SENTINEL}};
    }
    Vec2 *v = (Vec2 *)args[0].val.blob.data;
    v->x = 111.0f;
    v->y = 222.0f;
    return (StdrotValue){.type = STDROT_FLOAT, .val = {.f = v->x + v->y}};
}

/* Reads every field of the padded shape, so a wrong interior offset
 * (rather than merely a wrong total size) changes the result. */
static StdrotValue native_mixed_probe(StdrotValue *args, int argc)
{
    (void)argc;
    if (args[0].val.blob.size != sizeof(Mixed))
    {
        return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
    }
    Mixed m;
    memcpy(&m, args[0].val.blob.data, sizeof(m));
    return (StdrotValue){.type = STDROT_INT,
                         .val = {.i = (m.flag ? 1000000 : 0) + m.n + (int)m.d}};
}

/* Two struct parameters, so the per-argument scratch copies are proven
 * independent of each other (one shared/overwritten buffer would make the
 * second argument's values show up for both). */
static StdrotValue native_vec2_dot(StdrotValue *args, int argc)
{
    (void)argc;
    if (args[0].val.blob.size != sizeof(Vec2) ||
        args[1].val.blob.size != sizeof(Vec2))
    {
        return (StdrotValue){.type = STDROT_FLOAT,
                             .val = {.f = SIZE_MISMATCH_SENTINEL}};
    }
    Vec2 a;
    Vec2 b;
    memcpy(&a, args[0].val.blob.data, sizeof(a));
    memcpy(&b, args[1].val.blob.data, sizeof(b));
    return (StdrotValue){.type = STDROT_FLOAT,
                         .val = {.f = a.x * b.x + a.y * b.y}};
}

static const StdrotParam vec2_params[] = {
    {STDROT_STRUCT, "Vec2", 0},
};
STDROT_EXPORT_SIG("vec2_len2", native_vec2_len2,
                  ((StdrotParam){STDROT_FLOAT, NULL, 0}), vec2_params, 1, 1,
                  false);
STDROT_EXPORT_SIG("vec2_scribble", native_vec2_scribble,
                  ((StdrotParam){STDROT_FLOAT, NULL, 0}), vec2_params, 1, 1,
                  false);

static const StdrotParam mixed_params[] = {
    {STDROT_STRUCT, "Mixed", 0},
};
STDROT_EXPORT_SIG("mixed_probe", native_mixed_probe,
                  ((StdrotParam){STDROT_INT, NULL, 0}), mixed_params, 1, 1,
                  false);

static const StdrotParam vec2_dot_params[] = {
    {STDROT_STRUCT, "Vec2", 0},
    {STDROT_STRUCT, "Vec2", 0},
};
STDROT_EXPORT_SIG("vec2_dot", native_vec2_dot,
                  ((StdrotParam){STDROT_FLOAT, NULL, 0}), vec2_dot_params, 2, 2,
                  false);

/* Deliberately UNCALLABLE: a STDROT_STRUCT *return* is well-formed as a
 * descriptor (validate_native_registry() accepts it, so this module loads
 * fine) but semantic_check_native_call() rejects every call to it,
 * because returning a by-value aggregate needs an ownership answer this
 * ABI hasn't made -- see STDROT_STRUCT's own comment in stdrot_api.h. The
 * body is never reached; it exists so the rejection is tested against a
 * real registered export rather than a hypothetical one. If a future
 * change implements struct returns, this fixture's test is the one that
 * should start failing and tell you to update it. */
static StdrotValue native_vec2_make(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){.type = STDROT_NONE};
}
STDROT_EXPORT_SIG("vec2_make", native_vec2_make,
                  ((StdrotParam){STDROT_STRUCT, "Vec2", 0}), NULL, 0, 0, false);
