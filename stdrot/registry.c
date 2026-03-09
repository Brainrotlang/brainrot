/* stdrot/registry.c – Self-registration collector for libstdrot.so
 *
 * Uses linker section magic to auto-collect all STDROT_EXPORT() entries
 * from individual source files. Each .c file self-registers via the macro
 * at the end of the file.
 */

#include "stdrot_api.h"

/* Linker provides these symbols marking the start/end of the section */
extern StdrotEntry __start_stdrot_exports;
extern StdrotEntry __stop_stdrot_exports;

/* Entry point called by stdrot.c after dlopen() */
StdrotAPI stdrot_get_api(void)
{
    StdrotAPI api;
    api.functions = &__start_stdrot_exports;
    api.count = (int)(&__stop_stdrot_exports - &__start_stdrot_exports);
    return api;
}
