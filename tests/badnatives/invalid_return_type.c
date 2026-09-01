/* Round-14 review, finding #3: validate_native_registry() must range-
 * check StdrotType itself, not just the individual pointer_level/type-
 * pair fields -- a hand-written descriptor with an out-of-range value
 * (e.g. cast from an int past STDROT_NONE, the last real value) shouldn't
 * graduate from registry validation and become somebody else's problem
 * later (a switch with no matching case, an array index out of bounds in
 * a lookup table keyed by StdrotType, etc). */
#include "stdrot_api.h"

static StdrotValue stdrot_bad_return_type(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){STDROT_INT, {0}};
}

STDROT_EXPORT_SIG("bad_invalid_return_type", stdrot_bad_return_type,
                  ((StdrotParam){(StdrotType)999, NULL, 0}), NULL, 0, 0, false);
