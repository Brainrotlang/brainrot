/* semantic_analyzer.c - Semantic analysis visitor implementation */

#include "semantic_analyzer.h"
#include "stdrot.h"
#include "lib/mem.h"
#include <stdio.h>
#include <string.h>

extern int yylineno;
extern void yyerror(const char *s);

/* Forward declaration: infer_expression_type()'s own NODE_FUNC_CALL/
   return_like_arg case needs this (mutual recursion -- see
   infer_expression_abi_type()'s own definition and comment, below
   infer_expression_type()). */
static VarType infer_expression_abi_type(ASTNode *expr,
                                         SemanticAnalyzer *analyzer);

/* Forward declaration: infer_expression_pointer_level()'s own
   NODE_STRUCT_ACCESS case (round-21 review, finding #2) needs this --
   defined below both (it recurses on a struct-access chain's own
   object, the same shape infer_expression_type()'s own NODE_STRUCT_
   ACCESS case already resolves this way). */
static StructDef *infer_struct_def_static(ASTNode *expr,
                                          SemanticAnalyzer *analyzer);

/* Create a new semantic analyzer */
SemanticAnalyzer *semantic_analyzer_new(void)
{
    SemanticAnalyzer *analyzer = SAFE_MALLOC(SemanticAnalyzer);
    if (!analyzer)
    {
        yyerror("Failed to allocate memory for semantic analyzer");
        return NULL;
    }

    /* Initialize the visitor function pointers */
    analyzer->base.visit_identifier = semantic_visit_identifier;
    analyzer->base.visit_function_call = semantic_visit_function_call;
    analyzer->base.visit_declaration = semantic_visit_declaration;
    analyzer->base.visit_assignment = semantic_visit_assignment;
    analyzer->base.visit_function_definition =
        semantic_visit_function_definition;
    analyzer->base.visit_binary_operation = semantic_visit_binary_operation;

    /* Initialize other visitor methods to NULL */
    analyzer->base.visit_int_literal = NULL;
    analyzer->base.visit_float_literal = NULL;
    analyzer->base.visit_double_literal = NULL;
    analyzer->base.visit_char_literal = NULL;
    analyzer->base.visit_short_literal = NULL;
    analyzer->base.visit_boolean_literal = NULL;
    analyzer->base.visit_string_literal = NULL;
    analyzer->base.visit_unary_operation = NULL;
    analyzer->base.visit_array_access = NULL;
    analyzer->base.visit_sizeof = NULL;
    analyzer->base.visit_if_statement = NULL;
    analyzer->base.visit_for_statement = NULL;
    analyzer->base.visit_while_statement = NULL;
    analyzer->base.visit_do_while_statement = NULL;
    analyzer->base.visit_switch_statement = NULL;
    analyzer->base.visit_break_statement = NULL;
    analyzer->base.visit_return_statement = NULL;
    analyzer->base.visit_statement_list = NULL;
    analyzer->base.visit_print_statement = NULL;
    analyzer->base.visit_error_statement = NULL;

    analyzer->current_scope = NULL;
    analyzer->symbol_table = NULL;
    analyzer->errors = NULL;
    analyzer->has_errors = false;
    analyzer->error_count = 0;
    analyzer->is_collecting_phase = false;
    analyzer->scope_depth = 0;
    analyzer->current_function_name = (String){0};

    return analyzer;
}

/* Free semantic analyzer */
void semantic_analyzer_free(SemanticAnalyzer *analyzer)
{
    if (!analyzer)
        return;

    free_semantic_errors(analyzer->errors);
    free_symbol_table(analyzer->symbol_table);

    while (analyzer->current_scope)
    {
        exit_semantic_scope(analyzer);
    }

    SAFE_FREE(analyzer);
}

/* Main semantic analysis function */
bool semantic_analyze(ASTNode *root)
{
    if (!root)
        return true;

    SemanticAnalyzer *analyzer = semantic_analyzer_new();
    if (!analyzer)
    {
        fprintf(stderr, "Error: Failed to create semantic analyzer\n");
        return false;
    }

    /* Phase 1: Collect all declarations */
    analyzer->is_collecting_phase = true;
    analyzer->scope_depth = 0;
    collect_declarations(analyzer, root);

    /* Phase 2: Perform semantic analysis */
    analyzer->is_collecting_phase = false;
    analyzer->scope_depth = 0;

    semantic_analyze_with_scope_tracking(analyzer, root);

    bool success = !analyzer->has_errors;

    if (!success)
    {
        print_semantic_errors(analyzer);
    }

    semantic_analyzer_free(analyzer);
    return success;
}

/* Add a semantic error */
void add_semantic_error(SemanticAnalyzer *analyzer, SemanticErrorType type,
                        String message, int line_number)
{
    if (!analyzer || !message.data)
        return;

    SemanticError *error = SAFE_MALLOC(SemanticError);
    if (!error)
        return;

    error->type = type;
    error->message = safe_strdup(&message);
    error->line_number = (line_number > 0) ? line_number : 1;
    error->next = analyzer->errors;

    analyzer->errors = error;
    analyzer->has_errors = true;
    analyzer->error_count++;
}

/* Convert VarType enum to readable string */
const char *vartype_to_string(VarType type)
{
    switch (type)
    {
    case VAR_INT:
        return "int";
    case VAR_SHORT:
        return "short";
    case VAR_FLOAT:
        return "float";
    case VAR_DOUBLE:
        return "double";
    case VAR_BOOL:
        return "bool";
    case VAR_CHAR:
        return "char";
    case VAR_STRING:
        return "string";
    case VAR_ENUM:
        return "enum";
    case VAR_STRUCT:
        return "struct";
    case VAR_PTR:
        return "pointer";
    case VAR_VOID:
        return "void";
    case NONE:
    default:
        /* NONE means "couldn't determine a type," not "determined it to
           be void" -- that's VAR_VOID's own case, just above. Reusing
           "void" for NONE was itself an instance of the sentinel-
           collision bug VAR_VOID exists to close: an error message that
           printed "expected int, got void" for a genuinely INDETERMINATE
           expression would misreport why the mismatch happened. */
        return "indeterminate";
    }
}

/* Free semantic error list */
void free_semantic_errors(SemanticError *errors)
{
    while (errors)
    {
        SemanticError *next = errors->next;
        SAFE_FREE(errors->message);
        SAFE_FREE(errors);
        errors = next;
    }
}

/* Print all semantic errors */
void print_semantic_errors(SemanticAnalyzer *analyzer)
{
    if (!analyzer || !analyzer->errors)
        return;

    SemanticError *error = analyzer->errors;
    while (error)
    {
        /* Use simple, direct error messages */
        switch (error->type)
        {
        case SEMANTIC_ERROR_UNDEFINED_VARIABLE:
            if (error->line_number > 0)
            {
                fprintf(stderr, "Error: Undefined variable at line %d\n",
                        error->line_number);
            }
            else
            {
                fprintf(stderr, "Error: Undefined variable\n");
            }
            break;
        case SEMANTIC_ERROR_UNDEFINED_FUNCTION:
            if (error->line_number > 0)
            {
                fprintf(stderr, "Error: Undefined function at line %d\n",
                        error->line_number);
            }
            else
            {
                fprintf(stderr, "Error: Undefined function\n");
            }
            break;
        case SEMANTIC_ERROR_CONST_ASSIGNMENT:
            if (error->line_number > 0)
            {
                fprintf(stderr,
                        "Error: Cannot modify const variable at line %d\n",
                        error->line_number);
            }
            else
            {
                fprintf(stderr, "Error: Cannot modify const variable\n");
            }
            break;
        case SEMANTIC_ERROR_REDEFINITION:
            if (error->line_number > 0)
            {
                fprintf(stderr, "Error: Function redefinition at line %d\n",
                        error->line_number);
            }
            else
            {
                fprintf(stderr, "Error: Function redefinition\n");
            }
            break;
        case SEMANTIC_ERROR_SCOPE_ERROR:
            if (error->line_number > 0)
            {
                fprintf(stderr, "Error: Variable out of scope at line %d\n",
                        error->line_number);
            }
            else
            {
                fprintf(stderr, "Error: Variable out of scope\n");
            }
            break;
        default:
            if (error->line_number > 0)
            {
                fprintf(stderr, "Error: %s at line %d\n", error->message.data,
                        error->line_number);
            }
            else
            {
                fprintf(stderr, "Error: %s\n", error->message.data);
            }
            break;
        }

        error = error->next;
    }
}

/* Type checking helper functions */
bool check_type_compatibility(VarType expected, VarType actual)
{
    return check_type_compatibility_ex(expected, 0, actual, 0);
}

bool check_type_compatibility_ex(VarType expected, int expected_pointer_level,
                                 VarType actual, int actual_pointer_level)
{
    if (expected_pointer_level != actual_pointer_level)
        return false;
    if (expected_pointer_level > 0)
    {
        /* VAR_PTR (an opaque native pointer -- STDROT_PTR) has no
           concrete base type by design, so it's wildcard-compatible with
           any base type once pointer *levels* already match (checked
           above). This makes `rizz *p = get_ptr();` type-check without
           needing VAR_PTR to equal every possible base VarType.
           Deliberately NOT the same guarantee as C's void* conversion
           rule, despite the surface resemblance: void* <-> T* is sound
           for exactly one level of indirection, but void** <-> T** is
           not (writing an unrelated pointer through a void** silently
           corrupts whatever the T** side believes it points to). This
           check applies the wildcard at every depth uniformly --
           STDROT_PTR level N is opaque all the way down, not just at the
           outermost level -- because StdrotParam (stdrot_api.h) has no
           field to express a *partially* erased pointer ("pointer to
           pointer to known-int" vs. "...to known-double"); only
           pointer_level exists, no base type at any depth. That is an
           intentional simplification for now, not an oversight: a
           binding generator emitting STDROT_PTR at depth > 1 should
           treat the whole pointed-to graph as opaque, not assume any
           C-void**-like base-type safety this check does not provide.
           NONE, deliberately, gets no such exception here: unlike
           VAR_PTR it means "couldn't determine a type at all", and
           granting it pointer-compatibility would silently accept any
           pointer assignment whose type inference happened to fail for
           an unrelated reason -- exactly the fail-open hole VAR_PTR
           exists to avoid. */
        if (expected == VAR_PTR || actual == VAR_PTR)
            return true;
        return expected == actual;
    }
    /* VAR_VOID (ast.h's own comment has the full reasoning) is
       compatible with NOTHING -- not even itself. Every other type in
       this checker means "some concrete representation exists"; VAR_VOID
       means "no value exists to have a representation at all." There is
       no context where consuming a certainly-void expression as a value
       is ever correct, so this has to be checked before the `expected ==
       actual` shortcut below (which would otherwise wave through
       `rizz x = a_void_native();` whenever both source and destination
       "type" happened to be reported as VAR_VOID -- structurally
       possible only for the RHS today, but the guard belongs here,
       unconditionally, not on the strength of what currently calls
       it). */
    if (expected == VAR_VOID || actual == VAR_VOID)
        return false;

    if (expected == actual)
        return true;

    /* Allow implicit conversions between numeric types. An enum variable
       is included here (rather than requiring an exact VAR_ENUM match) to
       match C, where enum constants/variables freely interconvert with
       int without a cast. */
    if ((expected == VAR_INT || expected == VAR_SHORT ||
         expected == VAR_FLOAT || expected == VAR_DOUBLE ||
         expected == VAR_ENUM) &&
        (actual == VAR_INT || actual == VAR_SHORT || actual == VAR_FLOAT ||
         actual == VAR_DOUBLE || actual == VAR_ENUM))
    {
        return true;
    }

    return false;
}

int infer_expression_pointer_level(ASTNode *node, SemanticAnalyzer *analyzer)
{
    if (!node)
        return 0;

    switch (node->type)
    {
    case NODE_IDENTIFIER:
    {
        SymbolEntry *symbol = find_symbol(analyzer, node->data.name);
        if (symbol)
            return symbol->pointer_level;
        Variable *var = get_variable(node->data.name);
        return var ? var->pointer_level : node->pointer_level;
    }
    case NODE_ARRAY_ACCESS:
    {
        /* Round-22 review, finding #2 -- mirrors NODE_IDENTIFIER's own
           case just above (find_symbol() first, get_variable() as a
           fallback for whenever runtime Variable state genuinely is
           available), for the identical reason: semantic_check_native_
           call() (NODE_FUNC_CALL's own case, below) runs BEFORE the
           argument-list loop visits an array-access argument, so
           get_variable() alone -- returning NULL during that single
           static pass, since no runtime Variable exists yet (semantic_
           analyze()'s own "Phase 1"/"Phase 2" comment) -- fell through
           to node->pointer_level, unpopulated at that point. A pointer
           array (`rizz *ptrs[4];`) element was falsely rejected against
           a STDROT_PTR parameter (pointer_level read as 0) and falsely
           *accepted* against a scalar one (also read as 0, hiding the
           mismatch until runtime ABI enforcement caught it) -- the
           identical bug NODE_STRUCT_ACCESS had (round 21, finding #2),
           just for arrays instead of struct fields. */
        if (node->data.array.base)
        {
            /* `foo.arr[i]` -- mirrors NODE_STRUCT_ACCESS's own case
               below exactly (runtime resolve_struct_access() first,
               static infer_struct_def_static()+find_struct_field()
               fallback for Phase 1, before any runtime Variable
               exists), since Array.base is itself a NODE_STRUCT_ACCESS
               node (`foo.arr`) and needs the identical two-path
               resolution. Both branches require is_array: a resolved-
               but-not-actually-an-array field (e.g. indexing a scalar)
               must be treated as unresolved here, matching ast.c's
               resolve_array_access_element() -- returning a "valid"
               pointer_level for `f.n[0]` on a scalar `rizz n` would let
               this disagree with infer_expression_type()'s own gate on
               the identical node. */
            StructDef *def = NULL;
            void *base = NULL;
            StructField *fld = NULL;
            if (resolve_struct_access(node->data.array.base, &def, &base, &fld,
                                      false) &&
                fld->is_array)
                return fld->pointer_level;

            StructDef *static_def = infer_struct_def_static(
                node->data.array.base->data.struct_access.object, analyzer);
            if (!static_def)
                return node->pointer_level;
            StructField *f = find_struct_field(
                static_def,
                node->data.array.base->data.struct_access.member_name);
            return f && f->is_array ? f->pointer_level : node->pointer_level;
        }
        SymbolEntry *symbol = find_symbol(analyzer, node->data.array.name);
        if (symbol && symbol->is_array)
            return symbol->pointer_level;
        Variable *var = get_variable(node->data.array.name);
        return var && var->is_array ? var->pointer_level : node->pointer_level;
    }
    case NODE_UNARY_OPERATION:
        if (node->data.unary.op == OP_ADDRESS_OF)
        {
            return infer_expression_pointer_level(node->data.unary.operand,
                                                  analyzer) +
                   1;
        }
        if (node->data.unary.op == OP_DEREFERENCE)
        {
            int operand_level = infer_expression_pointer_level(
                node->data.unary.operand, analyzer);
            return operand_level > 0 ? operand_level - 1 : 0;
        }
        return infer_expression_pointer_level(node->data.unary.operand,
                                              analyzer);
    case NODE_FUNC_CALL:
    {
        if (is_builtin_function(node->data.func_call.function_name))
        {
            const StdrotEntry *entry =
                get_native_function(node->data.func_call.function_name);
            /* Only STDROT_PTR has a pointer_level to report -- an opaque
               pointer is inherently one level of indirection
               (param->pointer_level counts any *further* indirection
               beyond that, e.g. a "pointer to a pointer"). Every other
               native return (including the unmarshalled STDROT_HANDLE,
               rejected elsewhere) is pointer_level 0 as far as static
               analysis is concerned. */
            if (entry && entry->return_type.type == STDROT_PTR)
                return entry->return_type.pointer_level + 1;
            return 0;
        }
        SymbolEntry *symbol =
            find_symbol(analyzer, node->data.func_call.function_name);
        if (symbol && symbol->is_function)
            return symbol->return_pointer_level;
        Function *func = get_function(node->data.func_call.function_name);
        return func ? func->return_pointer_level : 0;
    }
    case NODE_OPERATION:
        switch (node->data.op.op)
        {
        case OP_PLUS:
        case OP_MINUS:
        {
            int left_level =
                infer_expression_pointer_level(node->data.op.left, analyzer);
            int right_level =
                infer_expression_pointer_level(node->data.op.right, analyzer);
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
        /* Round-21 review, finding #2 -- mirrors infer_expression_type()'s
           own NODE_STRUCT_ACCESS case (below) exactly, for the identical
           reason: semantic_check_native_call() (called from NODE_FUNC_
           CALL's own case in semantic_analyze_with_scope_tracking(),
           BEFORE that switch's argument-list loop visits a struct-access
           argument) needs this expression's pointer_level before the
           visitor that would normally populate node->pointer_level
           (this function's own `default:` fallback, just below) has
           ever run. Without this case, EVERY struct-access argument's
           static pointer_level was 0, unconditionally, regardless of
           the field's actual declared pointer_level -- silently
           disabling STDROT_PTR argument-type checking for any native
           call passing one (false rejection of a genuinely PTR-typed
           field against a PTR parameter, since the level 0 vs 1
           mismatch alone was enough to reject it) and, worse, false
           ACCEPTANCE of a non-pointer field mistakenly checked against
           a scalar parameter as if it were pointer-free, only for the
           runtime ABI to discover the field is actually a pointer once
           the argument is really marshalled (ast_expr_to_stdrot_value()
           unconditionally checks get_expression_pointer_level() > 0
           first). resolve_struct_access() only works once runtime
           Variable state exists (semantic_analyze()'s own "Phase 1"/
           "Phase 2" comment) -- falls back to infer_struct_def_static()
           (below), which resolves purely from the global struct-
           definition registry and the analyzer's own symbol table, the
           same static path infer_expression_type() already uses. */
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (resolve_struct_access(node, &def, &base, &fld, false))
            return fld->pointer_level;

        StructDef *static_def =
            infer_struct_def_static(node->data.struct_access.object, analyzer);
        if (!static_def)
            return 0;
        StructField *f =
            find_struct_field(static_def, node->data.struct_access.member_name);
        return f ? f->pointer_level : 0;
    }
    default:
        return node->pointer_level;
    }
}

/* Purely static struct-type resolution for a struct-typed expression node
 * (NODE_IDENTIFIER, or a nested NODE_STRUCT_ACCESS for a chain like
 * `a.b.c`), used by infer_expression_type()'s NODE_STRUCT_ACCESS case
 * below to resolve a member-access chain of ANY depth using only the
 * analyzer's own symbol table (find_symbol) and the global struct-
 * definition registry (get_struct_def) -- never get_variable()/runtime
 * Variable storage, which (like resolve_struct_access()) isn't populated
 * yet during this single static pass (see semantic_analyze()'s own
 * comment).
 *
 * This mirrors the recursive parent-type resolution semantic_analyze_
 * with_scope_tracking()'s own NODE_STRUCT_ACCESS case performs (which
 * additionally falls back to get_variable() and mutates node->var_type on
 * the AST node) -- but that mutation can't be relied on here: semantic_
 * check_native_call() (which calls infer_expression_type() on each
 * argument via infer_expression_abi_type()) runs from that same case's
 * NODE_FUNC_CALL handling BEFORE the arguments themselves get visited by
 * that case's own argument loop, so a struct-access argument's node->
 * var_type may still be its unpopulated default the first time this asks
 * about it. Recursing independently here, purely off static type
 * metadata, doesn't depend on visit order at all.
 *
 * A single-level pointer-typed intermediate field (`gang Node *next` in
 * `a.next.val`, #197) resolves through to the pointee's definition, the
 * same way resolve_struct_access() follows it at runtime. Returns NULL if
 * `expr` doesn't statically resolve to a struct-typed value this way
 * (unknown symbol, non-struct type, or a multi-level pointer field
 * `pointer_level > 1`, which needs an explicit `(*x)->` and is rejected by
 * both this helper and semantic_analyze_with_scope_tracking()).
 */
static StructDef *infer_struct_def_static(ASTNode *expr,
                                          SemanticAnalyzer *analyzer)
{
    if (!expr)
        return NULL;

    if (expr->type == NODE_IDENTIFIER)
    {
        /* No pointer_level check here, matching resolve_struct_access()'s
           own top-level NODE_IDENTIFIER case (ast.c) and semantic_
           analyze_with_scope_tracking()'s NODE_STRUCT_ACCESS case (this
           file): a single-level pointer-typed base or intermediate field
           is followed, not rejected (#196/#197); only a multi-level
           pointer field (`pointer_level > 1`) is rejected, in the
           NODE_STRUCT_ACCESS branch below, matching both of those. */
        SymbolEntry *sym = find_symbol(analyzer, expr->data.name);
        if (!sym || sym->type != VAR_STRUCT || !sym->struct_name.data)
            return NULL;
        return get_struct_def(sym->struct_name);
    }

    if (expr->type == NODE_STRUCT_ACCESS)
    {
        StructDef *parent_def =
            infer_struct_def_static(expr->data.struct_access.object, analyzer);
        if (!parent_def)
            return NULL;

        StructField *fld =
            find_struct_field(parent_def, expr->data.struct_access.member_name);
        /* #197: a single-level pointer field (`gang Node *next`) is now a
           resolvable intermediate -- resolve_struct_access() (ast.c)
           follows it at runtime -- so only reject `pointer_level > 1`
           (needs an explicit `(*x)->`), matching that function's own rule.
           struct_name is populated for pointer-typed struct fields too, so
           get_struct_def() below still finds the pointee's definition. */
        if (!fld || fld->type != VAR_STRUCT || fld->pointer_level > 1 ||
            !fld->struct_name.data)
            return NULL;
        return get_struct_def(fld->struct_name);
    }

    return NULL;
}

/* Best-effort resolution of the struct/union TAG a pointer-typed
   expression's pointee statically has -- not just "is this a struct
   pointer" (infer_expression_type()/infer_expression_pointer_level()
   already answer that), but which ONE. VAR_STRUCT is a category, not a
   type: two different struct tags of the same or different size are
   both VAR_STRUCT, pointer_level 1, and check_type_compatibility_ex()
   (which has no struct_name parameter at all) calls them compatible.
   `gang Point *pp = &some_rect;` therefore passed the category check
   check_declaration_initializer_compatibility() runs and then followed
   the pointer using Point's layout on an actually Rect-shaped blob (PR
   #248 review, round 2, finding 1) -- for two structs of different size
   that is a real out-of-bounds read/write, not just a wrong value.
   Returns an empty String when the tag can't be determined statically
   (e.g. a function call's return struct tag isn't tracked anywhere) --
   callers must treat that as "unknown, don't block," the same fail-open
   convention every other NONE-typed case in this analyzer already
   follows, not as "confirmed no struct." */
static String infer_expression_struct_name(ASTNode *expr,
                                           SemanticAnalyzer *analyzer)
{
    if (!expr)
        return (String){0};

    switch (expr->type)
    {
    case NODE_IDENTIFIER:
    {
        SymbolEntry *symbol = find_symbol(analyzer, expr->data.name);
        if (symbol)
            return symbol->struct_name;
        Variable *var = get_variable(expr->data.name);
        return var ? var->struct_name : (String){0};
    }
    case NODE_UNARY_OPERATION:
        /* Neither &x nor *x changes which struct tag is at the other end
           -- `&r` still refers to r's own tag, `*pp` still refers to
           whatever pp points at. */
        if (expr->data.unary.op == OP_ADDRESS_OF ||
            expr->data.unary.op == OP_DEREFERENCE)
            return infer_expression_struct_name(expr->data.unary.operand,
                                                analyzer);
        return (String){0};
    case NODE_STRUCT_ACCESS:
    {
        StructDef *parent_def =
            infer_struct_def_static(expr->data.struct_access.object, analyzer);
        if (!parent_def)
            return (String){0};
        StructField *fld =
            find_struct_field(parent_def, expr->data.struct_access.member_name);
        return fld ? fld->struct_name : (String){0};
    }
    case NODE_ARRAY_ACCESS:
    {
        /* Mirrors infer_expression_pointer_level()'s own NODE_ARRAY_ACCESS
           case (above in this file) exactly -- an array of struct/union
           POINTERS (`PointPtr values[2]; values[0] = &r;`) needs its
           element tag resolved the same two-path way (struct-field-backed
           via Array.base, or a plain array Variable/SymbolEntry), or an
           array-element assignment target silently skipped this whole
           tag check (empty struct_name looked exactly like "unknown,"
           the same conflation finding 2 flagged for the source side --
           PR #248 review, round 3). */
        if (expr->data.array.base)
        {
            StructDef *def = NULL;
            void *base = NULL;
            StructField *fld = NULL;
            if (resolve_struct_access(expr->data.array.base, &def, &base, &fld,
                                      false) &&
                fld->is_array)
                return fld->struct_name;

            StructDef *static_def = infer_struct_def_static(
                expr->data.array.base->data.struct_access.object, analyzer);
            if (!static_def)
                return (String){0};
            StructField *f = find_struct_field(
                static_def,
                expr->data.array.base->data.struct_access.member_name);
            return f && f->is_array ? f->struct_name : (String){0};
        }
        SymbolEntry *symbol = find_symbol(analyzer, expr->data.array.name);
        if (symbol && symbol->is_array)
            return symbol->struct_name;
        Variable *var = get_variable(expr->data.array.name);
        return var && var->is_array ? var->struct_name : (String){0};
    }
    case NODE_FUNC_CALL:
    {
        /* A user-defined function's declared return struct/union tag lives
           on its Function object (return_struct_name, set at registration),
           and IS the tag a pointer-to-struct call result points at
           (`bussin get_rect(r);`, `gang Point *p = get_rect(&rc);`, `p =
           get_rect(&rc);`, #193). Without this arm the helper fail-opened
           on every call, so a wrong-tag relay/init/assignment through a
           call type-punned silently (PR #255 review). A native call has no
           user Function and no tracked struct tag, so it stays unknown
           (fail-open), same as any other unresolvable source. */
        if (is_builtin_function(expr->data.func_call.function_name))
            return (String){0};
        Function *fn = get_function(expr->data.func_call.function_name);
        return fn ? fn->return_struct_name : (String){0};
    }
    default:
        return (String){0};
    }
}

/* Checks that a pointer-typed struct/union destination's declared TAG
   matches a pointer-typed source expression's own tag, when both are
   statically knowable -- the piece check_declaration_initializer_
   compatibility()/check_type_compatibility_ex() cannot express (neither
   takes a struct_name; VAR_STRUCT is a category, not a type, so `gang
   Point *pp = &some_rect;` passes both of those and then follows the
   pointer using Point's layout on an actually Rect-shaped blob -- PR
   #248 review, round 2). Silent (no error) when either tag can't be
   determined statically -- same fail-open convention
   infer_expression_struct_name() itself documents; this is a best-effort
   catch, not a complete type system. */
static void check_pointer_struct_tag_match(SemanticAnalyzer *analyzer,
                                           String declared_tag,
                                           ASTNode *source_expr,
                                           const char *message_prefix,
                                           int line_number)
{
    if (!declared_tag.data || !source_expr)
        return;
    String source_tag = infer_expression_struct_name(source_expr, analyzer);
    if (!source_tag.data || strcmp(declared_tag.data, source_tag.data) == 0)
        return;

    char error_msg[MAX_BUFFER_LEN];
    snprintf(error_msg, sizeof(error_msg),
             "%s: expected pointer to struct/union '%s', got pointer to '%s'",
             message_prefix, declared_tag.data, source_tag.data);
    add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                       STRING_LITERAL(error_msg), line_number);
}

/* Infer the type of an expression */
VarType infer_expression_type(ASTNode *node, SemanticAnalyzer *analyzer)
{
    if (!node)
        return NONE;

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
        return VAR_CHAR;
    case NODE_STRING:
    case NODE_STRING_LITERAL:
        return VAR_STRING;

    case NODE_IDENTIFIER:
    {
        /* First try semantic analyzer symbol table */
        SymbolEntry *symbol = find_symbol(analyzer, node->data.name);
        if (symbol)
        {
            return symbol->type;
        }

        /* Fallback to runtime variable lookup */
        Variable *var = get_variable(node->data.name);
        if (var)
        {
            return var->var_type;
        }
        /* Not a variable -- an enum constant has type int in C. */
        if (find_global_enum_constant(node->data.name))
        {
            return VAR_INT;
        }
        return NONE;
    }

    case NODE_ARRAY_ACCESS:
    {
        /* Previously missing entirely (fell to `default: return NONE`),
           which meant EVERY array-access expression's static type was
           unknown, unconditionally -- not just the VAR_CHAR ones
           infer_expression_abi_type() cares about. That silently
           disabled argument type-checking for any native call passing
           one (semantic_check_native_call()'s `actual == NONE`
           fallthrough), regardless of the array's actual element type.
           Reports the ELEMENT type (the array's own declared type, same
           as NODE_IDENTIFIER just above) -- infer_expression_abi_type()
           is what narrows a VAR_CHAR one to VAR_INT to match how
           ast_expr_to_stdrot_value() (stdrot.c) actually marshals it. */
        if (node->data.array.base)
        {
            /* Same two-path resolution as infer_expression_pointer_
               level()'s own NODE_ARRAY_ACCESS case above -- see that
               case's comment, including why both branches require
               is_array. */
            StructDef *def = NULL;
            void *base = NULL;
            StructField *fld = NULL;
            if (resolve_struct_access(node->data.array.base, &def, &base, &fld,
                                      false) &&
                fld->is_array)
                return fld->type;

            StructDef *static_def = infer_struct_def_static(
                node->data.array.base->data.struct_access.object, analyzer);
            if (!static_def)
                return NONE;
            StructField *f = find_struct_field(
                static_def,
                node->data.array.base->data.struct_access.member_name);
            return f && f->is_array ? f->type : NONE;
        }

        const String array_name = node->data.array.name;
        if (!array_name.data)
            return NONE;

        SymbolEntry *symbol = find_symbol(analyzer, array_name);
        if (symbol && symbol->is_array)
            return symbol->type;

        Variable *var = get_variable(array_name);
        if (var && var->is_array)
            return var->var_type;

        return NONE;
    }

    case NODE_OPERATION:
    {
        VarType left_type = infer_expression_type(node->data.op.left, analyzer);
        VarType right_type =
            infer_expression_type(node->data.op.right, analyzer);
        int left_pointer_level =
            infer_expression_pointer_level(node->data.op.left, analyzer);
        int right_pointer_level =
            infer_expression_pointer_level(node->data.op.right, analyzer);

        if (node->data.op.op == OP_EQ || node->data.op.op == OP_NE ||
            node->data.op.op == OP_LT || node->data.op.op == OP_GT ||
            node->data.op.op == OP_LE || node->data.op.op == OP_GE ||
            node->data.op.op == OP_AND || node->data.op.op == OP_OR)
        {
            return VAR_BOOL;
        }

        if (left_pointer_level > 0 && right_pointer_level == 0)
        {
            return left_type;
        }
        if (right_pointer_level > 0 && left_pointer_level == 0 &&
            node->data.op.op == OP_PLUS)
        {
            return right_type;
        }

        /* For arithmetic operations, return the "wider" type */
        if (left_type == VAR_DOUBLE || right_type == VAR_DOUBLE)
        {
            return VAR_DOUBLE;
        }
        if (left_type == VAR_FLOAT || right_type == VAR_FLOAT)
        {
            return VAR_FLOAT;
        }
        if (left_type == VAR_INT || right_type == VAR_INT)
        {
            return VAR_INT;
        }

        return left_type; /* Default to left operand type */
    }

    case NODE_UNARY_OPERATION:
        return infer_expression_type(node->data.unary.operand, analyzer);

    case NODE_FUNC_CALL:
    {
        /* Check if it's a built-in function */
        if (is_builtin_function(node->data.func_call.function_name))
        {
            const StdrotEntry *entry =
                get_native_function(node->data.func_call.function_name);
            if (!entry)
                return NONE;

            /* return_like_arg is an explicit declaration (set via
               STDROT_EXPORT_SIG_IDENTITY(), e.g. slorp<T>(T) -> T), never
               inferred from return_type/params happening to both say
               STDROT_ANY -- that shape is ambiguous on its own: a legacy
               STDROT_EXPORT() also has an STDROT_ANY return, for a
               completely different reason (genuinely unknown, not "same
               as this argument"). Uses infer_expression_abi_type(), not
               plain infer_expression_type(), because the *identity*
               this call promises is "same ABI representation as this
               argument," not "same Brainrot source-level VarType" --
               those two differ for a char array (`yap buf[32]`):
               source-level it's VAR_CHAR, but ast_expr_to_stdrot_value()
               (stdrot.c) marshals it as STDROT_STRING, and slorp's own
               dispatch returns STDROT_STRING right back. Reporting plain
               VAR_CHAR here would let `chad c = slorp(buf);` type-check
               (WRONG -- the runtime result is a string) while rejecting
               `rant s = slorp(buf);` (WRONG -- the runtime result IS a
               string, just not by the source-level type). */
            if (entry->return_like_arg >= 0)
            {
                int idx = 0;
                for (ArgumentList *a = node->data.func_call.arguments; a;
                     a = a->next, idx++)
                {
                    if (idx == entry->return_like_arg)
                        return a->expr ? infer_expression_abi_type(a->expr,
                                                                   analyzer)
                                       : NONE;
                }
                return NONE;
            }

            if (entry->return_type.type == STDROT_ANY)
            {
                /* Legacy/untyped STDROT_EXPORT(): genuinely unknown. */
                return NONE;
            }

            return stdrot_type_to_vartype(entry->return_type.type);
        }

        /* Look up user-defined function */
        Function *func = get_function(node->data.func_call.function_name);
        if (func)
        {
            return func->return_type;
        }

        return NONE;
    }

    case NODE_STRUCT_ACCESS:
    {
        StructDef *def = NULL;
        void *base = NULL;
        StructField *fld = NULL;
        if (resolve_struct_access(node, &def, &base, &fld, false))
            return fld->type;

        /* resolve_struct_access() only works at runtime -- its object
           resolution goes through get_variable(), which is always NULL
           during this single static pass (see semantic_analyze(),
           "Phase 1: Collect all declarations" then "Phase 2" -- the
           whole program is analyzed before the interpreter executes
           anything). Without a static fallback, EVERY struct-field
           access's type was unconditionally NONE, silently disabling
           argument type-checking for any native call passing one
           (semantic_check_native_call()'s `actual == NONE` fallthrough)
           regardless of the field's actual type. A struct's field
           *types* don't depend on any particular instance, though --
           get_struct_def() is a global type-definition registry,
           independent of runtime Variable state -- so infer_struct_def_
           static() (above) resolves the object -- direct (`foo.c`) or a
           chain of any depth (`foo.inner.c`, `foo.inner.deeper.c`) --
           using only that registry and the analyzer's own symbol table. */
        StructDef *static_def =
            infer_struct_def_static(node->data.struct_access.object, analyzer);
        if (!static_def)
            return NONE;

        StructField *f =
            find_struct_field(static_def, node->data.struct_access.member_name);
        return f ? f->type : NONE;
    }

    default:
        return NONE;
    }
}

/* The static VarType a native-ABI marshalling call (ast_expr_to_stdrot_
 * value(), stdrot.c) will actually produce for this expression --
 * distinct from infer_expression_type() because that function reports
 * the SOURCE-LEVEL type, while the ABI boundary sometimes lowers it to
 * something else. Two independent exceptions, both delegated to a
 * single shared rule each side consults instead of encoding its own
 * copy (so this function and ast_expr_to_stdrot_value() cannot
 * independently drift out of agreement, the way they briefly did):
 *
 *   1. stdrot_char_narrows_to_int() (stdrot.h) -- binary operations,
 *      unary arithmetic negation, and (deliberately NOT) array access,
 *      struct access, dereference, or increment/decrement never preserve
 *      a VAR_CHAR base type as STDROT_CHAR for the ones that DO narrow;
 *      they lower through plain int instead, matching the specific C
 *      rule that actually applies to each shape (see that function's own
 *      per-case reasoning, stdrot.c) rather than a single blanket "sub-
 *      int expression" rule. `takes_char(c + c)` must be checked as a
 *      VAR_INT argument; `takes_char(buf[0])` and `takes_char(foo.c)`
 *      must NOT -- a subscript or member-access expression is an
 *      ordinary lvalue of its element/field's own declared type in C,
 *      promoted to int only for an unprototyped call or a variadic tail,
 *      never merely for appearing as a normally-prototyped argument.
 *
 *   2. A VAR_CHAR *array identifier* (`yap buf[32]`) marshals as
 *      STDROT_STRING (ast_expr_to_stdrot_value()'s VAR_CHAR/is_array
 *      branch), which stdrot_type_to_vartype() maps back to VAR_STRING
 *      -- but infer_expression_type() reports the ELEMENT type
 *      (VAR_CHAR) for any VAR_CHAR expression, array or not, so it
 *      can't tell "a single char" and "a char buffer used as a string"
 *      apart. Only NODE_IDENTIFIER is checked here: that's the only
 *      expression shape SymbolEntry.is_array is populated for (see its
 *      own comment, semantic_analyzer.h), and is mutually exclusive
 *      with exception 1 above (an identifier is never one of the node
 *      shapes that narrows).
 *
 * Used wherever static checking needs to agree with what the ABI
 * boundary will actually construct: slorp<T>(T)->T's return_like_arg
 * identity (infer_expression_type()'s own NODE_FUNC_CALL case, above)
 * and native argument type-checking (semantic_check_native_call()). */
static VarType infer_expression_abi_type(ASTNode *expr,
                                         SemanticAnalyzer *analyzer)
{
    VarType t = infer_expression_type(expr, analyzer);
    if (!expr)
        return t;

    /* No StdrotType exists for an enum (stdrot_api.h's StdrotType enum
       has no ENUM member) -- ast_expr_to_stdrot_value()'s NODE_IDENTIFIER/
       VAR_ENUM case (stdrot.c) always marshals an enum-typed expression
       as STDROT_INT, the exact same representation a bare int gets.
       Every OTHER reachable enum-typed expression shape (array access,
       struct access -- there's no dedicated VAR_ENUM handling for either,
       so they fall to ast_expr_to_stdrot_value()'s unconditional
       NODE_ARRAY_ACCESS/NODE_STRUCT_ACCESS -> STDROT_INT catch-all)
       marshals the same way. Normalized here unconditionally, before the
       VAR_CHAR-specific handling below, so identity<T>(T)'s static T
       (infer_expression_type() alone still faithfully reports VAR_ENUM,
       which callers that care about the *source* type still want) can't
       diverge from the runtime T an enum argument/identity-return
       actually marshals as. */
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

    SymbolEntry *sym = find_symbol(analyzer, expr->data.name);
    if (sym && sym->is_array)
        return VAR_STRING;
    return t;
}

/* Round-19 review, finding #1 -- VAR_VOID (ast.h's own comment has the
 * full reasoning) is "known with certainty to produce no value," so
 * consuming one as a value anywhere -- an operator operand, a
 * condition, a switch discriminant, ... -- is never correct, the same
 * way a declared-type mismatch never is. Without a single shared check,
 * VAR_VOID stopped being enforced the moment it left the one context
 * (a declaration initializer) that happened to already reject it: a
 * binary operation's arithmetic-type validation only ever treated
 * VAR_STRING/VAR_BOOL as "clearly incompatible," so `yapping("hi") + 1`
 * fell through its `return true` default, got promoted to VAR_INT by
 * infer_expression_type()'s own widening rule (neither operand being
 * VAR_DOUBLE/VAR_FLOAT, matching VAR_INT's fallthrough), and type-
 * checked -- laundering "known to produce nothing" into a real int at
 * the type-checking level, even though nothing about VAR_VOID's own
 * definition changed. `if`/`while`/`do`-`while` conditions and a
 * `switch` discriminant had the identical gap: each recursively visits
 * its condition/discriminant expression, but never asked whether it
 * actually produced a value at all. One shared check here, called from
 * every value-consuming context below, instead of teaching each context
 * individually that void is bad. */
static bool require_value_expression(SemanticAnalyzer *analyzer, ASTNode *expr,
                                     const char *context_name)
{
    if (!expr)
        return true;
    /* Round-21 review, finding #1 -- a type is (base VarType, pointer_
       level), and that composite is what determines whether an
       expression genuinely produces no value, not the base type alone.
       (VAR_VOID, 0) is void -- no value, ever. (VAR_VOID, 1) is `void *`
       -- a perfectly real pointer value (e.g. test_ptr_source(), a
       STDROT_PTR-returning native, statically typed as VAR_VOID with
       pointer_level 1 by marshal_native_return_value()'s own STDROT_PTR
       branch, ast.c). Checking base type alone rejected `skibidi *p =
       test_ptr_source(); edgy (p) { ... }` -- a coherent, valid program
       under the very (base, pointer_level) model check_type_
       compatibility_ex() and semantic_visit_declaration() already use --
       as if `p` held no value at all. */
    if (infer_expression_type(expr, analyzer) != VAR_VOID ||
        infer_expression_pointer_level(expr, analyzer) > 0)
        return true;

    char error_msg[MAX_BUFFER_LEN];
    snprintf(error_msg, sizeof(error_msg),
             "Native call result (void) cannot be used in a %s context",
             context_name);
    add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                       STRING_LITERAL(error_msg),
                       expr->line_number > 0 ? expr->line_number : 1);
    return false;
}

/* Validate binary operation types */
bool validate_binary_operation(ASTNode *left, ASTNode *right, OperatorType op,
                               SemanticAnalyzer *analyzer)
{
    if (!require_value_expression(analyzer, left, "binary operator operand") ||
        !require_value_expression(analyzer, right, "binary operator operand"))
    {
        return false;
    }

    VarType left_type = infer_expression_type(left, analyzer);
    VarType right_type = infer_expression_type(right, analyzer);
    int left_pointer_level = infer_expression_pointer_level(left, analyzer);
    int right_pointer_level = infer_expression_pointer_level(right, analyzer);

    /* Skip validation if we can't determine types */
    if (left_type == NONE || right_type == NONE)
    {
        return true;
    }

    switch (op)
    {
    case OP_PLUS:
    case OP_MINUS:
        if (left_pointer_level > 0 || right_pointer_level > 0)
        {
            /* Round-23 review, finding #4 -- a single-level opaque
               pointer (VAR_PTR, this codebase's own "no concrete base
               type by design" native-pointer type -- see its own
               comment, ast.h -- or VAR_VOID/`void *`) has no known
               pointee size, so `ptr + n` has no defined element stride:
               get_type_size_for_descriptor() (ast.c) returns 0 for
               either base type at pointer_level 0 (the level the
               arithmetic's own pointee is AT, one below the pointer's
               own level 1), and evaluate_expression_pointer()'s own
               NODE_OPERATION case used to silently default an unknown
               scale to 1 -- meaning `test_ptr_source() + 1` quietly
               meant "advance exactly one byte" for a type the ABI
               explicitly documents as pointee-erased, not "the runtime
               genuinely doesn't know, so ask the type system before
               guessing." Rejected here, before arithmetic is even
               considered valid, for the identical reason a bare
               dereference of the same shape is already rejected
               (NODE_UNARY_OPERATION's own OP_DEREFERENCE case, above):
               a MULTI-level opaque pointer (`void **`, pointer_level >
               1) is NOT rejected by this -- arithmetic on it advances by
               sizeof(uintptr_t), a perfectly well-defined stride,
               regardless of what the innermost pointee eventually
               turns out to be. */
            if (left_pointer_level == 1 &&
                (left_type == VAR_PTR || left_type == VAR_VOID))
            {
                add_semantic_error(
                    analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                    STRING_LITERAL("Cannot perform pointer arithmetic on a "
                                   "type-erased pointer -- its pointee size "
                                   "is unknown"),
                    1);
                return false;
            }
            if (right_pointer_level == 1 &&
                (right_type == VAR_PTR || right_type == VAR_VOID))
            {
                add_semantic_error(
                    analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                    STRING_LITERAL("Cannot perform pointer arithmetic on a "
                                   "type-erased pointer -- its pointee size "
                                   "is unknown"),
                    1);
                return false;
            }
            if (left_pointer_level > 0 && right_pointer_level == 0 &&
                (right_type == VAR_INT || right_type == VAR_SHORT ||
                 right_type == VAR_CHAR || right_type == VAR_BOOL))
            {
                return true;
            }
            if (op == OP_PLUS && right_pointer_level > 0 &&
                left_pointer_level == 0 &&
                (left_type == VAR_INT || left_type == VAR_SHORT ||
                 left_type == VAR_CHAR || left_type == VAR_BOOL))
            {
                return true;
            }
            if (op == OP_MINUS && left_pointer_level > 0 &&
                right_pointer_level > 0 &&
                left_pointer_level == right_pointer_level &&
                left_type == right_type)
            {
                return true;
            }
            add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                               STRING_LITERAL("Invalid pointer arithmetic"), 1);
            return false;
        }
        /* fallthrough */
    case OP_TIMES:
    case OP_DIVIDE:
    case OP_MOD:
        /* Arithmetic operations require numeric types */
        if ((left_type == VAR_INT || left_type == VAR_SHORT ||
             left_type == VAR_FLOAT || left_type == VAR_DOUBLE ||
             left_type == VAR_ENUM) &&
            (right_type == VAR_INT || right_type == VAR_SHORT ||
             right_type == VAR_FLOAT || right_type == VAR_DOUBLE ||
             right_type == VAR_ENUM))
        {
            return true;
        }
        else
        {
            /* Only report errors for really incompatible types */
            if ((left_type == VAR_STRING || left_type == VAR_BOOL) ||
                (right_type == VAR_STRING || right_type == VAR_BOOL))
            {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg),
                         "Arithmetic operation requires numeric types, got %s "
                         "and %s",
                         vartype_to_string(left_type),
                         vartype_to_string(right_type));
                add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                   STRING_LITERAL(error_msg), 1);
                return false;
            }
            return true; /* Allow most numeric combinations */
        }

    case OP_EQ:
    case OP_NE:
        if (left_pointer_level > 0 || right_pointer_level > 0)
        {
            return left_pointer_level == right_pointer_level &&
                   left_type == right_type;
        }
        /* Equality comparisons allow same or compatible types */
        return true; /* Be more lenient for equality */

    case OP_LT:
    case OP_GT:
    case OP_LE:
    case OP_GE:
        if (left_pointer_level > 0 || right_pointer_level > 0)
        {
            return left_pointer_level == right_pointer_level &&
                   left_type == right_type;
        }
        /* Relational comparisons require numeric types */
        if ((left_type == VAR_INT || left_type == VAR_SHORT ||
             left_type == VAR_FLOAT || left_type == VAR_DOUBLE ||
             left_type == VAR_ENUM) &&
            (right_type == VAR_INT || right_type == VAR_SHORT ||
             right_type == VAR_FLOAT || right_type == VAR_DOUBLE ||
             right_type == VAR_ENUM))
        {
            return true;
        }

        /* Only report errors for clearly incompatible types */
        if ((left_type == VAR_STRING || left_type == VAR_BOOL) ||
            (right_type == VAR_STRING || right_type == VAR_BOOL))
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "Relational comparison requires numeric types, got %s "
                     "and %s",
                     vartype_to_string(left_type),
                     vartype_to_string(right_type));
            add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                               STRING_LITERAL(error_msg), 1);
            return false;
        }
        return true;

    // Same effect as `default` below, kept as an explicit case to document
    // that logical ops accepting any type is deliberate, not an unhandled
    // operator falling through.
    case OP_AND: // NOLINT(bugprone-branch-clone)
    case OP_OR:  // NOLINT(bugprone-branch-clone)
        /* Logical operations work with any type (truthiness) */
        return true;

    default:
        return true;
    }
}

/* Visitor method implementations */

void *semantic_visit_identifier(Visitor *self, ASTNode *node)
{
    SemanticAnalyzer *analyzer = (SemanticAnalyzer *)self;

    if (!node || !node->data.name.data)
        return NULL;

    if (analyzer->is_collecting_phase)
        return NULL;

    const String name = node->data.name;
    SymbolEntry *symbol = find_symbol(analyzer, name);

    if (!symbol)
    {
        SymbolEntry *entry = analyzer->symbol_table;
        bool found_in_deeper_scope = false;

        while (entry)
        {
            if (strcmp(entry->name.data, name.data) == 0)
            {
                if (entry->scope_depth > analyzer->scope_depth)
                {
                    found_in_deeper_scope = true;
                    break;
                }
            }
            entry = entry->next;
        }

        if (found_in_deeper_scope)
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "Variable '%s' is out of scope", name.data);
            add_semantic_error(analyzer, SEMANTIC_ERROR_SCOPE_ERROR,
                               STRING_LITERAL(error_msg),
                               node->line_number > 0 ? node->line_number : 1);
        }
        else
        {
            Variable *var = get_variable(name);
            if (!var)
            {
                if (!is_builtin_function(name) &&
                    !find_global_enum_constant(name))
                {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg),
                             "Undefined variable '%s'", name.data);
                    add_semantic_error(
                        analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE,
                        STRING_LITERAL(error_msg),
                        node->line_number > 0 ? node->line_number : 1);
                }
            }
        }
    }

    return NULL;
}

/* True when `expr` is a non-char array identifier -- Variable's own value
 * union (ast.h) aliases a scalar's storage with an array's backing
 * pointer, so any code path that reads that union as a scalar (int,
 * short, float, double, bool, or an enum, all of which ast_expr_to_
 * stdrot_value() marshals by reading the scalar member unconditionally)
 * would actually read the array's backing pointer's raw bytes instead --
 * an address silently reinterpreted as whatever scalar type the
 * descriptor declared. VAR_CHAR arrays are the one exception: they have
 * a real StdrotValue representation (STDROT_STRING, via ast_expr_to_
 * stdrot_value()'s own VAR_CHAR/is_array branch), so they're excluded
 * here and handled by ordinary type-compatibility checking instead.
 *
 * find_symbol(), not get_variable(): this runs during the single static
 * analysis pass, before the interpreter creates any runtime Variable
 * (see semantic_analyze()'s own "Phase 1"/"Phase 2" comment) --
 * SymbolEntry.is_array (semantic_analyzer.h) is the field actually
 * populated at this point. */
static bool is_unmarshallable_array_arg(ASTNode *expr,
                                        SemanticAnalyzer *analyzer)
{
    if (!expr || expr->type != NODE_IDENTIFIER)
        return false;

    SymbolEntry *sym = find_symbol(analyzer, expr->data.name);
    return sym && sym->is_array && sym->type != VAR_CHAR;
}

/* True when `expr` has NO valid StdrotValue representation ast_expr_to_
 * stdrot_value() (stdrot.c) can honestly construct for it -- generalizes
 * is_unmarshallable_array_arg() from "one specific representation bug"
 * (a non-char array's backing pointer read as a scalar) to the actual
 * invariant: does a real StdrotValue exist for this expression at all.
 * That function's own closed accepted-type comment (VAR_INT/SHORT/FLOAT/
 * DOUBLE/BOOL/CHAR/STRING/ENUM) already states the rule; this is that
 * same rule applied everywhere an expression needs to become a native
 * argument, not just where STDROT_ANY happens to ask.
 *
 * A struct identifier is the clearest failure case: ast_expr_to_stdrot_
 * value() has no VAR_STRUCT branch in its NODE_IDENTIFIER switch, so
 * out->type survives at its initialized STDROT_NONE -- the native
 * receives "no value" where source code plainly supplied one, and (for
 * a variadic consumer with no fixed parameter to have rejected this
 * argument earlier) nothing downstream would otherwise notice.
 *
 * A pointer-level expression is NOT flagged here, unlike STDROT_ANY's
 * own additional pointer rejection (semantic_check_native_call()'s
 * STDROT_ANY branch) -- a pointer marshals honestly as STDROT_PTR, a
 * real representation, just one STDROT_ANY specifically refuses to
 * accept for its own reasons (no scalar/string to dispatch on). Callers
 * that need STDROT_ANY's stricter rule check pointer-level themselves,
 * on top of this.
 *
 * Returns false (does not flag) when the expression's static type is
 * NONE -- genuinely unknowable ahead of time, e.g. an argument that is
 * itself a legacy STDROT_ANY-returning call whose real runtime type
 * this analyzer cannot see. Failing open there matches every other
 * static check in this analyzer; it is NOT a soundness gap, because
 * execute_native_call() (stdrot.c) independently rejects a variadic-tail
 * argument that marshals to STDROT_NONE at the runtime ABI boundary --
 * the same reasoning enforce_arg_type() already applies to fixed
 * parameters, now covering the case no static check could. */
static bool is_unmarshallable_expr(ASTNode *expr, SemanticAnalyzer *analyzer)
{
    if (is_unmarshallable_array_arg(expr, analyzer))
        return true;

    if (infer_expression_pointer_level(expr, analyzer) > 0)
        return false;

    switch (infer_expression_type(expr, analyzer))
    {
    case VAR_INT:
    case VAR_SHORT:
    case VAR_FLOAT:
    case VAR_DOUBLE:
    case VAR_BOOL:
    case VAR_CHAR:
    case VAR_STRING:
    case VAR_ENUM:
    case NONE:
        return false;
    default:
        return true;
    }
}

/* Builds a throwaway literal node whose sole purpose is to carry `type`
   through the existing return_like_arg machinery as a zero-argument
   identity-polymorphic call's (currently only slorp()) type witness -- its
   VALUE is never read (see stdrot/slorp.c: every slorp_TYPE() ignores the
   value it's handed and only uses argument 0 to select which slorp_TYPE()
   to call). Returns NULL for a type this contextual form doesn't support
   (VAR_STRING/rant included -- a scalar rant needs dynamic allocation,
   issue #144, not just a type tag; the buffer form `slorp(yap_buf)` still
   covers strings). */
static ASTNode *create_type_witness_node(VarType type)
{
    switch (type)
    {
    case VAR_INT:
        return create_int_node(0);
    case VAR_SHORT:
        return create_short_node(0);
    case VAR_FLOAT:
        return create_float_node(0.0f);
    case VAR_DOUBLE:
        return create_double_node(0.0);
    case VAR_BOOL:
        return create_boolean_node(false);
    case VAR_CHAR:
        return create_char_node(0);
    default:
        return NULL;
    }
}

/* Resolves a zero-argument, identity-polymorphic native call (slorp<T>() ->
   T, marked by StdrotEntry.return_like_arg) from `expected`, a type already
   statically known at this AST position (a declaration's declared type, an
   assignment target's type, an enclosing function's return type, or a
   typed parameter's type). On success, rewrites `node` in place to carry a
   synthetic type-witness argument (see create_type_witness_node() above),
   so every existing return_like_arg-based check -- infer_expression_type(),
   semantic_check_native_call(), and, at runtime, ast_expr_to_stdrot_value()
   -- keeps working completely unmodified: `slorp()` becomes indistinguishable
   from `slorp(<value of the right type>)` everywhere downstream.

   Does nothing if `node` isn't such a call, or already has an argument (an
   explicit `slorp(x)` call, or a call this function already resolved --
   idempotent, so it's safe to call from more than one context site without
   worrying about re-entry).

   Sets `node->contextual_type_hint` to `expected` even when resolution
   fails (a pointer-typed or otherwise unsupported `expected`) -- a
   diagnostic-only scratch field (see its own comment, ast.h), never
   `node->var_type`, so semantic_visit_function_call()'s still-unresolved
   check can report a specific reason (e.g. "rant needs a buffer") without
   any other pass mistaking this call for one that actually resolved.
   Callers that compare a still-unresolved call's type/pointer-level
   against a declared/target type (semantic_visit_declaration,
   semantic_visit_assignment, the NODE_RETURN case) must skip that
   comparison instead -- see is_unresolved_contextual_call() below -- since
   infer_expression_type()/infer_expression_pointer_level() correctly still
   report NONE/0 for a call with no witness attached, and comparing that
   against the real declared type would misreport a second, redundant
   error on top of the one this function's caller already lets
   semantic_visit_function_call() raise. */
static void propagate_contextual_call_type(ASTNode *node, VarType expected,
                                           int expected_pointer_level)
{
    if (!node || node->type != NODE_FUNC_CALL || node->data.func_call.arguments)
        return;

    const StdrotEntry *entry =
        get_native_function(node->data.func_call.function_name);
    if (!entry || entry->return_like_arg < 0)
        return;

    node->contextual_type_hint = expected;

    if (expected_pointer_level != 0)
        return;

    ASTNode *witness = create_type_witness_node(expected);
    if (!witness)
        return;

    node->data.func_call.arguments = create_argument_list(witness, NULL);
}

/* True when `node` is a zero-argument identity-polymorphic native call
   (slorp<T>() -> T) that propagate_contextual_call_type() either never
   saw or saw and couldn't resolve -- i.e. a call semantic_visit_function_
   call() will (or already did) report its own "cannot infer type"
   diagnostic for. Declaration/assignment/return type-compatibility checks
   use this to skip comparing such a call's (necessarily NONE/0)
   inferred type/pointer-level against the real declared type, which would
   otherwise raise a second, misleading error alongside the one already
   raised for the call itself -- see propagate_contextual_call_type()'s own
   comment above. */
static bool is_unresolved_contextual_call(ASTNode *node)
{
    if (!node || node->type != NODE_FUNC_CALL || node->data.func_call.arguments)
        return false;

    const StdrotEntry *entry =
        get_native_function(node->data.func_call.function_name);
    return entry && entry->return_like_arg >= 0;
}

/* Type-checks a native call's fixed/checked argument prefix against its
   registered StdrotEntry -- arity plus, for each checked parameter actually
   supplied, that the argument's inferred type is compatible. STDROT_ANY
   parameters (e.g. slorp's) accept anything. Arguments beyond param_count
   are only reached when is_variadic is true, and are left unchecked
   (format-string tails, legacy/untyped exports). */
static void semantic_check_native_call(SemanticAnalyzer *analyzer,
                                       ASTNode *node, const StdrotEntry *entry)
{
    const String func_name = node->data.func_call.function_name;

    int arg_count = 0;
    ArgumentList *cur = node->data.func_call.arguments;
    while (cur)
    {
        arg_count++;
        cur = cur->next;
    }

    int line = node->line_number > 0 ? node->line_number : 1;

    bool arity_ok = entry->is_variadic ? arg_count >= entry->min_args
                                       : (arg_count >= entry->min_args &&
                                          arg_count <= entry->param_count);
    if (!arity_ok)
    {
        char error_msg[MAX_BUFFER_LEN];
        if (entry->is_variadic || entry->min_args == entry->param_count)
        {
            int n = entry->is_variadic ? entry->min_args : entry->param_count;
            snprintf(error_msg, sizeof(error_msg),
                     "'%s' expects %s%d argument%s, got %d", func_name.data,
                     entry->is_variadic ? "at least " : "", n,
                     n == 1 ? "" : "s", arg_count);
        }
        else
        {
            snprintf(error_msg, sizeof(error_msg),
                     "'%s' expects between %d and %d arguments, got %d",
                     func_name.data, entry->min_args, entry->param_count,
                     arg_count);
        }
        add_semantic_error(analyzer, SEMANTIC_ERROR_ARITY_MISMATCH,
                           STRING_LITERAL(error_msg), line);
        return;
    }

    if (entry->param_count > 0 && !entry->params)
    {
        /* A malformed STDROT_EXPORT_SIG (param_count > 0 but no params
           array) -- reject the call outright rather than silently skipping
           type checks on it, since accepting it unchecked would be
           fail-open on exactly the thing this analyzer exists to catch. */
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "'%s' has an inconsistent native signature (param_count "
                 "> 0 but no params declared)",
                 func_name.data);
        add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                           STRING_LITERAL(error_msg), line);
        return;
    }

    if (entry->return_type.type == STDROT_HANDLE)
    {
        /* STDROT_HANDLE exists in the ABI as reserved groundwork (see
           stdrot_api.h) but nothing marshals a handle-valued return yet,
           and -- unlike STDROT_PTR -- a handle isn't just a raw address:
           it needs a resource-identity/ownership model (type_name-based
           type checking, who frees it, GC vs manual release) that Phase 2
           deliberately hasn't designed yet (see the roadmap's Appendix B
           Q6, "ownership of native resources"). Silently treating that as
           unchecked would make the descriptor decorative -- claim a type
           it can't actually deliver. Reject it outright instead of
           guessing at a design that hasn't been made. STDROT_PTR (a plain
           opaque address, which this pipeline *can* honestly represent
           with the existing pointer_level machinery) is handled below,
           not rejected. */
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "'%s': native handle return types are not marshalled yet",
                 func_name.data);
        add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                           STRING_LITERAL(error_msg), line);
        return;
    }

    if (entry->return_type.type == STDROT_CSTRING)
    {
        /* STDROT_CSTRING is argument-side ABI groundwork only: coerce_arg_
           to_param() knows how to convert a Brainrot String INTO a const
           char* for a native to consume, but marshal_native_return_value()
           (ast.c) has no code path the other way -- a native returning
           STDROT_CSTRING falls through its switch doing nothing, leaving
           current_return_value.value.strvalue whatever stale/zeroed state
           happened to already be in that union. stdrot_type_to_vartype()
           still honestly maps STDROT_CSTRING to VAR_STRING, so without
           this check the analyzer would approve `chungus s = a_cstring_
           native();` as a real string and hand the interpreter a value
           that was never actually constructed. Same reasoning as the
           STDROT_HANDLE rejection above: reject outright rather than let
           the descriptor advertise a return representation the final
           marshalling layer cannot produce. */
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "'%s': native CSTRING return types are not marshalled yet",
                 func_name.data);
        add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                           STRING_LITERAL(error_msg), line);
        return;
    }

    cur = node->data.func_call.arguments;
    for (int i = 0; i < entry->param_count && cur; i++, cur = cur->next)
    {
        const StdrotParam *param = &entry->params[i];
        if (!cur->expr)
            continue;

        /* A typed native parameter (anything but STDROT_ANY/PTR/HANDLE,
           none of which name a single concrete type) is a "typed
           argument" context per propagate_contextual_call_type()'s own
           contract -- resolves a zero-argument `slorp()` passed here
           (e.g. a hypothetical `some_native(slorp())`) before the type
           checks below run, so they see the desugared 1-argument call
           like any other. A no-op for anything that isn't such a call. */
        if (param->type != STDROT_ANY && param->type != STDROT_PTR &&
            param->type != STDROT_HANDLE)
        {
            propagate_contextual_call_type(cur->expr,
                                           stdrot_type_to_vartype(param->type),
                                           param->pointer_level);
        }

        /* A still-unresolved contextual `slorp()` argument -- either the
           propagate attempt just above never applied (an STDROT_PTR/ANY/
           HANDLE param has no single concrete VarType to give it) or it
           applied and failed (an unsupported concrete type) -- already
           gets its own "cannot infer type" error from semantic_visit_
           function_call() when the argument-list recursion below (this
           switch's own NODE_FUNC_CALL case) reaches it. Checking its
           necessarily NONE/0 inferred type/pointer-level against this
           parameter here -- HANDLE, PTR, ANY, or the generic scalar
           branch alike -- would raise a second, misleading error for the
           same argument (e.g. `peek_int(slorp());`, an STDROT_PTR param,
           used to print both "cannot infer type" and "expected a pointer
           (level 1), got pointer level 0"); skip straight to the next
           parameter instead, matching semantic_visit_declaration()'s/
           semantic_visit_assignment()'s/the NODE_RETURN case's identical
           guard (see is_unresolved_contextual_call()'s own comment). */
        if (is_unresolved_contextual_call(cur->expr))
            continue;

        if (param->type == STDROT_HANDLE)
        {
            /* Same reasoning as the return-type check above. */
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "'%s' argument %d: native handle parameters are not "
                     "marshalled yet",
                     func_name.data, i + 1);
            add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                               STRING_LITERAL(error_msg), line);
            continue;
        }

        if (param->type == STDROT_PTR)
        {
            /* An opaque pointer parameter accepts a pointer of *any* base
               type at the right indirection depth -- erased uniformly at
               every depth, not just C void*'s one safe level (see
               STDROT_PTR's own comment in stdrot_api.h for why) -- so
               this checks pointer_level only, deliberately skipping the
               base-VarType comparison below (there is no base type to
               compare; that's what "opaque" means). param->pointer_level
               counts indirection *beyond* the one level STDROT_PTR itself
               already represents (a plain STDROT_PTR argument is
               pointer_level 1). */
            int actual_pl = infer_expression_pointer_level(cur->expr, analyzer);
            int expected_pl = param->pointer_level + 1;
            if (actual_pl != expected_pl)
            {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg),
                         "'%s' argument %d: expected a pointer (level %d), "
                         "got pointer level %d",
                         func_name.data, i + 1, expected_pl, actual_pl);
                add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                   STRING_LITERAL(error_msg), line);
            }
            continue;
        }

        if (param->type == STDROT_ANY)
        {
            /* STDROT_ANY means "any of the scalar/string types this
               pipeline can already marshal" (int/short/float/double/bool/
               char/string) -- not "any value representable at all,
               pointers included." A pointer-level argument has no
               scalar/string representation to dispatch on (slorp, the
               only STDROT_ANY consumer today, has no notion of "read a
               pointer from stdin"), so without this check a pointer would
               silently sail through the STDROT_ANY shortcut and reach the
               native function tagged as whatever the generic non-pointer
               scalar fallback in ast_expr_to_stdrot_value() produced from
               its raw address -- not a pointer at all. An opaque-pointer
               parameter must be declared STDROT_PTR explicitly instead. */
            if (infer_expression_pointer_level(cur->expr, analyzer) > 0)
            {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg),
                         "'%s' argument %d: pointer arguments require an "
                         "explicit STDROT_PTR parameter, not STDROT_ANY",
                         func_name.data, i + 1);
                add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                   STRING_LITERAL(error_msg), line);
                continue;
            }

            /* The comment above claims a specific closed set of accepted
               types -- enforce it, rather than "not a pointer" being the
               only check. ast_expr_to_stdrot_value() (stdrot.c) only
               knows how to construct a StdrotValue for VAR_INT/SHORT/
               FLOAT/DOUBLE/BOOL/CHAR/STRING/ENUM; anything else (a struct
               identifier, most notably) falls through its switch leaving
               out->type at its initialized STDROT_NONE, silently handing
               the native "no value" instead of being rejected here before
               the call is even approved. */
            VarType actual_any = infer_expression_type(cur->expr, analyzer);
            switch (actual_any)
            {
            case VAR_INT:
            case VAR_SHORT:
            case VAR_FLOAT:
            case VAR_DOUBLE:
            case VAR_BOOL:
            case VAR_CHAR:
            case VAR_STRING:
            case VAR_ENUM:
                break;
            default:
            {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg),
                         "'%s' argument %d: %s is not a scalar/string type "
                         "this native can accept",
                         func_name.data, i + 1, vartype_to_string(actual_any));
                add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                   STRING_LITERAL(error_msg), line);
                continue;
            }
            }

            /* See is_unmarshallable_array_arg()'s own comment for the
               union-aliasing hazard this guards against: arrays report
               the same VarType as their element (`rizz arr[2]`'s
               var_type is VAR_INT, indistinguishable from a scalar
               `rizz x` by infer_expression_type() alone), but passing
               one through would reinterpret its backing pointer as a
               scalar value. */
            if (is_unmarshallable_array_arg(cur->expr, analyzer))
            {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg),
                         "'%s' argument %d: %s arrays cannot be passed "
                         "where a scalar/string is expected",
                         func_name.data, i + 1, vartype_to_string(actual_any));
                add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                   STRING_LITERAL(error_msg), line);
            }
            continue;
        }

        VarType expected = stdrot_type_to_vartype(param->type);
        /* infer_expression_abi_type(), not infer_expression_type(): a
           STDROT_STRING/STDROT_CSTRING param maps to VAR_STRING here
           (stdrot_type_to_vartype()), and the runtime bridge already
           accepts a char-array argument for either (ast_expr_to_stdrot_
           value()'s VAR_CHAR/is_array case produces STDROT_STRING;
           coerce_arg_to_param() already knows STDROT_STRING ->
           STDROT_CSTRING) -- rejecting it here on the strength of plain
           infer_expression_type() reporting VAR_CHAR (the array's
           element type) would be a false negative against a call the
           ABI can actually service, exactly the same representation gap
           slorp's return_like_arg identity has (see infer_expression_
           abi_type()'s own comment). */
        VarType actual = infer_expression_abi_type(cur->expr, analyzer);
        int actual_pointer_level =
            infer_expression_pointer_level(cur->expr, analyzer);

        /* Same union-aliasing hazard the STDROT_ANY branch above already
           guards against (see is_unmarshallable_array_arg()'s own
           comment) -- a FIXED, non-ANY scalar parameter is just as
           vulnerable: infer_expression_abi_type() reports a non-char
           array identifier's ELEMENT type (e.g. VAR_INT for `rizz
           arr[2]`), indistinguishable here from a scalar `rizz x`, and
           check_type_compatibility_ex() below has no way to know the
           argument is actually an array backed by a pointer the runtime
           marshaller would read as if it were that scalar's own value.
           Checked before the ordinary type-compatibility check, not
           folded into it, so this fires with a clear "array, not a
           scalar" diagnostic instead of a confusing "expected int, got
           int" non-mismatch. */
        if (is_unmarshallable_array_arg(cur->expr, analyzer))
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg),
                     "'%s' argument %d: %s arrays cannot be passed where a "
                     "scalar/string is expected",
                     func_name.data, i + 1, vartype_to_string(actual));
            add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                               STRING_LITERAL(error_msg), line);
            continue;
        }

        if (expected == NONE || actual == NONE)
            continue;

        if (!check_type_compatibility_ex(expected, param->pointer_level, actual,
                                         actual_pointer_level))
        {
            char error_msg[MAX_BUFFER_LEN];
            /* Round-21 review, finding #2 -- when the base VarType
               strings are identical (e.g. a pointer-typed struct field
               like `rizz *p;` reports the same base VAR_INT a plain
               `rizz` parameter does), the mismatch is entirely in
               pointer_level, and the plain "expected %s, got %s" form
               below prints the nonsensical "expected int, got int" --
               correct about THERE being a mismatch (check_type_
               compatibility_ex() above already used actual_pointer_level
               to find it), useless about WHAT it actually is. Reported
               explicitly whenever either side is a pointer, matching the
               pointer-specific mismatch messages elsewhere in this
               file. */
            if (param->pointer_level > 0 || actual_pointer_level > 0)
            {
                snprintf(error_msg, sizeof(error_msg),
                         "'%s' argument %d: expected %s (pointer level %d), "
                         "got %s (pointer level %d)",
                         func_name.data, i + 1, vartype_to_string(expected),
                         param->pointer_level, vartype_to_string(actual),
                         actual_pointer_level);
            }
            else
            {
                snprintf(error_msg, sizeof(error_msg),
                         "'%s' argument %d: expected %s, got %s",
                         func_name.data, i + 1, vartype_to_string(expected),
                         vartype_to_string(actual));
            }
            add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                               STRING_LITERAL(error_msg), line);
        }
    }

    /* `cur` now sits at the first argument beyond param_count -- the
       unchecked variadic tail (format-string arguments, or every
       argument to a legacy/untyped STDROT_EXPORT() export, which is
       also is_variadic == true with param_count == 0, see promote_
       variadic_tail's own comment for why those two are NOT the same
       concept). Its TYPE is deliberately left unchecked -- the whole
       point of is_variadic is that this pipeline doesn't know what type
       each tail argument ought to be -- but "unchecked type" is not the
       same claim as "unchecked representation validity": is_
       unmarshallable_expr() (its own comment has the full reasoning)
       still has to hold for every argument that reaches ast_expr_to_
       stdrot_value(), fixed-parameter or not. Deliberately worded as
       "has no supported native ABI representation", not "is a variadic
       argument" or "cannot be passed to a variadic argument" -- this
       same check, and this same message, applies whether entry is a
       genuine C-style variadic native or a legacy/untyped export with
       an unrelated reason for leaving its arity unchecked; calling both
       "variadic" here would blur exactly the distinction promote_
       variadic_tail exists to keep separate. */
    if (entry->is_variadic)
    {
        int tail_index = entry->param_count;
        for (; cur; cur = cur->next, tail_index++)
        {
            if (!cur->expr)
                continue;

            if (is_unmarshallable_expr(cur->expr, analyzer))
            {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg),
                         "'%s' argument %d: %s has no supported native ABI "
                         "representation",
                         func_name.data, tail_index + 1,
                         vartype_to_string(
                             infer_expression_type(cur->expr, analyzer)));
                add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                   STRING_LITERAL(error_msg), line);
            }
        }
    }
}

/* Resolves a zero-argument `slorp()` at every leaf of a braced initializer
   (`rizz arr[2] = { slorp(), 1 };`, and nested sublists for `{ {1,2}, 3 }`-
   style matrix init) against `expected` -- the declaration's own element
   type/pointer_level, the same for every leaf regardless of depth, since
   VarType carries no per-dimension element type of its own (an array's
   var_type/pointer_level already describe its ELEMENT, not the array
   itself -- see infer_expression_type()'s own comments on that
   representation). Must run before semantic_check_expression_list() walks
   the same list below, exactly like propagate_contextual_call_type()'s
   other call sites must run before their own recursive walk. */
static void propagate_contextual_type_into_expression_list(
    ExpressionList *list, VarType expected, int expected_pointer_level)
{
    if (!list)
        return;

    /* Same circular-list traversal as semantic_check_expression_list()
       below -- see its own comment for why this can't stop on NULL. */
    ExpressionList *current = list;
    do
    {
        if (current->expr)
        {
            propagate_contextual_call_type(current->expr, expected,
                                           expected_pointer_level);
        }
        else if (current->sublist)
        {
            propagate_contextual_type_into_expression_list(
                current->sublist, expected, expected_pointer_level);
        }
        current = current->next;
    } while (current != list);
}

/* Same job as propagate_contextual_type_into_expression_list() above, but
   for a braced STRUCT initializer (`gang Point p = { slorp(), 1 };`),
   whose leaves are NOT homogeneous -- each one has its own field's type,
   not one shared element type. `field` walks StructDef.fields in lockstep
   with `list`, positionally (this grammar has no designated initializers,
   see struct_field/struct_initializer_list in lang.y), so leaf i gets
   field i's (type, pointer_level). A nested sublist (`gang Outer o = {
   {1, 2}, 3 };`, a struct-typed field with its own braced sub-initializer)
   recurses using THAT field's own struct definition, mirroring validate_
   struct_initializer_shape()'s (ast.c) shape-only check of the same
   nesting at parse time. `field` NULL (no StructDef resolved for the tag
   at all -- e.g. an unregistered/misspelled struct name -- or fewer
   fields than leaves, a shape mismatch validate_struct_initializer_
   shape() at parse time already rejects most instances of) leaves the
   remaining/all leaves untouched, same as propagate_contextual_call_
   type() no-op'ing on any node it doesn't recognize. */
static void
propagate_contextual_type_into_struct_initializer(ExpressionList *list,
                                                  StructField *field)
{
    if (!list)
        return;

    ExpressionList *current = list;
    do
    {
        if (field)
        {
            if (current->expr)
            {
                propagate_contextual_call_type(current->expr, field->type,
                                               field->pointer_level);
            }
            else if (current->sublist && field->type == VAR_STRUCT &&
                     field->pointer_level == 0)
            {
                StructDef *nested_def = get_struct_def(field->struct_name);
                propagate_contextual_type_into_struct_initializer(
                    current->sublist, nested_def ? nested_def->fields : NULL);
            }
            field = field->next;
        }
        current = current->next;
    } while (current != list);
}

/* Validates each leaf of a by-value struct's braced initializer (`gang
   Holder h = {&r};`) against its own field's declared type -- the
   validation counterpart of propagate_contextual_type_into_struct_
   initializer() just above, which only ever propagates a contextual TYPE
   HINT into slorp()-shaped leaves and never compares an already-typed
   leaf (like `&r`) against the field it initializes. Walks StructField in
   the identical positional lockstep as that function, and recurses into a
   nested-struct-typed field's own sublist the same way -- so a
   pointer-typed field nested arbitrarily deep (`gang Outer o = { {&r} };`)
   is still checked, not just a top-level one. Only pointer-typed
   struct/union fields are checked (category via infer_expression_type/
   pointer_level, tag via check_pointer_struct_tag_match()) -- a by-value
   struct-typed field's own tag mismatch remains the separate,
   pre-existing, runtime-only gap check_declaration_initializer_
   compatibility()'s own comment already documents as out of scope. PR
   #248 review, round 5, finding 1: `gang Holder { gang Point *pt; }; gang
   Holder h = {&r};` stored a Rect* into a field every later read/call
   treats as Point*, with no analyzer-time check at all until now. */
static void check_struct_initializer_pointer_tags(SemanticAnalyzer *analyzer,
                                                  ExpressionList *list,
                                                  StructField *field, int line)
{
    if (!list || !field)
        return;

    ExpressionList *current = list;
    StructField *fld = field;
    do
    {
        if (fld)
        {
            if (current->expr && fld->type == VAR_STRUCT &&
                fld->pointer_level > 0 &&
                !is_unresolved_contextual_call(current->expr))
            {
                VarType elem_type =
                    infer_expression_type(current->expr, analyzer);
                int elem_pl =
                    infer_expression_pointer_level(current->expr, analyzer);
                if ((elem_type != NONE && elem_type != VAR_STRUCT) ||
                    elem_pl != fld->pointer_level)
                {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg),
                             "Type mismatch initializing field '%s': "
                             "expected pointer to struct/union '%s' (level "
                             "%d), got %s pointer level %d",
                             fld->name.data ? fld->name.data : "?",
                             fld->struct_name.data ? fld->struct_name.data
                                                   : "?",
                             fld->pointer_level, vartype_to_string(elem_type),
                             elem_pl);
                    add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                       STRING_LITERAL(error_msg), line);
                }
                else
                {
                    char prefix[MAX_BUFFER_LEN];
                    snprintf(prefix, sizeof(prefix),
                             "Type mismatch initializing field '%s'",
                             fld->name.data ? fld->name.data : "?");
                    check_pointer_struct_tag_match(analyzer, fld->struct_name,
                                                   current->expr, prefix, line);
                }
            }
            else if (current->sublist && fld->type == VAR_STRUCT &&
                     fld->pointer_level == 0)
            {
                StructDef *nested_def = get_struct_def(fld->struct_name);
                check_struct_initializer_pointer_tags(
                    analyzer, current->sublist,
                    nested_def ? nested_def->fields : NULL, line);
            }
            fld = fld->next;
        }
        current = current->next;
    } while (current != list);
}

/* Walks a braced initializer's expression list (`{1, 2, bet(2)}`, and
   nested sublists for `{ {1, 2}, 3 }`-style matrix init), running the same
   checks -- native-call arity/type included -- on every leaf expression as
   semantic_analyze_with_scope_tracking() runs on any other expression. */
static void semantic_check_expression_list(SemanticAnalyzer *analyzer,
                                           ExpressionList *list)
{
    if (!list)
        return;

    /* ExpressionList is a circular doubly-linked list (see
       create_expression_list()/append_expression_list_node() in ast.c --
       a single element's next/prev both point back to itself), not
       NULL-terminated, so this has to stop on returning to the start
       (matching count_expression_list()/free_expression_list()'s own
       traversal), not on next == NULL. */
    ExpressionList *current = list;
    do
    {
        if (current->expr)
        {
            semantic_analyze_with_scope_tracking(analyzer, current->expr);
        }
        else if (current->sublist)
        {
            semantic_check_expression_list(analyzer, current->sublist);
        }
        current = current->next;
    } while (current != list);
}

void *semantic_visit_function_call(Visitor *self, ASTNode *node)
{
    SemanticAnalyzer *analyzer = (SemanticAnalyzer *)self;

    if (!node || !node->data.func_call.function_name.data)
        return NULL;

    const String func_name = node->data.func_call.function_name;
    const StdrotEntry *native_entry = get_native_function(func_name);

    if (native_entry)
    {
        /* A zero-argument identity-polymorphic call (slorp()) that no
           context site (NODE_DECLARATION/NODE_ASSIGNMENT/NODE_RETURN
           cases, or the typed-native-parameter check above, all in this
           file) managed to resolve via propagate_contextual_call_type().
           `slorp`'s own entry keeps min_args == 1 (STDROT_EXPORT_SIG_
           IDENTITY, stdrot/slorp.c) -- validate_native_registry() requires
           return_like_arg to name a MANDATORY argument, so min_args == 0
           would abort stdrot_load() outright, before any Brainrot program
           runs. Zero AST-level arguments is therefore never a legal arity
           for this call; it's reported here, ahead of semantic_check_
           native_call()'s own arity check, purely to give a better
           diagnostic than that check's generic "expects 1 argument, got
           0" -- naming the missing piece (a typed context) instead of a
           raw count, and picking out the rant/#144 case by name via
           node->contextual_type_hint (diagnostic-only; never node->
           var_type, which every other pass treats as this node's actual
           resolved type -- see both fields' own comments). */
        if (native_entry->return_like_arg >= 0 &&
            !node->data.func_call.arguments)
        {
            char error_msg[MAX_BUFFER_LEN];
            if (node->contextual_type_hint == VAR_STRING)
            {
                snprintf(error_msg, sizeof(error_msg),
                         "'%s()' cannot infer a scalar 'rant' result from "
                         "context -- pass a 'yap[N]' buffer instead (e.g. "
                         "%s(buf))",
                         func_name.data, func_name.data);
            }
            else
            {
                snprintf(error_msg, sizeof(error_msg),
                         "cannot infer type for %s(); use it in a typed "
                         "context",
                         func_name.data);
            }
            add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                               STRING_LITERAL(error_msg),
                               node->line_number > 0 ? node->line_number : 1);
            return NULL;
        }

        semantic_check_native_call(analyzer, node, native_entry);
    }
    else
    {
        Function *func = get_function(func_name);
        if (!func)
        {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg), "Undefined function '%s'",
                     func_name.data);
            add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_FUNCTION,
                               STRING_LITERAL(error_msg),
                               node->line_number > 0 ? node->line_number : 1);
        }
        else
        {
            /* Same "typed argument" context as the native-parameter check
               above, for a user-defined function's declared parameter
               types -- resolves a zero-argument `slorp()` passed as an
               argument here (e.g. `takes_int(slorp())`) before the
               generic per-argument recursion (this function's caller,
               semantic_analyze_with_scope_tracking()'s NODE_FUNC_CALL
               case) walks into it. */
            Parameter *param = func->parameters;
            ArgumentList *arg = node->data.func_call.arguments;
            int arg_index = 0;
            while (param && arg)
            {
                arg_index++;
                if (arg->expr)
                {
                    propagate_contextual_call_type(arg->expr, param->type,
                                                   param->pointer_level);
                    /* Struct/union pointer parameters: this analyzer does
                       not type-check user-defined call arguments against
                       their parameters at all otherwise (a separate,
                       much larger, pre-existing gap out of scope here) --
                       but a `gang Point *pp` parameter silently accepting
                       `&some_rect` is the call-argument reproduction of
                       the same struct-tag hole the declaration/assignment
                       checks above just closed (PR #248 review, round 2,
                       finding 1), and enter_function_scope() (ast.c) then
                       follows that address using the PARAMETER's declared
                       tag's layout on an actually differently-shaped
                       blob -- a real out-of-bounds read/write for two
                       structs of different size, not just a wrong value. */
                    if (param->type == VAR_STRUCT && param->pointer_level > 0)
                    {
                        int line =
                            node->line_number > 0 ? node->line_number : 1;
                        VarType actual_type =
                            infer_expression_type(arg->expr, analyzer);
                        int actual_pl =
                            infer_expression_pointer_level(arg->expr, analyzer);
                        /* check_pointer_struct_tag_match() alone fail-opens
                           on an argument whose struct TAG it can't
                           determine -- correct for a genuinely unknown
                           expression, but infer_expression_struct_name()
                           also (necessarily) returns empty for an
                           expression that is DEFINITELY NOT a struct at
                           all (e.g. `rizz n; bump(&n);` -- &n is VAR_INT,
                           pointer_level 1, no struct_name to report), so
                           the tag helper alone can't tell "unknown" from
                           "confirmed not a struct" and silently passed the
                           latter (PR #248 review, round 3, finding 2).
                           declaration/assignment already have this
                           category check for free via check_type_
                           compatibility_ex(); this call-argument path
                           needs its own, since it was never wired to run
                           any category check, only the tag one. */
                        if ((actual_type != NONE &&
                             actual_type != VAR_STRUCT) ||
                            actual_pl != param->pointer_level)
                        {
                            char error_msg[MAX_BUFFER_LEN];
                            snprintf(error_msg, sizeof(error_msg),
                                     "'%s' argument %d: expected pointer to "
                                     "struct/union '%s' (level %d), got %s "
                                     "pointer level %d",
                                     func_name.data, arg_index,
                                     param->struct_name.data
                                         ? param->struct_name.data
                                         : "?",
                                     param->pointer_level,
                                     vartype_to_string(actual_type), actual_pl);
                            add_semantic_error(analyzer,
                                               SEMANTIC_ERROR_TYPE_MISMATCH,
                                               STRING_LITERAL(error_msg), line);
                        }
                        else
                        {
                            char prefix[MAX_BUFFER_LEN];
                            snprintf(prefix, sizeof(prefix),
                                     "'%s' argument %d: type mismatch",
                                     func_name.data, arg_index);
                            check_pointer_struct_tag_match(
                                analyzer, param->struct_name, arg->expr, prefix,
                                line);
                        }
                    }
                }
                param = param->next;
                arg = arg->next;
            }
        }
    }

    return NULL;
}

/* Type-validates a binary operation's operands. Does NOT walk them --
   both callers (ast_accept()'s NODE_OPERATION case, via visit_children();
   and semantic_analyze_with_scope_tracking()'s own NODE_OPERATION case)
   already recurse into left/right themselves before calling this. This
   used to also call ast_accept() on both operands here, which double-
   visited them through either caller -- harmless when nothing the second
   pass touches reports errors, but it double-reports for anything that
   does (e.g. two "Undefined variable" errors for `undefined_var + 1`, or
   two arity errors for a native call in an operand). */
void *semantic_visit_binary_operation(Visitor *self, ASTNode *node)
{
    SemanticAnalyzer *analyzer = (SemanticAnalyzer *)self;

    if (!node || !node->data.op.left || !node->data.op.right)
        return NULL;

    validate_binary_operation(node->data.op.left, node->data.op.right,
                              node->data.op.op, analyzer);

    return NULL;
}

/* Shared by both branches of semantic_visit_declaration() below: checks
   an initializer expression's inferred type/pointer-level against a
   declaration's declared type/pointer-level, reporting a type-mismatch
   error if they disagree. Pointer-level is checked first and
   independently of the base-type check (see the pointer-level branch's
   own comment at its original call site for why -- a NONE-typed but
   still pointer-level-known initializer, e.g. a legacy STDROT_ANY-
   returning native call, must still be checked against a pointer-typed
   destination). */
static void check_declaration_initializer_compatibility(
    SemanticAnalyzer *analyzer, String var_name, VarType declared_type,
    int declared_pointer_level, ASTNode *init_expr, int line_number)
{
    if (!init_expr || is_unresolved_contextual_call(init_expr))
        return;

    int init_pointer_level =
        infer_expression_pointer_level(init_expr, analyzer);
    if (declared_pointer_level > 0 &&
        declared_pointer_level != init_pointer_level)
    {
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "Type mismatch in initialization of '%s': expected a "
                 "pointer (level %d), got pointer level %d",
                 var_name.data, declared_pointer_level, init_pointer_level);
        add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                           STRING_LITERAL(error_msg), line_number);
        return;
    }

    VarType init_type = infer_expression_type(init_expr, analyzer);
    if (declared_type != NONE && init_type != NONE &&
        !check_type_compatibility_ex(declared_type, declared_pointer_level,
                                     init_type, init_pointer_level))
    {
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "Type mismatch in initialization of '%s': expected %s, got %s",
                 var_name.data, vartype_to_string(declared_type),
                 vartype_to_string(init_type));
        add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                           STRING_LITERAL(error_msg), line_number);
    }
}

/* Validates this declaration node only -- pending_initializer/
   struct_init_expr/data.op.right are walked by the switch's
   NODE_DECLARATION case before this is called, per the traversal
   invariant on semantic_analyze_with_scope_tracking(). */
void semantic_visit_declaration(Visitor *self, ASTNode *node)
{
    SemanticAnalyzer *analyzer = (SemanticAnalyzer *)self;

    if (!node || !node->data.op.left || !node->data.op.left->data.name.data)
        return;

    const String var_name = node->data.op.left->data.name;

    /* Round-20 review, finding #1 -- `skibidi` (ast.h's own VAR_VOID
       comment: "no declaration syntax produces a void-typed Variable")
       now maps to VAR_VOID, not NONE, so a void-typed function's return
       type is correctly distinguishable from "genuinely unknown" -- but
       `type` (lang.y) is the SAME grammar nonterminal a variable
       declaration's own type comes from, so `skibidi x;` now parses to
       a VAR_VOID-typed declaration too, unless something rejects it.
       Checked unconditionally (not gated behind node->data.op.right,
       unlike the initializer-compatibility check below) so a bare
       `skibidi x;` with no initializer is caught just as surely as
       `skibidi x = 5;`. */
    if (node->var_type == VAR_VOID && node->pointer_level == 0)
    {
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "Cannot declare '%s' with type void", var_name.data);
        add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                           STRING_LITERAL(error_msg),
                           node->line_number > 0 ? node->line_number : 1);
        return;
    }

    if (node->data.op.right && node->data.op.right->type == NODE_STRUCT_DEF)
    {
        StructDef *def =
            get_struct_def(node->data.op.right->data.struct_def.name);
        int init_count = node->data.op.right->data.struct_def.initializer_count;
        if (def && def->is_union && init_count >= 0 && init_count != 1)
        {
            add_semantic_error(
                analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                STRING_LITERAL("Union initializer must have exactly one value"),
                node->line_number > 0 ? node->line_number : 1);
        }
        /* data.op.right here is only the NODE_STRUCT_DEF type marker, not
           an initializer expression, so the general check below (which
           reads data.op.right as the initializer) never runs for this
           production -- meaning a pointer-typed struct/union
           declaration's REAL initializer (struct_init_expr, set by
           lang.y's `struct_or_union name_token declarator EQUALS
           expression` production) went completely untyped: `gang Point
           *pp = &some_int;` parsed and ran, silently aliasing an int as
           a Point (PR #248 review, finding 1). Scoped to pointer-typed
           declarations only -- a by-value struct initializer's tag match
           (`gang Point p = make_other_struct();`) is a separate,
           pre-existing gap caught at runtime (interpreter.c's own
           struct_name comparison), not something this fix takes on. */
        if (node->pointer_level > 0)
        {
            int line = node->line_number > 0 ? node->line_number : 1;
            check_declaration_initializer_compatibility(
                analyzer, var_name, node->var_type, node->pointer_level,
                node->struct_init_expr, line);
            char prefix[MAX_BUFFER_LEN];
            snprintf(prefix, sizeof(prefix),
                     "Type mismatch in initialization of '%s'", var_name.data);
            check_pointer_struct_tag_match(
                analyzer, node->data.op.right->data.struct_def.name,
                node->struct_init_expr, prefix, line);
        }
        return;
    }

    /* A still-unresolved contextual `slorp()` initializer (e.g. `rizz *p =
       slorp();` -- unsupported pointer context, see propagate_contextual_
       call_type()'s own comment) already gets its own "cannot infer type"
       error from semantic_visit_function_call() when the switch's own
       recursion above reaches it. Comparing its necessarily NONE/0
       inferred type/pointer-level against the declared type here would
       raise a second, misleading error for the same node. Pointer-level
       compatibility is knowable even when the base type isn't (e.g. a
       legacy STDROT_ANY-returning native call used as an initializer, whose
       actual scalar type the analyzer genuinely can't predict) -- see
       check_declaration_initializer_compatibility()'s own comment for why
       that's still checked independently of the base-type check. */
    /* This hardcoded `1` (not node->line_number) is pre-existing behavior
       from before this PR touched this function -- several unrelated
       fixtures (semantic_error_native_ptr_return_scalar_init and siblings)
       already encode "at line 1" as their expected output for THIS check
       specifically. Switching it to the real line number is a correct fix
       in isolation but has a blast radius well beyond struct pointers (7
       unrelated fixtures broke when tried), so it's left alone here --
       only the NEW check below (struct-tag match, added by this PR) uses
       the real line number, since nothing pre-existing depends on its
       line being wrong. */
    check_declaration_initializer_compatibility(
        analyzer, var_name, node->var_type, node->pointer_level,
        node->data.op.right, 1);
    /* Same struct-tag check as the NODE_STRUCT_DEF branch above, for the
       OTHER declaration shape that reaches a pointer-typed struct/union
       variable: a `lit`-aliased pointer type used as a plain declarator
       (`lit gang Point *PointPtr; PointPtr pp = &r;`). That grammar
       production never carries a NODE_STRUCT_DEF marker on data.op.right
       -- data.op.right IS the real initializer here, same as any
       non-struct pointer -- so it falls through to this general branch,
       whose only check until now was the category one just above.
       node->struct_name is where the declared tag lives for this shape
       (ast.h's own comment: "for declaration nodes ... that do not carry
       a NODE_STRUCT_DEF child, such as pointer arrays declared through a
       typedef alias" -- create_alias_declaration already populates it).
       Without this, `PointPtr pp = &some_rect;` passed the category
       check above (both VAR_STRUCT pointers) and was never tag-checked
       at all (PR #248 review, round 3, finding 1). Uses the real line
       number (PR #248 review, round 4, finding 2 -- this check is new,
       nothing pre-existing depends on it reporting the wrong one). */
    if (node->var_type == VAR_STRUCT && node->pointer_level > 0)
    {
        int line = node->line_number > 0 ? node->line_number : 1;
        char prefix[MAX_BUFFER_LEN];
        snprintf(prefix, sizeof(prefix),
                 "Type mismatch in initialization of '%s'", var_name.data);
        check_pointer_struct_tag_match(analyzer, node->struct_name,
                                       node->data.op.right, prefix, line);
    }
}

/* Validates this assignment node only -- data.op.right and the
   array-index/struct-access/dereference-operand shapes of data.op.left
   are walked by the switch's NODE_ASSIGNMENT case before this is called,
   per the traversal invariant on semantic_analyze_with_scope_tracking(). */
void semantic_visit_assignment(Visitor *self, ASTNode *node)
{
    SemanticAnalyzer *analyzer = (SemanticAnalyzer *)self;

    if (!node || !node->data.op.left)
        return;

    if (node->data.op.left->type == NODE_UNARY_OPERATION &&
        node->data.op.left->data.unary.op == OP_DEREFERENCE)
    {
        ASTNode *operand = node->data.op.left->data.unary.operand;
        int operand_pointer_level =
            infer_expression_pointer_level(operand, analyzer);
        if (operand_pointer_level <= 0)
        {
            add_semantic_error(
                analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                STRING_LITERAL("Cannot dereference a non-pointer expression"),
                node->line_number > 0 ? node->line_number : 1);
            return;
        }
        /* Round-22 review, finding #3, extended by round-23 finding #2
           -- same check as the general NODE_UNARY_OPERATION case (above
           in this file, see its own comment for the VAR_PTR reasoning),
           needed independently here because an assignment's dereference
           LHS (`*p = 42;`) is deliberately NOT walked as a whole unary
           node by semantic_analyze_with_scope_tracking()'s own NODE_
           ASSIGNMENT case (only `operand` is visited, to avoid this
           exact pointer-ness check running twice) -- meaning THIS is
           the only place that validates a dereference LHS's pointee
           type at all. A `void *`/opaque-VAR_PTR LHS (operand_pointer_
           level == 1) has no pointee representation to write through; a
           `void **`/pointer-to-VAR_PTR LHS (operand_pointer_level > 1)
           dereferences fine, down to a `void *`/VAR_PTR value. */
        if (operand_pointer_level == 1 &&
            (infer_expression_type(operand, analyzer) == VAR_VOID ||
             infer_expression_type(operand, analyzer) == VAR_PTR))
        {
            add_semantic_error(
                analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                STRING_LITERAL("Cannot dereference a type-erased pointer -- "
                               "its pointee type is unknown"),
                node->line_number > 0 ? node->line_number : 1);
            return;
        }
    }

    if (node->data.op.left->type == NODE_IDENTIFIER)
    {
        const String var_name = node->data.op.left->data.name;

        if (analyzer->is_collecting_phase)
            return;

        SymbolEntry *symbol = find_symbol(analyzer, var_name);

        if (!symbol)
        {
            Variable *var = get_variable(var_name);
            if (!var)
            {
                if (!is_builtin_function(var_name))
                {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg),
                             "Assignment to undefined variable '%s'",
                             var_name.data);
                    add_semantic_error(
                        analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE,
                        STRING_LITERAL(error_msg),
                        node->line_number > 0 ? node->line_number : 1);
                }
                return;
            }

            if (var->modifiers.is_const)
            {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg),
                         "Cannot assign to const variable '%s'", var_name.data);
                add_semantic_error(analyzer, SEMANTIC_ERROR_CONST_ASSIGNMENT,
                                   STRING_LITERAL(error_msg),
                                   node->line_number > 0 ? node->line_number
                                                         : 1);
            }
        }
        else
        {
            if (symbol->is_const)
            {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg),
                         "Cannot assign to const variable '%s'", var_name.data);
                add_semantic_error(analyzer, SEMANTIC_ERROR_CONST_ASSIGNMENT,
                                   STRING_LITERAL(error_msg),
                                   node->line_number > 0 ? node->line_number
                                                         : 1);
            }
        }
    }

    if (node->data.op.left->type != NODE_IDENTIFIER &&
        node->data.op.left->type != NODE_ARRAY_ACCESS &&
        node->data.op.left->type != NODE_STRUCT_ACCESS &&
        !(node->data.op.left->type == NODE_UNARY_OPERATION &&
          node->data.op.left->data.unary.op == OP_DEREFERENCE))
    {
        add_semantic_error(
            analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
            STRING_LITERAL("Left-hand side of assignment is not assignable"),
            node->line_number > 0 ? node->line_number : 1);
        return;
    }

    VarType target_type = infer_expression_type(node->data.op.left, analyzer);
    int target_pointer_level =
        infer_expression_pointer_level(node->data.op.left, analyzer);
    VarType value_type = infer_expression_type(node->data.op.right, analyzer);
    int value_pointer_level =
        infer_expression_pointer_level(node->data.op.right, analyzer);

    /* Same reasoning as semantic_visit_declaration()'s identical guard --
       a still-unresolved contextual `slorp()` assigned to a pointer-typed
       TARGET (e.g. `p = slorp();`, not `*p = slorp();` -- dereferencing
       `p` first makes the target the POINTEE's type, which a witness
       resolves against successfully; see propagate_contextual_call_type()'s
       own call site above, which already computes the dereferenced
       target's type/pointer_level, not `p`'s own) already gets its own
       "cannot infer type" error from semantic_visit_function_call();
       comparing its necessarily NONE/0 inferred type/pointer-level
       against the target here would raise a second, misleading error for
       the same node. */
    if ((target_pointer_level > 0 || value_pointer_level > 0) &&
        !is_unresolved_contextual_call(node->data.op.right) &&
        !check_type_compatibility_ex(target_type, target_pointer_level,
                                     value_type, value_pointer_level))
    {
        add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                           STRING_LITERAL("Assignment type mismatch"),
                           node->line_number > 0 ? node->line_number : 1);
    }
    else if (target_pointer_level > 0 && target_type == VAR_STRUCT &&
             value_type == VAR_STRUCT &&
             !is_unresolved_contextual_call(node->data.op.right))
    {
        /* check_type_compatibility_ex() above already passed -- both
           sides are VAR_STRUCT pointers at the same level -- but that
           check has no struct_name parameter, so `pp = &some_rect;`
           (pp declared `gang Point *`) sails through it too (PR #248
           review, round 2, finding 1: the assignment-form reproduction of
           the same tag hole the declaration form has). */
        check_pointer_struct_tag_match(
            analyzer,
            infer_expression_struct_name(node->data.op.left, analyzer),
            node->data.op.right, "Assignment type mismatch",
            node->line_number > 0 ? node->line_number : 1);
    }
}

void semantic_visit_function_definition(Visitor *self, ASTNode *node)
{
    if (!node || !node->data.function_def.name.data)
        return;

    if (node->data.function_def.body)
    {
        ast_accept(node->data.function_def.body, self);
    }
}

/* Symbol table management functions */
void add_symbol(SemanticAnalyzer *analyzer, const String name, VarType type,
                int pointer_level, bool is_const, bool is_function,
                VarType return_type, int return_pointer_level, int line_number,
                const String struct_name, bool is_array)
{
    if (!analyzer || !name.data)
        return;

    SymbolEntry *entry = SAFE_MALLOC(SymbolEntry);
    if (!entry)
        return;

    entry->name = safe_strdup(&name);
    entry->type = type;
    entry->pointer_level = pointer_level;
    entry->is_const = is_const;
    entry->is_function = is_function;
    entry->is_array = is_array;
    entry->return_type = return_type;
    entry->return_pointer_level = return_pointer_level;
    entry->line_number = line_number;
    entry->scope_depth = analyzer->scope_depth;
    entry->struct_name =
        struct_name.data ? safe_strdup(&struct_name) : (String){0};
    entry->function_name = analyzer->current_function_name.data
                               ? safe_strdup(&analyzer->current_function_name)
                               : (String){0};
    entry->next = analyzer->symbol_table;

    analyzer->symbol_table = entry;
}

SymbolEntry *find_symbol(SemanticAnalyzer *analyzer, const String name)
{
    if (!analyzer || !name.data)
        return NULL;

    SymbolEntry *entry = analyzer->symbol_table;

    while (entry)
    {
        if (strcmp(entry->name.data, name.data) == 0 &&
            entry->scope_depth <= analyzer->scope_depth)
        {
            /* scope_depth alone isn't enough: it resets to 0 for every
               function, so without also checking function_name, a local
               in function A would look "accessible" while analyzing
               function B's body at the same depth (both entries satisfy
               scope_depth <= analyzer->scope_depth simultaneously, and
               this list only grows -- entries from a function already
               fully analyzed are never removed). Global entries (function
               names, and top-level skibidi main declarations, both tagged
               with function_name == {0}) stay visible everywhere;
               everything else must belong to the function currently being
               analyzed. */
            bool is_global = !entry->function_name.data;
            bool same_function =
                entry->function_name.data &&
                analyzer->current_function_name.data &&
                strcmp(entry->function_name.data,
                       analyzer->current_function_name.data) == 0;
            if (is_global || same_function)
            {
                return entry; /* Symbol is accessible */
            }
        }
        entry = entry->next;
    }

    return NULL; /* Symbol not found or not accessible */
}

void free_symbol_table(SymbolEntry *symbols)
{
    while (symbols)
    {
        SymbolEntry *next = symbols->next;
        SAFE_FREE(symbols->name);
        if (symbols->struct_name.data)
            SAFE_FREE(symbols->struct_name);
        if (symbols->function_name.data)
            SAFE_FREE(symbols->function_name);
        SAFE_FREE(symbols);
        symbols = next;
    }
}

/* lit aliases occupy the ordinary identifier namespace. Reverse collisions
   (a later variable, function, or parameter reusing an alias name) are
   diagnosed here because the lexer classifies the name as TYPE_NAME after
   the alias is registered, and the parser still accepts that token as a
   declarator so we can report a semantic error instead of a parse fail. */
static bool report_alias_name_conflict(SemanticAnalyzer *analyzer,
                                       const String name, int line_number)
{
    if (!get_type_alias(name))
        return false;

    char error_msg[MAX_BUFFER_LEN];
    snprintf(error_msg, sizeof(error_msg),
             "Typedef alias '%s' is already defined",
             name.data ? name.data : "?");
    add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                       STRING_LITERAL(error_msg),
                       line_number > 0 ? line_number : 1);
    return true;
}

/* Phase 1: Collect all declarations */
void collect_declarations(SemanticAnalyzer *analyzer, ASTNode *node)
{
    if (!node || !analyzer)
        return;

    static int depth = 0;
    if (depth > 1000)
    {
        fprintf(stderr, "Warning: Maximum recursion depth reached in "
                        "collect_declarations\n");
        return;
    }
    depth++;

    switch (node->type)
    {
    case NODE_DECLARATION:
        if (node->data.op.left && node->data.op.left->data.name.data)
        {
            const String var_name = node->data.op.left->data.name;
            VarType var_type = node->var_type;
            bool is_const = node->modifiers.is_const;
            const String struct_name =
                node->data.op.right &&
                        node->data.op.right->type == NODE_STRUCT_DEF
                    ? node->data.op.right->data.struct_def.name
                    : node->struct_name;

            if (!report_alias_name_conflict(analyzer, var_name,
                                            node->line_number))
            {
                add_symbol(analyzer, var_name, var_type, node->pointer_level,
                           is_const, false, NONE, 0,
                           node->line_number > 0 ? node->line_number : 1,
                           struct_name, node->is_array);
            }
        }
        if (node->data.op.right)
        {
            collect_declarations(analyzer, node->data.op.right);
        }
        break;

    case NODE_FUNCTION_DEF:
        if (node->data.function_def.name.data)
        {
            SymbolEntry *existing =
                find_symbol(analyzer, node->data.function_def.name);
            /* Reverse ordinary-identifier collision: lit aliases are parsed
               before this collection pass, so a later function definition
               cannot reuse an alias name. */
            bool alias_conflict = report_alias_name_conflict(
                analyzer, node->data.function_def.name, node->line_number);
            if (!alias_conflict && existing && existing->is_function)
            {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg),
                         "Function '%s' is already defined",
                         node->data.function_def.name.data);
                add_semantic_error(analyzer, SEMANTIC_ERROR_REDEFINITION,
                                   STRING_LITERAL(error_msg),
                                   node->line_number > 0 ? node->line_number
                                                         : 1);
            }
            else if (!alias_conflict)
            {
                add_symbol(analyzer, node->data.function_def.name, NONE, 0,
                           false, true, node->data.function_def.return_type,
                           node->pointer_level,
                           node->line_number > 0 ? node->line_number : 1,
                           (String){0}, false);
            }
        }

        analyzer->scope_depth++;

        {
            String outer_function_name = analyzer->current_function_name;
            analyzer->current_function_name = node->data.function_def.name;

            Parameter *param = node->data.function_def.parameters;
            while (param)
            {
                if (param->name.data &&
                    !report_alias_name_conflict(analyzer, param->name,
                                                node->line_number))
                {
                    add_symbol(analyzer, param->name, param->type,
                               param->pointer_level, false, false, NONE, 0,
                               node->line_number > 0 ? node->line_number : 1,
                               param->struct_name, false);
                }
                param = param->next;
            }

            if (node->data.function_def.body)
            {
                collect_declarations(analyzer, node->data.function_def.body);
            }

            analyzer->current_function_name = outer_function_name;
        }

        analyzer->scope_depth--;
        break;

    case NODE_STATEMENT_LIST:
        if (node->data.statements)
        {
            StatementList *current = node->data.statements;
            while (current)
            {
                if (current->statement)
                {
                    collect_declarations(analyzer, current->statement);
                }
                current = current->next;
            }
        }
        break;

    case NODE_IF_STATEMENT:
        if (node->data.if_stmt.then_branch)
        {
            collect_declarations(analyzer, node->data.if_stmt.then_branch);
        }
        if (node->data.if_stmt.else_branch)
        {
            collect_declarations(analyzer, node->data.if_stmt.else_branch);
        }
        break;

    case NODE_FOR_STATEMENT:
        analyzer->scope_depth++;
        if (node->data.for_stmt.init &&
            node->data.for_stmt.init->type == NODE_DECLARATION)
        {
            collect_declarations(analyzer, node->data.for_stmt.init);
        }
        if (node->data.for_stmt.body)
        {
            collect_declarations(analyzer, node->data.for_stmt.body);
        }
        analyzer->scope_depth--;
        break;

    case NODE_WHILE_STATEMENT:
    case NODE_DO_WHILE_STATEMENT:
        analyzer->scope_depth++;
        if (node->data.while_stmt.body)
        {
            collect_declarations(analyzer, node->data.while_stmt.body);
        }
        analyzer->scope_depth--;
        break;

    case NODE_OPERATION:
        if (node->data.op.op == OP_ASSIGN)
        {
            break;
        }
        break;

    default:
        break;
    }

    depth--;
}

/* Scope-aware semantic analysis that tracks scope depth during traversal.
 *
 * TRAVERSAL INVARIANT: this function is the sole owner of recursion into
 * an AST node's children during semantic analysis. Every case below that
 * has children calls itself (or a small node-specific helper, like
 * semantic_check_expression_list() for ExpressionList) on each child
 * exactly once; the semantic_visit_*() functions it calls out to
 * (semantic_visit_declaration, semantic_visit_assignment,
 * semantic_visit_function_call, semantic_visit_binary_operation,
 * semantic_visit_identifier) validate *only* the node passed to them --
 * none of them may call ast_accept() or semantic_analyze_with_scope_
 * tracking() on a child this function has already visited or is about to.
 *
 * This isn't cosmetic. semantic_visit_binary_operation() used to call
 * ast_accept() on both operands *in addition to* this function's own
 * NODE_OPERATION case having already recursed into them -- every operand
 * of every binary operation was visited twice, silently, until something
 * that reports on the second visit (arity/type errors, "Undefined
 * variable") made it visible as duplicate error messages. Multiple
 * rounds of native-call-checking work then had to individually rediscover
 * and route around node kinds this function didn't yet have a case for
 * (array indices, sizeof, return, do-while, switch, ...), each visited
 * exactly once nowhere. Both problems are the same root cause: no single
 * place owned "does the child get visited, and by whom."
 *
 * A visitor that genuinely needs to inspect a child's shape (not walk
 * its children) may call infer_expression_type()/infer_expression_pointer_
 * level() -- those are pure queries, not traversal, and are safe to call
 * as many times as needed. */
void semantic_analyze_with_scope_tracking(SemanticAnalyzer *analyzer,
                                          ASTNode *node)
{
    if (!node || !analyzer)
        return;

    /* Prevent infinite recursion */
    static int recursion_depth = 0;
    if (recursion_depth > 100)
    {
        fprintf(stderr,
                "Warning: Maximum recursion depth reached in scope tracking\n");
        return;
    }
    recursion_depth++;

    switch (node->type)
    {
    case NODE_STATEMENT_LIST:
    {
        /* Process each statement in the list */
        if (node->data.statements)
        {
            StatementList *current = node->data.statements;
            while (current)
            {
                if (current->statement)
                {
                    semantic_analyze_with_scope_tracking(analyzer,
                                                         current->statement);
                }
                current = current->next;
            }
        }
        break;
    }

    case NODE_FUNCTION_DEF:
    {
        /* Enter function scope */
        analyzer->scope_depth++;
        String outer_function_name = analyzer->current_function_name;
        analyzer->current_function_name = node->data.function_def.name;

        /* Process function body */
        if (node->data.function_def.body)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.function_def.body);
        }

        /* Exit function scope */
        analyzer->current_function_name = outer_function_name;
        analyzer->scope_depth--;
        break;
    }

    case NODE_FOR_STATEMENT:
    {
        /* Enter loop scope */
        analyzer->scope_depth++;

        /* Process all parts of the for loop */
        if (node->data.for_stmt.init)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.for_stmt.init);
        }
        if (node->data.for_stmt.cond)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.for_stmt.cond);
            require_value_expression(analyzer, node->data.for_stmt.cond,
                                     "condition");
        }
        if (node->data.for_stmt.incr)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.for_stmt.incr);
        }
        if (node->data.for_stmt.body)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.for_stmt.body);
        }

        /* Exit loop scope */
        analyzer->scope_depth--;
        break;
    }

    case NODE_WHILE_STATEMENT:
    {
        /* Process condition at current scope */
        if (node->data.while_stmt.cond)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.while_stmt.cond);
            require_value_expression(analyzer, node->data.while_stmt.cond,
                                     "condition");
        }

        /* Enter loop scope for body */
        analyzer->scope_depth++;
        if (node->data.while_stmt.body)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.while_stmt.body);
        }
        analyzer->scope_depth--;
        break;
    }

    case NODE_IF_STATEMENT:
    {
        /* Process condition at current scope */
        if (node->data.if_stmt.condition)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.if_stmt.condition);
            require_value_expression(analyzer, node->data.if_stmt.condition,
                                     "condition");
        }

        /* Process then branch in new scope */
        analyzer->scope_depth++;
        if (node->data.if_stmt.then_branch)
        {
            semantic_analyze_with_scope_tracking(
                analyzer, node->data.if_stmt.then_branch);
        }
        analyzer->scope_depth--;

        /* Process else branch in new scope */
        if (node->data.if_stmt.else_branch)
        {
            analyzer->scope_depth++;
            semantic_analyze_with_scope_tracking(
                analyzer, node->data.if_stmt.else_branch);
            analyzer->scope_depth--;
        }
        break;
    }

    case NODE_IDENTIFIER:
    {
        /* Use the visitor method for identifier checking */
        semantic_visit_identifier((Visitor *)analyzer, node);
        break;
    }

    case NODE_ASSIGNMENT:
    {
        /* Per the traversal invariant above: walk everything that can
           contain a native call before calling semantic_visit_assignment(),
           which then validates the node only. A plain-identifier LHS is
           deliberately not walked -- semantic_visit_identifier() would
           double-report "Undefined variable" on top of the assignment-
           specific undefined-variable check semantic_visit_assignment()
           already does for it. A dereference LHS only has its operand
           walked, not the whole node -- semantic_visit_assignment()
           already fully validates the dereference itself (pointer-ness of
           the operand); walking the whole node here would re-run that
           exact check a second time via this same switch's own
           NODE_UNARY_OPERATION case. */
        if (node->data.op.right)
        {
            /* An assignment target's type is already fully known (it
               doesn't depend on the right-hand side) -- resolve a
               zero-argument `slorp()` on the right against it before
               walking the right-hand side, same "typed context" idea as
               the NODE_DECLARATION case below. */
            propagate_contextual_call_type(
                node->data.op.right,
                infer_expression_type(node->data.op.left, analyzer),
                infer_expression_pointer_level(node->data.op.left, analyzer));
            semantic_analyze_with_scope_tracking(analyzer, node->data.op.right);
        }
        if (node->data.op.left)
        {
            if (node->data.op.left->type == NODE_ARRAY_ACCESS ||
                node->data.op.left->type == NODE_STRUCT_ACCESS)
            {
                semantic_analyze_with_scope_tracking(analyzer,
                                                     node->data.op.left);
            }
            else if (node->data.op.left->type == NODE_UNARY_OPERATION &&
                     node->data.op.left->data.unary.op == OP_DEREFERENCE)
            {
                semantic_analyze_with_scope_tracking(
                    analyzer, node->data.op.left->data.unary.operand);
            }
        }
        semantic_visit_assignment((Visitor *)analyzer, node);
        break;
    }

    case NODE_FUNC_CALL:
    {
        /* Use the visitor method for function call checking */
        semantic_visit_function_call((Visitor *)analyzer, node);

        /* Also process function arguments */
        if (node->data.func_call.arguments)
        {
            ArgumentList *args = node->data.func_call.arguments;
            while (args)
            {
                if (args->expr)
                {
                    semantic_analyze_with_scope_tracking(analyzer, args->expr);
                }
                args = args->next;
            }
        }
        break;
    }

    case NODE_OPERATION:
    {
        /* Process both operands */
        if (node->data.op.left)
        {
            semantic_analyze_with_scope_tracking(analyzer, node->data.op.left);
        }
        if (node->data.op.right)
        {
            semantic_analyze_with_scope_tracking(analyzer, node->data.op.right);
        }

        /* Use visitor method for operation checking */
        semantic_visit_binary_operation((Visitor *)analyzer, node);
        break;
    }

    case NODE_UNARY_OPERATION:
    {
        if (node->data.unary.operand)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.unary.operand);
        }
        if (node->data.unary.op == OP_ADDRESS_OF)
        {
            ASTNode *operand = node->data.unary.operand;
            bool valid_lvalue =
                operand && (operand->type == NODE_IDENTIFIER ||
                            operand->type == NODE_ARRAY_ACCESS ||
                            (operand->type == NODE_UNARY_OPERATION &&
                             operand->data.unary.op == OP_DEREFERENCE));
            if (!valid_lvalue)
            {
                add_semantic_error(
                    analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                    STRING_LITERAL(
                        "Address-of requires an assignable expression"),
                    node->line_number > 0 ? node->line_number : 1);
            }
        }
        else if (node->data.unary.op == OP_DEREFERENCE)
        {
            int operand_pointer_level = infer_expression_pointer_level(
                node->data.unary.operand, analyzer);
            if (operand_pointer_level <= 0)
            {
                add_semantic_error(
                    analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                    STRING_LITERAL(
                        "Cannot dereference a non-pointer expression"),
                    node->line_number > 0 ? node->line_number : 1);
            }
            /* Round-22 review, finding #3, extended by round-23 finding
               #2 -- `void *` is a real value (round 21), but that
               doesn't make `*(void *)` valid: a pointer needs a pointee
               TYPE to know what representation and size to read, and
               VAR_VOID has none. VAR_PTR (this codebase's OWN opaque-
               native-pointer type, per its own comment just above its
               declaration, ast.h) has the identical problem for the
               identical reason -- "an opaque native pointer with no
               concrete base type by design" -- so `*test_ptr_source()`
               (a bare STDROT_PTR-returning native call) was rejected no
               more than `*(void *)` originally was: nothing here checked
               VAR_PTR at all, so a dereferenced opaque pointer's
               resulting type/pointer_level (VAR_PTR, 0) sailed through,
               and whichever scalar evaluator happened to consume it
               (evaluate_expression_bool()'s OP_DEREFERENCE case,
               evaluate_expression_int()'s, ...) silently DECIDED the
               pointee's representation after the fact -- the exact
               opposite of a typed ABI, where the descriptor is supposed
               to be authoritative. The invalidity in both cases is
               specifically that dereferencing would PRODUCE pointer_
               level 0 with a type-erased base (VAR_VOID or VAR_PTR) --
               not that the operand's base happens to be one of those at
               ANY depth: `void **q`/a pointer-to-VAR_PTR dereferences
               fine, landing back at (VAR_VOID, 1)/(VAR_PTR, 1), still a
               perfectly good opaque pointer VALUE one level down.
               Checked as its own case (not folded into the `<= 0`
               rejection above) because pointer_level 2 legitimately
               passes that check and must not be rejected by it. */
            else if (operand_pointer_level == 1 &&
                     (infer_expression_type(node->data.unary.operand,
                                            analyzer) == VAR_VOID ||
                      infer_expression_type(node->data.unary.operand,
                                            analyzer) == VAR_PTR))
            {
                add_semantic_error(
                    analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                    STRING_LITERAL(
                        "Cannot dereference a type-erased pointer -- its "
                        "pointee type is unknown"),
                    node->line_number > 0 ? node->line_number : 1);
            }
        }
        else
        {
            /* Every other unary operator (arithmetic negation, logical
               not, increment/decrement, ...) genuinely consumes its
               operand as a value -- unlike ADDRESS_OF (wants an lvalue
               location, not a value; already rejects a non-lvalue
               operand shape like a call above) and DEREFERENCE (wants a
               pointer; already rejected by the pointer_level check
               above), neither of which needed this. */
            require_value_expression(analyzer, node->data.unary.operand,
                                     "unary operator operand");
        }
        break;
    }

    case NODE_DECLARATION:
    {
        /* A braced initializer (`rizz arr[1] = {bet(2)};`) lives on
           pending_initializer, not data.op.right -- create_multi_array_
           declaration_node() leaves data.op.right NULL. A struct's plain-
           expression initializer (`gang Point r = make_point(1, 2);`)
           lives on struct_init_expr; data.op.right there is a
           NODE_STRUCT_DEF type placeholder, not the initializer.
           Per the traversal invariant above, this switch walks all three
           -- semantic_visit_declaration() validates the node only. */
        if (node->pending_initializer)
        {
            /* A struct's braced initializer (`gang Point p = { slorp(), 1
               };`) reuses this same pending_initializer field (lang.y's
               struct-declarator productions), but its leaves are each a
               different FIELD's type -- node->var_type is just VAR_STRUCT
               here, not one shared element type the way an array's is.
               The split below is exactly "plain struct declaration" vs.
               "homogeneous scalar array declaration" (`rizz a[2] = {...}`)
               -- not "struct" vs. "array of structs": this grammar has no
               array-of-structs declaration syntax at all (struct
               declarators, lang.y, have no `dimensions` production the
               way the scalar/array ones do; `gang Point arr[2] = {...}`
               is a parse error). The NODE_STRUCT_DEF placeholder on
               data.op.right (only the plain struct-declaration grammar
               productions create one; create_multi_array_declaration_
               node() never does) is what tells the two apart here, and
               carries the struct tag needed to look up each field's real
               type. */
            bool is_struct_brace_init =
                node->var_type == VAR_STRUCT && node->data.op.right &&
                node->data.op.right->type == NODE_STRUCT_DEF;
            StructDef *struct_init_def = NULL;
            if (is_struct_brace_init)
            {
                struct_init_def =
                    get_struct_def(node->data.op.right->data.struct_def.name);
                propagate_contextual_type_into_struct_initializer(
                    node->pending_initializer,
                    struct_init_def ? struct_init_def->fields : NULL);
            }
            else
            {
                propagate_contextual_type_into_expression_list(
                    node->pending_initializer, node->var_type,
                    node->pointer_level);
            }
            semantic_check_expression_list(analyzer, node->pending_initializer);

            /* By-value struct brace-init of a pointer-typed FIELD (`gang
               Holder { gang Point *pt; }; gang Holder h = {&r};`) is a
               FIFTH place a pointer-to-struct value gets stored, distinct
               from the array-element brace-init check just below (that one
               guards pointer_level > 0 on the DECLARATION itself; this one
               is pointer_level == 0 at the declaration level -- a by-value
               struct -- with the pointer nested inside one of its FIELDS).
               PR #248 review, round 5, finding 1. */
            if (is_struct_brace_init)
                check_struct_initializer_pointer_tags(
                    analyzer, node->pending_initializer,
                    struct_init_def ? struct_init_def->fields : NULL,
                    node->line_number > 0 ? node->line_number : 1);

            /* Brace-initialized array of struct/union POINTERS (`lit gang
               Point *PointPtr; PointPtr values[2] = {&r};`) is a FOURTH
               place a pointer-to-struct value gets stored, distinct from
               struct_init_expr/data.op.right/plain-assignment -- this
               grammar has no array-of-structs-BY-VALUE declaration syntax
               at all (struct declarators have no `dimensions` production),
               so var_type == VAR_STRUCT with pointer_level > 0 on an array
               declaration always means "array of struct/union pointers,"
               unambiguously. semantic_check_expression_list() just above
               only visits each element (undefined-variable/native-call
               checks); it never compares an element's inferred type/tag
               against the array's own declared type the way struct_init_
               expr/data.op.right do via check_declaration_initializer_
               compatibility()/check_pointer_struct_tag_match() elsewhere
               in this function. Without this, `values[0] = &some_rect;`
               (checked, PR #248 review round 3) is a semantic error but
               `PointPtr values[2] = {&some_rect};` (this brace-init form)
               silently stored a Rect* into a slot every later read treats
               as Point* -- for two structs of different size, an
               ASan-visible heap-buffer-overflow, not a wrong integer (PR
               #248 review, round 4, finding 1). */
            if (node->var_type == VAR_STRUCT && node->pointer_level > 0)
            {
                int line = node->line_number > 0 ? node->line_number : 1;
                const char *var_name =
                    node->data.op.left && node->data.op.left->data.name.data
                        ? node->data.op.left->data.name.data
                        : "?";
                /* ExpressionList is a circular doubly-linked list (see
                   create_expression_list()/append_expression_list_node(),
                   ast.c -- a single element's next/prev both point back to
                   itself), not NULL-terminated -- matching semantic_check_
                   expression_list()'s own traversal just above, this has
                   to stop on returning to the start, not on next == NULL. */
                ExpressionList *elem = node->pending_initializer;
                do
                {
                    if (!elem->expr ||
                        is_unresolved_contextual_call(elem->expr))
                    {
                        elem = elem->next;
                        continue;
                    }

                    VarType elem_type =
                        infer_expression_type(elem->expr, analyzer);
                    int elem_pl =
                        infer_expression_pointer_level(elem->expr, analyzer);
                    if ((elem_type != NONE && elem_type != VAR_STRUCT) ||
                        elem_pl != node->pointer_level)
                    {
                        char error_msg[MAX_BUFFER_LEN];
                        snprintf(error_msg, sizeof(error_msg),
                                 "Type mismatch in initialization of '%s': "
                                 "expected pointer to struct/union '%s' (level "
                                 "%d), got %s pointer level %d",
                                 var_name,
                                 node->struct_name.data ? node->struct_name.data
                                                        : "?",
                                 node->pointer_level,
                                 vartype_to_string(elem_type), elem_pl);
                        add_semantic_error(analyzer,
                                           SEMANTIC_ERROR_TYPE_MISMATCH,
                                           STRING_LITERAL(error_msg), line);
                        elem = elem->next;
                        continue;
                    }

                    char prefix[MAX_BUFFER_LEN];
                    snprintf(prefix, sizeof(prefix),
                             "Type mismatch in initialization of '%s'",
                             var_name);
                    check_pointer_struct_tag_match(analyzer, node->struct_name,
                                                   elem->expr, prefix, line);
                    elem = elem->next;
                } while (elem != node->pending_initializer);
            }
        }
        if (node->struct_init_expr)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->struct_init_expr);
        }
        if (node->data.op.right)
        {
            /* A declaration's declared type is already fully known at
               this point -- resolve a zero-argument `slorp()` initializer
               against it before walking the initializer, so semantic_
               check_native_call() (reached below, via this same
               recursive call) sees an ordinary desugared 1-argument call
               like any other. */
            propagate_contextual_call_type(node->data.op.right, node->var_type,
                                           node->pointer_level);
            semantic_analyze_with_scope_tracking(analyzer, node->data.op.right);
        }
        semantic_visit_declaration((Visitor *)analyzer, node);
        break;
    }

    case NODE_STRUCT_ACCESS:
    {
        /* Check the object is a known struct/union — either a variable or
           a nested member access, e.g. the `a.b` in `a.b.c` — with that
           field. Resolved here (bottom-up: the object is analyzed first,
           just above) using the analyzer's own symbol table rather than
           runtime Variables, since no runtime storage exists yet at
           semantic-analysis time -- struct/array locals are now allocated
           by the interpreter's declaration visitor when their declaration
           statement actually executes (see interpreter_visit_declaration),
           not at parse time. On success this node's var_type /
           pointer_level / struct_access.struct_name are populated so an
           outer .field one level up can consume them the same way,
           handling any chain depth. */
        ASTNode *obj = node->data.struct_access.object;
        if (obj)
            semantic_analyze_with_scope_tracking(analyzer, obj);

        StructDef *parent_def = NULL;
        bool parent_is_struct_typed = false;

        if (obj && obj->type == NODE_IDENTIFIER)
        {
            VarType obj_type = NONE;
            int obj_pointer_level = 0;
            String obj_struct_name = {0};
            SymbolEntry *symbol = find_symbol(analyzer, obj->data.name);
            if (symbol)
            {
                obj_type = symbol->type;
                obj_pointer_level = symbol->pointer_level;
                obj_struct_name = symbol->struct_name;
            }
            else
            {
                Variable *var = get_variable(obj->data.name);
                if (!var)
                    break; /* undefined variable is reported elsewhere */
                obj_type = var->var_type;
                obj_pointer_level = var->pointer_level;
                obj_struct_name = var->struct_name;
            }
            parent_is_struct_typed = (obj_type == VAR_STRUCT);
            /* Same restriction as resolve_struct_access()'s own runtime
               check (ast.c, PR #248 review finding 2): `.` as an implicit
               `->` only makes sense for exactly one level of indirection
               (`gang Foo *pp; pp.field`); `gang Foo **pp; pp.field` needs
               an explicit double-dereference in C and must be rejected
               here too, not just at runtime -- this static path is what
               currently lets `pp.field` on a `Foo **` reach interpretation
               at all. */
            if (parent_is_struct_typed && obj_pointer_level > 1)
            {
                add_semantic_error(
                    analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                    STRING_LITERAL(
                        "Member access via '.' through a multi-level "
                        "pointer (pointer_level > 1) is not supported"),
                    node->line_number > 0 ? node->line_number : 1);
                break;
            }
            if (parent_is_struct_typed)
                parent_def = get_struct_def(obj_struct_name);
        }
        else if (obj && obj->type == NODE_STRUCT_ACCESS)
        {
            if (obj->var_type == NONE)
                break; /* obj's own access already failed and was reported */
            parent_is_struct_typed = (obj->var_type == VAR_STRUCT);
            /* Same single-level implicit-`->` rule as the NODE_IDENTIFIER-
               object branch just above (and resolve_struct_access(),
               ast.c): #197 lets `.` chain through a pointer-typed
               struct/union FIELD (`n.next.v` where `next` is `gang Node
               *`, obj->pointer_level == 1) by following the pointer at
               runtime, but `gang Node **next` (pointer_level > 1) needs an
               explicit `(*x)->` and is rejected here so interpretation
               never starts. */
            if (parent_is_struct_typed && obj->pointer_level > 1)
            {
                add_semantic_error(
                    analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                    STRING_LITERAL(
                        "Member access via '.' through a multi-level "
                        "pointer (pointer_level > 1) is not supported"),
                    node->line_number > 0 ? node->line_number : 1);
                break;
            }
            if (parent_is_struct_typed &&
                obj->data.struct_access.struct_name.data)
                parent_def =
                    get_struct_def(obj->data.struct_access.struct_name);
        }
        else
        {
            break;
        }

        if (!parent_is_struct_typed)
        {
            add_semantic_error(
                analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                STRING_LITERAL("Member access on non-struct/union value"),
                node->line_number > 0 ? node->line_number : 1);
            break;
        }

        if (!parent_def)
            break; /* unknown struct/union type; nothing more to check */

        StructField *fld =
            find_struct_field(parent_def, node->data.struct_access.member_name);
        if (!fld)
        {
            char msg[MAX_BUFFER_LEN];
            snprintf(msg, sizeof(msg), "%s '%s' has no member '%s'",
                     parent_def->is_union ? "Union" : "Struct",
                     parent_def->name.data ? parent_def->name.data : "?",
                     node->data.struct_access.member_name.data);
            add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                               STRING_LITERAL(msg),
                               node->line_number > 0 ? node->line_number : 1);
            break;
        }

        node->var_type = fld->type;
        node->pointer_level = fld->pointer_level;
        node->modifiers = fld->modifiers;
        if (fld->type == VAR_STRUCT && fld->struct_name.data)
            node->data.struct_access.struct_name = fld->struct_name;
        break;
    }

    case NODE_ARRAY_ACCESS:
    {
        /* Mirrors ast_accept()'s NODE_ARRAY_ACCESS handling (visitor.c):
           num_dimensions > 0 means the multi-dimensional indices[] form is
           in use, otherwise it's the single-dimension index. Needed so an
           index expression like `arr[bet(2)]` gets the same native-call
           arity/type checking as any other expression. */
        if (node->data.array.base)
        {
            /* `foo.arr[i]` -- visit the struct_access base itself so it
               gets NODE_STRUCT_ACCESS's own validation (unknown struct
               type, unknown field, etc.), exactly as if `foo.arr` had
               been used standalone. */
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.array.base);
        }
        if (node->data.array.num_dimensions > 0)
        {
            for (int i = 0; i < node->data.array.num_dimensions; i++)
            {
                if (node->data.array.indices[i])
                {
                    semantic_analyze_with_scope_tracking(
                        analyzer, node->data.array.indices[i]);
                    require_value_expression(
                        analyzer, node->data.array.indices[i], "subscript");
                }
            }
        }
        else if (node->data.array.index)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.array.index);
            require_value_expression(analyzer, node->data.array.index,
                                     "subscript");
        }
        break;
    }

    case NODE_SIZEOF:
    {
        if (node->data.sizeof_stmt.expr)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.sizeof_stmt.expr);
            /* sizeof's own operand isn't consumed as a VALUE (it's never
               evaluated at all -- handle_sizeof()'s own comment, ast.c),
               but a VAR_VOID operand is still a statically-known type
               error worth rejecting here rather than deferring to
               handle_sizeof()'s runtime "Invalid type in sizeof" -- the
               type is already fully known without running anything. */
            require_value_expression(analyzer, node->data.sizeof_stmt.expr,
                                     "sizeof operand");
        }
        break;
    }

    case NODE_RETURN:
    {
        if (node->data.op.left)
        {
            /* A `bussin` expression's expected type is the enclosing
               function's own declared return type, already known before
               the expression itself is walked -- resolve a zero-argument
               `slorp()` return value against it here (moved ahead of the
               get_function() lookup just below, which used to run only
               after the walk), same "typed context" idea as the
               NODE_DECLARATION/NODE_ASSIGNMENT cases above. */
            Function *current_func =
                get_function(analyzer->current_function_name);
            if (current_func)
            {
                propagate_contextual_call_type(
                    node->data.op.left, current_func->return_type,
                    current_func->return_pointer_level);
            }

            semantic_analyze_with_scope_tracking(analyzer, node->data.op.left);

            /* Round-22 review, finding #4 -- a return expression was
               traversed (so a nested native call inside it still got
               its own signature checked), but never compared against
               the enclosing function's declared (return_type,
               return_pointer_level) at all. `rizz terrible() { bussin
               yapping("oops"); }` -- yapping() statically resolves to
               VAR_VOID -- passed semantic analysis outright; only
               ast.c's handle_return_statement() (at actual call time)
               would eventually reject it. Skipped for VAR_STRUCT: that
               return shape has its own dedicated struct-name-aware
               check already (handle_return_statement(), ast.c) that
               this VarType-only comparison can't replicate (VAR_STRUCT
               == VAR_STRUCT alone says nothing about WHICH struct).
               Skipped for a genuinely void-declared function
               (VAR_VOID, pointer_level 0): `bussin <anything>;` inside
               one is deliberately accepted and its value ignored
               (handle_return_statement()'s own VAR_VOID case) -- the
               same convention this codebase already uses for `bussin
               0;` ending a `skibidi` function, not something this new
               check should start rejecting. `skibidi *` (a void-
               pointer-declared function) is NOT skipped: that's a real
               pointer type with a real compatibility rule, just like
               any other pointer return. */
            /* Same reasoning as semantic_visit_declaration()'s identical
               guard -- a still-unresolved contextual `slorp()` (e.g.
               `skibidi *f() { bussin slorp(); }`) already gets its own
               "cannot infer type" error from semantic_visit_function_
               call(); comparing its necessarily NONE/0 inferred type/
               pointer-level against the declared return type here would
               raise a second, misleading error for the same node. */
            if (current_func && current_func->return_type != VAR_STRUCT &&
                !(current_func->return_type == VAR_VOID &&
                  current_func->return_pointer_level == 0) &&
                !is_unresolved_contextual_call(node->data.op.left))
            {
                VarType declared_type = current_func->return_type;
                int declared_pointer_level = current_func->return_pointer_level;
                VarType actual_type =
                    infer_expression_type(node->data.op.left, analyzer);
                int actual_pointer_level = infer_expression_pointer_level(
                    node->data.op.left, analyzer);

                if (declared_pointer_level > 0 &&
                    declared_pointer_level != actual_pointer_level)
                {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg),
                             "Return type mismatch in '%s': expected a "
                             "pointer (level %d), got pointer level %d",
                             current_func->name.data, declared_pointer_level,
                             actual_pointer_level);
                    add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                       STRING_LITERAL(error_msg),
                                       node->line_number > 0 ? node->line_number
                                                             : 1);
                }
                else if (declared_type != NONE && actual_type != NONE &&
                         !check_type_compatibility_ex(
                             declared_type, declared_pointer_level, actual_type,
                             actual_pointer_level))
                {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg),
                             "Return type mismatch in '%s': expected %s, "
                             "got %s",
                             current_func->name.data,
                             vartype_to_string(declared_type),
                             vartype_to_string(actual_type));
                    add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                       STRING_LITERAL(error_msg),
                                       node->line_number > 0 ? node->line_number
                                                             : 1);
                }
            }

            /* Pointer-to-struct return (VAR_STRUCT, return_pointer_level >
               0, #193) is the one VAR_STRUCT return shape the runtime does
               NOT tag-check -- handle_return_statement's declared_pointer_
               level > 0 branch just boxes the pointer, so `gang Point *f()
               { bussin some_rect_ptr; }` would otherwise return a Rect*
               that a later `f().x` reads through Point's layout. Check the
               pointee's tag (and category/level) here, reusing the same
               helpers as the pointer-struct declaration/assignment/argument
               checks (#248/#253); the by-value VAR_STRUCT return still
               defers to handle_return_statement's own struct-name check. */
            if (current_func && current_func->return_type == VAR_STRUCT &&
                current_func->return_pointer_level > 0 &&
                !is_unresolved_contextual_call(node->data.op.left))
            {
                VarType actual_type =
                    infer_expression_type(node->data.op.left, analyzer);
                int actual_pl = infer_expression_pointer_level(
                    node->data.op.left, analyzer);
                int line = node->line_number > 0 ? node->line_number : 1;
                if ((actual_type != NONE && actual_type != VAR_STRUCT) ||
                    actual_pl != current_func->return_pointer_level)
                {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(
                        error_msg, sizeof(error_msg),
                        "Return type mismatch in '%s': expected pointer to "
                        "struct/union '%s' (level %d), got %s pointer level %d",
                        current_func->name.data,
                        current_func->return_struct_name.data
                            ? current_func->return_struct_name.data
                            : "?",
                        current_func->return_pointer_level,
                        vartype_to_string(actual_type), actual_pl);
                    add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                       STRING_LITERAL(error_msg), line);
                }
                else
                {
                    char prefix[MAX_BUFFER_LEN];
                    snprintf(prefix, sizeof(prefix),
                             "Return type mismatch in '%s'",
                             current_func->name.data);
                    check_pointer_struct_tag_match(
                        analyzer, current_func->return_struct_name,
                        node->data.op.left, prefix, line);
                }
            }
        }
        break;
    }

    case NODE_DO_WHILE_STATEMENT:
    {
        /* Unlike while, the body runs before the condition is first
           tested and its scope is still open when the condition runs
           (`mewing { rizz i = 5; } goon (i < 10);` is legal) -- so, like
           NODE_FOR_STATEMENT, enter the loop scope once and process both
           body and condition inside it. */
        analyzer->scope_depth++;
        if (node->data.while_stmt.body)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.while_stmt.body);
        }
        if (node->data.while_stmt.cond)
        {
            semantic_analyze_with_scope_tracking(analyzer,
                                                 node->data.while_stmt.cond);
            require_value_expression(analyzer, node->data.while_stmt.cond,
                                     "condition");
        }
        analyzer->scope_depth--;
        break;
    }

    case NODE_SWITCH_STATEMENT:
    {
        if (node->data.switch_stmt.expression)
        {
            semantic_analyze_with_scope_tracking(
                analyzer, node->data.switch_stmt.expression);
            require_value_expression(analyzer,
                                     node->data.switch_stmt.expression,
                                     "switch discriminant");
        }
        analyzer->scope_depth++;
        for (CaseNode *c = node->data.switch_stmt.cases; c; c = c->next)
        {
            if (c->value)
            {
                semantic_analyze_with_scope_tracking(analyzer, c->value);
                require_value_expression(analyzer, c->value, "case value");
            }
            if (c->statements)
            {
                semantic_analyze_with_scope_tracking(analyzer, c->statements);
            }
        }
        analyzer->scope_depth--;
        break;
    }

    /* Round-20 review, finding #3 -- interpreter_visit_error_statement()
       (interpreter.c) hands this node's own expression straight to
       execute_func_call("baka", ...), which marshals it as `baka`'s real
       (typed, STDROT_STRING-format) argument -- but this switch had no
       case for NODE_ERROR_STATEMENT at all, falling to the generic
       `default:` below, which does nothing. That meant any native call
       NESTED inside `baka(...)`'s argument (`baka(yapping(42))`) never
       reached semantic_check_native_call() at all: `yapping`'s own
       fixed-format-argument type checking never ran, silently tunneling
       a statically-detectable ABI violation past semantic analysis
       entirely, caught only by the runtime enforcement this PR's whole
       point was to make redundant. Recursing into the expression (same
       as every other statement form that wraps one) makes any nested
       native call visited normally, and require_value_expression()
       rejects a VAR_VOID argument the same way every other value-
       consuming context now does. NODE_PRINT_STATEMENT (ast.h) is
       structurally identical but not actually reachable from the
       grammar (yapping() is an ordinary call now, not a dedicated
       statement form) -- handled here anyway, defensively, so it can't
       silently regain this exact hole if that ever changes. */
    case NODE_ERROR_STATEMENT:
    case NODE_PRINT_STATEMENT:
    {
        if (node->data.op.left)
        {
            semantic_analyze_with_scope_tracking(analyzer, node->data.op.left);
            require_value_expression(analyzer, node->data.op.left,
                                     "baka/yapping argument");
        }
        break;
    }

    default:
        /* For other node types, only process simple operands to avoid crashes
         */
        break;
    }

    recursion_depth--;
}

/* Single-phase analysis with integrated scope management */
bool analyze_with_scopes(SemanticAnalyzer *analyzer, ASTNode *root)
{
    if (!analyzer || !root)
        return true;

    /* Process the AST and manage scopes as we go */
    semantic_analyze_node(analyzer, root);

    return !analyzer->has_errors;
}

/* Process individual AST nodes with scope management */
void semantic_analyze_node(SemanticAnalyzer *analyzer, ASTNode *node)
{
    if (!node || !analyzer)
        return;

    switch (node->type)
    {
    case NODE_STATEMENT_LIST:
    {
        /* Process each statement in the list */
        StatementList *current = node->data.statements;
        while (current)
        {
            if (current->statement)
            {
                semantic_analyze_node(analyzer, current->statement);
            }
            current = current->next;
        }
        break;
    }

    case NODE_DECLARATION:
    {
        /* Add variable to current scope when we encounter declaration */
        if (node->data.op.left && node->data.op.left->data.name.data)
        {
            const String var_name = node->data.op.left->data.name;
            VarType var_type = node->var_type;
            bool is_const = node->modifiers.is_const;

            add_semantic_variable(analyzer, var_name, var_type,
                                  node->pointer_level, is_const);

            /* Also analyze the initialization expression */
            if (node->data.op.right)
            {
                semantic_analyze_node(analyzer, node->data.op.right);
            }
        }
        break;
    }

    case NODE_IDENTIFIER:
    {
        /* Check if identifier is defined when we encounter it */
        const String name = node->data.name;
        SymbolEntry *symbol = NULL;

        if (!find_semantic_variable(analyzer, name, &symbol))
        {
            /* Check if it's a built-in function/keyword, or an unscoped
               enum constant (e.g. bare `RED`). */
            if (!is_builtin_function(name) && !find_global_enum_constant(name))
            {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg),
                         "Undefined variable '%s'", name.data);
                add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE,
                                   STRING_LITERAL(error_msg),
                                   node->line_number > 0 ? node->line_number
                                                         : 1);
            }
        }
        break;
    }

    case NODE_ASSIGNMENT:
    {
        /* Check assignment target and value */
        if (node->data.op.left)
        {
            if (node->data.op.left->type == NODE_IDENTIFIER)
            {
                const String var_name = node->data.op.left->data.name;
                SymbolEntry *symbol = NULL;

                if (!find_semantic_variable(analyzer, var_name, &symbol))
                {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg),
                             "Assignment to undefined variable '%s'",
                             var_name.data);
                    add_semantic_error(
                        analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE,
                        STRING_LITERAL(error_msg),
                        node->line_number > 0 ? node->line_number : 1);
                }
                else if (symbol && symbol->is_const)
                {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg),
                             "Cannot assign to const variable '%s'",
                             var_name.data);
                    add_semantic_error(
                        analyzer, SEMANTIC_ERROR_CONST_ASSIGNMENT,
                        STRING_LITERAL(error_msg),
                        node->line_number > 0 ? node->line_number : 1);
                }
            }
            else if (node->data.op.left->type != NODE_ARRAY_ACCESS &&
                     node->data.op.left->type != NODE_STRUCT_ACCESS &&
                     !(node->data.op.left->type == NODE_UNARY_OPERATION &&
                       node->data.op.left->data.unary.op == OP_DEREFERENCE))
            {
            }
            else
            {
                /* Analyze left side (could be array access, etc.) */
                semantic_analyze_node(analyzer, node->data.op.left);
            }
        }

        /* Analyze the right side (value being assigned) */
        if (node->data.op.right)
        {
            semantic_analyze_node(analyzer, node->data.op.right);
        }
        break;
    }

    case NODE_OPERATION:
    {
        /* Analyze both operands */
        if (node->data.op.left)
        {
            semantic_analyze_node(analyzer, node->data.op.left);
        }
        if (node->data.op.right)
        {
            semantic_analyze_node(analyzer, node->data.op.right);
        }
        break;
    }

    case NODE_IF_STATEMENT:
    {
        /* Analyze condition and branches */
        if (node->data.if_stmt.condition)
        {
            semantic_analyze_node(analyzer, node->data.if_stmt.condition);
        }

        /* Create new scope for then branch */
        enter_semantic_scope(analyzer, false);
        if (node->data.if_stmt.then_branch)
        {
            semantic_analyze_node(analyzer, node->data.if_stmt.then_branch);
        }
        exit_semantic_scope(analyzer);

        /* Create new scope for else branch if it exists */
        if (node->data.if_stmt.else_branch)
        {
            enter_semantic_scope(analyzer, false);
            semantic_analyze_node(analyzer, node->data.if_stmt.else_branch);
            exit_semantic_scope(analyzer);
        }
        break;
    }

    case NODE_FOR_STATEMENT:
    {
        /* Enter new scope for the for loop */
        enter_semantic_scope(analyzer, false);

        /* Analyze init, condition, increment, and body */
        if (node->data.for_stmt.init)
        {
            semantic_analyze_node(analyzer, node->data.for_stmt.init);
        }
        if (node->data.for_stmt.cond)
        {
            semantic_analyze_node(analyzer, node->data.for_stmt.cond);
        }
        if (node->data.for_stmt.incr)
        {
            semantic_analyze_node(analyzer, node->data.for_stmt.incr);
        }
        if (node->data.for_stmt.body)
        {
            semantic_analyze_node(analyzer, node->data.for_stmt.body);
        }

        exit_semantic_scope(analyzer);
        break;
    }

    case NODE_WHILE_STATEMENT:
    {
        /* Analyze condition */
        if (node->data.while_stmt.cond)
        {
            semantic_analyze_node(analyzer, node->data.while_stmt.cond);
        }

        /* Enter new scope for loop body */
        enter_semantic_scope(analyzer, false);
        if (node->data.while_stmt.body)
        {
            semantic_analyze_node(analyzer, node->data.while_stmt.body);
        }
        exit_semantic_scope(analyzer);
        break;
    }

    case NODE_FUNC_CALL:
    {
        /* Analyze function arguments */
        ArgumentList *args = node->data.func_call.arguments;
        while (args)
        {
            if (args->expr)
            {
                semantic_analyze_node(analyzer, args->expr);
            }
            args = args->next;
        }
        break;
    }

    case NODE_UNARY_OPERATION:
    {
        if (node->data.unary.operand)
        {
            semantic_analyze_node(analyzer, node->data.unary.operand);
        }
        if (node->data.unary.op == OP_ADDRESS_OF)
        {
            ASTNode *operand = node->data.unary.operand;
            bool valid_lvalue =
                operand && (operand->type == NODE_IDENTIFIER ||
                            operand->type == NODE_ARRAY_ACCESS ||
                            (operand->type == NODE_UNARY_OPERATION &&
                             operand->data.unary.op == OP_DEREFERENCE));
            if (!valid_lvalue)
            {
                add_semantic_error(
                    analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                    STRING_LITERAL(
                        "Address-of requires an assignable expression"),
                    node->line_number > 0 ? node->line_number : 1);
            }
        }
        else if (node->data.unary.op == OP_DEREFERENCE)
        {
            if (infer_expression_pointer_level(node->data.unary.operand,
                                               analyzer) <= 0)
            {
                add_semantic_error(
                    analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                    STRING_LITERAL(
                        "Cannot dereference a non-pointer expression"),
                    node->line_number > 0 ? node->line_number : 1);
            }
        }
        break;
    }

    default:
        /* For other node types, recursively visit children if they exist */
        if (node->data.op.left)
            semantic_analyze_node(analyzer, node->data.op.left);
        if (node->data.op.right)
            semantic_analyze_node(analyzer, node->data.op.right);
        break;
    }
}

/* Semantic scope management functions */
SemanticScope *create_semantic_scope(SemanticScope *parent,
                                     bool is_function_scope)
{
    SemanticScope *scope = SAFE_MALLOC(SemanticScope);
    if (!scope)
        return NULL;

    scope->variables = hm_new();
    scope->functions = hm_new();
    scope->parent = parent;
    scope->is_function_scope = is_function_scope;
    scope->depth = parent ? parent->depth + 1 : 0;

    return scope;
}

void free_semantic_scope(SemanticScope *scope)
{
    if (!scope)
        return;

    if (scope->variables)
    {
        hm_free(scope->variables);
    }
    if (scope->functions)
    {
        hm_free(scope->functions);
    }
    SAFE_FREE(scope);
}

void enter_semantic_scope(SemanticAnalyzer *analyzer, bool is_function_scope)
{
    if (!analyzer)
        return;

    SemanticScope *new_scope =
        create_semantic_scope(analyzer->current_scope, is_function_scope);
    if (!new_scope)
        return;

    analyzer->current_scope = new_scope;
    analyzer->scope_depth++;
}

void exit_semantic_scope(SemanticAnalyzer *analyzer)
{
    if (!analyzer || !analyzer->current_scope)
        return;

    SemanticScope *old_scope = analyzer->current_scope;
    analyzer->current_scope = old_scope->parent;
    analyzer->scope_depth--;

    free_semantic_scope(old_scope);
}

bool add_semantic_variable(SemanticAnalyzer *analyzer, const String name,
                           VarType type, int pointer_level, bool is_const)
{
    if (!analyzer || !analyzer->current_scope || !name.data)
        return false;

    /* Check if variable already exists in current scope */
    if (hm_get(analyzer->current_scope->variables, name.data, name.len + 1))
    {
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg),
                 "Variable '%s' already declared in current scope", name.data);
        add_semantic_error(analyzer, SEMANTIC_ERROR_REDEFINITION,
                           STRING_LITERAL(error_msg), 1);
        return false;
    }

    /* Create symbol entry */
    SymbolEntry *entry = SAFE_MALLOC(SymbolEntry);
    if (!entry)
        return false;

    entry->name = safe_strdup(&name);
    entry->type = type;
    entry->pointer_level = pointer_level;
    entry->is_const = is_const;
    entry->is_function = false;
    entry->return_type = NONE;
    entry->return_pointer_level = 0;
    entry->scope_depth = analyzer->scope_depth;
    entry->line_number = 1;
    entry->next = NULL;

    /* Add to current scope */
    hm_put(analyzer->current_scope->variables, name.data, name.len + 1, entry,
           sizeof(SymbolEntry *));

    return true;
}

bool find_semantic_variable(SemanticAnalyzer *analyzer, const String name,
                            SymbolEntry **result)
{
    if (!analyzer || !name.data || !result)
        return false;

    *result = NULL;

    /* Search through scope chain */
    SemanticScope *scope = analyzer->current_scope;
    while (scope)
    {
        SymbolEntry **entry_ptr =
            (SymbolEntry **)hm_get(scope->variables, name.data, name.len + 1);
        if (entry_ptr && *entry_ptr)
        {
            *result = *entry_ptr;
            return true;
        }
        scope = scope->parent;
    }

    return false;
}
