/* stdrot/stdrot_format.h -- libstdrot-internal formatting helper.
 *
 * Deliberately NOT part of stdrot_api.h. That header is the external ABI
 * contract every native module compiles against; this is one shared
 * implementation detail between two files inside libstdrot.so, and putting
 * it in the ABI header would publish it as something third-party modules
 * may rely on and this project must keep stable.
 */
#ifndef STDROT_FORMAT_H
#define STDROT_FORMAT_H

#include "stdrot_api.h"
#include <stdio.h>
#include <string.h>

/* Rewrites a printf integer conversion specifier so it prints a 64-bit
 * (STDROT_LONG / giga / thicc) value correctly regardless of the length
 * modifier the source wrote. It drops any existing length modifier (h/l/j/z/
 * t/L) and forces `ll`, so "%d", "%ld" and "%lld" all become "%lld" (flags,
 * width and precision are preserved). Without this, a giga printed with a bare
 * "%d" would hand snprintf a 64-bit value under an int conversion -- undefined
 * behavior (#282). `spec` is the conversion char (d/i/o/u/x/X); `out` must hold
 * at least strlen(specifier)+3 bytes. */
static inline void stdrot_ll_specifier(const char *specifier, char spec,
                                       char *out, size_t outsz)
{
    size_t n = strlen(specifier);
    size_t o = 0;
    /* Copy every char except the trailing conversion char, dropping length
       modifiers so only the forced `ll` remains. */
    for (size_t i = 0; i + 1 < n && o + 4 < outsz; i++)
    {
        char c = specifier[i];
        if (c == 'h' || c == 'l' || c == 'j' || c == 'z' || c == 't' ||
            c == 'L')
            continue;
        out[o++] = c;
    }
    if (o + 3 < outsz)
    {
        out[o++] = 'l';
        out[o++] = 'l';
        out[o++] = spec;
    }
    out[o] = '\0';
}

/* Renders `format` against `arg_count` StdrotValue arguments and writes the
 * result to `out`, appending a newline when `add_newline` is nonzero, then
 * flushes. Shared by yapping/yappin (stdout) and yapto (a file). */
void stdrot_format_to_stream(FILE *out, const char *format,
                             const StdrotValue *args, int arg_count,
                             int add_newline);

#endif
