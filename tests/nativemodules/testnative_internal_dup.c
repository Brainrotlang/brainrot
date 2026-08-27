/* tests/nativemodules/testnative_internal_dup.c – Two entries exported
 * under the same name WITHIN one module's own table -- proves
 * stdrot_load_module() (stdrot.c) actually runs validate_native_registry()
 * on a cooked module's table (rejecting it exactly like it already rejects
 * a malformed core libstdrot.so, see tests/badnatives/duplicate_name.c),
 * not just trusting whatever brainrot_module_init() hands back.
 */
#include "stdrot_api.h"

static StdrotValue stdrot_dup_one(StdrotValue *args, int argc)
{
    (void)argc;
    return args[0];
}

static StdrotValue stdrot_dup_two(StdrotValue *args, int argc)
{
    (void)argc;
    return args[0];
}

static const StdrotParam dup_params[] = {
    {STDROT_INT, NULL, 0},
};
STDROT_EXPORT_SIG("dup_within_module", stdrot_dup_one,
                  ((StdrotParam){STDROT_INT, NULL, 0}), dup_params, 1, 1,
                  false);
STDROT_EXPORT_SIG("dup_within_module", stdrot_dup_two,
                  ((StdrotParam){STDROT_INT, NULL, 0}), dup_params, 1, 1,
                  false);
