/* semantic_analyzer.h - Semantic analysis visitor */

#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H

#include "visitor.h"
#include "ast.h"

/* Error types for semantic analysis */
typedef enum {
    SEMANTIC_ERROR_UNDEFINED_VARIABLE,
    SEMANTIC_ERROR_UNDEFINED_FUNCTION,
    SEMANTIC_ERROR_TYPE_MISMATCH,
    SEMANTIC_ERROR_CONST_ASSIGNMENT,
    SEMANTIC_ERROR_ARRAY_BOUNDS,
    SEMANTIC_ERROR_REDEFINITION,
    SEMANTIC_ERROR_SCOPE_ERROR,
    SEMANTIC_ERROR_INVALID_OPERATION
} SemanticErrorType;

/* Semantic error structure */
typedef struct SemanticError {
    SemanticErrorType type;
    char *message;
    int line_number;
    struct SemanticError *next;
} SemanticError;

/* Semantic analyzer visitor */
typedef struct {
    Visitor base;               /* Inherit from Visitor */
    Scope *current_scope;       /* Current scope for symbol resolution */
    SemanticError *errors;      /* List of semantic errors */
    bool has_errors;            /* Flag indicating if errors were found */
    int error_count;            /* Number of errors found */
} SemanticAnalyzer;

/* Create and destroy semantic analyzer */
SemanticAnalyzer* semantic_analyzer_new(void);
void semantic_analyzer_free(SemanticAnalyzer *analyzer);

/* Main analysis function */
bool semantic_analyze(ASTNode *root);

/* Error reporting functions */
void add_semantic_error(SemanticAnalyzer *analyzer, SemanticErrorType type, 
                       const char *message, int line_number);
void print_semantic_errors(SemanticAnalyzer *analyzer);
void free_semantic_errors(SemanticError *errors);

/* Type checking functions */
bool check_type_compatibility(VarType expected, VarType actual);
VarType infer_expression_type(ASTNode *node, SemanticAnalyzer *analyzer);
bool validate_binary_operation(ASTNode *left, ASTNode *right, OperatorType op, SemanticAnalyzer *analyzer);

/* Utility functions */
const char* vartype_to_string(VarType type);

/* Visitor method implementations */
void* semantic_visit_identifier(Visitor *self, ASTNode *node);
void* semantic_visit_function_call(Visitor *self, ASTNode *node);
void semantic_visit_declaration(Visitor *self, ASTNode *node);
void semantic_visit_assignment(Visitor *self, ASTNode *node);
void semantic_visit_function_definition(Visitor *self, ASTNode *node);
void* semantic_visit_binary_operation(Visitor *self, ASTNode *node);

#endif /* SEMANTIC_ANALYZER_H */
