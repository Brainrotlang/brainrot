/* Round-12 review, finding #1 ("Exhibit A"): an identity-polymorphic
 * descriptor (return_like_arg >= 0) whose referenced parameter is a fixed
 * type (here STDROT_CSTRING) rather than STDROT_ANY. Static inference would
 * report the *source* expression's type (e.g. STRING, before coercion);
 * runtime enforcement sees the argument *after* parameter coercion to
 * CSTRING -- two different types for the same call, and CSTRING return
 * marshalling was never implemented on top of that. validate_native_registry()
 * (stdrot.c) must reject this at load time, before any of that can happen. */
#include "stdrot_api.h"

static StdrotValue stdrot_bad_identity(StdrotValue *args, int argc)
{
    (void)argc;
    return args[0];
}

static const StdrotParam bad_identity_params[] = {
    {STDROT_CSTRING, NULL, 0},
};
STDROT_EXPORT_SIG_IDENTITY("bad_identity_non_any", stdrot_bad_identity,
                           bad_identity_params, 1, 1);
