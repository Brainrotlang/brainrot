/* interpreter.c - Interpreter visitor implementation */

#include "interpreter.h"
#include "ast.h"
#include "stdrot.h"
#include "lib/mem.h"
#include <stdio.h>

extern void yyerror(const char *s);
extern char *safe_strdup(const char *str);
extern void execute_func_call(const char *func_name, ArgumentList *args);

/* External functions we need from the original implementation */
extern Variable *variable_new(char *name);
extern void add_variable_to_scope(const char *name, Variable *var);
extern Variable *get_variable(const char *name);
extern Function *get_function(const char *name);
extern bool is_builtin_function(const char *name);
extern void execute_builtin_function(const char *name, ArgumentList *args);
extern void execute_assignment(ASTNode *node);
extern void execute_for_statement(ASTNode *node);
extern void execute_while_statement(ASTNode *node);
extern void execute_do_while_statement(ASTNode *node);
extern void execute_switch_statement(ASTNode *node);
extern void handle_return_statement(ASTNode *expr);
extern Function *create_function(char *name, VarType return_type, Parameter *params, ASTNode *body);
extern void bruh(void);

/* For now, use the original evaluation system with visitor wrappers */
extern int evaluate_expression_int(ASTNode *node);
extern float evaluate_expression_float(ASTNode *node);
extern double evaluate_expression_double(ASTNode *node);
extern short evaluate_expression_short(ASTNode *node);
extern bool evaluate_expression_bool(ASTNode *node);
extern char *evaluate_expression_string(ASTNode *node);
extern void *evaluate_multi_array_access(ASTNode *node);
extern void *handle_function_call(ASTNode *node);
extern size_t handle_sizeof(ASTNode *node);

/* Global pointer to current interpreter for function calls */
Interpreter* current_interpreter = NULL;

/* Create a new interpreter */
Interpreter* interpreter_new(void) {
    Interpreter *interp = SAFE_MALLOC(Interpreter);
    if (!interp) {
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
    interp->base.visit_do_while_statement = interpreter_visit_do_while_statement;
    interp->base.visit_switch_statement = interpreter_visit_switch_statement;
    interp->base.visit_break_statement = interpreter_visit_break_statement;
    interp->base.visit_return_statement = interpreter_visit_return_statement;
    interp->base.visit_function_definition = interpreter_visit_function_definition;
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
void interpreter_free(Interpreter *interp) {
    if (interp) {
        SAFE_FREE(interp);
    }
}

/* Main interpretation function */
void interpret(ASTNode *root, Interpreter *interp) {
    if (!root || !interp) return;
    
    extern Scope* current_scope;
    
    /* Set global interpreter pointer for function calls */
    current_interpreter = interp;
    
    /* Ensure there's a global scope for the visitor pattern */
    if (!current_scope) {
        extern void enter_scope();
        enter_scope();
    }
    
    /* Execute the AST using visitor pattern */
    ast_accept(root, (Visitor*)interp);
    
    /* Clear global interpreter pointer */
    current_interpreter = NULL;
}

/* Expression visitor implementations */

void* interpreter_visit_int_literal(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory
     * since we're using this for side effects, not expression evaluation */
    return NULL;
}

void* interpreter_visit_float_literal(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void* interpreter_visit_double_literal(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void* interpreter_visit_char_literal(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void* interpreter_visit_short_literal(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void* interpreter_visit_boolean_literal(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void* interpreter_visit_string_literal(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    /* For the interpreter visitor, we don't need to return allocated memory */
    return NULL;
}

void* interpreter_visit_identifier(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node || !node->data.name) return NULL;
    
    Variable *var = get_variable(node->data.name);
    if (!var) {
        yyerror("Undefined variable");
        return NULL;
    }
    
    /* For interpreter visitor, we don't need to return allocated memory
     * since this is used for side effects, not expression evaluation */
    return NULL;
}

void* interpreter_visit_binary_operation(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node || !node->data.op.left || !node->data.op.right) return NULL;
    
    /* For interpreter visitor, we don't need to return allocated memory
     * since this is used for side effects, not expression evaluation */
    return NULL;
}

void* interpreter_visit_unary_operation(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    
    if (node->data.unary.op == OP_PRE_INC || node->data.unary.op == OP_PRE_DEC ||
        node->data.unary.op == OP_POST_INC || node->data.unary.op == OP_POST_DEC) {
        evaluate_expression_int(node);
    }
    return NULL;
}

void* interpreter_visit_array_access(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    
    /* WORKAROUND: If num_dimensions is 0 but we know this is array access, attempt recovery */
    if (node->data.array.num_dimensions == 0) {
        /* Get the variable to determine expected dimensions */
        Variable *var = get_variable(node->data.array.name);
        if (!var || !var->is_array) {
            return NULL;
        }
        
        /* For now, assume single dimension access and try to find the index expression */
        /* Create a temporary fixed node structure */
        ASTNode temp_node = *node;
        temp_node.data.array.num_dimensions = 1;
        
        /* Try to recover the index expression from the old single-index field */
        if (node->data.array.index) {
            temp_node.data.array.indices[0] = node->data.array.index;
        } else {
            /* Create a dummy index of 0 - this is the fallback */
            ASTNode *zero_node = create_int_node(0);
            temp_node.data.array.indices[0] = zero_node;
        }
        
        return evaluate_multi_array_access(&temp_node);
    }
    
    /* Use the existing array access implementation */
    return evaluate_multi_array_access(node);
}

void* interpreter_visit_function_call(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    
    const char *func_name = node->data.func_call.function_name;
    ArgumentList *args = node->data.func_call.arguments;
    
    extern Scope* current_scope;
    
    /* Handle built-in functions */
    if (is_builtin_function(func_name)) {
        execute_builtin_function(func_name, args);
    } else {
        /* Handle user-defined functions directly without return value allocation */
        execute_function_call(func_name, args);
    }
    
    return NULL;
}

void* interpreter_visit_sizeof(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return NULL;
    
    /* For the interpreter visitor, we don't need to return allocated memory
     * The sizeof operation can be handled by the existing evaluation system */
    handle_sizeof(node);
    return NULL;
}

/* Statement visitor implementations */

void interpreter_visit_declaration(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node || !node->data.op.left || !node->data.op.left->data.name) return;
    
    char *name = node->data.op.left->data.name;
    /* Struct declarations: set up blob now (at runtime, after hashmap is stable) */
    if (node->var_type == VAR_STRUCT ||
        (node->data.op.right && node->data.op.right->type == NODE_STRUCT_DEF)) {
        const char *struct_type = node->data.op.right
                                  ? node->data.op.right->data.struct_def.name
                                  : NULL;
        if (!struct_type) return;
        Variable *sv = get_variable(name);
        if (sv && sv->var_type == VAR_STRUCT && !sv->value.array_data) {
            StructDef *def = get_struct_def(sv->struct_name ? sv->struct_name : struct_type);
            if (def) sv->value.array_data = calloc(1, def->total_size);
        }
        return;
    }
    Variable *var = variable_new(name);
    var->modifiers = node->modifiers;
    var->var_type = node->var_type;
    var->pointer_level = node->pointer_level;

    /* If static and already exists in static map, skip entirely */
    if (node->modifiers.is_static) {
        Variable *existing = get_variable(name);
        if (existing) {
            SAFE_FREE(var);
            return;
        }
    }

    /* Detect struct declaration: right node is a NODE_STRUCT_DEF */
    if (node->data.op.right && node->data.op.right->type == NODE_STRUCT_DEF) {
        var->var_type    = VAR_STRUCT;
        var->struct_name = safe_strdup(node->data.op.right->data.struct_def.name);
    }

    add_variable_to_scope(name, var);
    SAFE_FREE(var);

    /* Handle initialization */
    if (node->data.op.right) {
        Variable *scope_var = get_variable(name);
        if (scope_var && scope_var->var_type == VAR_STRUCT) {
            if (!scope_var->value.array_data) {
                StructDef *def = get_struct_def(scope_var->struct_name);
                if (def) {
                    scope_var->value.array_data = calloc(1, def->total_size);
                    hm_put(current_scope->variables, name, strlen(name),
                        scope_var, sizeof(Variable));
                }
            }
            return;
        }
        if (scope_var) {
            if (scope_var->pointer_level > 0) {
                scope_var->value.pvalue = evaluate_expression_pointer(node->data.op.right);
                return;
            }
            switch (scope_var->var_type) {
                case VAR_INT: {
                    int int_value = evaluate_expression_int(node->data.op.right);
                    scope_var->value.ivalue = int_value;
                    break;
                }
                case VAR_FLOAT: {
                    float float_value = evaluate_expression_float(node->data.op.right);
                    scope_var->value.fvalue = float_value;
                    break;
                }
                case VAR_DOUBLE: {
                    double double_value = evaluate_expression_double(node->data.op.right);
                    scope_var->value.dvalue = double_value;
                    break;
                }
                case VAR_CHAR: {
                    int int_value = evaluate_expression_int(node->data.op.right);
                    scope_var->value.ivalue = int_value;
                    break;
                }
                case VAR_SHORT: {
                    short short_value = evaluate_expression_short(node->data.op.right);
                    scope_var->value.svalue = short_value;
                    break;
                }
                case VAR_BOOL: {
                    bool bool_value = evaluate_expression_bool(node->data.op.right);
                    scope_var->value.bvalue = bool_value;
                    break;
                }
                case VAR_STRING: {
                    char *string_value = evaluate_expression_string(node->data.op.right);
                    scope_var->value.strvalue = string_value;
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void interpreter_visit_assignment(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node || !node->data.op.left || !node->data.op.right) return;

    execute_assignment(node);
}

void interpreter_visit_if_statement(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return;
    
    int condition = evaluate_expression_int(node->data.if_stmt.condition);
    
    if (condition) {
        if (node->data.if_stmt.then_branch) {
            ast_accept(node->data.if_stmt.then_branch, (Visitor*)self);
        }
    } else if (node->data.if_stmt.else_branch) {
        ast_accept(node->data.if_stmt.else_branch, (Visitor*)self);
    }
}

void interpreter_visit_for_statement(Visitor *self, ASTNode *node) {
    if (!node) return;
    
    extern void enter_scope();
    extern void exit_scope();
    
    PUSH_JUMP_BUFFER();
    if (setjmp(CURRENT_JUMP_BUFFER()) == 0) {
        enter_scope();
        
        if (node->data.for_stmt.init) {
            ast_accept(node->data.for_stmt.init, self);
        }
        
        while (1) {
            enter_scope();
            if (node->data.for_stmt.cond) {
                int cond_result = evaluate_expression_int(node->data.for_stmt.cond);
                if (!cond_result) {
                    exit_scope();
                    break;
                }
            }
            
            if (node->data.for_stmt.body) {
                ast_accept(node->data.for_stmt.body, self);
            }
            
            if (node->data.for_stmt.incr) {
                ast_accept(node->data.for_stmt.incr, self);
            }
            exit_scope();
        }
        
        exit_scope();
    }
    POP_JUMP_BUFFER();
}

void interpreter_visit_while_statement(Visitor *self, ASTNode *node) {
    if (!node) return;
    
    extern void enter_scope();
    extern void exit_scope();
    
    PUSH_JUMP_BUFFER();
    enter_scope();
    while (evaluate_expression_int(node->data.while_stmt.cond) && setjmp(CURRENT_JUMP_BUFFER()) == 0) {
        enter_scope();
        
        if (node->data.while_stmt.body) {
            ast_accept(node->data.while_stmt.body, self);
        }
        
        exit_scope();
    }
    exit_scope();
    POP_JUMP_BUFFER();
}

void interpreter_visit_do_while_statement(Visitor *self, ASTNode *node) {
    if (!node) return;
    
    /* Add external function declarations */
    extern void enter_scope();
    extern void exit_scope();
    
    /* Use setjmp/longjmp for break handling like the old code */
    PUSH_JUMP_BUFFER();
    enter_scope();
    do {
        /* Enter new scope for each iteration */
        enter_scope();
        
        /* Execute the body using the visitor */
        if (node->data.while_stmt.body) {
            ast_accept(node->data.while_stmt.body, self);
        }
        
        /* Exit scope before checking condition */
        exit_scope();
        
    } while (evaluate_expression_int(node->data.while_stmt.cond) && setjmp(CURRENT_JUMP_BUFFER()) == 0);
    exit_scope();
    POP_JUMP_BUFFER();
}

void interpreter_visit_switch_statement(Visitor *self, ASTNode *node) {
    (void)self;
    execute_switch_statement(node);
}

void interpreter_visit_break_statement(Visitor *self, ASTNode *node) {
    (void)node;
    (void)self;
    /* Use bruh() which calls LONGJMP() to break out of the current loop */
    bruh();
}

void interpreter_visit_return_statement(Visitor *self, ASTNode *node) {
    (void)self;
    if (node && node->data.op.left) {
        handle_return_statement(node->data.op.left);
    } else {
        handle_return_statement(NULL);
    }
}

void interpreter_visit_function_definition(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return;
    
    Function *func = create_function(
        node->data.function_def.name,
        node->data.function_def.return_type,
        node->data.function_def.parameters,
        node->data.function_def.body);
    
    if (node->pointer_level > 0) {
        func = create_function_ex(
            node->data.function_def.name,
            node->data.function_def.return_type,
            node->pointer_level,
            node->data.function_def.parameters,
            node->data.function_def.body);
    }
    
    if (!func) {
        yyerror("Failed to create function");
        exit(1);
    }
}

void interpreter_visit_statement_list(Visitor *self, ASTNode *node) {
    if (!node) return;
    
    /* Manually traverse all statements in the list */
    StatementList *stmt = node->data.statements;
    while (stmt) {
        if (stmt->statement)
            ast_accept(stmt->statement, self);
        stmt = stmt->next;
    }
}

void interpreter_visit_print_statement(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node || !node->data.op.left) return;
    
    ASTNode *expr = node->data.op.left;
    ArgumentList args = {expr, NULL};
    execute_func_call("yapping", &args);
}

void interpreter_visit_error_statement(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node || !node->data.op.left) return;
    
    ASTNode *expr = node->data.op.left;
    ArgumentList args = {expr, NULL};
    execute_func_call("baka", &args);
}
