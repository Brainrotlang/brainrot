/* stdrot/yaplen.c -- yaplen(rant s) -> rizz, string length in BYTES.
 *
 * Byte semantics, not runes: a Brainrot `rant` is the length-prefixed
 * String { char *data; size_t len; } from lib/string_value.h, and every
 * string operation in this v1 (#251) measures and indexes it as a flat
 * byte buffer, matching C and Go's []byte view. UTF-8/codepoint-aware
 * variants are explicitly out of scope for v1.
 *
 * Reads String.len rather than calling strlen(): a `rant` is NOT
 * NUL-terminated, so strlen() would run past the buffer for a string
 * built by any path that does not happen to append one, and would stop
 * early on a string containing an embedded NUL. The length prefix is the
 * only correct source.
 *
 * No defensive check that args[0] really is a string: the declared
 * StdrotParam below is enforced by enforce_arg_type() (stdrot.c) before
 * fn() is entered, and that boundary exists precisely so that "a native
 * wrapper is entitled to trust its own StdrotParam blindly" -- re-checking
 * here would be unreachable code that no test could ever cover.
 */
#include "stdrot_api.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

StdrotValue stdrot_yaplen(StdrotValue *args, int argc)
{
    (void)argc;
    /* A String with .data == NULL is representable by the struct even
       though no path here was found to produce one, so it is treated as
       empty rather than trusting .len alongside a null buffer. */
    size_t len = args[0].val.str.data ? args[0].val.str.len : 0;

    /* The ABI's integer carrier is `int` (STDROT_INT), so a string longer
       than INT_MAX has no representable length. Refusing is the honest
       answer: silently truncating would report a negative or wrapped
       length that every caller would then use as a bound. Unreachable in
       practice, checked anyway because the cost is one comparison. */
    if (len > (size_t)INT_MAX)
    {
        fprintf(stderr,
                "Error: yaplen: string length %zu exceeds the largest "
                "representable rizz at line %d\n",
                len, g_exec_context.line_number);
        exit(1);
    }

    return (StdrotValue){.type = STDROT_INT, .val = {.i = (int)len}};
}

static const StdrotParam yaplen_params[] = {
    {STDROT_STRING, NULL, 0},
};
STDROT_EXPORT_SIG("yaplen", stdrot_yaplen, ((StdrotParam){STDROT_INT, NULL, 0}),
                  yaplen_params, 1, 1, false);
