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
 * lives on the Brainrot side, in test_cases/struct_layout_padding.
 * brainrot and test_cases/struct_field_long_modifier.brainrot's
 * maxxing() calls (cross-checked against the exact numbers asserted
 * here) and, more specifically, that fixture's read-back-without-
 * corruption check for interior field placement -- sizeof alone cannot
 * distinguish a correctly-aligned layout from one that happens to pack
 * fields and then pad only the total to the same final size.
 *
 * Each struct here is the literal C shape of a fixture in
 * test_cases/struct_layout_padding.brainrot and
 * test_cases/native_void_pointer_struct_field.brainrot; the sizes
 * asserted here are exactly what those fixtures' maxxing() (sizeof)
 * calls are expected to print on native (see tests/run_wasm_tests.mjs's
 * WASM_EXPECTED_OVERRIDES for the wasm32 numbers this file does not
 * cover, and issue #177 for why they differ).
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

int main(void)
{
    printf("struct Mixed:  sizeof=%zu offsetof(n)=%zu offsetof(d)=%zu\n",
           sizeof(struct Mixed), offsetof(struct Mixed, n),
           offsetof(struct Mixed, d));
    printf("union WideUnion: sizeof=%zu\n", sizeof(union WideUnion));
    printf("struct Box:    sizeof=%zu offsetof(x)=%zu\n", sizeof(struct Box),
           offsetof(struct Box, x));
    return 0;
}
