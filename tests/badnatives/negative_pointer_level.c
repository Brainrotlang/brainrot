/* Round-12 review, finding #4: a parameter descriptor with a negative
 * pointer_level. StdrotParam's own comment documents pointer_level as a
 * non-negative depth (0 = the base type itself, N = N levels of STDROT_PTR
 * on top) -- a negative value has no meaning downstream (semantic_
 * analyzer.c's pointer-depth checks, coerce_arg_to_param()'s pointer-vs-
 * scalar branch) and must never reach either. */
#include "stdrot_api.h"

static StdrotValue stdrot_bad_ptr(StdrotValue *args, int argc)
{
    (void)argc;
    return args[0];
}

static const StdrotParam bad_ptr_params[] = {
    {STDROT_INT, NULL, -1},
};
STDROT_EXPORT_SIG("bad_negative_pointer_level", stdrot_bad_ptr,
                  ((StdrotParam){STDROT_INT, NULL, 0}), bad_ptr_params, 1, 1,
                  false);
