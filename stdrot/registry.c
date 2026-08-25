/* stdrot/registry.c – Self-registration collector for libstdrot.so
 *
 * Uses linker section magic to auto-collect all STDROT_EXPORT() entries
 * from individual source files. Each .c file self-registers via the macro
 * at the end of the file.
 */

#include "stdrot_api.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <mach-o/getsect.h>
#include <mach-o/loader.h>

#if defined(__LP64__)
extern const struct mach_header_64 _mh_dylib_header;
#else
extern const struct mach_header _mh_dylib_header;
#endif
#else
/* Linker provides these symbols marking the start/end of the section. The
 * section holds StdrotEntry pointers (see STDROT_EXPORT_SIG in
 * stdrot_api.h), so these are themselves pointer-typed slots -- taking
 * their address gives the start/end of an array of StdrotEntry *.
 * __start_SECTION/__stop_SECTION are the mandated linker-provided names for
 * this idiom (ld(1)) -- not renameable, so the reserved-identifier warning
 * doesn't apply here. */
// NOLINTBEGIN(bugprone-reserved-identifier)
extern StdrotEntry *__start_stdrot_exports;
extern StdrotEntry *__stop_stdrot_exports;
// NOLINTEND(bugprone-reserved-identifier)
#endif

/* Entry point called by stdrot.c after dlopen() -- named/numbered for
 * STDROT_ABI_VERSION (stdrot_api.h): renaming this alongside a real ABI
 * layout change means an old .so's stdrot_get_api() (the pre-v2 name)
 * is simply absent from a new host's dlsym() lookup, rather than being
 * called and returning a StdrotAPI populated from a completely
 * different memory layout. See that macro's own comment for the full
 * reasoning. */
StdrotAPI stdrot_get_api_v2(void)
{
#if defined(__APPLE__) && defined(__MACH__)
    unsigned long section_byte_len = 0;
    const StdrotEntry *const *functions =
        (const StdrotEntry *const *)getsectiondata(
            &_mh_dylib_header, STDROT_EXPORT_SEGMENT_NAME,
            STDROT_EXPORT_SECTION_NAME, &section_byte_len);
    ptrdiff_t byte_len = (ptrdiff_t)section_byte_len;
    if (!functions || byte_len == 0)
    {
        fprintf(stderr,
                "stdrot: native function registry section %s,%s was not "
                "found in libstdrot.so\n",
                STDROT_EXPORT_SEGMENT_NAME, STDROT_EXPORT_SECTION_NAME);
        exit(1);
    }
#else
    /* Every slot in the section is exactly sizeof(StdrotEntry *) --
       there's no variable-sized-struct alignment padding to worry about
       here, unlike when the section held StdrotEntry values directly (see
       git history: a compiler heuristic that pads the storage alignment
       of "large" static objects once silently corrupted that scheme's
       entry count). The modulus check is now just a sanity check against
       a genuinely unexpected toolchain, not a load-bearing tripwire. */
    ptrdiff_t byte_len =
        (char *)&__stop_stdrot_exports - (char *)&__start_stdrot_exports;
    const StdrotEntry *const *functions =
        (const StdrotEntry *const *)&__start_stdrot_exports;
#endif
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
    api.functions = functions;
    api.count = (int)(byte_len / (ptrdiff_t)sizeof(StdrotEntry *));
    return api;
}
