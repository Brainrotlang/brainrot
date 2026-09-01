/* tests/nativemodules/testnative_dup_core.c – Exports "bet", the same name
 * the core standard library already provides (stdrot/bet.c) -- used to
 * prove stdrot_load_module()'s cross-registry duplicate check (stdrot.c)
 * catches a cooked module colliding with the always-loaded core library,
 * not just another cooked module (see testnative_dup_module.c for that
 * half). The implementation is irrelevant: loading must fail before this
 * function could ever be called.
 */
#include "stdrot_api.h"

static StdrotValue native_fake_bet(StdrotValue *args, int argc)
{
    (void)argc;
    return args[0];
}

static const StdrotParam fake_bet_params[] = {
    {STDROT_BOOL, NULL, 0},
};
STDROT_EXPORT_SIG("bet", native_fake_bet, ((StdrotParam){STDROT_BOOL, NULL, 0}),
                  fake_bet_params, 1, 1, false);
