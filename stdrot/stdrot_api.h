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
                     * struct/aggregate identifiers: a StdrotValue
                       representation does now exist (STDROT_STRUCT,
                       below), but STDROT_ANY still rejects one, for
                       the same reason it rejects a pointer -- an
                       aggregate is only meaningful together with its
                       tag, and "any type, unchecked" has nowhere to
                       carry or compare one. Declare the parameter
                       STDROT_STRUCT with an explicit type_name instead.
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
    STDROT_HANDLE,  /* An opaque native RESOURCE -- a file, later a socket
                        or a texture -- carried as an address plus a "kind"
                        tag, see StdrotValue.val.handle.
 
                        ── The ownership model (roadmap Appendix B Q6) ──
                        Q6 asked whether "handles sidestep ownership by
                        keeping it in C" is the general answer. It is, and
                        this is that answer written down, validated first on
                        files (#213) because they are the simplest resource
                        that outlives a statement:
 
                        1. OWNERSHIP STAYS IN C. The native library creates
                           the resource, owns it, and provides an explicit
                           release. Brainrot never sees a freeable pointer,
                           only a token, so there is no Brainrot-side free()
                           to get wrong and nothing for the arena or the
                           string machinery to take responsibility for.
                        2. RELEASE IS MANUAL, NOT COLLECTED. Brainrot has no
                           destructors and no GC, so a handle is closed by
                           calling the library's release function. What makes
                           that safe rather than merely conventional is (3).
                        3. THE LIBRARY KEEPS A REGISTRY OF LIVE HANDLES, and
                           validates every handle it is given against it.
                           This is the part that matters, and it is why a
                           handle is genuinely safer than the raw STDROT_PTR
                           it superficially resembles: a Brainrot program can
                           hold a value that no longer means anything (a
                           stale handle kept past release) and hand it back,
                           and a raw pointer would be dereferenced or
                           free()d. A registered handle is looked up first
                           and rejected if it is not live -- so
                           use-after-release and double-release are
                           diagnosed, not undefined.
 
                           THE HANDLE MUST BE A TOKEN, NOT THE RESOURCE'S
                           ADDRESS, and this is a correctness requirement
                           rather than a style note. Registering addresses
                           checks LIVENESS ("is some live resource here?")
                           when every caller needs IDENTITY ("is this the
                           resource the program opened?"). Those diverge the
                           instant the allocator reuses an address, which it
                           does immediately: released, reopened, and the
                           stale handle passes the check while naming a
                           different resource. Measured at 50 reuses out of
                           50 on a release build when stdrot/file.c was
                           first written this way (#329 review) -- and
                           invisible under ASan and valgrind, whose
                           quarantines delay reuse, so a green test suite is
                           no evidence either way. Issue a value that is
                           never issued twice (a counter, or slot+generation)
                           and the guarantee stops depending on the
                           allocator.
 
                        3a. CONSEQUENTLY, val.handle.handle IS AN OPAQUE
                            TOKEN, NOT A POINTER. A binding must not
                            dereference it, must not compare it against
                            addresses of its own, and must not assume two
                            handles naming the same resource compare equal
                            (or that two different resources compare
                            unequal). The only thing it may do is hand the
                            value back to the library that issued it. It is
                            declared void * because the ABI has nowhere
                            better to put an integer of pointer width -- not
                            because it points at anything.
                        4. ANYTHING STILL LIVE AT UNLOAD IS RELEASED by the
                           library itself. That is what makes "no leaked
                           resource on any exit path" true for paths a
                           program cannot clean up after -- ragequit(), a
                           fatal error, or simply forgetting to close.
 
                        The `kind` tag (val.handle.type_name, mirrored by
                        StdrotParam.type_name) is checked at the ABI boundary
                        the same way STDROT_STRUCT's tag is: two resources
                        are both STDROT_HANDLE, so the base type alone cannot
                        tell a SAUCE from a future socket, and enforce_arg_
                        type() compares the tags rather than trusting it.
 
                        Both directions are implemented: a native may take a
                        handle and RETURN one. That is the difference from
                        STDROT_CSTRING and STDROT_STRUCT, which remain
                        argument-direction-only -- their return side needs an
                        ownership answer that returning a token does not,
                        because a token is not memory the caller must free. */
    STDROT_STRUCT,  /* A `gang`/`chungus` aggregate passed BY VALUE, as a
                        flat byte image laid out to the C ABI -- see
                        StdrotValue.val.blob. This is what makes a native
                        taking `Vector2`/`Color`/`Rectangle` expressible
                        without inventing a per-struct handle (Phase 5
                        Road B, issue #208); compute_struct_layout()
                        (ast.c) already produces exactly C's offsets,
                        alignment and trailing padding for a `gang`, so
                        the bytes a native receives are directly
                        memcpy-able into the real C type. That equivalence
                        is the whole premise, and it is verified rather
                        than assumed -- see tests/abi/struct_layout_abi_
                        check.c, whose _Static_assert/offsetof ground
                        truth every layout fixture is checked against.
 
                        ARGUMENT direction only, for now: a STDROT_STRUCT
                        *return* is rejected outright by semantic_check_
                        native_call() (semantic_analyzer.c), the same way
                        STDROT_HANDLE and STDROT_CSTRING returns are, and
                        for the same reason -- returning an aggregate
                        needs an ownership answer this ABI hasn't made
                        yet. The obvious sketch (a native returning a
                        pointer to a local `Texture2D tex;`) is a
                        dangling pointer the instant the native returns,
                        and STDROT_STRING's "adapter deep-copies
                        immediately" trick does not rescue it: that
                        contract works only because the borrowed storage
                        is still alive at the moment of return, which a
                        dead stack frame's is not. Do not add a struct-
                        returning native until that question is answered
                        (roadmap Appendix B Q6, "ownership of native
                        resources").
 
                        Ownership, as an argument: `.val.blob.data` is
                        adapter-owned scratch -- a fresh COPY of the
                        caller's struct, allocated immediately before the
                        call and freed immediately after entry->fn()
                        returns (execute_native_call(), stdrot.c),
                        exactly like STDROT_CSTRING's buffer. A native
                        MUST NOT retain the pointer past its own return.
                        Because it is a copy and not the Brainrot
                        variable's own storage, a native may freely
                        mutate those bytes: that is real C by-value
                        semantics, and it matches the value-copy rule
                        struct assignment/argument-passing/return already
                        follow everywhere else in the language (roadmap
                        Appendix B Q3). The copy is deliberate cost --
                        one malloc+memcpy per struct argument per call --
                        chosen over aliasing the live variable because
                        every aliasing shortcut this ABI has taken with
                        borrowed buffers has eventually turned into a
                        use-after-free (see STDROT_STRING's own comment
                        for the last two). */
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
        /* STDROT_STRUCT's carrier. Named `blob` because that is already
           this codebase's word for a struct's flat byte image (ast.c/
           ast.h speak of a struct blob throughout) -- `struct` is a
           keyword and `s` is already taken by the short. */
        struct
        {
            /* The Brainrot tag, NUL-terminated and WITHOUT the `gang`/
               `chungus` keyword: `gang Vector2` is "Vector2". Borrowed
               for the duration of the call, and anchored to the type
               registry rather than to any caller-side storage: it points
               into the registered StructDef's own name, which outlives
               every call. That is a property of the single producer of
               these values -- marshal_struct_argument() (stdrot.c),
               which sets it from def->name.data specifically so this
               sentence stays true even when the source expression it
               resolved was a temporary. Any future second producer must
               do the same; pointing it at a live Variable's descriptor
               instead would silently narrow the documented lifetime to
               that variable's scope. Checked against the
               declared StdrotParam.type_name by enforce_arg_type()
               (stdrot.c) before entry->fn() ever sees it, so a native
               may read this purely for its own dispatch/debugging and is
               entitled to assume it already matches what it declared --
               size alone is NOT a type check (`gang Vector2 {chad x, y;}`
               and `gang Size {chad w, h;}` are both 8 bytes and would
               otherwise be silently interchangeable). */
            const char *type_name;
            /* First byte of the C-ABI-laid-out image. Adapter-owned
               scratch, freed the instant the call returns -- see
               STDROT_STRUCT's own comment for the full contract. */
            void *data;
            /* def->total_size: the size of one instance INCLUDING
               trailing padding, i.e. what C's sizeof would report, not
               the sum of the field sizes. A native that memcpy's this
               into a real C struct should assert it equals sizeof(that
               type); the generator emits exactly that assertion. */
            size_t size;
        } blob;
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
    /* set when type == STDROT_HANDLE (the resource "kind") or
       STDROT_STRUCT (the `gang`/`chungus` tag, no keyword -- "Vector2").
       REQUIRED for STDROT_STRUCT, not optional: validate_native_registry()
       (stdrot.c) rejects a STDROT_STRUCT descriptor with a NULL/empty
       type_name at load time, because without a tag to compare there is
       nothing to check an argument against but its size, and equal size
       is not equal type. */
    const char *type_name;
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
#define STDROT_EXPORT_SECTION_NAME "stdrot_exports"
#if defined(__APPLE__) && defined(__MACH__)
#define STDROT_EXPORT_SEGMENT_NAME "__DATA"
#define STDROT_EXPORT_SECTION                                                  \
    STDROT_EXPORT_SEGMENT_NAME "," STDROT_EXPORT_SECTION_NAME                  \
                               ",regular,no_dead_strip"
#define STDROT_EXPORT_ATTR used, section(STDROT_EXPORT_SECTION)
#else
#define STDROT_EXPORT_SECTION STDROT_EXPORT_SECTION_NAME
#define STDROT_EXPORT_ATTR used, section(STDROT_EXPORT_SECTION)
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
        .name = (name_str),                                                    \
        .return_type = (ret),                                                  \
        .params = (params_ptr),                                                \
        .param_count = (pcount),                                               \
        .min_args = (min_count),                                               \
        .is_variadic = (variadic),                                             \
        .return_like_arg = -1,                                                 \
        .fn = (func_ptr)};                                                     \
    __attribute__((STDROT_EXPORT_ATTR)) static const StdrotEntry *const        \
    STDROT_CONCAT(__stdrot_export_, __LINE__) =                                \
        &STDROT_CONCAT(__stdrot_entry_, __LINE__)
#define STDROT_EXPORT_SIG_IDENTITY(name_str, func_ptr, params_ptr, pcount,     \
                                   min_count)                                  \
    static const StdrotEntry STDROT_CONCAT(__stdrot_entry_, __LINE__) = {      \
        .name = (name_str),                                                    \
        .return_type = ((StdrotParam){STDROT_ANY, NULL, 0}),                   \
        .params = (params_ptr),                                                \
        .param_count = (pcount),                                               \
        .min_args = (min_count),                                               \
        .is_variadic = false,                                                  \
        .return_like_arg = 0,                                                  \
        .fn = (func_ptr)};                                                     \
    __attribute__((STDROT_EXPORT_ATTR)) static const StdrotEntry *const        \
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
        .name = (name_str),                                                    \
        .return_type = (ret),                                                  \
        .params = (params_ptr),                                                \
        .param_count = (pcount),                                               \
        .min_args = (min_count),                                               \
        .is_variadic = true,                                                   \
        .promote_variadic_tail = true,                                         \
        .return_like_arg = -1,                                                 \
        .fn = (func_ptr)};                                                     \
    __attribute__((STDROT_EXPORT_ATTR)) static const StdrotEntry *const        \
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
 * (stdrot_get_api -> stdrot_get_api_v2 -> stdrot_get_api_v3, this
 * version) means an old .so exporting the old symbol under the old name
 * is simply invisible to a new host's dlsym() -- it fails the lookup
 * instead of returning a StdrotAPI whose `functions`/`count` fields the
 * old .so populated from a completely different memory layout (an old
 * StdrotEntry array reinterpreted as an array of StdrotEntry POINTERS
 * turns each old entry's own `name` field into a bogus pointer the new
 * host would then dereference). stdrot_load() (stdrot.c) treats a missing
 * v3 symbol as a hard, loud, immediate failure -- "rebuild
 * libstdrot.so", not a guess at how to interpret unfamiliar memory. Bump
 * this version (and the entrypoint name/number together) again the next
 * time StdrotEntry's layout, StdrotType's numbering, or StdrotAPI's own
 * shape changes in a way an old .so's memory could be misread as.
 *
 * v2 -> v3 (Phase 5 Road B, #208) is exactly such a change, twice over:
 *   - StdrotType gained STDROT_STRUCT *before* STDROT_NONE rather than
 *     after it, so STDROT_NONE's integer value moved. Appending at the
 *     end would have preserved the old numbering, but it would also have
 *     put a real value type after the "void return" sentinel, and every
 *     switch in this codebase that ends at STDROT_NONE reads in that
 *     order; a renumber the version guard already catches is cheaper
 *     than a permanently confusing enum.
 *   - StdrotValue's union grew a 3-word member (val.blob), widening the
 *     whole struct. StdrotValue is passed and returned BY VALUE across
 *     this boundary, so a v2 .so and a v3 host disagree about the size
 *     of every argument slot and every return -- the single most
 *     dangerous kind of silent mismatch this guard exists to prevent. */
#define STDROT_ABI_VERSION 3

StdrotAPI stdrot_get_api_v3(void);

#endif /* STDROT_API_H */
