/* tests/nativemodules/testnative_dup_module.c – Exports "tripled" too, the
 * same name as tests/nativemodules/testnative.c -- used to prove
 * stdrot_load_module()'s cross-registry duplicate check (stdrot.c) catches
 * a name collision between two DIFFERENT cooked modules, not just within
 * one module's own table (validate_native_registry() already covers
 * that half). The implementation differs on purpose (quadruples instead
 * of triples): if this module's export ever silently won over the
 * earlier one, a test would see the wrong arithmetic instead of the load
 * failure it's supposed to get.
 */
#include "stdrot_api.h"

static StdrotValue native_quadrupled(StdrotValue *args, int argc)
{
    (void)argc;
    return (StdrotValue){.type = STDROT_INT, .val = {.i = args[0].val.i * 4}};
}

static const StdrotParam tripled_params[] = {
    {STDROT_INT, NULL, 0},
};
STDROT_EXPORT_SIG("tripled", native_quadrupled,
                  ((StdrotParam){STDROT_INT, NULL, 0}), tripled_params, 1, 1,
                  false);
