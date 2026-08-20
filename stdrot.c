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
 *        discovers all functions via stdrot_get_api()
 *     2. Thin varargs stubs (yapping/yappin/baka) and per-type slorp/
 *        ragequit/chill stubs that dlsym their real implementation by name
 *        on first use
 *
 *   STDROT_STATIC defined (wasm build, see `make wasm`):
 *     the stdrot sources are compiled directly into the same binary —
 *     there is no .so and no dlopen surface at all (wasm has no dynamic
 *     loader worth using for a single-artifact build). stdrot_load() calls
 *     stdrot_get_api() directly, and ragequit/chill/slorp_* are provided
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
String slorp_string(String string, size_t size);
int slorp_int(int val);
short slorp_short(short val);
float slorp_float(float var);
double slorp_double(double var);
#endif

#ifdef STDROT_STATIC
/* Statically linked in from stdrot/yapping.c and stdrot/baka.c — called
 * directly below instead of going through dlsym-by-name.
 * stdrot_get_api() (statically linked from stdrot/registry.c) is already
 * declared by stdrot_api.h, included transitively via stdrot.h above. */
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

/* ── Loader ──────────────────────────────────────────────────────────────── */

#ifdef STDROT_STATIC

void stdrot_load(void)
{
    /* stdrot/registry.c is linked directly into this binary, so the
     * function table is just a direct call away — no loader needed. */
    StdrotAPI api = stdrot_get_api();
    functions = api.functions;
    function_count = api.count;
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

    /* Get the API entrypoint */
    StdrotAPI (*get_api)(void);
    *(void **)(&get_api) = dlsym(lib_handle, "stdrot_get_api");
    if (!get_api)
    {
        fprintf(stderr, "libstdrot.so missing stdrot_get_api(): %s\n",
                dlerror());
        dlclose(lib_handle);
        exit(EXIT_FAILURE);
    }

    /* Discover all functions */
    StdrotAPI api = get_api();
    functions = api.functions;
    function_count = api.count;
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
    /* Gated to NODE_FUNC_CALL specifically (a native call returning a char
       or string, e.g. `yapping("%s", slorp(buf))`) rather than folded into
       the general is_expression() checks above: NODE_ARRAY_ACCESS is
       unconditionally treated as int below regardless of element type
       (e.g. a lone `buf[0]`), and is_expression(expr, VAR_CHAR) would
       otherwise also match that case, changing its marshalling too. */
    else if (expr->type == NODE_FUNC_CALL && is_expression(expr, VAR_CHAR))
    {
        out->type = STDROT_CHAR;
        out->val.c = (char)evaluate_expression_int(expr);
    }
    else if (expr->type == NODE_FUNC_CALL && is_expression(expr, VAR_STRING))
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
           a different front door. Static and runtime would disagree
           about what this expression actually is -- exactly the class of
           bug this whole enforcement function exists to close. */
        if (result->type == STDROT_PTR || result->type == STDROT_HANDLE ||
            result->type == STDROT_CSTRING)
        {
            const char *actual_name = result->type == STDROT_PTR ? "STDROT_PTR"
                                      : result->type == STDROT_HANDLE
                                          ? "STDROT_HANDLE"
                                          : "STDROT_CSTRING";
            fprintf(stderr,
                    "Error: stdrot: native '%s' is declared to return "
                    "STDROT_ANY (legacy/untyped export or "
                    "identity-polymorphic result) but actually returned a "
                    "%s -- pointer/handle/cstring results require an "
                    "explicit typed signature, not the unchecked ANY "
                    "fallback\n",
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
            value->type = STDROT_CSTRING;
            value->val.cstr = cstr;
            return cstr != NULL;
        }
        return false;
    default:
        /* STDROT_BOOL/STDROT_CHAR/STDROT_STRING/STDROT_ANY/STDROT_PTR/
           STDROT_HANDLE/STDROT_NONE: either the semantic checker already
           requires an exact match (no coercion needed), or the analyzer
           refuses to accept a call whose signature declares these at all
           (see semantic_check_native_call()) -- so this is unreachable
           for a program that passed semantic analysis. */
        return false;
    }
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

StdrotValue execute_native_call(const String func_name, ArgumentList *args)
{
    if (!func_name.data || !functions)
    {
        yyerror("Function not found");
        return (StdrotValue){STDROT_NONE, {0}};
    }

    const StdrotEntry *entry = get_native_function(func_name);

    if (!entry || !entry->fn)
    {
        yyerror("Unknown function");
        return (StdrotValue){STDROT_NONE, {0}};
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
           allocated. */
        enforce_return_type(func_name, &result, entry, NULL);
        return result;
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
        return (StdrotValue){STDROT_NONE, {0}};
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
        if (arg_count < entry->param_count && entry->params &&
            coerce_arg_to_param(&arg_values[arg_count],
                                entry->params[arg_count].type))
        {
            owned_cstring_bufs[arg_count] = arg_values[arg_count].val.cstr;
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

    /* Reached: entry->fn() returned normally rather than exit()ing, so
       ordinary cleanup below handles this frame, and the atexit handler
       above must not also try to (double-free) -- pop it first. Any
       nested call's own frame already popped itself the same way before
       returning here, so this is always exactly our own frame. */
    pending_native_call_stack = frame.next;

    /* None of the registered natives return one of their own string
       arguments back out (slorp -- the one native that could reuse an
       argument buffer in its result -- only ever takes a plain identifier
       per the grammar, never a nested call), so freeing here can't dangle
       a pointer the caller is still holding. */
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

    return result;
}

void execute_func_call(const String func_name, ArgumentList *args)
{
    StdrotValue result = execute_native_call(func_name, args);

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
                set_int_variable(name, result.val.c, var->modifiers);
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
                break;
            case STDROT_BOOL:
                set_bool_variable(name, result.val.b, var->modifiers);
                break;
            default:
                break;
            }
        }
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

String slorp_string(String string, size_t size)
{
    String s = {.data = "slorp_string", .len = sizeof("slorp_string") - 1};
    String (*fn)(String, size_t) =
        (String(*)(String, size_t))stdrot_lookup_symbol(s);
    return fn ? fn(string, size) : string;
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
