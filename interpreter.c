/* interpreter.c - Interpreter visitor implementation */

#include "interpreter.h"
#include "ast.h"
#include "stdrot.h"
#include "lib/mem.h"
#include <stdio.h>

extern void yyerror(const char *s);
extern char *safe_strdup(const char *str);
extern void yapping(const char* format, ...);
extern void baka(const char* format, ...);

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
    
    /* For the interpreter visitor, we don't need to return allocated memory
     * The unary operations are handled by the existing evaluation system for side effects */
    evaluate_expression_int(node);
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
    Variable *var = variable_new(name);
    
    /* TEMPORARY FIX: Set the variable type from the AST node, but fix invalid types */
    if (node->data.op.left->var_type >= VAR_INT && node->data.op.left->var_type < NONE) {
        var->var_type = node->data.op.left->var_type;
    } else {
        /* Fallback: Try to guess the type based on initialization value */
        if (node->data.op.right) {
            switch (node->data.op.right->type) {
                case NODE_DOUBLE:
                    var->var_type = VAR_DOUBLE;
                    break;
                case NODE_FLOAT:
                    var->var_type = VAR_FLOAT;
                    break;
                case NODE_STRING_LITERAL:
                    var->var_type = VAR_STRING;
                    break;
                case NODE_BOOLEAN:
                    var->var_type = VAR_BOOL;
                    break;
                case NODE_CHAR:
                    var->var_type = VAR_CHAR;
                    break;
                default:
                    var->var_type = VAR_INT;  /* Default to int for most cases */
                    break;
            }
        } else {
            var->var_type = VAR_INT;  /* Default to int if no initializer */
        }
    }
    
    add_variable_to_scope(name, var);
    
    /* Handle initialization */
    if (node->data.op.right) {
        /* Get the variable from scope (it should be there now) */
        Variable *scope_var = get_variable(name);
        if (scope_var) {
            /* Evaluate and set the initial value based on variable type */
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
                    scope_var->value.ivalue = int_value;  /* char stored as int */
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
    
    SAFE_FREE(var);
}

void interpreter_visit_assignment(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node || !node->data.op.left || !node->data.op.right) return;
    
    /* Handle simple identifier assignments directly */
    if (node->data.op.left->type == NODE_IDENTIFIER) {
        const char *name = node->data.op.left->data.name;
        
        /* Check for const assignment */
        extern void check_const_assignment(const char *name);
        check_const_assignment(name);
        
        Variable *var = get_variable(name);
        
        if (!var) {
            yyerror("Assignment to undefined variable");
            return;
        }
        
        /* Evaluate the right-hand side expression based on the target variable type */
        switch (var->var_type) {
            case VAR_INT: {
                /* Check if the expression is actually a float/double that needs bit-level conversion */
                extern VarType get_expression_type(ASTNode *node);
                VarType expr_type = get_expression_type(node->data.op.right);
                
                if (expr_type == VAR_FLOAT) {
                    /* Do bit-level conversion (reinterpret float bits as int) */
                    float float_value = evaluate_expression_float(node->data.op.right);
                    union { float f; int i; } u;
                    u.f = float_value;
                    var->value.ivalue = u.i;
                } else if (expr_type == VAR_DOUBLE) {
                    /* Do bit-level conversion (reinterpret double bits as int) */
                    double double_value = evaluate_expression_double(node->data.op.right);
                    union { double d; int i; } u;
                    u.d = double_value;
                    var->value.ivalue = u.i;
                } else {
                    int int_value = evaluate_expression_int(node->data.op.right);
                    var->value.ivalue = int_value;
                }
                break;
            }
            case VAR_FLOAT: {
                float float_value = evaluate_expression_float(node->data.op.right);
                var->value.fvalue = float_value;
                break;
            }
            case VAR_DOUBLE: {
                double double_value = evaluate_expression_double(node->data.op.right);
                var->value.dvalue = double_value;
                break;
            }
            case VAR_CHAR: {
                int int_value = evaluate_expression_int(node->data.op.right);
                var->value.ivalue = int_value;  /* char stored as int */
                break;
            }
            case VAR_SHORT: {
                short short_value = evaluate_expression_short(node->data.op.right);
                var->value.svalue = short_value;
                break;
            }
            case VAR_BOOL: {
                bool bool_value = evaluate_expression_bool(node->data.op.right);
                var->value.bvalue = bool_value;
                break;
            }
            case VAR_STRING: {
                char *string_value = evaluate_expression_string(node->data.op.right);
                var->value.strvalue = string_value;
                break;
            }
            default:
                break;
        }
    } else if (node->data.op.left->type == NODE_ARRAY_ACCESS) {
        /* Handle array element assignments - use evaluate_multi_array_access for multi-dimensional arrays */
        ASTNode *array_node = node->data.op.left;
        Variable *var = get_variable(array_node->data.array.name);
        
        if (!var) {
            yyerror("Assignment to undefined array");
            return;
        }
        
        if (!var->is_array) {
            yyerror("Variable is not an array");
            return;
        }
        
        /* Use evaluate_multi_array_access to get pointer to element */
        void *element = evaluate_multi_array_access(array_node);
        if (!element) {
            yyerror("Invalid array access");
            return;
        }
        
        /* Assign to array element based on the array type */
        switch (var->var_type) {
            case VAR_INT: {
                int value = evaluate_expression_int(node->data.op.right);
                *(int*)element = value;
                break;
            }
            case VAR_SHORT: {
                short value = evaluate_expression_short(node->data.op.right);
                *(short*)element = value;
                break;
            }
            case VAR_FLOAT: {
                float value = evaluate_expression_float(node->data.op.right);
                *(float*)element = value;
                break;
            }
            case VAR_DOUBLE: {
                double value = evaluate_expression_double(node->data.op.right);
                *(double*)element = value;
                break;
            }
            case VAR_BOOL: {
                bool value = evaluate_expression_bool(node->data.op.right);
                *(bool*)element = value;
                break;
            }
            case VAR_CHAR: {
                int value = evaluate_expression_int(node->data.op.right);
                *(char*)element = (char)value;
                break;
            }
            default:
                yyerror("Unsupported array type for assignment");
                break;
        }
    } else {
        /* Handle other complex assignments using old system */
        execute_assignment(node);
    }
}

void interpreter_visit_if_statement(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node) return;
    
    /* Use existing expression evaluation */
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
    
    /* Use setjmp/longjmp for break handling like the old code */
    PUSH_JUMP_BUFFER();
    if (setjmp(CURRENT_JUMP_BUFFER()) == 0) {
        /* Enter scope for the entire for loop (including initialization) */
        enter_scope();
        
        /* Execute initialization */
        if (node->data.for_stmt.init) {
            ast_accept(node->data.for_stmt.init, self);
        }
        
        /* Loop while condition is true */
        while (1) {
            /* Evaluate condition */
            enter_scope();
            if (node->data.for_stmt.cond) {
                int cond_result = evaluate_expression_int(node->data.for_stmt.cond);
                if (!cond_result) {
                    exit_scope();
                    break;
                }
            }
            
            /* Execute the body using the visitor */
            if (node->data.for_stmt.body) {
                ast_accept(node->data.for_stmt.body, self);
            }
            
            /* Execute increment */
            if (node->data.for_stmt.incr) {
                ast_accept(node->data.for_stmt.incr, self);
            }
            exit_scope();
        }
        
        /* Exit scope for the entire for loop */
        exit_scope();
    }
    POP_JUMP_BUFFER();
}

void interpreter_visit_while_statement(Visitor *self, ASTNode *node) {
    if (!node) return;
    
    extern void enter_scope();
    extern void exit_scope();
    
    /* Use setjmp/longjmp for break handling like the old code */
    PUSH_JUMP_BUFFER();
    enter_scope();
    while (evaluate_expression_int(node->data.while_stmt.cond) && setjmp(CURRENT_JUMP_BUFFER()) == 0) {
        /* Enter new scope for each iteration */
        enter_scope();
        
        /* Execute the body using the visitor */
        if (node->data.while_stmt.body) {
            ast_accept(node->data.while_stmt.body, self);
        }
        
        /* Exit scope before next iteration */
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
    if (expr->type == NODE_STRING_LITERAL) {
        yapping("%s", expr->data.strvalue);
    } else {
        int value = evaluate_expression_int(expr);
        yapping("%d", value);
    }
}

void interpreter_visit_error_statement(Visitor *self, ASTNode *node) {
    (void)self;
    if (!node || !node->data.op.left) return;
    
    ASTNode *expr = node->data.op.left;
    if (expr->type == NODE_STRING_LITERAL) {
        baka("%s", expr->data.strvalue);
    } else {
        int value = evaluate_expression_int(expr);
        baka("%d", value);
    }
}
