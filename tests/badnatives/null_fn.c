/* Round-12 review, finding #4: a descriptor with .fn == NULL. Nothing
 * upstream of the actual call site ever dereferences entry->fn, so without
 * load-time validation this would crash (or silently do nothing useful)
 * the first time a Brainrot program actually calls it, instead of failing
 * immediately at load. */
#include "stdrot_api.h"
#include <stddef.h>

static const StdrotParam null_fn_params[] = {
    {STDROT_INT, NULL, 0},
};
STDROT_EXPORT_SIG("bad_null_fn", NULL, ((StdrotParam){STDROT_INT, NULL, 0}),
                  null_fn_params, 1, 1, false);
