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

/* ── Runtime query / dispatch ────────────────────────────────────────────── */
bool is_builtin_function(const String func_name);
void execute_builtin_function(const String func_name, ArgumentList *args);
void execute_func_call(const String func_name, ArgumentList *args);
/* Registry lookup for a native export's descriptor (name, return_type,
 * params, param_count, min_args, is_variadic). Returns NULL if func_name
 * isn't a registered native function. Used by the semantic analyzer to
 * check native calls the way user-defined functions are checked. */
const StdrotEntry *get_native_function(const String func_name);
/* Shared StdrotType -> VarType mapping, used by both the interpreter
 * (ast.c) and the semantic analyzer so the two never drift apart. */
VarType stdrot_type_to_vartype(StdrotType type);
/* True for the AST expression shapes whose marshalled ABI representation
 * never preserves a VAR_CHAR base type as STDROT_CHAR (or a VAR_STRUCT/
 * VAR_STRING one, though those have their own separate handling) --
 * array access, struct field access, unary operations, and binary
 * operations all lower a char through plain int instead (matching C's
 * own integer-promotion rules for sub-int arithmetic/access contexts).
 * Only a bare identifier, a literal, or a native call's own declared
 * result preserves char/string faithfully. This is the ONE place this
 * exception is defined -- both the runtime marshaller
 * (ast_expr_to_stdrot_value(), stdrot.c) and the static checker
 * (infer_expression_abi_type(), semantic_analyzer.c) call this instead
 * of maintaining their own copy of the exception list, so the two
 * cannot independently drift out of agreement about which node shapes
 * it applies to. */
bool stdrot_char_narrows_to_int(NodeType node_type);

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
/* Evaluates args and invokes the native function, returning its result
 * directly -- no write-back, no deprecation warning. This is what
 * expression-position native calls (ast.c's handle_function_call) use;
 * execute_func_call() layers the deprecated write-back convention on top
 * of this for statement-position calls. */
StdrotValue execute_native_call(const String func_name, ArgumentList *args);

/* ── Stub functions (forward declarations for use by ast.c) ──────────────── */
void yapping(const String format, ...);
void yappin(const String format, ...);
void baka(const String format, ...);
void ragequit(int exit_code);
void chill(unsigned int seconds);
char slorp_char(char chr);
/* NOTE: this declared signature does not match stdrot/slorp.c's actual
 * `char *slorp_string(char *, size_t)`. Nothing currently calls
 * slorp_string() — the `slorp` builtin goes through the generic
 * StdrotFn registry (stdrot_slorp in stdrot/slorp.c) instead — so this
 * has never been linked/exercised under either build mode. If you add a
 * caller, fix the signature mismatch first (native's dlsym-cast path has
 * the same latent bug, just harder to see there). */
String slorp_string(String string, size_t size);
int slorp_int(int val);
short slorp_short(short val);
float slorp_float(float var);
double slorp_double(double var);

#endif /* STDROT_H */
