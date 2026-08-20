/* Round-13 review, finding #2: a parameter descriptor whose type is
 * STDROT_NONE ("void return", per stdrot_api.h -- only meaningful as a
 * RETURN type). A parameter that "consumes void" can't coherently accept
 * an actual argument; zero arguments are already expressed via
 * param_count == 0. validate_native_registry() (stdrot.c) must reject
 * this at load time. */
#include "stdrot_api.h"

static StdrotValue stdrot_bad_none_param(StdrotValue *args, int argc)
{
    (void)argc;
    return args[0];
}

static const StdrotParam bad_none_param_params[] = {
    {STDROT_NONE, NULL, 0},
};
STDROT_EXPORT_SIG("bad_none_typed_param", stdrot_bad_none_param,
                  ((StdrotParam){STDROT_INT, NULL, 0}), bad_none_param_params,
                  1, 1, false);
