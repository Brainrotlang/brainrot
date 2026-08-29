/* tests/nativemodules/no_module_init.c – A structurally valid native .so
 * that exports stdrot_get_api_v3() (via registry.c, same as every other
 * fixture in this directory) but deliberately has NO brainrot_module_init_v3()
 * of its own -- proves stdrot_load_module() (stdrot.c) fails loudly on a
 * missing entrypoint instead of silently loading nothing, the same
 * dlsym-failure posture stdrot_load() already has for a pre-ABI-versioning
 * libstdrot.so (see tests/old_abi_sim/ own file comment).
 */
#include "stdrot_api.h"

static StdrotValue native_noop(StdrotValue *args, int argc)
{
    (void)argc;
    return args[0];
}

static const StdrotParam noop_params[] = {
    {STDROT_INT, NULL, 0},
};
STDROT_EXPORT_SIG("no_module_init_noop", native_noop,
                  ((StdrotParam){STDROT_INT, NULL, 0}), noop_params, 1, 1,
                  false);
