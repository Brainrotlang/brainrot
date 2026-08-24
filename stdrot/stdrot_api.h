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
 *      analyzer can check arity and argument types -- this example
 *      declares `myfunc(rizz) -> rizz`, one mandatory int argument,
 *      returning an int:
 *
 *        static const StdrotParam myfunc_params[] = {
 *            {STDROT_INT, NULL, 0},
 *        };
 *        STDROT_EXPORT_SIG("myfunc", stdrot_myfunc,
 *                          ((StdrotParam){STDROT_INT, NULL, 0}),
 *                          myfunc_params, 1, 1, false);
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
    STDROT_ANY, /* accepts/returns any ABI-marshallable scalar/string
                   value (int/short/float/double/bool/char/string/enum) --
                   used for legacy exports (STDROT_EXPORT(), signature
                   genuinely unknown) and identity-polymorphic builtins
                   like slorp (return_like_arg, see StdrotEntry) -- kept
                   as the zero value so a zero-initialized StdrotParam
                   means "unchecked" rather than silently claiming int.
                   NOT "any value representable at all": pointers,
                   handles, structs/aggregates, and most array types are
                   all deliberately rejected even here, both statically
                   (semantic_check_native_call(), semantic_analyzer.c)
                   and at the runtime ABI boundary (enforce_return_type()/
                   enforce_arg_type(), stdrot.c) --
                     * pointer-valued: a pointer's whole point is its
                       address and level of indirection, which "any type,
                       unchecked" can't express safely -- say so
                       explicitly via STDROT_PTR below instead.
                     * STDROT_HANDLE-valued: needs a resource-ownership
                       model this ABI hasn't designed yet.
                     * STDROT_CSTRING-valued (as a *return*): nothing
                       marshals a returned C string into a Brainrot
                       String yet (as an *argument*, this is fine --
                       ordinary strings and char arrays both convert to
                       STDROT_CSTRING when a param declares it directly).
                     * struct/aggregate identifiers: no StdrotValue
                       representation exists for one.
                     * numeric arrays: Variable's value union aliases a
                       scalar's own storage with an array's backing
                       pointer, so marshalling one as a scalar would
                       reinterpret that pointer as data. A VAR_CHAR array
                       (`yap buf[32]`) is the one exception -- it has a
                       real representation (STDROT_STRING) and is
                       accepted.
                   A StdrotValue whose own .type tag is literally
                   STDROT_ANY is also rejected wherever this enum value
                   would otherwise be compared against one: STDROT_ANY is
                   a descriptor placeholder, never a value a native
                   should actually construct. */
    STDROT_INT,
    STDROT_FLOAT,
    STDROT_DOUBLE,
    STDROT_SHORT,
    STDROT_BOOL,
    STDROT_CHAR,
    STDROT_STRING,  /* Brainrot String (length-prefixed, not NUL-terminated).
                        Ownership differs by direction:
                          - as an ARGUMENT: borrowed for the duration of
                            the call only. A native must not retain
                            .val.str.data past its own return.
                          - as a RETURN value: also only borrowed by the
                            native past its own return, in the following
                            specific sense -- execute_native_call()
                            (stdrot.c) materializes (deep-copies) the
                            returned .val.str.data into independent memory
                            immediately after the call, BEFORE releasing
                            any of this call's own argument-owned scratch
                            buffers, specifically so a native that returns
                            one of its own arguments unchanged (T -> T,
                            see STDROT_EXPORT_SIG_IDENTITY below) doesn't
                            hand back a pointer this adapter is about to
                            free. A native's own return value therefore
                            does not need to be independently heap-owned
                            by the native itself -- returning a borrowed/
                            aliased buffer (including one of its own
                            arguments, as long as that argument was itself
                            a STDROT_STRING) is safe by construction. */
    STDROT_CSTRING, /* NUL-terminated const char *, STRICTLY NON-ESCAPING:
                        the adapter owns the buffer, allocated fresh from
                        the Brainrot String argument immediately before
                        the call and freed immediately after entry->fn()
                        returns (execute_native_call(), stdrot.c). A
                        native's C implementation MUST NOT retain this
                        pointer past its own return -- storing it in a
                        global, a callback registration, or any structure
                        that outlives the call is a use-after-free the
                        moment this adapter's cleanup runs. There is
                        currently no annotation for a native that needs
                        the opposite contract (retaining/owning the
                        string beyond the call, e.g. a C API like
                        `set_global_name(const char *)` that keeps the
                        pointer) -- that is a real, tracked gap (see
                        issue #205 and the roadmap's Phase 2 "string
                        boundary" section), not an oversight silently
                        left unaddressed. A native needing an escaping
                        string is not yet expressible through this ABI;
                        do not write one until this gap is closed. */
    STDROT_PTR,     /* An opaque native pointer -- base type intentionally
                        erased, AT EVERY DEPTH, not just the outermost one.
                        A StdrotParam of {STDROT_PTR, NULL, N} describes a
                        Brainrot-visible pointer of level N + 1 (STDROT_PTR
                        itself already represents one level of indirection,
                        on top of whatever `pointer_level` counts) -- e.g.
                        {STDROT_PTR, NULL, 0} is `rizz*`, {STDROT_PTR,
                        NULL, 1} is `rizz**`, and so on. Only pointer_level
                        is checked against the argument/return site; there
                        is no base type to compare, at any depth. This is
                        NOT the same guarantee as C's void* conversion
                        rule: void* <-> T* is sound for one level of
                        indirection, but void** <-> T** is not (a callee
                        could write an unrelated pointer through the
                        void**, corrupting whatever the T** side believes
                        it holds). StdrotParam has no field to express a
                        partially-erased pointer ("pointer to pointer to
                        known-int" vs. "...to known-double") -- only
                        pointer_level exists -- so STDROT_PTR at depth > 1
                        erases the *entire* pointed-to graph, uniformly. A
                        binding generator emitting STDROT_PTR at depth > 1
                        must treat it as fully opaque all the way down,
                        not assume any base-type safety at inner levels. */
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
 * libstdrot.so MUST export one versioned entrypoint function --
 * stdrot_get_api_v2() (see STDROT_ABI_VERSION's own comment below, and
 * registry.c) -- which returns a StdrotAPI built from every StdrotEntry
 * self-registered via STDROT_EXPORT_SIG()/STDROT_EXPORT_SIG_IDENTITY()/
 * STDROT_EXPORT_SIG_VARIADIC()/STDROT_EXPORT() below. There is no bare
 * `stdrot_exports[]`/`stdrot_export_count` pair to export directly --
 * that was this ABI's very first shape, before entries were even
 * self-registered into a linker section, let alone versioned.
 *
 * fn != NULL  →  generic function, called with pre-evaluated StdrotValue args
 *
 * return_type / params describe the signature for the semantic analyzer.
 * params holds param_count entries, each checked (type + pointer_level)
 * whenever the caller actually supplies that argument -- min_args of them
 * are mandatory (arity error below that), the rest (min_args..param_count)
 * are optional-but-typed, e.g. bet's trailing message string. is_variadic
 * == true additionally allows args beyond param_count, left completely
 * type-unchecked -- but that covers TWO unrelated situations (see
 * promote_variadic_tail below for why a third field, not this one,
 * decides whether C-style promotion applies to those args):
 *   - a genuine C-style variadic tail (format-string arguments, e.g.
 *     yapping) -- param_count/min_args are the real fixed-prefix arity.
 *   - a legacy/untyped STDROT_EXPORT() export, whose real signature this
 *     ABI simply doesn't know -- param_count and min_args are both 0,
 *     arity is unchecked purely for backward compatibility, not because
 *     the function has C varargs semantics.
 */
typedef struct
{
    const char *name;
    StdrotParam return_type;
    const StdrotParam *params;
    int param_count;
    int min_args;
    bool is_variadic;
    /* Only true for a genuine C-style variadic tail (set via
       STDROT_EXPORT_SIG_VARIADIC() below, e.g. yapping/yappin/baka) --
       NEVER inferred from is_variadic/param_count/return_type shape
       (e.g. "param_count == 0 && is_variadic" looking legacy-shaped).
       execute_native_call() (stdrot.c) applies C's default argument
       promotions (char/bool/short -> int, float -> double) to every
       argument at index >= param_count only when this is true. A
       legacy STDROT_EXPORT() export is ALSO is_variadic == true (arity
       is unchecked because the real signature is unknown, not because
       it's C-variadic) but leaves this false -- applying promotion
       there would silently change an existing legacy binding's
       StdrotValue.type for arguments it never asked to have promoted,
       without recompiling or touching that binding's own source: a
       real ABI break for any wrapper that switches on args[i].type. */
    bool promote_variadic_tail;
    /* -1: return_type.type is the call's real, fixed return type (the
       common case). >= 0: identity-polymorphic -- the call's actual
       result type is whatever the argument at this index turned out to
       be at the call site (slorp<T>(T) -> T is return_like_arg == 0), and
       return_type is a placeholder (STDROT_ANY), not a real type. Set
       explicitly via STDROT_EXPORT_SIG_IDENTITY() below -- never inferred
       by pattern-matching return_type/params shapes, since STDROT_ANY
       alone is also legitimately used for "legacy export, type genuinely
       unknown" (STDROT_EXPORT()'s expansion), a completely different
       relationship that must not be guessed at from the same shape. */
    int return_like_arg;
    StdrotFn fn;
} StdrotEntry;

/* ── Self-registration via linker section ────────────────────────────────── *
 * STDROT_EXPORT_SIG(...) places the function descriptor into a special
 * linker section. The library startup code collects all entries automatically.
 *
 * ret is a StdrotParam compound literal, e.g. ((StdrotParam){STDROT_BOOL,
 * NULL, 0}). params_ptr is NULL or a `static const StdrotParam foo[] = {...}`
 * declared above the STDROT_EXPORT_SIG call, covering the first pcount
 * arguments (min_count of which are mandatory, the rest optional-but-typed);
 * anything beyond pcount is unchecked when variadic is true.
 *
 * STDROT_EXPORT_SIG_IDENTITY(...) is for the rare identity-polymorphic
 * shape (currently only slorp<T>(T) -> T): no explicit return type since
 * there isn't a fixed one -- the result type is whatever argument 0 turns
 * out to be at the call site (see StdrotEntry.return_like_arg).
 *
 * STDROT_EXPORT(name, fn) is the untyped legacy form: signature unknown,
 * arity unchecked, return type STDROT_ANY (genuinely unknown -- not to be
 * confused with STDROT_EXPORT_SIG_IDENTITY's STDROT_ANY, a different
 * relationship that happens to reuse the same placeholder type; the two
 * are told apart by return_like_arg, not by any similarity in shape).
 *
 * All three use designated initializers so a future StdrotEntry field
 * doesn't require touching every existing call site to add a positional
 * argument -- new fields default to whichever zero value is a safe
 * "does nothing extra" default.
 */

#if defined(__GNUC__) || defined(__clang__)
#define STDROT_CONCAT_IMPL(x, y) x##y
#define STDROT_CONCAT(x, y) STDROT_CONCAT_IMPL(x, y)
#if defined(__APPLE__) && defined(__MACH__)
#define STDROT_EXPORT_SECTION "__DATA,stdrot_exports"
#else
#define STDROT_EXPORT_SECTION "stdrot_exports"
#endif
/* The StdrotEntry itself lives in ordinary static storage -- normal
 * variable-sized-struct layout rules apply, nothing unusual about it.
 * Only a *pointer* to it goes in the special section, so every slot
 * registry.c walks is sizeof(StdrotEntry *), uniformly, regardless of
 * what StdrotEntry itself contains or how the compiler chooses to align
 * a "large" static struct. See StdrotAPI's comment in this header for why
 * that matters. */
#define STDROT_EXPORT_SIG(name_str, func_ptr, ret, params_ptr, pcount,         \
                          min_count, variadic)                                 \
    static const StdrotEntry STDROT_CONCAT(__stdrot_entry_, __LINE__) = {      \
        .name = name_str,                                                      \
        .return_type = ret,                                                    \
        .params = params_ptr,                                                  \
        .param_count = pcount,                                                 \
        .min_args = min_count,                                                 \
        .is_variadic = variadic,                                               \
        .return_like_arg = -1,                                                 \
        .fn = func_ptr};                                                       \
    __attribute__((                                                            \
        used, section(STDROT_EXPORT_SECTION))) static const StdrotEntry *const \
    STDROT_CONCAT(__stdrot_export_, __LINE__) =                                \
        &STDROT_CONCAT(__stdrot_entry_, __LINE__)
#define STDROT_EXPORT_SIG_IDENTITY(name_str, func_ptr, params_ptr, pcount,     \
                                   min_count)                                  \
    static const StdrotEntry STDROT_CONCAT(__stdrot_entry_, __LINE__) = {      \
        .name = name_str,                                                      \
        .return_type = ((StdrotParam){STDROT_ANY, NULL, 0}),                   \
        .params = params_ptr,                                                  \
        .param_count = pcount,                                                 \
        .min_args = min_count,                                                 \
        .is_variadic = false,                                                  \
        .return_like_arg = 0,                                                  \
        .fn = func_ptr};                                                       \
    __attribute__((                                                            \
        used, section(STDROT_EXPORT_SECTION))) static const StdrotEntry *const \
    STDROT_CONCAT(__stdrot_export_, __LINE__) =                                \
        &STDROT_CONCAT(__stdrot_entry_, __LINE__)
#define STDROT_EXPORT(name_str, func_ptr)                                      \
    STDROT_EXPORT_SIG(name_str, func_ptr,                                      \
                      ((StdrotParam){STDROT_ANY, NULL, 0}), NULL, 0, 0, true)
/* Same as STDROT_EXPORT_SIG, but for a native with a genuine C-style
 * variadic tail (arguments beyond pcount are format-string-style
 * varargs, e.g. yapping/yappin/baka) rather than STDROT_EXPORT()'s
 * "legacy export, real signature unknown" use of is_variadic. Sets
 * StdrotEntry.promote_variadic_tail (see its own comment) explicitly,
 * so execute_native_call() applies C's default argument promotions to
 * this native's tail specifically, never inferred from is_variadic/
 * param_count alone -- a plain STDROT_EXPORT_SIG(..., true) or
 * STDROT_EXPORT() stays un-promoted. */
#define STDROT_EXPORT_SIG_VARIADIC(name_str, func_ptr, ret, params_ptr,        \
                                   pcount, min_count)                          \
    static const StdrotEntry STDROT_CONCAT(__stdrot_entry_, __LINE__) = {      \
        .name = name_str,                                                      \
        .return_type = ret,                                                    \
        .params = params_ptr,                                                  \
        .param_count = pcount,                                                 \
        .min_args = min_count,                                                 \
        .is_variadic = true,                                                   \
        .promote_variadic_tail = true,                                         \
        .return_like_arg = -1,                                                 \
        .fn = func_ptr};                                                       \
    __attribute__((                                                            \
        used, section(STDROT_EXPORT_SECTION))) static const StdrotEntry *const \
    STDROT_CONCAT(__stdrot_export_, __LINE__) =                                \
        &STDROT_CONCAT(__stdrot_entry_, __LINE__)
#else
#error                                                                         \
    "Linker sections not supported on this compiler. Add registry.c fallback."
#endif

/* ── API discovery entrypoint ────────────────────────────────────────────── *
 * libstdrot.so MUST export this function.
 * Returns pointer to the function table and count.
 *
 * functions is an array of *pointers* to entries, not an array of entries.
 * The linker section holds only these uniformly pointer-sized slots; the
 * StdrotEntry structs themselves live in ordinary static storage (see
 * STDROT_EXPORT_SIG below). This is deliberate: a linker section of
 * variable-content-but-fixed-declared-size structs is exactly what
 * previously broke registry.c's entry count (a compiler heuristic padded
 * each struct's storage alignment once StdrotEntry grew past 16 bytes,
 * silently corrupting (stop-start)/sizeof(StdrotEntry)). A section of
 * pointers doesn't have that failure mode at all -- every slot is
 * sizeof(StdrotEntry *), always, regardless of how many fields
 * StdrotEntry grows to carry. */
typedef struct
{
    const StdrotEntry *const *functions;
    int count;
} StdrotAPI;

/* ── ABI version boundary ────────────────────────────────────────────────── *
 * This header is the public contract between libstdrot.so and the main
 * binary -- both are built from it, but nothing forces them to be built
 * from the SAME copy of it. `make install` puts libstdrot.so somewhere the
 * dynamic linker finds it independently of any particular ./brainrot
 * build, and stdrot_load() dlopen's it by bare filename -- an old .so left
 * over from before this ABI existed (StdrotEntry == {name, fn}, StdrotType
 * had no STDROT_ANY at index 0, the registry section held StdrotEntry
 * structs directly rather than pointers to them) is exactly as reachable
 * at runtime as a freshly rebuilt one.
 *
 * Bumping STDROT_ABI_VERSION and renaming the entrypoint together
 * (stdrot_get_api -> stdrot_get_api_v2, this version) means an old .so
 * exporting the old symbol under the old name is simply invisible to a
 * new host's dlsym() -- it fails the lookup instead of returning a
 * StdrotAPI whose `functions`/`count` fields the old .so populated from a
 * completely different memory layout (an old StdrotEntry array
 * reinterpreted as an array of StdrotEntry POINTERS turns each old
 * entry's own `name` field into a bogus pointer the new host would then
 * dereference). stdrot_load() (stdrot.c) treats a missing v2 symbol as a
 * hard, loud, immediate failure -- "rebuild libstdrot.so", not a guess at
 * how to interpret unfamiliar memory. Bump this version (and the
 * entrypoint name/number together) again the next time StdrotEntry's
 * layout, StdrotType's numbering, or StdrotAPI's own shape changes in a
 * way an old .so's memory could be misread as. */
#define STDROT_ABI_VERSION 2

StdrotAPI stdrot_get_api_v2(void);

#endif /* STDROT_API_H */
