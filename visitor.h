/* visitor.h - Visitor pattern interface for AST traversal */

#ifndef VISITOR_H
#define VISITOR_H

#include "ast.h"

/* Forward declarations */
typedef struct Visitor Visitor;

/* Visitor interface - base class for all visitors */
struct Visitor {
    /* Expression visitors - return void* for flexibility */
    void* (*visit_int_literal)(Visitor *self, ASTNode *node);
    void* (*visit_float_literal)(Visitor *self, ASTNode *node);
    void* (*visit_double_literal)(Visitor *self, ASTNode *node);
    void* (*visit_char_literal)(Visitor *self, ASTNode *node);
    void* (*visit_short_literal)(Visitor *self, ASTNode *node);
    void* (*visit_boolean_literal)(Visitor *self, ASTNode *node);
    void* (*visit_string_literal)(Visitor *self, ASTNode *node);
    void* (*visit_identifier)(Visitor *self, ASTNode *node);
    void* (*visit_binary_operation)(Visitor *self, ASTNode *node);
    void* (*visit_unary_operation)(Visitor *self, ASTNode *node);
    void* (*visit_array_access)(Visitor *self, ASTNode *node);
    void* (*visit_function_call)(Visitor *self, ASTNode *node);
    void* (*visit_sizeof)(Visitor *self, ASTNode *node);
    
    /* Statement visitors - return void */
    void (*visit_declaration)(Visitor *self, ASTNode *node);
    void (*visit_assignment)(Visitor *self, ASTNode *node);
    void (*visit_if_statement)(Visitor *self, ASTNode *node);
    void (*visit_for_statement)(Visitor *self, ASTNode *node);
    void (*visit_while_statement)(Visitor *self, ASTNode *node);
    void (*visit_do_while_statement)(Visitor *self, ASTNode *node);
    void (*visit_switch_statement)(Visitor *self, ASTNode *node);
    void (*visit_break_statement)(Visitor *self, ASTNode *node);
    void (*visit_return_statement)(Visitor *self, ASTNode *node);
    void (*visit_function_definition)(Visitor *self, ASTNode *node);
    void (*visit_statement_list)(Visitor *self, ASTNode *node);
    void (*visit_print_statement)(Visitor *self, ASTNode *node);
    void (*visit_error_statement)(Visitor *self, ASTNode *node);
};

/* Generic AST traversal function */
void ast_accept(ASTNode *node, Visitor *visitor);

/* Helper function to traverse child nodes */
void visit_children(ASTNode *node, Visitor *visitor);

#endif /* VISITOR_H */
