/* Round-12 review, finding #4: two entries exported under the same
 * Brainrot-visible name. get_native_function() (stdrot.c) just returns the
 * first linker-section match, so without load-time validation one binding
 * would silently "win" purely by registration order -- for an ABI
 * explicitly designed to feed generated bindings, a silent collision like
 * that is not acceptable. */
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
STDROT_EXPORT_SIG("bad_duplicate", stdrot_dup_one,
                  ((StdrotParam){STDROT_INT, NULL, 0}), dup_params, 1, 1,
                  false);
STDROT_EXPORT_SIG("bad_duplicate", stdrot_dup_two,
                  ((StdrotParam){STDROT_INT, NULL, 0}), dup_params, 1, 1,
                  false);
