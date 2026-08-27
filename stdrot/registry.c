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

/* STDROT_REGISTRY_ENTRYPOINT lets this same linker-section-collecting body
 * serve two distinct roles under two distinct exported names, so that a
 * cooked native module built with it (tests/nativemodules/*.c,
 * -DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init in the Makefile) NEVER
 * defines a symbol literally named stdrot_get_api_v2 at all:
 *
 *   - Undefined (the core libstdrot.so build): this is stdrot_get_api_v2(),
 *     dlsym'd by stdrot_load() (stdrot.c).
 *   - Defined as brainrot_module_init (a cooked module's own build): this
 *     is brainrot_module_init(), dlsym'd by stdrot_load_module() (stdrot.c).
 *
 * That is load-bearing, not cosmetic, but NOT because brainrot_module_init
 * is a process-wide-unique name -- it isn't: every cooked module is built
 * with this same override, so two loaded modules both export a global
 * symbol named brainrot_module_init, and that is fine. What actually
 * matters is HOW each one gets called: stdrot_load_module() (stdrot.c)
 * calls dlsym(handle, "brainrot_module_init") against that specific
 * module's own dlopen() handle, which searches that object (and its own
 * dependencies) directly -- never the process-wide global symbol scope --
 * so which OTHER object also happens to export that name is irrelevant.
 * Contrast that with the bug this rename fixes: a module built as this
 * same registry.c PLUS a separate small wrapper that CALLS
 * stdrot_get_api_v2() BY ORDINARY NAME from inside the module's own code
 * is a normal global-scope-resolved symbol reference, not a dlsym-by-
 * handle lookup -- and since the core library's own same-named
 * stdrot_get_api_v2 was already loaded into that scope first (see
 * stdrot_load_module()'s own comment on why cooked modules use
 * RTLD_LOCAL specifically to keep this from ever mattering), the dynamic
 * linker's "first definition in load order wins" interposition rule
 * would silently redirect that internal call to the CORE's copy instead
 * of the module's own, handing back the core library's entire function
 * table under the guise of loading the module. Making the EXPORTED
 * entrypoint itself do the collection -- no internal cross-symbol call at
 * all -- removes that specific hazard by construction; RTLD_LOCAL
 * (stdrot_load_module()) independently ensures a module's exports never
 * reach the global scope to begin with, so this is defense in depth, not
 * a single point of correctness. */
#ifndef STDROT_REGISTRY_ENTRYPOINT
#define STDROT_REGISTRY_ENTRYPOINT stdrot_get_api_v2
#endif

/* Named/numbered for STDROT_ABI_VERSION (stdrot_api.h) in its default
 * (core-library) role: renaming this alongside a real ABI layout change
 * means an old .so's stdrot_get_api() (the pre-v2 name) is simply absent
 * from a new host's dlsym() lookup, rather than being called and
 * returning a StdrotAPI populated from a completely different memory
 * layout. See that macro's own comment for the full reasoning. */
StdrotAPI STDROT_REGISTRY_ENTRYPOINT(void)
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
