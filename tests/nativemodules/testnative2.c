/* tests/nativemodules/testnative2.c – A SECOND, independent native module
 * fixture exporting a DIFFERENT, non-colliding name ("halved") from
 * testnative.c's ("tripled"). Exists specifically to prove two distinct
 * cooked ".so" modules can be loaded simultaneously and both remain
 * independently callable -- #207's own "two modules loaded at once" DoD
 * item, which testnative_dup_module.c (deliberately colliding, load must
 * fail) and the core-plus-one-module dual-load test don't exercise: this
 * is the one where BOTH loads succeed and BOTH exports work.
 */
#include "stdrot_api.h"

static StdrotValue native_halved(StdrotValue *args, int argc)
{
    (void)argc;
    return (StdrotValue){.type = STDROT_INT, .val = {.i = args[0].val.i / 2}};
}

static const StdrotParam halved_params[] = {
    {STDROT_INT, NULL, 0},
};
STDROT_EXPORT_SIG("halved", native_halved, ((StdrotParam){STDROT_INT, NULL, 0}),
                  halved_params, 1, 1, false);
