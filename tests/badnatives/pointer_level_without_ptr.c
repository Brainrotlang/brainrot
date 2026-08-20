/* Round-13 review, finding #2: a parameter descriptor whose pointer_level
 * is nonzero but whose type isn't STDROT_PTR. pointer_level is only
 * meaningful stacked on top of STDROT_PTR (StdrotParam's own comment,
 * stdrot_api.h) -- {STDROT_INT, NULL, 1} is an internally contradictory
 * descriptor: static checking would approve it as an ordinary pointer to
 * int, but ast_expr_to_stdrot_value() tags ANY pointer_level > 0
 * expression STDROT_PTR regardless of declared base type, so the argument
 * always arrives tagged STDROT_PTR while the descriptor insists it's
 * STDROT_INT. validate_native_registry() (stdrot.c) must reject this at
 * load time. */
#include "stdrot_api.h"

static StdrotValue stdrot_bad_ptr_level(StdrotValue *args, int argc)
{
    (void)argc;
    return args[0];
}

static const StdrotParam bad_ptr_level_params[] = {
    {STDROT_INT, NULL, 1},
};
STDROT_EXPORT_SIG("bad_pointer_level_without_ptr", stdrot_bad_ptr_level,
                  ((StdrotParam){STDROT_INT, NULL, 0}), bad_ptr_level_params, 1,
                  1, false);
