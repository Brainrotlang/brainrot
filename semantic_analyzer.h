/* semantic_analyzer.h - Semantic analysis visitor */

#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H

#include "visitor.h"
#include "ast.h"
#include "lib/string_value.h"

/* Error types for semantic analysis */
typedef enum
{
    SEMANTIC_ERROR_UNDEFINED_VARIABLE,
    SEMANTIC_ERROR_UNDEFINED_FUNCTION,
    SEMANTIC_ERROR_TYPE_MISMATCH,
    SEMANTIC_ERROR_CONST_ASSIGNMENT,
    SEMANTIC_ERROR_ARRAY_BOUNDS,
    SEMANTIC_ERROR_REDEFINITION,
    SEMANTIC_ERROR_SCOPE_ERROR,
    SEMANTIC_ERROR_INVALID_OPERATION,
    SEMANTIC_ERROR_ARITY_MISMATCH
} SemanticErrorType;

/* Semantic error structure */
typedef struct SemanticError
{
    SemanticErrorType type;
    String message;
    int line_number;
    struct SemanticError *next;
} SemanticError;

/* Unified scope management for semantic analysis */
typedef struct SemanticScope
{
    HashMap *variables;           /* Variables in this scope */
    HashMap *functions;           /* Functions in this scope */
    struct SemanticScope *parent; /* Parent scope */
    bool is_function_scope;       /* Is this a function scope */
    int depth;                    /* Scope nesting depth */
} SemanticScope;

/* Symbol table for pre-collected declarations */
typedef struct SymbolEntry
{
    String name;
    VarType type;
    int pointer_level;
    bool is_const;
    bool is_function;
    /* Set for a `T name[N]` declaration (NODE_DECLARATION's own
       ASTNode.is_array, see ast.h) -- distinct from pointer_level: this
       language doesn't decay arrays to pointers, so an array identifier
       reports pointer_level 0, the same as a scalar. type still holds
       the ELEMENT type (VAR_INT for `rizz arr[N]`), so callers that need
       to tell "array of T" from "a single T" apart (e.g. STDROT_ANY
       argument checking, where Variable's value union aliases a
       scalar's own value with an array's backing pointer) must check
       this flag too, not just type. Not set for function/parameter
       symbols -- those don't track array-ness in Parameter (ast.h) at
       all yet. */
    bool is_array;
    VarType return_type; /* For functions */
    int return_pointer_level;
    int line_number;
    int scope_depth;    /* Track which scope level this was declared in */
    String struct_name; /* struct/union tag; set when type == VAR_STRUCT */
    /* Name of the function this symbol was declared inside, or {0} for a
       top-level (skibidi main) symbol. scope_depth alone can't tell two
       functions' locals apart -- it resets to 0 for every function, so a
       flat "scope_depth <= current" comparison would happily match a
       same-named local belonging to a *different*, unrelated function.
       See find_symbol(). */
    String function_name;
    struct SymbolEntry *next;
} SymbolEntry;

/* Semantic analyzer visitor */
typedef struct
{
    Visitor base;                 /* Inherit from Visitor */
    SemanticScope *current_scope; /* Current semantic scope */
    SymbolEntry *symbol_table;    /* Pre-collected symbols */
    SemanticError *errors;        /* List of semantic errors */
    bool has_errors;              /* Flag indicating if errors were found */
    int error_count;              /* Number of errors found */
    bool is_collecting_phase;     /* Flag for collection vs analysis phase */
    int scope_depth;              /* Current scope depth */
    /* Name of the function currently being walked, or {0} at top level;
       tagged onto every SymbolEntry added while set (see add_symbol) and
       used by find_symbol to keep one function's locals from leaking into
       another's lookup. Maintained by both collect_declarations and
       semantic_analyze_with_scope_tracking around NODE_FUNCTION_DEF. */
    String current_function_name;
} SemanticAnalyzer;

/* Create and destroy semantic analyzer */
SemanticAnalyzer *semantic_analyzer_new(void);
void semantic_analyzer_free(SemanticAnalyzer *analyzer);

/* Main analysis function */
bool semantic_analyze(ASTNode *root);

/* Symbol table management */
void add_symbol(SemanticAnalyzer *analyzer, const String name, VarType type,
                int pointer_level, bool is_const, bool is_function,
                VarType return_type, int return_pointer_level, int line_number,
                const String struct_name, bool is_array);
SymbolEntry *find_symbol(SemanticAnalyzer *analyzer, const String name);
void free_symbol_table(SymbolEntry *symbols);

/* Semantic scope management */
SemanticScope *create_semantic_scope(SemanticScope *parent,
                                     bool is_function_scope);
void free_semantic_scope(SemanticScope *scope);
void enter_semantic_scope(SemanticAnalyzer *analyzer, bool is_function_scope);
void exit_semantic_scope(SemanticAnalyzer *analyzer);

/* Variable management in semantic scopes */
bool add_semantic_variable(SemanticAnalyzer *analyzer, const String name,
                           VarType type, int pointer_level, bool is_const);
bool find_semantic_variable(SemanticAnalyzer *analyzer, const String name,
                            SymbolEntry **result);

/* Two-phase analysis */
void collect_declarations(SemanticAnalyzer *analyzer, ASTNode *root);
void semantic_analyze_with_scope_tracking(SemanticAnalyzer *analyzer,
                                          ASTNode *node);
bool analyze_with_scopes(SemanticAnalyzer *analyzer, ASTNode *root);
void semantic_analyze_node(SemanticAnalyzer *analyzer, ASTNode *node);

/* Error reporting functions */
void add_semantic_error(SemanticAnalyzer *analyzer, SemanticErrorType type,
                        String message, int line_number);
void print_semantic_errors(SemanticAnalyzer *analyzer);
void free_semantic_errors(SemanticError *errors);

/* Type checking functions */
bool check_type_compatibility(VarType expected, VarType actual);
bool check_type_compatibility_ex(VarType expected, int expected_pointer_level,
                                 VarType actual, int actual_pointer_level);
VarType infer_expression_type(ASTNode *node, SemanticAnalyzer *analyzer);
int infer_expression_pointer_level(ASTNode *node, SemanticAnalyzer *analyzer);
bool validate_binary_operation(ASTNode *left, ASTNode *right, OperatorType op,
                               SemanticAnalyzer *analyzer);

/* Utility functions */
const char *vartype_to_string(VarType type);

/* Visitor method implementations */
void *semantic_visit_identifier(Visitor *self, ASTNode *node);
void *semantic_visit_function_call(Visitor *self, ASTNode *node);
void semantic_visit_declaration(Visitor *self, ASTNode *node);
void semantic_visit_assignment(Visitor *self, ASTNode *node);
void semantic_visit_function_definition(Visitor *self, ASTNode *node);
void *semantic_visit_binary_operation(Visitor *self, ASTNode *node);

#endif /* SEMANTIC_ANALYZER_H */
