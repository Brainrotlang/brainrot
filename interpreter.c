/* interpreter.c - Interpreter visitor implementation */

#include "interpreter.h"
#include "ast.h"
#include "stdrot.h"
#include "lib/mem.h"
#include <stdio.h>

extern void yyerror(const char *s);
extern void execute_switch_statement(ASTNode *node);
extern String evaluate_expression_string(ASTNode *node);
extern void *evaluate_multi_array_access(ASTNode *node);

/* Global pointer to current interpreter for function calls */
Interpreter *current_interpreter = NULL;

/* Create a new interpreter */
Interpreter *interpreter_new(void)
{
    Interpreter *interp = SAFE_MALLOC(Interpreter);
    if (!interp)
    {
        yyerror("Failed to allocate memory for interpreter");
        return NULL;
    }

    /* Initialize visitor function pointers for expressions */
    interp->base.visit_int_literal = interpreter_visit_int_literal;
    interp->base.visit_float_literal = interpreter_visit_float_literal;
    interp->base.visit_double_literal = interpreter_visit_double_literal;
    interp->base.visit_char_literal = interpreter_visit_char_literal;
    interp->base.visit_short_literal = interpreter_visit_short_literal;
    interp->base.visit_boolean_literal = interpreter_visit_boolean_literal;
    interp->base.visit_string_literal = interpreter_visit_string_literal;
    interp->base.visit_identifier = interpreter_visit_identifier;
    interp->base.visit_binary_operation = interpreter_visit_binary_operation;
    interp->base.visit_unary_operation = interpreter_visit_unary_operation;
    interp->base.visit_array_access = interpreter_visit_array_access;
    interp->base.visit_function_call = interpreter_visit_function_call;
    interp->base.visit_sizeof = interpreter_visit_sizeof;

    /* Initialize visitor function pointers for statements */
    interp->base.visit_declaration = interpreter_visit_declaration;
    interp->base.visit_assignment = interpreter_visit_assignment;
    interp->base.visit_if_statement = interpreter_visit_if_statement;
    interp->base.visit_for_statement = interpreter_visit_for_statement;
    interp->base.visit_while_statement = interpreter_visit_while_statement;
    interp->base.visit_do_while_statement =
        interpreter_visit_do_while_statement;
    interp->base.visit_switch_statement = interpreter_visit_switch_statement;
    interp->base.visit_break_statement = interpreter_visit_break_statement;
    interp->base.visit_return_statement = interpreter_visit_return_statement;
    interp->base.visit_function_definition =
        interpreter_visit_function_definition;
    interp->base.visit_statement_list = interpreter_visit_statement_list;
    interp->base.visit_print_statement = interpreter_visit_print_statement;
    interp->base.visit_error_statement = interpreter_visit_error_statement;

    /* Initialize interpreter state */
    interp->current_scope = current_scope;
    interp->return_value.has_value = false;
    interp->should_break = false;
    interp->should_return = false;

    return interp;
}

/* Free interpreter */
void interpreter_free(Interpreter *interp)
{
    if (interp)
    {
        SAFE_FREE(interp);
    }
}

/* Main interpretation function */
void interpret(ASTNode *root, Interpreter *interp)
{
    if (!root || !interp)
        return;

    /* Set global interpreter pointer for function calls */
    current_interpreter = interp;

    /* Ensure there's a global scope for the visitor pattern */
    if (!current_scope)
    {
        enter_scope();
    }

    /* Execute the AST using visitor pattern */
    ast_accept(root, (Visitor *)interp);

    /* Clear global interpreter pointer */
    current_interpreter = NULL;
}

/* Expression visitor implementations */

void *interpreter_visit_int_literal(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory
     * since we're using this for side effects, not expression evaluation */
    return NULL;
}

void *interpreter_visit_float_literal(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void *interpreter_visit_double_literal(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void *interpreter_visit_char_literal(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void *interpreter_visit_short_literal(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void *interpreter_visit_boolean_literal(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void *interpreter_visit_string_literal(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void *interpreter_visit_identifier(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node || !node->data.name.data)
        return NULL;

    Variable *var = get_variable(node->data.name);
    if (!var)
    {
        /* Not a variable -- an unscoped enum constant (e.g. bare `GREEN`
           on the right-hand side of `favorite = GREEN;`) is expected here,
           not an error; the actual value is resolved separately by
           whichever evaluate_expression_* call handles this node. */
        if (find_global_enum_constant(node->data.name) != NULL)
            return NULL;
        yyerror("Undefined variable");
        return NULL;
    }

    /* For interpreter visitor, we don't need to return allocated memory
     * since this is used for side effects, not expression evaluation */
    return NULL;
}

void *interpreter_visit_binary_operation(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node || !node->data.op.left || !node->data.op.right)
        return NULL;

    /* For interpreter visitor, we don't need to return allocated memory
     * since this is used for side effects, not expression evaluation */
    return NULL;
}

void *interpreter_visit_unary_operation(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;

    if (node->data.unary.op == OP_PRE_INC ||
        node->data.unary.op == OP_PRE_DEC ||
        node->data.unary.op == OP_POST_INC ||
        node->data.unary.op == OP_POST_DEC)
    {
        evaluate_expression_int(node);
    }
    return NULL;
}

void *interpreter_visit_array_access(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;

    /* WORKAROUND: If num_dimensions is 0 but we know this is array access,
     * attempt recovery */
    if (node->data.array.num_dimensions == 0)
    {
        /* Get the variable to determine expected dimensions */
        Variable *var = get_variable(node->data.array.name);
        if (!var || !var->is_array)
        {
            return NULL;
        }

        /* For now, assume single dimension access and try to find the index
         * expression */
        /* Create a temporary fixed node structure */
        ASTNode temp_node = *node;
        temp_node.data.array.num_dimensions = 1;

        /* Try to recover the index expression from the old single-index field
         */
        if (node->data.array.index)
        {
            temp_node.data.array.indices[0] = node->data.array.index;
        }
        else
        {
            /* Create a dummy index of 0 - this is the fallback */
            ASTNode *zero_node = create_int_node(0);
            temp_node.data.array.indices[0] = zero_node;
        }

        return evaluate_multi_array_access(&temp_node);
    }

    /* Use the existing array access implementation */
    return evaluate_multi_array_access(node);
}

/* Executes a function call that is genuinely a bare statement -- the one
   unambiguous point where a call's result is intentionally discarded and
   the deprecated native write-back convention should still apply. Used
   directly (bypassing ast_accept()/interpreter_visit_function_call()) at
   every site where a bare NODE_FUNC_CALL has no other visitor coming along
   to evaluate it for real: statement-list entries, and a for-loop's own
   init/increment clause. See interpreter_visit_statement_list() and
   interpreter_visit_for_statement(), the two callers. */
static void interpreter_execute_call_statement(ASTNode *node)
{
    if (!node)
        return;

    const String func_name = node->data.func_call.function_name;
    ArgumentList *args = node->data.func_call.arguments;

    if (is_builtin_function(func_name))
    {
        execute_builtin_function(func_name, args);
    }
    else
    {
        /* Handle user-defined functions directly without return value
         * allocation */
        execute_function_call(func_name, args);

        /* A call in statement position discards its result -- if that
         * result was a struct, free the blob handle_return_statement
         * allocated for it rather than leaving it to a later call's
         * cleanup (or leaking it, if this was the last call). */
        free_pending_return_value();
    }
}

/* ast_accept()'s generic pre-visit runs this on a NODE_FUNC_CALL reached as
   part of a declaration/assignment/return/print/error statement's
   right-hand expression, or (via visitor.c's NODE_DO_WHILE_STATEMENT case)
   a do-while condition -- in every one of those cases, the statement's own
   dedicated visitor (interpreter_visit_declaration et al., or this
   interpreter's own per-iteration evaluate_expression_int() condition
   check) is about to evaluate this exact node for real via
   evaluate_expression_* / handle_function_call. That pre-visit exists so
   shared visitors (e.g. the semantic analyzer, validating the call exists)
   get a chance to look at it; it is not itself a place where executing the
   call is correct. Doing so anyway is actively wrong, not just redundant:
   for a self-referential declaration like `rizz n = slorp(n);`, the
   pre-visit runs before interpreter_visit_declaration has created `n`, so
   the argument silently evaluates to nothing and the (wrong) result gets
   cached; for a do-while condition, the pre-visit runs before the loop
   body has executed even once, so the first real check reads a stale
   pre-loop value instead of re-evaluating. Do nothing here and let the
   downstream evaluate_expression_*() call populate the memo cache itself,
   at the right time. (Bare statement-position and for-loop init/incr
   calls never reach here at all -- see interpreter_execute_call_statement()
   above.) User-defined functions have no such cache and are still invoked
   from both places -- a pre-existing gap, not introduced here, tracked
   separately from native-call support. */
void *interpreter_visit_function_call(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;

    if (!is_builtin_function(node->data.func_call.function_name))
    {
        execute_function_call(node->data.func_call.function_name,
                              node->data.func_call.arguments);
        free_pending_return_value();
    }

    return NULL;
}

/* ast_accept(), but a bare NODE_FUNC_CALL is executed directly via
   interpreter_execute_call_statement() instead of going through
   interpreter_visit_function_call()'s pre-visit no-op. Use this at any site
   where a bare call has no other visitor coming along afterward to
   evaluate it for real -- currently statement-list entries and a for
   loop's own init/increment clause. A declaration/assignment (etc.)
   wrapping a call is unaffected: it still goes through ast_accept()
   normally, since interpreter_visit_declaration() et al. *are* that real
   evaluation. */
static void interpreter_accept_or_execute_call(ASTNode *node, Visitor *self)
{
    if (!node)
        return;
    if (node->type == NODE_FUNC_CALL)
        interpreter_execute_call_statement(node);
    else
        ast_accept(node, self);
}

void *interpreter_visit_sizeof(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return NULL;

    /* For the interpreter visitor, we don't need to return allocated memory
     * The sizeof operation can be handled by the existing evaluation system */
    handle_sizeof(node);
    return NULL;
}

/* Statement visitor implementations */

void interpreter_visit_declaration(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node || !node->data.op.left || !node->data.op.left->data.name.data)
        return;

    String name = node->data.op.left->data.name;

    /* Array declarations: create + populate storage here, at runtime, in
     * whatever scope is current for this execution -- a function's own
     * scope when this statement is inside a function body, or the shared
     * global scope at top level. Doing this at runtime (instead of once at
     * parse time, into whatever scope happened to be active while parsing)
     * is what gives each call its own array instance and makes a
     * function-local array visible inside the function that declares it. */
    if (node->is_array &&
        (node->var_type != VAR_STRUCT || node->pointer_level > 0))
    {
        if (node->modifiers.is_static && get_variable(name))
            return;

        Variable *var = variable_new(name);
        var->pointer_level = node->pointer_level;
        var->modifiers = node->modifiers;
        if (node->var_type == VAR_STRUCT)
            var->struct_name = safe_strdup(&node->struct_name);
        add_variable_to_scope(name, var);
        SAFE_FREE(var);

        int dims[MAX_DIMENSIONS];
        int num_dims = node->array_dimensions.num_dimensions;
        for (int i = 0; i < num_dims; i++)
            dims[i] = node->array_dimensions.dimensions[i];

        if (!set_multi_array_variable(name, dims, num_dims, node->modifiers,
                                      node->var_type))
        {
            yyerror("Failed to create array");
            return;
        }

        if (node->pending_initializer)
        {
            populate_multi_array_variable(name, node->pending_initializer, dims,
                                          num_dims);
        }
        return;
    }

    /* Struct/union declarations: same reasoning as arrays above -- create
     * the Variable and its data blob here, at runtime, in the current
     * scope, instead of at parse time. */
    if (node->pointer_level == 0 &&
        (node->var_type == VAR_STRUCT ||
         (node->data.op.right && node->data.op.right->type == NODE_STRUCT_DEF)))
    {
        const String struct_type =
            node->data.op.right ? node->data.op.right->data.struct_def.name
                                : (String){.data = NULL, .len = 0};
        if (!struct_type.data)
            return;

        if (node->modifiers.is_static && get_variable(name))
            return;

        Variable *var = variable_new(name);
        var->var_type = VAR_STRUCT;
        var->pointer_level = node->pointer_level;
        var->modifiers = node->modifiers;
        var->struct_name = safe_strdup(&struct_type);
        add_variable_to_scope(name, var);
        SAFE_FREE(var);

        Variable *sv = get_variable(name);
        StructDef *def = get_struct_def(struct_type);
        if (sv && def && !sv->value.array_data)
        {
            sv->value.array_data = calloc(1, def->total_size);
            if (node->pending_initializer)
                populate_struct_variable(name, node->pending_initializer);
            else if (node->struct_init_expr)
            {
                ASTNode *src_expr = node->struct_init_expr;
                if (src_expr->type == NODE_FUNC_CALL)
                {
                    execute_function_call(
                        src_expr->data.func_call.function_name,
                        src_expr->data.func_call.arguments);
                    if (current_return_value.has_value &&
                        current_return_value.type == VAR_STRUCT)
                    {
                        void *blob = (void *)current_return_value.value.pvalue;
                        /* Guard against copying a differently-shaped struct
                           into this blob (e.g. `gang Big b = make_small();`)
                           -- struct_name identifies the *declared* return
                           type, which the semantic analyzer should already
                           have rejected if it mismatches struct_type, but
                           this is the last line of defense against an
                           out-of-bounds memcpy. */
                        if (blob && sv->value.array_data &&
                            current_return_value.struct_name.data &&
                            strcmp(current_return_value.struct_name.data,
                                   struct_type.data) == 0)
                        {
                            memcpy(sv->value.array_data, blob, def->total_size);
                        }
                        else if (blob)
                        {
                            yyerror("Struct return type does not match "
                                    "declared type");
                        }
                        /* Ownership transfers to us on return; always free
                           our copy of the temporary, whether or not the
                           type check above allowed the memcpy. */
                        free_pending_return_value();
                    }
                }
                else if (src_expr->type == NODE_IDENTIFIER)
                {
                    Variable *src = get_variable(src_expr->data.name);
                    if (src && src->var_type == VAR_STRUCT &&
                        src->value.array_data && sv->value.array_data &&
                        src->struct_name.data &&
                        strcmp(src->struct_name.data, struct_type.data) == 0)
                    {
                        memcpy(sv->value.array_data, src->value.array_data,
                               def->total_size);
                    }
                    else if (src && src->var_type == VAR_STRUCT)
                    {
                        yyerror("Cannot copy-initialize from a struct "
                                "variable of a different type");
                    }
                }
            }
        }
        return;
    }
    Variable *var = variable_new(name);
    var->modifiers = node->modifiers;
    var->var_type = node->var_type;
    var->pointer_level = node->pointer_level;

    /* If static and already exists in static map, skip entirely */
    if (node->modifiers.is_static)
    {
        Variable *existing = get_variable(name);
        if (existing)
        {
            SAFE_FREE(var);
            return;
        }
    }

    /* Must come after the static-already-exists check above: that early
       return frees only the wrapper Variable, not enum_name's heap string. */
    if (node->var_type == VAR_ENUM)
        var->enum_name = safe_strdup(&node->enum_name);
    if (node->var_type == VAR_STRUCT && node->struct_name.data)
        var->struct_name = safe_strdup(&node->struct_name);

    /* Detect struct declaration: right node is a NODE_STRUCT_DEF */
    if (node->data.op.right && node->data.op.right->type == NODE_STRUCT_DEF)
    {
        var->var_type = VAR_STRUCT;
        var->struct_name =
            safe_strdup(&node->data.op.right->data.struct_def.name);
    }

    add_variable_to_scope(name, var);
    SAFE_FREE(var);

    /* Handle initialization */
    if (node->data.op.right)
    {
        Variable *scope_var = get_variable(name);
        if (scope_var)
        {
            if (scope_var->pointer_level > 0)
            {
                /* For a struct/union-tagged pointer declared via the
                   `struct_or_union name_token declarator ...` grammar
                   (lang.y) -- `gang Foo *pp;` / `gang Foo *pp = &f;` --
                   data.op.right always points at a NODE_STRUCT_DEF type
                   marker (the struct tag) regardless of whether an
                   initializer was given, including the no-initializer
                   form; the real initializer, when present, lives
                   separately in struct_init_expr (see that grammar's
                   `EQUALS expression` production). evaluate_expression_
                   pointer() doesn't handle NODE_STRUCT_DEF, so blindly
                   evaluating data.op.right here either silently left the
                   pointer NULL for `gang Foo *pp = &f;` (the real
                   initializer was never read) or, for a bare `gang Foo
                   *pp;`, spuriously reported "Invalid pointer expression"
                   even though no initializer was ever written and
                   pvalue's zero default (variable_new() memsets the whole
                   Variable) is already correct. A `lit`-aliased struct
                   pointer (`PointPtr first = &p;`) is declared through a
                   *different*, generic type-declarator grammar path where
                   data.op.right already holds the real initializer
                   directly, same as any non-struct pointer (`rizz *p =
                   &x;`) -- so only reroute through struct_init_expr when
                   data.op.right is genuinely that type marker, not for
                   every VAR_STRUCT pointer declaration. */
                if (node->var_type == VAR_STRUCT && node->data.op.right &&
                    node->data.op.right->type == NODE_STRUCT_DEF)
                {
                    if (node->struct_init_expr)
                        scope_var->value.pvalue =
                            evaluate_expression_pointer(node->struct_init_expr);
                }
                else
                {
                    scope_var->value.pvalue =
                        evaluate_expression_pointer(node->data.op.right);
                }
                return;
            }
            if (scope_var->var_type == VAR_STRUCT)
            {
                if (!scope_var->value.array_data)
                {
                    StructDef *def = get_struct_def(scope_var->struct_name);
                    if (def)
                    {
                        scope_var->value.array_data =
                            calloc(1, def->total_size);
                        hm_put(current_scope->variables, name.data, name.len,
                               scope_var, sizeof(Variable));
                    }
                }
                return;
            }
            switch (scope_var->var_type)
            {
            case VAR_INT:
            {
                int int_value = evaluate_expression_int(node->data.op.right);
                scope_var->value.ivalue = int_value;
                break;
            }
            case VAR_FLOAT:
            {
                float float_value =
                    evaluate_expression_float(node->data.op.right);
                scope_var->value.fvalue = float_value;
                break;
            }
            case VAR_DOUBLE:
            {
                double double_value =
                    evaluate_expression_double(node->data.op.right);
                scope_var->value.dvalue = double_value;
                break;
            }
            case VAR_CHAR:
            {
                /* char_scalar_slot_value() (ast.h/ast.c): every write
                   into a scalar VAR_CHAR Variable's own slot must zero-
                   extend, not plain-widen, so a later narrower 1-byte
                   write through a `yap *` alias into this same slot
                   can't leave stale bytes behind. */
                int int_value = evaluate_expression_int(node->data.op.right);
                scope_var->value.ivalue = char_scalar_slot_value(int_value);
                break;
            }
            case VAR_SHORT:
            {
                short short_value =
                    evaluate_expression_short(node->data.op.right);
                scope_var->value.svalue = short_value;
                break;
            }
            case VAR_BOOL:
            {
                bool bool_value = evaluate_expression_bool(node->data.op.right);
                scope_var->value.bvalue = bool_value;
                break;
            }
            case VAR_STRING:
            {
                String string_value =
                    evaluate_expression_string(node->data.op.right);
                scope_var->value.strvalue = string_value;
                break;
            }
            case VAR_ENUM:
            {
                int int_value = evaluate_expression_int(node->data.op.right);
                scope_var->value.ivalue = int_value;
                break;
            }
            default:
                break;
            }
        }
    }
}

void interpreter_visit_assignment(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node || !node->data.op.left || !node->data.op.right)
        return;

    execute_assignment(node);
}

void interpreter_visit_if_statement(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return;

    int condition = evaluate_expression_int(node->data.if_stmt.condition);

    if (condition)
    {
        if (node->data.if_stmt.then_branch)
        {
            ast_accept(node->data.if_stmt.then_branch, (Visitor *)self);
        }
    }
    else if (node->data.if_stmt.else_branch)
    {
        ast_accept(node->data.if_stmt.else_branch, (Visitor *)self);
    }
}

void interpreter_visit_for_statement(Visitor *self, ASTNode *node)
{
    if (!node)
        return;

    PUSH_JUMP_BUFFER();
    if (setjmp(CURRENT_JUMP_BUFFER()) == 0)
    {
        enter_scope();

        if (node->data.for_stmt.init)
        {
            interpreter_accept_or_execute_call(node->data.for_stmt.init, self);
        }

        while (1)
        {
            enter_scope();
            if (node->data.for_stmt.cond)
            {
                int cond_result =
                    evaluate_expression_int(node->data.for_stmt.cond);
                if (!cond_result)
                {
                    exit_scope();
                    break;
                }
            }

            if (node->data.for_stmt.body)
            {
                ast_accept(node->data.for_stmt.body, self);
            }

            if (node->data.for_stmt.incr)
            {
                interpreter_accept_or_execute_call(node->data.for_stmt.incr,
                                                   self);
            }
            exit_scope();
        }

        exit_scope();
    }
    POP_JUMP_BUFFER();
}

void interpreter_visit_while_statement(Visitor *self, ASTNode *node)
{
    if (!node)
        return;

    PUSH_JUMP_BUFFER();
    enter_scope();
    while (evaluate_expression_int(node->data.while_stmt.cond) &&
           setjmp(CURRENT_JUMP_BUFFER()) == 0)
    {
        enter_scope();

        if (node->data.while_stmt.body)
        {
            ast_accept(node->data.while_stmt.body, self);
        }

        exit_scope();
    }
    exit_scope();
    POP_JUMP_BUFFER();
}

void interpreter_visit_do_while_statement(Visitor *self, ASTNode *node)
{
    if (!node)
        return;

    /* Use setjmp/longjmp for break handling like the old code */
    PUSH_JUMP_BUFFER();
    enter_scope();
    do
    {
        /* Enter new scope for each iteration */
        enter_scope();

        /* Execute the body using the visitor */
        if (node->data.while_stmt.body)
        {
            ast_accept(node->data.while_stmt.body, self);
        }

        /* Exit scope before checking condition */
        exit_scope();

    } while (evaluate_expression_int(node->data.while_stmt.cond) &&
             setjmp(CURRENT_JUMP_BUFFER()) == 0);
    exit_scope();
    POP_JUMP_BUFFER();
}

void interpreter_visit_switch_statement(Visitor *self, ASTNode *node)
{
    (void)self;
    execute_switch_statement(node);
}

void interpreter_visit_break_statement(Visitor *self, ASTNode *node)
{
    (void)node;
    (void)self;
    /* Use bruh() which calls LONGJMP() to break out of the current loop */
    bruh();
}

void interpreter_visit_return_statement(Visitor *self, ASTNode *node)
{
    (void)self;
    if (node && node->data.op.left)
    {
        handle_return_statement(node->data.op.left);
    }
    else
    {
        handle_return_statement(NULL);
    }
}

void interpreter_visit_function_definition(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node)
        return;

    if (node->data.function_def.return_type == VAR_STRUCT &&
        node->pointer_level > 0)
    {
        /* Pointer-to-struct returns are rejected at parse time (see
           create_function_def_node_struct) and deliberately left
           unregistered there. create_function()/create_function_ex()
           below have no knowledge of that rejection and would happily
           register the function anyway -- the unconditional call a few
           lines down registers it with return_pointer_level 0 regardless,
           and the pointer_level > 0 branch after it would then "fix" that
           up to the real pointer_level, undoing the rejection and letting
           the function run with a silently-wrong by-value return. Skip
           registration entirely so get_function() stays NULL and any call
           site reports a clear "Undefined function" instead. */
        return;
    }

    Function *func = create_function(
        node->data.function_def.name, node->data.function_def.return_type,
        node->data.function_def.parameters, node->data.function_def.body);

    if (node->pointer_level > 0)
    {
        func = create_function_ex(
            node->data.function_def.name, node->data.function_def.return_type,
            node->pointer_level, node->data.function_def.parameters,
            node->data.function_def.body);
    }

    if (!func)
    {
        yyerror("Failed to create function");
        exit(1);
    }
}

void interpreter_visit_statement_list(Visitor *self, ASTNode *node)
{
    if (!node)
        return;

    /* Manually traverse all statements in the list */
    StatementList *stmt = node->data.statements;
    while (stmt)
    {
        if (stmt->statement)
            interpreter_accept_or_execute_call(stmt->statement, self);
        stmt = stmt->next;
    }
}

void interpreter_visit_print_statement(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node || !node->data.op.left)
        return;

    ASTNode *expr = node->data.op.left;
    ArgumentList args = {expr, NULL};
    execute_func_call((String){.data = "yapping", .len = sizeof("yapping")},
                      &args);
}

void interpreter_visit_error_statement(Visitor *self, ASTNode *node)
{
    (void)self;
    if (!node || !node->data.op.left)
        return;

    ASTNode *expr = node->data.op.left;
    ArgumentList args = {expr, NULL};
    execute_func_call((String){.data = "baka", .len = sizeof("baka")}, &args);
}
