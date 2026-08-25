/* tests/abi/struct_layout_abi_check.c -- compile-time C-ABI proof for #206
 * (3a). compute_struct_layout()/compute_union_layout() (ast.c) claim to
 * reproduce the host C compiler's struct/union layout (alignment, field
 * offsets, trailing padding). That claim is only actually proven by the
 * host C compiler itself: _Static_assert against sizeof/offsetof of a
 * real C struct declaration, compiled by the same $(CC) that would
 * compile any FFI code Brainrot hands a `gang` to, on whatever target
 * this build is for (native LP64, wasm32 ILP32, ...).
 *
 * Each struct here is the literal C shape of a fixture in
 * test_cases/struct_layout_padding.brainrot and
 * test_cases/native_void_pointer_struct_field.brainrot; the sizes
 * asserted here are exactly what those fixtures' maxxing() (sizeof)
 * calls are expected to print. If a platform's ABI ever disagrees with
 * the hardcoded expectations in tests/expected_results.json (as already
 * happens for wasm32 -- see tests/run_wasm_tests.mjs's
 * WASM_EXPECTED_OVERRIDES and issue #177), this file is what tells you
 * which side is right: it fails to *compile* rather than merely
 * reporting a mismatched runtime number, so it can't silently pass by
 * both sides agreeing with each other.
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
