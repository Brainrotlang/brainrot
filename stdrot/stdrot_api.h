/* stdrot/stdrot_api.h – Public contract between libstdrot.so and the main binary.
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
 *   2. At the bottom of myfunc.c, add:
 *
 *        STDROT_EXPORT("myfunc", stdrot_myfunc);
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

#include <stdbool.h>
#include <stddef.h>

/* ── Global execution context ──────────────────────────────────────────── *
 * Set by the main binary before calling stdlib functions
 * Allows functions to report line numbers and context
 */
typedef struct {
    int line_number;
    const char *function_name;
    const char *condition_text;
} ExecutionContext;

extern ExecutionContext g_exec_context;

/* ── Pre-evaluated argument / return value ──────────────────────────────── */

typedef enum {
    STDROT_INT,
    STDROT_FLOAT,
    STDROT_DOUBLE,
    STDROT_SHORT,
    STDROT_BOOL,
    STDROT_CHAR,
    STDROT_STRING,
    STDROT_NONE   /* void return */
} StdrotType;

typedef struct {
    StdrotType type;
    union {
        int    i;
        float  f;
        double d;
        short  s;
        bool   b;
        char   c;
        char  *str;
    } val;
} StdrotValue;

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
 */
typedef struct {
    const char *name;
    StdrotFn    fn;
} StdrotEntry;

/* ── Self-registration via linker section ────────────────────────────────── *
 * STDROT_EXPORT(name, fn) places the function descriptor into a special
 * linker section. The library startup code collects all entries automatically.
 *
 * Use fn == NULL for core functions (yapping, baka, slorp, etc.) that need
 * AST bridge handling in stdrot.c.
 */

#if defined(__GNUC__) || defined(__clang__)
    #define STDROT_CONCAT_IMPL(x, y) x##y
    #define STDROT_CONCAT(x, y) STDROT_CONCAT_IMPL(x, y)
    #define STDROT_EXPORT(name_str, func_ptr) \
        __attribute__((used, section("stdrot_exports"))) \
        static const StdrotEntry STDROT_CONCAT(__stdrot_export_, __LINE__) = { name_str, func_ptr }
#else
    #error "Linker sections not supported on this compiler. Add registry.c fallback."
#endif

/* ── API discovery entrypoint ────────────────────────────────────────────── *
 * libstdrot.so MUST export this function.
 * Returns pointer to the function table and count.
 */
typedef struct {
    StdrotEntry *functions;
    int count;
} StdrotAPI;

StdrotAPI stdrot_get_api(void);

#endif /* STDROT_API_H */
