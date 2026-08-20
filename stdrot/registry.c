/* stdrot/registry.c – Self-registration collector for libstdrot.so
 *
 * Uses linker section magic to auto-collect all STDROT_EXPORT() entries
 * from individual source files. Each .c file self-registers via the macro
 * at the end of the file.
 */

#include "stdrot_api.h"
#include <stdio.h>
#include <stdlib.h>

/* Linker provides these symbols marking the start/end of the section. The
 * section holds StdrotEntry pointers (see STDROT_EXPORT_SIG in
 * stdrot_api.h), so these are themselves pointer-typed slots -- taking
 * their address gives the start/end of an array of StdrotEntry *. */
extern StdrotEntry *__start_stdrot_exports;
extern StdrotEntry *__stop_stdrot_exports;

/* Entry point called by stdrot.c after dlopen() -- named/numbered for
 * STDROT_ABI_VERSION (stdrot_api.h): renaming this alongside a real ABI
 * layout change means an old .so's stdrot_get_api() (the pre-v2 name)
 * is simply absent from a new host's dlsym() lookup, rather than being
 * called and returning a StdrotAPI populated from a completely
 * different memory layout. See that macro's own comment for the full
 * reasoning. */
StdrotAPI stdrot_get_api_v2(void)
{
    /* Every slot in the section is exactly sizeof(StdrotEntry *) --
       there's no variable-sized-struct alignment padding to worry about
       here, unlike when the section held StdrotEntry values directly (see
       git history: a compiler heuristic that pads the storage alignment
       of "large" static objects once silently corrupted that scheme's
       entry count). The modulus check is now just a sanity check against
       a genuinely unexpected toolchain, not a load-bearing tripwire. */
    ptrdiff_t byte_len =
        (char *)&__stop_stdrot_exports - (char *)&__start_stdrot_exports;
    if (byte_len % (ptrdiff_t)sizeof(StdrotEntry *) != 0)
    {
        fprintf(stderr,
                "stdrot: corrupt native function registry (stdrot_exports "
                "section is %td bytes, not a multiple of sizeof(StdrotEntry "
                "*) = %zu)\n",
                byte_len, sizeof(StdrotEntry *));
        exit(1);
    }

    StdrotAPI api;
    api.functions = (const StdrotEntry *const *)&__start_stdrot_exports;
    api.count = (int)(byte_len / (ptrdiff_t)sizeof(StdrotEntry *));
    return api;
}
