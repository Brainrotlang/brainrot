/* tests/nativemodules/testnative.c – Minimal, well-formed native module
 * fixture for #cooked <name> resolving to a native ".so"
 * (module_path.h's MODULE_ARTIFACT_NATIVE), built into
 * tests/nativemodules/testnative.so by the Makefile. Exercises the real
 * brainrot_module_init() entrypoint end to end -- exports one ordinary
 * function so a test can prove it's actually callable after being cooked,
 * not just that loading didn't crash. Linked with stdrot/registry.c (like
 * tests/badnatives/*.c), whose linker-section collection already gathers
 * this file's STDROT_EXPORT_SIG() below -- built with
 * -DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init (Makefile,
 * registry.c's own comment) so that collection is exported directly as
 * brainrot_module_init(), the name stdrot_load_module() (stdrot.c) looks
 * for.
 */
#include "stdrot_api.h"

static StdrotValue native_tripled(StdrotValue *args, int argc)
{
    (void)argc;
    return (StdrotValue){.type = STDROT_INT, .val = {.i = args[0].val.i * 3}};
}

static const StdrotParam tripled_params[] = {
    {STDROT_INT, NULL, 0},
};
STDROT_EXPORT_SIG("tripled", native_tripled,
                  ((StdrotParam){STDROT_INT, NULL, 0}), tripled_params, 1, 1,
                  false);
