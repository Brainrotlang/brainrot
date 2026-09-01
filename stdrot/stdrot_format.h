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

/* Renders `format` against `arg_count` StdrotValue arguments and writes the
 * result to `out`, appending a newline when `add_newline` is nonzero, then
 * flushes. Shared by yapping/yappin (stdout) and yapto (a file). */
void stdrot_format_to_stream(FILE *out, const char *format,
                             const StdrotValue *args, int arg_count,
                             int add_newline);

#endif
