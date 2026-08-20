/* tests/stdrot/testabi.c – Deliberately lying native bindings, committed
 * as permanent test-only infrastructure for execute_native_call()'s
 * return-type enforcement (enforce_return_type() in stdrot.c).
 *
 * Every native below declares one return representation in its
 * StdrotEntry and then actually constructs a StdrotValue tagged
 * something else -- the exact class of native-binding bug
 * enforce_return_type() exists to catch instead of letting it corrupt
 * whatever reads the result downstream. See tests/stdrot/testptr.c's
 * comment for why this lives outside stdrot/ rather than being a real,
 * permanently-shipped Brainrot builtin.
 */
#include "stdrot_api.h"

/* Declares DOUBLE, actually returns INT -- numerically coercible, so
 * enforce_return_type() should silently convert 42 -> 42.0 rather than
 * treating this as a violation. */
static StdrotValue stdrot_lying_double(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    StdrotValue out = {STDROT_INT, {0}};
    out.val.i = 42;
    return out;
}

STDROT_EXPORT_SIG("lying_double", stdrot_lying_double,
                  ((StdrotParam){STDROT_DOUBLE, NULL, 0}), NULL, 0, 0, false);

/* Declares BOOL, actually returns STRING -- genuinely incompatible (not
 * in the numeric-coercion group), so enforce_return_type() must exit(1)
 * rather than let a scalar evaluator read a bool out of a String's
 * backing bytes. */
static StdrotValue stdrot_lying_bool(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    StdrotValue out = {STDROT_STRING, {0}};
    out.val.str = STRING_LITERAL("oops");
    return out;
}

STDROT_EXPORT_SIG("lying_bool", stdrot_lying_bool,
                  ((StdrotParam){STDROT_BOOL, NULL, 0}), NULL, 0, 0, false);

/* Declares a STDROT_PTR return, actually returns a plain STDROT_INT --
 * genuinely incompatible (STDROT_PTR isn't in coerce_numeric()'s group
 * either), so this must exit(1) too, not silently box a non-pointer
 * value as an address. */
static StdrotValue stdrot_lying_ptr_return(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    StdrotValue out = {STDROT_INT, {0}};
    out.val.i = 7;
    return out;
}

STDROT_EXPORT_SIG("lying_ptr_return", stdrot_lying_ptr_return,
                  ((StdrotParam){STDROT_PTR, NULL, 0}), NULL, 0, 0, false);

/* Legacy/untyped export (return_type.type == STDROT_ANY, the "unchecked,
 * arity/types not validated" fallback -- see STDROT_EXPORT's own comment
 * in stdrot_api.h) whose C implementation returns a real STDROT_PTR.
 * STDROT_ANY documents that pointer-valued results are not covered by
 * "any type, unchecked" -- enforce_return_type() must reject this at the
 * runtime boundary, since the semantic analyzer has no way to catch it
 * statically (the descriptor says ANY, not PTR). */
static int legacy_ptr_leak_backing = 0;

static StdrotValue stdrot_legacy_ptr_leak(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    StdrotValue out = {STDROT_PTR, {0}};
    out.val.ptr = &legacy_ptr_leak_backing;
    return out;
}

STDROT_EXPORT("legacy_ptr_leak", stdrot_legacy_ptr_leak);

/* Declares a STDROT_CSTRING return -- semantic_check_native_call()
 * (semantic_analyzer.c) must reject any call to this outright, since
 * marshal_native_return_value() (ast.c) has no code path that actually
 * constructs a Brainrot String from a returned const char*. The
 * implementation is never reached by a Brainrot program (the call itself
 * is a semantic error), so what it returns doesn't matter -- STDROT_NONE
 * documents that. */
static StdrotValue stdrot_cstring_return(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){STDROT_NONE, {0}};
}

STDROT_EXPORT_SIG("cstring_return", stdrot_cstring_return,
                  ((StdrotParam){STDROT_CSTRING, NULL, 0}), NULL, 0, 0, false);
