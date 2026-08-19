/* stdrot/stdrot_api.h – Public contract between libstdrot.so and the main
 * binary.
 *
 * This is the ONLY header shared between the two compilation units.
 * It contains plain C types with zero dependency on the interpreter or AST.
 *
 * ── HOW TO ADD A NEW STDLIB FUNCTION ────────────────────────────────────────
 *
 *   1. Create stdrot/myfunc.c and implement:
 *
 *        StdrotValue stdrot_myfunc(StdrotValue *args, int argc) { ... }
 *
 *   2. At the bottom of myfunc.c, add a typed signature so the semantic
 *      analyzer can check arity and argument types:
 *
 *        static const StdrotParam myfunc_params[] = {
 *            {STDROT_INT, NULL, 0},
 *        };
 *        STDROT_EXPORT_SIG("myfunc", stdrot_myfunc,
 *                          ((StdrotParam){STDROT_BOOL, NULL, 0}),
 *                          myfunc_params, 1, false);
 *
 *      Or, for an unchecked/untyped export: STDROT_EXPORT("myfunc",
 *      stdrot_myfunc); -- arity and types are then left unchecked.
 *
 *   3. Recompile only the shared library:
 *
 *        make libstdrot.so
 *
 *   The interpreter discovers it automatically on the next run.
 *   No changes to stdrot.c, stdrot.h, or any other main-binary file needed.
 *
 * Builtins and extensions are all exposed through the same generic
 * StdrotFn signature, so the host does not hardcode function names.
 */

#ifndef STDROT_API_H
#define STDROT_API_H

#include "../lib/string_value.h"
#include <stdbool.h>
#include <stddef.h>

/* ── Global execution context ──────────────────────────────────────────── *
 * Set by the main binary before calling stdlib functions
 * Allows functions to report line numbers and context
 */
typedef struct
{
    int line_number;
    String function_name;
    String condition_text;
} ExecutionContext;

extern ExecutionContext g_exec_context;

/* ── Pre-evaluated argument / return value ──────────────────────────────── */

typedef enum
{
    STDROT_ANY, /* unchecked: accepts/returns any type (legacy exports,
                   identity-polymorphic builtins like slorp) -- kept as the
                   zero value so a zero-initialized StdrotParam means
                   "unchecked" rather than silently claiming int */
    STDROT_INT,
    STDROT_FLOAT,
    STDROT_DOUBLE,
    STDROT_SHORT,
    STDROT_BOOL,
    STDROT_CHAR,
    STDROT_STRING,  /* Brainrot String (length-prefixed, not NUL-terminated) */
    STDROT_CSTRING, /* NUL-terminated const char *, owned by the caller for
                        the duration of the call */
    STDROT_PTR,     /* untyped pointer / address-of result */
    STDROT_HANDLE,  /* opaque native resource, see StdrotValue.val.handle */
    STDROT_NONE     /* void return */
} StdrotType;

typedef struct
{
    StdrotType type;
    union
    {
        int i;
        float f;
        double d;
        short s;
        bool b;
        char c;
        String str;
        const char *cstr;
        void *ptr;
        struct
        {
            const char *type_name; /* handle "kind", e.g. "Texture2D" */
            void *handle;
        } handle;
    } val;
} StdrotValue;

/* ── Native call parameter / return descriptor ───────────────────────────── *
 * Describes one parameter (or the return value) of a native export so the
 * semantic analyzer can check arity and types ahead of time, the way
 * Brainrot-defined functions already are.
 */
typedef struct
{
    StdrotType type;
    const char *type_name; /* set when type == STDROT_HANDLE */
    int pointer_level;
} StdrotParam;

/* ── Generic extensible function signature ──────────────────────────────── *
 * The main binary evaluates every AST argument into a StdrotValue before
 * calling this, so the .so never needs to touch ASTNode or interpreter types.
 */
typedef StdrotValue (*StdrotFn)(StdrotValue *args, int argc);

/* ── Function registry entry ─────────────────────────────────────────────── *
 * libstdrot.so MUST export two symbols:
 *
 *   extern StdrotEntry stdrot_exports[];
 *   extern int         stdrot_export_count;
 *
 * fn != NULL  →  generic function, called with pre-evaluated StdrotValue args
 *
 * return_type / params describe the signature for the semantic analyzer.
 * params covers only the fixed/checked prefix of the argument list --
 * is_variadic == true means "arity >= param_count, remaining args
 * unchecked" (used for format-string builtins and for legacy/untyped
 * exports, where param_count is 0).
 */
typedef struct
{
    const char *name;
    StdrotParam return_type;
    const StdrotParam *params;
    int param_count;
    bool is_variadic;
    StdrotFn fn;
} StdrotEntry;

/* ── Self-registration via linker section ────────────────────────────────── *
 * STDROT_EXPORT_SIG(...) places the function descriptor into a special
 * linker section. The library startup code collects all entries automatically.
 *
 * ret is a StdrotParam compound literal, e.g. ((StdrotParam){STDROT_BOOL,
 * NULL, 0}). params_ptr is NULL or a `static const StdrotParam foo[] = {...}`
 * declared above the STDROT_EXPORT_SIG call, covering the first pcount
 * arguments; anything beyond that is unchecked when variadic is true.
 *
 * STDROT_EXPORT(name, fn) is the untyped legacy form: signature unknown,
 * arity unchecked, return type STDROT_ANY. Use fn == NULL for core
 * functions (yapping, baka, slorp, etc.) that need AST bridge handling in
 * stdrot.c.
 */

#if defined(__GNUC__) || defined(__clang__)
#define STDROT_CONCAT_IMPL(x, y) x##y
#define STDROT_CONCAT(x, y) STDROT_CONCAT_IMPL(x, y)
/* aligned(_Alignof(StdrotEntry)) pins each entry to its ABI-minimum
 * alignment. Without it, GCC's/Clang's heuristic that bumps the storage
 * alignment of "large" static objects (StdrotEntry is now well past the
 * old 16-byte struct) pads every entry up to the next 16-byte boundary,
 * so stdrot_exports's byte length stops being an exact multiple of
 * sizeof(StdrotEntry) and registry.c's (stop-start)/sizeof(StdrotEntry)
 * undercounts/overcounts entries -- corrupting the registry. */
#define STDROT_EXPORT_SIG(name_str, func_ptr, ret, params_ptr, pcount,         \
                          variadic)                                            \
    __attribute__((used, section("stdrot_exports"),                            \
                   aligned(_Alignof(StdrotEntry)))) static const StdrotEntry   \
    STDROT_CONCAT(__stdrot_export_, __LINE__) = {                              \
        name_str, ret, params_ptr, pcount, variadic, func_ptr}
#define STDROT_EXPORT(name_str, func_ptr)                                      \
    STDROT_EXPORT_SIG(name_str, func_ptr,                                      \
                      ((StdrotParam){STDROT_ANY, NULL, 0}), NULL, 0, true)
#else
#error                                                                         \
    "Linker sections not supported on this compiler. Add registry.c fallback."
#endif

/* ── API discovery entrypoint ────────────────────────────────────────────── *
 * libstdrot.so MUST export this function.
 * Returns pointer to the function table and count.
 */
typedef struct
{
    StdrotEntry *functions;
    int count;
} StdrotAPI;

StdrotAPI stdrot_get_api(void);

#endif /* STDROT_API_H */
