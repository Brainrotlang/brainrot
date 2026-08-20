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
 *        discovers all functions via stdrot_get_api_v2()
 *     2. Thin varargs stubs (yapping/yappin/baka) and per-type slorp/
 *        ragequit/chill stubs that dlsym their real implementation by name
 *        on first use
 *
 *   STDROT_STATIC defined (wasm build, see `make wasm`):
 *     the stdrot sources are compiled directly into the same binary —
 *     there is no .so and no dlopen surface at all (wasm has no dynamic
 *     loader worth using for a single-artifact build). stdrot_load() calls
 *     stdrot_get_api_v2() directly, and ragequit/chill/slorp_* are provided
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifndef STDROT_STATIC
#include <dlfcn.h>
#endif

/* ── Global execution context ────────────────────────────────────────────── */
ExecutionContext g_exec_context = {0, {NULL, 0}, {NULL, 0}};

/* ── External interpreter functions ──────────────────────────────────────── */
extern void yyerror(const char *s);
extern int evaluate_expression_int(ASTNode *node);
extern float evaluate_expression_float(ASTNode *node);
extern double evaluate_expression_double(ASTNode *node);
extern short evaluate_expression_short(ASTNode *node);
extern bool evaluate_expression_bool(ASTNode *node);
extern String evaluate_expression_string(ASTNode *node);
extern bool is_expression(ASTNode *node, VarType type);
extern Variable *get_variable(const String name);
extern TypeModifiers get_variable_modifiers(const String name);
extern void *evaluate_multi_array_access(ASTNode *node);
extern bool set_int_variable(const String name, int value, TypeModifiers mods);
extern bool set_float_variable(const String name, float value,
                               TypeModifiers mods);
extern bool set_double_variable(const String name, double value,
                                TypeModifiers mods);
extern bool set_short_variable(const String name, short value,
                               TypeModifiers mods);
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
#endif

/* ── Forward declarations of stub functions ──────────────────────────────── */
void yapping(const String format, ...);
void yappin(const String format, ...);
void baka(const String format, ...);
#ifndef STDROT_STATIC
void ragequit(int exit_code);
void chill(unsigned int seconds);
char slorp_char(char chr);
int slorp_int(int val);
short slorp_short(short val);
float slorp_float(float var);
double slorp_double(double var);
#endif

#ifdef STDROT_STATIC
/* Statically linked in from stdrot/yapping.c and stdrot/baka.c — called
 * directly below instead of going through dlsym-by-name.
 * stdrot_get_api_v2() (statically linked from stdrot/registry.c) is
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
 * describe is actually implemented end to end. STDROT_HANDLE (any
 * position) and STDROT_CSTRING as a *return* type are real, well-formed
 * StdrotType values -- reserved groundwork for capabilities this ABI
 * hasn't finished (see stdrot_api.h's own STDROT_HANDLE/STDROT_CSTRING
 * comments) -- not malformed metadata. A descriptor using either loads
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

static void validate_native_registry(void)
{
    /* Validate the table itself before ever indexing into it --
       stdrot_get_api_v2() (registry.c) is trusted to return a StdrotAPI
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
     * mismatch stdrot_get_api_v2()'s naming exists to catch (an old
     * libstdrot.so loaded by a new host, see STDROT_ABI_VERSION's own
     * comment) is structurally impossible here. */
    StdrotAPI api = stdrot_get_api_v2();
    functions = api.functions;
    function_count = api.count;
    validate_native_registry();
}

void stdrot_unload(void)
{
    functions = NULL;
    function_count = 0;
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

    /* Open libstdrot.so from the same directory as the binary, or
     * LD_LIBRARY_PATH Use RTLD_GLOBAL so the library can access symbols from
     * the main binary (e.g., g_exec_context)
     */
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
    *(void **)(&get_api) = dlsym(lib_handle, "stdrot_get_api_v2");
    if (!get_api)
    {
        fprintf(stderr,
                "libstdrot.so is missing stdrot_get_api_v2() -- it was "
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
    validate_native_registry();
}

void stdrot_unload(void)
{
    if (lib_handle)
    {
        dlclose(lib_handle);
        lib_handle = NULL;
        functions = NULL;
        function_count = 0;
        cache_count = 0;
    }
}

#endif /* STDROT_STATIC */

/* ── Runtime query ──────────────────────────────────────────────────────────
 */

bool is_builtin_function(const String func_name)
{
    if (!func_name.data || !functions)
        return false;

    for (int i = 0; i < function_count; i++)
    {
        if (strcmp(func_name.data, functions[i]->name) == 0)
        {
            return true;
        }
    }
    return false;
}

const StdrotEntry *get_native_function(const String func_name)
{
    if (!func_name.data || !functions)
        return NULL;

    for (int i = 0; i < function_count; i++)
    {
        if (strcmp(func_name.data, functions[i]->name) == 0)
        {
            return functions[i];
        }
    }
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
    case STDROT_ANY:
    case STDROT_HANDLE:
    case STDROT_NONE:
    default:
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

void execute_builtin_function(const String func_name, ArgumentList *args)
{
    execute_func_call(func_name, args);
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
        switch (var->var_type)
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
            if (var->is_array)
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
            result->type == STDROT_CSTRING || result->type == STDROT_ANY)
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
            default:
                actual_name = "STDROT_ANY";
                break;
            }
            fprintf(stderr,
                    "Error: stdrot: native '%s' is declared to return "
                    "STDROT_ANY (legacy/untyped export or "
                    "identity-polymorphic result) but actually returned a "
                    "%s -- pointer/handle/cstring results require an "
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
        value->val.i = value->val.c;
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

static bool coerce_arg_to_param(StdrotValue *value, StdrotType declared)
{
    if (value->type == declared)
        return false;

    if (coerce_numeric(value, declared))
        return false;

    switch (declared)
    {
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
           still-mismatched value afterward. */
        return false;
    }
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
        return;
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
        }
        SAFE_FREE(frame->arg_values);
        SAFE_FREE(frame->owned_string_bufs);
        SAFE_FREE(frame->owned_cstring_bufs);
    }
    pending_native_call_stack = NULL;
}

NativeResult execute_native_call(const String func_name, ArgumentList *args)
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

    /* Set execution context - get line number from first argument node */
    g_exec_context.function_name.data = func_name.data;
    g_exec_context.line_number = 0;
    if (args && args->expr && args->expr->line_number > 0)
    {
        g_exec_context.line_number = args->expr->line_number;
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
           uncaught (see enforce_return_type()'s comment). No
           argument-marshalling cleanup needed here since none was
           allocated -- and with no arguments, a STDROT_STRING result
           can't possibly alias one, so owns_string is always false. */
        enforce_return_type(func_name, &result, entry, NULL);
        return (NativeResult){result, false};
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
    if (!arg_values || !owned_string_bufs || !owned_cstring_bufs)
    {
        SAFE_FREE(arg_values);
        SAFE_FREE(owned_string_bufs);
        SAFE_FREE(owned_cstring_bufs);
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
                                   .arg_count = total_args,
                                   .next = pending_native_call_stack};
    pending_native_call_stack = &frame;

    int arg_count = 0;
    ArgumentList *cur = args;
    while (cur && cur->expr && arg_count < total_args)
    {
        ASTNode *expr = cur->expr;

        ast_expr_to_stdrot_value(expr, &arg_values[arg_count]);
        owned_string_bufs[arg_count] =
            (expr->type == NODE_FUNC_CALL &&
             arg_values[arg_count].type == STDROT_STRING)
                ? arg_values[arg_count].val.str.data
                : NULL;
        owned_cstring_bufs[arg_count] = NULL;

        /* Convert to whatever entry->params[arg_count] actually declared
           -- see coerce_arg_to_param()'s comment for why this has to
           happen here, not just at the semantic-analysis type check. */
        if (arg_count < entry->param_count && entry->params)
        {
            if (coerce_arg_to_param(&arg_values[arg_count],
                                    entry->params[arg_count].type))
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
    bool owns_string = false;
    if (result.type == STDROT_STRING)
    {
        result.val.str = safe_strdup(&result.val.str);
        owns_string = true;
    }

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
    }

    SAFE_FREE(arg_values);
    SAFE_FREE(owned_string_bufs);
    SAFE_FREE(owned_cstring_bufs);

    return (NativeResult){result, owns_string};
}

void execute_func_call(const String func_name, ArgumentList *args)
{
    NativeResult nr = execute_native_call(func_name, args);
    StdrotValue result = nr.value;

    /* Deprecated write-back: if first arg is an identifier and the function
     * returned a value, also write the returned value back to that variable.
     * This is the pre-#204 calling convention (`slorp(x);` instead of
     * `rizz x = slorp(...);`); it is kept working for one release, with a
     * warning, so existing programs don't break outright. New code should
     * consume the return value directly -- see execute_native_call() /
     * handle_function_call(). */
    if (result.type != STDROT_NONE && args && args->expr &&
        args->expr->type == NODE_IDENTIFIER)
    {
        const String name = args->expr->data.name;
        Variable *var = get_variable(name);
        if (var)
        {
            fprintf(stderr,
                    "Warning: line %d: `%s(%s, ...)` writing its result back "
                    "into `%s` is deprecated -- use `%s = %s(...)` instead\n",
                    g_exec_context.line_number, func_name.data, name.data,
                    name.data, name.data, func_name.data);
            switch (result.type)
            {
            case STDROT_INT:
                set_int_variable(name, result.val.i, var->modifiers);
                break;
            case STDROT_FLOAT:
                set_float_variable(name, result.val.f, var->modifiers);
                break;
            case STDROT_DOUBLE:
                set_double_variable(name, result.val.d, var->modifiers);
                break;
            case STDROT_SHORT:
                set_short_variable(name, result.val.s, var->modifiers);
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
                set_char_variable(name, result.val.c, var->modifiers);
                break;
            case STDROT_STRING:
                if (var->is_array && var->var_type == VAR_CHAR &&
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
                set_bool_variable(name, result.val.b, var->modifiers);
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
