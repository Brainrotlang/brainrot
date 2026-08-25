/* tests/abi/struct_layout_abi_check.c -- ground truth for #206 (3a).
 *
 * What this file IS: _Static_assert/offsetof checks against real C
 * struct/union declarations, compiled by this build's own $(CC) (never
 * emcc -- this binary is host-only, native LP64 in CI; it does NOT run
 * on or represent the wasm32 target, despite some fields below having
 * an ILP32 branch for documentation). It establishes, independently of
 * anything in ast.c, what a real C compiler's sizeof/offsetof actually
 * are for the field lists that mirror this repo's struct-layout
 * fixtures.
 *
 * What this file is NOT: a check that Brainrot's compute_struct_layout()
 * produces those numbers. It never calls into ast.c, never reads a
 * StructField.offset, and never touches the interpreter -- C agreeing
 * with C is not evidence about this codebase's layout code. That check
 * lives on the Brainrot side: every struct declared below has a
 * matching gang/chungus in a test_cases fixture (named in each struct's
 * own comment) whose maxxing() calls are expected to
 * print exactly the numbers asserted here on native. If a fixture's
 * expected_results.json entry is ever hand-edited to a number that
 * disagrees with this file's _Static_assert for the same shape, that is
 * the bug this file exists to catch -- but only for shapes actually
 * declared below. A fixture with no matching struct here (there was one
 * such gap, since closed: struct_field_long_modifier.brainrot's
 * `giga`-via-`lit` field, now struct Distance) is not covered by this
 * file at all, regardless of what a comment elsewhere claims.
 *
 * Coverage is native-only: this binary is built with this build's own
 * $(CC), never emcc, so it establishes LP64 ground truth. The ILP32
 * branches below are the wasm32-equivalent C values for the record, not
 * something this binary runs or verifies -- see tests/run_wasm_tests.
 * mjs's WASM_EXPECTED_OVERRIDES for the wasm32 numbers, and issue #177
 * for why native and wasm32 disagree on `giga`/pointer sizes.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* gang Mixed { cap flag; rizz n; gigachad d; }; */
struct Mixed
{
    _Bool flag;
    int n;
    double d;
};
_Static_assert(sizeof(struct Mixed) == 16,
               "struct Mixed size drifted from Brainrot's compute_struct_"
               "layout() -- update the fixture AND this file together");
_Static_assert(offsetof(struct Mixed, n) == 4, "struct Mixed.n offset");
_Static_assert(offsetof(struct Mixed, d) == 8, "struct Mixed.d offset");

/* gang Triple { rizz a; rizz b; rizz c; }; */
struct Triple
{
    int a;
    int b;
    int c;
};
_Static_assert(sizeof(struct Triple) == 12, "struct Triple size");

/* chungus WideUnion { gang Triple t; gigachad d; }; */
union WideUnion
{
    struct Triple t;
    double d;
};
_Static_assert(sizeof(union WideUnion) == 16,
               "union WideUnion size -- trailing pad to max member "
               "alignment (8), not just max member size (12)");

/* gang Box { skibidi *p; rizz x; }; */
struct Box
{
    void *p;
    int x;
};
#if UINTPTR_MAX == 0xffffffffffffffffULL
_Static_assert(sizeof(struct Box) == 16, "struct Box size (LP64)");
#elif UINTPTR_MAX == 0xffffffffUL
_Static_assert(sizeof(struct Box) == 8, "struct Box size (ILP32/wasm32)");
#endif
_Static_assert(offsetof(struct Box, x) == sizeof(void *),
               "struct Box.x offset");

/* gang Distance { yap unit; Meters value; }; -- Meters is `lit giga rizz`
 * (long). This is the ABI-sensitive shape struct_field_long_modifier.
 * brainrot exists for: sizeof(long)/_Alignof(long) is 8 on LP64 but only
 * 4 on ILP32 (wasm32), same split as pointer size above -- reusing the
 * same UINTPTR_MAX check is valid here because LP64 and ILP32 happen to
 * size long and a pointer identically on every target this repo builds
 * for (native + wasm32; there is no LLP64/Windows target in this repo's
 * CI). UINTPTR_MAX == pointer width is not a general definition of
 * sizeof(long) -- LLP64 (64-bit Windows) has 8-byte pointers but a
 * 4-byte long, which this #if would misclassify as the LP64 branch.
 * Don't reuse this #if for a target this repo doesn't actually build.
 * thicc (`long long`) is 8 on both models and is not covered here.
 */
struct Distance
{
    char unit;
    long value;
};
#if UINTPTR_MAX == 0xffffffffffffffffULL
_Static_assert(sizeof(struct Distance) == 16, "struct Distance size (LP64)");
_Static_assert(offsetof(struct Distance, value) == 8,
               "struct Distance.value offset (LP64)");
#elif UINTPTR_MAX == 0xffffffffUL
_Static_assert(sizeof(struct Distance) == 8,
               "struct Distance size (ILP32/wasm32)");
_Static_assert(offsetof(struct Distance, value) == 4,
               "struct Distance.value offset (ILP32/wasm32)");
#endif

int main(void)
{
    printf("struct Mixed:  sizeof=%zu offsetof(n)=%zu offsetof(d)=%zu\n",
           sizeof(struct Mixed), offsetof(struct Mixed, n),
           offsetof(struct Mixed, d));
    printf("union WideUnion: sizeof=%zu\n", sizeof(union WideUnion));
    printf("struct Box:    sizeof=%zu offsetof(x)=%zu\n", sizeof(struct Box),
           offsetof(struct Box, x));
    printf("struct Distance: sizeof=%zu offsetof(value)=%zu\n",
           sizeof(struct Distance), offsetof(struct Distance, value));
    return 0;
}
