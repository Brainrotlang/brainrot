/* Round-14 review, finding #3: same as invalid_return_type.c but for a
 * parameter's type instead of the return type -- validate_native_
 * registry() must range-check both. */
#include "stdrot_api.h"

static StdrotValue stdrot_bad_param_type(StdrotValue *args, int argc)
{
    (void)argc;
    return args[0];
}

static const StdrotParam bad_param_type_params[] = {
    {(StdrotType)999, NULL, 0},
};
STDROT_EXPORT_SIG("bad_invalid_param_type", stdrot_bad_param_type,
                  ((StdrotParam){STDROT_INT, NULL, 0}), bad_param_type_params,
                  1, 1, false);
