/* stdrot.c - Standard Brainrot library loader and AST bridge
 *
 * This file is the glue between:
 *   • the AST/interpreter (understands ASTNode, ArgumentList, Variable, etc.)
 *   • the stdrot implementations (pure I/O functions, zero interpreter
 *     dependency), reached one of two ways depending on STDROT_STATIC:
 *
 *   STDROT_STATIC undefined (default, native build):
 *     the stdrot sources are compiled into libstdrot.so and dlopen'd at
 *     runtime.
 *     1. Dynamic loader (stdrot_load/unload) that opens libstdrot.so and
 *        discovers all functions via stdrot_get_api_v3()
 *     2. Thin varargs stubs (yapping/yappin/baka) and per-type slorp/
 *        ragequit/chill stubs that dlsym their real implementation by name
 *        on first use
 *
 *   STDROT_STATIC defined (wasm build, see `make wasm`):
 *     the stdrot sources are compiled directly into the same binary —
 *     there is no .so and no dlopen surface at all (wasm has no dynamic
 *     loader worth using for a single-artifact build). stdrot_load() calls
 *     stdrot_get_api_v3() directly, and ragequit/chill/slorp_* are provided
 *     solely by their stdrot definitions — this file only keeps the
 *     yapping/yappin/baka varargs stubs, redirecting them straight to
 *     v_yapping/v_yappin/v_baka instead of looking them up by name.
 *
 *   3. AST bridge functions (execute_*_call) that evaluate arguments and
 *      call the raw implementations — identical in both modes.
 */

#include "stdrot.h"
#include "ast.h"
#include "lib/mem.h"
#include "lib/module_path.h" /* MODULE_NATIVE_LOADER: can this build load a
                              * #cooked <name> native module? (Shared with
                              * module_path.c and lang.l -- one definition.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifndef STDROT_STATIC
#include <dlfcn.h> /* core library loader (dlopen/dlsym) -- POSIX native build */
#elif defined(_WIN32)
#include <stdint.h> /* uintptr_t, for the GetProcAddress round-trip below */
#include <windows.h> /* Win32 native-module loader (LoadLibraryA/GetProcAddress) */
#endif

/* ── Native-module loader shim ────────────────────────────────────────────
 * One trio -- open/sym/close -- over dlopen and the Win32 loader, so
 * stdrot_load_module() and stdrot_unload() read identically on both. The
 * existing void* handle fields hold either a dlopen handle or an HMODULE
 * (a pointer). Only compiled where a loader exists. */
#ifdef MODULE_NATIVE_LOADER
#if defined(_WIN32)
static void *br_module_open(const char *path)
{
    return (void *)LoadLibraryA(path);
}
static void *br_module_sym(void *handle, const char *name)
{
    /* GetProcAddress returns FARPROC; round-trip through uintptr_t to a data
     * pointer, the same shape dlsym() hands back. */
    return (void *)(uintptr_t)GetProcAddress((HMODULE)handle, name);
}
static void br_module_close(void *handle)
{
    FreeLibrary((HMODULE)handle);
}
/* Formats the last loader error into a static buffer (single-threaded loader
 * path, same as dlerror()'s own not-thread-safe contract). */
static const char *br_module_error(void)
{
    static char buf[256];
    DWORD err = GetLastError();
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             NULL, err, 0, buf, (DWORD)sizeof(buf), NULL);
    if (n == 0)
    {
        snprintf(buf, sizeof(buf), "Win32 error %lu", (unsigned long)err);
    }
    else
    {
        /* Trim the trailing CR/LF FormatMessage appends. */
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        {
            buf[--n] = '\0';
        }
    }
    return buf;
}
#else
/* RTLD_LOCAL, unlike the core library's own dlopen(): a cooked module is
 * looked up entirely by explicit handle (br_module_sym below searches that
 * object only), so its exports -- including its own brainrot_module_init_v3,
 * shared by every module -- never enter the process-wide symbol scope, and
 * two modules exporting that name never collide. */
static void *br_module_open(const char *path)
{
    return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
}
static void *br_module_sym(void *handle, const char *name)
{
    return dlsym(handle, name);
}
static void br_module_close(void *handle)
{
    dlclose(handle);
}
static const char *br_module_error(void)
{
    return dlerror();
}
#endif
#endif /* MODULE_NATIVE_LOADER */

/* ── Global execution context ────────────────────────────────────────────── */
ExecutionContext g_exec_context = {0, {NULL, 0}, {NULL, 0}};

/* ── External interpreter functions ──────────────────────────────────────── */
extern void yyerror(const char *s);
extern String evaluate_expression_string(ASTNode *node);
extern void *evaluate_multi_array_access(ASTNode *node);
extern bool set_bool_variable(const String name, bool value,
                              TypeModifiers mods);
extern bool set_char_variable(const String name, int value, TypeModifiers mods);

/* ── Dynamic library state (native build only) ───────────────────────────── */
#ifndef STDROT_STATIC
static void *lib_handle = NULL;
#endif
static const StdrotEntry *const *functions = NULL;
static int function_count = 0;

#ifndef STDROT_STATIC
/* Symbol cache to avoid repeated dlsym calls */
#define STDROT_CACHE_SIZE 64
typedef struct
{
    String name;
    void *ptr;
} SymbolCache;

static SymbolCache symbol_cache[STDROT_CACHE_SIZE];
static int cache_count = 0;
#endif /* !STDROT_STATIC (core-library dynamic state) */

#ifdef MODULE_NATIVE_LOADER
/* ── Cooked native modules (#cooked <name> resolving to a .so/.dll) ────────
 * A SEPARATE list from the core library's own functions/function_count
 * above, rather than unifying the two: the core lib is always loaded
 * unconditionally, once, before any Brainrot program has even been
 * parsed, and is exercised by nearly every existing test in this repo --
 * keeping it untouched keeps this purely additive, opt-in mechanism from
 * putting that already thoroughly-tested path at risk. is_builtin_
 * function()/get_native_function() below check the core lib first, then
 * this list, in #cooked order.
 *
 * Fixed-size, not realloc'd: STDROT_MAX_COOKED_MODULES mirrors lang.l's
 * own MAX_COOKED_FILES (the actual enforcement point -- lang.l refuses to
 * even resolve a name once its shared visited-file/module budget is
 * exhausted, so this array can never be asked to hold more than that many
 * entries in practice). The bounds check in stdrot_load_module() below is
 * defense in depth, the same relationship validate_native_registry() has
 * to the semantic analyzer's own already-enforced checks. */
#define STDROT_MAX_COOKED_MODULES 128

typedef struct
{
    void *handle;
    char *name; /* the #cooked <name> spelling, for diagnostics */
    const StdrotEntry *const *functions;
    int function_count;
} LoadedNativeModule;

static LoadedNativeModule cooked_modules[STDROT_MAX_COOKED_MODULES];
static int cooked_module_count = 0;

/* An in-flight module handle stdrot_load_module() has dlopen'd but not yet
 * either closed itself or fully committed into cooked_modules[] -- see
 * that function's own comment on why this exists. NULL whenever no
 * stdrot_load_module() call is in progress. */
static void *pending_module_handle = NULL;
#endif /* MODULE_NATIVE_LOADER */

#ifdef STDROT_STATIC
/* Statically linked in from stdrot/yapping.c and stdrot/baka.c — called
 * directly below instead of going through dlsym-by-name.
 * stdrot_get_api_v3() (statically linked from stdrot/registry.c) is
 * already declared by stdrot_api.h, included transitively via stdrot.h
 * above. */
extern void v_yapping(const char *fmt, va_list ap);
extern void v_yappin(const char *fmt, va_list ap);
extern void v_baka(const char *fmt, va_list ap);
#else

/* ── Dynamic symbol lookup with caching ──────────────────────────────────── */

static void *stdrot_lookup_symbol(const String symbol_name)
{
    if (!lib_handle || !symbol_name.data)
        return NULL;

    /* Check cache first */
    for (int i = 0; i < cache_count; i++)
    {
        if (strcmp(symbol_cache[i].name.data, symbol_name.data) == 0)
        {
            return symbol_cache[i].ptr;
        }
    }

    /* Not in cache, lookup via dlsym */
    void *ptr = dlsym(lib_handle, symbol_name.data);
    if (ptr && cache_count < STDROT_CACHE_SIZE)
    {
        symbol_cache[cache_count].name.data = symbol_name.data;
        symbol_cache[cache_count].name.len = symbol_name.len;
        symbol_cache[cache_count].ptr = ptr;
        cache_count++;
    }

    return ptr;
}
#endif /* STDROT_STATIC */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

/* ── Registry validation ───────────────────────────────────────────────────
 * A malformed StdrotEntry is a bug in the native binding itself (a hand-
 * written STDROT_EXPORT_SIG (or STDROT_EXPORT_SIG_IDENTITY) invocation with
 * inconsistent arguments), never something a Brainrot program can trigger
 * -- so, like every other native-binding bug this ABI catches (enforce_
 * return_type()/enforce_arg_type()), it should fail loudly and immediately
 * rather than surfacing piecemeal depending on which call path happens to
 * touch the broken field first. Run once, right after the registry loads,
 * instead of at each individual call site.
 *
 * What "validated" means here: every field is internally coherent (in
 * range, self-consistent with the other fields on the same descriptor --
 * pointer_level only meaningful alongside STDROT_PTR, return_like_arg only
 * meaningful naming a STDROT_ANY mandatory argument, and so on). It does
 * NOT mean every *capability* a structurally-valid descriptor could
 * describe is actually implemented end to end. STDROT_CSTRING as a
 * *return* type is a real, well-formed StdrotType value -- reserved
 * groundwork for a capability this ABI hasn't finished (see
 * stdrot_api.h's own STDROT_CSTRING comment) -- not malformed metadata.
 * STDROT_HANDLE was in that set until #213 and no longer is: handles are
 * now implemented in both directions. A descriptor using either loads
 * successfully; semantic_check_native_call() (semantic_analyzer.c)
 * rejects any *call* to it before that call could ever reach a marshaller
 * with no code path to honor it. That is a deliberate two-tier design,
 * not a gap in this function: collapsing "structurally coherent" and
 * "fully implemented" into one load-time check would mean a single .so
 * exporting one reserved-but-unimplemented capability refuses to load at
 * all, taking down every unrelated, fully-supported native alongside it,
 * for a call that might never even happen. */
/* An intentionally generous upper bound on how many natives a single
 * libstdrot.so could plausibly export -- this codebase ships roughly a
 * dozen production natives plus another dozen or so test-only ones; four
 * orders of magnitude more than that is not "a large but legitimate
 * library," it's a corrupt or hostile StdrotAPI.count that would
 * otherwise send validate_native_registry()'s loop below walking through
 * arbitrary address space one StdrotEntry pointer at a time. */
#define STDROT_MAX_PLAUSIBLE_FUNCTION_COUNT 4096

static void validate_native_registry(const StdrotEntry *const *functions,
                                     int function_count)
{
    /* Validate the table itself before ever indexing into it --
       stdrot_get_api_v3() (registry.c) is trusted to return a StdrotAPI
       shaped the way this header currently declares, but "the ABI is
       versioned" only means the STRUCT LAYOUT is trustworthy, not that
       every possible bit pattern inside it is coherent. A negative
       count would make the loop below execute zero times, silently
       accepting a nonsensical table as if it legitimately had no
       exports; a positive count paired with a NULL functions pointer,
       or an implausibly large count (a corrupt or hostile library,
       intentionally or not), would walk this loop into a NULL
       dereference or arbitrary out-of-bounds memory well before any
       individual entry's own fields are ever examined. */
    if (function_count < 0 ||
        function_count > STDROT_MAX_PLAUSIBLE_FUNCTION_COUNT)
    {
        fprintf(stderr,
                "stdrot: registry function_count (%d) is not a plausible "
                "value (expected 0 <= count <= %d)\n",
                function_count, STDROT_MAX_PLAUSIBLE_FUNCTION_COUNT);
        exit(1);
    }
    if (function_count > 0 && !functions)
    {
        fprintf(stderr,
                "stdrot: registry function_count (%d) is > 0 but "
                "functions is NULL\n",
                function_count);
        exit(1);
    }

    for (int i = 0; i < function_count; i++)
    {
        const StdrotEntry *entry = functions[i];
        if (!entry || !entry->name)
        {
            fprintf(stderr, "stdrot: registry entry %d has no name\n", i);
            exit(1);
        }
        if (entry->return_type.type < STDROT_ANY ||
            entry->return_type.type > STDROT_NONE)
        {
            fprintf(stderr,
                    "stdrot: native '%s': return_type.type (%d) is not a "
                    "valid StdrotType\n",
                    entry->name, (int)entry->return_type.type);
            exit(1);
        }
        if (entry->min_args < 0 || entry->param_count < 0)
        {
            fprintf(stderr,
                    "stdrot: native '%s': min_args (%d) and param_count "
                    "(%d) must both be >= 0\n",
                    entry->name, entry->min_args, entry->param_count);
            exit(1);
        }
        if (entry->min_args > entry->param_count)
        {
            fprintf(stderr,
                    "stdrot: native '%s': min_args (%d) cannot exceed "
                    "param_count (%d)\n",
                    entry->name, entry->min_args, entry->param_count);
            exit(1);
        }
        if (entry->param_count > 0 && !entry->params)
        {
            fprintf(stderr,
                    "stdrot: native '%s': param_count (%d) > 0 but params "
                    "is NULL\n",
                    entry->name, entry->param_count);
            exit(1);
        }
        /* Safe to dereference entry->params[p] from here on: the NULL
           check just above guarantees it's non-NULL whenever param_count
           > 0 (the only way this loop actually iterates). */
        for (int p = 0; p < entry->param_count; p++)
        {
            if (entry->params[p].type < STDROT_ANY ||
                entry->params[p].type > STDROT_NONE)
            {
                fprintf(stderr,
                        "stdrot: native '%s': params[%d].type (%d) is not "
                        "a valid StdrotType\n",
                        entry->name, p, (int)entry->params[p].type);
                exit(1);
            }
        }
        if (!entry->fn)
        {
            fprintf(stderr, "stdrot: native '%s': fn is NULL\n", entry->name);
            exit(1);
        }
        if (entry->promote_variadic_tail && !entry->is_variadic)
        {
            /* promote_variadic_tail (stdrot_api.h) only means something
               for arguments beyond param_count -- there's no "tail" to
               promote at all when is_variadic is false. */
            fprintf(stderr,
                    "stdrot: native '%s': promote_variadic_tail is true "
                    "but is_variadic is false\n",
                    entry->name);
            exit(1);
        }
        if (entry->return_type.pointer_level < 0)
        {
            fprintf(stderr,
                    "stdrot: native '%s': return_type.pointer_level (%d) "
                    "must be >= 0\n",
                    entry->name, entry->return_type.pointer_level);
            exit(1);
        }
        /* pointer_level is only meaningful stacked on top of STDROT_PTR
           (StdrotParam's own comment, stdrot_api.h: "{STDROT_PTR, NULL,
           N} describes N levels of indirection on top of whatever
           pointer_level counts" -- {STDROT_PTR, NULL, 0} is one pointer
           level, {STDROT_PTR, NULL, 1} is two, and so on). A non-PTR base
           type with a nonzero pointer_level is an internally
           contradictory descriptor: static checking (semantic_analyzer.c)
           reads type + pointer_level directly and would approve it as an
           ordinary N-level pointer to that base type, but runtime
           marshalling (ast_expr_to_stdrot_value()) tags ANY expression
           with pointer_level > 0 as STDROT_PTR regardless of declared
           base type -- so the argument would always arrive tagged
           STDROT_PTR while the descriptor insists it's something else,
           and enforce_arg_type()/enforce_return_type() would reject
           every call this "valid" descriptor was supposed to describe.
           Reject the contradiction at the source instead of certifying a
           descriptor nothing downstream can actually honor. */
        if (entry->return_type.type != STDROT_PTR &&
            entry->return_type.pointer_level != 0)
        {
            fprintf(stderr,
                    "stdrot: native '%s': return_type.pointer_level (%d) "
                    "must be 0 when return_type.type isn't STDROT_PTR\n",
                    entry->name, entry->return_type.pointer_level);
            exit(1);
        }
        for (int p = 0; p < entry->param_count; p++)
        {
            if (entry->params[p].pointer_level < 0)
            {
                fprintf(stderr,
                        "stdrot: native '%s': params[%d].pointer_level "
                        "(%d) must be >= 0\n",
                        entry->name, p, entry->params[p].pointer_level);
                exit(1);
            }
            if (entry->params[p].type != STDROT_PTR &&
                entry->params[p].pointer_level != 0)
            {
                fprintf(stderr,
                        "stdrot: native '%s': params[%d].pointer_level "
                        "(%d) must be 0 when params[%d].type isn't "
                        "STDROT_PTR\n",
                        entry->name, p, entry->params[p].pointer_level, p);
                exit(1);
            }
            /* A parameter's type describes what representation it
               consumes; STDROT_NONE ("void return") only makes sense as
               a RETURN type -- a parameter that "consumes void" cannot
               coherently accept an actual argument. Zero-argument
               natives are already expressed via param_count == 0; a
               STDROT_NONE-typed parameter slot is never necessary and
               would leave both static checking and enforce_arg_type()
               with no coherent rule for what value could ever satisfy
               it. */
            if (entry->params[p].type == STDROT_NONE)
            {
                fprintf(stderr,
                        "stdrot: native '%s': params[%d].type must not be "
                        "STDROT_NONE -- a void-typed parameter can't "
                        "consume an argument; use param_count to express "
                        "zero arguments instead\n",
                        entry->name, p);
                exit(1);
            }
            /* A by-value aggregate is only checkable against its tag:
               `gang Vector2 {chad x, y;}` and `gang Size {chad w, h;}`
               have identical size and alignment, so a STDROT_STRUCT
               descriptor with no type_name leaves both static checking
               (semantic_check_native_call()) and enforce_arg_type() with
               nothing to compare but a byte count that cannot tell them
               apart -- the native would silently receive whichever
               8-byte struct the caller happened to pass. Reject the
               descriptor at load time rather than certify a parameter
               nothing downstream can honestly type-check. Empty is
               rejected alongside NULL: "" matches no `gang` tag, so it
               is a descriptor that can never accept any argument at
               all, which is a bug in the binding, not a usable
               signature. */
            if (entry->params[p].type == STDROT_STRUCT &&
                (!entry->params[p].type_name ||
                 entry->params[p].type_name[0] == '\0'))
            {
                fprintf(stderr,
                        "stdrot: native '%s': params[%d].type is "
                        "STDROT_STRUCT but type_name is missing -- a "
                        "by-value struct parameter must name the "
                        "gang/chungus tag it accepts (size alone cannot "
                        "distinguish two same-sized structs)\n",
                        entry->name, p);
                exit(1);
            }
        }
        /* return_like_arg (StdrotEntry's own comment, stdrot_api.h): -1
           means "not identity-polymorphic," otherwise it must name a
           MANDATORY argument -- an identity relationship ("same type as
           argument N") is meaningless if argument N might not be
           supplied. Catches e.g. STDROT_EXPORT_SIG_IDENTITY(name, fn,
           params, 1, 0) -- one optional param, zero mandatory ones, so
           .return_like_arg = 0 (hardcoded by that macro) could point at
           an argument that was never actually passed. */
        if (entry->return_like_arg != -1 &&
            (entry->return_like_arg < 0 ||
             entry->return_like_arg >= entry->min_args))
        {
            fprintf(stderr,
                    "stdrot: native '%s': return_like_arg (%d) must be -1 "
                    "or a mandatory argument index (0 <= return_like_arg "
                    "< min_args = %d)\n",
                    entry->name, entry->return_like_arg, entry->min_args);
            exit(1);
        }
        /* return_like_arg must name a STDROT_ANY parameter, full stop.
           "identity-polymorphic" only means something coherent if the
           parameter itself carries no fixed representation to coerce
           into -- T -> T, with T decided entirely by the caller. If the
           parameter is a fixed type (STDROT_CSTRING, STDROT_DOUBLE,
           STDROT_PTR, ...), static analysis infers the result type from
           the *source* expression (before coercion) while the runtime
           marshaller/enforce_return_type() see the argument *after*
           parameter coercion -- two different types for the same call
           whenever the source type and the fixed parameter type differ
           (STRING literal coerced to CSTRING, INT coerced to DOUBLE, a
           bare pointer with no STDROT_PTR-shaped static inference,
           etc). Forcing STDROT_ANY here removes the coercion step
           entirely, so there is only one type to agree on. A native that
           genuinely wants "same value, fixed type" should declare that
           fixed type as an ordinary (non-identity) return_type instead
           of borrowing this mechanism. */
        if (entry->return_like_arg != -1 &&
            entry->params[entry->return_like_arg].type != STDROT_ANY)
        {
            fprintf(stderr,
                    "stdrot: native '%s': return_like_arg (%d) names a "
                    "parameter that isn't STDROT_ANY -- identity-"
                    "polymorphic natives may only alias a STDROT_ANY "
                    "parameter, since any fixed parameter type undergoes "
                    "coercion that static inference and runtime "
                    "enforcement would then disagree about\n",
                    entry->name, entry->return_like_arg);
            exit(1);
        }
        for (int j = 0; j < i; j++)
        {
            if (functions[j] && functions[j]->name &&
                strcmp(functions[j]->name, entry->name) == 0)
            {
                fprintf(stderr,
                        "stdrot: duplicate native export '%s' -- "
                        "get_native_function() would silently resolve "
                        "every call to whichever entry happens to come "
                        "first in the linker section\n",
                        entry->name);
                exit(1);
            }
        }
    }
}

/* ── Loader ──────────────────────────────────────────────────────────────── */

#ifdef STDROT_STATIC

void stdrot_load(void)
{
    /* stdrot/registry.c is linked directly into this binary, so the
     * function table is just a direct call away — no loader needed, and
     * no dlsym-based version check either: this is a single statically
     * linked binary, compiled from one copy of stdrot_api.h, so the ABI
     * mismatch stdrot_get_api_v3()'s naming exists to catch (an old
     * libstdrot.so loaded by a new host, see STDROT_ABI_VERSION's own
     * comment) is structurally impossible here. */
    StdrotAPI api = stdrot_get_api_v3();
    functions = api.functions;
    function_count = api.count;
    validate_native_registry(functions, function_count);
}

#else

void stdrot_load(void)
{
    /* First, make main binary symbols available to subsequently loaded
     * libraries by loading the main program's symbols with RTLD_GLOBAL
     */
    dlopen(NULL, RTLD_LAZY | RTLD_GLOBAL);

    /* STDROT_LIB_PATH, when set, names an exact library to load instead --
     * used exclusively by `make test`/`make valgrind` and CI's test job to
     * point at tests/libstdrot.so (production natives plus test-only ones
     * from tests/stdrot/, see that directory's own comment) without ever
     * touching the plain "./libstdrot.so" lookup below, which is what
     * `make install` and every ordinary invocation of this binary still
     * resolve to. Unset in normal use, so this changes nothing for anyone
     * not explicitly opting into a different library. */
    const char *lib_path_override = getenv("STDROT_LIB_PATH");
    if (lib_path_override)
    {
        lib_handle = dlopen(lib_path_override, RTLD_LAZY | RTLD_GLOBAL);
        if (!lib_handle)
        {
            fprintf(stderr, "Failed to load STDROT_LIB_PATH=%s: %s\n",
                    lib_path_override, dlerror());
            exit(EXIT_FAILURE);
        }
    }

    /* Try cwd-relative ./libstdrot.so first, then the dynamic linker's
     * search path. Release builds add rpath so the leaf-name lookup can find
     * libstdrot.so next to the binary after the cwd lookup misses. Use
     * RTLD_GLOBAL so the library can access symbols from the main binary
     * (e.g., g_exec_context). */
    if (!lib_handle)
    {
        lib_handle = dlopen("./libstdrot.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (!lib_handle)
    {
        lib_handle = dlopen("libstdrot.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (!lib_handle)
    {
        fprintf(stderr, "Failed to load libstdrot.so: %s\n", dlerror());
        exit(EXIT_FAILURE);
    }

    /* Get the API entrypoint -- by its versioned name (STDROT_ABI_VERSION,
       stdrot_api.h), never the pre-v2 "stdrot_get_api". A libstdrot.so
       built before this ABI existed (StdrotEntry == {name, fn}, no
       STDROT_ANY at StdrotType index 0, registry section holding
       StdrotEntry structs directly rather than pointers to them) simply
       doesn't export this symbol -- dlsym() fails the lookup cleanly,
       instead of finding an old stdrot_get_api() under the old name and
       calling it as if its StdrotAPI were shaped like this version's.
       That would silently reinterpret the old .so's actual memory (e.g.
       an entry's own `name` field bytes) as this version's `functions`
       array of StdrotEntry POINTERS -- exactly the class of ABI-version
       confusion this rename exists to make structurally impossible to
       reach, not just unlikely. */
    StdrotAPI (*get_api)(void);
    *(void **)(&get_api) = dlsym(lib_handle, "stdrot_get_api_v3");
    if (!get_api)
    {
        fprintf(stderr,
                "libstdrot.so is missing stdrot_get_api_v3() -- it was "
                "built against an incompatible stdrot ABI (expected "
                "STDROT_ABI_VERSION %d). Rebuild libstdrot.so from this "
                "checkout (`make lib`) before running this binary.\n",
                STDROT_ABI_VERSION);
        dlclose(lib_handle);
        /* exit() below runs every registered atexit handler, including
           stdrot_unload() (atexit(stdrot_unload), lang.y) -- which would
           otherwise dlclose() this same, already-closed handle again
           (its own guard is `if (lib_handle)`, which does nothing to
           protect against a stale pointer this function itself already
           passed to dlclose()). A pre-existing bug in this exact error
           path, uncovered by tests/old_abi_sim's fixture -- confirmed
           via valgrind (invalid reads inside glibc's own _dl_close,
           deep in freed loader bookkeeping) before this fix, clean
           after it. */
        lib_handle = NULL;
        exit(EXIT_FAILURE);
    }

    /* Discover all functions */
    StdrotAPI api = get_api();
    functions = api.functions;
    function_count = api.count;
    validate_native_registry(functions, function_count);
}

#endif /* STDROT_STATIC -- core-library load path */

/* Unloads the core library (dynamic-core builds only -- Windows and wasm
 * compile the core in, so there is no lib_handle to close there) and every
 * #cooked native module (wherever a module loader exists). Two independent
 * guards, not one: on Windows the core is static yet modules still load via
 * br_module_open() and must be freed here. */
void stdrot_unload(void)
{
#ifndef STDROT_STATIC
    if (lib_handle)
    {
        dlclose(lib_handle);
        lib_handle = NULL;
        functions = NULL;
        function_count = 0;
        cache_count = 0;
    }
#else
    functions = NULL;
    function_count = 0;
#endif
#ifdef MODULE_NATIVE_LOADER
    for (int i = 0; i < cooked_module_count; i++)
    {
        br_module_close(cooked_modules[i].handle);
        free(cooked_modules[i].name);
    }
    cooked_module_count = 0;
    if (pending_module_handle)
    {
        /* stdrot_load_module() exited (e.g. via validate_native_registry())
           before either closing this handle itself or committing it into
           cooked_modules[] above -- see that function's own comment. */
        br_module_close(pending_module_handle);
        pending_module_handle = NULL;
    }
#endif
}

#ifdef MODULE_NATIVE_LOADER

/* Describes whichever already-registered source (the core library, or an
 * earlier #cooked module) provides `func_name` -- used only to name that
 * source in stdrot_load_module()'s duplicate-export diagnostic below.
 * Caller must already know func_name IS registered somewhere (e.g. via
 * is_builtin_function()); returns a generic fallback description otherwise,
 * which should be unreachable in practice. */
static const char *describe_native_source(const char *func_name)
{
    for (int i = 0; i < function_count; i++)
    {
        if (strcmp(func_name, functions[i]->name) == 0)
        {
            return "the core standard library";
        }
    }
    for (int m = 0; m < cooked_module_count; m++)
    {
        for (int i = 0; i < cooked_modules[m].function_count; i++)
        {
            if (strcmp(func_name, cooked_modules[m].functions[i]->name) == 0)
            {
                return cooked_modules[m].name;
            }
        }
    }
    return "another already-loaded source";
}

/* Loads a native module (a .so resolved from #cooked <name>, module_path.c)
 * and registers its functions alongside the core library's. `name` is the
 * #cooked <name> the user wrote; `so_path` is the already-resolved absolute
 * path. Exits with a diagnostic on any failure -- dlopen, a missing/
 * incompatible brainrot_module_init_v3, a malformed registry, or a name
 * already provided by the core library or an earlier #cooked module -- the
 * same fail-loud posture stdrot_load() already has for the core library:
 * none of these are something a Brainrot program can trigger or recover
 * from, and every existing ABI-enforcement function in this file already
 * treats that class of failure as exit(1), not a value to propagate. */
void stdrot_load_module(const char *name, const char *so_path)
{
    if (cooked_module_count >= STDROT_MAX_COOKED_MODULES)
    {
        fprintf(stderr,
                "stdrot: too many distinct #cooked files/modules (max %d)\n",
                STDROT_MAX_COOKED_MODULES);
        exit(1);
    }

    /* br_module_open() loads the module in isolation -- RTLD_LOCAL on POSIX,
       and the Win32 loader's per-module handle scope -- so its exports never
       enter the process-wide symbol namespace. That matters most for the
       module's OWN brainrot_module_init_v3: every cooked module (built with
       -DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init_v3) exports one under
       that exact name, and isolation is why two of them are never a collision,
       regardless of load order -- not because the name happens to be unique
       (it isn't). br_module_sym() below looks the entrypoint up on this
       handle specifically, never globally. See the shim near the top. */
    void *handle = br_module_open(so_path);
    if (!handle)
    {
        fprintf(stderr, "Error: cannot load module '%s' (%s): %s\n", name,
                so_path, br_module_error());
        exit(1);
    }
    /* Recorded before this handle is fully validated, for the same reason
       PendingNativeCallArgs (above) tracks an in-flight native call's own
       scratch before it's done with it: validate_native_registry() below
       can itself exit(1) on a malformed table, and that exit() doesn't
       unwind this function's stack -- without this, `handle` would still
       be open (mmap'd, not merely a heap pointer, so nothing else in this
       file's cleanup would ever see it) with nothing tracking it for
       stdrot_unload() (atexit(stdrot_unload), lang.y) to dlclose. Cleared
       on every path out of this function, success or failure, so it never
       describes a handle this function itself already closed or handed
       off to cooked_modules[]. */
    pending_module_handle = handle;

    /* Same versioned-entrypoint discipline as stdrot_get_api_v3() above,
       for the same reason: a module built against a stdrot_api.h whose
       layout has since changed must fail this dlsym() cleanly, not have
       its actual memory misread as the current shape.

       This symbol carried NO version suffix through ABI v2, on the
       reasoning that there was no prior, differently-shaped
       "brainrot_module_init" to disambiguate from -- with the standing
       caveat that a future incompatible change would have to rename it
       the same way stdrot_get_api itself was renamed. ABI v3
       (STDROT_STRUCT, #208) is that change, and it is worth being
       precise about why, because the usual tell was absent: StdrotAPI,
       StdrotEntry and StdrotParam all kept their exact v2 layouts, so a
       stale module's function TABLE would have been read back
       correctly. What changed is the calling convention on the other
       side of that table -- StdrotValue gained val.blob and grew from 24
       to 32 bytes, and StdrotType renumbered STDROT_NONE out from under
       every v2-compiled switch. A v2 module would therefore have loaded
       silently and then had every argument and return value passed at
       the wrong width: memory corruption on the very first call, with no
       diagnostic anywhere. Renaming to _v3 turns that into the loud
       dlsym() failure below.

       The lesson for the next bump: this symbol needs renaming whenever
       ANYTHING crossing it changes shape -- StdrotValue and StdrotType
       included -- not only when StdrotAPI/StdrotEntry do. */
    StdrotAPI (*module_init)(void);
    *(void **)(&module_init) = br_module_sym(handle, "brainrot_module_init_v3");
    if (!module_init)
    {
        fprintf(stderr,
                "Error: module '%s' (%s) does not export "
                "brainrot_module_init_v3() -- it was built against an "
                "incompatible or missing module ABI (expected "
                "STDROT_ABI_VERSION %d). Rebuild this module against the "
                "current stdrot_api.h.\n",
                name, so_path, STDROT_ABI_VERSION);
        br_module_close(handle);
        pending_module_handle = NULL;
        exit(1);
    }

    StdrotAPI api = module_init();
    validate_native_registry(api.functions, api.count);

    /* validate_native_registry() only proved this module's OWN table is
       internally coherent -- it has no way to know about the core library
       or any module cooked earlier in this same compilation. Without this,
       a name colliding with an existing export would silently resolve to
       whichever source happened to register it first, making a call's
       target depend on #cooked order instead of on the program's own
       text -- the exact class of ambiguity validate_native_registry()'s
       own within-one-table duplicate check exists to reject, just across
       tables instead of within one. */
    for (int i = 0; i < api.count; i++)
    {
        const char *entry_name = api.functions[i]->name;
        const String probe = {.data = (char *)entry_name,
                              .len = strlen(entry_name)};
        if (is_builtin_function(probe))
        {
            fprintf(stderr,
                    "Error: module '%s' (%s): native export '%s' is "
                    "already provided by %s\n",
                    name, so_path, entry_name,
                    describe_native_source(entry_name));
            br_module_close(handle);
            pending_module_handle = NULL;
            exit(1);
        }
    }

    char *name_copy = strdup(name);
    if (!name_copy)
    {
        fprintf(stderr, "out of memory\n");
        br_module_close(handle);
        pending_module_handle = NULL;
        exit(1);
    }

    cooked_modules[cooked_module_count].handle = handle;
    cooked_modules[cooked_module_count].name = name_copy;
    cooked_modules[cooked_module_count].functions = api.functions;
    cooked_modules[cooked_module_count].function_count = api.count;
    cooked_module_count++;
    pending_module_handle =
        NULL; /* ownership transferred to cooked_modules[] */
}

#else /* !MODULE_NATIVE_LOADER */

/* wasm has no dynamic loader worth using (see this file's own top comment)
 * -- module_path_resolve() (module_path.c) never resolves a #cooked <name>
 * to a native module in this build, so this should never actually be
 * called here. Fails loudly instead of silently doing nothing, on the same
 * principle as every other ABI-enforcement function in this file: a path
 * that's "supposed to be unreachable" still needs to fail safely if it's
 * ever reached anyway (a module_path.c bug, or a future caller that
 * doesn't route through the resolver), not corrupt state or crash. */
void stdrot_load_module(const char *name, const char *so_path)
{
    (void)so_path;
    fprintf(stderr,
            "Error: cannot load native module '%s' -- native modules are "
            "not supported in this build (no dynamic loader)\n",
            name);
    exit(1);
}

#endif /* MODULE_NATIVE_LOADER */

/* ── Runtime query ──────────────────────────────────────────────────────────
 */

bool is_builtin_function(const String func_name)
{
    if (!func_name.data)
        return false;

    for (int i = 0; i < function_count; i++)
    {
        if (strcmp(func_name.data, functions[i]->name) == 0)
        {
            return true;
        }
    }
#ifdef MODULE_NATIVE_LOADER
    for (int m = 0; m < cooked_module_count; m++)
    {
        for (int i = 0; i < cooked_modules[m].function_count; i++)
        {
            if (strcmp(func_name.data, cooked_modules[m].functions[i]->name) ==
                0)
            {
                return true;
            }
        }
    }
#endif
    return false;
}

const StdrotEntry *get_native_function(const String func_name)
{
    if (!func_name.data)
        return NULL;

    for (int i = 0; i < function_count; i++)
    {
        if (strcmp(func_name.data, functions[i]->name) == 0)
        {
            return functions[i];
        }
    }
#ifdef MODULE_NATIVE_LOADER
    for (int m = 0; m < cooked_module_count; m++)
    {
        for (int i = 0; i < cooked_modules[m].function_count; i++)
        {
            if (strcmp(func_name.data, cooked_modules[m].functions[i]->name) ==
                0)
            {
                return cooked_modules[m].functions[i];
            }
        }
    }
#endif
    return NULL;
}

VarType stdrot_type_to_vartype(StdrotType type)
{
    switch (type)
    {
    case STDROT_INT:
        return VAR_INT;
    case STDROT_FLOAT:
        return VAR_FLOAT;
    case STDROT_DOUBLE:
        return VAR_DOUBLE;
    case STDROT_SHORT:
        return VAR_SHORT;
    case STDROT_BOOL:
        return VAR_BOOL;
    case STDROT_CHAR:
        return VAR_CHAR;
    case STDROT_STRING:
    case STDROT_CSTRING:
        return VAR_STRING;
    case STDROT_PTR:
        /* A real, known category -- "opaque pointer, base type
           intentionally erased" -- not NONE ("unknown, skip checking").
           See VAR_PTR's own comment in ast.h for why conflating the two
           would silently defeat every "type == NONE, don't validate"
           shortcut this analyzer already relies on. */
        return VAR_PTR;
    case STDROT_NONE:
        /* Same reasoning as STDROT_PTR/VAR_PTR just above, for the exact
           same class of bug: STDROT_NONE means a native's descriptor
           return type genuinely is void -- known with total certainty to
           produce no value -- not "unknown, don't validate." Mapping it
           to plain NONE meant `rizz x = a_void_native();` type-checked,
           because every "type == NONE, fail open" shortcut in this
           analyzer treated a certainly-void expression as an unknowable
           one. See VAR_VOID's own comment (ast.h) for the full
           reasoning. */
        return VAR_VOID;
    case STDROT_STRUCT:
        /* A real, known category, like STDROT_PTR above and unlike
           STDROT_HANDLE below: an aggregate passed by value has a
           genuine Brainrot type (VAR_STRUCT) that the analyzer can and
           must check the argument against. The base VarType alone is
           not the whole check, though -- two different `gang`s are both
           VAR_STRUCT -- so semantic_check_native_call() compares the
           tag (StdrotParam.type_name) separately rather than relying on
           this mapping to distinguish them. */
        return VAR_STRUCT;
    case STDROT_HANDLE:
        /* An opaque native resource (#213). VAR_PTR, for the same reason
           STDROT_PTR is: from the type system's point of view a handle IS
           an address whose base type is deliberately erased, and VAR_PTR
           is exactly that category. What distinguishes a handle from a
           plain pointer is not its Brainrot type but its `kind` tag and
           the owning library's live-handle registry -- see
           STDROT_HANDLE's own comment in stdrot_api.h. enforce_arg_type()
           compares the tags separately, the same way it does for
           STDROT_STRUCT, because VAR_PTR alone cannot tell two kinds
           apart. */
        return VAR_PTR;
    case STDROT_ANY:
    default:
        /* Genuinely unknown representations -- STDROT_ANY (legacy export,
           real type not statically knowable) correctly remains NONE,
           distinct from STDROT_NONE's VAR_VOID just above. */
        return NONE;
    }
}

bool stdrot_char_narrows_to_int(NodeType node_type, OperatorType unary_op)
{
    switch (node_type)
    {
    case NODE_UNARY_OPERATION:
        switch (unary_op)
        {
        case OP_DEREFERENCE:
        case OP_PRE_INC:
        case OP_PRE_DEC:
        case OP_POST_INC:
        case OP_POST_DEC:
            /* Dereferencing a char* yields an ordinary char lvalue with
               no promotion of its own -- the pointed-to object's type IS
               char, the same as any other char lvalue. Pre/post
               increment and decrement are defined (C11 6.5.2.4,
               6.5.3.1 -- "as if by" c = c +/- 1, via 6.5.16.2's
               assignment conversion) to convert the result back to the
               operand's own type before the expression's value is
               determined: the *expression's type* is that of the
               operand (char), even though the addition/subtraction
               arithmetic itself happens in int. Neither belongs in the
               narrowing set. */
            return false;
        case OP_ADDRESS_OF:
        /* Unreachable in practice: ast_expr_to_stdrot_value()'s
           pointer-level check (its own comment) already routes any
           pointer_level > 0 expression, &c included, through STDROT_PTR
           before this predicate is ever consulted for a "does this stay
           char" question. Included here, narrowing, purely so a
           hypothetical direct caller gets a defensible answer -- &c's
           result plainly isn't a char value to begin with. */
        default:
            /* OP_NEG (unary arithmetic negation) undergoes the same
               integer promotion as any other arithmetic operator --
               this narrowing is genuine, correct C semantics, unlike
               the array/struct-access cases removed below. */
            return true;
        }
    case NODE_OPERATION:
        /* Binary arithmetic and comparison both apply the usual
           arithmetic conversions, which promote a char operand to int
           before the operator itself runs -- the *value* underwent
           promotion here, not just the storage location it came from,
           so this narrowing is correct C semantics. */
        return true;
    default:
        /* NODE_ARRAY_ACCESS, NODE_STRUCT_ACCESS (and everything else --
           identifiers, literals, a native call's own declared result):
           a subscript or member-access expression is an ordinary lvalue
           of the accessed element/field's own declared type -- `buf[0]`
           and `foo.c` both have type char in C, full stop, the same as
           a bare identifier naming a char variable. C only promotes a
           char ARGUMENT to int for an unprototyped function call or the
           variadic tail of a variadic one (default argument promotions,
           C11 6.5.2.2p6-7) -- never merely because the argument
           expression happens to be an array subscript or a member-
           access, when it's being passed to a normally-prototyped
           parameter, which is every one of Brainrot's fixed-signature
           native calls. This function previously narrowed these two
           node shapes as well, which taught the static checker to
           agree with a marshalling bug instead of fixing the bug. */
        return false;
    }
}

const char *stdrot_string_to_cstring(String s)
{
    if (!s.data)
        return NULL;

    char *out = SAFE_MALLOC_ARRAY(char, s.len + 1);
    if (!out)
        return NULL;
    memcpy(out, s.data, s.len);
    out[s.len] = '\0';
    return out;
}

void stdrot_release_cstring(const char *p)
{
    SAFE_FREE(p);
}

void execute_builtin_function(const String func_name, ArgumentList *args,
                              int call_line)
{
    execute_func_call(func_name, args, call_line);
}

static void ast_expr_to_stdrot_value(ASTNode *expr, StdrotValue *out)
{
    out->type = STDROT_NONE;
    if (!expr)
        return;

    /* Checked ahead of the type-specific switch below: any expression
       with a nonzero pointer level (a pointer variable, &x, a
       STDROT_PTR-returning native call, pointer arithmetic, ...)
       marshals as a raw address regardless of its base VarType -- an
       opaque pointer parameter (STDROT_PTR) has no base type to dispatch
       on by design (see semantic_check_native_call()'s STDROT_PTR
       handling). A call whose semantic-checked STDROT_PTR parameter got
       here is guaranteed pointer_level > 0 by that same check; this just
       has to actually carry the address through. */
    if (get_expression_pointer_level(expr) > 0)
    {
        out->type = STDROT_PTR;
        out->val.ptr = (void *)evaluate_expression_pointer(expr);
        return;
    }

    switch (expr->type)
    {
    case NODE_STRING_LITERAL:
        out->type = STDROT_STRING;
        out->val.str = expr->data.name;
        return;
    case NODE_INT:
        out->type = STDROT_INT;
        out->val.i = expr->data.ivalue;
        return;
    case NODE_SHORT:
        out->type = STDROT_SHORT;
        out->val.s = expr->data.svalue;
        return;
    case NODE_FLOAT:
        out->type = STDROT_FLOAT;
        out->val.f = expr->data.fvalue;
        return;
    case NODE_DOUBLE:
        out->type = STDROT_DOUBLE;
        out->val.d = expr->data.dvalue;
        return;
    case NODE_CHAR:
        out->type = STDROT_CHAR;
        out->val.c = (char)expr->data.ivalue;
        return;
    case NODE_BOOLEAN:
        out->type = STDROT_BOOL;
        out->val.b = expr->data.bvalue;
        return;
    case NODE_SIZEOF:
        out->type = STDROT_INT;
        out->val.i = evaluate_expression_int(expr);
        return;
    case NODE_IDENTIFIER:
    {
        Variable *var = get_variable(expr->data.name);
        if (!var)
        {
            /* Not a variable -- an unscoped enum constant (e.g. bare
               `RED`), which has type int in C. */
            EnumConstant *econst = find_global_enum_constant(expr->data.name);
            if (econst)
            {
                out->type = STDROT_INT;
                out->val.i = econst->value;
            }
            return;
        }
        switch (var->desc.type)
        {
        case VAR_INT:
            out->type = STDROT_INT;
            out->val.i = var->value.ivalue;
            return;
        case VAR_FLOAT:
            out->type = STDROT_FLOAT;
            out->val.f = var->value.fvalue;
            return;
        case VAR_DOUBLE:
            out->type = STDROT_DOUBLE;
            out->val.d = var->value.dvalue;
            return;
        case VAR_SHORT:
            out->type = STDROT_SHORT;
            out->val.s = var->value.svalue;
            return;
        case VAR_BOOL:
            out->type = STDROT_BOOL;
            out->val.b = var->value.bvalue;
            return;
        case VAR_CHAR:
            if (var->desc.is_array)
            {
                out->type = STDROT_STRING;
                out->val.str.data = (char *)var->value.array_data;
                out->val.str.len = (size_t)var->array_length;
            }
            else
            {
                out->type = STDROT_CHAR;
                out->val.c = (char)var->value.ivalue;
            }
            return;
        case VAR_STRING:
            out->type = STDROT_STRING;
            out->val.str.data = (char *)var->value.array_data;
            out->val.str.len = var->value.strvalue.len;
            return;
        case VAR_ENUM:
            out->type = STDROT_INT;
            out->val.i = var->value.ivalue;
            return;
        default:
            return;
        }
    }
    default:
        break;
    }

    /* General expression fallback */
    if (is_expression(expr, VAR_BOOL))
    {
        out->type = STDROT_BOOL;
        out->val.b = evaluate_expression_bool(expr);
    }
    else if (is_expression(expr, VAR_SHORT))
    {
        out->type = STDROT_SHORT;
        out->val.s = evaluate_expression_short(expr);
    }
    else if (is_expression(expr, VAR_FLOAT))
    {
        out->type = STDROT_FLOAT;
        out->val.f = evaluate_expression_float(expr);
    }
    else if (is_expression(expr, VAR_DOUBLE))
    {
        out->type = STDROT_DOUBLE;
        out->val.d = evaluate_expression_double(expr);
    }
    /* stdrot_char_narrows_to_int() (stdrot.h) is the ONE place this
       exception is defined -- infer_expression_abi_type()
       (semantic_analyzer.c) consults the identical predicate so the
       static checker and this runtime marshaller can't independently
       drift out of agreement about which node shapes lower a char
       through plain int instead of preserving it as STDROT_CHAR. A
       native call is the only shape excluded: its own declared result
       (e.g. `yapping("%s", slorp(buf))`) is preserved faithfully. */
    else if (!stdrot_char_narrows_to_int(expr->type,
                                         expr->type == NODE_UNARY_OPERATION
                                             ? expr->data.unary.op
                                             : OP_PLUS) &&
             is_expression(expr, VAR_CHAR))
    {
        out->type = STDROT_CHAR;
        out->val.c = (char)evaluate_expression_int(expr);
    }
    else if (!stdrot_char_narrows_to_int(expr->type,
                                         expr->type == NODE_UNARY_OPERATION
                                             ? expr->data.unary.op
                                             : OP_PLUS) &&
             is_expression(expr, VAR_STRING))
    {
        out->type = STDROT_STRING;
        out->val.str = evaluate_expression_string(expr);
    }
    else if (is_expression(expr, VAR_INT) || expr->type == NODE_ARRAY_ACCESS ||
             expr->type == NODE_OPERATION ||
             expr->type == NODE_UNARY_OPERATION ||
             expr->type == NODE_STRUCT_ACCESS)
    {
        out->type = STDROT_INT;
        out->val.i = evaluate_expression_int(expr);
    }
}

/* The semantic analyzer accepts int/short/float/double interchangeably for
 * a numeric-typed native parameter (matching the language's existing
 * numeric-coercion rules -- see check_type_compatibility_ex()), and
 * STDROT_STRING for a STDROT_CSTRING one. That check is a promise about
 * what the *call site* looks like, not about what union member
 * ast_expr_to_stdrot_value() actually populated -- an int literal always
 * arrives tagged STDROT_INT, never STDROT_DOUBLE, regardless of what the
 * declared parameter is. Without this, a native wrapper that trusts its
 * own declared signature and reads args[i].val.d for a statically-checked
 * `foo(42)` call reads the wrong union member. This is the one place that
 * reconciles the two: it converts *value* in place to whatever the
 * signature actually promised, so entry->fn always receives the
 * representation its own StdrotParam declared, not whatever the call site
 * happened to spell.
 *
 * Returns true if a new heap buffer was allocated for this argument
 * (STDROT_CSTRING conversion is the only case that does), so the caller
 * knows to release it after the call.
 */
/* Interconverts the numeric StdrotTypes in place (int/short/float/double
 * -- the same group the semantic analyzer's numeric-compatibility rules
 * accept interchangeably, see check_type_compatibility_ex()). Returns
 * true if value->type == declared afterward (either already matched, or
 * the conversion succeeded); false means declared isn't one of the
 * numeric types, or value->type wasn't either, so nothing was done.
 * Shared by coerce_arg_to_param() (argument marshalling) and
 * execute_native_call()'s post-call return-value check (making the
 * declared return type authoritative, not just advisory). */
static bool coerce_numeric(StdrotValue *value, StdrotType declared)
{
    if (value->type == declared)
        return true;

    switch (declared)
    {
    case STDROT_INT:
        if (value->type == STDROT_SHORT)
            value->val.i = value->val.s;
        else if (value->type == STDROT_FLOAT)
            value->val.i = (int)value->val.f;
        else if (value->type == STDROT_DOUBLE)
            value->val.i = (int)value->val.d;
        else
            return false;
        value->type = STDROT_INT;
        return true;
    case STDROT_SHORT:
        if (value->type == STDROT_INT)
            value->val.s = (short)value->val.i;
        else if (value->type == STDROT_FLOAT)
            value->val.s = (short)value->val.f;
        else if (value->type == STDROT_DOUBLE)
            value->val.s = (short)value->val.d;
        else
            return false;
        value->type = STDROT_SHORT;
        return true;
    case STDROT_FLOAT:
        if (value->type == STDROT_INT)
            value->val.f = (float)value->val.i;
        else if (value->type == STDROT_SHORT)
            value->val.f = (float)value->val.s;
        else if (value->type == STDROT_DOUBLE)
            value->val.f = (float)value->val.d;
        else
            return false;
        value->type = STDROT_FLOAT;
        return true;
    case STDROT_DOUBLE:
        if (value->type == STDROT_INT)
            value->val.d = value->val.i;
        else if (value->type == STDROT_SHORT)
            value->val.d = value->val.s;
        else if (value->type == STDROT_FLOAT)
            value->val.d = value->val.f;
        else
            return false;
        value->type = STDROT_DOUBLE;
        return true;
    default:
        return false;
    }
}

/* Makes the native's declared return_type authoritative, not merely
 * advisory: entry->fn() is ordinary C code, and can have the same class
 * of bug an argument-marshalling mismatch would (see coerce_arg_to_param()'s
 * comment) -- declare one return representation in its StdrotEntry, then
 * actually construct a StdrotValue tagged something else. Without this,
 * that mismatch is only discovered downstream, by whichever scalar
 * evaluator dereferences the boxed result through the wrong C pointer
 * type (declared double, returned int, handle_function_call() boxes an
 * int-sized allocation, evaluate_expression_double() reads 8 bytes out of
 * a 4-byte box). Attempts the same numeric coercion arguments get;
 * anything still mismatched after that is a bug in the native's own C
 * implementation, not something a Brainrot program can trigger by
 * construction (the semantic analyzer already only approves calls whose
 * *declared* signature type-checks), so this aborts with a diagnostic
 * aimed at whoever wrote the binding.
 *
 * identity_arg, when non-NULL, is the actual marshalled argument
 * return_like_arg pointed at (see StdrotEntry.return_like_arg) -- an
 * identity-polymorphic native's (e.g. slorp) result must match that
 * argument's own runtime type, not a fixed declared one. */
static void enforce_return_type(const String func_name, StdrotValue *result,
                                const StdrotEntry *entry,
                                const StdrotValue *identity_arg)
{
    StdrotType declared =
        identity_arg ? identity_arg->type : entry->return_type.type;

    if (declared == STDROT_ANY)
    {
        /* STDROT_ANY means "any of the scalar/string types this pipeline
           can marshal" (see its own comment in stdrot_api.h), not "any
           value representable at all" -- the same rule the argument side
           already enforces (see semantic_check_native_call()'s
           STDROT_ANY branch, which rejects a pointer-valued *argument*
           for the identical reason). Without this check, a legacy/
           untyped export (return_type.type == STDROT_ANY, return_like_arg
           == -1) could hand back a StdrotValue genuinely tagged
           STDROT_PTR/STDROT_HANDLE/STDROT_CSTRING and nothing here would
           object -- but the *static* side would never know:
           entry->return_type.type is STDROT_ANY, not the real tag, so
           every guard downstream that depends on the *declared* type
           (the scalar evaluators' NODE_FUNC_CALL checks,
           VAR_PTR's wildcard-compatibility rule, the STDROT_CSTRING-
           return rejection in semantic_check_native_call()) never fires.
           STDROT_CSTRING belongs in this rejected set for the same
           reason STDROT_CSTRING-as-a-declared-return-type is rejected
           outright elsewhere (semantic_check_native_call(), semantic_
           analyzer.c): marshal_native_return_value() (ast.c) has no code
           path that constructs a Brainrot String from one, typed or not
           -- reaching that switch's STDROT_CSTRING case through THIS
           route would hit the exact same "structurally unreachable"
           marshalling gap the typed rejection exists to close, just via
           a different front door. STDROT_ANY itself also belongs here:
           it is a descriptor placeholder ("type genuinely unknown" or
           "identity-polymorphic", see StdrotEntry's own comment in
           stdrot_api.h), not a real value tag -- no native's C
           implementation should ever construct a StdrotValue literally
           tagged STDROT_ANY, and marshal_native_return_value() has
           nothing meaningful to unbox if one does (see its own
           STDROT_ANY case). Static and runtime would disagree about
           what this expression actually is -- exactly the class of bug
           this whole enforcement function exists to close. */
        if (result->type == STDROT_PTR || result->type == STDROT_HANDLE ||
            result->type == STDROT_CSTRING || result->type == STDROT_ANY ||
            result->type == STDROT_STRUCT)
        /* NOTE: this branch is the STDROT_ANY-DECLARED case only -- a
           legacy untyped export handing back a tag the static side never
           saw. A native that DECLARES STDROT_HANDLE is checked by the
           exact-match path below and is entirely legal (#213); it is only
           smuggling one through an ANY descriptor that stays rejected,
           since then nothing static knows it is a handle. */
        {
            const char *actual_name;
            switch (result->type)
            {
            case STDROT_PTR:
                actual_name = "STDROT_PTR";
                break;
            case STDROT_HANDLE:
                actual_name = "STDROT_HANDLE";
                break;
            case STDROT_CSTRING:
                actual_name = "STDROT_CSTRING";
                break;
            case STDROT_STRUCT:
                actual_name = "STDROT_STRUCT";
                break;
            default:
                actual_name = "STDROT_ANY";
                break;
            }
            fprintf(stderr,
                    "Error: stdrot: native '%s' is declared to return "
                    "STDROT_ANY (legacy/untyped export or "
                    "identity-polymorphic result) but actually returned a "
                    "%s -- pointer/handle/cstring/struct results require an "
                    "explicit typed signature, and a native must never "
                    "construct a StdrotValue tagged STDROT_ANY itself, "
                    "not the unchecked ANY fallback\n",
                    func_name.data, actual_name);
            exit(1);
        }
        return; /* legacy/untyped export: actual tag wins for any other
                    type */
    }

    if (result->type == declared)
        return;

    if (coerce_numeric(result, declared))
        return;

    fprintf(stderr,
            "Error: stdrot: native '%s' declared to return %s but actually "
            "returned an incompatible StdrotType (%d instead of %d) -- "
            "this is a bug in the native binding's C implementation, not "
            "the Brainrot program\n",
            func_name.data,
            identity_arg ? "the same type as its identity argument"
                         : "a fixed type",
            (int)result->type, (int)declared);
    exit(1);
}

/* C's default argument promotions (C11 6.5.2.2p6-7), applied to exactly
 * the arguments this ABI's own STDROT_CSTRING-narrowing/char-lowering
 * fix (stdrot_char_narrows_to_int(), stdrot.h) just started deliberately
 * NOT applying to fixed, prototyped parameters: `char`/`_Bool`/`short`
 * promote to `int`, `float` promotes to `double`. A fixed parameter is
 * "converted as if by assignment" to its declared type -- no promotion
 * -- which is exactly why `takes_char(buf[0])` now correctly stays char.
 * A native's unchecked variadic tail (is_variadic, argument index >=
 * param_count -- e.g. yapping's format-string arguments) has no
 * prototype to convert "as if by assignment" *to*; C's own variadic
 * calling convention promotes every such argument the same way, and any
 * variadic consumer (process_yapping_format(), stdrot/yapping.c) is
 * entitled to expect that canonical representation rather than having to
 * understand every legal tagged Brainrot type individually. Centralized
 * here, once, at the ABI boundary every variadic native call passes
 * through -- not duplicated per-formatter. Other types (STRING, PTR,
 * CSTRING, ...) pass through unchanged; C's own default promotions don't
 * touch them either. */
static void apply_variadic_promotion(StdrotValue *value)
{
    switch (value->type)
    {
    case STDROT_CHAR:
        /* Deliberately a plain signed conversion, not a bug -- this
         * function exists specifically to emulate C's own default
         * argument promotions (see the function comment above), which
         * sign-extend a signed char to int exactly like this. */
        value->val.i = value->val.c; // NOLINT(bugprone-signed-char-misuse)
        value->type = STDROT_INT;
        break;
    case STDROT_BOOL:
        value->val.i = value->val.b ? 1 : 0;
        value->type = STDROT_INT;
        break;
    case STDROT_SHORT:
        value->val.i = value->val.s;
        value->type = STDROT_INT;
        break;
    case STDROT_FLOAT:
        value->val.d = value->val.f;
        value->type = STDROT_DOUBLE;
        break;
    default:
        break;
    }
}

static bool coerce_arg_to_param(StdrotValue *value, StdrotType declared,
                                const char *declared_type_name)
{
    if (value->type == declared)
        return false;

    if (coerce_numeric(value, declared))
        return false;

    switch (declared)
    {
    case STDROT_HANDLE:
        /* A `SAUCE *` variable arrives tagged STDROT_PTR, because
           ast_expr_to_stdrot_value() marshals ANY pointer-level
           expression as a raw address before it can know what the
           parameter declared (#213). Retag it as the handle kind the
           parameter names.

           Be precise about what this does and does not establish: it
           ASSERTS the declared kind, it does not VERIFY it -- a Variable
           holds only an address, with no kind tag of its own to compare
           against. Static typing is what keeps kinds apart at the source
           level (`SAUCE *` is a distinct type spelling), and the owning
           library's live-handle registry is what actually catches a
           fabricated, stale or wrong-kind token at runtime, by refusing
           any address that is not currently one of ITS live handles. That
           split is the whole ownership model -- see STDROT_HANDLE's
           comment in stdrot_api.h -- and it is why a handle is safe in a
           way the raw STDROT_PTR underneath it is not. Retagging here
           without the registry behind it would be security theatre. */
        if (value->type == STDROT_PTR)
        {
            void *addr = value->val.ptr;
            value->type = STDROT_HANDLE;
            value->val.handle.type_name = declared_type_name;
            value->val.handle.handle = addr;
            return false; /* nothing allocated; nothing to free after */
        }
        return false;
    case STDROT_CSTRING:
        if (value->type == STDROT_STRING)
        {
            const char *cstr = stdrot_string_to_cstring(value->val.str);
            /* stdrot_string_to_cstring() can genuinely return NULL --
               value->val.str.data == NULL, or (more likely in practice)
               the string exceeds MAX_ALLOC_SIZE (lib/mem.c's
               handle_malloc_error() returns NULL rather than aborting,
               so this is reachable by an ordinary Brainrot program
               building a large-enough string, not just a hypothetical
               true malloc() failure). value->type must NOT become
               STDROT_CSTRING unless conversion actually succeeded:
               tagging it CSTRING with a NULL .val.cstr would make
               enforce_arg_type() (below) approve the argument against a
               native that trusts its own declared signature and
               dereferences it unconditionally (e.g. strlen()) --
               silently turning an allocation failure into a null-
               pointer dereference inside someone else's C code. Leaving
               `value` as STDROT_STRING (unconverted) on failure instead
               makes enforce_arg_type() see a genuine, honest type
               mismatch and report *that*, rather than a tag that lies
               about what the union actually holds. */
            if (!cstr)
                return false;
            value->type = STDROT_CSTRING;
            value->val.cstr = cstr;
            return true;
        }
        return false;
    default:
        /* STDROT_BOOL/STDROT_CHAR/STDROT_STRING/STDROT_ANY/STDROT_PTR/
           STDROT_HANDLE/STDROT_NONE: the semantic checker requires an
           exact match for a *statically typed* argument, so this is a
           no-op there -- but that check fails open when the argument's
           inferred type is NONE (e.g. a legacy STDROT_ANY-returning call
           used as an argument, see semantic_check_native_call()'s
           `actual == NONE` short-circuit), so this genuinely can be
           reached with a value that has no available coercion. Returning
           false here (as opposed to silently accepting) is correct;
           enforce_arg_type() below is what actually catches the
           still-mismatched value afterward.

           STDROT_STRUCT belongs here rather than in a case of its own,
           because "no coercion applies" is the whole of its behavior:
           nothing converts INTO a by-value aggregate, and a struct
           argument already arrives tagged STDROT_STRUCT from
           ast_expr_to_stdrot_value(), so it returns at the top of this
           function without ever reaching the switch. (A separate case
           returning the same `false` was a genuine branch clone, which
           clang-tidy is right to reject -- the distinction was only ever
           documentary.) The defensive copy STDROT_STRUCT's contract
           promises is not made here at all: it happens in
           execute_native_call(), next to the ownership arrays that have
           to free it, since it must happen for every struct argument
           whether or not any coercion was needed. */
        return false;
    }
}

/* Marshals one argument for a parameter declared STDROT_STRUCT, producing
 * the adapter-owned byte image that type's contract promises
 * (stdrot_api.h) and, via *owned_blob_out, handing the buffer to the
 * caller's per-call cleanup.
 *
 * Deliberately NOT routed through ast_expr_to_stdrot_value(): that
 * function can only recognize a plain struct *identifier*, so a by-value
 * member access (`body.pos`) or a struct-returning call (`make_vec()`)
 * would miss its VAR_STRUCT case and fall through to the general
 * scalar fallback at its end -- which would both produce a nonsense
 * STDROT_INT and, for the call form, EVALUATE the call, leaving the real
 * evaluation below to run it a second time (the double-execution class of
 * bug #303 fixed). resolve_by_value_struct_source() (ast.c) is the single
 * shared "get me a by-value struct blob" step every other struct
 * copy site already goes through -- struct arguments to Brainrot-defined
 * functions, struct returns, struct copy-initializers -- so a native call
 * accepts exactly the same set of source expressions those do, rather
 * than a narrower ad-hoc one.
 *
 * Exits (rather than returning a failure the caller must handle) on an
 * unresolvable source, matching enforce_arg_type()'s own treatment of an
 * argument that turns out not to satisfy its declared parameter:
 * resolve_by_value_struct_source() has already emitted exactly one
 * diagnostic, and the contract this boundary owes the native -- a valid
 * image of the declared struct -- simply cannot be met. */
static void marshal_struct_argument(const String func_name, ASTNode *expr,
                                    int arg_index, StdrotValue *out,
                                    void **owned_blob_out)
{
    void *blob = NULL;
    String tag = {0};
    bool resolver_owns_blob = false;

    /* A bare struct-returning call needs the same special case the
       Brainrot-defined-function struct-parameter path already makes
       (enter_function_scope(), ast.c): its blob lives in the shared
       current_return_value slot rather than in any addressable variable,
       so resolve_by_value_struct_source() cannot reach it -- only a
       member access ON a call result (`make_outer().inner`) goes through
       the resolver. Handled here so a native's struct parameter accepts
       exactly the set of source expressions a Brainrot function's struct
       parameter does, rather than a narrower one. Restricted to
       user-defined functions: a native returning a struct is rejected at
       semantic-analysis time, so a builtin here can only be a type error,
       and is left to the resolver below to report as one. */
    if (expr->type == NODE_FUNC_CALL &&
        !is_builtin_function(expr->data.func_call.function_name))
    {
        execute_function_call(expr->data.func_call.function_name,
                              expr->data.func_call.arguments);
        if (!current_return_value.has_value ||
            current_return_value.desc.type != VAR_STRUCT ||
            current_return_value.desc.pointer_level != 0 ||
            !current_return_value.desc.struct_name.data ||
            !current_return_value.value.pvalue)
        {
            free_pending_return_value();
            fprintf(stderr,
                    "Error: stdrot: native '%s' argument %d: call does not "
                    "return a by-value struct/union\n",
                    func_name.data, arg_index + 1);
            exit(1);
        }
        tag = current_return_value.desc.struct_name;
        StructDef *rdef = get_struct_def(tag);
        void *copy = rdef && rdef->total_size > 0
                         ? SAFE_MALLOC_ARRAY(char, rdef->total_size)
                         : NULL;
        if (!copy)
        {
            /* Reported BEFORE free_pending_return_value(): `tag` borrows
               current_return_value's own struct_name, which that call
               frees along with the blob -- printing it afterward would
               read freed memory on the way out. */
            fprintf(stderr,
                    "Error: stdrot: native '%s' argument %d: could not "
                    "materialize the returned '%s' struct\n",
                    func_name.data, arg_index + 1, tag.data);
            free_pending_return_value();
            exit(1);
        }
        memcpy(copy, (void *)current_return_value.value.pvalue,
               rdef->total_size);
        /* Copied out, so release the shared slot immediately: a LATER
           argument's own call would otherwise overwrite (and free) the
           blob this one is still pointing at -- the same hazard
           enter_function_scope()'s owned-temporary handling exists for.
           tag is re-read from the fresh StructDef below rather than kept
           pointing into the slot this just freed. */
        free_pending_return_value();
        out->type = STDROT_STRUCT;
        out->val.blob.type_name = rdef->name.data;
        out->val.blob.data = copy;
        out->val.blob.size = rdef->total_size;
        *owned_blob_out = copy;
        return;
    }

    if (!resolve_by_value_struct_source(expr, &blob, &tag, &resolver_owns_blob,
                                        true) ||
        !blob || !tag.data)
    {
        exit(1);
    }

    StructDef *def = get_struct_def(tag);
    /* Copy unconditionally, even when resolve_by_value_struct_source()
       reports it already allocated one (`make_outer().inner`). Adopting
       that allocation would look like it saves a copy, but the two sides
       do not agree on an allocator: the resolver hands back plain
       calloc() memory (ast.c, freed there with plain free()), while
       everything this call frees goes through SAFE_FREE, which reads a
       safe_malloc() header block that a calloc'd pointer simply does not
       have -- it would reject the pointer as corrupt and leak it, after
       reading in front of the allocation to decide that. Copying into a
       SAFE_MALLOC_ARRAY buffer and releasing the resolver's own with the
       allocator it actually came from keeps each allocation paired with
       its matching free. */
    void *copy = (def && def->total_size > 0)
                     ? SAFE_MALLOC_ARRAY(char, def->total_size)
                     : NULL;
    if (!copy)
    {
        if (resolver_owns_blob)
        {
            free(blob);
        }
        fprintf(stderr,
                "Error: stdrot: native '%s' argument %d: could not "
                "materialize a '%s' struct argument\n",
                func_name.data, arg_index + 1, tag.data);
        exit(1);
    }
    memcpy(copy, blob, def->total_size);
    if (resolver_owns_blob)
    {
        free(blob);
    }

    out->type = STDROT_STRUCT;
    /* def->name, not the caller's `tag`: when the resolver owned the
       blob, tag borrowed from storage that was just released above. The
       registered StructDef outlives every call. */
    out->val.blob.type_name = def->name.data;
    out->val.blob.data = copy;
    out->val.blob.size = def->total_size;
    *owned_blob_out = copy;
}

/* Makes the native's declared param type authoritative for what
 * entry->fn() actually receives, the argument-side counterpart to
 * enforce_return_type(). Static argument checking (semantic_check_
 * native_call()) fails open whenever an argument's inferred type is
 * NONE -- most commonly a legacy STDROT_ANY-returning call used as an
 * argument, since the analyzer genuinely has no static type for it -- so
 * a call the analyzer approved can still hand coerce_arg_to_param() a
 * StdrotValue with no valid conversion to the declared param type.
 * Without this check, entry->fn() would receive that mismatched value
 * anyway: coerce_arg_to_param() returning false (no owned cstring
 * buffer) is silently treated as "nothing further to do," not "this
 * didn't work." A native wrapper is entitled to trust its own
 * StdrotParam blindly (e.g. `bool x = args[0].val.b;` for a declared
 * STDROT_BOOL param) -- that trust has to be enforced at this boundary,
 * not left to every wrapper to defensively re-check. */
static void enforce_arg_type(const String func_name, int arg_index,
                             const StdrotValue *value, const StdrotParam *param)
{
    if (param->type == STDROT_ANY)
    {
        /* Same accepted set as semantic_check_native_call()'s static
           STDROT_ANY check: any scalar/string this pipeline actually
           marshals, not a pointer/handle/cstring-shaped value. */
        switch (value->type)
        {
        case STDROT_INT:
        case STDROT_SHORT:
        case STDROT_FLOAT:
        case STDROT_DOUBLE:
        case STDROT_BOOL:
        case STDROT_CHAR:
        case STDROT_STRING:
            return;
        default:
            break;
        }
    }
    else if (value->type == param->type)
    {
        /* Matching StdrotType is sufficient for every other type, but
           not for an aggregate: every `gang` is STDROT_STRUCT, so the
           tags have to agree too or a native declaring `Vector2` would
           accept any same-shaped struct (and, worse, any DIFFERENTLY
           shaped one -- nothing above compares sizes either). The
           semantic analyzer already rejects a tag mismatch statically;
           this is the same fail-open case enforce_arg_type() exists for
           in general (an argument whose static type was NONE), applied
           to the one property that makes a struct argument meaningful.
           validate_native_registry() guarantees param->type_name is
           non-NULL for STDROT_STRUCT, and ast_expr_to_stdrot_value()
           only ever produces a STDROT_STRUCT value with a non-NULL tag,
           so neither side needs a NULL guard beyond this. */
        if (param->type != STDROT_STRUCT ||
            (value->val.blob.type_name && param->type_name &&
             strcmp(value->val.blob.type_name, param->type_name) == 0))
        {
            return;
        }

        fprintf(stderr,
                "Error: stdrot: native '%s' argument %d: declared to accept "
                "struct '%s' but the actual argument is struct '%s'\n",
                func_name.data, arg_index + 1,
                param->type_name ? param->type_name : "(none)",
                value->val.blob.type_name ? value->val.blob.type_name
                                          : "(none)");
        exit(1);
    }

    fprintf(stderr,
            "Error: stdrot: native '%s' argument %d: declared to accept "
            "StdrotType %d but the actual argument is StdrotType %d, "
            "even after attempted coercion -- static type checking "
            "approved this call without knowing the argument's real "
            "type (see semantic_check_native_call()'s NONE fallthrough), "
            "and the runtime value doesn't satisfy the declared "
            "parameter after all\n",
            func_name.data, arg_index + 1, (int)param->type, (int)value->type);
    exit(1);
}

/* A native's own C implementation can call exit() directly (bet's
 * assertion failure, ragequit) instead of returning through entry->fn(),
 * skipping execute_native_call()'s ordinary post-call cleanup entirely --
 * harmless back when the argument vector and ownership-tracking arrays
 * were fixed-size stack arrays (the OS reclaims stack and heap alike on
 * exit()), but now that they're heap-allocated to fit the actual call
 * (see execute_native_call()'s total_args), an exit() from inside
 * entry->fn(), or from a nested native call being evaluated while
 * marshalling an argument for this one, would leak them every time.
 *
 * Tracked as a stack (linked list), not a single slot, because native
 * calls nest: marshalling an argument can itself invoke
 * execute_native_call() for a nested call (e.g. `yapping("%s",
 * slorp(buf))`), which must not clobber the outer call's still-pending
 * allocation. Each frame is an ordinary local variable in
 * execute_native_call() -- exit() doesn't unwind the C call stack, so
 * every still-active frame's PendingNativeCallArgs is still valid memory
 * for this atexit handler to walk, however deep the nesting was when
 * exit() was called. Freed via the pending_stack pointer, not
 * individually SAFE_FREE'd, since each frame lives in its function's own
 * stack space rather than being heap-allocated itself. */
typedef struct PendingNativeCallArgs
{
    StdrotValue *arg_values;
    char **owned_string_bufs;
    const char **owned_cstring_bufs;
    /* The by-value copy made for each STDROT_STRUCT argument
       (STDROT_STRUCT's contract, stdrot_api.h). A third array rather
       than a reuse of owned_cstring_bufs for the same reason those two
       are already separate: the buffer pointer lives in a different
       StdrotValue union member, and one slot can legitimately own
       several kinds of scratch at once. */
    void **owned_blob_bufs;
    int arg_count;
    struct PendingNativeCallArgs *next;
} PendingNativeCallArgs;

static PendingNativeCallArgs *pending_native_call_stack = NULL;
static bool native_call_cleanup_registered = false;

static void free_pending_native_call_args(void)
{
    for (PendingNativeCallArgs *frame = pending_native_call_stack; frame;
         frame = frame->next)
    {
        for (int i = 0; i < frame->arg_count; i++)
        {
            if (frame->owned_string_bufs && frame->owned_string_bufs[i])
            {
                SAFE_FREE(frame->owned_string_bufs[i]);
            }
            if (frame->owned_cstring_bufs && frame->owned_cstring_bufs[i])
            {
                stdrot_release_cstring(frame->owned_cstring_bufs[i]);
            }
            if (frame->owned_blob_bufs && frame->owned_blob_bufs[i])
            {
                SAFE_FREE(frame->owned_blob_bufs[i]);
            }
        }
        SAFE_FREE(frame->arg_values);
        SAFE_FREE(frame->owned_string_bufs);
        SAFE_FREE(frame->owned_cstring_bufs);
        SAFE_FREE(frame->owned_blob_bufs);
    }
    pending_native_call_stack = NULL;
}

/* Round-19 review, finding #2 -- materializes `result` into an
 * independently-owned copy if (and only if) it's a STDROT_STRING,
 * mutating it in place and returning whether it now owns that copy (the
 * value NativeResult.owns_string is built from). This is the ENTIRE
 * string-return lifetime contract this ABI promises: a native may return
 * borrowed/aliased string storage from anywhere -- an argument's own
 * scratch buffer, static storage, a live variable's backing storage --
 * because the adapter copies it immediately, unconditionally, before
 * anything downstream can free or mutate whatever it was borrowed from
 * (see the general path's own comment, below, for why this used to be a
 * narrower alias-specific check and had to become unconditional).
 * Factored out specifically so execute_native_call()'s zero-argument
 * fast path and its general path enforce that contract identically --
 * the zero-argument path previously skipped this altogether, reasoning
 * that "no argument buffers exist to alias" was still the operative
 * invariant after the general path had already moved past it: a zero-
 * argument native returning a pointer into its own static/scratch
 * storage (`static char scratch[256]; ...`) is exactly as borrowed as an
 * aliased argument buffer, and the contract this ABI documents doesn't
 * carve out an exception for arity. */
static bool finalize_native_string_result(StdrotValue *result)
{
    if (result->type != STDROT_STRING)
        return false;

    /* Round-20 review, finding #4, corrected by round-21 finding #4 --
       safe_strdup() (lib/mem.c) genuinely can fail (OOM, or a
       pathologically large string exceeding MAX_ALLOC_SIZE), and until
       its own round-20 fix that failure meant a NULL-pointer memcpy
       destination. The round-20 fix for THIS function's own failure
       path was still wrong, though: it degraded result->type to
       STDROT_NONE, reasoning that every VAR_VOID-rejecting context
       would then correctly reject the result -- but that retags a
       native call that genuinely, successfully returned a STDROT_STRING
       (enforce_return_type() already validated it, just above the call
       site below) as if it had returned nothing at all. The descriptor
       said STRING; the native returned STRING; only the ADAPTER's own
       copy failed -- that is not a different, valid return value, it is
       this call failing to execute, the same severity class as every
       other stdrot.c ABI enforcement failure (enforce_return_type()
       itself, enforce_arg_type(), validate_native_registry()), all of
       which report via stderr and exit(1) rather than inventing a
       degraded-but-valid result. Every caller of this function runs it
       BEFORE popping its own pending_native_call_stack frame (see the
       general call path's own comment on that ordering, and identity_
       arg's use just above enforce_return_type()) specifically so an
       exit() from anywhere in this sequence still leaves that frame
       reachable for the atexit handler to free -- this failure path
       relies on that same invariant, not a frame release of its own. */
    String copy = safe_strdup(&result->val.str);
    if (!copy.data)
    {
        fprintf(stderr, "stdrot: out of memory materializing a native's "
                        "STDROT_STRING return value -- the native itself "
                        "executed and returned a valid string, but this "
                        "adapter's own required copy of it could not be "
                        "allocated\n");
        exit(1);
    }
    result->val.str = copy;
    return true;
}

NativeResult execute_native_call(const String func_name, ArgumentList *args,
                                 int call_line)
{
    if (!func_name.data || !functions)
    {
        yyerror("Function not found");
        return (NativeResult){{STDROT_NONE, {0}}, false};
    }

    const StdrotEntry *entry = get_native_function(func_name);

    if (!entry || !entry->fn)
    {
        yyerror("Unknown function");
        return (NativeResult){{STDROT_NONE, {0}}, false};
    }

    /* Set execution context. Prefer the first argument node's line (keeps
       every existing multi-arg diagnostic byte-for-byte), and fall back to
       the call node's own line -- the only source a ZERO-argument native has,
       so an arg-less abort (a CSPRNG failure, the wasm gamba() stub) reports
       the real call site instead of "line 0". */
    g_exec_context.function_name.data = func_name.data;
    g_exec_context.line_number = 0;
    if (args && args->expr && args->expr->line_number > 0)
    {
        g_exec_context.line_number = args->expr->line_number;
    }
    else if (call_line > 0)
    {
        g_exec_context.line_number = call_line;
    }

    /* Count first so the argument vector is sized to the actual call --
       no arbitrary cap silently dropping trailing arguments the semantic
       analyzer already approved (it counts the whole list against
       min_args/param_count/is_variadic with no limit of its own; a fixed
       stack array here previously just stopped filling in past 64,
       amputating any argument beyond that with no error at all). */
    int total_args = 0;
    for (ArgumentList *c = args; c && c->expr; c = c->next)
    {
        total_args++;
    }

    if (total_args == 0)
    {
        StdrotValue result = entry->fn(NULL, 0);
        /* Same return-type enforcement as the general path below --
           can't skip it just because this call happens to take no
           arguments, or a zero-arg native's return-type bug would go
           uncaught (see enforce_return_type()'s comment). No argument-
           marshalling cleanup needed here since none was allocated --
           but a STDROT_STRING result still needs the exact same
           unconditional materialization the general path performs
           (finalize_native_string_result(), just above): "no argument
           buffers to alias" was the old, narrower invariant this ABI
           already moved past, and a zero-argument native can return
           borrowed static/scratch storage just as easily as an aliased
           argument buffer can. */
        enforce_return_type(func_name, &result, entry, NULL);
        bool owns_string = finalize_native_string_result(&result);
        return (NativeResult){result, owns_string};
    }

    StdrotValue *arg_values = SAFE_MALLOC_ARRAY(StdrotValue, total_args);
    /* Two independent reasons a slot can own a heap buffer that needs
       freeing after the call -- tracked as the buffer pointer itself, not
       just a bool, because coerce_arg_to_param() overwrites the union
       member that held the original STDROT_STRING buffer with the new
       STDROT_CSTRING one. A bool flag can't recover a pointer that's
       already been overwritten out from under it by the time cleanup
       runs; both can legitimately apply to the same slot at once (a
       nested native call result gets both its own fresh String buffer
       *and* a fresh C-string conversion of it), and each needs freeing
       independently, into its own union member. */
    char **owned_string_bufs = SAFE_MALLOC_ARRAY(char *, total_args);
    const char **owned_cstring_bufs =
        SAFE_MALLOC_ARRAY(const char *, total_args);
    void **owned_blob_bufs = SAFE_MALLOC_ARRAY(void *, total_args);
    if (!arg_values || !owned_string_bufs || !owned_cstring_bufs ||
        !owned_blob_bufs)
    {
        SAFE_FREE(arg_values);
        SAFE_FREE(owned_string_bufs);
        SAFE_FREE(owned_cstring_bufs);
        SAFE_FREE(owned_blob_bufs);
        yyerror("Out of memory marshalling native call arguments");
        return (NativeResult){{STDROT_NONE, {0}}, false};
    }

    if (!native_call_cleanup_registered)
    {
        atexit(free_pending_native_call_args);
        native_call_cleanup_registered = true;
    }
    PendingNativeCallArgs frame = {.arg_values = arg_values,
                                   .owned_string_bufs = owned_string_bufs,
                                   .owned_cstring_bufs = owned_cstring_bufs,
                                   .owned_blob_bufs = owned_blob_bufs,
                                   .arg_count = total_args,
                                   .next = pending_native_call_stack};
    pending_native_call_stack = &frame;

    int arg_count = 0;
    ArgumentList *cur = args;
    while (cur && cur->expr && arg_count < total_args)
    {
        ASTNode *expr = cur->expr;
        const StdrotParam *declared_param =
            (arg_count < entry->param_count && entry->params)
                ? &entry->params[arg_count]
                : NULL;

        owned_string_bufs[arg_count] = NULL;
        owned_cstring_bufs[arg_count] = NULL;
        owned_blob_bufs[arg_count] = NULL;

        /* A declared by-value struct parameter takes its own marshalling
           path, chosen BEFORE the argument expression is evaluated at
           all -- see marshal_struct_argument()'s own comment for why
           routing it through ast_expr_to_stdrot_value() first would both
           mistype it and double-evaluate a call-shaped argument. */
        if (declared_param && declared_param->type == STDROT_STRUCT)
        {
            marshal_struct_argument(func_name, expr, arg_count,
                                    &arg_values[arg_count],
                                    &owned_blob_bufs[arg_count]);
            enforce_arg_type(func_name, arg_count, &arg_values[arg_count],
                             declared_param);
            arg_count++;
            cur = cur->next;
            continue;
        }

        ast_expr_to_stdrot_value(expr, &arg_values[arg_count]);
        /* Which argument shapes hand back a buffer this frame must free.
           Both of these reach ast_expr_to_stdrot_value()'s generic
           `is_expression(expr, VAR_STRING)` branch, which calls
           evaluate_expression_string() -- and that ALWAYS returns a
           safe_strdup'd buffer the caller owns.

           A plain `rant` identifier is deliberately not in this list: it
           takes the VAR_STRING case of the identifier path instead, which
           borrows the Variable's own storage rather than copying it, so
           freeing it here would destroy a live variable.

           NODE_STRING_SLICE joined NODE_FUNC_CALL when `s[i:j]` was added
           (#251) -- without it, `yapping("%s", s[0:2])` leaked one buffer
           per call, which is what LeakSanitizer caught. */
        if ((expr->type == NODE_FUNC_CALL || expr->type == NODE_STRING_SLICE) &&
            arg_values[arg_count].type == STDROT_STRING)
        {
            owned_string_bufs[arg_count] = arg_values[arg_count].val.str.data;
        }

        /* Convert to whatever entry->params[arg_count] actually declared
           -- see coerce_arg_to_param()'s comment for why this has to
           happen here, not just at the semantic-analysis type check. */
        if (arg_count < entry->param_count && entry->params)
        {
            if (coerce_arg_to_param(&arg_values[arg_count],
                                    entry->params[arg_count].type,
                                    entry->params[arg_count].type_name))
            {
                owned_cstring_bufs[arg_count] = arg_values[arg_count].val.cstr;
            }
            /* Re-verifies the contract coerce_arg_to_param() just
               attempted to satisfy -- see enforce_arg_type()'s own
               comment for why this can genuinely fail even after a
               program passed semantic analysis. */
            enforce_arg_type(func_name, arg_count, &arg_values[arg_count],
                             &entry->params[arg_count]);
        }
        else
        {
            /* Beyond entry->param_count -- the unchecked tail, for both
               a genuine C-style variadic native and a legacy/untyped
               STDROT_EXPORT() export (see promote_variadic_tail's own
               comment, stdrot_api.h, for why those are different
               concepts -- but this specific check applies to both:
               neither can do anything meaningful with an argument that
               marshalled to "no value" at all).

               semantic_check_native_call()'s is_unmarshallable_expr()
               (semantic_analyzer.c) already rejects most unmarshallable
               shapes statically (a struct identifier, a non-char
               array), but one shape it structurally cannot see: an
               argument that's itself a call to a native declared
               STDROT_ANY (a legacy/untyped export -- infer_expression_
               type()'s own NONE fallthrough for exactly this case)
               whose real, only-known-at-runtime return happens to be
               STDROT_NONE (e.g. a legacy void-returning export). Reject
               it here, the first point it's actually knowable, the same
               way enforce_arg_type() already rejects a fixed
               parameter's argument that turns out not to satisfy its
               declared type after all despite passing static
               analysis. */
            if (arg_values[arg_count].type == STDROT_NONE)
            {
                fprintf(stderr,
                        "Error: stdrot: native '%s' argument %d: "
                        "expression produced no value (STDROT_NONE) -- "
                        "cannot be passed as a native argument\n",
                        func_name.data, arg_count + 1);
                exit(1);
            }

            /* apply_variadic_promotion()'s own comment has the full
               reasoning for why this is gated on promote_variadic_tail,
               not entry->is_variadic: this is where C's default
               argument promotions belong, centralized once instead of
               every variadic consumer re-deriving them (or, as this ABI
               briefly did, silently NOT applying them after fixed-
               parameter char handling was corrected to stop narrowing
               -- yapping's own "%d" never accepted STDROT_CHAR to begin
               with) -- but applying that promotion to a legacy export's
               arguments too would silently change its observed
               StdrotValue.type without recompiling or touching that
               binding's own source, a real ABI break for any wrapper
               that switches on args[i].type. */
            if (entry->promote_variadic_tail)
            {
                apply_variadic_promotion(&arg_values[arg_count]);
            }
        }

        arg_count++;
        cur = cur->next;
    }

    StdrotValue result = entry->fn(arg_values, arg_count);

    /* Called before popping the pending-cleanup frame below: if this
       finds a native ABI violation and exit()s, the frame -- and
       arg_values/owned_string_bufs/owned_cstring_bufs it still owns --
       must still be reachable from pending_native_call_stack for the
       atexit handler to free, the same as any other exit() from inside
       this call. */
    const StdrotValue *identity_arg =
        (entry->return_like_arg >= 0 && entry->return_like_arg < arg_count)
            ? &arg_values[entry->return_like_arg]
            : NULL;
    enforce_return_type(func_name, &result, entry, identity_arg);

    /* Materialize EVERY STDROT_STRING result BEFORE releasing argument
     * scratch below, unconditionally -- not just the ones this function
     * can prove alias a specific tracked scratch buffer. The public
     * contract this ABI documents (STDROT_STRING's own comment,
     * stdrot_api.h) is "the adapter deep-copies the returned string
     * immediately after the call, before releasing any argument-owned
     * scratch, so a native may safely return a borrowed/aliased buffer
     * including one of its own arguments" -- that promise has to hold
     * for ANY argument-owned buffer a result might alias, not just the
     * ones this function happens to recognize.
     *
     * The previous version only checked owned_string_bufs[] (a nested
     * native call's STDROT_STRING result, marshalled fresh for this
     * call's own argument) -- but coerce_arg_to_param()'s STDROT_CSTRING
     * conversion allocates an ADDITIONAL, independently-tracked buffer
     * per argument (owned_cstring_bufs[]), and nothing stopped a
     * (legitimately typed, STDROT_CSTRING-parameter) native from handing
     * back a STDROT_STRING result built by pointing straight at that
     * cstring buffer (e.g. `out.val.str.data = (char *)args[0].val.
     * cstr;`). That alias was never checked, so it sailed through
     * unmaterialized, got freed by the owned_cstring_bufs[] cleanup loop
     * below like any other argument scratch, and the caller (marshal_
     * native_return_value()/handle_function_call(), ast.c) copied from
     * already-freed memory -- the exact same class of use-after-free the
     * original owned_string_bufs[]-only check existed to close, just
     * through the OTHER scratch array.
     *
     * Rather than extending the alias search to also check owned_
     * cstring_bufs[] (and being one undiscovered scratch-buffer type away
     * from the next version of this exact bug), copy every STDROT_STRING
     * result unconditionally: whichever buffer it happens to point at --
     * argument-owned scratch of either kind, a string literal, a live
     * Brainrot variable's own backing storage -- the copy is independent
     * of all of them, so it no longer matters which one (if any) `result`
     * was aliasing. A copy of a buffer that wasn't scratch at all (a
     * literal, a live variable) is simply an extra copy, not a
     * correctness problem -- the original is never touched by anything
     * this function frees.
     *
     * That copy still needs an owner, though -- owns_string, on the
     * NativeResult this function returns (stdrot.h), is how this
     * communicates "yes, actually free this one" to whichever of
     * native_call_peek()/native_call_consume() (ast.c) or execute_
     * func_call()'s deprecated write-back path (below, this file) ends
     * up holding this result next, and ultimately to handle_function_
     * call()'s VAR_STRING case (ast.c), which frees current_return_
     * value.value.strvalue.data right after copying it into the box the
     * caller actually consumes. This is a local, scoped to exactly this
     * invocation's own result -- not a global -- precisely because
     * evaluating this call's own arguments (ast_expr_to_stdrot_value(),
     * above) can recursively invoke execute_native_call() again for a
     * nested call, each producing its own independent NativeResult. See
     * NativeResult's own comment (stdrot.h) for the full reasoning. */
    bool owns_string = finalize_native_string_result(&result);

    /* Reached: entry->fn() returned normally rather than exit()ing, so
       ordinary cleanup below handles this frame, and the atexit handler
       above must not also try to (double-free) -- pop it first. Any
       nested call's own frame already popped itself the same way before
       returning here, so this is always exactly our own frame. */
    pending_native_call_stack = frame.next;

    for (int i = 0; i < arg_count; i++)
    {
        if (owned_string_bufs[i])
        {
            SAFE_FREE(owned_string_bufs[i]);
        }
        if (owned_cstring_bufs[i])
        {
            stdrot_release_cstring(owned_cstring_bufs[i]);
        }
        if (owned_blob_bufs[i])
        {
            SAFE_FREE(owned_blob_bufs[i]);
        }
    }

    SAFE_FREE(arg_values);
    SAFE_FREE(owned_string_bufs);
    SAFE_FREE(owned_cstring_bufs);
    SAFE_FREE(owned_blob_bufs);

    return (NativeResult){result, owns_string};
}

void execute_func_call(const String func_name, ArgumentList *args,
                       int call_line)
{
    /* Forward the caller's call-site line straight through: the live
       statement dispatcher (interpreter_execute_call_statement()) does not
       populate g_exec_context.line_number itself, so this must not read it
       back as a fallback -- a zero-arg abort's line comes from call_line. */
    NativeResult nr = execute_native_call(func_name, args, call_line);
    StdrotValue result = nr.value;

    /* Deprecated write-back: if first arg is an identifier and the function
     * returned a value, also write the returned value back to that variable.
     * This is the pre-#204 calling convention (`slorp(x);` instead of
     * `rizz x = slorp(...);`); it is kept working for one release, with a
     * warning, so existing programs don't break outright. New code should
     * consume the return value directly -- see execute_native_call() /
     * handle_function_call().
     *
     * A `yap[N]` char-array argument is exempt: that's the canonical
     * bounded-buffer form (#229/#230, e.g. `yap name[32]; slorp(name);`),
     * never a deprecated scalar type-witness. The native already wrote
     * into the buffer's own backing storage in place (stdrot_slorp()'s
     * STDROT_STRING case, stdrot/slorp.c), so there is nothing left to
     * write back either -- flagging it here would both misdescribe
     * intended usage as deprecated and be redundant. */
    /* A STDROT_HANDLE first parameter is exempt for a stronger reason
       than the `yap[N]` case above: there, writing back is merely
       redundant, whereas here it is actively destructive. `peaceout(f)`
       returns fclose's status, and writing that into `f` would overwrite
       the handle with 0 -- silently turning a live SAUCE variable into a
       null one, or worse, a small integer later handed back as an
       address. A handle argument names the RESOURCE being operated on; it
       is never a pre-#204 scalar type witness, and no file operation has
       ever had the write-back convention to preserve. */
    const StdrotEntry *wb_entry = get_native_function(func_name);
    const bool first_param_is_handle =
        wb_entry && wb_entry->params && wb_entry->param_count > 0 &&
        wb_entry->params[0].type == STDROT_HANDLE;

    if (result.type != STDROT_NONE && args && args->expr &&
        args->expr->type == NODE_IDENTIFIER && !first_param_is_handle)
    {
        const String name = args->expr->data.name;
        Variable *var = get_variable(name);
        if (var && !(var->desc.is_array && var->desc.type == VAR_CHAR))
        {
            fprintf(stderr,
                    "Warning: line %d: `%s(%s, ...)` writing its result back "
                    "into `%s` is deprecated -- use `%s = %s(...)` instead\n",
                    g_exec_context.line_number, func_name.data, name.data,
                    name.data, name.data, func_name.data);
            switch (result.type)
            {
            case STDROT_INT:
                set_int_variable(name, result.val.i, var->desc.modifiers);
                break;
            case STDROT_FLOAT:
                set_float_variable(name, result.val.f, var->desc.modifiers);
                break;
            case STDROT_DOUBLE:
                set_double_variable(name, result.val.d, var->desc.modifiers);
                break;
            case STDROT_SHORT:
                set_short_variable(name, result.val.s, var->desc.modifiers);
                break;
            case STDROT_CHAR:
                /* set_char_variable(), not set_int_variable(): the latter
                   sets var_type to VAR_INT, corrupting the variable's own
                   type tag for every later use of it -- including a
                   subsequent native-call argument, which would then
                   marshal as STDROT_INT instead of STDROT_CHAR despite
                   still holding an ordinary char value (see
                   ast_expr_to_stdrot_value()'s NODE_IDENTIFIER/VAR_CHAR
                   case, which dispatches purely on var->var_type). */
                set_char_variable(name, result.val.c, var->desc.modifiers);
                break;
            case STDROT_STRING:
                if (var->desc.is_array && var->desc.type == VAR_CHAR &&
                    var->array_length > 0)
                {
                    char *dst = (char *)var->value.array_data;

                    if (result.val.str.data && result.val.str.data != dst)
                    {

                        size_t max = var->array_length - 1;
                        size_t n = result.val.str.len;

                        if (n > max)
                            n = max;

                        memcpy(dst, result.val.str.data, n);
                        dst[n] = '\0';
                    }
                }
                /* Freeing an owned buffer (if any) is handled once, below,
                   after this whole write-back block -- see the comment
                   there. This case only needs to have finished reading
                   result.val.str.data (via the memcpy above) before that
                   point, which it has. */
                break;
            case STDROT_BOOL:
                set_bool_variable(name, result.val.b, var->desc.modifiers);
                break;
            default:
                break;
            }
        }
    }

    /* nr.owns_string (NativeResult, stdrot.h): true only when result.type
       == STDROT_STRING *and* execute_native_call() had to materialize an
       independent copy of an aliased argument buffer to avoid a
       use-after-free once its own argument-cleanup loop ran -- scoped to
       this specific `nr`, not a global, so a nested call evaluated while
       marshalling `args` above can never leave this flag describing a
       result other than this function's own. This function is the last
       consumer of `result` on every path above -- whether that path
       copied the string out (the STDROT_STRING case), discarded it for
       some other reason (no identifier argument, an identifier that
       didn't resolve to a live variable, or a result type this
       deprecated write-back doesn't forward), or never entered the
       write-back block at all. Whichever it was, nothing downstream
       still needs this buffer, so free it here unconditionally rather
       than leaking it on every path that isn't the one case that used to
       remember to do this. The result.type check is redundant with how
       execute_native_call() sets owns_string (only ever true alongside
       STDROT_STRING) but kept as a belt-and-suspenders guard against
       ever reinterpreting a non-string union member as a heap pointer. */
    if (nr.owns_string && result.type == STDROT_STRING)
    {
        SAFE_FREE(result.val.str.data);
    }
}

/* ── Stub functions (thin wrappers that forward to the implementation) ───────
 */

#ifdef STDROT_STATIC

void yapping(const String format, ...)
{
    va_list ap;
    va_start(ap, format);
    v_yapping(format.data, ap);
    va_end(ap);
}

void yappin(const String format, ...)
{
    va_list ap;
    va_start(ap, format);
    v_yappin(format.data, ap);
    va_end(ap);
}

void baka(const String format, ...)
{
    va_list ap;
    va_start(ap, format);
    v_baka(format.data, ap);
    va_end(ap);
}

#else

void yapping(const String format, ...)
{
    String s = {.data = "v_yapping", .len = sizeof("v_yapping") - 1};
    va_list ap;
    va_start(ap, format);
    void (*fn)(const String, va_list) =
        (void (*)(const String, va_list))stdrot_lookup_symbol(s);
    if (fn)
        fn(format, ap);
    va_end(ap);
}

void yappin(const String format, ...)
{
    String s = {.data = "v_yappin", .len = sizeof("v_yappin") - 1};
    va_list ap;
    va_start(ap, format);
    void (*fn)(const String, va_list) =
        (void (*)(const String, va_list))stdrot_lookup_symbol(s);
    if (fn)
        fn(format, ap);
    va_end(ap);
}

void baka(const String format, ...)
{
    String s = {.data = "v_baka", .len = sizeof("v_baka") - 1};
    va_list ap;
    va_start(ap, format);
    void (*fn)(const String, va_list) =
        (void (*)(const String, va_list))stdrot_lookup_symbol(s);
    if (fn)
        fn(format, ap);
    va_end(ap);
}

void ragequit(int exit_code)
{
    String s = {.data = "ragequit", .len = sizeof("ragequit") - 1};
    void (*fn)(int) = (void (*)(int))stdrot_lookup_symbol(s);
    if (fn)
        fn(exit_code);
}

void chill(unsigned int seconds)
{
    String s = {.data = "chill", .len = sizeof("chill") - 1};
    void (*fn)(unsigned int) = (void (*)(unsigned int))stdrot_lookup_symbol(s);
    if (fn)
        fn(seconds);
}

char slorp_char(char chr)
{
    String s = {.data = "slorp_char", .len = sizeof("slorp_char") - 1};
    char (*fn)(char) = (char (*)(char))stdrot_lookup_symbol(s);
    return fn ? fn(chr) : chr;
}

int slorp_int(int val)
{
    String s = {.data = "slorp_int", .len = sizeof("slorp_int") - 1};
    int (*fn)(int) = (int (*)(int))stdrot_lookup_symbol(s);
    return fn ? fn(val) : val;
}

short slorp_short(short val)
{
    String s = {.data = "slorp_short", .len = sizeof("slorp_short") - 1};
    short (*fn)(short) = (short (*)(short))stdrot_lookup_symbol(s);
    return fn ? fn(val) : val;
}

float slorp_float(float var)
{
    String s = {.data = "slorp_float", .len = sizeof("slorp_float") - 1};
    float (*fn)(float) = (float (*)(float))stdrot_lookup_symbol(s);
    return fn ? fn(var) : var;
}

double slorp_double(double var)
{
    String s = {.data = "slorp_double", .len = sizeof("slorp_double") - 1};
    double (*fn)(double) = (double (*)(double))stdrot_lookup_symbol(s);
    return fn ? fn(var) : var;
}

#endif /* STDROT_STATIC */

#pragma GCC diagnostic pop
