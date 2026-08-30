/* stdrot/yapidx.c -- yapidx(rant hay, rant needle) -> rizz, first byte
 * index of `needle` in `hay`, or -1 when absent.
 *
 * An empty needle returns 0, matching Go's strings.Index (every string
 * contains the empty string at position 0). That is a deliberate choice
 * rather than a degenerate case falling out of the loop: the alternative
 * (-1, "not found") would make `yapidx(h, x) >= 0` a wrong test for
 * "contains" the moment x is empty, which is exactly the shape callers
 * will build on top of this.
 *
 * Not strstr(): a `rant` is length-prefixed and NOT NUL-terminated
 * (lib/string_value.h), so strstr() would read past both buffers, and it
 * cannot find a needle containing an embedded NUL. This is a plain
 * length-bounded scan -- O(n*m) worst case, which is the right trade for
 * v1 at these sizes and matches what libc does for short needles anyway.
 *
 * The declared StdrotParams are enforced by enforce_arg_type() (stdrot.c)
 * before fn() is entered, so there is no argument-type check here.
 */
#include "stdrot_api.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

StdrotValue stdrot_yapidx(StdrotValue *args, int argc)
{
    (void)argc;
    const char *hay = args[0].val.str.data;
    const char *needle = args[1].val.str.data;
    /* .data == NULL is representable by the String struct; treat such a
       value as empty rather than pairing a null buffer with a length. */
    size_t hlen = hay ? args[0].val.str.len : 0;
    size_t nlen = needle ? args[1].val.str.len : 0;

    if (nlen == 0)
    {
        return (StdrotValue){.type = STDROT_INT, .val = {.i = 0}};
    }
    /* A needle longer than the haystack cannot match, and returning here
       also keeps the loop's bound from underflowing: `i + nlen <= hlen` is
       size_t arithmetic. */
    if (nlen > hlen)
    {
        return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
    }
    /* Every returned index is < hlen, so bounding hlen bounds the result.
       See yaplen.c for why an unrepresentable length is refused rather
       than truncated. */
    if (hlen > (size_t)INT_MAX)
    {
        fprintf(stderr,
                "Error: yapidx: haystack length %zu exceeds the largest "
                "representable rizz at line %d\n",
                hlen, g_exec_context.line_number);
        exit(1);
    }

    for (size_t i = 0; i + nlen <= hlen; i++)
    {
        if (memcmp(hay + i, needle, nlen) == 0)
        {
            return (StdrotValue){.type = STDROT_INT, .val = {.i = (int)i}};
        }
    }
    return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
}

static const StdrotParam yapidx_params[] = {
    {STDROT_STRING, NULL, 0},
    {STDROT_STRING, NULL, 0},
};
STDROT_EXPORT_SIG("yapidx", stdrot_yapidx, ((StdrotParam){STDROT_INT, NULL, 0}),
                  yapidx_params, 2, 2, false);
