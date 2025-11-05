/* interpreter.c - Interpreter visitor implementation */

#include "interpreter.h"
#include "stdrot.h"
#include "lib/mem.h"
#include <stdio.h>

/* External declarations we need */
extern void yyerror(const char *s);
extern Variable *variable_new(char *name);
extern void add_variable_to_scope(const char *name, Variable *var);
extern void enter_scope(void);
extern void exit_scope(void);
extern void execute_assignment(ASTNode *node);
extern Function *create_function(char *name, VarType return_type, Parameter *params, ASTNode *body);
extern void bruh(void);
extern char *safe_strdup(const char *str);

/* External functions from original ast.c that we'll reuse */
extern int evaluate_expression_int(ASTNode *node);
extern float evaluate_expression_float(ASTNode *node);
extern double evaluate_expression_double(ASTNode *node);
extern short evaluate_expression_short(ASTNode *node);
extern bool evaluate_expression_bool(ASTNode *node);
extern char *evaluate_expression_string(ASTNode *node);
extern void *handle_binary_operation(ASTNode *node);
extern void *handle_unary_expression(ASTNode *node, void *operand, VarType type);
extern void *evaluate_multi_array_access(ASTNode *node);
extern void *handle_function_call(ASTNode *node);
extern size_t handle_sizeof(ASTNode *node);
extern void *handle_identifier(ASTNode *node, const char *error_msg, int promotion);

/* External execution functions that we'll gradually replace */
extern void execute_for_statement(ASTNode *node);
extern void execute_while_statement(ASTNode *node);
extern void execute_do_while_statement(ASTNode *node);
extern void execute_switch_statement(ASTNode *node);
extern void execute_function_call(const char *name, ArgumentList *args);
extern void handle_return_statement(ASTNode *expr);

extern void yapping(const char* format, ...);
extern void baka(const char* format, ...);
extern int evaluate_expression(ASTNode *node);

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
    interp->current_scope = NULL;
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
    
    /* Set the current scope */
    interp->current_scope = current_scope;
    
    /* Execute the AST using visitor pattern */
    ast_accept(root, (Visitor*)interp);
}

/* Expression visitor implementations - these wrap existing evaluation functions */

void* interpreter_visit_int_literal(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    int *result = SAFE_MALLOC(int);
    *result = node->data.ivalue;
    return result;
}

void* interpreter_visit_float_literal(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    float *result = SAFE_MALLOC(float);
    *result = node->data.fvalue;
    return result;
}

void* interpreter_visit_double_literal(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    double *result = SAFE_MALLOC(double);
    *result = node->data.dvalue;
    return result;
}

void* interpreter_visit_char_literal(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    char *result = SAFE_MALLOC(char);
    *result = (char)node->data.ivalue;
    return result;
}

void* interpreter_visit_short_literal(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    short *result = SAFE_MALLOC(short);
    *result = node->data.svalue;
    return result;
}

void* interpreter_visit_boolean_literal(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    bool *result = SAFE_MALLOC(bool);
    *result = node->data.bvalue;
    return result;
}

void* interpreter_visit_string_literal(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    return safe_strdup(node->data.strvalue);
}

void* interpreter_visit_identifier(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    /* Reuse existing handle_identifier function */
    return handle_identifier(node, "Undefined variable", 0);
}

void* interpreter_visit_binary_operation(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    /* Reuse existing handle_binary_operation function */
    return handle_binary_operation(node);
}

void* interpreter_visit_unary_operation(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    /* This is more complex - for now, delegate to existing evaluation */
    int result = evaluate_expression_int(node);
    int *ret = SAFE_MALLOC(int);
    *ret = result;
    return ret;
}

void* interpreter_visit_array_access(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    /* Reuse existing evaluate_multi_array_access function */
    return evaluate_multi_array_access(node);
}

void* interpreter_visit_function_call(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    
    const char *func_name = node->data.func_call.function_name;
    ArgumentList *args = node->data.func_call.arguments;
    
    /* Check if it's a built-in function */
    if (is_builtin_function(func_name)) {
        execute_builtin_function(func_name, args);
        return NULL;
    }
    
    /* Otherwise, use existing handle_function_call for user-defined functions */
    return handle_function_call(node);
}

void* interpreter_visit_sizeof(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return NULL;
    size_t size = handle_sizeof(node);
    size_t *result = SAFE_MALLOC(size_t);
    *result = size;
    return result;
}

/* Statement visitor implementations - these wrap existing execution functions */

void interpreter_visit_declaration(Visitor *self, ASTNode *node) {
    /* For now, delegate to the existing execute_statement function */
    /* We'll gradually replace this with proper visitor implementation */
    if (!node) return;
    
    char *name = node->data.op.left->data.name;
    Variable *var = variable_new(name);
    add_variable_to_scope(name, var);
    
    /* Handle the assignment part if present */
    if (node->data.op.right) {
        /* This is a declaration with initialization - treat like assignment */
        node->type = NODE_ASSIGNMENT; /* Temporarily change type */
        interpreter_visit_assignment(self, node);
        node->type = NODE_DECLARATION; /* Restore type */
    }
    
    SAFE_FREE(var);
}

void interpreter_visit_assignment(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    /* Delegate to existing execute_assignment function for now */
    execute_assignment(node);
}

void interpreter_visit_if_statement(Visitor *self, ASTNode *node) {
    if (!node) return;
    
    Interpreter *interp = (Interpreter*)self;
    
    /* Enter new scope */
    enter_scope();
    
    /* Evaluate condition */
    if (evaluate_expression(node->data.if_stmt.condition)) {
        if (node->data.if_stmt.then_branch) {
            ast_accept(node->data.if_stmt.then_branch, (Visitor*)interp);
        }
    } else if (node->data.if_stmt.else_branch) {
        ast_accept(node->data.if_stmt.else_branch, (Visitor*)interp);
    }
    
    /* Exit scope */
    exit_scope();
}

void interpreter_visit_for_statement(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    /* Delegate to existing function for now */
    execute_for_statement(node);
}

void interpreter_visit_while_statement(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    /* Delegate to existing function for now */
    execute_while_statement(node);
}

void interpreter_visit_do_while_statement(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    /* Delegate to existing function for now */
    execute_do_while_statement(node);
}

void interpreter_visit_switch_statement(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    /* Delegate to existing function for now */
    execute_switch_statement(node);
}

void interpreter_visit_break_statement(Visitor *self, ASTNode *node) {
    (void)node; /* Suppress unused parameter warning */
    Interpreter *interp = (Interpreter*)self;
    interp->should_break = true;
    /* Also call existing bruh() function for compatibility */
    bruh();
}

void interpreter_visit_return_statement(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    /* Delegate to existing function for now */
    handle_return_statement(node->data.op.left);
}

void interpreter_visit_function_definition(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node) return;
    
    /* Create the function and add to function table */
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
    (void)self; /* Suppress unused parameter warning */
    (void)node; /* Suppress unused parameter warning */
    /* The visitor pattern already handles traversing statement lists in visitor.c */
    /* So we don't need to do anything special here */
}

void interpreter_visit_print_statement(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node || !node->data.op.left) return;
    
    ASTNode *expr = node->data.op.left;
    if (expr->type == NODE_STRING_LITERAL) {
        yapping("%s\n", expr->data.name);
    } else {
        int value = evaluate_expression(expr);
        yapping("%d\n", value);
    }
}

void interpreter_visit_error_statement(Visitor *self, ASTNode *node) {
    (void)self; /* Suppress unused parameter warning */
    if (!node || !node->data.op.left) return;
    
    ASTNode *expr = node->data.op.left;
    if (expr->type == NODE_STRING_LITERAL) {
        baka("%s\n", expr->data.name);
    } else {
        int value = evaluate_expression(expr);
        baka("%d\n", value);
    }
}
