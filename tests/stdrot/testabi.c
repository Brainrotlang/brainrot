/* tests/stdrot/testabi.c – Permanent test-only native bindings for
 * execute_native_call()'s ABI enforcement (stdrot.c) and the static
 * checks in semantic_check_native_call() (semantic_analyzer.c) that are
 * supposed to agree with it.
 *
 * Most natives below deliberately lie: each declares one return
 * representation in its StdrotEntry and then actually constructs a
 * StdrotValue tagged something else -- the exact class of native-
 * binding bug enforce_return_type()/enforce_arg_type() exist to catch
 * instead of letting it corrupt whatever reads the result downstream.
 * A few (e.g. takes_cstring) are perfectly honest natives that exist
 * only to exercise a representation the ABI is supposed to accept.
 * See tests/stdrot/testptr.c's comment for why this file lives outside
 * stdrot/ rather than being a real, permanently-shipped Brainrot
 * builtin.
 */
#include "stdrot_api.h"
#include <string.h>

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

/* Legacy/untyped export whose actual runtime result is a plain
 * STDROT_INT -- non-pointer, so enforce_return_type() lets it through
 * unconditionally (correctly: STDROT_ANY genuinely doesn't know the
 * type ahead of time). What consumes this result must still not
 * reinterpret the boxed int as whatever wider/narrower C type the
 * calling context happens to expect. */
static StdrotValue stdrot_legacy_int(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    StdrotValue out = {STDROT_INT, {0}};
    out.val.i = 42;
    return out;
}

STDROT_EXPORT("legacy_int", stdrot_legacy_int);

/* Legacy/untyped export whose actual runtime result is a STDROT_STRING
 * -- not numerically coercible at all, so a numeric evaluator consuming
 * this must reject it outright rather than reinterpret the String*
 * box's bytes as a number. */
static StdrotValue stdrot_legacy_string(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    StdrotValue out = {STDROT_STRING, {0}};
    out.val.str = STRING_LITERAL("nope");
    return out;
}

STDROT_EXPORT("legacy_string", stdrot_legacy_string);

/* Legacy/untyped export whose actual runtime result is a STDROT_CSTRING
 * -- must be rejected by enforce_return_type()'s STDROT_ANY branch the
 * same way STDROT_PTR/STDROT_HANDLE already are, since marshal_native_
 * return_value() has no code path that constructs a Brainrot String
 * from one (see the STDROT_CSTRING-as-declared-return-type rejection in
 * semantic_check_native_call() for the identical reasoning). */
static StdrotValue stdrot_legacy_cstring(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    StdrotValue out = {STDROT_CSTRING, {0}};
    out.val.cstr = "hello";
    return out;
}

STDROT_EXPORT("legacy_cstring", stdrot_legacy_cstring);

/* Legacy/untyped export whose actual runtime result is STDROT_NONE
 * (void) -- a genuinely valid, unremarkable native when called as its
 * own bare statement. Used where a value is expected (e.g.
 * `gigachad x = legacy_void();`), the scalar evaluators must reject it
 * rather than silently treating "no value" as 0. */
static StdrotValue stdrot_legacy_void(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){STDROT_NONE, {0}};
}

STDROT_EXPORT("legacy_void", stdrot_legacy_void);

/* Legacy/untyped export whose C implementation constructs a StdrotValue
 * literally tagged STDROT_ANY -- a native-binding bug (STDROT_ANY is a
 * descriptor placeholder, never a real value tag) that enforce_return_
 * type()'s STDROT_ANY branch must reject, not silently accept as "any
 * type, actual tag wins". */
static StdrotValue stdrot_legacy_returns_any_tag(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){STDROT_ANY, {0}};
}

STDROT_EXPORT("legacy_returns_any_tag", stdrot_legacy_returns_any_tag);

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

/* Takes a STDROT_CSTRING parameter, returns its length via strlen() --
 * exercises the representation-gap fix in semantic_check_native_call():
 * a char-array argument (`yap path[256]`) is source-level VAR_CHAR, but
 * ast_expr_to_stdrot_value() marshals it as STDROT_STRING and coerce_
 * arg_to_param() already converts STDROT_STRING -> STDROT_CSTRING, so a
 * call like `takes_cstring(path)` must type-check, not be rejected on
 * the strength of the array's element type alone. */
static StdrotValue stdrot_takes_cstring(StdrotValue *args, int argc)
{
    (void)argc;
    StdrotValue out = {STDROT_INT, {0}};
    out.val.i = (int)strlen(args[0].val.cstr);
    return out;
}

static const StdrotParam takes_cstring_params[] = {
    {STDROT_CSTRING, NULL, 0},
};
STDROT_EXPORT_SIG("takes_cstring", stdrot_takes_cstring,
                  ((StdrotParam){STDROT_INT, NULL, 0}), takes_cstring_params, 1,
                  1, false);

/* Takes a plain STDROT_CHAR parameter, returns it widened to int --
 * exercises the stdrot_char_narrows_to_int() fix (stdrot.h): a char
 * ARRAY-ACCESS argument (`buf[0]`) is source-level VAR_CHAR, but both
 * ast_expr_to_stdrot_value() and infer_expression_abi_type() must agree
 * it lowers to (STDROT_)INT, not (STDROT_)CHAR, for this call --
 * declared STDROT_CHAR -- to correctly be REJECTED (a genuine type
 * mismatch, argument is INT not CHAR), while a bare char identifier
 * (`c`) is correctly ACCEPTED. */
static StdrotValue stdrot_takes_char(StdrotValue *args, int argc)
{
    (void)argc;
    StdrotValue out = {STDROT_INT, {0}};
    out.val.i = (int)args[0].val.c;
    return out;
}

static const StdrotParam takes_char_params[] = {
    {STDROT_CHAR, NULL, 0},
};
STDROT_EXPORT_SIG("takes_char", stdrot_takes_char,
                  ((StdrotParam){STDROT_INT, NULL, 0}), takes_char_params, 1, 1,
                  false);

/* Identity function, T -> T, exactly like STDROT_EXPORT_SIG_IDENTITY's
 * own documentation describes -- and exactly the shape that turns a
 * nested legacy-ANY string argument into a use-after-free without
 * execute_native_call()'s materialize-before-free fix: `identity(legacy_
 * string())`'s argument marshalling allocates a fresh buffer for the
 * nested call's result, tracked as owned scratch to free once this call
 * is done -- but this native hands that SAME buffer straight back out as
 * its own result, so "done with it" isn't true until the caller has
 * actually consumed the return value. */
static StdrotValue stdrot_identity(StdrotValue *args, int argc)
{
    (void)argc;
    return args[0];
}

static const StdrotParam identity_params[] = {
    {STDROT_ANY, NULL, 0},
};
STDROT_EXPORT_SIG_IDENTITY("identity", stdrot_identity, identity_params, 1, 1);
