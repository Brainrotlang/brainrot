/* stdrot.h – Standard Brainrot library interface (main binary side)
 *
 * stdrot.c provides:
 *   • Thin varargs stubs (yapping / yappin / baka) that forward to the .so
 *   • AST bridge functions (execute_*_call) that evaluate arguments and call
 *     the raw implementations in libstdrot.so
 *   • A loader (stdrot_load / stdrot_unload) that uses dlopen/dlsym to wire
 *     the stubs and read the function registry from the .so
 */

#ifndef STDROT_H
#define STDROT_H

#include "ast.h"
#include "stdrot/stdrot_api.h"
#include "lib/string_value.h"
#include <stdbool.h>
#include <stddef.h>

/* ── Loader lifecycle ────────────────────────────────────────────────────── *
 * Call stdrot_load() once at interpreter startup (interpreter_new).
 * Call stdrot_unload() at interpreter shutdown  (interpreter_free).
 */
void stdrot_load(void);
void stdrot_unload(void);
/* Loads a native module resolved from #cooked <name> (module_path.c's
 * MODULE_ARTIFACT_NATIVE) and registers its functions alongside the core
 * library's. `name` is the #cooked <name> spelling (diagnostics only);
 * `so_path` is the already-resolved absolute path to dlopen. Exits with a
 * diagnostic on any failure, the same fail-loud posture stdrot_load() has
 * for the core library -- see stdrot.c's own comment on this function.
 * Unavailable in a STDROT_STATIC (wasm) build, which has no dynamic loader
 * to dlopen a module with: calling it there always fails loudly rather
 * than being silently absent, since module_path_resolve() (module_path.h)
 * never resolves a name to MODULE_ARTIFACT_NATIVE in that build, so this
 * should be unreachable there in practice. */
void stdrot_load_module(const char *name, const char *so_path);

/* ── Runtime query / dispatch ────────────────────────────────────────────── */
bool is_builtin_function(const String func_name);
/* call_line is the source line of the CALL node, forwarded to
 * execute_native_call() as the line source a ZERO-argument native has (see
 * that function's own comment). Statement-position callers
 * (interpreter_execute_call_statement()) must pass node->line_number so a
 * bare `gamba();` abort reports its call site, not "line 0". */
void execute_builtin_function(const String func_name, ArgumentList *args,
                              int call_line);
void execute_func_call(const String func_name, ArgumentList *args,
                       int call_line);
/* Registry lookup for a native export's descriptor (name, return_type,
 * params, param_count, min_args, is_variadic). Returns NULL if func_name
 * isn't a registered native function. Used by the semantic analyzer to
 * check native calls the way user-defined functions are checked. */
const StdrotEntry *get_native_function(const String func_name);
/* Shared StdrotType -> VarType mapping, used by both the interpreter
 * (ast.c) and the semantic analyzer so the two never drift apart. */
VarType stdrot_type_to_vartype(StdrotType type);
/* True for the AST expression shapes whose marshalled ABI representation
 * never preserves a VAR_CHAR base type as STDROT_CHAR -- narrowed to plain
 * int instead, matching whichever specific C rule actually applies to
 * that shape (NOT a single blanket "sub-int expression" rule -- see the
 * per-case reasoning in the implementation, stdrot.c). unary_op is only
 * consulted when node_type == NODE_UNARY_OPERATION (the same OperatorType
 * that expression's own data.unary.op holds); ignored, so any value is
 * fine, for every other node_type.
 *
 * This is the ONE place this exception is defined -- both the runtime
 * marshaller (ast_expr_to_stdrot_value(), stdrot.c) and the static
 * checker (infer_expression_abi_type(), semantic_analyzer.c) call this
 * instead of maintaining their own copy of the exception list, so the two
 * cannot independently drift out of agreement about which node shapes
 * it applies to. */
bool stdrot_char_narrows_to_int(NodeType node_type, OperatorType unary_op);

/* ── String boundary ──────────────────────────────────────────────────────
 * Brainrot's String is length-prefixed and not guaranteed NUL-terminated;
 * C library functions want a NUL-terminated const char *. This is the one
 * place that conversion happens.
 *
 * Ownership: the returned pointer is adapter-owned scratch, valid only for
 * the duration of the native call -- release it with
 * stdrot_release_cstring() once the call returns. No signature is
 * annotated as escaping yet, so nothing may hold onto the pointer past
 * that point. */
const char *stdrot_string_to_cstring(String s);
void stdrot_release_cstring(const char *p);
/* execute_native_call()'s result, PAIRED with whether .value's
 * STDROT_STRING payload (if any) is a heap buffer this specific result
 * now owns and nothing else references (see execute_native_call()'s own
 * comment for the one case that sets owns_string true: an identity-style
 * native, T -> T, handing back a nested call's result unchanged).
 *
 * Ownership travels WITH the value, in this struct, rather than through
 * a side-channel global -- a global cannot survive nesting: evaluating
 * one native call's arguments can itself invoke other native calls (this
 * is not an edge case, it's the ordinary case for e.g.
 * `takes_cstring(identity(legacy_string()))`), each with its own
 * execute_native_call() invocation and its own materialize-or-not
 * decision. A single mutable flag set by the innermost call and never
 * reset by an outer call whose OWN result isn't a string (its materialize
 * check is gated on `result.type == STDROT_STRING`, so it never touches
 * the flag at all) would leave that flag describing a completely
 * unrelated, already-freed inner result by the time the outer call's
 * cleanup code reads it -- and then reinterpret the outer result's own
 * non-string union member (an int, a bool, ...) as a heap pointer and
 * free it. Every caller of execute_native_call() gets its OWN
 * NativeResult, scoped to exactly the call that produced it, so this
 * can't happen regardless of how deeply calls nest. */
typedef struct
{
    StdrotValue value;
    bool owns_string;
} NativeResult;

/* Evaluates args and invokes the native function, returning its result
 * directly -- no write-back, no deprecation warning. This is what
 * expression-position native calls (ast.c's handle_function_call) use;
 * execute_func_call() layers the deprecated write-back convention on top
 * of this for statement-position calls.
 *
 * call_line is the source line of the CALL node itself, used to populate
 * g_exec_context.line_number for a native that reports an error. It is the
 * only line source for a ZERO-argument call (e.g. `gamba()`): the line was
 * historically taken from the first argument's node, so an arg-less native
 * that aborts (a CSPRNG failure, the wasm gamba stub) would otherwise report
 * "line 0". A positive first-argument line still wins when present, keeping
 * every existing multi-arg diagnostic unchanged; call_line is the fallback.
 * Pass 0 when no call-site line is available. */
NativeResult execute_native_call(const String func_name, ArgumentList *args,
                                 int call_line);

/* ── Stub functions (forward declarations for use by ast.c) ──────────────── */
void yapping(const String format, ...);
void yappin(const String format, ...);
void baka(const String format, ...);
void ragequit(int exit_code);
void chill(unsigned int seconds);
char slorp_char(char chr);
int slorp_int(int val);
short slorp_short(short val);
float slorp_float(float var);
double slorp_double(double var);

#endif /* STDROT_H */
