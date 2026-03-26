/* stdrot.c - Standard Brainrot library loader and AST bridge
 *
 * This file is the glue between:
 *   • the AST/interpreter (understands ASTNode, ArgumentList, Variable, etc.)
 *   • libstdrot.so (pure I/O functions, zero interpreter dependency)
 *
 * It provides:
 *   1. Dynamic loader (stdrot_load/unload) that opens libstdrot.so and
 *      discovers all functions via stdrot_get_api()
 *   2. Thin varargs stubs (yapping/yappin/baka) that forward to the .so
 *   3. AST bridge functions (execute_*_call) that evaluate arguments and
 *      call the raw implementations
 */

#include "stdrot.h"
#include "ast.h"
#include "lib/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>

/* ── Global execution context ────────────────────────────────────────────── */
ExecutionContext g_exec_context = {0, NULL, NULL};

/* ── External interpreter functions ──────────────────────────────────────── */
extern void yyerror(const char *s);
extern int evaluate_expression_int(ASTNode *node);
extern float evaluate_expression_float(ASTNode *node);
extern double evaluate_expression_double(ASTNode *node);
extern short evaluate_expression_short(ASTNode *node);
extern bool evaluate_expression_bool(ASTNode *node);
extern bool is_expression(ASTNode *node, VarType type);
extern Variable *get_variable(const char *name);
extern TypeModifiers get_variable_modifiers(const char* name);
extern void *evaluate_multi_array_access(ASTNode *node);
extern bool set_int_variable(const char *name, int value, TypeModifiers mods);
extern bool set_float_variable(const char *name, float value, TypeModifiers mods);
extern bool set_double_variable(const char *name, double value, TypeModifiers mods);
extern bool set_short_variable(const char *name, short value, TypeModifiers mods);
extern bool set_bool_variable(const char *name, bool value, TypeModifiers mods);

/* ── Dynamic library state ────────────────────────────────────────────────── */
static void *lib_handle = NULL;
static StdrotEntry *functions = NULL;
static int function_count = 0;

/* Symbol cache to avoid repeated dlsym calls */
#define STDROT_CACHE_SIZE 64
typedef struct {
    const char *name;
    void *ptr;
} SymbolCache;

static SymbolCache symbol_cache[STDROT_CACHE_SIZE];
static int cache_count = 0;

/* ── Forward declarations of stub functions ──────────────────────────────── */
void yapping(const char* format, ...);
void yappin(const char* format, ...);
void baka(const char* format, ...);
void ragequit(int exit_code);
void chill(unsigned int seconds);
char slorp_char(char chr);
char *slorp_string(char *string, size_t size);
int slorp_int(int val);
short slorp_short(short val);
float slorp_float(float var);
double slorp_double(double var);

/* ── Dynamic symbol lookup with caching ──────────────────────────────────── */

static void *stdrot_lookup_symbol(const char *symbol_name)
{
    if (!lib_handle || !symbol_name) return NULL;

    /* Check cache first */
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(symbol_cache[i].name, symbol_name) == 0) {
            return symbol_cache[i].ptr;
        }
    }

    /* Not in cache, lookup via dlsym */
    void *ptr = dlsym(lib_handle, symbol_name);
    if (ptr && cache_count < STDROT_CACHE_SIZE) {
        symbol_cache[cache_count].name = symbol_name;
        symbol_cache[cache_count].ptr = ptr;
        cache_count++;
    }

    return ptr;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

/* ── Loader ──────────────────────────────────────────────────────────────── */

void stdrot_load(void)
{
    /* First, make main binary symbols available to subsequently loaded libraries
     * by loading the main program's symbols with RTLD_GLOBAL
     */
    dlopen(NULL, RTLD_LAZY | RTLD_GLOBAL);
    
    /* Open libstdrot.so from the same directory as the binary, or LD_LIBRARY_PATH
     * Use RTLD_GLOBAL so the library can access symbols from the main binary (e.g., g_exec_context)
     */
    lib_handle = dlopen("./libstdrot.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!lib_handle) {
        lib_handle = dlopen("libstdrot.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    if (!lib_handle) {
        fprintf(stderr, "Failed to load libstdrot.so: %s\n", dlerror());
        exit(EXIT_FAILURE);
    }

    /* Get the API entrypoint */
    StdrotAPI (*get_api)(void);
    *(void **)(&get_api) = dlsym(lib_handle, "stdrot_get_api");
    if (!get_api) {
        fprintf(stderr, "libstdrot.so missing stdrot_get_api(): %s\n", dlerror());
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
    if (lib_handle) {
        dlclose(lib_handle);
        lib_handle = NULL;
        functions = NULL;
        function_count = 0;
        cache_count = 0;
    }
}

/* ── Runtime query ────────────────────────────────────────────────────────── */

bool is_builtin_function(const char *func_name)
{
    if (!func_name || !functions) return false;

    for (int i = 0; i < function_count; i++) {
        if (strcmp(func_name, functions[i].name) == 0) {
            return true;
        }
    }
    return false;
}

void execute_builtin_function(const char *func_name, ArgumentList *args)
{
    execute_func_call(func_name, args);
}

static void ast_expr_to_stdrot_value(ASTNode *expr, StdrotValue *out)
{
    out->type = STDROT_NONE;
    if (!expr) return;

    switch (expr->type) {
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
    case NODE_IDENTIFIER: {
        Variable *var = get_variable(expr->data.name);
        if (!var) return;
        switch (var->var_type) {
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
            if (var->is_array) {
                out->type = STDROT_STRING;
                out->val.str = (char *)var->value.array_data;
            } else {
                out->type = STDROT_CHAR;
                out->val.c = (char)var->value.ivalue;
            }
            return;
        case VAR_STRING:
            out->type = STDROT_STRING;
            out->val.str = (char *)var->value.array_data;
            return;
        default:
            return;
        }
    }
    default:
        break;
    }

    /* General expression fallback */
    if (is_expression(expr, VAR_BOOL)) {
        out->type = STDROT_BOOL;
        out->val.b = evaluate_expression_bool(expr);
    } else if (is_expression(expr, VAR_SHORT)) {
        out->type = STDROT_SHORT;
        out->val.s = evaluate_expression_short(expr);
    } else if (is_expression(expr, VAR_FLOAT)) {
        out->type = STDROT_FLOAT;
        out->val.f = evaluate_expression_float(expr);
    } else if (is_expression(expr, VAR_DOUBLE)) {
        out->type = STDROT_DOUBLE;
        out->val.d = evaluate_expression_double(expr);
    } else if (is_expression(expr, VAR_INT) || expr->type == NODE_ARRAY_ACCESS || expr->type == NODE_OPERATION || expr->type == NODE_UNARY_OPERATION    || expr->type == NODE_STRUCT_ACCESS) {
        out->type = STDROT_INT;
        out->val.i = evaluate_expression_int(expr);
    }
}

void execute_func_call(const char *func_name, ArgumentList *args)
{
    if (!func_name || !functions) {
        yyerror("Function not found");
        return;
    }

    /* Look up function in the registry */
    StdrotEntry *entry = NULL;
    for (int i = 0; i < function_count; i++) {
        if (strcmp(functions[i].name, func_name) == 0) {
            entry = &functions[i];
            break;
        }
    }

    if (!entry || !entry->fn) {
        yyerror("Unknown function");
        return;
    }

    /* Set execution context - get line number from first argument node */
    g_exec_context.function_name = func_name;
    g_exec_context.line_number = 0;
    if (args && args->expr && args->expr->line_number > 0) {
        g_exec_context.line_number = args->expr->line_number;
    }

    /* Generic function call - evaluate all arguments to StdrotValue */
    StdrotValue arg_values[64];
    int arg_count = 0;

    ArgumentList *cur = args;
    while (cur && arg_count < 64) {
        ASTNode *expr = cur->expr;
        if (!expr) break;

        ast_expr_to_stdrot_value(expr, &arg_values[arg_count]);

        arg_count++;
        cur = cur->next;
    }

    StdrotValue result = entry->fn(arg_values, arg_count);

    /* Generic write-back: if first arg is an identifier and function returned a value,
     * write the returned value back to that variable. */
    if (result.type != STDROT_NONE && args && args->expr && args->expr->type == NODE_IDENTIFIER) {
        const char *name = args->expr->data.name;
        Variable *var = get_variable(name);
        if (var) {
            switch (result.type) {
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
                if (var->is_array && var->array_length > 0 && result.val.str) {
                    char *dst = (char *)var->value.array_data;
                    /* Avoid overlapping copy */
                    if (dst != result.val.str) {
                        strncpy(dst, result.val.str, var->array_length - 1);
                        dst[var->array_length - 1] = '\0';
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

/* ── Stub functions (thin wrappers that forward to .so) ────────────────────── */

void yapping(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    void (*fn)(const char *, va_list) = (void (*)(const char *, va_list))stdrot_lookup_symbol("v_yapping");
    if (fn) fn(format, ap);
    va_end(ap);
}

void yappin(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    void (*fn)(const char *, va_list) = (void (*)(const char *, va_list))stdrot_lookup_symbol("v_yappin");
    if (fn) fn(format, ap);
    va_end(ap);
}

void baka(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    void (*fn)(const char *, va_list) = (void (*)(const char *, va_list))stdrot_lookup_symbol("v_baka");
    if (fn) fn(format, ap);
    va_end(ap);
}

void ragequit(int exit_code)
{
    void (*fn)(int) = (void (*)(int))stdrot_lookup_symbol("ragequit");
    if (fn) fn(exit_code);
}

void chill(unsigned int seconds)
{
    void (*fn)(unsigned int) = (void (*)(unsigned int))stdrot_lookup_symbol("chill");
    if (fn) fn(seconds);
}

char slorp_char(char chr)
{
    char (*fn)(char) = (char (*)(char))stdrot_lookup_symbol("slorp_char");
    return fn ? fn(chr) : chr;
}

char *slorp_string(char *string, size_t size)
{
    char *(*fn)(char *, size_t) = (char *(*)(char *, size_t))stdrot_lookup_symbol("slorp_string");
    return fn ? fn(string, size) : string;
}

int slorp_int(int val)
{
    int (*fn)(int) = (int (*)(int))stdrot_lookup_symbol("slorp_int");
    return fn ? fn(val) : val;
}

short slorp_short(short val)
{
    short (*fn)(short) = (short (*)(short))stdrot_lookup_symbol("slorp_short");
    return fn ? fn(val) : val;
}

float slorp_float(float var)
{
    float (*fn)(float) = (float (*)(float))stdrot_lookup_symbol("slorp_float");
    return fn ? fn(var) : var;
}

double slorp_double(double var)
{
    double (*fn)(double) = (double (*)(double))stdrot_lookup_symbol("slorp_double");
    return fn ? fn(var) : var;
}

#pragma GCC diagnostic pop
