/* stdrot/registry.c – Self-registration collector for libstdrot.so
 *
 * Uses linker section magic to auto-collect all STDROT_EXPORT() entries
 * from individual source files. Each .c file self-registers via the macro
 * at the end of the file.
 */

#include "stdrot_api.h"
#include <stdio.h>
#include <stdlib.h>

/* Linker provides these symbols marking the start/end of the section */
extern StdrotEntry __start_stdrot_exports;
extern StdrotEntry __stop_stdrot_exports;

/* Entry point called by stdrot.c after dlopen() */
StdrotAPI stdrot_get_api(void)
{
    /* (stop-start)/sizeof(StdrotEntry) only gives the right count if the
       section is packed with zero gaps between entries -- true as long as
       every STDROT_EXPORT_SIG/STDROT_EXPORT instance carries the explicit
       aligned(_Alignof(StdrotEntry)) attribute (see stdrot_api.h). Without
       it, the compiler's large-static-object alignment heuristic silently
       pads entries and this count goes wrong, corrupting the whole
       registry -- catch that immediately instead of scanning garbage. */
    ptrdiff_t byte_len =
        (char *)&__stop_stdrot_exports - (char *)&__start_stdrot_exports;
    if (byte_len % (ptrdiff_t)sizeof(StdrotEntry) != 0)
    {
        fprintf(stderr,
                "stdrot: corrupt native function registry (stdrot_exports "
                "section is %td bytes, not a multiple of sizeof(StdrotEntry) "
                "= %zu)\n",
                byte_len, sizeof(StdrotEntry));
        exit(1);
    }

    StdrotAPI api;
    api.functions = &__start_stdrot_exports;
    api.count = (int)(byte_len / (ptrdiff_t)sizeof(StdrotEntry));
    return api;
}
