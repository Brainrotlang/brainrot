/* A STDROT_STRUCT parameter descriptor with no type_name (issue #208,
 * Phase 5 Road B). Unlike every other StdrotType, a by-value aggregate
 * carries no self-describing type: `gang Vector2 { chad x, y; }` and
 * `gang Size { chad w, h; }` are both 8 bytes with identical alignment,
 * so a descriptor that names no tag leaves both static checking
 * (semantic_check_native_call()) and enforce_arg_type() with nothing to
 * compare -- the native would silently accept whichever same-sized struct
 * the caller happened to pass, and memcpy its bytes into an unrelated C
 * type. validate_native_registry() (stdrot.c) must reject this at load
 * time. */
#include "stdrot_api.h"

static StdrotValue stdrot_bad_struct_param(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){.type = STDROT_INT, .val = {.i = 0}};
}

static const StdrotParam bad_struct_param_params[] = {
    {STDROT_STRUCT, NULL, 0},
};
STDROT_EXPORT_SIG("bad_struct_param_without_type_name", stdrot_bad_struct_param,
                  ((StdrotParam){STDROT_INT, NULL, 0}), bad_struct_param_params,
                  1, 1, false);
