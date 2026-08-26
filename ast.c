/* ast.c */

#include "ast.h"
#include "stdrot.h"
#include "visitor.h"
#include "interpreter.h"
#include "lib/mem.h"
#include <stdbool.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

JumpBuffer *jump_buffer = {0};

HashMap *function_map = NULL;
static HashMap *static_variable_map = NULL;
static HashMap *struct_registry = NULL;
static StructDef *struct_registry_list = NULL;
static HashMap *enum_registry = NULL;
static EnumDef *enum_registry_list = NULL;
static HashMap *type_alias_registry = NULL;
static TypeAlias *type_alias_registry_list = NULL;
bool struct_def_had_error = false;
/* Set by lit alias helpers below; lang.y's post-yyparse gate aborts before
   semantic analysis/execution when true, and cleanup resets it through
   free_type_alias_registry(). */
bool typedef_had_error = false;
ReturnValue current_return_value;
Arena arena;

TypeModifiers current_modifiers = {false, false, false, false,
                                   false, false, false, false};
extern VarType current_var_type;

Scope *current_scope;

/* Include the symbol table functions */
extern void yyerror(const char *s);
extern void yyerror_current_line(const char *s);
extern void cleanup(void);
extern const char *vartype_to_string(VarType type);
extern int yylineno;
static int get_function_return_pointer_level(const String name);
String evaluate_expression_string(ASTNode *node);

/* ── Native-call memo cache ──────────────────────────────────────────────
 * A TYPED native (stdrot) call -- Phase 2 (issue #205) -- DOES have a
 * static return type, readable off its registered StdrotEntry with no
 * execution at all (get_native_call_static_type(), infer_runtime_
 * expression_type_noeval(), below): a fixed return_type, or an identity-
 * polymorphic return_like_arg resolved from the referenced argument's own
 * (execution-free) type. This cache exists for what's left: a genuine
 * legacy/untyped STDROT_EXPORT() export (return_type.type == STDROT_ANY,
 * no return_like_arg) still has no way to know its return type short of
 * running it, and expression evaluation routinely asks that question once
 * (e.g. handle_binary_operation peeking at operand types via get_
 * expression_type) and then reads the real value a second time (e.g.
 * evaluate_expression_short calling handle_function_call on the same
 * node) -- without this cache, that would invoke the native function, and
 * any side effects it has, twice per read, for that legacy case. (A TYPED
 * native's value-producing call still goes through native_call_consume()
 * too, purely for uniformity -- it just never needs a prior peek to have
 * populated the cache first, since its type never required one.)
 * native_call_peek() answers type-probe questions from the cache without
 * consuming it; native_call_consume(), used only by the terminal value-
 * producing call (handle_function_call), reads it and clears it so the
 * next syntactic visit to this call site (e.g. the next loop iteration)
 * invokes fresh.
 *
 * This is strictly a type-probe-then-consume cache within a single real
 * evaluation. It is deliberately *not* used to paper over
 * interpreter.c's separate generic-AST-walk pre-visit of a call embedded in
 * a declaration/assignment/return/print/error statement (or a do-while
 * condition) -- priming the cache from that pre-visit ran the call before
 * its argument variables necessarily existed (e.g. `rizz n = slorp(n);`
 * evaluated the pre-visit's `n` before interpreter_visit_declaration had
 * created it) or before a loop body had executed once, and then fed that
 * wrong/stale result back to the real evaluation as a cache hit. See
 * interpreter_visit_function_call()'s comment for how that's avoided
 * instead: the pre-visit does nothing for builtins, full stop.
 *
 * A binary operation with two native-call operands (e.g. `slorp(a) +
 * slorp(b)`) peeks *both* operands (get_expression_type on each, to decide
 * the promoted type) before either is consumed -- so this can't be a single
 * pending slot, or peeking the second call would evict the first call's
 * still-unconsumed entry and force a spurious re-invocation. A table of
 * independently-tracked entries, keyed by node identity, avoids that --
 * grown dynamically (native_call_cache_ensure_capacity(), below) rather
 * than a fixed-size array. A fixed cap here is not a harmless
 * implementation detail: it would mean the type this pipeline reports
 * for a legacy/untyped native call (see get_native_call_static_type()'s
 * own comment for why a TYPED native no longer needs this cache at all)
 * depends on how many OTHER pending type-probes happen to already be in
 * flight when it's asked -- the same expression's static type changing
 * depending on unrelated cache occupancy is exactly the kind of
 * capacity-dependent language semantics this pipeline must never have.
 * Growth failure (allocator exhaustion) is the one case still reported
 * as an unknown (STDROT_NONE) type rather than crashing -- indistinguishable
 * from any other allocation failure elsewhere in this interpreter, not a
 * deliberately-designed capacity limit. */
#define NATIVE_CALL_CACHE_INITIAL_CAPACITY 16
typedef struct
{
    ASTNode *node;
    /* The full NativeResult (stdrot.h), not just its StdrotValue -- string
       ownership travels with the cached value itself, the same way it
       travels through every other consumer of execute_native_call()'s
       result (see NativeResult's own comment for why this can't be a
       separate global). A cache hit hands back exactly the NativeResult
       that was cached, ownership bit included. */
    NativeResult result;
    bool valid;
} NativeCallCacheEntry;
static NativeCallCacheEntry *native_call_cache = NULL;
static int native_call_cache_capacity = 0;
static bool native_call_cache_cleanup_registered = false;

/* Frees native_call_cache itself, plus any owned string still sitting
 * inside a *valid, unconsumed* entry -- e.g. a peeked-but-never-consumed
 * legacy STDROT_STRING result orphaned by a rejected sizeof operand
 * (handle_sizeof() erroring out on a nested unknown-type call before the
 * peek it already performed elsewhere in the same expression was ever
 * consumed). Assuming every entry needs no per-entry cleanup was wrong:
 * a NativeResult's owns_string (see its own comment, stdrot.h) means
 * *this array* is the sole owner of that heap string until consumed, the
 * same way stdrot.c's own deprecated write-back path (just above) frees
 * an nr.owns_string buffer it's the last consumer of -- this is that
 * same obligation, for whichever entries never got that far. Registered
 * via atexit() -- see native_call_cache_grow() -- the same lazy-
 * registration pattern stdrot.c's free_pending_native_call_args()
 * already uses for its own atexit hook, since this array (unlike the old
 * fixed-size one it replaced) is now a genuine heap allocation that
 * would otherwise leak. */
static void free_native_call_cache(void)
{
    for (int i = 0; i < native_call_cache_capacity; i++)
    {
        if (native_call_cache[i].valid &&
            native_call_cache[i].result.owns_string &&
            native_call_cache[i].result.value.type == STDROT_STRING)
        {
            SAFE_FREE(native_call_cache[i].result.value.val.str.data);
        }
    }
    SAFE_FREE(native_call_cache);
    native_call_cache_capacity = 0;
}

/* Doubles native_call_cache's capacity (or allocates the initial one),
 * preserving every existing entry -- SAFE_MALLOC_ARRAY() zero-initializes
 * the new buffer, so every newly added slot's `valid` already reads
 * false with no separate init loop needed. Returns false (leaving the
 * existing cache, if any, untouched and fully usable) only on genuine
 * allocation failure -- the sole case native_call_peek() below still
 * reports STDROT_NONE for, same as any other out-of-memory condition in
 * this interpreter. */
static bool native_call_cache_grow(void)
{
    int new_capacity = native_call_cache_capacity == 0
                           ? NATIVE_CALL_CACHE_INITIAL_CAPACITY
                           : native_call_cache_capacity * 2;
    NativeCallCacheEntry *grown =
        SAFE_MALLOC_ARRAY(NativeCallCacheEntry, new_capacity);
    if (!grown)
        return false;

    if (native_call_cache_capacity > 0)
    {
        memcpy(grown, native_call_cache,
               sizeof(NativeCallCacheEntry) *
                   (size_t)native_call_cache_capacity);
        SAFE_FREE(native_call_cache);
    }
    native_call_cache = grown;
    native_call_cache_capacity = new_capacity;

    if (!native_call_cache_cleanup_registered)
    {
        atexit(free_native_call_cache);
        native_call_cache_cleanup_registered = true;
    }
    return true;
}

static NativeResult native_call_peek(ASTNode *node)
{
    int free_slot = -1;
    for (int i = 0; i < native_call_cache_capacity; i++)
    {
        if (native_call_cache[i].valid && native_call_cache[i].node == node)
        {
            return native_call_cache[i].result;
        }
        if (free_slot < 0 && !native_call_cache[i].valid)
        {
            free_slot = i;
        }
    }

    if (free_slot < 0)
    {
        free_slot = native_call_cache_capacity;
        if (!native_call_cache_grow())
        {
            return (NativeResult){{STDROT_NONE, {0}}, false};
        }
    }

    NativeResult result = execute_native_call(
        node->data.func_call.function_name, node->data.func_call.arguments);
    native_call_cache[free_slot].node = node;
    native_call_cache[free_slot].result = result;
    native_call_cache[free_slot].valid = true;
    return result;
}

static NativeResult native_call_consume(ASTNode *node)
{
    for (int i = 0; i < native_call_cache_capacity; i++)
    {
        if (native_call_cache[i].valid && native_call_cache[i].node == node)
        {
            native_call_cache[i].valid = false;
            return native_call_cache[i].result;
        }
    }
    return execute_native_call(node->data.func_call.function_name,
                               node->data.func_call.arguments);
}

/* Helper to build a namespaced static key */
static String make_static_key(const String func_name, const String var_name)
{
    static char buf[MAX_BUFFER_LEN];
    size_t len = (size_t)snprintf(buf, sizeof(buf), "%s::%s",
                                  func_name.data ? func_name.data : "__global",
                                  var_name.data);
    return (String){.data = buf, .len = len};
}

size_t get_type_size_for_descriptor(VarType type, int pointer_level,
                                    TypeModifiers mods)
{
    if (pointer_level > 0)
    {
        return sizeof(uintptr_t);
    }

    switch (type)
    {
    case VAR_FLOAT:
        return sizeof(float);
    case VAR_DOUBLE:
        return sizeof(double);
    case VAR_BOOL:
        return sizeof(bool);
    case VAR_SHORT:
        return mods.is_unsigned ? sizeof(unsigned short) : sizeof(short);
    case VAR_CHAR:
        return sizeof(char);
    case VAR_INT:
        if (mods.is_long_long)
            return sizeof(long long);
        if (mods.is_long)
            return sizeof(long);
        if (mods.is_unsigned)
            return sizeof(unsigned int);
        return sizeof(int);
    case VAR_STRING:
        return sizeof(String);
    case VAR_ENUM:
        return sizeof(int);
    case VAR_VOID: /* genuinely zero-sized -- not "unknown, guess 0" */
    case NONE:
    default:
        return 0;
    }
}

static void write_value_to_address(void *address, VarType type,
                                   int pointer_level, ASTNode *expr,
                                   TypeModifiers mods, bool packed_storage);
static void initialize_variable_from_expr(Variable *var, ASTNode *expr);

// Symbol table functions
bool set_variable(const String name, void *value, VarType type,
                  TypeModifiers mods)
{
    Variable *var = get_variable(name);
    if (var != NULL)
    {

        var->modifiers = mods;
        var->var_type = type;
        switch (type)

        case VAR_INT:
        {
            if (var->modifiers.is_long)
            {
                var->value.ivalue = (long long)(*(int *)value);
            }
            else if (var->modifiers.is_long_long)
            {
                var->value.ivalue = (long)(*(int *)value);
            }
            else
            {
                var->value.ivalue = *(int *)value;
            }
            break;
        case VAR_SHORT:
            var->value.svalue = *(short *)value;
            break;
        case VAR_FLOAT:
            var->value.fvalue = *(float *)value;
            break;
        case VAR_DOUBLE:
            var->value.dvalue = *(double *)value;
            break;
        case VAR_BOOL:
            var->value.bvalue = *(bool *)value;
            break;
        case VAR_CHAR:
            var->value.ivalue = *(unsigned char *)value;
            break;
        case VAR_STRING:
            var->value.strvalue = ARENA_STRDUP(*(String *)value);
            break;
        case VAR_STRUCT:
            /* struct blob is managed separately via array_data; nothing to copy
             * here */
            break;
        case VAR_ENUM:
            var->value.ivalue = *(int *)value;
            break;
        case VAR_PTR:
        /* VAR_PTR only ever appears as the *inferred type of an
           expression* (a native call returning STDROT_PTR) -- no
           Brainrot declaration syntax can give an actual Variable this
           as its own var_type, so this is structurally unreachable. Same
           reasoning for VAR_VOID: no declaration syntax can give a
           Variable "void" as its own type either -- it only ever
           appears as an expression's inferred type (a call whose
           descriptor return type is STDROT_NONE). */
        case VAR_VOID:
        case NONE:
            break;
        }
            return true;
    }
    return false; // Symbol table is full
}

bool set_multi_array_variable(const String name, const int dimensions[],
                              int num_dimensions, TypeModifiers mods,
                              VarType type)
{
    Variable *var = get_variable(name);
    if (var == NULL)
        return false;

    var->is_array = true;
    var->modifiers = mods;
    var->var_type = type;

    // calculate the total size of the array
    var->array_dimensions.num_dimensions = num_dimensions;
    size_t total = 1;

    for (int i = 0; i < num_dimensions; i++)
    {
        var->array_dimensions.dimensions[i] = dimensions[i];
        total *= dimensions[i];
    }

    var->array_dimensions.total_size = total;
    var->array_length = total;

    size_t element_size =
        get_type_size_for_descriptor(type, var->pointer_level, mods);
    if (element_size == 0)
        element_size = sizeof(int);

    var->value.array_data = safe_malloc_array(total, element_size);
    if (var->value.array_data == NULL)
    {
        return false;
    }

    memset(var->value.array_data, 0, total * element_size);
    return true;
}

ASTNode *create_struct_def_node(String name, StructField *fields)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_STRUCT_DEF;
    node->data.struct_def.name = ARENA_STRDUP(name);
    node->data.struct_def.fields =
        fields; /* pointer only — registry owns memory */
    node->data.struct_def.initializer_count = -1;
    return node;
}

ASTNode *create_struct_access_node(ASTNode *object, String member)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_STRUCT_ACCESS;
    node->line_number = yylineno;
    node->data.struct_access.object = object;
    node->data.struct_access.member_name = ARENA_STRDUP(member);
    /* Resolve eagerly (works for plain `a.b` and chains like `a.b.c`, since
       resolve_struct_access recurses on `object` when it is itself a
       NODE_STRUCT_ACCESS) so var_type/pointer_level/struct_name are usable
       by anything inspecting the node directly, without re-deriving them.
       Failures here are expected for forward references / not-yet-declared
       variables at parse time and are silently ignored — semantic analysis
       reports the real errors. */
    StructDef *def = NULL;
    void *base = NULL;
    StructField *fld = NULL;
    if (resolve_struct_access(node, &def, &base, &fld,
                              /* report_errors */ false))
    {
        node->var_type = fld->type;
        node->pointer_level = fld->pointer_level;
        node->modifiers = fld->modifiers;
        if (fld->type == VAR_STRUCT && fld->struct_name.data)
            node->data.struct_access.struct_name =
                ARENA_STRDUP(fld->struct_name);
    }
    return node;
}

ASTNode *create_multi_array_declaration_node(String name,
                                             const int dimensions[],
                                             int num_dimensions, VarType type)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    /* Declaration only: storage is allocated later, at runtime, by the
       interpreter's declaration visitor (see interpreter_visit_declaration)
       in whatever scope is current at execution time -- not here at parse
       time. That's what lets an array declared inside a function body get
       its own storage inside that function's runtime scope, instead of
       leaking into (and colliding across) the single global scope that
       exists throughout parsing. Callers (lang.y) set pointer_level and
       modifiers on the returned node afterwards, and attach any braced
       initializer via set_declaration_pending_initializer(). */
    node->type = NODE_DECLARATION;
    node->var_type = type;
    node->is_array = true;
    node->pointer_level = 0;

    // Store dimensions in node
    for (int i = 0; i < num_dimensions; i++)
    {
        node->array_dimensions.dimensions[i] = dimensions[i];
    }
    node->array_dimensions.num_dimensions = num_dimensions;

    // Calculate total size
    size_t total = 1;
    for (int i = 0; i < num_dimensions; i++)
    {
        total *= dimensions[i];
    }
    node->array_dimensions.total_size = total;
    node->array_length = total; // For backward compatibility

    node->data.op.left = create_identifier_node(name);
    node->data.op.right = NULL;

    return node;
}

ASTNode *create_multi_array_access_node(String name, ASTNode *indices[],
                                        int num_indices)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    node->type = NODE_ARRAY_ACCESS;

    // Store the array name
    node->data.array.name = ARENA_STRDUP(name);

    // Store indices
    node->data.array.num_dimensions = num_indices;
    for (int i = 0; i < num_indices; i++)
    {
        node->data.array.indices[i] = indices[i];
    }

    Variable *var = get_variable(name);
    if (var)
    {
        node->var_type = var->var_type;
        node->pointer_level = var->pointer_level;
        node->modifiers = var->modifiers;
        node->is_array = var->is_array;
    }

    return node;
}

/* The struct-field counterpart of create_multi_array_access_node(): `base`
   is an already-built struct_access expression (e.g. `foo.arr`), not a
   Variable name -- Array.base (ast.h) is what tells every other consumer
   (evaluate_multi_array_access(), get_expression_type()/get_expression_
   pointer_level(), and their semantic_analyzer.c mirrors) to resolve
   through resolve_struct_access() instead of get_variable(). Resolves
   eagerly where possible, same reasoning and same "expected to fail
   silently" contract as create_struct_access_node()'s own comment
   (forward references / not-yet-declared variables at parse time). */
ASTNode *create_struct_field_array_access_node(ASTNode *base,
                                               ASTNode *indices[],
                                               int num_indices)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    node->type = NODE_ARRAY_ACCESS;
    node->data.array.base = base;
    node->data.array.num_dimensions = num_indices;
    for (int i = 0; i < num_indices; i++)
    {
        node->data.array.indices[i] = indices[i];
    }

    StructDef *def = NULL;
    void *field_base = NULL;
    StructField *fld = NULL;
    if (base->type == NODE_STRUCT_ACCESS &&
        resolve_struct_access(base, &def, &field_base, &fld,
                              /* report_errors */ false))
    {
        node->var_type = fld->type;
        node->pointer_level = fld->pointer_level;
        node->modifiers = fld->modifiers;
        /* NOT fld->is_array: this node denotes the INDEXED ELEMENT
           (`foo.arr[i]`), a scalar/pointer value, not the array field
           itself -- fld->is_array (true) describes the field being
           indexed into, not the result of indexing it. Under-indexing
           (fewer indices than the field's own rank, which in C would
           yield a sub-array) is rejected outright by evaluate_struct_
           field_array_access()'s own rank check, so a successfully-
           evaluated node here is always a genuine scalar/pointer
           element; false is correct unconditionally. */
        node->is_array = false;
    }

    return node;
}

// Function to rename the old create_array_access_node to maintain compatibility
ASTNode *create_array_access_node_single(String name, ASTNode *index)
{
    // Create a wrapper that calls the multi-dimensional version with a single
    // index
    ASTNode *indices[1] = {index};
    return create_multi_array_access_node(name, indices, 1);
}

/* Calculate the memory offset for multi-dimensional array access.
   Takes the two pieces an "is this actually an array, and what shape"
   check needs directly, rather than a `Variable *`, so a StructField's
   own is_array/array_dimensions (an array-typed struct field, e.g.
   `chad params[4];`) can reuse this without a fake Variable standing in
   for it -- the logic below never depended on anything else a Variable
   carries. */
size_t calculate_array_offset(bool is_array, const ArrayDimensions *dims,
                              int indices[], int num_indices)
{
    // TEMPORARY FIX: Skip strict dimension checking due to variable lookup bug
    // The issue is that get_variable() sometimes returns the wrong variable
    // This is a complex memory/hash collision bug that needs deeper
    // investigation

    // If the variable is not actually an array or dimensions don't match,
    // try to handle it gracefully instead of crashing
    if (!is_array)
    {
        // Variable is not an array - return offset 0 for single element access
        return 0;
    }

    if (num_indices != dims->num_dimensions)
    {
        // Dimension mismatch - for now, just use the first few indices that are
        // available This is not ideal but prevents crashes
        int actual_indices = (num_indices < dims->num_dimensions)
                                 ? num_indices
                                 : dims->num_dimensions;

        if (actual_indices <= 0)
        {
            return 0; // Fallback to first element
        }

        // Use the available indices for offset calculation
        num_indices = actual_indices;
    }

    // Calculate the offset using row-major order
    size_t offset = 0;

    // For row-major order: offset = i0 * (d1 * d2 * ... * dn-1) + i1 * (d2 *
    // ... * dn-1) + ... + in-1
    for (int i = 0; i < num_indices; i++)
    {
        // Check if the index is within bounds
        if (indices[i] < 0 || indices[i] >= dims->dimensions[i])
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(
                error_msg, sizeof(error_msg),
                "Array index out of bounds: dimension %d (index=%d, size=%d)",
                i + 1, indices[i], dims->dimensions[i]);
            yyerror(error_msg);
            exit(EXIT_FAILURE);
        }

        // Calculate the multiplier for this dimension
        // Multiply by all dimensions to the right
        size_t multiplier = 1;
        for (int j = i + 1; j < num_indices; j++)
        {
            multiplier *= dims->dimensions[j];
        }

        offset += indices[i] * multiplier;
    }

    return offset;
}

/* Resolve a NODE_STRUCT_ACCESS node to the StructDef/base-address/field it
   refers to. Handles arbitrary chains (a.b.c.d...) by recursing on the
   object when it is itself a NODE_STRUCT_ACCESS: the inner call resolves
   the object's own member access, and that resolved field describes what
   the object evaluates to (its nested StructDef and offset within the
   grandparent blob), which becomes the base for this level.

   Chaining through a pointer-typed struct/union field (e.g. `a.ptr.b`
   where `ptr` is `gang Foo *`, #197) follows the pointer, applying the
   same single-level implicit-`->` rule the pointer-typed-VARIABLE base
   case (#196) uses: `pointer_level == 1` dereferences and continues from
   the pointee; `pointer_level > 1` is rejected (C requires an explicit
   `(*x)->`); a null pointer is a clean error, not a crash. */
bool resolve_struct_access(ASTNode *node, StructDef **def_out, void **base_out,
                           StructField **field_out, bool report_errors)
{
    if (!node || node->type != NODE_STRUCT_ACCESS)
    {
        if (report_errors)
            yyerror("Invalid struct member access node");
        return false;
    }

    ASTNode *obj = node->data.struct_access.object;
    const String member = node->data.struct_access.member_name;

    StructDef *parent_def = NULL;
    void *parent_base = NULL;

    if (obj && obj->type == NODE_IDENTIFIER)
    {
        Variable *var = get_variable(obj->data.name);
        if (!var)
        {
            if (report_errors)
                yyerror("Undefined struct or union variable");
            return false;
        }
        if (var->var_type != VAR_STRUCT)
        {
            if (report_errors)
                yyerror("Variable is not a struct or union");
            return false;
        }
        parent_def = get_struct_def(var->struct_name);
        if (!parent_def)
        {
            if (report_errors)
                yyerror("Unknown struct or union type");
            return false;
        }
        /* #196: a pointer-typed struct/union variable (`gang Foo *pp;
           pp.field`) follows the pointer -- the base is whatever `pp`
           points at, not a blob owned by `pp` itself. `pp`'s own union
           slot is `value.pvalue` (an address), never `value.array_data`;
           reading `array_data` for a pointer variable would either
           misinterpret that address as a blob pointer or -- when unset --
           silently calloc a brand-new, disconnected blob (the leak this
           branch's predecessor, PR #247, hardened against without yet
           following the pointer).

           Only ONE level of indirection: `.` as an implicit `->` is
           defensible for `gang Foo *pp` (pointer_level == 1) -- `pp.field`
           reads exactly like C's `pp->field`. It is not defensible for
           `gang Foo **pp` (pointer_level == 2): C requires an explicit
           `(*pp)->field`, because `pvalue` at that level holds the
           address of a `Foo *`, not a `Foo` blob -- reinterpreting those
           bytes as a `Foo` (what treating every pointer_level > 0
           uniformly did before this check, PR #248 review finding 2)
           silently reads/writes through the wrong type. */
        if (var->pointer_level > 1)
        {
            if (report_errors)
                yyerror("Member access via '.' through a multi-level "
                        "pointer (pointer_level > 1) is not supported");
            return false;
        }
        if (var->pointer_level == 1)
        {
            uintptr_t target = var->value.pvalue;
            if (!target)
            {
                if (report_errors)
                    yyerror("Null pointer dereference in struct member "
                            "access");
                return false;
            }
            parent_base = (void *)target;
        }
        else
        {
            /* Lazily allocate blob if missing — handles cases where
               parse-time pointer was invalidated by hashmap resize during
               semantic analysis. */
            if (!var->value.array_data)
            {
                var->value.array_data = calloc(1, parent_def->total_size);
                if (!var->value.array_data)
                {
                    if (report_errors)
                        yyerror("Out of memory for struct/union blob");
                    return false;
                }
            }
            parent_base = var->value.array_data;
        }
    }
    else if (obj && obj->type == NODE_STRUCT_ACCESS)
    {
        StructDef *outer_def = NULL;
        void *outer_base = NULL;
        StructField *outer_field = NULL;
        if (!resolve_struct_access(obj, &outer_def, &outer_base, &outer_field,
                                   report_errors))
            return false;

        if (outer_field->type != VAR_STRUCT)
        {
            if (report_errors)
                yyerror("Member access on non-struct/union field");
            return false;
        }
        parent_def = get_struct_def(outer_field->struct_name);
        if (!parent_def)
        {
            if (report_errors)
                yyerror("Unknown nested struct or union type");
            return false;
        }

        /* Address of the outer field's own storage within its enclosing
           blob. For a plain (non-pointer) nested struct/union field this
           IS the nested blob (structs live inline); for a pointer-typed
           field it's the slot holding the pointer VALUE. */
        void *outer_field_addr = (char *)outer_base + outer_field->offset;

        /* #197: chaining `.` through a pointer-typed struct/union FIELD
           (`n.next.val` where `next` is `gang Node *`) follows the pointer,
           the same single-level-of-indirection rule the pointer-typed
           VARIABLE case above (#196) already uses. The field's slot holds a
           uintptr_t pointer value, not the nested blob, so read it and
           continue member resolution from there. `pointer_level > 1`
           (`gang Node **next`) needs an explicit `(*x)->` in C and is
           rejected for the identical reason the variable case rejects it
           -- the slot holds a `Node *`, not a `Node` blob. */
        if (outer_field->pointer_level > 1)
        {
            if (report_errors)
                yyerror("Member access via '.' through a multi-level "
                        "pointer (pointer_level > 1) is not supported");
            return false;
        }
        if (outer_field->pointer_level == 1)
        {
            uintptr_t target = *(uintptr_t *)outer_field_addr;
            if (!target)
            {
                if (report_errors)
                    yyerror("Null pointer dereference in struct member "
                            "access");
                return false;
            }
            parent_base = (void *)target;
        }
        else
        {
            parent_base = outer_field_addr;
        }
    }
    else
    {
        if (report_errors)
            yyerror("Unsupported struct member access expression");
        return false;
    }

    StructField *fld = find_struct_field(parent_def, member);
    if (!fld)
    {
        if (report_errors)
        {
            char msg[MAX_BUFFER_LEN];
            snprintf(msg, sizeof(msg), "%s '%s' has no member '%s'",
                     parent_def->is_union ? "Union" : "Struct",
                     parent_def->name.data ? parent_def->name.data : "?",
                     member.data);
            yyerror(msg);
        }
        return false;
    }

    *def_out = parent_def;
    *base_out = parent_base;
    *field_out = fld;
    return true;
}

void *evaluate_struct_member_address(ASTNode *node)
{
    StructDef *def = NULL;
    void *base = NULL;
    StructField *fld = NULL;
    if (!resolve_struct_access(node, &def, &base, &fld, true))
        return NULL;
    return (char *)base + fld->offset;
}

// Evaluate a multi-dimensional array access node
/* The struct-field counterpart of evaluate_multi_array_access() below,
   for a `foo.arr[i]` node (Array.base set -- see ast.h's own comment on
   that field). Kept as its own function rather than interleaved into
   the name-based one: that function's existing name-based path has
   several rounds of hard-won, narrowly-targeted bugfixes documented
   inline (its own comments), and splitting keeps this new path from
   perturbing any of that. Mirrors its structure and error-handling
   style closely, field offset standing in for array_data. */
static void *evaluate_struct_field_array_access(ASTNode *node)
{
    ASTNode *base = node->data.array.base;
    int num_indices = node->data.array.num_dimensions;
    if (num_indices <= 0)
    {
        yyerror("Invalid number of array indices");
        exit(EXIT_FAILURE);
    }

    StructDef *def = NULL;
    void *field_base = NULL;
    StructField *fld = NULL;
    if (!resolve_struct_access(base, &def, &field_base, &fld,
                               /* report_errors */ true))
    {
        yyerror("Cannot resolve struct field for array access");
        exit(EXIT_FAILURE);
    }
    if (!fld->is_array)
    {
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "Struct/union field '%.100s' is not an array",
                 fld->name.data ? fld->name.data : "?");
        yyerror(error_msg);
        exit(EXIT_FAILURE);
    }
    /* calculate_array_offset()'s own "TEMPORARY FIX" dimension-mismatch
       handling silently truncates to whichever index count is smaller
       and keeps going -- a leftover workaround for a Variable-lookup
       bug on the name-based path, not a real rank-checking policy. That
       leniency is tolerable for a standalone array's own private
       buffer; it is not tolerable here: an array field lives inline in
       the struct's single blob, so a rank mismatch silently computing
       an offset for the wrong number of dimensions reads/writes into a
       neighboring field's bytes instead of just misreading its own
       array (e.g. `g.cells[1]` on a `rizz cells[2][3];` field would
       silently resolve to `cells[1][0]`'s address and be treated as a
       lone int, not rejected). Reject the mismatch outright here,
       before calculate_array_offset() ever gets a chance to be lenient
       about it. */
    if (num_indices != fld->array_dimensions.num_dimensions)
    {
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "Struct/union field '%.100s' expects %d array index/indices, "
                 "got %d",
                 fld->name.data ? fld->name.data : "?",
                 fld->array_dimensions.num_dimensions, num_indices);
        yyerror(error_msg);
        exit(EXIT_FAILURE);
    }

    int indices[MAX_DIMENSIONS];
    for (int i = 0; i < num_indices; i++)
    {
        ASTNode *index_node = node->data.array.indices[i];
        if (!index_node)
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "Missing index %d for array field '%.100s'", i,
                     fld->name.data ? fld->name.data : "?");
            yyerror(error_msg);
            exit(EXIT_FAILURE);
        }
        indices[i] = evaluate_expression_int(index_node);
    }

    size_t offset = calculate_array_offset(
        fld->is_array, &fld->array_dimensions, indices, num_indices);
    void *element_base = (char *)field_base + fld->offset;

    /* Pointer-ness dominates element stride here too, for the identical
       reason the name-based path below checks it first (that path's own
       "Round-23 review, finding #1" comment). */
    if (fld->pointer_level > 0)
        return (uintptr_t *)element_base + offset;

    switch (fld->type)
    {
    case VAR_INT:
        return (int *)element_base + offset;
    case VAR_SHORT:
        return (short *)element_base + offset;
    case VAR_FLOAT:
        return (float *)element_base + offset;
    case VAR_DOUBLE:
        return (double *)element_base + offset;
    case VAR_BOOL:
        return (bool *)element_base + offset;
    case VAR_CHAR:
        return (char *)element_base + offset;
    default:
        yyerror("Unknown struct array field element type");
        exit(EXIT_FAILURE);
    }
}

void *evaluate_multi_array_access(ASTNode *node)
{
    // Validate the node structure
    if (!node)
    {
        yyerror("Invalid array access node: null node");
        exit(EXIT_FAILURE);
    }
    if (node->type != NODE_ARRAY_ACCESS)
    {
        yyerror("Invalid node type for array access");
        exit(EXIT_FAILURE);
    }
    if (node->data.array.base)
        return evaluate_struct_field_array_access(node);

    // CRITICAL: Store the array name in a local copy IMMEDIATELY
    // The array name might be corrupted if we access node->data.array.name
    // after evaluating indices, due to union memory layout issues
    char array_name_buffer[MAX_BUFFER_LEN];

    const String original_array_name = node->data.array.name;
    if (!original_array_name.data)
    {
        yyerror("Invalid array access node: missing array name");
        exit(EXIT_FAILURE);
    }

    size_t name_len = original_array_name.len;

    if (name_len == 0 || name_len >= sizeof(array_name_buffer))
    {
        yyerror("Invalid array name in array access");
        exit(EXIT_FAILURE);
    }

    memcpy(array_name_buffer, original_array_name.data, name_len);
    array_name_buffer[name_len] = '\0';
    const String array_name = {
        .data = array_name_buffer,
        .len = original_array_name.len // ← use the actual name length
    };

    // Also store num_dimensions locally before evaluation
    int num_indices = node->data.array.num_dimensions;
    if (num_indices <= 0)
    {
        yyerror("Invalid number of array indices");
        exit(EXIT_FAILURE);
    }

    // Get the variable using the preserved array name
    Variable *var = get_variable(array_name);
    if (var == NULL)
    {
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "Variable '%.100s' is not defined", array_name.data);
        yyerror(error_msg);
        exit(EXIT_FAILURE);
    }
    if (!var->is_array)
    {
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "Variable '%.100s' is not an array", array_name.data);
        yyerror(error_msg);
        exit(EXIT_FAILURE);
    }

    // Extract the indices - evaluate them AFTER we've preserved the array name
    int indices[MAX_DIMENSIONS];

    // Evaluate each index expression - make sure we don't modify the array
    // access node
    for (int i = 0; i < num_indices; i++)
    {
        ASTNode *index_node = node->data.array.indices[i];
        if (!index_node)
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "Missing index %d for array '%.100s'", i, array_name.data);
            yyerror(error_msg);
            exit(EXIT_FAILURE);
        }
        // Evaluate the index expression - this should return an integer value
        // Make sure we're not accidentally treating the index as an array
        // access
        indices[i] = evaluate_expression_int(index_node);

        // After evaluating each index, verify the array name hasn't been
        // corrupted
        if (node->data.array.name.data != original_array_name.data)
        {
            // Restore the original array name if it was modified
            // Note: We need to cast away const because the field is not const
            node->data.array.name.data = (char *)original_array_name.data;
        }
    }

    // Calculate the offset
    size_t offset = calculate_array_offset(
        var->is_array, &var->array_dimensions, indices, num_indices);

    /* Round-23 review, finding #1 -- pointer_level DOMINATES element
       stride the same way it dominates return-value boxing
       (handle_function_call(), round 22) and native-argument marshalling
       (ast_expr_to_stdrot_value(), stdrot.c): get_type_size_for_
       descriptor() (this file) already allocates each element of a
       pointer-typed array (`rizz *ptrs[N]`) as sizeof(uintptr_t) via its
       own unconditional `pointer_level > 0` check, regardless of the
       array's base VarType -- but this function's switch below dispatched
       purely on var->var_type, computing each element's address using
       THAT type's width (sizeof(int), sizeof(char), ...) instead of
       sizeof(uintptr_t). For element 0 those strides coincidentally
       agree with the correctly-allocated layout (both start at byte 0),
       masking the bug completely -- but for element 1 onward, a `rizz
       *ptrs[2]` array (each slot really 8 bytes on LP64) got indexed as
       if each slot were 4 bytes (sizeof(int)), so `ptrs[1]` pointed
       halfway into slot 0's own 8 bytes: an out-of-bounds/aliased
       address, not the real second pointer -- real memory corruption on
       both read and write, not merely a wrong value. Checked here,
       before the base-type switch, for the identical reason every other
       "pointer-ness dominates representation" fix in this PR checks it
       first. VAR_VOID (`skibidi *ptrs[N]`) previously had no case at all
       in the switch below and hit "Unknown variable type" unconditionally
       -- also fixed by this same check, since it's a pointer-typed array
       exactly like any other now. */
    if (var->pointer_level > 0)
    {
        return (uintptr_t *)var->value.array_data + offset;
    }

    // Return a pointer to the element
    switch (var->var_type)
    {
    case VAR_INT:
        return (int *)var->value.array_data + offset;
    case VAR_SHORT:
        return (short *)var->value.array_data + offset;
    case VAR_FLOAT:
        return (float *)var->value.array_data + offset;
    case VAR_DOUBLE:
        return (double *)var->value.array_data + offset;
    case VAR_BOOL:
        return (bool *)var->value.array_data + offset;
    case VAR_CHAR:
        return (char *)var->value.array_data + offset;
    default:
        yyerror("Unknown variable type");
        exit(EXIT_FAILURE);
    }
}

bool set_int_variable(const String name, int value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_INT, mods);
}

bool set_char_variable(const String name, int value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_CHAR, mods);
}

bool set_array_variable(String name, int length, TypeModifiers mods,
                        VarType type)
{
    // search for an existing variable
    Variable *var = get_variable(name);
    if (var != NULL)
    {
        if (var->is_array)
        {
            // free the old array
            SAFE_FREE(var->value.array_data);
        }
        var->var_type = type;
        var->is_array = true;
        var->array_length = length;
        var->modifiers = mods;
        switch (type)
        {
        case VAR_INT:
            var->value.array_data = SAFE_MALLOC_ARRAY(int, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(int));
            break;
        case VAR_SHORT:
            var->value.array_data = SAFE_MALLOC_ARRAY(short, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(short));
            break;
        case VAR_FLOAT:
            var->value.array_data = SAFE_MALLOC_ARRAY(float, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(float));
            break;
        case VAR_DOUBLE:
            var->value.array_data = SAFE_MALLOC_ARRAY(double, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(double));
            break;
        case VAR_BOOL:
            var->value.array_data = SAFE_MALLOC_ARRAY(bool, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(bool));
            break;
        case VAR_CHAR:
            var->value.array_data = SAFE_MALLOC_ARRAY(char, length);
            if (length)
                memset(var->value.array_data, 0, length * sizeof(char));
            break;
        default:
            break;
        }
        return true;
    }

    return false; // no space
}

bool set_short_variable(const String name, short value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_SHORT, mods);
}

bool set_string_variable(const String name, String value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_STRING, mods);
}

bool set_float_variable(const String name, float value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_FLOAT, mods);
}

bool set_double_variable(const String name, double value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_DOUBLE, mods);
}

bool set_bool_variable(const String name, bool value, TypeModifiers mods)
{
    return set_variable(name, &value, VAR_BOOL, mods);
}

void reset_modifiers(void)
{
    current_modifiers.is_volatile = false;
    current_modifiers.is_signed = false;
    current_modifiers.is_unsigned = false;
    current_modifiers.is_sizeof = false;
    current_modifiers.is_const = false;
    current_modifiers.is_long = false;
    current_modifiers.is_long_long = false;
    current_modifiers.is_static = false;
}

TypeModifiers get_current_modifiers(void)
{
    TypeModifiers mods = current_modifiers;
    reset_modifiers(); // Reset for next declaration
    return mods;
}

/* Function implementations */

bool check_and_mark_identifier(ASTNode *node, const String contextErrorMessage)
{
    if (!node->already_checked)
    {
        node->already_checked = true;
        node->is_valid_symbol = false;

        // Do the table lookup
        Variable *var = get_variable(node->data.name);
        /* A variable, or -- falling back to the enum-constant namespace
           (e.g. bare `RED` from `gyatt Color { RED, ... };`), matching C's
           unscoped enum constants. */
        if (var != NULL || find_global_enum_constant(node->data.name) != NULL)
            node->is_valid_symbol = true;

        if (!node->is_valid_symbol)
        {
            yylineno = yylineno - 2;
            yyerror(contextErrorMessage.data);
        }
    }

    return node->is_valid_symbol;
}

void execute_switch_statement(ASTNode *node)
{
    int switch_value = evaluate_expression(node->data.switch_stmt.expression);
    CaseNode *current_case = node->data.switch_stmt.cases;
    int matched = 0;

    PUSH_JUMP_BUFFER();
    if (setjmp(CURRENT_JUMP_BUFFER()) == 0)
    {
        while (current_case)
        {
            if (current_case->value)
            {
                int case_value = evaluate_expression(current_case->value);
                if (case_value == switch_value || matched)
                {
                    matched = 1;
                    execute_statements(current_case->statements);
                }
            }
            else
            {
                // Default case
                // Known bug tracked in #179: a `based` default placed
                // before a matching numbered case fires unconditionally
                // instead of only as a true fallthrough/default. Fixing
                // this is a switch/default semantics change that needs
                // its own test_cases fixture; not addressed here.
                // cppcheck-suppress incorrectLogicOperator
                if (matched || !matched)
                {
                    execute_statements(current_case->statements);
                    break;
                }
            }
            current_case = current_case->next;
        }
    }
    else
    {
        // Break encountered; do nothing
    }
    POP_JUMP_BUFFER();
}

static ASTNode *create_node(NodeType type, VarType var_type,
                            TypeModifiers modifiers)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Error: Memory allocation failed for ASTNode.\n");
        exit(EXIT_FAILURE);
    }
    node->type = type;
    node->var_type = var_type;
    node->modifiers = modifiers;
    node->already_checked = false;
    node->is_valid_symbol = false;
    node->pointer_level = 0;
    node->line_number = yylineno;
    node->contextual_type_hint = NONE;
    return node;
}

/* Helper function to allocate and zero-initialize an ASTNode */
ASTNode *arena_alloc_astnode(void)
{
    ASTNode *node = ARENA_ALLOC(ASTNode);
    if (node)
    {
        memset(node, 0, sizeof(ASTNode));
    }
    return node;
}

ASTNode *create_int_node(int value)
{
    ASTNode *node = create_node(NODE_INT, VAR_INT, current_modifiers);
    SET_DATA_INT(node, value);
    return node;
}

ASTNode *create_array_declaration_node(String name, int length,
                                       VarType var_type)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
        return NULL;

    node->type = NODE_ARRAY_ACCESS;
    node->var_type = var_type;
    node->is_array = true;
    node->array_length = length;
    node->data.array.name = ARENA_STRDUP(name);
    return node;
}

ASTNode *create_array_access_node(String name, ASTNode *index)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    node->type = NODE_ARRAY_ACCESS;
    node->data.array.name = ARENA_STRDUP(name);
    node->data.array.index = index;
    node->data.array.indices[0] =
        index; // Also set the multi-dimensional access
    node->data.array.num_dimensions = 1; // Set dimension count
    node->is_array = true;

    // Look up and set the array's type from the symbol table
    Variable *var = get_variable(name);
    if (var != NULL)
    {
        node->var_type = var->var_type;
        node->pointer_level = var->pointer_level;
        node->array_length = var->array_length;
        node->modifiers = var->modifiers;
    }

    return node;
}

ASTNode *create_short_node(short value)
{
    ASTNode *node = create_node(NODE_SHORT, VAR_SHORT, current_modifiers);
    SET_DATA_SHORT(node, value);
    return node;
}

ASTNode *create_float_node(float value)
{
    ASTNode *node = create_node(NODE_FLOAT, VAR_FLOAT, current_modifiers);
    SET_DATA_FLOAT(node, value);
    return node;
}

ASTNode *create_char_node(char value)
{
    ASTNode *node = create_node(NODE_CHAR, VAR_CHAR, current_modifiers);
    SET_DATA_INT(node, (unsigned char)value); // Store char as integer
    return node;
}

ASTNode *create_boolean_node(bool value)
{
    ASTNode *node = create_node(NODE_BOOLEAN, VAR_BOOL, current_modifiers);
    SET_DATA_BOOL(node, value);
    return node;
}

ASTNode *create_identifier_node(String name)
{
    return create_identifier_node_ex(name, 0);
}

ASTNode *create_identifier_node_ex(String name, int pointer_level)
{
    ASTNode *node =
        create_node(NODE_IDENTIFIER, current_var_type, current_modifiers);
    node->pointer_level = pointer_level;
    SET_DATA_NAME(node, name);
    return node;
}

ASTNode *create_assignment_node(String name, ASTNode *expr)
{
    return create_assignment_target_node(create_identifier_node(name), expr);
}

ASTNode *create_assignment_target_node(ASTNode *target, ASTNode *expr)
{
    ASTNode *node = create_node(NODE_ASSIGNMENT,
                                target ? target->var_type : current_var_type,
                                get_current_modifiers());
    node->pointer_level = target ? target->pointer_level : 0;
    SET_DATA_OP(node, target, expr, OP_ASSIGN);
    return node;
}

ASTNode *create_declaration_node(String name, ASTNode *expr)
{
    return create_declaration_node_ex(name, expr, 0);
}

ASTNode *create_declaration_node_ex(String name, ASTNode *expr,
                                    int pointer_level)
{
    ASTNode *node = create_node(NODE_DECLARATION, current_var_type,
                                get_current_modifiers());
    node->pointer_level = pointer_level;
    SET_DATA_OP(node, create_identifier_node_ex(name, pointer_level), expr,
                OP_ASSIGN);
    return node;
}

ASTNode *create_operation_node(OperatorType op, ASTNode *left, ASTNode *right)
{
    ASTNode *node = create_node(NODE_OPERATION, NONE, current_modifiers);
    SET_DATA_OP(node, left, right, op);
    return node;
}

ASTNode *create_unary_operation_node(OperatorType op, ASTNode *operand)
{
    ASTNode *node = create_node(NODE_UNARY_OPERATION, NONE, current_modifiers);
    SET_DATA_UNARY_OP(node, operand, op);
    return node;
}

ASTNode *create_for_statement_node(ASTNode *init, ASTNode *cond, ASTNode *incr,
                                   ASTNode *body)
{
    ASTNode *node = create_node(NODE_FOR_STATEMENT, NONE, current_modifiers);
    SET_DATA_FOR(node, init, cond, incr, body);
    return node;
}

ASTNode *create_while_statement_node(ASTNode *cond, ASTNode *body)
{
    ASTNode *node = create_node(NODE_WHILE_STATEMENT, NONE, current_modifiers);
    SET_DATA_WHILE(node, cond, body);
    return node;
}

ASTNode *create_do_while_statement_node(ASTNode *cond, ASTNode *body)
{
    ASTNode *node =
        create_node(NODE_DO_WHILE_STATEMENT, NONE, current_modifiers);
    SET_DATA_WHILE(node, cond, body);
    return node;
}

ASTNode *create_function_call_node(String func_name, ArgumentList *args)
{
    ASTNode *node = create_node(NODE_FUNC_CALL, NONE, current_modifiers);
    SET_DATA_FUNC_CALL(node, func_name, args);
    return node;
}

ASTNode *create_double_node(double value)
{
    ASTNode *node = create_node(NODE_DOUBLE, VAR_DOUBLE, current_modifiers);
    SET_DATA_DOUBLE(node, value);
    return node;
}

ASTNode *create_sizeof_node(ASTNode *expr)
{
    ASTNode *node = create_node(NODE_SIZEOF, NONE, current_modifiers);
    SET_SIZEOF(node, expr);
    return node;
}

// @param promotion: 0 for no promotion, 1 for promotion to double 2 for
// promotion to float
void *handle_identifier(ASTNode *node, const String contextErrorMessage,
                        int promote)
{
    if (!check_and_mark_identifier(node, contextErrorMessage))
        ragequit(1);

    String name = node->data.name;
    Variable *var = get_variable(name);
    if (var != NULL)
    {
        static Value promoted_value;
        if (var->pointer_level > 0)
        {
            if (promote != 0)
            {
                yyerror("Cannot use pointer in floating-point context");
                return NULL;
            }
            return &var->value.pvalue;
        }
        if (promote == 1)
        {

            switch (var->var_type)
            {
            case VAR_DOUBLE:
                return &var->value.dvalue;
            case VAR_FLOAT:
                promoted_value.dvalue = (double)var->value.fvalue;
                return &promoted_value;
            case VAR_INT:
            case VAR_ENUM:
                promoted_value.dvalue = (double)var->value.ivalue;
                return &promoted_value;
            case VAR_CHAR:
            case VAR_SHORT:
                promoted_value.dvalue = (double)var->value.svalue;
                return &promoted_value;
            case VAR_BOOL:
                promoted_value.dvalue = (double)var->value.ivalue;
                return &promoted_value;
            case VAR_STRING:
                return &var->value.strvalue;
            default:
                yyerror("Unsupported variable type");
                return NULL;
            }
        }
        else if (promote == 2)
        {
            switch (var->var_type)
            {
            case VAR_DOUBLE:
                promoted_value.fvalue = (float)var->value.dvalue;
                return &promoted_value.fvalue;
            case VAR_FLOAT:
                return &var->value.fvalue;
            case VAR_INT:
            case VAR_ENUM:
                promoted_value.fvalue = (float)var->value.ivalue;
                return &promoted_value.fvalue;
            case VAR_CHAR:
            case VAR_SHORT:
                promoted_value.fvalue = (float)var->value.svalue;
                return &promoted_value.fvalue;
            case VAR_BOOL:
                promoted_value.fvalue = (float)var->value.ivalue;
                return &promoted_value.fvalue;
            case VAR_STRING:
                return &var->value.strvalue;
            default:
                yyerror("Unsupported variable type");
                return NULL;
            }
        }
        else
        {
            switch (var->var_type)
            {
            case VAR_DOUBLE:
                return &var->value.dvalue;
            case VAR_FLOAT:
                return &var->value.fvalue;
            case VAR_INT:
            case VAR_ENUM:
                return &var->value.ivalue;
            case VAR_CHAR:
            case VAR_SHORT:
                return &var->value.svalue;
            case VAR_BOOL:
                return &var->value.ivalue;
            case VAR_STRING:
                return &var->value.strvalue;
            default:
                yyerror("Unsupported variable type");
                return NULL;
            }
        }
    }
    /* Not a variable -- fall back to the enum-constant namespace (bare
       `RED`-style usage). Enumerators have type int in C, so every promote
       mode here mirrors its VAR_INT arm above. */
    EnumConstant *econst = find_global_enum_constant(name);
    if (econst != NULL)
    {
        static Value promoted_value;
        if (promote == 1)
        {
            promoted_value.dvalue = (double)econst->value;
            return &promoted_value;
        }
        if (promote == 2)
        {
            promoted_value.fvalue = (float)econst->value;
            return &promoted_value.fvalue;
        }
        promoted_value.ivalue = econst->value;
        return &promoted_value.ivalue;
    }
    yyerror("Undefined variable");
    return NULL;
}

/* What a NODE_ARRAY_ACCESS node indexes into, regardless of whether
   it's the classic IDENTIFIER form (Array.name, a Variable) or the
   struct-field form (Array.base, `foo.arr[i]`, a StructField). Single
   source of truth for "is this a valid array access, and what does it
   mean" -- every consumer below (get_expression_type,
   get_expression_pointer_level, is_expression,
   infer_runtime_expression_type_noeval) must agree on this, or one can
   call an expression well-typed while another calls it undefined for
   the identical node (confirmed: for `f.n[0]` on a scalar `rizz n`,
   is_expression(node, VAR_INT) used to report true while
   get_expression_type(node) reported NONE/"Undefined array field" --
   two public type queries disagreeing about the same expression).
   Returns false -- leaving *out untouched -- for a node that isn't a
   NODE_ARRAY_ACCESS, a name/field that doesn't resolve, or one that
   resolves but isn't actually an array (indexing a scalar): all three
   are "not a valid array access" and every caller here must treat them
   identically. */
typedef struct
{
    VarType type;
    int pointer_level;
    TypeModifiers modifiers;
    const ArrayDimensions *dimensions;
} ArrayAccessElement;

static bool resolve_array_access_element(ASTNode *node, ArrayAccessElement *out)
{
    if (!node || node->type != NODE_ARRAY_ACCESS)
        return false;

    if (node->data.array.base)
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (!resolve_struct_access(node->data.array.base, &def, &base, &fld,
                                   false) ||
            !fld->is_array)
            return false;
        out->type = fld->type;
        out->pointer_level = fld->pointer_level;
        out->modifiers = fld->modifiers;
        out->dimensions = &fld->array_dimensions;
        return true;
    }

    Variable *var = get_variable(node->data.array.name);
    if (!var || !var->is_array)
        return false;
    out->type = var->var_type;
    out->pointer_level = var->pointer_level;
    out->modifiers = var->modifiers;
    out->dimensions = &var->array_dimensions;
    return true;
}

int get_expression_pointer_level(ASTNode *node)
{
    if (!node)
    {
        return 0;
    }

    switch (node->type)
    {
    case NODE_IDENTIFIER:
    {
        Variable *var = get_variable(node->data.name);
        return var ? var->pointer_level : node->pointer_level;
    }
    case NODE_ARRAY_ACCESS:
    {
        ArrayAccessElement elem;
        if (resolve_array_access_element(node, &elem))
            return elem.pointer_level;
        return node->pointer_level;
    }
    case NODE_UNARY_OPERATION:
        if (node->data.unary.op == OP_ADDRESS_OF)
            return get_expression_pointer_level(node->data.unary.operand) + 1;
        if (node->data.unary.op == OP_DEREFERENCE)
        {
            int operand_level =
                get_expression_pointer_level(node->data.unary.operand);
            return operand_level > 0 ? operand_level - 1 : 0;
        }
        return get_expression_pointer_level(node->data.unary.operand);
    case NODE_FUNC_CALL:
        if (is_builtin_function(node->data.func_call.function_name))
        {
            /* A native's declared signature (return_type.type/pointer_level)
               is static data on the registered StdrotEntry -- answering
               this from it never invokes the call, so it never touches
               the native-call memo cache, same as before this consulted
               STDROT_PTR at all. Only STDROT_PTR has a pointer_level to
               report (see marshal_native_return_value()'s comment on why
               that reuses VAR_INT + pointer_level); everything else,
               including the unmarshalled STDROT_HANDLE, is level 0. */
            const StdrotEntry *entry =
                get_native_function(node->data.func_call.function_name);
            if (entry && entry->return_type.type == STDROT_PTR)
                return entry->return_type.pointer_level + 1;
            return 0;
        }
        return get_function_return_pointer_level(
            node->data.func_call.function_name);
    case NODE_OPERATION:
        switch (node->data.op.op)
        {
        case OP_EQ:
        case OP_NE:
        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE:
        case OP_AND:
        case OP_OR:
            return 0;
        case OP_PLUS:
        case OP_MINUS:
        {
            int left_level = get_expression_pointer_level(node->data.op.left);
            int right_level = get_expression_pointer_level(node->data.op.right);
            if (left_level > 0 && right_level == 0)
                return left_level;
            if (right_level > 0 && left_level == 0 &&
                node->data.op.op == OP_PLUS)
                return right_level;
            return 0;
        }
        default:
            return 0;
        }
    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (!resolve_struct_access(node, &def, &base, &fld, false))
            return 0;
        return fld->pointer_level;
    }
    default:
        return node->pointer_level;
    }
}

/* Forward declaration: get_expression_type()'s own NODE_FUNC_CALL case
   calls this (defined below it, since it in turn calls infer_runtime_
   expression_abi_type_noeval() for the identity-arg case -- see its own
   comment). */
static VarType get_native_call_static_type(ASTNode *node);

/* Forward declarations: infer_runtime_expression_type_noeval() and
   infer_runtime_expression_abi_type_noeval() (defined below get_
   expression_type(), since the ABI one builds on the plain one, and the
   plain one's own NODE_FUNC_CALL case calls get_native_call_static_type()
   just above -- a three-way mutual-recursion knot, broken here the same
   way get_native_call_static_type() itself already was. */
static VarType infer_runtime_expression_type_noeval(ASTNode *expr);
static VarType infer_runtime_expression_abi_type_noeval(ASTNode *expr);

VarType get_expression_type(ASTNode *node)
{
    if (!node)
    {
        yyerror("Null node in get_expression_type");
        return NONE; // Return an unknown type if the node is null
    }

    switch (node->type)
    {
    case NODE_INT:
        return VAR_INT;
    case NODE_SHORT:
        return VAR_SHORT;
    case NODE_FLOAT:
        return VAR_FLOAT;
    case NODE_DOUBLE:
        return VAR_DOUBLE;
    case NODE_BOOLEAN:
        return VAR_BOOL;
    case NODE_CHAR:
        return VAR_INT;
    case NODE_ARRAY_ACCESS:
    {
        ArrayAccessElement elem;
        if (resolve_array_access_element(node, &elem))
            return elem.type;
        /* Deliberately not "Undefined array" -- resolve_array_access_
           element() returns false both when the name/field genuinely
           doesn't resolve AND when it resolves to something that just
           isn't an array (e.g. indexing a scalar struct field);
           claiming "undefined" for the latter is a real thing that
           exists, just not indexable, and evaluate_struct_field_array_
           access()/evaluate_multi_array_access() (ast.c) report the
           precise reason at the point they actually have it. */
        yyerror("Invalid array access in expression");
        return NONE;
    }
    case NODE_IDENTIFIER:
    {
        // Look up the variable type in the symbol table
        const String array_name = node->data.name;
        Variable *var = get_variable(array_name);
        if (var != NULL)
        {
            return var->var_type;
        }
        /* Not a variable -- an enum constant has type int in C. */
        if (find_global_enum_constant(array_name) != NULL)
        {
            return VAR_INT;
        }
        yyerror("Undefined variable in get_expression_type");
        return NONE;
    }
    case NODE_OPERATION:
    {
        OperatorType op = node->data.op.op;
        if (op == OP_EQ || op == OP_NE || op == OP_LT || op == OP_GT ||
            op == OP_LE || op == OP_GE || op == OP_AND || op == OP_OR)
            return VAR_BOOL;

        VarType left_type = get_expression_type(node->data.op.left);
        VarType right_type = get_expression_type(node->data.op.right);
        int left_level = get_expression_pointer_level(node->data.op.left);
        int right_level = get_expression_pointer_level(node->data.op.right);

        if (left_level > 0 && right_level == 0)
            return left_type;
        if (right_level > 0 && left_level == 0 && op == OP_PLUS)
            return right_type;
        if (left_level > 0 || right_level > 0)
            return left_type;

        if (left_type == VAR_DOUBLE || right_type == VAR_DOUBLE)
            return VAR_DOUBLE;
        if (left_type == VAR_FLOAT || right_type == VAR_FLOAT)
            return VAR_FLOAT;
        return VAR_INT;
    }
    case NODE_UNARY_OPERATION:
    {
        if (node->data.unary.op == OP_ADDRESS_OF ||
            node->data.unary.op == OP_DEREFERENCE)
        {
            return get_expression_type(node->data.unary.operand);
        }
        return get_expression_type(node->data.unary.operand);
    }
    case NODE_SIZEOF:
    {
        return VAR_INT; // Sizeof always returns an integer
    }
    case NODE_FUNC_CALL:
    {
        // Look up the function in the symbol table
        const String func_name = node->data.func_call.function_name;
        if (is_builtin_function(func_name))
        {
            /* get_native_call_static_type() (below) answers this from
               the registered StdrotEntry alone, no execution, for every
               TYPED native (a fixed return_type, or an identity-
               polymorphic return_like_arg) -- Phase 2 (issue #205) made
               that statically knowable, so this no longer has to run
               arbitrary native C code merely to learn what type a call
               produces. Only a genuine legacy/untyped STDROT_EXPORT()
               export (return_type.type == STDROT_ANY, no return_like_arg)
               falls back to native_call_peek()'s execute-to-discover
               behavior -- unavoidable there since nothing describes its
               real return type ahead of time, and safe here specifically
               because THIS caller (unlike handle_sizeof(), which uses
               get_native_call_static_type() directly and never falls
               back) always goes on to actually evaluate the call for its
               value regardless of what this type-probe reports. */
            VarType static_type = get_native_call_static_type(node);
            if (static_type != NONE)
                return static_type;

            NativeResult result = native_call_peek(node);
            return stdrot_type_to_vartype(result.value.type);
        }
        Function *func = get_function(func_name);
        if (func != NULL)
        {
            return func->return_type;
        }
        yyerror("Undefined function in get_expression_type");
        return NONE;
    }
    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (!resolve_struct_access(node, &def, &base, &fld, false))
            return NONE;
        return fld->type;
    }
    default:
        yyerror("Unknown node type in get_expression_type");
        return NONE;
    }
}

static StructDef *get_struct_def_for_expression(ASTNode *expr)
{
    if (!expr)
        return NULL;

    switch (expr->type)
    {
    case NODE_IDENTIFIER:
    {
        Variable *var = get_variable(expr->data.name);
        if (var && var->var_type == VAR_STRUCT && var->struct_name.data)
            return get_struct_def(var->struct_name);
        return NULL;
    }
    case NODE_ARRAY_ACCESS:
    {
        /* NOT resolve_array_access_element(): that helper's
           ArrayAccessElement has no struct_name field (struct-typed
           array elements aren't supported at all for the struct-field/
           Array.base form -- build_struct_fields_from_params(), lang.y,
           rejects that combination for struct fields outright), but a
           plain array VARIABLE of struct-pointer-typed elements is a
           real, working, tested feature (e.g. `lit gang Point *P; P
           values[2]; ... *values[1] ...`, lit_struct_pointer_alias_
           array.brainrot) that needs var->struct_name specifically.
           Array.base (struct-field form) has no such feature yet, so it
           falls through to NULL below, same as before this field
           existed. */
        if (expr->data.array.base)
            return NULL;
        Variable *var = get_variable(expr->data.array.name);
        if (var && var->is_array && var->var_type == VAR_STRUCT &&
            var->struct_name.data)
            return get_struct_def(var->struct_name);
        return NULL;
    }
    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (resolve_struct_access(expr, &def, &base, &fld, false) &&
            fld->type == VAR_STRUCT && fld->struct_name.data)
            return get_struct_def(fld->struct_name);
        return NULL;
    }
    case NODE_UNARY_OPERATION:
        if (expr->data.unary.op == OP_DEREFERENCE)
        {
            ASTNode *operand = expr->data.unary.operand;
            if (get_expression_pointer_level(operand) <= 0)
                return NULL;
            return get_struct_def_for_expression(operand);
        }
        return NULL;
    default:
        return NULL;
    }
}

/* A genuinely recursive, execution-free expression type query -- the
 * runtime-side counterpart of semantic_analyzer.c's own infer_expression_
 * type(), reading runtime Variable/Function/enum-constant state
 * (get_variable(), get_function(), find_global_enum_constant()) instead
 * of the analyzer's static SymbolEntry table, but sharing that function's
 * defining property: NO code path here ever reaches execute_native_call().
 * A NODE_FUNC_CALL is answered purely from get_native_call_static_type()
 * (never native_call_peek(), unlike get_expression_type()'s own NODE_
 * FUNC_CALL case just above) -- and critically, that "never falls back to
 * execution" property survives *nesting*: NODE_OPERATION/NODE_UNARY_
 * OPERATION recurse into this same function on their operands, and if
 * either operand's type is genuinely unknowable (NONE -- a legacy/
 * untyped STDROT_ANY export anywhere underneath, however deeply nested),
 * the whole containing expression's type is NONE too, propagated upward
 * rather than silently resolved by running something. Previously,
 * handle_sizeof() (below) only checked whether its OPERAND was directly a
 * bare native call -- `maxxing(legacy_int())` was caught, but
 * `maxxing(legacy_int() + 1)` fell through to plain get_expression_type(),
 * which recursed into its NODE_OPERATION case, hit the same fallback, and
 * executed legacy_int() anyway. handle_sizeof() now gates on this
 * function over the *whole* operand first, rejecting instead of ever
 * reaching that fallback -- see its own comment.
 *
 * Also the base infer_runtime_expression_abi_type_noeval() (below) builds
 * its ABI-lowering on top of, for get_native_call_static_type()'s
 * identity-argument case -- see that function's own comment for why an
 * identity argument specifically needs the ABI-lowered type, not this
 * function's plain source-level one. */
static VarType infer_runtime_expression_type_noeval(ASTNode *expr)
{
    if (!expr)
        return NONE;

    switch (expr->type)
    {
    case NODE_INT:
        return VAR_INT;
    case NODE_SHORT:
        return VAR_SHORT;
    case NODE_FLOAT:
        return VAR_FLOAT;
    case NODE_DOUBLE:
        return VAR_DOUBLE;
    case NODE_BOOLEAN:
        return VAR_BOOL;
    case NODE_CHAR:
        return VAR_CHAR;
    case NODE_STRING:
    case NODE_STRING_LITERAL:
        return VAR_STRING;
    case NODE_SIZEOF:
        /* sizeof's own static type is always an integer byte count --
           this reports that without evaluating the inner sizeof at all
           (handle_sizeof() is never called here), matching get_
           expression_type()'s identical NODE_SIZEOF case. */
        return VAR_INT;
    case NODE_ARRAY_ACCESS:
    {
        ArrayAccessElement elem;
        return resolve_array_access_element(expr, &elem) ? elem.type : NONE;
    }
    case NODE_IDENTIFIER:
    {
        Variable *var = get_variable(expr->data.name);
        if (var != NULL)
            return var->var_type;
        if (find_global_enum_constant(expr->data.name) != NULL)
            return VAR_INT;
        return NONE;
    }
    case NODE_OPERATION:
    {
        OperatorType op = expr->data.op.op;
        if (op == OP_EQ || op == OP_NE || op == OP_LT || op == OP_GT ||
            op == OP_LE || op == OP_GE || op == OP_AND || op == OP_OR)
            return VAR_BOOL;

        VarType left_type =
            infer_runtime_expression_type_noeval(expr->data.op.left);
        VarType right_type =
            infer_runtime_expression_type_noeval(expr->data.op.right);
        if (left_type == NONE || right_type == NONE)
            return NONE;

        int left_level = get_expression_pointer_level(expr->data.op.left);
        int right_level = get_expression_pointer_level(expr->data.op.right);

        if (left_level > 0 && right_level == 0)
            return left_type;
        if (right_level > 0 && left_level == 0 && op == OP_PLUS)
            return right_type;
        if (left_level > 0 || right_level > 0)
            return left_type;

        if (left_type == VAR_DOUBLE || right_type == VAR_DOUBLE)
            return VAR_DOUBLE;
        if (left_type == VAR_FLOAT || right_type == VAR_FLOAT)
            return VAR_FLOAT;
        if (left_type == VAR_INT || right_type == VAR_INT)
            return VAR_INT;
        return left_type;
    }
    case NODE_UNARY_OPERATION:
        return infer_runtime_expression_type_noeval(expr->data.unary.operand);
    case NODE_FUNC_CALL:
    {
        const String func_name = expr->data.func_call.function_name;
        if (is_builtin_function(func_name))
            return get_native_call_static_type(expr);

        Function *func = get_function(func_name);
        if (func != NULL)
            return func->return_type;
        return NONE;
    }
    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (!resolve_struct_access(expr, &def, &base, &fld, false))
            return NONE;
        return fld->type;
    }
    default:
        return NONE;
    }
}

/* Runtime/execution-free counterpart of semantic_analyzer.c's own
 * infer_expression_abi_type() -- see that function's comment for the two
 * ABI-lowering rules this mirrors exactly (stdrot_char_narrows_to_int()
 * for binary/unary-negation narrowing, and a char *array* identifier
 * marshaling as STRING), so this file and the semantic analyzer's static
 * pass cannot independently drift out of agreement about what ABI type
 * an expression actually marshals as -- the two now share the *rule*
 * (stdrot_char_narrows_to_int(), stdrot.h) even though each has to read
 * it off a different data source (SymbolEntry vs. Variable) at a
 * different phase (static analysis vs. execution). Built strictly on
 * infer_runtime_expression_type_noeval() just above, so it inherits that
 * function's "never executes anything" property outright.
 *
 * Deliberately distinct from get_expression_type()'s existing NODE_CHAR
 * handling (VAR_INT, unconditionally, matching how a bare char literal
 * behaves in ordinary Brainrot arithmetic/sizeof contexts): a native's
 * identity-polymorphic argument (slorp<T>(T) -> T) marshals as exactly
 * whatever ast_expr_to_stdrot_value() would produce for that argument,
 * and a bare char literal marshals as STDROT_CHAR there, not STDROT_INT
 * -- only narrowed for the specific node shapes stdrot_char_narrows_to_
 * int() says narrow. Consuming get_expression_type() here (as get_
 * native_call_static_type()'s identity case previously did) reported the
 * wrong type for exactly this case, along with the same char-array-as-
 * STRING and enum-as-INT mismatches infer_expression_abi_type() already
 * fixed on the static-analysis side. */
static VarType infer_runtime_expression_abi_type_noeval(ASTNode *expr)
{
    VarType t = infer_runtime_expression_type_noeval(expr);
    if (!expr || t == NONE)
        return t;

    /* No StdrotType exists for an enum -- an enum-typed expression always
       marshals as STDROT_INT (ast_expr_to_stdrot_value()'s NODE_
       IDENTIFIER/VAR_ENUM case, stdrot.c), the same representation a
       bare int gets. Matches infer_expression_abi_type()'s own
       unconditional VAR_ENUM normalization. */
    if (t == VAR_ENUM)
        return VAR_INT;

    if (t != VAR_CHAR)
        return t;

    if (stdrot_char_narrows_to_int(
            expr->type,
            expr->type == NODE_UNARY_OPERATION ? expr->data.unary.op : OP_PLUS))
        return VAR_INT;

    if (expr->type != NODE_IDENTIFIER)
        return t;

    Variable *var = get_variable(expr->data.name);
    if (var != NULL && var->is_array)
        return VAR_STRING;
    return t;
}

/* Purely static return type for a call to a builtin (stdrot) native,
 * using only its registered StdrotEntry -- NEVER executing the native.
 * The runtime-side counterpart of semantic_analyzer.c's own static
 * native-call type inference (infer_expression_type()'s NODE_FUNC_CALL
 * case): Phase 2 (issue #205) gave every native a real, registered
 * signature, so the type a TYPED native's call produces is knowable
 * ahead of time from that descriptor alone, the same way a Brainrot-
 * defined function's return type already was before this file ever
 * needed to execute anything to answer that question.
 *
 * Returns NONE when the type genuinely cannot be known without running
 * the native -- a legacy/untyped STDROT_EXPORT() export (return_type.type
 * == STDROT_ANY, return_like_arg == -1, "signature unknown" by design,
 * see StdrotEntry's own comment, stdrot_api.h). get_expression_type()'s
 * own NODE_FUNC_CALL case still falls back to native_call_peek() (which
 * actually executes) for exactly that remaining case -- safe there
 * specifically because that caller always goes on to evaluate the call
 * for its value regardless. handle_sizeof() (this file) calls this
 * function directly instead, and does NOT fall back to executing
 * anything when it returns NONE: sizeof's operand must never be
 * evaluated, full stop, and "the type is only knowable by running the
 * native" is exactly the situation where honoring that contract means
 * reporting the size as unknowable rather than quietly violating it. */
static VarType get_native_call_static_type(ASTNode *node)
{
    const String func_name = node->data.func_call.function_name;
    const StdrotEntry *entry = get_native_function(func_name);
    if (!entry)
        return NONE;

    if (entry->return_like_arg >= 0)
    {
        /* Identity-polymorphic (slorp<T>(T) -> T, STDROT_EXPORT_SIG_
           IDENTITY()): the call's real type is whatever the referenced
           argument's own type turns out to be -- return_like_arg is only
           ever validated (validate_native_registry(), stdrot.c) to name
           a STDROT_ANY parameter, so there is no fixed StdrotType to read
           off return_type here at all. Delegating to infer_runtime_
           expression_abi_type_noeval() (above), not plain get_expression_
           type(), because the identity this call promises is "same ABI
           representation as this argument," not "same source-level
           VarType" -- those differ for a char array (marshals as
           STDROT_STRING, not the element's VAR_CHAR) and a bare char
           literal (marshals as STDROT_CHAR, not get_expression_type()'s
           unconditional VAR_INT) -- see that function's own comment.
           Still execution-free for the same reason recursively: a nested
           builtin call underneath hits this same static-first path via
           infer_runtime_expression_type_noeval(), and unknowable
           anywhere underneath propagates NONE instead of falling back to
           running anything. */
        int idx = 0;
        for (ArgumentList *a = node->data.func_call.arguments; a;
             a = a->next, idx++)
        {
            if (idx == entry->return_like_arg)
                return a->expr
                           ? infer_runtime_expression_abi_type_noeval(a->expr)
                           : NONE;
        }
        return NONE;
    }

    if (entry->return_type.type == STDROT_ANY)
        return NONE;

    return stdrot_type_to_vartype(entry->return_type.type);
}

void *handle_binary_operation(ASTNode *node)
{
    if (!node || node->type != NODE_OPERATION)
    {
        yyerror("Invalid binary operation node");
        return NULL;
    }

    // Evaluate left and right operands.
    void *left_value = NULL;
    void *right_value = NULL;

    // Determine the actual types of the operands.
    int left_type = get_expression_type(node->data.op.left);
    int right_type = get_expression_type(node->data.op.right);

    // An enum operand is an int in C -- normalize here so every VAR_INT
    // check below treats it as one too.
    if (left_type == VAR_ENUM)
        left_type = VAR_INT;
    if (right_type == VAR_ENUM)
        right_type = VAR_INT;

    // Promote types if necessary (short -> int -> float -> double).
    int promoted_type = VAR_SHORT;
    if (left_type == VAR_DOUBLE || right_type == VAR_DOUBLE)
        promoted_type = VAR_DOUBLE;
    else if (left_type == VAR_FLOAT || right_type == VAR_FLOAT)
        promoted_type = VAR_FLOAT;
    else if (left_type == VAR_INT || right_type == VAR_INT)
        promoted_type = VAR_INT;

    void *result = NULL;

    // Allocate and evaluate operands based on promoted type.
    switch (promoted_type)
    {
    case VAR_INT:
        left_value = SAFE_MALLOC(int);
        right_value = SAFE_MALLOC(int);
        *(int *)left_value = evaluate_expression_int(node->data.op.left);
        *(int *)right_value = evaluate_expression_int(node->data.op.right);
        break;

    case VAR_FLOAT:
        left_value = SAFE_MALLOC(float);
        right_value = SAFE_MALLOC(float);
        *(float *)left_value =
            (left_type == VAR_INT)
                ? (float)evaluate_expression_int(node->data.op.left)
                : evaluate_expression_float(node->data.op.left);
        *(float *)right_value =
            (right_type == VAR_INT)
                ? (float)evaluate_expression_int(node->data.op.right)
                : evaluate_expression_float(node->data.op.right);
        break;

    case VAR_DOUBLE:
        left_value = SAFE_MALLOC(double);
        right_value = SAFE_MALLOC(double);
        *(double *)left_value =
            (left_type == VAR_INT)
                ? (double)evaluate_expression_int(node->data.op.left)
            : (left_type == VAR_FLOAT)
                ? (double)evaluate_expression_float(node->data.op.left)
                : evaluate_expression_double(node->data.op.left);
        *(double *)right_value =
            (right_type == VAR_INT)
                ? (double)evaluate_expression_int(node->data.op.right)
            : (right_type == VAR_FLOAT)
                ? (double)evaluate_expression_float(node->data.op.right)
                : evaluate_expression_double(node->data.op.right);
        break;
    case VAR_SHORT:
        left_value = SAFE_MALLOC(short);
        right_value = SAFE_MALLOC(short);
        *(short *)left_value = evaluate_expression_short(node->data.op.left);
        *(short *)right_value = evaluate_expression_short(node->data.op.right);
        break;

    default:
        yyerror("Unsupported type promotion");
        return NULL;
    }

    // Perform the operation and allocate the result.
    // This branch and the final "else if (!result)" below both allocate an
    // int, but for unrelated reasons -- this one because comparison
    // operators always produce an int result regardless of promoted_type,
    // the other as the int/long/etc. fallback once every other
    // promoted_type has been tried. Not adjacent, not mergeable without
    // losing that distinction.
    if (node->data.op.op == OP_LT || node->data.op.op == OP_GT ||
        node->data.op.op == OP_LE || node->data.op.op == OP_GE ||
        node->data.op.op == OP_EQ || node->data.op.op == OP_NE)
    { // NOLINT(bugprone-branch-clone)
        result = SAFE_MALLOC(int);
    }
    else if (!result && promoted_type == VAR_DOUBLE)
    {
        result = SAFE_MALLOC(double);
    }
    else if (!result && promoted_type == VAR_FLOAT)
    {
        result = SAFE_MALLOC(float);
    }
    else if (!result && promoted_type == VAR_SHORT)
    {
        result = SAFE_MALLOC(short);
    }
    else if (!result)
    {
        result = SAFE_MALLOC(int);
    }

    switch (node->data.op.op)
    {
    case OP_PLUS:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value + *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value + *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value + *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value + *(short *)right_value;
        break;

    case OP_MINUS:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value - *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value - *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value - *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value - *(short *)right_value;

        break;

    case OP_TIMES:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value * *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(float *)result = *(float *)left_value * *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(double *)result = *(double *)left_value * *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(short *)result = *(short *)left_value * *(short *)right_value;
        break;

    case OP_DIVIDE:
        if (promoted_type == VAR_INT)
        {
            if (*(int *)right_value == 0)
            {
                yyerror("Division by zero");
                *(int *)result =
                    0; // Define a fallback behavior for int division by zero
            }
            else
            {
                *(int *)result = *(int *)left_value / *(int *)right_value;
            }
        }
        else if (promoted_type == VAR_FLOAT)
        {
            *(float *)result = *(float *)left_value / *(float *)right_value;
        }
        else if (promoted_type == VAR_DOUBLE)
        {
            *(double *)result = *(double *)left_value / *(double *)right_value;
        }
        else if (promoted_type == VAR_SHORT)
        {
            if (*(short *)right_value == 0)
            {
                yyerror("Division by zero");
                *(short *)result =
                    0; // Define a fallback behavior for short division by zero
            }
            else
            {
                *(short *)result = *(short *)left_value / *(short *)right_value;
            }
        }
        break;
    case OP_MOD:
        if (promoted_type == VAR_INT)
        {
            int left = *(int *)left_value;
            int right = *(int *)right_value;

            if (right == 0)
            {
                yyerror("Modulo by zero");
                *(int *)result = 0; // Define fallback for modulo by zero
            }
            else if (node->modifiers.is_unsigned)
            {
                // Explicitly handle unsigned modulo
                unsigned int ul = (unsigned int)left;
                unsigned int ur = (unsigned int)right;
                *(int *)result = (int)(ul % ur);
            }
            else
            {
                *(int *)result = left % right;
            }
        }
        else if (promoted_type == VAR_FLOAT)
        {
            *(float *)result =
                fmodf(*(float *)left_value, *(float *)right_value);
        }
        else if (promoted_type == VAR_DOUBLE)
        {
            *(double *)result =
                fmod(*(double *)left_value, *(double *)right_value);
        }
        else if (promoted_type == VAR_SHORT)
        {
            *(short *)result = *(short *)left_value % *(short *)right_value;
        }
        break;
    case OP_LT:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value < *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(int *)result = *(float *)left_value < *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(int *)result = *(double *)left_value < *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(int *)result = *(short *)left_value < *(short *)right_value;
        break;

    case OP_GT:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value > *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(int *)result = *(float *)left_value > *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(int *)result = *(double *)left_value > *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(int *)result = *(short *)left_value > *(short *)right_value;
        break;

    case OP_LE:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value <= *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(int *)result = *(float *)left_value <= *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(int *)result = *(double *)left_value <= *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(int *)result = *(short *)left_value <= *(short *)right_value;
        break;

    case OP_GE:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value >= *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(int *)result = *(float *)left_value >= *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(int *)result = *(double *)left_value >= *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(int *)result = *(short *)left_value >= *(short *)right_value;
        break;

    case OP_EQ:

        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value == *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(int *)result = *(float *)left_value == *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(int *)result = *(double *)left_value == *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(int *)result = *(short *)left_value == *(short *)right_value;
        break;

    case OP_NE:
        if (promoted_type == VAR_INT)
            *(int *)result = *(int *)left_value != *(int *)right_value;
        else if (promoted_type == VAR_FLOAT)
            *(int *)result = *(float *)left_value != *(float *)right_value;
        else if (promoted_type == VAR_DOUBLE)
            *(int *)result = *(double *)left_value != *(double *)right_value;
        else if (promoted_type == VAR_SHORT)
            *(int *)result = *(short *)left_value != *(short *)right_value;
        break;

    default:
        yyerror("Unsupported binary operator");
        SAFE_FREE(result);
        result = NULL;
    }

    SAFE_FREE(left_value);
    SAFE_FREE(right_value);

    return result;
}

void *evaluate_lvalue_address(ASTNode *node)
{
    if (!node)
    {
        yyerror("Invalid lvalue");
        return NULL;
    }

    switch (node->type)
    {
    case NODE_IDENTIFIER:
    {
        Variable *var = get_variable(node->data.name);
        if (!var)
        {
            yyerror("Undefined variable");
            return NULL;
        }

        /* Pointer-typed variables, including `gang T *p`, store the pointer
           value in pvalue. Assignment to the pointer itself writes that slot;
           dereference assignment handles the pointee through the unary case
           below. */
        if (var->pointer_level > 0)
            return &var->value.pvalue;

        switch (var->var_type)
        {
        case VAR_INT:
            return &var->value.ivalue;
        case VAR_SHORT:
            return &var->value.svalue;
        case VAR_FLOAT:
            return &var->value.fvalue;
        case VAR_DOUBLE:
            return &var->value.dvalue;
        case VAR_BOOL:
            return &var->value.bvalue;
        case VAR_CHAR:
            return &var->value.ivalue;
        case VAR_STRING:
            return &var->value.strvalue;
        case VAR_ENUM:
            return &var->value.ivalue;
        case VAR_STRUCT:
            /* Non-pointer struct/union values live in the layout blob at
               value.array_data (allocated by interpreter_visit_declaration).
               Pointer-to-struct variables are handled above via
               pointer_level > 0 and pvalue. */
            if (!var->value.array_data)
            {
                yyerror("Uninitialized struct lvalue");
                return NULL;
            }
            return var->value.array_data;
        default:
            yyerror("Unsupported lvalue type");
            return NULL;
        }
    }
    case NODE_ARRAY_ACCESS:
        return evaluate_multi_array_access(node);
    case NODE_UNARY_OPERATION:
        if (node->data.unary.op == OP_DEREFERENCE)
            return (void *)evaluate_expression_pointer(
                node->data.unary.operand);
        break;
    case NODE_STRUCT_ACCESS:
        return evaluate_struct_member_address(node);
    default:
        break;
    }

    yyerror("Expression is not assignable");
    return NULL;
}

uintptr_t evaluate_expression_pointer(ASTNode *node)
{
    if (!node)
        return (uintptr_t)0;

    switch (node->type)
    {
    case NODE_IDENTIFIER:
    {
        Variable *var = get_variable(node->data.name);
        if (!var)
        {
            yyerror("Undefined variable");
            return (uintptr_t)0;
        }
        if (var->pointer_level <= 0)
        {
            yyerror("Expression is not a pointer");
            return (uintptr_t)0;
        }
        return var->value.pvalue;
    }
    case NODE_ARRAY_ACCESS:
        return *(uintptr_t *)evaluate_multi_array_access(node);
    case NODE_UNARY_OPERATION:
        if (node->data.unary.op == OP_ADDRESS_OF)
            return (uintptr_t)evaluate_lvalue_address(node->data.unary.operand);
        if (node->data.unary.op == OP_DEREFERENCE)
            /* A NULL pointer value reaching a dereference here is a real
             * possible Brainrot-program runtime crash (matching C's own
             * *NULL semantics), not a bug in this call site specifically --
             * every typed dereference in this dispatch (evaluate_expression_
             * int/short/float/.../bool) shares the same unguarded shape.
             * Whether pointer dereference should instead raise a catchable
             * runtime error is a language-semantics decision, out of scope
             * for this CI-adoption pass. */
            // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
            return *(uintptr_t *)(uintptr_t)evaluate_expression_pointer(
                node->data.unary.operand);
        break;
    case NODE_FUNC_CALL:
    {
        /* A pointer *destination* (the caller of this function, e.g. a
           pointer-typed declaration's runtime handler) decides to call
           this based only on ITS OWN declared pointer_level, not on
           whatever this call's source expression actually is -- so a
           call whose own return isn't declared to produce a pointer
           (get_expression_pointer_level(node) == 0) must be rejected
           here rather than blindly reinterpreting whatever
           handle_function_call() boxed as a uintptr_t*. Without this,
           a legacy STDROT_ANY export that actually returns e.g. a plain
           int (enforce_return_type() only rejects one that returns a
           real STDROT_PTR, since that's the only case a legacy export
           genuinely isn't allowed to produce) would have its boxed int
           value reinterpreted as a raw memory address -- `rizz *p =
           legacy_int();` handing `p` the address 0x2a instead of being
           rejected. */
        if (get_expression_pointer_level(node) == 0)
        {
            yyerror("Native call result is not a pointer, cannot be used "
                    "in a pointer context");
            return (uintptr_t)0;
        }
        uintptr_t *res = (uintptr_t *)handle_function_call(node);
        if (res)
        {
            uintptr_t value = *res;
            SAFE_FREE(res);
            return value;
        }
        return (uintptr_t)0;
    }
    case NODE_OPERATION:
    {
        int left_ptr = get_expression_pointer_level(node->data.op.left);
        int right_ptr = get_expression_pointer_level(node->data.op.right);
        if (node->data.op.op == OP_PLUS || node->data.op.op == OP_MINUS)
        {
            if (left_ptr > 0 && right_ptr == 0)
            {
                uintptr_t base =
                    evaluate_expression_pointer(node->data.op.left);
                ptrdiff_t offset = evaluate_expression_int(node->data.op.right);
                size_t scale = get_type_size_for_descriptor(
                    get_expression_type(node->data.op.left), left_ptr - 1,
                    node->data.op.left->modifiers);
                /* Round-23 review, finding #4 -- a scale of 0 means the
                   pointee's size is genuinely unknown (a single-level
                   opaque VAR_PTR/VAR_VOID pointer -- semantic analysis
                   already rejects this shape statically, see validate_
                   binary_operation()'s own comment), not "assume one
                   byte." Defaulting to 1 silently gave `test_ptr_source()
                   + 1` byte-pointer semantics for a type this ABI
                   explicitly documents as pointee-erased. Kept as a
                   defensive runtime check, matching every other place in
                   this codebase where a static rejection has a runtime
                   backstop too. */
                if (scale == 0)
                {
                    yyerror("Cannot perform pointer arithmetic on a "
                            "type-erased pointer -- its pointee size is "
                            "unknown");
                    return base;
                }
                return node->data.op.op == OP_PLUS
                           ? base + (uintptr_t)(offset * (ptrdiff_t)scale)
                           : base - (uintptr_t)(offset * (ptrdiff_t)scale);
            }
            if (right_ptr > 0 && left_ptr == 0 && node->data.op.op == OP_PLUS)
            {
                uintptr_t base =
                    evaluate_expression_pointer(node->data.op.right);
                ptrdiff_t offset = evaluate_expression_int(node->data.op.left);
                size_t scale = get_type_size_for_descriptor(
                    get_expression_type(node->data.op.right), right_ptr - 1,
                    node->data.op.right->modifiers);
                if (scale == 0)
                {
                    yyerror("Cannot perform pointer arithmetic on a "
                            "type-erased pointer -- its pointee size is "
                            "unknown");
                    return base;
                }
                return base + (uintptr_t)(offset * (ptrdiff_t)scale);
            }
        }
        break;
    }
    case NODE_STRUCT_ACCESS:
    {
        /* Round-21 review, finding #2's runtime half -- missing here for
           the identical reason it was missing from get_expression_
           pointer_level() and infer_expression_pointer_level()
           (semantic_analyzer.c): a pointer-typed struct/union field
           (`b.p`, `rizz *p;` inside `gang Box`) was never readable as a
           pointer VALUE at all, only as an lvalue ADDRESS (evaluate_
           lvalue_address()'s own NODE_STRUCT_ACCESS case, above, used
           for *writing* to the field). Once the static side correctly
           stopped rejecting `poke_int(b.p, 42)` before this native call
           ever reached runtime marshalling, ast_expr_to_stdrot_value()
           (stdrot.c) started actually calling this function for it --
           and falling through to "Invalid pointer expression" (returning
           a bogus NULL address) let the native write through a null
           pointer instead of erroring cleanly, let alone working.
           evaluate_struct_member_address() resolves the field's storage
           location within its enclosing struct; the uintptr_t stored
           there (not the field's own address) is the pointer VALUE this
           function's contract promises. */
        if (get_expression_pointer_level(node) <= 0)
        {
            yyerror("Expression is not a pointer");
            return (uintptr_t)0;
        }
        void *addr = evaluate_struct_member_address(node);
        return addr ? *(uintptr_t *)addr : (uintptr_t)0;
    }
    default:
        break;
    }

    yyerror("Invalid pointer expression");
    return (uintptr_t)0;
}

/* See this function's declaration (ast.h) for the full invariant. */
int char_scalar_slot_value(int raw)
{
    return (unsigned char)raw;
}

/* `packed_storage` distinguishes two genuinely different memory shapes
   `address` can point into, which only diverge for VAR_CHAR:
   - false (a plain NODE_IDENTIFIER target): address is a scalar
     Variable's own union slot (evaluate_lvalue_address's NODE_IDENTIFIER
     case), which for VAR_CHAR is `&var->value.ivalue` -- a real 4-byte
     `int` slot, not a 1-byte one (see that union's definition), because
     there is no dedicated 1-byte member for it to live in instead.
   - true (NODE_ARRAY_ACCESS, NODE_STRUCT_ACCESS, or a dereferenced
     pointer target): address points into a tightly packed array/struct
     blob (or, for a dereference, wherever the pointer says -- possibly
     a scalar Variable's own slot, possibly one of those same blobs),
     where a `yap` element/field genuinely occupies 1 byte with real
     neighbors on both sides. Every other VarType already has matching
     width between the two shapes (BOOL, SHORT, FLOAT, DOUBLE, INT all
     use a same-sized union member and a same-sized array/struct slot),
     which is why only VAR_CHAR needs this distinction at all.

   A dereferenced pointer's `address` can land in EITHER shape and
   there is no way to tell which from the pointer alone -- `yap *p`
   could be `&someCharVariable` or `&someArray[i]` or `&someStruct.
   field`. That ambiguity is why the VAR_CHAR case below does not just
   pick a width from `packed_storage` and stop: the scalar (non-packed)
   branch also narrows the value to a single byte's worth of range
   before zero-extending it back out to fill the full 4-byte slot,
   keeping that slot's upper 3 bytes permanently zero. That is what
   makes a *later*, narrower 1-byte write through a `yap *` alias into
   this same slot (the `packed_storage` branch, taken because the write
   arrived via a dereference, not because anyone determined this
   particular address is actually a scalar's slot) safe: it only ever
   touches the low byte, and the upper three were already zero and stay
   that way. Without this, a scalar `yap c; yap *p = &c;` sequence
   ending in `*p = 'x'` would leave whatever was previously in bytes
   1-3 of `c`'s slot behind, corrupting any subsequent raw (unnarrowed)
   read of `c` as a full int (confirmed: `c = 1000;` followed by
   `*p = 'x';` used to leave `c + 0` reading 888, not 120). Writing
   `*(int *)address` in the packed case overwrites 3 bytes of whatever
   follows -- padding if you're lucky, a real neighboring field/element
   if you're not (confirmed: `gang { rizz a; yap b; yap c; };
   f.b = 'x';` silently zeroed `c`). */
static void write_value_to_address(void *address, VarType type,
                                   int pointer_level, ASTNode *expr,
                                   TypeModifiers mods, bool packed_storage)
{
    if (!address)
    {
        yyerror("Invalid assignment target");
        return;
    }

    if (pointer_level > 0)
    {
        *(uintptr_t *)address = evaluate_expression_pointer(expr);
        return;
    }

    switch (type)
    {
    case VAR_INT:
        *(int *)address = evaluate_expression_int(expr);
        break;
    case VAR_SHORT:
        *(short *)address = evaluate_expression_short(expr);
        break;
    case VAR_FLOAT:
        *(float *)address = evaluate_expression_float(expr);
        break;
    case VAR_DOUBLE:
        *(double *)address = evaluate_expression_double(expr);
        break;
    case VAR_BOOL:
        *(bool *)address = evaluate_expression_bool(expr);
        break;
    case VAR_CHAR:
        if (packed_storage)
            *(char *)address = (char)evaluate_expression_int(expr);
        else
            *(int *)address =
                char_scalar_slot_value(evaluate_expression_int(expr));
        break;
    case VAR_STRING:
        *(String *)address = evaluate_expression_string(expr);
        break;
    case VAR_ENUM:
        *(int *)address = evaluate_expression_int(expr);
        break;
    case NONE:
    default:
        yyerror("Unsupported assignment type");
        break;
    }
    (void)mods;
}

static void initialize_variable_from_expr(Variable *var, ASTNode *expr)
{
    if (!var || !expr)
        return;

    if (var->pointer_level > 0)
    {
        var->value.pvalue = evaluate_expression_pointer(expr);
        return;
    }

    switch (var->var_type)
    {
    case VAR_INT:
        var->value.ivalue = evaluate_expression_int(expr);
        break;
    case VAR_SHORT:
        var->value.svalue = evaluate_expression_short(expr);
        break;
    case VAR_FLOAT:
        var->value.fvalue = evaluate_expression_float(expr);
        break;
    case VAR_DOUBLE:
        var->value.dvalue = evaluate_expression_double(expr);
        break;
    case VAR_BOOL:
        var->value.bvalue = evaluate_expression_bool(expr);
        break;
    case VAR_CHAR:
        var->value.ivalue =
            char_scalar_slot_value(evaluate_expression_int(expr));
        break;
    case VAR_STRING:
        var->value.strvalue = evaluate_expression_string(expr);
        break;
    case VAR_ENUM:
        var->value.ivalue = evaluate_expression_int(expr);
        break;
    case NONE:
    default:
        break;
    }
}

void *handle_unary_expression(ASTNode *node, void *operand_value,
                              int operand_type)
{
    switch (node->data.unary.op)
    {
    case OP_NEG:
        if (operand_type == VAR_INT)
        {
            int *result = SAFE_MALLOC(int);
            *result = -(*(int *)operand_value);
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = SAFE_MALLOC(short);
            *result = !(*(short *)operand_value);
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = SAFE_MALLOC(float);
            *result = -(*(float *)operand_value);
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = SAFE_MALLOC(double);
            *result = -(*(double *)operand_value);
            return result;
        }
        else if (operand_type == VAR_BOOL)
        {
            bool *result = SAFE_MALLOC(bool);
            *result = !(*(bool *)operand_value);
            return result;
        }
        else
        {
            yyerror("Invalid type for unary negation");
            return NULL;
        }

    case OP_PRE_INC:
        if (operand_type == VAR_INT)
        {
            int *result = SAFE_MALLOC(int);
            *result = *(int *)operand_value + 1;
            set_int_variable(
                node->data.unary.operand->data.name, *result,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = SAFE_MALLOC(short);
            *result = *(short *)operand_value + 1;
            set_short_variable(
                node->data.unary.operand->data.name, *result,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = SAFE_MALLOC(float);
            *result = *(float *)operand_value + 1;
            set_float_variable(
                node->data.unary.operand->data.name, *result,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = SAFE_MALLOC(double);
            *result = *(double *)operand_value + 1;
            set_double_variable(
                node->data.unary.operand->data.name, *result,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else
        {
            yyerror("Invalid type for pre-increment");
            return NULL;
        }
    case OP_PRE_DEC:
        if (operand_type == VAR_INT)
        {
            int *result = SAFE_MALLOC(int);
            *result = *(int *)operand_value - 1;
            set_int_variable(
                node->data.unary.operand->data.name, *result,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = SAFE_MALLOC(short);
            *result = *(short *)operand_value - 1;
            set_short_variable(
                node->data.unary.operand->data.name, *result,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = SAFE_MALLOC(float);
            *result = *(float *)operand_value - 1;
            set_float_variable(
                node->data.unary.operand->data.name, *result,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = SAFE_MALLOC(double);
            *result = *(double *)operand_value - 1;
            set_double_variable(
                node->data.unary.operand->data.name, *result,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else
        {
            yyerror("Invalid type for pre-decrement");
            return NULL;
        }
    case OP_POST_INC:
        if (operand_type == VAR_INT)
        {
            int *result = SAFE_MALLOC(int);
            *result = *(int *)operand_value;
            set_int_variable(
                node->data.unary.operand->data.name, *result + 1,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = SAFE_MALLOC(short);
            *result = *(short *)operand_value;
            set_short_variable(
                node->data.unary.operand->data.name, *result + 1,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = SAFE_MALLOC(float);
            *result = *(float *)operand_value;
            set_float_variable(
                node->data.unary.operand->data.name, *result + 1,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = SAFE_MALLOC(double);
            *result = *(double *)operand_value;
            set_double_variable(
                node->data.unary.operand->data.name, *result + 1,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else
        {
            yyerror("Invalid type for post-increment");
            return NULL;
        }
    case OP_POST_DEC:
        if (operand_type == VAR_INT)
        {
            int *result = SAFE_MALLOC(int);
            *result = *(int *)operand_value;
            set_int_variable(
                node->data.unary.operand->data.name, *result - 1,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_SHORT)
        {
            short *result = SAFE_MALLOC(short);
            *result = *(short *)operand_value;
            set_short_variable(
                node->data.unary.operand->data.name, *result - 1,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_FLOAT)
        {
            float *result = SAFE_MALLOC(float);
            *result = *(float *)operand_value;
            set_float_variable(
                node->data.unary.operand->data.name, *result - 1,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else if (operand_type == VAR_DOUBLE)
        {
            double *result = SAFE_MALLOC(double);
            *result = *(double *)operand_value;
            set_double_variable(
                node->data.unary.operand->data.name, *result - 1,
                get_variable_modifiers(node->data.unary.operand->data.name));
            return result;
        }
        else
        {
            yyerror("Invalid type for post-decrement");
            return NULL;
        }
    default:
        yyerror("Unknown unary operator");
        return NULL;
    }
}

/* Frees the void* box handle_function_call() returned, accounting for
 * VAR_STRING's nested allocation -- that box is a malloc'd String* whose
 * ->data is itself a separate safe_strdup'd buffer (see handle_function_
 * call()'s VAR_STRING case), which a single SAFE_FREE(raw) would leak.
 * Every other VarType's box is a flat scalar with nothing further to
 * free. Used by the type-mismatch error paths below, where `raw` is
 * discarded unread rather than unpacked through its own evaluator's
 * usual (type-appropriate) free sequence. */
static void free_native_result_box(void *raw, VarType actual)
{
    if (actual == VAR_STRING)
        SAFE_FREE(((String *)raw)->data);
    SAFE_FREE(raw);
}

/* current_return_value.type is the ACTUAL runtime type
 * handle_function_call() just boxed `raw` as -- for a typed native this is
 * guaranteed (by execute_native_call()'s enforce_return_type(), stdrot.c)
 * to match what the semantic analyzer approved for this call site, but
 * for a legacy STDROT_ANY export (return_type.type == STDROT_ANY,
 * return_like_arg == -1) there is no such guarantee at all: the analyzer
 * couldn't determine a static type for the call (infer_expression_type()
 * reports NONE for it, so declaration/argument type checks involving it
 * are skipped outright), so the call is approved into whatever numeric
 * context it's used in regardless of what the native's C implementation
 * actually constructs. Blindly reinterpreting `raw` as whatever this
 * evaluator's calling context expects reproduces, for every non-pointer
 * type, exactly the wrong-union-member bug the STDROT_PTR pointer guards
 * elsewhere in this file already close for pointer-valued results -- this
 * closes it the rest of the way, by reading the box back out through the
 * type it was ACTUALLY boxed as, coercing numerically only within the
 * same group check_type_compatibility_ex() (semantic_analyzer.c) already
 * grants int/short/float/double/enum, plus VAR_CHAR (not part of that
 * static group, but a native call result specifically is a case this
 * codebase already deliberately reads numerically -- see ast_expr_to_
 * stdrot_value()'s NODE_FUNC_CALL/VAR_CHAR case, which routes a char-
 * returning call like `slorp(c)` through evaluate_expression_int() on
 * purpose), and refusing outright otherwise instead of casting past the
 * allocation's real size. A no-op for a typed native or an identity-
 * polymorphic one (slorp): `actual` always already matches what's
 * expected there, since the semantic analyzer genuinely knows the
 * static type in both cases. */
static bool unbox_native_numeric_result(void *raw, VarType actual, double *out)
{
    switch (actual)
    {
    case VAR_INT:
    case VAR_ENUM:
        *out = (double)*(int *)raw;
        return true;
    case VAR_SHORT:
        *out = (double)*(short *)raw;
        return true;
    case VAR_FLOAT:
        *out = (double)*(float *)raw;
        return true;
    case VAR_DOUBLE:
        *out = *(double *)raw;
        return true;
    case VAR_CHAR:
        *out = (double)*(char *)raw;
        return true;
    default:
        return false;
    }
}

/* handle_function_call() returns NULL for two different reasons: the
 * call's actual result is genuinely void (current_return_value.type ==
 * VAR_VOID -- a legacy STDROT_ANY export whose real runtime result
 * turned out to be STDROT_NONE, or a STDROT_NONE-declared native, either
 * way used where a value is expected), or it's a struct result this
 * generic scalar context can't represent (current_return_value.type ==
 * VAR_STRUCT, already discarded inside handle_function_call() itself).
 * A native call can only ever produce the former (StdrotType has no
 * struct variant reaching this path), so this only reports the void
 * case -- silently returning a default 0-ish value for "no value at
 * all" is exactly the kind of static-type-information-implies-runtime-
 * type-safety hole this file's other native-result checks already
 * close (see unbox_native_numeric_result()'s own comment). Returns true
 * (having already reported the error) only for the void case; every
 * caller still falls through to its own default-value return either
 * way, matching what a NULL raw already meant before this check existed
 * for the struct case.
 *
 * VAR_VOID, not NONE: stdrot_type_to_vartype() (stdrot.c) maps a real,
 * already-executed native result's STDROT_NONE to VAR_VOID specifically
 * so this exact check (and every other "is this call's result usable as
 * a value" question) can tell "certainly nothing" apart from "couldn't
 * determine a type ahead of time" -- see VAR_VOID's own comment (ast.h)
 * for the full reasoning. current_return_value.type reaching here as
 * plain NONE would mean marshal_native_return_value() populated it from
 * something enforce_return_type() should already have rejected earlier
 * (STDROT_ANY/STDROT_HANDLE/STDROT_CSTRING as an actual result tag) --
 * checked defensively alongside VAR_VOID rather than assumed
 * unreachable. */
static bool warn_if_native_result_void(const char *context_name)
{
    if (current_return_value.type != VAR_VOID &&
        current_return_value.type != NONE)
        return false;
    char error_msg[MAX_BUFFER_LEN];
    snprintf(error_msg, sizeof(error_msg),
             "Native call result (void) cannot be used in a %s context",
             context_name);
    yyerror(error_msg);
    return true;
}

float evaluate_expression_float(ASTNode *node)
{
    if (!node)
        return 0.0f;

    switch (node->type)
    {
    case NODE_ARRAY_ACCESS:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in float context");
            return 0.0f;
        }
        return *(float *)evaluate_multi_array_access(node);
    }
    case NODE_FLOAT:
        return node->data.fvalue;
    case NODE_DOUBLE:
        return (float)node->data.dvalue;
    case NODE_INT:
        return (float)node->data.ivalue;
    case NODE_IDENTIFIER:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in float context");
            return 0.0f;
        }
        String error = {.data = "Undefined variable",
                        .len = sizeof("Undefined variable") - 1};
        return *(float *)handle_identifier(node, error, 2);
    }
    case NODE_OPERATION:
    {
        int result_type = get_expression_type(node);
        if (result_type == VAR_BOOL)
            return (float)evaluate_expression_bool(node);
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in float context");
            return 0.0f;
        }
        void *result = handle_binary_operation(node);
        float result_float = 0.0f;
        result_float = (result_type == VAR_INT) ? (float)(*(int *)result)
                       : (result_type == VAR_FLOAT)
                           ? *(float *)result
                           : (float)(*(double *)result);
        SAFE_FREE(result);
        return result_float;
    }
    case NODE_UNARY_OPERATION:
    {
        if (node->data.unary.op == OP_DEREFERENCE)
        {
            if (get_expression_pointer_level(node) > 0)
            {
                yyerror("Cannot use pointer in float context");
                return 0.0f;
            }
            return *(float *)(uintptr_t)evaluate_expression_pointer(
                node->data.unary.operand);
        }
        if (node->data.unary.op == OP_ADDRESS_OF)
        {
            yyerror("Cannot use pointer in float context");
            return 0.0f;
        }
        float operand = evaluate_expression_float(node->data.unary.operand);
        float *result =
            (float *)handle_unary_expression(node, &operand, VAR_FLOAT);
        float return_val = *result;
        SAFE_FREE(result);
        return return_val;
    }
    case NODE_SIZEOF:
    {
        return (float)handle_sizeof(node);
    }
    case NODE_FUNC_CALL:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in float context");
            return 0.0f;
        }
        void *raw = handle_function_call(node);
        if (raw == NULL)
        {
            warn_if_native_result_void("float");
            return 0.0f;
        }
        double value;
        if (!unbox_native_numeric_result(raw, current_return_value.type,
                                         &value))
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "Native call result (%s) cannot be used in a float "
                     "context",
                     vartype_to_string(current_return_value.type));
            yyerror(error_msg);
            free_native_result_box(raw, current_return_value.type);
            return 0.0f;
        }
        SAFE_FREE(raw);
        return (float)value;
    }
    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (!resolve_struct_access(node, &def, &base, &fld, true))
            return 0;
        void *addr = (char *)base + fld->offset;
        if (fld->pointer_level > 0)
            return (float)*(uintptr_t *)addr;
        switch (fld->type)
        {
        case VAR_INT:
        case VAR_ENUM:
            return (float)*(int *)addr;
        case VAR_SHORT:
            return (float)*(short *)addr;
        case VAR_BOOL:
            return (float)*(bool *)addr;
        case VAR_CHAR:
            /* unsigned char: see evaluate_expression_int()'s
               OP_DEREFERENCE case for why. */
            return (float)*(unsigned char *)addr;
        case VAR_FLOAT:
            return *(float *)addr;
        case VAR_DOUBLE:
            return (float)*(double *)addr;
        default:
            return 0;
        }
    }
    default:
        yyerror("Invalid float expression");
        return 0.0f;
    }
}

double evaluate_expression_double(ASTNode *node)
{
    if (!node)
        return 0.0L;

    switch (node->type)
    {
    case NODE_ARRAY_ACCESS:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in double context");
            return 0.0;
        }
        return *(double *)evaluate_multi_array_access(node);
    }
    case NODE_DOUBLE:
        return node->data.dvalue;
    case NODE_FLOAT:
        return (double)node->data.fvalue;
    case NODE_INT:
        return (double)node->data.ivalue;
    case NODE_IDENTIFIER:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in double context");
            return 0.0;
        }
        String error = {.data = "Undefined variable",
                        .len = sizeof("Undefined variable") - 1};
        return *(double *)handle_identifier(node, error, 1);
    }
    case NODE_OPERATION:
    {
        int result_type = get_expression_type(node);
        if (result_type == VAR_BOOL)
            return (double)evaluate_expression_bool(node);
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in double context");
            return 0.0;
        }
        void *result = handle_binary_operation(node);
        double result_double = 0.0L;
        result_double = (result_type == VAR_INT) ? (double)(*(int *)result)
                        : (result_type == VAR_FLOAT)
                            ? (double)(*(float *)result)
                            : *(double *)result;
        SAFE_FREE(result);
        return result_double;
    }
    case NODE_UNARY_OPERATION:
    {
        if (node->data.unary.op == OP_DEREFERENCE)
        {
            if (get_expression_pointer_level(node) > 0)
            {
                yyerror("Cannot use pointer in double context");
                return 0.0;
            }
            return *(double *)(uintptr_t)evaluate_expression_pointer(
                node->data.unary.operand);
        }
        if (node->data.unary.op == OP_ADDRESS_OF)
        {
            yyerror("Cannot use pointer in double context");
            return 0.0;
        }
        double operand = evaluate_expression_double(node->data.unary.operand);
        double *result =
            (double *)handle_unary_expression(node, &operand, VAR_DOUBLE);
        double return_val = *result;
        SAFE_FREE(result);
        return return_val;
    }
    case NODE_SIZEOF:
    {
        return (double)handle_sizeof(node);
    }
    case NODE_FUNC_CALL:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in double context");
            return 0.0L;
        }
        void *raw = handle_function_call(node);
        if (raw == NULL)
        {
            warn_if_native_result_void("double");
            return 0.0L;
        }
        double value;
        if (!unbox_native_numeric_result(raw, current_return_value.type,
                                         &value))
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "Native call result (%s) cannot be used in a double "
                     "context",
                     vartype_to_string(current_return_value.type));
            yyerror(error_msg);
            free_native_result_box(raw, current_return_value.type);
            return 0.0L;
        }
        SAFE_FREE(raw);
        return value;
    }
    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (!resolve_struct_access(node, &def, &base, &fld, true))
            return 0;
        void *addr = (char *)base + fld->offset;
        if (fld->pointer_level > 0)
            return (double)*(uintptr_t *)addr;
        switch (fld->type)
        {
        case VAR_INT:
        case VAR_ENUM:
            return (double)*(int *)addr;
        case VAR_SHORT:
            return (double)*(short *)addr;
        case VAR_BOOL:
            return (double)*(bool *)addr;
        case VAR_CHAR:
            /* unsigned char: see evaluate_expression_int()'s
               OP_DEREFERENCE case for why. */
            return (double)*(unsigned char *)addr;
        case VAR_FLOAT:
            return (double)*(float *)addr;
        case VAR_DOUBLE:
            return *(double *)addr;
        default:
            return 0;
        }
    }
    default:
        yyerror("Invalid double expression");
        return 0.0L;
    }
}
size_t get_type_size(String name)
{
    Variable *var = get_variable(name);
    if (var != NULL)
    {
        /* A struct/union value has no primitive size; look up its
           definition and use the computed layout size instead. Pointers
           to structs fall through to the descriptor path (pointer size). */
        if (var->var_type == VAR_STRUCT && var->pointer_level == 0)
        {
            StructDef *def = get_struct_def(var->struct_name);
            if (def != NULL)
                return var->is_array ? def->total_size * var->array_length
                                     : def->total_size;
            yyerror("Unknown struct or union type");
            return 0;
        }
        size_t base = get_type_size_for_descriptor(
            var->var_type, var->pointer_level, var->modifiers);
        if (base == 0)
        {
            yyerror("Undefined variable in sizeof");
            return 0;
        }
        return var->is_array ? base * var->array_length : base;
    }
    /* Not a variable -- a bare enum constant (e.g. `maxxing(RED)`) has type
       int in C. */
    if (find_global_enum_constant(name) != NULL)
    {
        return sizeof(int);
    }
    yyerror("Undefined variable in sizeof");
    return 0;
}

size_t handle_sizeof(ASTNode *node)
{
    ASTNode *expr = node->data.sizeof_stmt.expr;

    if (expr->type == NODE_IDENTIFIER)
    {
        // For identifiers, use get_type_size which looks up the variable
        return get_type_size(expr->data.name);
    }

    /* sizeof's operand is never evaluated -- that's its defining
           property, matching C. get_expression_type() alone doesn't
           honor that for a native call: its own NODE_FUNC_CALL case
           falls back to actually invoking a legacy/untyped native to
           learn a type this function has no way to know is only needed
           for a byte count, not a value -- and that fallback isn't
           limited to a bare call as the whole operand: get_expression_
           type()'s NODE_OPERATION/NODE_UNARY_OPERATION cases recurse
           into the very same fallback for any native call nested
           *inside* a larger expression too (`maxxing(a + legacy_int())`).
           Gating on infer_runtime_expression_type_noeval() (above) over
           the *whole* operand first closes that: it recurses the same
           shapes get_expression_type() does, but its own NODE_FUNC_CALL
           case only ever consults get_native_call_static_type() (never
           native_call_peek()), and propagates NONE up through any
           containing operation/unary node rather than resolving it by
           running something. If it reports NONE, get_expression_type()
           below is *guaranteed* not to hit its own execute-to-discover
           fallback either, for exactly the same reason (every builtin
           call reachable from `expr` already static-typed, or this gate
           would already have returned NONE) -- so this doesn't replace
           get_expression_type() below, just proves ahead of time that
           calling it is safe. */
    if (infer_runtime_expression_type_noeval(expr) == NONE)
    {
        yyerror("maxxing (sizeof) of an expression whose type is not "
                "statically known -- sizeof's operand is never "
                "evaluated, so a native call it depends on cannot be "
                "invoked to discover it");
        return 0;
    }

    // For non-identifiers (like literals), use get_expression_type
    VarType type = get_expression_type(expr);
    switch (type)
    {
    case VAR_INT:
    case VAR_FLOAT:
    case VAR_DOUBLE:
    case VAR_SHORT:
    case VAR_BOOL:
    case VAR_CHAR:
    case VAR_ENUM:
    /* Round-19 review, finding #3 -- get_type_size_for_descriptor()
       (above) has always defined VAR_STRING as sizeof(String),
       and a plain identifier's sizeof (the branch above this
       `else`) already uses it via get_type_size(); this generic
       path (a native-call expression like `identity(buf)`
       resolving to STDROT_STRING, per get_native_call_static_
       type()) fell to the `default: yyerror("Invalid type in
       sizeof")` below purely because this case was missing --
       an AST-shape accident, not an actual "strings have no
       size" rule, since the identical VarType already has a
       well-defined size everywhere else. */
    case VAR_STRING:
    /* A typed native's STDROT_PTR result (or any other pointer-
       typed expression reaching this generic path) -- get_type_
       size_for_descriptor() already handles pointer_level > 0
       correctly (sizeof(uintptr_t)) via its own unconditional
       check at the top; this case was simply missing, so a
       pointer-typed non-identifier expression fell to the
       `default: yyerror("Invalid type in sizeof")` case below
       despite being a perfectly well-defined size. */
    case VAR_PTR:
        return get_type_size_for_descriptor(
            type, get_expression_pointer_level(expr), expr->modifiers);
    case VAR_STRUCT:
    {
        int plevel = get_expression_pointer_level(expr);
        if (plevel > 0)
            return get_type_size_for_descriptor(type, plevel, expr->modifiers);

        StructDef *sdef = get_struct_def_for_expression(expr);
        if (sdef != NULL)
            return sdef->total_size;
        yyerror("Invalid type in sizeof");
        return 0;
    }
    default:
        yyerror("Invalid type in sizeof");
        return 0;
    }
}

String evaluate_expression_string(ASTNode *node)
{
    if (!node)
        return (String){.data = NULL, .len = 0};

    switch (node->type)
    {
    case NODE_STRING_LITERAL:
    case NODE_STRING:
        return safe_strdup(&node->data.strvalue);
    case NODE_IDENTIFIER:
    {
        String error = {.data = "Undefined variable",
                        .len = sizeof("Undefined variable") - 1};
        return safe_strdup((String *)handle_identifier(node, error, 3));
    }
    case NODE_FUNC_CALL:
    {
        /* A pointer-level result is boxed as a uintptr_t (see
           handle_function_call()'s VAR_INT case), not a String -- without
           this check, the cast below would reinterpret that
           sizeof(uintptr_t)-byte allocation as a much larger String
           struct and read res->data/res->len straight past the end of
           it, not just misread a value like the numeric evaluators'
           equivalent guard, an actual out-of-bounds heap read. */
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in string context");
            return (String){.data = NULL, .len = 0};
        }
        void *raw = handle_function_call(node);
        if (raw == NULL)
        {
            warn_if_native_result_void("string");
            return (String){.data = NULL, .len = 0};
        }
        /* Unlike the numeric evaluators, there is no coercion group to
           fall back on here -- a native call result is either genuinely
           a String (current_return_value.type == VAR_STRING, in which
           case `raw` really is handle_function_call()'s malloc'd String*
           container) or it isn't, and reinterpreting anything else as
           one reads ->data/->len straight past a smaller allocation (see
           the pointer-level guard above for the same failure mode with a
           STDROT_PTR result specifically -- this closes the identical
           hole for every other mismatched type too, e.g. a legacy
           STDROT_ANY export that actually returned an int). */
        if (current_return_value.type != VAR_STRING)
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "Native call result (%s) cannot be used in a string "
                     "context",
                     vartype_to_string(current_return_value.type));
            yyerror(error_msg);
            SAFE_FREE(raw);
            return (String){.data = NULL, .len = 0};
        }
        String *res = (String *)raw;
        String result = safe_strdup(res);
        /* res is handle_function_call()'s malloc'd String* container
           (VAR_STRING case), already itself holding a freshly
           safe_strdup'd buffer -- free that buffer too, not just the
           container, or it's never reachable again after this
           function returns. */
        SAFE_FREE(res->data);
        SAFE_FREE(res);
        return result;
    }
    default:
        yyerror("Invalid string expression");
        return (String){.data = NULL, .len = 0};
    }
}

short evaluate_expression_short(ASTNode *node)
{
    if (!node)
        return 0;

    switch (node->type)
    {
    case NODE_INT:
        return (short)node->data.ivalue;
    case NODE_BOOLEAN:
        return (short)node->data.bvalue; // Already 1 or 0
    case NODE_CHAR:                      // Add explicit handling for characters
        return (short)node->data.ivalue;
    case NODE_SHORT:
        return node->data.svalue;
    case NODE_FLOAT:
        yyerror("Cannot use float in integer context");
        return (short)node->data.fvalue;
    case NODE_DOUBLE:
        yyerror("Cannot use double in integer context");
        return (short)node->data.dvalue;
    case NODE_SIZEOF:
    {
        return handle_sizeof(node);
    }
    case NODE_IDENTIFIER:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in integer context");
            return 0;
        }
        String error = {.data = "Undefined variable",
                        .len = sizeof("Undefined variable") - 1};
        return *(short *)handle_identifier(node, error, 0);
    }
    case NODE_OPERATION:
    {
        // Special handling for logical operations
        if (node->data.op.op == OP_AND || node->data.op.op == OP_OR)
        {
            short left = evaluate_expression_short(node->data.op.left);
            short right = evaluate_expression_short(node->data.op.right);

            switch (node->data.op.op)
            {
            case OP_AND:
                return left && right;
            case OP_OR:
                return left || right;
            default:
                break;
            }
        }

        // Regular integer operations
        int result_type = get_expression_type(node);
        if (result_type == VAR_BOOL)
            return (short)evaluate_expression_bool(node);
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in integer context");
            return 0;
        }
        void *result = handle_binary_operation(node);
        short result_short = 0;
        result_short = (result_type == VAR_SHORT)   ? *(short *)result
                       : (result_type == VAR_FLOAT) ? (short)(*(float *)result)
                       : (result_type == VAR_DOUBLE)
                           ? (short)(*(double *)result)
                           : (short)(*(int *)result);
        SAFE_FREE(result);
        return result_short;
    }
    case NODE_UNARY_OPERATION:
    {
        if (node->data.unary.op == OP_DEREFERENCE)
        {
            if (get_expression_pointer_level(node) > 0)
            {
                yyerror("Cannot use pointer in integer context");
                return 0;
            }
            return *(short *)(uintptr_t)evaluate_expression_pointer(
                node->data.unary.operand);
        }
        if (node->data.unary.op == OP_ADDRESS_OF)
        {
            yyerror("Cannot use pointer in integer context");
            return 0;
        }
        short operand = evaluate_expression_short(node->data.unary.operand);
        short *result =
            (short *)handle_unary_expression(node, &operand, VAR_SHORT);
        short return_val = *result;
        SAFE_FREE(result);
        return return_val;
    }
    case NODE_ARRAY_ACCESS:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in integer context");
            return 0;
        }
        return *(short *)evaluate_multi_array_access(node);
    }
    case NODE_FUNC_CALL:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in integer context");
            return 0;
        }
        void *raw = handle_function_call(node);
        if (raw == NULL)
        {
            warn_if_native_result_void("integer");
            return 0;
        }
        double value;
        if (!unbox_native_numeric_result(raw, current_return_value.type,
                                         &value))
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "Native call result (%s) cannot be used in an "
                     "integer context",
                     vartype_to_string(current_return_value.type));
            yyerror(error_msg);
            free_native_result_box(raw, current_return_value.type);
            return 0;
        }
        SAFE_FREE(raw);
        return (short)value;
    }
    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (!resolve_struct_access(node, &def, &base, &fld, true))
            return 0;
        void *addr = (char *)base + fld->offset;
        if (fld->pointer_level > 0)
            return (short)*(uintptr_t *)addr;
        switch (fld->type)
        {
        case VAR_INT:
        case VAR_ENUM:
            return (short)*(int *)addr;
        case VAR_SHORT:
            return *(short *)addr;
        case VAR_BOOL:
            return (short)*(bool *)addr;
        case VAR_CHAR:
            /* unsigned char: see evaluate_expression_int()'s
               OP_DEREFERENCE case for why. */
            return (short)*(unsigned char *)addr;
        case VAR_FLOAT:
            return (short)*(float *)addr;
        case VAR_DOUBLE:
            return (short)*(double *)addr;
        default:
            return 0;
        }
    }
    default:
        yyerror("Invalid short expression");
        return 0;
    }
}

int evaluate_expression_int(ASTNode *node)
{
    if (!node)
        return 0;

    switch (node->type)
    {
    case NODE_INT:
        return node->data.ivalue;
    case NODE_BOOLEAN:
        return node->data.bvalue; // Already 1 or 0
    case NODE_CHAR:               // Add explicit handling for characters
        return node->data.ivalue;
    case NODE_SHORT:
        return node->data.svalue;
    case NODE_FLOAT:
        yyerror("Cannot use float in integer context");
        return (int)node->data.fvalue;
    case NODE_DOUBLE:
        yyerror("Cannot use double in integer context");
        return (int)node->data.dvalue;
    case NODE_SIZEOF:
    {
        return handle_sizeof(node);
    }
    case NODE_IDENTIFIER:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in integer context");
            return 0;
        }
        String error = {.data = "Undefined variable",
                        .len = sizeof("Undefined variable") - 1};
        return *(int *)handle_identifier(node, error, 0);
    }
    case NODE_OPERATION:
    {
        if (get_expression_type(node) == VAR_BOOL)
            return evaluate_expression_bool(node) ? 1 : 0;
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in integer context");
            return 0;
        }
        // Special handling for logical operations
        if (node->data.op.op == OP_AND)
        {
            int left = evaluate_expression_int(node->data.op.left);
            if (!left)
                return 0;
            int right = evaluate_expression_int(node->data.op.right);
            return right != 0;
        }
        if (node->data.op.op == OP_OR)
        {
            int left = evaluate_expression_int(node->data.op.left);
            if (left)
                return 1;
            int right = evaluate_expression_int(node->data.op.right);
            return right != 0;
        }

        // Regular integer operations
        int result_type = get_expression_type(node);
        void *result = handle_binary_operation(node);
        int result_int = 0;
        result_int = (result_type == VAR_INT)     ? *(int *)result
                     : (result_type == VAR_FLOAT) ? (int)(*(float *)result)
                                                  : (int)(*(double *)result);
        SAFE_FREE(result);
        return result_int;
    }
    case NODE_UNARY_OPERATION:
    {
        if (node->data.unary.op == OP_DEREFERENCE)
        {
            if (get_expression_pointer_level(node) > 0)
            {
                yyerror("Cannot use pointer in integer context");
                return 0;
            }
            // See the NODE_UNARY_OPERATION/OP_DEREFERENCE case in
            // evaluate_expression_pointer() above for why a possible NULL
            // dereference here is expected/deferred, not a bug to silently
            // wave off. Both returns below dereference `pointee`, so both
            // need their own suppression -- NOLINTNEXTLINE only covers the
            // one line immediately after it.
            void *pointee = (void *)(uintptr_t)evaluate_expression_pointer(
                node->data.unary.operand);
            /* get_expression_type() on a dereference returns the
               operand's own type (see that function's own
               NODE_UNARY_OPERATION case) -- for a `yap *p`, that's the
               pointee's real VarType, VAR_CHAR, regardless of what `p`
               happens to point at (a scalar variable's union slot, or a
               genuinely packed array/struct byte). Same width bug as
               the NODE_ARRAY_ACCESS case below if left as a blind
               `*(int *)`: a `yap *` into a packed buffer would over-read
               3 bytes past a real 1-byte slot. `unsigned char`, not
               `char`: char_scalar_slot_value() (ast.h) zero-extends a
               scalar `yap` variable's own slot on every write, so a
               scalar read is always 0-255; a signed `*(char *)` read
               here would disagree with that for every packed byte
               >= 128 (confirmed: after `c = 1000`, a scalar read gives
               232, a signed packed read gave -24) even though both are
               reading what should be the same logical value. */
            if (get_expression_type(node) == VAR_CHAR)
                // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
                return (int)*(unsigned char *)pointee;
            // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
            return *(int *)pointee;
        }
        if (node->data.unary.op == OP_ADDRESS_OF)
        {
            yyerror("Cannot use pointer in integer context");
            return 0;
        }
        int operand = evaluate_expression_int(node->data.unary.operand);
        int *result = (int *)handle_unary_expression(node, &operand, VAR_INT);
        int return_val = *result;
        SAFE_FREE(result);
        return return_val;
    }
    case NODE_ARRAY_ACCESS:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in integer context");
            return 0;
        }
        /* This function is the shared "integer family" evaluator for
           every element type that doesn't get its own dedicated
           evaluate_expression_{bool,short,float,double}() -- VAR_CHAR
           and VAR_ENUM both funnel through here (get_expression_type()
           already maps a char literal to VAR_INT). Unlike those other
           four types, whose array-element width always matches this
           function's own `int` return width, a `yap` array element is
           1 byte: reading it back as `*(int *)` over-reads 3 bytes past
           a single-element array and misreads every other element's
           address as if elements were 4 bytes apart instead of 1
           (mirrors the write-side bug fixed in write_value_to_address()
           for the identical reason -- VAR_CHAR is the one type whose
           packed-array width doesn't match this function's own).
           `unsigned char`, not `char`: see the matching comment on the
           OP_DEREFERENCE case above -- a signed read here would
           disagree with a scalar `yap` variable's own always-0-255
           representation (char_scalar_slot_value(), ast.h) for every
           byte >= 128. */
        void *addr = evaluate_multi_array_access(node);
        if (get_expression_type(node) == VAR_CHAR)
            return (int)*(unsigned char *)addr;
        return *(int *)addr;
    }
    case NODE_FUNC_CALL:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            yyerror("Cannot use pointer in integer context");
            return 0;
        }
        void *raw = handle_function_call(node);
        if (raw == NULL)
        {
            warn_if_native_result_void("integer");
            return 0;
        }
        double value;
        if (!unbox_native_numeric_result(raw, current_return_value.type,
                                         &value))
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "Native call result (%s) cannot be used in an "
                     "integer context",
                     vartype_to_string(current_return_value.type));
            yyerror(error_msg);
            free_native_result_box(raw, current_return_value.type);
            return 0;
        }
        SAFE_FREE(raw);
        return (int)value;
    }
    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (!resolve_struct_access(node, &def, &base, &fld, true))
            return 0;
        void *addr = (char *)base + fld->offset;
        if (fld->pointer_level > 0)
            return (int)*(uintptr_t *)addr;
        switch (fld->type)
        {
        case VAR_INT:
        case VAR_ENUM:
            return *(int *)addr;
        case VAR_SHORT:
            return (int)*(short *)addr;
        case VAR_BOOL:
            return (int)*(bool *)addr;
        case VAR_CHAR:
            /* unsigned char, not char: must agree with a scalar `yap`
               variable's own always-0-255 representation
               (char_scalar_slot_value(), ast.h) -- see the matching
               comment on evaluate_expression_int()'s OP_DEREFERENCE
               case for the full reasoning. */
            return (int)*(unsigned char *)addr;
        case VAR_FLOAT:
            return (int)*(float *)addr;
        case VAR_DOUBLE:
            return (int)*(double *)addr;
        default:
            return 0;
        }
    }
    default:
        yyerror("Invalid integer expression");
        return 0;
    }
}

/* Marshals a native call's StdrotValue result into current_return_value,
   the same global slot execute_function_call() populates for user-defined
   functions, so the void*-marshalling switch below (and every
   evaluate_expression_* caller of handle_function_call) can treat native
   and Brainrot calls identically. */
static void marshal_native_return_value(ASTNode *node)
{
    free_pending_return_value();
    NativeResult nr = native_call_consume(node);
    StdrotValue result = nr.value;

    current_return_value.pointer_level = 0;
    current_return_value.struct_name = (String){0};
    /* nr.owns_string (NativeResult, stdrot.h) is scoped to exactly this
       `nr` -- not a global -- so it can only ever be true here when THIS
       call's own result really is the materialized string it describes,
       regardless of what any nested call evaluated while marshalling
       this call's arguments (or consumed from this cache) set it to. */
    current_return_value.owns_strvalue =
        result.type == STDROT_STRING && nr.owns_string;
    /* An opaque native pointer reuses VAR_INT + pointer_level, the same
       representation every other pointer-typed value in this interpreter
       already uses (see e.g. handle_function_call()'s VAR_INT case, which
       boxes current_return_value.value.pvalue as a uintptr_t whenever
       pointer_level > 0) -- not because a raw address is semantically an
       int, but because that's the storage convention already in place
       for "an address with no further type information attached", and
       reusing it means every existing consumer of a pointer-valued
       expression (dereference, pointer arithmetic, assignment to a
       pointer variable, ...) already knows how to handle it. The
       semantic analyzer, separately, reports VAR_PTR for this same
       call's static type (see infer_expression_type()) -- the two don't
       need to agree on a VarType tag (this function tags it VAR_INT, not
       VAR_PTR), only on pointer_level and the raw
       value. */
    if (result.type == STDROT_PTR)
    {
        const StdrotEntry *entry =
            get_native_function(node->data.func_call.function_name);
        current_return_value.type = VAR_INT;
        current_return_value.pointer_level =
            (entry ? entry->return_type.pointer_level : 0) + 1;
    }
    else
    {
        current_return_value.type = stdrot_type_to_vartype(result.type);
    }
    current_return_value.has_value = result.type != STDROT_NONE;

    switch (result.type)
    {
    case STDROT_INT:
        current_return_value.value.ivalue = result.val.i;
        break;
    case STDROT_FLOAT:
        current_return_value.value.fvalue = result.val.f;
        break;
    case STDROT_DOUBLE:
        current_return_value.value.dvalue = result.val.d;
        break;
    case STDROT_SHORT:
        current_return_value.value.svalue = result.val.s;
        break;
    case STDROT_BOOL:
        current_return_value.value.bvalue = result.val.b;
        break;
    case STDROT_CHAR:
        current_return_value.value.ivalue = (unsigned char)result.val.c;
        break;
    case STDROT_STRING:
        current_return_value.value.strvalue = result.val.str;
        break;
    case STDROT_PTR:
        current_return_value.value.pvalue = (uintptr_t)result.val.ptr;
        break;
    case STDROT_CSTRING:
    case STDROT_HANDLE:
        /* semantic_check_native_call() rejects any call to a native whose
           return_type.type is STDROT_CSTRING or STDROT_HANDLE outright
           (CSTRING has no return-side marshalling implemented -- this
           switch has no case that would populate strvalue for it; HANDLE
           needs a resource-ownership model Phase 2 hasn't designed yet,
           see roadmap Appendix B Q6) -- so a Brainrot program can never
           reach this call with result.type equal to either, structurally
           unreachable here. Add real marshalling (and remove the
           semantic-analyzer rejection) once a builtin actually needs to
           return one. */
    case STDROT_ANY:
        /* STDROT_ANY is a descriptor placeholder ("type genuinely
           unknown" or "identity-polymorphic", see StdrotEntry's own
           comment in stdrot_api.h) -- no native's actual StdrotValue
           result should ever be tagged STDROT_ANY itself; if one is,
           there is nothing meaningful to unbox. */
    case STDROT_NONE:
        break;
    }
}

void *handle_function_call(ASTNode *node)
{
    const String func_name = node->data.func_call.function_name;
    if (is_builtin_function(func_name))
    {
        marshal_native_return_value(node);
    }
    else
    {
        execute_function_call(func_name, node->data.func_call.arguments);
    }
    void *return_value = NULL;
    if (current_return_value.has_value)
    {
        /* Round-22 review, finding #1 -- a type here is (base VarType,
           pointer_level), and pointer_level DOMINATES: any expression
           with pointer_level > 0 marshals as a raw address regardless
           of its base type, the same rule ast_expr_to_stdrot_value()
           (stdrot.c) already enforces for the native-call boundary
           (checked before ITS OWN type-specific switch, for the exact
           same reason). Checking this before the switch below --
           instead of duplicating a `pointer_level > 0` branch inside
           only the VAR_INT/VAR_ENUM cases, as this function used to --
           means a user-defined function returning `chad *`/`yap *`/
           `cap *`/`smol *`/`skibidi *` (any base type at all, pointer_
           level > 0) is boxed as an address here too: previously those
           fell into their base type's ordinary SCALAR case (VAR_FLOAT,
           VAR_CHAR, VAR_BOOL, VAR_SHORT, or -- for skibidi * specifically
           -- VAR_VOID's own "structurally unreachable" case, which
           returned NULL despite this call genuinely having a value),
           reinterpreting a real address as if it were the scalar value
           at that address, or discarding it outright. */
        if (current_return_value.pointer_level > 0)
        {
            return_value = SAFE_MALLOC(uintptr_t);
            *(uintptr_t *)return_value = current_return_value.value.pvalue;
            return return_value;
        }

        switch (current_return_value.type)
        {
        case VAR_INT:
            return_value = SAFE_MALLOC(int);
            *(int *)return_value = current_return_value.value.ivalue;
            break;
        case VAR_FLOAT:
            return_value = SAFE_MALLOC(float);
            *(float *)return_value = current_return_value.value.fvalue;
            break;
        case VAR_DOUBLE:
            return_value = SAFE_MALLOC(double);
            *(double *)return_value = current_return_value.value.dvalue;
            break;
        case VAR_BOOL:
            return_value = SAFE_MALLOC(bool);
            *(bool *)return_value = current_return_value.value.bvalue;
            break;
        case VAR_CHAR:
            return_value = SAFE_MALLOC(char);
            *(char *)return_value = current_return_value.value.ivalue;
            break;
        case VAR_SHORT:
            return_value = SAFE_MALLOC(short);
            *(short *)return_value = current_return_value.value.svalue;
            break;
        case VAR_STRING:
            return_value = SAFE_MALLOC(String);
            *(String *)return_value =
                safe_strdup(&current_return_value.value.strvalue);
            /* current_return_value.owns_strvalue (ast.h, set by
               marshal_native_return_value() from that call's own
               NativeResult, stdrot.h): true only for a native result the
               ABI boundary already knows is a heap buffer nothing else
               references -- safe (and necessary, to avoid leaking it)
               to free now that the copy above has captured everything
               the caller needs. False for every other VAR_STRING source
               (a Brainrot-defined function's own return, or a native
               result that might alias a string literal or a live
               variable's own backing storage) -- freeing those
               unconditionally would corrupt still-live state, which is
               exactly why this was never done unconditionally before. */
            if (current_return_value.owns_strvalue)
            {
                SAFE_FREE(current_return_value.value.strvalue.data);
                current_return_value.owns_strvalue = false;
            }
            break;
        case VAR_STRUCT:
            /* A struct return has no meaningful representation in this
               generic scalar-expression context; discard it (freeing the
               blob handle_return_statement allocated) rather than leak. */
            free_pending_return_value();
            break;
        case VAR_ENUM:
            /* pointer_level > 0 already handled above, before this
               switch -- an enum pointer return reaches this case only
               with pointer_level == 0, an ordinary by-value enum. */
            return_value = SAFE_MALLOC(int);
            *(int *)return_value = current_return_value.value.ivalue;
            break;
        case VAR_PTR:
            /* marshal_native_return_value() always sets
               current_return_value.type to VAR_INT (not VAR_PTR) for a
               STDROT_PTR result, reusing the pointer-boxing check above --
               VAR_PTR is the semantic analyzer's own static type for the
               expression, a separate concern from this runtime value's
               representation (see that function's comment). Structurally
               unreachable here. */
        case VAR_VOID:
            /* pointer_level > 0 (`skibidi *`) already handled above,
               before this switch -- reaching HERE with type == VAR_VOID
               means pointer_level == 0, i.e. genuinely void, and this
               whole switch is gated on current_return_value.has_value
               above; marshal_native_return_value() sets has_value false
               for a genuinely void (STDROT_NONE) native result, and
               (once handle_return_statement() is fixed to match, see
               its own VAR_VOID case) a Brainrot-defined void function
               never sets has_value true either. Reaching here would
               mean has_value was true for a call known to return
               nothing -- a bug elsewhere, not a case this function
               should paper over. */
        case NONE:
            return NULL;
        }
    }
    return return_value;
}

bool evaluate_expression_bool(ASTNode *node)
{
    if (!node)
        return 0;

    switch (node->type)
    {
    case NODE_INT:
        return (bool)node->data.ivalue;
    case NODE_SHORT:
        return (bool)node->data.svalue;
    case NODE_BOOLEAN:
        return node->data.bvalue;
    case NODE_CHAR:
        return (bool)node->data.ivalue;
    case NODE_FLOAT:
        return (bool)node->data.fvalue;
    case NODE_DOUBLE:
        return (bool)node->data.dvalue;
    case NODE_IDENTIFIER:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            return evaluate_expression_pointer(node) != (uintptr_t)0;
        }
        String error = {.data = "Undefined variable",
                        .len = sizeof("Undefined variable") - 1};
        return *(bool *)handle_identifier(node, error, 0);
    }
    case NODE_OPERATION:
    {
        int left_ptr_level = get_expression_pointer_level(node->data.op.left);
        int right_ptr_level = get_expression_pointer_level(node->data.op.right);
        if (left_ptr_level > 0 || right_ptr_level > 0)
        {
            uintptr_t left =
                left_ptr_level > 0
                    ? evaluate_expression_pointer(node->data.op.left)
                    : (uintptr_t)evaluate_expression_int(node->data.op.left);
            uintptr_t right =
                right_ptr_level > 0
                    ? evaluate_expression_pointer(node->data.op.right)
                    : (uintptr_t)evaluate_expression_int(node->data.op.right);
            switch (node->data.op.op)
            {
            case OP_EQ:
                return left == right;
            case OP_NE:
                return left != right;
            case OP_LT:
                return left < right;
            case OP_GT:
                return left > right;
            case OP_LE:
                return left <= right;
            case OP_GE:
                return left >= right;
            case OP_AND:
                return left && right;
            case OP_OR:
                return left || right;
            default:
                yyerror("Invalid pointer operation");
                return false;
            }
        }
        // Special handling for logical operations
        if (node->data.op.op == OP_AND)
        {
            bool left = evaluate_expression_bool(node->data.op.left);
            if (!left)
                return false;
            bool right = evaluate_expression_bool(node->data.op.right);
            return right;
        }
        if (node->data.op.op == OP_OR)
        {
            bool left = evaluate_expression_bool(node->data.op.left);
            if (left)
                return true;
            bool right = evaluate_expression_bool(node->data.op.right);
            return right;
        }

        // Regular integer operations
        int result_type = get_expression_type(node);
        void *result = handle_binary_operation(node);
        bool result_bool = 0;
        result_bool = (result_type == VAR_BOOL)    ? (*(int *)result != 0)
                      : (result_type == VAR_INT)   ? (bool)(*(int *)result)
                      : (result_type == VAR_FLOAT) ? (bool)(*(float *)result)
                                                   : (bool)(*(double *)result);
        SAFE_FREE(result);
        return result_bool;
    }
    case NODE_UNARY_OPERATION:
    {
        if (node->data.unary.op == OP_ADDRESS_OF ||
            get_expression_pointer_level(node) > 0)
            return evaluate_expression_pointer(node) != (uintptr_t)0;
        if (node->data.unary.op == OP_DEREFERENCE)
            // See the NODE_UNARY_OPERATION/OP_DEREFERENCE case in
            // evaluate_expression_pointer() above for why a possible NULL
            // dereference here is expected/deferred, not a bug to silently
            // wave off.
            // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
            return *(bool *)(uintptr_t)evaluate_expression_pointer(
                node->data.unary.operand);
        bool operand = evaluate_expression_bool(node->data.unary.operand);
        bool *result =
            (bool *)handle_unary_expression(node, &operand, VAR_BOOL);
        bool return_val = *result;
        SAFE_FREE(result);
        return return_val;
    }
    case NODE_ARRAY_ACCESS:
    {
        if (get_expression_pointer_level(node) > 0)
        {
            return *(uintptr_t *)evaluate_multi_array_access(node) !=
                   (uintptr_t)0;
        }
        return *(bool *)evaluate_multi_array_access(node);
    }
    case NODE_FUNC_CALL:
    {
        /* Same reasoning as NODE_IDENTIFIER/NODE_OPERATION above: a
           pointer-level result (a STDROT_PTR-returning native call) is
           boxed as a uintptr_t (see handle_function_call()'s VAR_INT
           case), not a bool -- reading it through a bare `bool *` would
           reinterpret the pointer's low byte as the whole truth value
           instead of doing a proper != 0 comparison on the real address. */
        if (get_expression_pointer_level(node) > 0)
        {
            uintptr_t *res = (uintptr_t *)handle_function_call(node);
            if (res != NULL)
            {
                bool return_val = *res != (uintptr_t)0;
                SAFE_FREE(res);
                return return_val;
            }
            return 0;
        }
        void *raw = handle_function_call(node);
        if (raw == NULL)
        {
            warn_if_native_result_void("bool");
            return 0;
        }
        /* No numeric-coercion group applies to bool (see
           check_type_compatibility_ex(), semantic_analyzer.c -- VAR_BOOL
           isn't part of the int/short/float/double/enum group), so this
           requires an exact match rather than falling back to
           unbox_native_numeric_result(): a mismatched result (e.g. a
           legacy STDROT_ANY export that actually returned a string) must
           be rejected here the same way it already is for the numeric
           evaluators, not silently reinterpreted through a `bool *`. */
        if (current_return_value.type != VAR_BOOL)
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "Native call result (%s) cannot be used in a bool "
                     "context",
                     vartype_to_string(current_return_value.type));
            yyerror(error_msg);
            free_native_result_box(raw, current_return_value.type);
            return 0;
        }
        bool return_val = *(bool *)raw;
        SAFE_FREE(raw);
        return return_val;
    }
    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (!resolve_struct_access(node, &def, &base, &fld, true))
            return 0;
        void *addr = (char *)base + fld->offset;
        if (fld->pointer_level > 0)
            return (bool)*(uintptr_t *)addr;
        switch (fld->type)
        {
        case VAR_INT:
        case VAR_ENUM:
            return (bool)*(int *)addr;
        case VAR_SHORT:
            return (bool)*(short *)addr;
        case VAR_BOOL:
            return *(bool *)addr;
        case VAR_CHAR:
            return (bool)*(char *)addr;
        case VAR_FLOAT:
            return (bool)*(float *)addr;
        case VAR_DOUBLE:
            return (bool)*(double *)addr;
        default:
            return 0;
        }
    }
    default:
        yyerror("Invalid boolean expression");
        return 0;
    }
}

ArgumentList *create_argument_list(ASTNode *expr, ArgumentList *existing_list)
{
    ArgumentList *new_node = ARENA_ALLOC(ArgumentList);
    new_node->expr = expr;
    new_node->next = NULL;

    if (!existing_list)
    {
        return new_node;
    }

    /* Append to the end of existing_list */
    ArgumentList *temp = existing_list;
    while (temp->next)
    {
        temp = temp->next;
    }
    temp->next = new_node;
    return existing_list;
}

ASTNode *create_print_statement_node(ASTNode *expr)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_PRINT_STATEMENT;
    node->data.op.left = expr;
    return node;
}

ASTNode *create_error_statement_node(ASTNode *expr)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_ERROR_STATEMENT;
    node->data.op.left = expr;
    return node;
}

ASTNode *create_statement_list(ASTNode *statement, ASTNode *existing_list)
{
    if (!existing_list)
    {
        // If there's no existing list, create a new one
        ASTNode *node = ARENA_ALLOC_ASTNODE();
        if (!node)
        {
            yyerror("Memory allocation failed");
            return NULL;
        }
        node->type = NODE_STATEMENT_LIST;
        node->data.statements = ARENA_ALLOC(StatementList);
        if (!node->data.statements)
        {
            SAFE_FREE(node);
            yyerror("Memory allocation failed");
            return NULL;
        }
        node->data.statements->statement = statement;
        node->data.statements->next = NULL;
        return node;
    }

    // Append at the end of existing_list
    StatementList *sl = existing_list->data.statements;
    while (sl->next)
    {
        sl = sl->next;
    }
    // Now sl is the last element; append the new statement
    StatementList *new_item = ARENA_ALLOC(StatementList);
    if (!new_item)
    {
        yyerror("Memory allocation failed");
        return existing_list;
    }
    new_item->statement = statement;
    new_item->next = NULL;
    sl->next = new_item;
    return existing_list;
}

bool is_const_variable(const String name)
{
    Variable *var = get_variable(name);
    if (var != NULL)
    {
        return var->modifiers.is_const;
    }
    return false;
}

void check_const_assignment(const String name)
{
    if (is_const_variable(name))
    {
        yylineno = yylineno - 2;
        yyerror("Cannot modify const variable");
        ragequit(EXIT_FAILURE);
    }
}

bool is_expression(ASTNode *node, VarType type)
{
    if (!node)
        return false;

    switch (node->type)
    {
    case NODE_ARRAY_ACCESS:
    {
        ArrayAccessElement elem;
        if (resolve_array_access_element(node, &elem))
            return elem.type == type;
        yyerror("Invalid array access in type check");
        return false;
    }
    case NODE_IDENTIFIER:
    {
        String error = {.data = "Undefined variable in type check",
                        .len = sizeof("Undefined variable in type check") - 1};
        if (!check_and_mark_identifier(node, error))
            ragequit(1);
        Variable *var = get_variable(node->data.name);
        if (var != NULL)
        {
            return var->var_type == type;
        }
        /* Not a variable -- an enum constant has type int in C. */
        if (find_global_enum_constant(node->data.name) != NULL)
        {
            return type == VAR_INT;
        }
        yyerror("Undefined variable in type check");
        return false;
    }
    case NODE_OPERATION:
    {
        // For operations, check if the result type matches the target type
        return get_expression_type(node) == type;
    }
    case NODE_UNARY_OPERATION:
        /* Previously uncased, falling to the default: below --
           `node->type == VART_TO_NODET(type)` can never be true for a
           NODE_UNARY_OPERATION node (VART_TO_NODET maps to a literal
           node type like NODE_CHAR, never to NODE_UNARY_OPERATION), so
           is_expression() always reported false here regardless of the
           unary expression's actual type. That silently forced every
           unary-operation argument through ast_expr_to_stdrot_value()'s
           final STDROT_INT fallback, masking whatever
           stdrot_char_narrows_to_int() (stdrot.h) said for operators
           that shouldn't narrow (OP_DEREFERENCE, the increment/decrement
           family) -- get_expression_type(), like NODE_OPERATION just
           above, already reports the correct type for every unary
           operator (see its own NODE_UNARY_OPERATION case). */
        return get_expression_type(node) == type;
    case NODE_FUNC_CALL:
    {
        const String func_name = node->data.func_call.function_name;
        if (is_builtin_function(func_name))
        {
            /* Static-first, matching get_expression_type()'s own NODE_
               FUNC_CALL case exactly (delegated to directly, rather than
               duplicating it): a TYPED native's return type is answered
               purely from its registered descriptor, via get_native_
               call_static_type() -- no execution. native_call_peek()
               only actually runs the call for a genuine legacy/untyped
               STDROT_ANY export, where nothing describes the return type
               ahead of time, same as before. Previously called native_
               call_peek() unconditionally here, executing even a TYPED
               native merely to answer a type-check question Phase 2
               (issue #205) made statically answerable without running
               anything -- inconsistent with get_expression_type() having
               already been fixed to prefer the static answer. */
            return get_expression_type(node) == type;
        }
        VarType ret = get_function_return_type(func_name);
        /* A by-value enum return has type int in C -- treat it as such so
           general int-context callers (e.g. ast_expr_to_stdrot_value's
           varargs marshalling) recognize it without a VAR_ENUM case of
           their own. A pointer-to-enum return is not an int, though. */
        if (type == VAR_INT && ret == VAR_ENUM &&
            get_function_return_pointer_level(func_name) == 0)
            return true;
        return ret == type;
    }
    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (!resolve_struct_access(node, &def, &base, &fld, false))
            return false;
        return fld->type == type;
    }
    default:
        return node->type == VART_TO_NODET(type);
    }
}

Function *get_function(const String name)
{
    if (!function_map || !name.data)
    {
        return NULL;
    }

    size_t name_len = name.len;
    Function **func_ptr =
        (Function **)hm_get(function_map, name.data, name_len);
    if (func_ptr)
    {
        return *func_ptr;
    }
    return NULL;
}

VarType get_function_return_type(const String name)
{
    Function *func = get_function(name);
    if (func != NULL)
    {
        return func->return_type;
    }
    yyerror("Undefined function in type check");
    return NONE;
}

static int get_function_return_pointer_level(const String name)
{
    Function *func = get_function(name);
    if (func != NULL)
    {
        return func->return_pointer_level;
    }
    yyerror("Undefined function in type check");
    return 0;
}

int evaluate_expression(ASTNode *node)
{
    if (is_expression(node, VAR_SHORT))
    {
        return (short)evaluate_expression_short(node);
    }
    if (is_expression(node, VAR_FLOAT))
    {
        return (int)evaluate_expression_float(node);
    }
    if (is_expression(node, VAR_DOUBLE))
    {
        return (int)evaluate_expression_double(node);
    }
    return evaluate_expression_int(node);
}

void execute_assignment(ASTNode *node)
{
    if (node->type != NODE_ASSIGNMENT)
    {
        yyerror("Expected assignment node");
        return;
    }

    ASTNode *target = node->data.op.left;
    ASTNode *value_node = node->data.op.right;
    VarType target_type = get_expression_type(target);
    int target_pointer_level = get_expression_pointer_level(target);
    TypeModifiers mods = node->modifiers;

    if (target->type == NODE_IDENTIFIER)
    {
        String name = target->data.name;
        check_const_assignment(name);
        Variable *var = get_variable(name);
        if (!var)
        {
            yyerror("Assignment to undefined variable");
            return;
        }
        target_type = var->var_type;
        target_pointer_level = var->pointer_level;
        mods = var->modifiers;
    }
    /* NODE_STRUCT_ACCESS targets (including chains, e.g. a.b.c) already have
       target_type/target_pointer_level resolved correctly above via
       get_expression_type()/get_expression_pointer_level(), which both
       route through resolve_struct_access(). */

    /* Every evaluate_lvalue_address() case except NODE_IDENTIFIER writes
       into packed storage (an array/struct blob, or wherever a
       dereferenced pointer points -- itself possibly one of those same
       blobs) rather than a scalar Variable's own union slot; see
       write_value_to_address()'s own comment for why that distinction
       matters. */
    bool packed_storage = target->type != NODE_IDENTIFIER;

    void *address = evaluate_lvalue_address(target);
    write_value_to_address(address, target_type, target_pointer_level,
                           value_node, mods, packed_storage);
}

void execute_statement(ASTNode *node)
{
    if (!node)
        return;
    switch (node->type)
    {
    case NODE_DECLARATION:
    {
        String name = node->data.op.left->data.name;
        Variable *var = variable_new(name);
        var->var_type = node->var_type;
        var->pointer_level = node->pointer_level;
        var->modifiers = node->modifiers;

        /* Check if it's static and already initialized */
        if (node->modifiers.is_static)
        {
            String func_name = {NULL, 0};
            Scope *s = current_scope;
            while (s)
            {
                if (s->is_function_scope)
                {
                    func_name = s->function_name;
                    break;
                }
                s = s->parent;
            }
            String static_key = make_static_key(func_name, name);
            Variable *existing =
                hm_get(static_variable_map, static_key.data, MAX_BUFFER_LEN);
            if (existing)
            {
                SAFE_FREE(var);
                break; /* Already initialized — skip assignment entirely */
            }
        }

        add_variable_to_scope(name, var);
        SAFE_FREE(var);

        if (node->data.op.right)
        {
            Variable *scope_var = get_variable(name);
            initialize_variable_from_expr(scope_var, node->data.op.right);
            break;
        }
    }
        __attribute__((fallthrough));
    case NODE_ASSIGNMENT:
    {
        execute_assignment(node);
        break;
    }
    case NODE_ARRAY_ACCESS:
        break;
    case NODE_OPERATION:
    case NODE_UNARY_OPERATION:
    case NODE_INT:
    case NODE_SHORT:
    case NODE_FLOAT:
    case NODE_DOUBLE:
    case NODE_CHAR:
    case NODE_IDENTIFIER:
        evaluate_expression(node);
        break;
    case NODE_FUNC_CALL:
    {
        // Set execution context with current line number
        g_exec_context.line_number = node->line_number;
        g_exec_context.function_name = node->data.func_call.function_name;

        // Use the stdrot built-in function system
        if (is_builtin_function(node->data.func_call.function_name))
        {
            execute_builtin_function(node->data.func_call.function_name,
                                     node->data.func_call.arguments);
        }
        else
        {
            execute_function_call(node->data.func_call.function_name,
                                  node->data.func_call.arguments);
        }
        break;
    }
    case NODE_FOR_STATEMENT:
        execute_for_statement(node);
        break;
    case NODE_WHILE_STATEMENT:
        execute_while_statement(node);
        break;
    case NODE_DO_WHILE_STATEMENT:
        execute_do_while_statement(node);
        break;
    case NODE_PRINT_STATEMENT:
    {
        ASTNode *expr = node->data.op.left;
        if (expr->type == NODE_STRING_LITERAL)
        {
            String s = {.data = "%s\n", .len = sizeof("%s\n") - 1};
            yapping(s, expr->data.name);
        }
        else
        {
            String s = {.data = "%d\n", .len = sizeof("%d\n") - 1};
            int value = evaluate_expression(expr);
            yapping(s, value);
        }
        break;
    }
    case NODE_ERROR_STATEMENT:
    {
        ASTNode *expr = node->data.op.left;
        if (expr->type == NODE_STRING_LITERAL)
        {
            String s = {.data = "%s\n", .len = sizeof("%s\n") - 1};
            baka(s, expr->data.name);
        }
        else
        {
            String s = {.data = "%d\n", .len = sizeof("%d\n") - 1};
            int value = evaluate_expression(expr);
            baka(s, value);
        }
        break;
    }
    case NODE_STATEMENT_LIST:
        execute_statements(node);
        break;
    case NODE_IF_STATEMENT:
        enter_scope();
        if (evaluate_expression(node->data.if_stmt.condition))
        {
            execute_statement(node->data.if_stmt.then_branch);
        }
        else if (node->data.if_stmt.else_branch)
        {
            execute_statement(node->data.if_stmt.else_branch);
        }
        exit_scope();
        break;
    case NODE_SWITCH_STATEMENT:
        execute_switch_statement(node);
        break;
    case NODE_BREAK_STATEMENT:
        // Signal to break out of the current loop/switch
        bruh();
        break;
    case NODE_FUNCTION_DEF:
    {
        Function *func = create_function(
            node->data.function_def.name, node->data.function_def.return_type,
            node->data.function_def.parameters, node->data.function_def.body);
        if (!func)
        {
            yyerror("Failed to create function");
            exit(1);
        }
        break;
    }
    case NODE_RETURN:
    {
        handle_return_statement(node->data.op.left);
        break;
    }
    default:
        yyerror("Unknown statement type");
        break;
    }
}

void execute_statements(ASTNode *node)
{
    if (!node)
        return;
    if (node->type != NODE_STATEMENT_LIST)
    {
        execute_statement(node);
        return;
    }
    StatementList *current = node->data.statements;
    while (current)
    {
        execute_statement(current->statement);
        current = current->next;
    }
}

void execute_for_statement(ASTNode *node)
{
    PUSH_JUMP_BUFFER();
    if (setjmp(CURRENT_JUMP_BUFFER()) == 0)
    {
        // Execute initialization once
        enter_scope();
        if (node->data.for_stmt.init)
        {
            execute_statement(node->data.for_stmt.init);
        }

        while (1)
        {
            // Evaluate condition
            enter_scope();
            if (node->data.for_stmt.cond)
            {
                int cond_result = evaluate_expression(node->data.for_stmt.cond);
                if (!cond_result)
                {
                    break;
                }
            }

            // Execute body
            if (node->data.for_stmt.body)
            {
                execute_statement(node->data.for_stmt.body);
            }

            // Execute increment
            if (node->data.for_stmt.incr)
            {
                execute_statement(node->data.for_stmt.incr);
            }
            exit_scope();
        }
        exit_scope();
    }
    POP_JUMP_BUFFER();
}

void execute_while_statement(ASTNode *node)
{
    PUSH_JUMP_BUFFER();
    enter_scope();
    while (evaluate_expression(node->data.while_stmt.cond) &&
           setjmp(CURRENT_JUMP_BUFFER()) == 0)
    {
        enter_scope();
        execute_statement(node->data.while_stmt.body);
        exit_scope();
    }
    exit_scope();
    POP_JUMP_BUFFER();
}

void execute_do_while_statement(ASTNode *node)
{
    PUSH_JUMP_BUFFER();
    enter_scope();
    do
    {
        enter_scope();
        execute_statement(node->data.while_stmt.body);
        exit_scope();
    } while (evaluate_expression(node->data.while_stmt.cond) &&
             setjmp(CURRENT_JUMP_BUFFER()) == 0);
    exit_scope();
    POP_JUMP_BUFFER();
}

ASTNode *create_if_statement_node(ASTNode *condition, ASTNode *then_branch,
                                  ASTNode *else_branch)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_IF_STATEMENT;
    node->data.if_stmt.condition = condition;
    node->data.if_stmt.then_branch = then_branch;
    node->data.if_stmt.else_branch = else_branch;
    return node;
}

ASTNode *create_string_literal_node(String string)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_STRING_LITERAL;
    node->data.name = ARENA_STRDUP(string);
    return node;
}

ASTNode *create_switch_statement_node(ASTNode *expression, CaseNode *cases)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_SWITCH_STATEMENT;
    node->data.switch_stmt.expression = expression;
    node->data.switch_stmt.cases = cases;
    return node;
}

CaseNode *create_case_node(ASTNode *value, ASTNode *statements)
{
    CaseNode *node = ARENA_ALLOC(CaseNode);
    node->value = value;
    node->statements = statements;
    node->next = NULL;
    return node;
}

CaseNode *create_default_case_node(ASTNode *statements)
{
    return create_case_node(NULL,
                            statements); // NULL value indicates default case
}

CaseNode *append_case_list(CaseNode *list, CaseNode *case_node)
{
    if (!list)
        return case_node;
    CaseNode *current = list;
    while (current->next)
        current = current->next;
    current->next = case_node;
    return list;
}

ASTNode *create_break_node()
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    node->type = NODE_BREAK_STATEMENT;
    node->data.break_stmt = NULL;
    return node;
}

void bruh()
{
    LONGJMP();
}

ASTNode *create_default_node(VarType var_type, int pointer_level)
{
    /* Round-21 review, finding #1 -- a declaration with no initializer
       to infer a pointer_level mismatch from (`declarator EQUALS
       expression` isn't in play here) needs pointer_level itself to
       decide what "no value to default to" even means: `skibidi x;`
       (VAR_VOID, pointer_level 0) is genuinely invalid -- void isn't a
       storable type -- but `skibidi *p;` (VAR_VOID, pointer_level 1) is
       `void *`, a real pointer type, and treating it identically was
       exactly the (VAR_VOID meaning both "no value" and "void*'s base
       type") confusion this round's review is about. This codebase has
       no general uninitialized-pointer-default policy for ANY base type
       today, though -- `rizz *p;` alone already fails semantic analysis
       ("Type mismatch ... expected a pointer (level 1), got pointer
       level 0"), because create_int_node(0)'s own pointer_level is 0,
       mismatching the declared one. Rather than inventing new "null
       pointer default" semantics that don't exist anywhere else in the
       language, a pointer-typed default (any base type, VAR_VOID
       included) falls through to that exact same numeric-zero node,
       so `skibidi *p;` fails the identical, already-established way
       `rizz *p;` does -- not a parse-time crash unique to void. */
    if (pointer_level > 0)
        return create_int_node(0);

    switch (var_type)
    {
    case VAR_INT:
        return create_int_node(0);
    case VAR_FLOAT:
        return create_float_node(0.0f);
    case VAR_DOUBLE:
        return create_double_node(0.0);
    case VAR_SHORT:
        return create_short_node(0);
    case VAR_CHAR:
        return create_char_node('\0');
    case VAR_BOOL:
        return create_boolean_node(0);
    case VAR_STRING:
    {
        String s = {.data = "\0", .len = sizeof("\0") - 1};
        return create_string_literal_node(s);
    }
    // VAR_ENUM's 0 (the natural enum default) and VAR_VOID's 0 (an
    // invalid-variable placeholder, see its own comment below) coincide but
    // for semantically distinct reasons -- not a merge candidate.
    case VAR_ENUM: // NOLINT(bugprone-branch-clone)
        return create_int_node(0);
    case VAR_VOID:
        /* Reached only for pointer_level == 0 now (the pointer_level > 0
           case -- `skibidi *p;` -- already returned above, alongside
           every other pointer-typed default). `skibidi x;` (no
           initializer, no pointer) is genuinely invalid -- a named
           void variable was never valid and has no meaningful default.
           Return the same placeholder initializer the pointer path uses so
           parsing can finish and the semantic declaration check can report
           the invalid variable through the normal cleanup path. */
        return create_int_node(0);
    default:
        yyerror("Unsupported type for default node");
        exit(1);
    }
}

ExpressionList *create_expression_list(ASTNode *expr)
{
    ExpressionList *list = SAFE_MALLOC(ExpressionList);
    if (!list)
    {
        yyerror("Failed to allocate memory for expression list");
        exit(1);
    }
    list->expr = expr;
    list->sublist = NULL;
    list->next = list;
    list->prev = list;
    return list;
}

ExpressionList *create_expression_sublist(ExpressionList *sub)
{
    ExpressionList *list = SAFE_MALLOC(ExpressionList);
    if (!list)
    {
        yyerror("Failed to allocate memory for expression list");
        exit(1);
    }
    list->expr = NULL;
    list->sublist = sub;
    list->next = list;
    list->prev = list;
    return list;
}

ExpressionList *append_expression_list(ExpressionList *list, ASTNode *expr)
{
    return append_expression_list_node(list, create_expression_list(expr));
}

ExpressionList *append_expression_list_node(ExpressionList *list,
                                            ExpressionList *node)
{
    if (!list)
    {
        node->next = node;
        node->prev = node;
        return node;
    }

    node->next = list;
    node->prev = list->prev;
    list->prev->next = node;
    list->prev = node;
    return list;
}

size_t count_expression_list(ExpressionList *list)
{
    if (!list)
        return 0;
    size_t count = 1;
    ExpressionList *current = list->next;
    while (current != list)
    {
        count++;
        current = current->next;
    }
    return count;
}

void free_expression_list(ExpressionList *list)
{
    if (!list)
        return;
    if (list->sublist)
        free_expression_list(list->sublist);
    ExpressionList *current = list->next;
    while (current != list)
    {
        ExpressionList *next = current->next;
        if (current->sublist)
            free_expression_list(current->sublist);
        SAFE_FREE(current);
        current = next;
    }
    SAFE_FREE(list);
}

/* Registry of ExpressionLists handed to array/struct declaration nodes via
   set_declaration_pending_initializer(). A declaration statement's AST
   node is visited once per execution (once per call, for a function-local
   declaration) but must keep reusing the same initializer values every
   time, so the list can't be freed after first use the way a one-shot
   parse-time consumer (e.g. the old struct/array initializer handling)
   used to. Instead every registered list is freed exactly once here, at
   program teardown. */
typedef struct PendingInitializerNode
{
    ExpressionList *list;
    struct PendingInitializerNode *next;
} PendingInitializerNode;

static PendingInitializerNode *pending_initializer_registry = NULL;

void set_declaration_pending_initializer(ASTNode *node, ExpressionList *list)
{
    if (!node || !list)
        return;

    node->pending_initializer = list;

    PendingInitializerNode *entry = SAFE_MALLOC(PendingInitializerNode);
    entry->list = list;
    entry->next = pending_initializer_registry;
    pending_initializer_registry = entry;
}

static void free_pending_initializer_registry(void)
{
    while (pending_initializer_registry)
    {
        PendingInitializerNode *next = pending_initializer_registry->next;
        free_expression_list(pending_initializer_registry->list);
        SAFE_FREE(pending_initializer_registry);
        pending_initializer_registry = next;
    }
}

/* Validates only the *shape* of a struct/union initializer against its
   StructDef -- a bare value vs. a `{ ... }` sub-initializer for each field
   -- without writing anything. This is the same check populate_struct_fields()
   does below, split out so it can still run at parse time (right after the
   struct_initializer_list is parsed) even though the actual value-writing
   in populate_struct_fields() no longer can: it needs a real Variable's
   data blob, which now (see interpreter_visit_declaration) is only
   allocated once the declaration statement actually executes, in whatever
   scope is current then -- not at parse time. Keeping this specific check
   at parse time preserves its existing behavior of gating main()'s
   struct_def_had_error check, so a malformed initializer is still reported
   before any interpretation starts (see the comment on that global). */
void validate_struct_initializer_shape(StructDef *def, ExpressionList *list)
{
    if (!def)
        return;

    StructField *fld = def->fields;
    ExpressionList *cur = list;
    // ExpressionList is circular (see create_expression_list()), so a
    // `cur` NULL check never terminates; bound by initializer_count
    // instead and leave unspecified trailing fields unvalidated (they
    // stay at their zero-initialized default -- see populate_struct_fields()).
    size_t initializer_count = count_expression_list(list);
    size_t index = 0;
    while (fld && index < initializer_count)
    {
        bool is_nested_aggregate =
            (fld->type == VAR_STRUCT && fld->pointer_level == 0);

        if (is_nested_aggregate != (cur->sublist != NULL))
        {
            char msg[MAX_BUFFER_LEN];
            if (is_nested_aggregate)
                snprintf(msg, sizeof(msg),
                         "Field '%s' is a nested struct/union and needs a "
                         "braced sub-initializer (e.g. '{ ... }'), not a "
                         "plain value",
                         fld->name.data ? fld->name.data : "?");
            else
                snprintf(msg, sizeof(msg),
                         "Field '%s' is not a nested struct/union and can't "
                         "be initialized with a braced sub-initializer",
                         fld->name.data ? fld->name.data : "?");
            yyerror(msg);
            struct_def_had_error = true;
        }
        else if (is_nested_aggregate)
        {
            validate_struct_initializer_shape(get_struct_def(fld->struct_name),
                                              cur->sublist);
        }

        if (def->is_union)
            break;
        fld = fld->next;
        cur = cur->next;
        index++;
    }
}

/* Shared by populate_struct_variable and, recursively, by itself for
   nested struct/union fields initialized with a braced sub-list
   (e.g. `Outer o = { {1, 2}, 3 };`). Shape mismatches are no longer
   reported here -- validate_struct_initializer_shape() already gated this
   from ever running on a malformed initializer (see its comment) -- but
   the check is kept as a defensive no-write skip in case that ever
   changes. */
static void populate_struct_fields(StructDef *def, void *base,
                                   ExpressionList *list)
{
    if (!def || !base)
        return;

    /* Union fields overlap at offset 0: only the first member is
       initialized, mirroring C's brace-init rule for unions. */
    StructField *fld = def->fields;
    ExpressionList *cur = list;
    // ExpressionList is circular (see create_expression_list()), so a
    // `cur` NULL check never terminates; bound by initializer_count
    // instead and leave every field beyond it at its calloc()'d zero
    // default, the same fix applied to populate_multi_array_variable()
    // for issue #226.
    size_t initializer_count = count_expression_list(list);
    size_t index = 0;
    while (fld && index < initializer_count)
    {
        void *addr = (char *)base + fld->offset;
        bool is_nested_aggregate =
            (fld->type == VAR_STRUCT && fld->pointer_level == 0);

        /* A shape mismatch here (braced sub-initializer for a scalar
           field, or a bare value for a nested struct/union field) would
           otherwise be silently misapplied (evaluate_expression_*(NULL)
           for a missing expr, or the sublist just being dropped) --
           already reported by validate_struct_initializer_shape() at
           parse time, which halts main() before this ever runs, but
           skipped defensively here too rather than assumed unreachable. */
        if (is_nested_aggregate != (cur->sublist != NULL))
        {
            /* unreachable in practice; see comment above */
        }
        else if (is_nested_aggregate)
        {
            populate_struct_fields(get_struct_def(fld->struct_name), addr,
                                   cur->sublist);
        }
        else if (fld->pointer_level > 0)
        {
            *(uintptr_t *)addr = evaluate_expression_pointer(cur->expr);
        }
        else
        {
            switch (fld->type)
            {
            case VAR_INT:
            case VAR_ENUM:
                *(int *)addr = evaluate_expression_int(cur->expr);
                break;
            case VAR_SHORT:
                *(short *)addr = evaluate_expression_short(cur->expr);
                break;
            case VAR_FLOAT:
                *(float *)addr = evaluate_expression_float(cur->expr);
                break;
            case VAR_DOUBLE:
                *(double *)addr = evaluate_expression_double(cur->expr);
                break;
            case VAR_BOOL:
                *(bool *)addr = evaluate_expression_bool(cur->expr);
                break;
            case VAR_CHAR:
                *(char *)addr = (char)evaluate_expression_int(cur->expr);
                break;
            default:
                break;
            }
        }
        if (def->is_union)
            break;
        fld = fld->next;
        cur = cur->next;
        index++;
    }
}

void populate_struct_variable(const String name, ExpressionList *list)
{
    Variable *var = get_variable(name);
    if (!var || var->var_type != VAR_STRUCT)
        return;
    StructDef *def = get_struct_def(var->struct_name);
    if (!def)
        return;
    populate_struct_fields(def, var->value.array_data, list);
}

void populate_multi_array_variable(String name, ExpressionList *list,
                                   const int dimensions[], int num_dimensions)
{
    Variable *var = get_variable(name);
    if (var == NULL || !var->is_array)
    {
        yyerror("Cannot initialize: not an array");
        return;
    }

    // Calculate total elements
    size_t total_elements = 1;
    for (int i = 0; i < num_dimensions; i++)
    {
        total_elements *= dimensions[i];
    }

    // Check if we have enough initializers
    size_t initializer_count = count_expression_list(list);
    if (initializer_count > total_elements)
    {
        yyerror("Too many initializers for array");
        return;
    }

    // Initialize the array elements
    // NOTE: ExpressionList is circular (see create_expression_list()), so
    // `current` never becomes NULL. Bound the loop by initializer_count
    // (already validated <= total_elements above) and leave every
    // remaining element at its zero-initialized default.
    size_t index = 0;
    ExpressionList *current = list;

    while (index < initializer_count)
    {
        /* Round-24 review, finding #2 -- pointer_level dominates element
           representation here too, the same rule round 23 already
           applied to reading an array element back out
           (evaluate_multi_array_access()) and to allocating the array's
           storage in the first place (get_type_size_for_descriptor(),
           consulted by set_multi_array_variable()): a pointer-typed
           array (`rizz *ptrs[N] = { &x, &y };`) is allocated at
           sizeof(uintptr_t) per element, but this switch dispatched
           purely on var->var_type, writing through an (int *)/(float *)/
           etc. cast at the WRONG stride for a pointer array, and
           evaluating each initializer expression (`&x`) with
           evaluate_expression_int() instead of evaluate_expression_
           pointer() -- reading a raw address through the wrong
           evaluator entirely, not just at the wrong offset. Checked
           before the base-type switch, dominant over it, for the
           identical reason every other "pointer-ness dominates
           representation" fix in this PR checks it first. */
        if (var->pointer_level > 0)
        {
            uintptr_t *array = (uintptr_t *)var->value.array_data;
            array[index] = evaluate_expression_pointer(current->expr);
            current = current->next;
            index++;
            continue;
        }

        switch (var->var_type)
        {
        case VAR_INT:
        {
            int *array = (int *)var->value.array_data;
            array[index] = evaluate_expression_int(current->expr);
            break;
        }
        case VAR_SHORT:
        {
            short *array = (short *)var->value.array_data;
            array[index] = evaluate_expression_short(current->expr);
            break;
        }
        case VAR_FLOAT:
        {
            float *array = (float *)var->value.array_data;
            array[index] = evaluate_expression_float(current->expr);
            break;
        }
        case VAR_DOUBLE:
        {
            double *array = (double *)var->value.array_data;
            array[index] = evaluate_expression_double(current->expr);
            break;
        }
        case VAR_BOOL:
        {
            bool *array = (bool *)var->value.array_data;
            array[index] = evaluate_expression_bool(current->expr);
            break;
        }
        default:
            yyerror("Unsupported array type for initialization");
            return;
        }

        current = current->next;
        index++;
    }
}
void free_statement_list(StatementList *list)
{
    while (list)
    {
        StatementList *next = list->next;

        // Free the current list node
        if (list)
            SAFE_FREE(list);

        // Move to the next node
        list = next;
    }
}

void free_ast()
{
    free_pending_initializer_registry();
    arena_free(&arena);
}

Scope *create_scope(Scope *parent)
{
    Scope *scope = SAFE_MALLOC(Scope);
    if (!scope)
    {
        yyerror("Failed to allocate memory for scope");
        SAFE_FREE(scope);
        exit(1);
    }
    scope->variables = hm_new();
    scope->parent = parent;
    return scope;
}

Variable *get_variable(const String name)
{
    /* Check static store first */
    if (static_variable_map)
    {
        String func_name = {NULL, 0};
        Scope *s = current_scope;
        while (s)
        {
            if (s->is_function_scope)
            {
                func_name = s->function_name;
                break;
            }
            s = s->parent;
        }
        String static_key = make_static_key(func_name, name);
        Variable *var =
            hm_get(static_variable_map, static_key.data, static_key.len);
        if (var)
        {
            return var;
        }
    }

    Scope *scope = current_scope;
    while (scope)
    {
        Variable *var = hm_get(scope->variables, name.data, name.len);
        if (var)
        {
            return var;
        }
        if (scope->is_function_scope)
        {
            return NULL;
        }
        scope = scope->parent;
    }
    return NULL;
}

void exit_scope()
{
    if (!current_scope)
    {
        yyerror("No scope to exit");
        exit(1);
    }
    Scope *parent = current_scope->parent;
    hm_free(current_scope->variables);
    SAFE_FREE(current_scope);
    current_scope = parent;
}

void free_scope(Scope *scope)
{
    if (!scope)
        return;
    hm_free(scope->variables);
    free_scope(scope->parent);
    SAFE_FREE(scope);
}
void enter_scope()
{
    current_scope = create_scope(current_scope);
}
Variable *variable_new(String name)
{
    Variable *var = SAFE_MALLOC(Variable);
    if (!var)
    {
        yyerror("Failed to allocate memory for variable");
        exit(1);
    }
    memset(var, 0, sizeof(Variable));
    var->name = name;
    var->is_array = false;
    return var;
}

void add_variable_to_scope(const String name, Variable *var)
{
    if (!current_scope)
    {
        yyerror("No scope to add variable to");
        exit(1);
    }

    /* Static variables go to the static store, not the scope */
    if (var->modifiers.is_static)
    {
        if (!static_variable_map)
            static_variable_map = hm_new();

        /* Find nearest function scope to namespace the key */
        String func_name = {NULL, 0};
        Scope *s = current_scope;
        while (s)
        {
            if (s->is_function_scope)
            {
                func_name = s->function_name;
                break;
            }
            s = s->parent;
        }

        String static_key = make_static_key(func_name, name);
        Variable *existing =
            hm_get(static_variable_map, static_key.data, static_key.len);
        if (!existing)
            hm_put(static_variable_map, static_key.data, static_key.len, var,
                   sizeof(Variable));

        return; /* <-- always return here, never fall through to normal scope */
    }

    /* Normal (non-static) path — unchanged from your original */
    size_t name_len = name.len;
    Variable *existing = hm_get(current_scope->variables, name.data, name_len);
    if (existing)
    {
        yyerror("Variable already exists in current scope");
        SAFE_FREE(var);
        exit(1);
    }

    hm_put(current_scope->variables, name.data, name_len, var,
           sizeof(Variable));
}

ASTNode *create_return_node(ASTNode *expr)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Memory allocation failed");
        return NULL;
    }
    node->type = NODE_RETURN;
    node->data.op.left = expr; // Store return expression in left operand
    return node;
}

Function *create_function_ex(String name, VarType return_type,
                             int return_pointer_level, Parameter *params,
                             ASTNode *body)
{
    /* Check if function already exists - if so, just return it (parse + execute
     * causes double creation) */
    Function *existing = get_function(name);
    if (existing)
    {
        return existing;
    }

    Function *func = SAFE_MALLOC(Function);
    if (!func)
    {
        yyerror("Failed to allocate memory for function");
        return NULL;
    }

    func->name = safe_strdup(&name);
    func->return_type = return_type;
    func->return_pointer_level = return_pointer_level;
    func->parameters = params;
    func->body = body;

    /* Initialize hash map if needed and add function for O(1) lookups */
    if (!function_map)
    {
        function_map = hm_new();
    }
    size_t name_len = name.len;
    hm_put(function_map, name.data, name_len, &func, sizeof(Function *));

    return func;
}

Function *create_function(String name, VarType return_type, Parameter *params,
                          ASTNode *body)
{
    return create_function_ex(name, return_type, 0, params, body);
}

void execute_function_call(const String name, ArgumentList *args)
{
    /* Use optimized O(1) hash map lookup instead of O(n) linked list search */
    Function *func = get_function(name);

    if (!func)
    {
        yyerror("Undefined function");
        return;
    }

    /* A previous call's struct return value (see handle_return_statement's
       VAR_STRUCT case) may still be sitting unconsumed in the global
       current_return_value slot -- e.g. it was returned from a call used
       as a bare statement, which has nowhere to put it. Free it now,
       before this call overwrites the slot. */
    free_pending_return_value();

    current_return_value.type = func->return_type;
    current_return_value.pointer_level = func->return_pointer_level;
    current_return_value.has_value = false;

    if (!enter_function_scope(func, args))
    {
        /* Argument binding failed (already reported via yyerror) -- no
           scope was left behind, and there's no valid parameter state to
           run the body against, so don't. */
        return;
    }

    PUSH_JUMP_BUFFER();
    if (setjmp(CURRENT_JUMP_BUFFER()) == 0)
    {
        /* Use visitor pattern instead of old AST execution for function bodies
         */
        extern Interpreter *current_interpreter;
        if (current_interpreter)
        {
            ast_accept(func->body, (Visitor *)current_interpreter);
        }
        else
        {
            /* Fallback to old system if no current interpreter */
            execute_statement(func->body);
        }

        // If we reach here without an explicit return, clean up function scope
        if (current_scope && current_scope->is_function_scope)
        {
            exit_scope(); // exit function scope
        }
    }
    POP_JUMP_BUFFER();
}

/* Frees whichever owned heap allocation, if any, is still sitting
 * unconsumed in the global current_return_value slot -- a VAR_STRUCT
 * blob or a VAR_STRING owns_strvalue buffer, both left behind by a call
 * whose result nothing ever read (e.g. ast_accept()'s generic pre-visit
 * of a call embedded in a declaration/assignment/etc. -- see
 * interpreter_visit_function_call()'s own comment for why a user-defined
 * function has no memo cache the way a native call does, and is
 * genuinely invoked twice as a result). Round 24 renamed this from
 * free_pending_struct_return_value() -- it stopped being struct-only the
 * moment round 23's finding #3 gave handle_return_statement() a
 * VAR_STRING case, and a name that only names half its job invites the
 * next VAR_STRING-shaped bug to be added right next to a cleanup
 * function that doesn't mention strings at all. Called at every point
 * this slot is about to be overwritten by a new call's result, so a
 * stale owned value never survives past the moment nothing can reach it
 * anymore. */
void free_pending_return_value(void)
{
    if (current_return_value.type == VAR_STRUCT &&
        current_return_value.value.pvalue)
    {
        free((void *)current_return_value.value.pvalue);
        current_return_value.value.pvalue = 0;
    }
    if (current_return_value.struct_name.data)
    {
        SAFE_FREE(current_return_value.struct_name);
        current_return_value.struct_name = (String){0};
    }
    if (current_return_value.type == VAR_STRING &&
        current_return_value.owns_strvalue &&
        current_return_value.value.strvalue.data)
    {
        SAFE_FREE(current_return_value.value.strvalue.data);
        current_return_value.owns_strvalue = false;
    }
}

void handle_return_statement(ASTNode *expr)
{
    /* current_return_value is a single global slot, overwritten by every
       function call (see execute_function_call) -- including any call
       made from within this function's own body before control reaches
       this return statement. Re-derive type/pointer_level fresh from
       whichever function is actually executing right now, rather than
       trusting whatever a nested call left behind: without this, e.g.
       `bussin 0;` in skibidi main right after calling a struct-returning
       function would find current_return_value.type still set to
       VAR_STRUCT from that call.

       Round-24 review, finding #1 -- that reasoning has to apply to the
       REST of this function too, not just this initial re-derivation:
       `declared_type`/`declared_pointer_level`, not current_return_
       value.type/.pointer_level, drive every decision below (which
       evaluator to call, which union member to write) from here on, and
       nothing writes INTO current_return_value.{type,pointer_level} until
       AFTER the expression has been fully evaluated into a local. The
       expression (`evaluate_expression_double(expr)` etc., or a nested
       user-defined call reached through it) can itself invoke another
       function -- native or Brainrot-defined -- which overwrites this
       exact global slot with ITS OWN type/pointer_level for the
       duration of that nested call. The previous version set current_
       return_value.type/.pointer_level to this function's OWN declared
       return type up front, then evaluated the expression, then wrote
       only current_return_value.value.* afterward -- so a nested call
       partway through evaluation left current_return_value.type
       permanently stuck on the NESTED call's type (e.g. VAR_INT from an
       inner native) while current_return_value.value held the OUTER
       function's correctly-converted payload (e.g. a double) -- a tag/
       payload mismatch handle_function_call() would then read through
       the wrong union member entirely. Capturing the declared contract
       in locals, evaluating into a local, and only then installing both
       together closes the exact class of bug NativeResult (stdrot.h)
       already closed at the native-ABI boundary -- payload and metadata
       now travel together here too, immune to whatever a nested call
       does to the shared global slot in between. */
    Scope *scope = current_scope;
    while (scope && !scope->is_function_scope)
        scope = scope->parent;
    Function *current_func = NULL;
    VarType declared_type = NONE;
    int declared_pointer_level = 0;
    if (scope)
    {
        current_func = get_function(scope->function_name);
        if (current_func)
        {
            declared_type = current_func->return_type;
            declared_pointer_level = current_func->return_pointer_level;
        }
    }
    /* Not inside any function (declared_type/declared_pointer_level stay
       NONE/0) -- this is skibidi main, which has no declared return
       type. */

    if (!expr)
    {
        current_return_value.type = declared_type;
        current_return_value.pointer_level = declared_pointer_level;
        current_return_value.has_value = true;
    }
    else if (declared_pointer_level > 0)
    {
        uintptr_t pointer_result = evaluate_expression_pointer(expr);
        current_return_value.type = declared_type;
        current_return_value.pointer_level = declared_pointer_level;
        current_return_value.has_value = true;
        current_return_value.value.pvalue = pointer_result;
    }
    else
    {
        switch (declared_type)
        {
        case VAR_INT:
        {
            int result = evaluate_expression_int(expr);
            current_return_value.type = declared_type;
            current_return_value.pointer_level = 0;
            current_return_value.has_value = true;
            current_return_value.value.ivalue = result;
            break;
        }
        case VAR_FLOAT:
        {
            float result = evaluate_expression_float(expr);
            current_return_value.type = declared_type;
            current_return_value.pointer_level = 0;
            current_return_value.has_value = true;
            current_return_value.value.fvalue = result;
            break;
        }
        case VAR_DOUBLE:
        {
            double result = evaluate_expression_double(expr);
            current_return_value.type = declared_type;
            current_return_value.pointer_level = 0;
            current_return_value.has_value = true;
            current_return_value.value.dvalue = result;
            break;
        }
        case VAR_BOOL:
        {
            bool result = evaluate_expression_bool(expr);
            current_return_value.type = declared_type;
            current_return_value.pointer_level = 0;
            current_return_value.has_value = true;
            current_return_value.value.bvalue = result;
            break;
        }
        case VAR_SHORT:
        {
            short result = evaluate_expression_short(expr);
            current_return_value.type = declared_type;
            current_return_value.pointer_level = 0;
            current_return_value.has_value = true;
            current_return_value.value.svalue = result;
            break;
        }
        case VAR_ENUM:
        {
            int result = evaluate_expression_int(expr);
            current_return_value.type = declared_type;
            current_return_value.pointer_level = 0;
            current_return_value.has_value = true;
            current_return_value.value.ivalue = result;
            break;
        }
        case VAR_CHAR:
        {
            /* Round-23 review, finding #3 -- the return-CHECKING side
               (semantic_analyze_with_scope_tracking()'s NODE_RETURN
               case, round 22) already approved a `yap f() { bussin
               'A'; }`-shaped function as statically coherent (VAR_CHAR
               == VAR_CHAR), but this return-PRODUCING switch had no
               VAR_CHAR case at all, so actually calling such a
               function hit `default: yyerror("Unsupported return
               type"); exit(1);` -- the checker approved a contract the
               producer couldn't fulfill. current_return_value.value.
               ivalue (not a dedicated char field) matches handle_
               function_call()'s own VAR_CHAR consumer case, which
               already reads a char return from that exact union
               member. */
            int result = evaluate_expression_int(expr);
            current_return_value.type = declared_type;
            current_return_value.pointer_level = 0;
            current_return_value.has_value = true;
            current_return_value.value.ivalue = result;
            break;
        }
        case VAR_STRING:
        {
            /* Same producer gap as VAR_CHAR just above, for `rant f()
               { bussin "hello"; }` -- including a native's own
               identity-polymorphic STRING result reaching here via
               `bussin slorp(s);` (the review's own exhibit), since
               nothing about THIS switch cares how the expression's
               value was produced, only what VarType it's declared to
               return. evaluate_expression_string() always hands back
               an independently safe_strdup'd copy (see its own
               comment) -- current_return_value.owns_strvalue = true
               documents that this copy is this return's own, to be
               consumed exactly the way handle_function_call()'s
               VAR_STRING case already consumes a native's owned string
               result: copy into the caller's box, then free this one.
               Not freed here -- ownership travels via owns_strvalue
               precisely so a caller that discards the return value
               entirely (a bare `f();` statement, no assignment) still
               has somewhere for this cleanup to happen (handle_
               function_call()'s own VAR_STRING case runs regardless of
               whether its result is ever used). Evaluated into a local
               BEFORE any of current_return_value's fields are touched,
               same as every other case here, so a nested call reached
               while evaluating this string can't leave owns_strvalue
               or type stuck on ITS OWN (unrelated) return afterward. */
            String result = evaluate_expression_string(expr);
            current_return_value.type = declared_type;
            current_return_value.pointer_level = 0;
            current_return_value.has_value = true;
            current_return_value.value.strvalue = result;
            current_return_value.owns_strvalue = true;
            break;
        }
        case VAR_VOID:
            /* A real `skibidi`-declared function's return type is
               VAR_VOID now (round 20), not NONE -- NONE here is
               reached only for `bussin` outside any function (`main`
               itself has no declared return type, see declared_type's
               own NONE default above). Both mean the same thing for
               this switch's purposes: ignore the expression's value
               entirely -- it is never evaluated at all (matching the
               original behavior this case has always had; `bussin
               someExpr;` inside a void function has never executed
               someExpr for its side effects, and introducing that now
               would be a new, unrelated behavior change, not a
               reentrancy fix). No nested-call reentrancy risk here for
               exactly that reason: nothing runs evaluate_expression_*
               on expr in this case, so current_return_value can't be
               clobbered by anything expr might otherwise have called.
               (Reaching this case at all, rather than the declared_
               pointer_level > 0 branch above, means declared_pointer_
               level == 0 -- genuinely void, not `skibidi *`.) */
        case NONE:
            current_return_value.type = declared_type;
            current_return_value.pointer_level = 0;
            current_return_value.has_value = true;
            break;
        case VAR_STRUCT:
        {
            current_return_value.type = declared_type;
            current_return_value.pointer_level = 0;
            current_return_value.has_value = true;
            /* Only a plain struct variable is supported as the return
               expression (matching the same constraint on struct
               arguments -- see enter_function_scope). The blob is
               copied into a fresh, heap-owned allocation *before* the
               scope cleanup below runs: that cleanup frees this
               function's own scope, which is where the source
               variable's blob lives, so evaluating lazily (e.g. from
               the caller, after this function returns) would read
               freed memory. The caller is responsible for copying out
               of current_return_value.value.pvalue and freeing it --
               see interpreter_visit_declaration's struct_init_expr
               handling. */
            Variable *src = NULL;
            if (expr->type == NODE_IDENTIFIER)
                src = get_variable(expr->data.name);
            if (!src || src->var_type != VAR_STRUCT)
            {
                yyerror("Return expression is not a struct variable");
                /* value.pvalue may hold a stale bit pattern left over
                   from a previous, differently-typed return sharing
                   this union -- has_value=false is what tells the
                   caller (interpreter_visit_declaration's
                   struct_init_expr handling) there's nothing usable
                   to read out of it. */
                current_return_value.has_value = false;
                break;
            }
            /* Catch a type mismatch here, at the return statement,
               rather than leaving it to be caught later by the
               caller's own destination-type check (still correct, but
               the error would point at the call site instead of this
               return). */
            if (current_func && current_func->return_struct_name.data &&
                (!src->struct_name.data ||
                 strcmp(src->struct_name.data,
                        current_func->return_struct_name.data) != 0))
            {
                yyerror("Return expression type does not match declared "
                        "return type");
                current_return_value.has_value = false;
                break;
            }
            StructDef *def = get_struct_def(src->struct_name);
            if (def)
            {
                void *blob = calloc(1, def->total_size);
                if (blob && src->value.array_data)
                    memcpy(blob, src->value.array_data, def->total_size);
                current_return_value.value.pvalue = (uintptr_t)blob;
                current_return_value.struct_name =
                    safe_strdup(&src->struct_name);
            }
            break;
        }
        default:
            yyerror("Unsupported return type");
            exit(1);
        }
    }
    // Clean up all scopes until we reach the function scope
    while (current_scope && !current_scope->is_function_scope)
    {
        exit_scope();
    }

    // skibidi main function do not have jump buffer
    if (jump_buffer)
    {
        exit_scope(); // exit current function scope
        LONGJMP();
    }
}

Parameter *create_parameter_ex(String name, VarType type, int pointer_level,
                               Parameter *next, TypeModifiers mods)
{
    Parameter *param = ARENA_ALLOC(Parameter);
    if (!param)
    {
        yyerror("Failed to allocate memory for parameter");
        return NULL;
    }

    param->name = ARENA_STRDUP(name);
    param->type = type;
    param->struct_name = (String){0};
    param->enum_name = (String){0};
    param->pointer_level = pointer_level;
    param->next = next;
    param->modifiers = mods;
    /* Arena memory is malloc'd, not calloc'd -- explicit zeroing, not a
       no-op. Callers that build an array-typed struct field (lang.y's
       `struct_field: type declarator dimensions SEMICOLON`) overwrite
       both fields immediately after this call returns. */
    param->is_array = false;
    param->array_dimensions = (ArrayDimensions){0};

    return param;
}

Parameter *create_parameter(String name, VarType type, Parameter *next,
                            TypeModifiers mods)
{
    return create_parameter_ex(name, type, 0, next, mods);
}

ASTNode *create_function_def_node_ex(String name, VarType return_type,
                                     int return_pointer_level,
                                     Parameter *params, ASTNode *body)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Failed to allocate memory for function definition node");
        return NULL;
    }

    node->type = NODE_FUNCTION_DEF;
    node->data.function_def.name = ARENA_STRDUP(name);
    node->data.function_def.return_type = return_type;
    node->pointer_level = return_pointer_level;
    node->data.function_def.parameters = params;
    node->data.function_def.body = body;

    // Add function to global function table
    create_function_ex(name, return_type, return_pointer_level, params, body);

    return node;
}

ASTNode *create_function_def_node(String name, VarType return_type,
                                  Parameter *params, ASTNode *body)
{
    return create_function_def_node_ex(name, return_type, 0, params, body);
}

ASTNode *create_function_def_node_struct(String name, String struct_name,
                                         int pointer_level, Parameter *params,
                                         ASTNode *body)
{
    ASTNode *node = ARENA_ALLOC_ASTNODE();
    if (!node)
    {
        yyerror("Failed to allocate memory for function definition node");
        return NULL;
    }

    node->type = NODE_FUNCTION_DEF;
    node->data.function_def.name = ARENA_STRDUP(name);
    node->data.function_def.return_type = VAR_STRUCT;
    node->data.function_def.return_struct_name = ARENA_STRDUP(struct_name);
    node->pointer_level = pointer_level;
    node->data.function_def.parameters = params;
    node->data.function_def.body = body;

    if (pointer_level > 0)
    {
        /* By-value struct returns are copied (see handle_return_statement's
           VAR_STRUCT case); a pointer-to-struct return would need its own,
           not-yet-implemented handling (interpreter_visit_declaration
           always allocates a fresh blob for a struct-typed variable,
           which is wrong for one that should just hold an address).
           Refuse to register the function rather than silently drop the
           `*` and treat this as by-value -- get_function() returning NULL
           at any call site gives a clear "Undefined function" instead of
           a confusing runtime type error. */
        yyerror("Struct pointer return types are not yet supported");
        return node;
    }

    Function *func = create_function_ex(name, VAR_STRUCT, 0, params, body);
    if (func)
        func->return_struct_name = ARENA_STRDUP(struct_name);

    return node;
}

ASTNode *create_function_def_node_enum(String name, String enum_name,
                                       int pointer_level, Parameter *params,
                                       ASTNode *body)
{
    /* An enum return is a plain int at runtime, so unlike the struct
       variant above, no pointer_level restriction is needed here. */
    ASTNode *node = create_function_def_node_ex(name, VAR_ENUM, pointer_level,
                                                params, body);
    if (node)
        node->data.function_def.return_enum_name = ARENA_STRDUP(enum_name);

    Function *func = get_function(name);
    if (func)
        func->return_enum_name = ARENA_STRDUP(enum_name);

    return node;
}

void free_static_variable_map(void)
{
    if (static_variable_map)
    {
        hm_free(static_variable_map);
        static_variable_map = NULL;
    }
}

void free_function_table(void)
{
    if (!function_map)
    {
        return;
    }

    /* Iterate through hash map and free all functions */
    for (size_t i = 0; i < function_map->capacity; i++)
    {
        if (function_map->nodes[i])
        {
            Function **func_ptr = (Function **)function_map->nodes[i]->value;
            if (func_ptr && *func_ptr)
            {
                Function *f = *func_ptr;

                // Free function name (it's a separate safe_strdup from the
                // AST's name)
                SAFE_FREE(f->name);

                // DO NOT free f->parameters or f->body here,
                // because those pointers belong to the AST and
                // are already freed in free_ast(root).

                SAFE_FREE(f);
            }
        }
    }

    /* Free the hash map using shallow free */
    hm_free_shallow(function_map);
    function_map = NULL;
}

void reverse_parameter_list(Parameter **head)
{
    Parameter *prev = NULL, *current = *head, *next = NULL;
    while (current)
    {
        next = current->next;
        current->next = prev;
        prev = current;
        current = next;
    }
    *head = prev;
}

bool enter_function_scope(Function *func, ArgumentList *args)
{
    ArgumentList *curr_arg = args;
    Value arg_values[MAX_ARGUMENTS];
    int arg_count = 0;

    // Reverse the parameter list
    reverse_parameter_list(&func->parameters);
    Parameter *curr_param = func->parameters;

    // Evaluate argument values before creating the scope
    while (curr_arg && curr_param)
    {
        arg_values[arg_count].pointer_level = curr_param->pointer_level;
        if (curr_param->pointer_level > 0)
        {
            arg_values[arg_count].pvalue =
                evaluate_expression_pointer(curr_arg->expr);
            curr_arg = curr_arg->next;
            curr_param = curr_param->next;
            arg_count++;
            continue;
        }
        switch (curr_param->type)
        {
        case VAR_INT:
        case VAR_CHAR:
        case VAR_ENUM:
            arg_values[arg_count].ivalue =
                evaluate_expression_int(curr_arg->expr);
            break;
        case VAR_FLOAT:
            arg_values[arg_count].fvalue =
                evaluate_expression_float(curr_arg->expr);
            break;
        case VAR_DOUBLE:
            arg_values[arg_count].dvalue =
                evaluate_expression_double(curr_arg->expr);
            break;
        case VAR_BOOL:
            arg_values[arg_count].bvalue =
                evaluate_expression_bool(curr_arg->expr);
            break;
        case VAR_SHORT:
            arg_values[arg_count].svalue =
                evaluate_expression_short(curr_arg->expr);
            break;
        case VAR_STRING:
            yyerror("String parameters are not supported");
            reverse_parameter_list(&func->parameters);
            return false;
        case VAR_STRUCT:
        {
            /* By-value struct argument: only a plain struct variable is
               supported as the source (matching what's actually needed --
               a struct-valued sub-expression, e.g. a function call
               returning a struct, isn't a thing here yet). Stash the
               source blob pointer now, while still in the caller's scope
               -- evaluating it after create_scope() below would resolve
               against the callee's (still-empty) scope instead. */
            if (curr_arg->expr->type != NODE_IDENTIFIER)
            {
                yyerror("Struct argument must be a plain struct variable");
                reverse_parameter_list(&func->parameters);
                return false;
            }
            Variable *src = get_variable(curr_arg->expr->data.name);
            if (!src || src->var_type != VAR_STRUCT)
            {
                yyerror("Struct argument is not a struct variable");
                reverse_parameter_list(&func->parameters);
                return false;
            }
            if (!src->struct_name.data || !curr_param->struct_name.data ||
                strcmp(src->struct_name.data, curr_param->struct_name.data) !=
                    0)
            {
                yyerror("Struct argument type does not match parameter type");
                reverse_parameter_list(&func->parameters);
                return false;
            }
            arg_values[arg_count].pvalue = (uintptr_t)src->value.array_data;
            break;
        }
        case VAR_PTR:
        /* curr_param->type is a user-defined function parameter's
           declared type -- no Brainrot syntax can declare a parameter
           VAR_PTR (or VAR_VOID, same reasoning), so this is structurally
           unreachable. */
        case VAR_VOID:
        case NONE:
            break;
        }

        curr_arg = curr_arg->next;
        curr_param = curr_param->next;
        arg_count++;
    }

    if (curr_arg || curr_param)
    {
        yyerror("Mismatched number of arguments and parameters");
        reverse_parameter_list(&func->parameters);
        return false;
    }

    // Create function scope after evaluating arguments
    Scope *scope = create_scope(current_scope);
    current_scope = scope;
    current_scope->is_function_scope = true;
    current_scope->function_name = func->name;
    curr_param = func->parameters; // Reset parameter list after reversing

    // Assign evaluated values to function parameters
    for (int i = 0; i < arg_count; i++)
    {
        Variable *var = variable_new(curr_param->name);
        var->var_type = curr_param->type;
        var->pointer_level = curr_param->pointer_level;
        TypeModifiers mods = curr_param->modifiers;
        add_variable_to_scope(curr_param->name, var);
        SAFE_FREE(var);

        if (curr_param->pointer_level > 0)
        {
            Variable *bound = get_variable(curr_param->name);
            if (bound)
            {
                bound->pointer_level = curr_param->pointer_level;
                /* A pointer-to-struct/union parameter (`gang Foo *pp`)
                   needs its tag copied too, same as the by-value VAR_STRUCT
                   case below -- resolve_struct_access()'s NODE_IDENTIFIER
                   branch (ast.c) resolves `pp.field` via `get_struct_def(
                   var->struct_name)` regardless of pointer_level, and this
                   loop's own var_type assignment above already treats the
                   parameter as a struct/union variable. Without this, a
                   pointer-to-struct parameter's struct_name stayed empty
                   and `pp.field` inside the callee died on "Unknown struct
                   or union type" (PR #248 review, finding 3). */
                if (curr_param->type == VAR_STRUCT)
                    bound->struct_name = safe_strdup(&curr_param->struct_name);
                bound->value.pvalue = arg_values[i].pvalue;
            }
            curr_param = curr_param->next;
            continue;
        }

        switch (curr_param->type)
        {
        case VAR_INT:
            set_int_variable(curr_param->name, arg_values[i].ivalue, mods);
            break;
        case VAR_CHAR:
            /* Not set_int_variable(): that calls set_variable(...,
               VAR_INT, ...), which overwrites this parameter's
               var_type to VAR_INT (clobbering the VAR_CHAR set moments
               ago, above) and stores the raw, unmasked argument value.
               A `yap` parameter passed an out-of-byte-range argument
               (nothing here rejects that) would then keep both a wrong
               var_type and non-zero upper bytes in its ivalue slot --
               exactly the stale-high-bytes hazard write_value_to_
               address()'s own comment describes, now reachable through
               a plain function call instead of a pointer alias
               (confirmed: `skibidi foo(yap c) { yap *p = &c; *p = 'x';
               yapping("%d", c + 0); } ... foo(1000);` printed 888, not
               120). set_char_variable() calls set_variable(..., VAR_
               CHAR, ...), whose own VAR_CHAR case already does the
               same `(unsigned char)` zero-extension write_value_to_
               address() does, and correctly leaves var_type as
               VAR_CHAR. */
            set_char_variable(curr_param->name, arg_values[i].ivalue, mods);
            break;
        case VAR_FLOAT:
            set_float_variable(curr_param->name, arg_values[i].fvalue, mods);
            break;
        case VAR_DOUBLE:
            set_double_variable(curr_param->name, arg_values[i].dvalue, mods);
            break;
        case VAR_BOOL:
            set_bool_variable(curr_param->name, arg_values[i].bvalue, mods);
            break;
        case VAR_SHORT:
            set_short_variable(curr_param->name, arg_values[i].svalue, mods);
            break;
        case VAR_STRING:
            yyerror("String parameters are not supported");
            exit_scope();
            reverse_parameter_list(&func->parameters);
            return false;
        case VAR_STRUCT:
        {
            /* Deep-copy: give the parameter its own blob (C struct-by-value
               semantics) rather than aliasing the caller's, using the
               source blob pointer captured above before this scope
               existed. Type already validated in the argument-evaluation
               pass above, so def's size matches the source blob's. */
            Variable *bound = get_variable(curr_param->name);
            StructDef *def = get_struct_def(curr_param->struct_name);
            if (bound && def)
            {
                bound->struct_name = safe_strdup(&curr_param->struct_name);
                bound->value.array_data = calloc(1, def->total_size);
                void *src_blob = (void *)arg_values[i].pvalue;
                if (bound->value.array_data && src_blob)
                    memcpy(bound->value.array_data, src_blob, def->total_size);
            }
            break;
        }
        case VAR_ENUM:
        {
            /* Not routed through set_int_variable() (unlike VAR_INT/
               VAR_CHAR above) because that would force var_type back to
               VAR_INT, losing the enum tag set on `var` a few lines up. */
            Variable *bound = get_variable(curr_param->name);
            if (bound)
            {
                bound->value.ivalue = arg_values[i].ivalue;
                bound->enum_name = safe_strdup(&curr_param->enum_name);
            }
            break;
        }
        case VAR_PTR:
        /* Same reasoning as the argument-evaluation switch above:
           structurally unreachable for a declared parameter type. */
        case VAR_VOID:
        case NONE:
            break;
        }
        curr_param = curr_param->next;
    }
    reverse_parameter_list(&func->parameters);
    return true;
}

void register_struct_def(StructDef *def)
{
    /* alignment is set only by compute_struct_layout()/
       compute_union_layout() (called by both StructDef construction
       sites in lang.y), never by this function. A still-zero alignment
       here means some future construction path skipped that call --
       fail loud now rather than let a struct/union that later embeds
       this one by value silently pack with zero padding
       (get_struct_field_alignment() trusts a *registered* def's
       alignment outright; see that function's comment in this file).
       Deliberately not assert(): this must still catch the bug in a
       build that defines NDEBUG, not just a debug build, so it's a
       plain unconditional check -- same "fail loud, always" contract
       as every other internal-invariant check in this file (e.g. the
       "Memory allocation failed" exit(EXIT_FAILURE) sites above). */
    if (def->alignment == 0)
    {
        yyerror("Internal error: StructDef registered before "
                "compute_struct_layout()/compute_union_layout() ran");
        exit(EXIT_FAILURE);
    }
    if (!struct_registry)
        struct_registry = hm_new();
    size_t len = def->name.len;
    hm_put(struct_registry, def->name.data, len, def, sizeof(StructDef));
    def->next_def = struct_registry_list;
    struct_registry_list = def;
}

StructDef *get_struct_def(const String name)
{
    if (!struct_registry || !name.data)
        return NULL;
    return (StructDef *)hm_get(struct_registry, name.data, name.len);
}

void free_struct_registry(void)
{
    if (struct_registry)
    {
        hm_free_shallow(struct_registry);
        struct_registry = NULL;
    }
    StructDef *def = struct_registry_list;
    while (def)
    {
        StructField *f = def->fields;
        while (f)
        {
            StructField *nxt = f->next;
            SAFE_FREE(f->name);
            SAFE_FREE(f->struct_name);
            SAFE_FREE(f->enum_name);
            SAFE_FREE(f);
            f = nxt;
        }
        SAFE_FREE(def->name);
        StructDef *nxt = def->next_def;
        SAFE_FREE(def);
        def = nxt;
    }
    struct_registry_list = NULL;
}

StructField *find_struct_field(StructDef *def, const String name)
{
    if (!def || !name.data)
        return NULL;
    StructField *f = def->fields;
    while (f)
    {
        if (strcmp(f->name.data, name.data) == 0)
            return f;
        f = f->next;
    }
    return NULL;
}

/* Size in bytes that a single field occupies within its enclosing
   struct/union blob. Nested struct/union fields (type == VAR_STRUCT,
   pointer_level == 0) take the nested definition's total_size; every
   other field falls back to get_type_size_for_descriptor, honoring
   f->modifiers (is_long/is_long_long/is_unsigned) -- e.g. a VAR_INT
   field reached through a `lit giga rizz ...` alias must size as an
   8-byte long, not silently collapse to a 4-byte int.

   An array field (f->is_array, e.g. `chad params[4];`) occupies its
   element size times the product of every dimension -- computed below
   as `element_size`, using the exact same per-type logic a scalar field
   of the same declared type would, since array-ness only ever
   multiplies occupancy, never changes what a single element looks
   like. */
static size_t get_struct_field_size(StructField *f)
{
    size_t element_size;
    if (f->pointer_level > 0)
    {
        element_size = sizeof(uintptr_t);
    }
    else if (f->type == VAR_STRUCT)
    {
        StructDef *nested = get_struct_def(f->struct_name);
        /* Should always be resolved by the parser before layout is
           computed; fall back defensively rather than corrupt offsets. */
        element_size = nested ? nested->total_size : 0;
    }
    else
    {
        element_size = get_type_size_for_descriptor(f->type, 0, f->modifiers);
        if (element_size == 0)
            element_size = sizeof(int);
    }

    if (!f->is_array)
        return element_size;

    size_t count = 1;
    for (int i = 0; i < f->array_dimensions.num_dimensions; i++)
        count *= (size_t)f->array_dimensions.dimensions[i];
    return element_size * count;
}

/* Alignment in bytes that a single field imposes on its enclosing
   struct/union, matching the C ABI (`_Alignof` of the corresponding C
   type). Nested struct/union fields take the nested definition's own
   alignment; pointer fields align as a pointer. Mirrors
   get_struct_field_size()'s type switch, including f->modifiers for
   VAR_INT -- an 8-byte long/long-long field (reachable via a `lit
   giga`/`lit thicc` alias) must align as 8, not fall back to plain
   int's 4, or a following field would be under-padded.

   Deliberately ignores f->is_array: a C array never needs more
   alignment than its own element (`chad params[4];` is 4-aligned, same
   as a lone `chad`, not 16-aligned), so the switch below already
   answers array fields correctly without a special case. */
static size_t get_struct_field_alignment(StructField *f)
{
    if (f->pointer_level > 0)
        return _Alignof(uintptr_t);

    if (f->type == VAR_STRUCT)
    {
        StructDef *nested = get_struct_def(f->struct_name);
        /* Should always be resolved by the parser before layout is
           computed; fall back defensively rather than corrupt offsets. */
        return nested ? nested->alignment : 1;
    }

    switch (f->type)
    {
    case VAR_FLOAT:
        return _Alignof(float);
    case VAR_DOUBLE:
        return _Alignof(double);
    case VAR_BOOL:
        return _Alignof(bool);
    case VAR_SHORT:
        return _Alignof(short);
    case VAR_CHAR:
        return _Alignof(char);
    case VAR_INT:
        if (f->modifiers.is_long_long)
            return _Alignof(long long);
        if (f->modifiers.is_long)
            return _Alignof(long);
        return _Alignof(int);
    case VAR_STRING:
        return _Alignof(String);
    case VAR_ENUM:
        return _Alignof(int);
    case VAR_PTR:
        return _Alignof(uintptr_t);
    case VAR_VOID:
    case NONE:
    default:
        return 1;
    }
}

/* Round `offset` up to the next multiple of `alignment` (a power of two). */
static size_t align_up(size_t offset, size_t alignment)
{
    if (alignment <= 1)
        return offset;
    return (offset + alignment - 1) & ~(alignment - 1);
}

/* Walk def->fields, assign C-ABI-aligned offsets (padding before each
   field as needed), and write def->total_size/def->alignment -- the
   struct's total size rounded up to its own max field alignment
   (trailing padding) and that alignment itself. Matches the standard C
   struct-layout algorithm (align, place, pad), so a `gang`'s *occupancy*
   -- its total size and every field's offset -- matches what a real C
   compiler would produce for the same field list.

   That is a claim about shape, not about full FFI byte-compatibility:
   no current code path hands a `gang` *by value* across the FFI (see
   ast_expr_to_stdrot_value()'s lack of a VAR_STRUCT case, stdrot.c), and
   a wide scalar field (e.g. a `lit giga`-aliased `long` field) occupies
   the C-correct number of bytes but is still loaded/stored through this
   interpreter's plain 32-bit int path (evaluate_expression_int(),
   write_value_to_address()) -- the same pre-existing limitation plain
   `giga`/`thicc` variables have outside of any struct. Fixing that is
   out of scope here; this function's job is only that the *slot* is the
   right size and at the right offset.

   def->total_size and def->alignment are set together, here, as the
   single writer of both -- callers only ever populate def->fields/
   is_union and then call this (or compute_union_layout), so there is no
   window where one is stale relative to the other. */
void compute_struct_layout(StructDef *def)
{
    size_t off = 0;
    size_t max_align = 1;
    StructField *f = def->fields;
    while (f)
    {
        size_t falign = get_struct_field_alignment(f);
        if (falign > max_align)
            max_align = falign;
        off = align_up(off, falign);
        f->offset = off;
        off += get_struct_field_size(f);
        f = f->next;
    }
    def->total_size = align_up(off, max_align); /* includes trailing pad */
    def->alignment = max_align;
}

/* Union fields all share offset 0; def->total_size becomes the largest
   member, rounded up to the largest member's alignment (unions pad too
   -- `union { char c; int i; }` is 4 bytes in C, not 1). Same
   single-writer contract as compute_struct_layout() above. */
void compute_union_layout(StructDef *def)
{
    size_t max_size = 0;
    size_t max_align = 1;
    StructField *f = def->fields;
    while (f)
    {
        f->offset = 0;
        size_t fsz = get_struct_field_size(f);
        if (fsz > max_size)
            max_size = fsz;
        size_t falign = get_struct_field_alignment(f);
        if (falign > max_align)
            max_align = falign;
        f = f->next;
    }
    def->total_size = align_up(max_size, max_align);
    def->alignment = max_align;
}

void register_enum_def(EnumDef *def)
{
    if (!enum_registry)
        enum_registry = hm_new();
    hm_put(enum_registry, def->name.data, def->name.len, def, sizeof(EnumDef));
    def->next_def = enum_registry_list;
    enum_registry_list = def;
}

EnumDef *get_enum_def(const String name)
{
    if (!enum_registry || !name.data)
        return NULL;
    return (EnumDef *)hm_get(enum_registry, name.data, name.len);
}

void free_enum_registry(void)
{
    if (enum_registry)
    {
        hm_free_shallow(enum_registry);
        enum_registry = NULL;
    }
    EnumDef *def = enum_registry_list;
    while (def)
    {
        EnumConstant *c = def->constants;
        while (c)
        {
            EnumConstant *nxt = c->next;
            SAFE_FREE(c->name);
            SAFE_FREE(c);
            c = nxt;
        }
        SAFE_FREE(def->name);
        EnumDef *nxt = def->next_def;
        SAFE_FREE(def);
        def = nxt;
    }
    enum_registry_list = NULL;
}

TypeDescriptor make_type_descriptor(VarType type, int pointer_level,
                                    TypeModifiers modifiers)
{
    TypeDescriptor descriptor = {0};
    descriptor.type = type;
    descriptor.pointer_level = pointer_level;
    descriptor.modifiers = modifiers;
    return descriptor;
}

TypeDescriptor type_descriptor_from_alias(const TypeAlias *alias)
{
    if (!alias)
        return make_type_descriptor(NONE, 0, (TypeModifiers){0});

    /* struct_name/enum_name are borrowed from the registry. Callers may
       copy them onto AST/symbol objects they own, but must not free or
       mutate these strings, and must not keep the descriptor after
       free_type_alias_registry(). */
    TypeDescriptor descriptor = make_type_descriptor(
        alias->type, alias->pointer_level, alias->modifiers);
    descriptor.struct_name = alias->struct_name;
    descriptor.enum_name = alias->enum_name;
    return descriptor;
}

bool merge_type_modifiers(TypeModifiers base, TypeModifiers extra,
                          TypeModifiers *out, const String name)
{
    TypeModifiers merged = base;

    if ((extra.is_signed || extra.is_unsigned) &&
        (base.is_signed || base.is_unsigned))
    {
        char msg[MAX_BUFFER_LEN];
        snprintf(msg, sizeof(msg),
                 "Conflicting signedness modifiers for typedef alias '%s'",
                 name.data ? name.data : "?");
        yyerror_current_line(msg);
        typedef_had_error = true;
        return false;
    }

    if ((extra.is_long || extra.is_long_long) &&
        (base.is_long || base.is_long_long))
    {
        char msg[MAX_BUFFER_LEN];
        snprintf(msg, sizeof(msg),
                 "Conflicting width modifiers for typedef alias '%s'",
                 name.data ? name.data : "?");
        yyerror_current_line(msg);
        typedef_had_error = true;
        return false;
    }

    if (base.is_static)
    {
        char msg[MAX_BUFFER_LEN];
        snprintf(msg, sizeof(msg),
                 "Storage-class modifier cannot be stored in typedef alias "
                 "'%s'",
                 name.data ? name.data : "?");
        yyerror_current_line(msg);
        typedef_had_error = true;
        return false;
    }

    merged.is_volatile = base.is_volatile || extra.is_volatile;
    merged.is_signed = base.is_signed || extra.is_signed;
    merged.is_unsigned = base.is_unsigned || extra.is_unsigned;
    merged.is_const = base.is_const || extra.is_const;
    merged.is_long = base.is_long || extra.is_long;
    merged.is_long_long = base.is_long_long || extra.is_long_long;
    merged.is_sizeof = extra.is_sizeof;
    merged.is_static = extra.is_static;

    if (out)
        *out = merged;
    return true;
}

bool register_type_alias(String name, TypeDescriptor descriptor)
{
    if (!name.data)
    {
        yyerror_current_line("Invalid typedef alias name");
        typedef_had_error = true;
        return false;
    }

    if (descriptor.modifiers.is_static)
    {
        yyerror_current_line(
            "Storage-class modifiers are not allowed in typedef aliases");
        typedef_had_error = true;
        return false;
    }

    TypeModifiers alias_modifiers = descriptor.modifiers;
    alias_modifiers.is_sizeof = false;

    /* lit aliases live in the top-level ordinary identifier namespace:
       aliases, functions, variables, and enum constants cannot reuse the name.
       Struct/union/enum tags deliberately remain separate C-like tag
       namespaces and are checked by their own registries. */
    if (get_type_alias(name) || get_function(name) || get_variable(name) ||
        find_global_enum_constant(name))
    {
        char msg[MAX_BUFFER_LEN];
        snprintf(msg, sizeof(msg), "Typedef alias '%s' is already defined",
                 name.data);
        yyerror_current_line(msg);
        typedef_had_error = true;
        return false;
    }

    TypeAlias *alias = SAFE_MALLOC(TypeAlias);
    alias->name = safe_strdup(&name);
    alias->type = descriptor.type;
    alias->pointer_level = descriptor.pointer_level;
    alias->modifiers = alias_modifiers;
    alias->struct_name = descriptor.struct_name.data
                             ? safe_strdup(&descriptor.struct_name)
                             : (String){0};
    alias->enum_name = descriptor.enum_name.data
                           ? safe_strdup(&descriptor.enum_name)
                           : (String){0};
    alias->next_def = NULL;

    if (!type_alias_registry)
        type_alias_registry = hm_new();
    /* hm_put copies the key bytes; hm_free_shallow frees that copy. The
       TypeAlias node and its owned strings are released via the linked
       list below, not by the hashmap. */
    hm_put(type_alias_registry, alias->name.data, alias->name.len, &alias,
           sizeof(TypeAlias *));
    alias->next_def = type_alias_registry_list;
    type_alias_registry_list = alias;
    return true;
}

TypeAlias *get_type_alias(const String name)
{
    if (!type_alias_registry || !name.data)
        return NULL;
    TypeAlias **alias =
        (TypeAlias **)hm_get(type_alias_registry, name.data, name.len);
    return alias ? *alias : NULL;
}

void free_type_alias_registry(void)
{
    if (type_alias_registry)
    {
        hm_free_shallow(type_alias_registry);
        type_alias_registry = NULL;
    }

    TypeAlias *alias = type_alias_registry_list;
    while (alias)
    {
        TypeAlias *next = alias->next_def;
        SAFE_FREE(alias->name.data);
        alias->name = (String){0};
        SAFE_FREE(alias->struct_name.data);
        alias->struct_name = (String){0};
        SAFE_FREE(alias->enum_name.data);
        alias->enum_name = (String){0};
        SAFE_FREE(alias);
        alias = next;
    }
    type_alias_registry_list = NULL;
    typedef_had_error = false;
}

EnumConstant *find_global_enum_constant(const String name)
{
    if (!name.data)
        return NULL;
    EnumDef *def = enum_registry_list;
    while (def)
    {
        EnumConstant *c = def->constants;
        while (c)
        {
            if (strcmp(c->name.data, name.data) == 0)
                return c;
            c = c->next;
        }
        def = def->next_def;
    }
    return NULL;
}

bool finalize_enum_constants(EnumDef *def)
{
    bool ok = true;
    int next_value = 0;
    EnumConstant *c = def->constants;
    while (c)
    {
        if (!c->has_explicit_value)
            c->value = next_value;

        EnumConstant *prev = def->constants;
        while (prev != c)
        {
            if (strcmp(prev->name.data, c->name.data) == 0)
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg),
                         "Duplicate enum constant '%s' in '%s'", c->name.data,
                         def->name.data);
                yyerror(msg);
                ok = false;
                break;
            }
            prev = prev->next;
        }
        if (ok &&
            (find_global_enum_constant(c->name) || get_type_alias(c->name)))
        {
            char msg[MAX_BUFFER_LEN];
            snprintf(msg, sizeof(msg), "Enum constant '%s' is already defined",
                     c->name.data);
            yyerror(msg);
            ok = false;
        }

        next_value = c->value + 1;
        c = c->next;
    }
    return ok;
}

ASTNode *create_enum_def_node(String name)
{
    ASTNode *node = create_node(NODE_ENUM_DEF, NONE, current_modifiers);
    node->data.name = ARENA_STRDUP(name);
    return node;
}
