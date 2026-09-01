/* stdrot/yapcmp.c -- yapcmp(rant a, rant b) -> rizz, lexicographic compare.
 *
 * Returns -1, 0 or 1 -- NORMALIZED, not a raw memcmp() delta. memcmp's
 * magnitude is unspecified beyond its sign, so returning it directly would
 * make `yapcmp(a, b) == -1` work on one libc and not another. Callers get
 * the three values the contract promises.
 *
 * Length-aware, and that is the reason this is not strcmp(): a `rant` is
 * length-prefixed and NOT NUL-terminated (lib/string_value.h), so strcmp()
 * would read past the buffer and would also stop at an embedded NUL,
 * declaring "a\0b" and "a\0c" equal. This compares min(a.len, b.len) bytes
 * and then breaks the tie on length, so a prefix sorts before what extends
 * it -- "app" < "apple" -- exactly as C's strcmp does for NUL-terminated
 * strings and as Go's strings.Compare does for byte slices.
 *
 * Bytes are compared as UNSIGNED char, which is memcmp's own rule and the
 * reason it is used here rather than a hand-rolled loop over `char`: plain
 * char is signed on x86-64, so comparing it directly would sort any byte
 * >= 0x80 (every non-ASCII UTF-8 byte) BELOW every ASCII one.
 *
 * The declared StdrotParams are enforced by enforce_arg_type() (stdrot.c)
 * before fn() is entered, so there is no argument-type check here.
 */
#include "stdrot_api.h"
#include <string.h>

StdrotValue stdrot_yapcmp(StdrotValue *args, int argc)
{
    (void)argc;
    const char *ad = args[0].val.str.data;
    const char *bd = args[1].val.str.data;
    /* .data == NULL is representable by the String struct; treat such a
       value as empty rather than pairing a null buffer with a length. */
    size_t alen = ad ? args[0].val.str.len : 0;
    size_t blen = bd ? args[1].val.str.len : 0;

    size_t common = alen < blen ? alen : blen;
    /* memcmp with a NULL pointer is undefined even when n == 0, so the
       common == 0 case is skipped rather than relying on it being a
       harmless no-op. */
    int diff = common > 0 ? memcmp(ad, bd, common) : 0;
    if (diff != 0)
    {
        return (StdrotValue){.type = STDROT_INT,
                             .val = {.i = diff < 0 ? -1 : 1}};
    }

    int result = 0;
    if (alen < blen)
        result = -1;
    else if (alen > blen)
        result = 1;
    return (StdrotValue){.type = STDROT_INT, .val = {.i = result}};
}

static const StdrotParam yapcmp_params[] = {
    {STDROT_STRING, NULL, 0},
    {STDROT_STRING, NULL, 0},
};
STDROT_EXPORT_SIG("yapcmp", stdrot_yapcmp, ((StdrotParam){STDROT_INT, NULL, 0}),
                  yapcmp_params, 2, 2, false);
