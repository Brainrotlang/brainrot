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

/* Unified scope management for semantic analysis */
typedef struct SemanticScope {
    HashMap *variables;           /* Variables in this scope */
    HashMap *functions;          /* Functions in this scope */
    struct SemanticScope *parent; /* Parent scope */
    bool is_function_scope;      /* Is this a function scope */
    int depth;                   /* Scope nesting depth */
} SemanticScope;

/* Symbol table for pre-collected declarations */
typedef struct SymbolEntry {
    char *name;
    VarType type;
    bool is_const;
    bool is_function;
    VarType return_type;        /* For functions */
    int line_number;
    int scope_depth;            /* Track which scope level this was declared in */
    struct SymbolEntry *next;
} SymbolEntry;

/* Semantic analyzer visitor */
typedef struct {
    Visitor base;               /* Inherit from Visitor */
    SemanticScope *current_scope; /* Current semantic scope */
    SymbolEntry *symbol_table;  /* Pre-collected symbols */
    SemanticError *errors;      /* List of semantic errors */
    bool has_errors;            /* Flag indicating if errors were found */
    int error_count;            /* Number of errors found */
    bool is_collecting_phase;   /* Flag for collection vs analysis phase */
    int scope_depth;            /* Current scope depth */
} SemanticAnalyzer;

/* Create and destroy semantic analyzer */
SemanticAnalyzer* semantic_analyzer_new(void);
void semantic_analyzer_free(SemanticAnalyzer *analyzer);

/* Main analysis function */
bool semantic_analyze(ASTNode *root);

/* Symbol table management */
void add_symbol(SemanticAnalyzer *analyzer, const char *name, VarType type, bool is_const, bool is_function, VarType return_type, int line_number);
SymbolEntry* find_symbol(SemanticAnalyzer *analyzer, const char *name);
void free_symbol_table(SymbolEntry *symbols);

/* Semantic scope management */
SemanticScope* create_semantic_scope(SemanticScope *parent, bool is_function_scope);
void free_semantic_scope(SemanticScope *scope);
void enter_semantic_scope(SemanticAnalyzer *analyzer, bool is_function_scope);
void exit_semantic_scope(SemanticAnalyzer *analyzer);

/* Variable management in semantic scopes */
bool add_semantic_variable(SemanticAnalyzer *analyzer, const char *name, VarType type, bool is_const);
bool find_semantic_variable(SemanticAnalyzer *analyzer, const char *name, SymbolEntry **result);

/* Two-phase analysis */
void collect_declarations(SemanticAnalyzer *analyzer, ASTNode *root);
void semantic_analyze_with_scope_tracking(SemanticAnalyzer *analyzer, ASTNode *node);
bool analyze_with_scopes(SemanticAnalyzer *analyzer, ASTNode *root);
void semantic_analyze_node(SemanticAnalyzer *analyzer, ASTNode *node);

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
