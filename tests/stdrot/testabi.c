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
#include <stdio.h>
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

/* Round-18 review, finding #1 -- identical to legacy_int() above (a
 * legacy/untyped STDROT_EXPORT() export, statically-unknowable return
 * type), except it also prints an unmistakable marker to stdout, so a
 * test can prove sizeof's operand was never *evaluated* even when the
 * unknowable native call is nested inside a larger expression
 * (`maxxing(legacy_int_prints() + 1)`), not just when it's the whole
 * operand by itself: if handle_sizeof() (ast.c) ever fell back to
 * get_expression_type()'s execute-to-discover path for a native call
 * buried inside a NODE_OPERATION/NODE_UNARY_OPERATION, this marker would
 * appear in the program's output despite the sizeof being rejected as a
 * semantic error. */
static StdrotValue stdrot_legacy_int_prints(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    printf("THIS MUST NEVER PRINT\n");
    StdrotValue out = {STDROT_INT, {0}};
    out.val.i = 42;
    return out;
}

STDROT_EXPORT("legacy_int_prints", stdrot_legacy_int_prints);

/* Round-17 review, finding #3 -- a second legacy/untyped export, this
 * one returning a fractional STDROT_DOUBLE, reused alongside legacy_int()
 * to build a chain of MORE than NATIVE_CALL_CACHE_INITIAL_CAPACITY (16)
 * simultaneously-pending native-call type-probes in one expression
 * (get_expression_type()'s NODE_OPERATION case recursively probes every
 * leaf's type before any of them are evaluated for their value). Proves
 * the native-call memo cache (ast.c) growing dynamically, instead of
 * silently reporting STDROT_NONE once a fixed-size cap filled up, means
 * this native's fractional value survives correctly regardless of how
 * many OTHER pending type-probes happen to be in flight at the same
 * time -- the whole expression's promoted type (and therefore its
 * computed value) must not depend on cache occupancy. */
static StdrotValue stdrot_legacy_half(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    StdrotValue out = {STDROT_DOUBLE, {0}};
    out.val.d = 0.5;
    return out;
}

STDROT_EXPORT("legacy_half", stdrot_legacy_half);

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
 * exercises stdrot_char_narrows_to_int() (stdrot.h): a char ARRAY-ACCESS
 * argument (`buf[0]`) is an ordinary char lvalue in C, the same as a
 * bare char identifier -- both ast_expr_to_stdrot_value() and
 * infer_expression_abi_type() must agree it stays (STDROT_)CHAR for
 * this call -- declared STDROT_CHAR -- to correctly be ACCEPTED, the
 * same as a bare char identifier (`c`). Only a genuinely promoting
 * shape (unary arithmetic negation, binary arithmetic/comparison, the
 * variadic tail of a variadic native) narrows to int -- see that
 * function's own per-case reasoning for the full rule. */
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

/* Round-15 review, finding #2: a legacy/untyped STDROT_EXPORT() export
 * (param_count == 0, is_variadic == true purely because the real
 * signature is unknown -- promote_variadic_tail, stdrot_api.h, stays
 * false) that actually INSPECTS args[0].type, unlike every other legacy
 * export in this file (legacy_int/legacy_string/legacy_cstring/
 * legacy_void/legacy_returns_any_tag all ignore args entirely). Exists
 * specifically so a regression that silently starts applying C default
 * argument promotion to a legacy export's arguments (as briefly
 * happened: is_variadic alone was mistaken for "this native has a C
 * variadic tail") is actually observable -- returns the numeric
 * StdrotType tag it received, letting the test assert exactly which one
 * arrived, not just that SOME call succeeded. */
static StdrotValue stdrot_legacy_type_probe(StdrotValue *args, int argc)
{
    StdrotValue out = {STDROT_INT, {0}};
    out.val.i = (argc > 0) ? (int)args[0].type : -1;
    return out;
}

STDROT_EXPORT("legacy_type_probe", stdrot_legacy_type_probe);

/* Round-17 review, finding #1 -- the exact scenario from the review:
 * a typed native declaring a STDROT_CSTRING parameter and a STDROT_STRING
 * return, whose C implementation points its STDROT_STRING result
 * straight at the SAME buffer coerce_arg_to_param() (stdrot.c) converted
 * its own argument into. That CSTRING conversion buffer is tracked in
 * owned_cstring_bufs[], a completely separate array from owned_string_
 * bufs[] (nested-native-call STDROT_STRING results) -- execute_native_
 * call()'s materialize-before-free logic previously only ever checked
 * the latter, so a result aliasing the FORMER sailed through
 * unmaterialized, got freed by the owned_cstring_bufs[] cleanup loop
 * like any other argument scratch, and the caller copied from already-
 * freed memory. Exists specifically to prove execute_native_call()'s
 * fix (unconditional materialization of every STDROT_STRING result,
 * regardless of which scratch array -- if any -- it happens to alias)
 * actually closes this, not just the one alias source the previous
 * round's fix recognized. */
static StdrotValue stdrot_cstring_as_string(StdrotValue *args, int argc)
{
    (void)argc;
    StdrotValue out = {STDROT_STRING, {0}};
    out.val.str.data = (char *)args[0].val.cstr;
    out.val.str.len = strlen(args[0].val.cstr);
    return out;
}

static const StdrotParam cstring_as_string_params[] = {
    {STDROT_CSTRING, NULL, 0},
};
STDROT_EXPORT_SIG("cstring_as_string", stdrot_cstring_as_string,
                  ((StdrotParam){STDROT_STRING, NULL, 0}),
                  cstring_as_string_params, 1, 1, false);
