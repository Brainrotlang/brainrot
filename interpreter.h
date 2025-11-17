/* interpreter.h - Interpreter visitor */

#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "visitor.h"
#include "ast.h"

/* Interpreter visitor - executes the AST */
typedef struct {
    Visitor base;                   /* Inherit from Visitor */
    Scope *current_scope;           /* Current execution scope */
    ReturnValue return_value;       /* Function return value */
    bool should_break;              /* Break flag for loops/switch */
    bool should_return;             /* Return flag for functions */
} Interpreter;
/* Create and destroy interpreter */
Interpreter* interpreter_new(void);
void interpreter_free(Interpreter *interpreter);

/* Main interpretation function */
void interpret(ASTNode *root, Interpreter *interp);
/* Visitor method implementations for expressions (return values) */
void* interpreter_visit_int_literal(Visitor *self, ASTNode *node);
void* interpreter_visit_float_literal(Visitor *self, ASTNode *node);
void* interpreter_visit_double_literal(Visitor *self, ASTNode *node);
void* interpreter_visit_char_literal(Visitor *self, ASTNode *node);
void* interpreter_visit_short_literal(Visitor *self, ASTNode *node);
void* interpreter_visit_boolean_literal(Visitor *self, ASTNode *node);
void* interpreter_visit_string_literal(Visitor *self, ASTNode *node);
void* interpreter_visit_identifier(Visitor *self, ASTNode *node);
void* interpreter_visit_binary_operation(Visitor *self, ASTNode *node);
void* interpreter_visit_unary_operation(Visitor *self, ASTNode *node);
void* interpreter_visit_array_access(Visitor *self, ASTNode *node);
void* interpreter_visit_function_call(Visitor *self, ASTNode *node);
void* interpreter_visit_sizeof(Visitor *self, ASTNode *node);

/* Visitor method implementations for statements */
void interpreter_visit_declaration(Visitor *self, ASTNode *node);
void interpreter_visit_assignment(Visitor *self, ASTNode *node);
void interpreter_visit_if_statement(Visitor *self, ASTNode *node);
void interpreter_visit_for_statement(Visitor *self, ASTNode *node);
void interpreter_visit_while_statement(Visitor *self, ASTNode *node);
void interpreter_visit_do_while_statement(Visitor *self, ASTNode *node);
void interpreter_visit_switch_statement(Visitor *self, ASTNode *node);
void interpreter_visit_break_statement(Visitor *self, ASTNode *node);
void interpreter_visit_return_statement(Visitor *self, ASTNode *node);
void interpreter_visit_function_definition(Visitor *self, ASTNode *node);
void interpreter_visit_statement_list(Visitor *self, ASTNode *node);
void interpreter_visit_print_statement(Visitor *self, ASTNode *node);
void interpreter_visit_error_statement(Visitor *self, ASTNode *node);

#endif /* INTERPRETER_H */
