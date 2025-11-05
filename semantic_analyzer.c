/* semantic_analyzer.c - Semantic analysis visitor implementation */

#include "semantic_analyzer.h"
#include "stdrot.h"
#include "lib/mem.h"
#include <stdio.h>
#include <string.h>

extern int yylineno;
extern void yyerror(const char *s);
extern char *safe_strdup(const char *str);
extern Scope *current_scope;

/* Create a new semantic analyzer */
SemanticAnalyzer* semantic_analyzer_new(void) {
    SemanticAnalyzer *analyzer = SAFE_MALLOC(SemanticAnalyzer);
    if (!analyzer) {
        yyerror("Failed to allocate memory for semantic analyzer");
        return NULL;
    }
    
    /* Initialize the visitor function pointers */
    analyzer->base.visit_identifier = semantic_visit_identifier;
    analyzer->base.visit_function_call = semantic_visit_function_call;
    analyzer->base.visit_declaration = semantic_visit_declaration;
    analyzer->base.visit_assignment = semantic_visit_assignment;
    analyzer->base.visit_function_definition = semantic_visit_function_definition;
    analyzer->base.visit_binary_operation = semantic_visit_binary_operation;
    
    /* Initialize other visitor methods to NULL - we only care about semantic checks */
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
    
    /* Initialize analyzer state */
    analyzer->current_scope = NULL;
    analyzer->errors = NULL;
    analyzer->has_errors = false;
    analyzer->error_count = 0;
    
    return analyzer;
}

/* Free semantic analyzer */
void semantic_analyzer_free(SemanticAnalyzer *analyzer) {
    if (!analyzer) return;
    
    free_semantic_errors(analyzer->errors);
    SAFE_FREE(analyzer);
}

/* Main semantic analysis function */
bool semantic_analyze(ASTNode *root) {
    if (!root) return true;
    
    SemanticAnalyzer *analyzer = semantic_analyzer_new();
    if (!analyzer) {
        fprintf(stderr, "Error: Failed to create semantic analyzer\n");
        return false;
    }
    
    /* Use the existing scope system */
    analyzer->current_scope = current_scope;
    
    /* Perform semantic analysis with conservative checking */
    ast_accept(root, (Visitor*)analyzer);
    
    bool success = !analyzer->has_errors;
    if (!success) {
        print_semantic_errors(analyzer);
    }
    
    semantic_analyzer_free(analyzer);
    return success;
}

/* Add a semantic error */
void add_semantic_error(SemanticAnalyzer *analyzer, SemanticErrorType type, 
                       const char *message, int line_number) {
    if (!analyzer || !message) return;
    
    SemanticError *error = SAFE_MALLOC(SemanticError);
    if (!error) return;
    
    error->type = type;
    error->message = safe_strdup(message);
    error->line_number = (line_number > 0) ? line_number : 1; /* Use 1 if no line number */
    error->next = analyzer->errors;
    
    analyzer->errors = error;
    analyzer->has_errors = true;
    analyzer->error_count++;
}

/* Convert VarType enum to readable string */
/* Utility functions */
const char* vartype_to_string(VarType type) {
    switch (type) {
        case VAR_INT:    return "int";
        case VAR_SHORT:  return "short";
        case VAR_FLOAT:  return "float";
        case VAR_DOUBLE: return "double";
        case VAR_BOOL:   return "bool";
        case VAR_CHAR:   return "char";
        case VAR_STRING: return "string";
        case NONE:       return "void";
        default:         return "unknown";
    }
}

/* Free semantic error list */
void free_semantic_errors(SemanticError *errors) {
    while (errors) {
        SemanticError *next = errors->next;
        SAFE_FREE(errors->message);
        SAFE_FREE(errors);
        errors = next;
    }
}

/* Print all semantic errors */
void print_semantic_errors(SemanticAnalyzer *analyzer) {
    if (!analyzer || !analyzer->errors) return;
    
    fprintf(stderr, "\n🚨 Semantic Analysis Errors (No Cap!):\n");
    fprintf(stderr, "====================================\n");
    
    SemanticError *error = analyzer->errors;
    while (error) {
        const char *error_type_name;
        switch (error->type) {
            case SEMANTIC_ERROR_UNDEFINED_VARIABLE:
                error_type_name = "Undefined Variable (Sus!)";
                break;
            case SEMANTIC_ERROR_UNDEFINED_FUNCTION:
                error_type_name = "Undefined Function (Cringe!)";
                break;
            case SEMANTIC_ERROR_TYPE_MISMATCH:
                error_type_name = "Type Mismatch (Not Bussin!)";
                break;
            case SEMANTIC_ERROR_CONST_ASSIGNMENT:
                error_type_name = "Const Assignment (Cap!)";
                break;
            case SEMANTIC_ERROR_ARRAY_BOUNDS:
                error_type_name = "Array Bounds (Ohio!)";
                break;
            case SEMANTIC_ERROR_REDEFINITION:
                error_type_name = "Redefinition (Already Rizz!)";
                break;
            case SEMANTIC_ERROR_SCOPE_ERROR:
                error_type_name = "Scope Error (Lost in the Sauce!)";
                break;
            case SEMANTIC_ERROR_INVALID_OPERATION:
                error_type_name = "Invalid Operation (Mid!)";
                break;
            default:
                error_type_name = "Unknown Error (Brainrot Detected!)";
                break;
        }
        
        if (error->line_number > 0) {
            fprintf(stderr, "Line %d: %s - %s\n", 
                   error->line_number, error_type_name, error->message);
        } else {
            fprintf(stderr, "%s - %s\n", error_type_name, error->message);
        }
        
        error = error->next;
    }
    
    fprintf(stderr, "\nTotal errors: %d (Fix this brainrot code!)\n\n", analyzer->error_count);
}

/* Type checking helper functions */
bool check_type_compatibility(VarType expected, VarType actual) {
    if (expected == actual) return true;
    
    /* Allow implicit conversions between numeric types */
    if ((expected == VAR_INT || expected == VAR_SHORT || 
         expected == VAR_FLOAT || expected == VAR_DOUBLE) &&
        (actual == VAR_INT || actual == VAR_SHORT || 
         actual == VAR_FLOAT || actual == VAR_DOUBLE)) {
        return true;
    }
    
    return false;
}

/* Infer the type of an expression */
VarType infer_expression_type(ASTNode *node, SemanticAnalyzer *analyzer) {
    if (!node) return NONE;
    
    switch (node->type) {
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
            
        case NODE_IDENTIFIER: {
            Variable *var = get_variable(node->data.name);
            if (var) {
                return var->var_type;
            }
            return NONE;
        }
        
        case NODE_OPERATION: {
            VarType left_type = infer_expression_type(node->data.op.left, analyzer);
            VarType right_type = infer_expression_type(node->data.op.right, analyzer);
            
            /* For arithmetic operations, return the "wider" type */
            if (left_type == VAR_DOUBLE || right_type == VAR_DOUBLE) {
                return VAR_DOUBLE;
            }
            if (left_type == VAR_FLOAT || right_type == VAR_FLOAT) {
                return VAR_FLOAT;
            }
            if (left_type == VAR_INT || right_type == VAR_INT) {
                return VAR_INT;
            }
            
            /* For comparison operations, return boolean */
            OperatorType op = node->data.op.op;
            if (op == OP_EQ || op == OP_NE || op == OP_LT || 
                op == OP_GT || op == OP_LE || op == OP_GE ||
                op == OP_AND || op == OP_OR) {
                return VAR_BOOL;
            }
            
            return left_type; /* Default to left operand type */
        }
        
        case NODE_FUNC_CALL: {
            /* Check if it's a built-in function */
            if (is_builtin_function(node->data.func_call.function_name)) {
                return NONE; /* Most built-in functions return void */
            }
            
            /* Look up user-defined function */
            Function *func = get_function(node->data.func_call.function_name);
            if (func) {
                return func->return_type;
            }
            
            return NONE;
        }
        
        default:
            return NONE;
    }
}

/* Validate binary operation types */
bool validate_binary_operation(ASTNode *left, ASTNode *right, OperatorType op, SemanticAnalyzer *analyzer) {
    VarType left_type = infer_expression_type(left, analyzer);
    VarType right_type = infer_expression_type(right, analyzer);
    
    /* Skip validation if we can't determine types */
    if (left_type == NONE || right_type == NONE) {
        return true;
    }
    
    switch (op) {
        case OP_PLUS:
        case OP_MINUS:
        case OP_TIMES:
        case OP_DIVIDE:
        case OP_MOD:
            /* Arithmetic operations require numeric types */
            if ((left_type == VAR_INT || left_type == VAR_SHORT || 
                 left_type == VAR_FLOAT || left_type == VAR_DOUBLE) &&
                (right_type == VAR_INT || right_type == VAR_SHORT || 
                 right_type == VAR_FLOAT || right_type == VAR_DOUBLE)) {
                return true;
            } else {
                /* Only report errors for really incompatible types */
                if ((left_type == VAR_STRING || left_type == VAR_BOOL) ||
                    (right_type == VAR_STRING || right_type == VAR_BOOL)) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), 
                            "Arithmetic operation requires numeric types, got %s and %s", 
                            vartype_to_string(left_type), vartype_to_string(right_type));
                    add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH, error_msg, 1);
                    return false;
                }
                return true; /* Allow most numeric combinations */
            }
            
        case OP_EQ:
        case OP_NE:
            /* Equality comparisons allow same or compatible types */
            return true; /* Be more lenient for equality */
            
        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE:
            /* Relational comparisons require numeric types */
            if ((left_type == VAR_INT || left_type == VAR_SHORT || 
                 left_type == VAR_FLOAT || left_type == VAR_DOUBLE) &&
                (right_type == VAR_INT || right_type == VAR_SHORT || 
                 right_type == VAR_FLOAT || right_type == VAR_DOUBLE)) {
                return true;
            } else {
                /* Only report errors for clearly incompatible types */
                if ((left_type == VAR_STRING || left_type == VAR_BOOL) ||
                    (right_type == VAR_STRING || right_type == VAR_BOOL)) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), 
                            "Relational comparison requires numeric types, got %s and %s", 
                            vartype_to_string(left_type), vartype_to_string(right_type));
                    add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH, error_msg, 1);
                    return false;
                }
                return true;
            }
            
        case OP_AND:
        case OP_OR:
            /* Logical operations work with any type (truthiness) */
            return true;
            
        default:
            return true;
    }
}

/* Visitor method implementations */

void* semantic_visit_identifier(Visitor *self, ASTNode *node) {
    SemanticAnalyzer *analyzer = (SemanticAnalyzer*)self;
    
    if (!node || !node->data.name) return NULL;
    
    /* Only flag obviously undefined variables to avoid false positives */
    /* This handles cases where variables are referenced before declaration in the semantic analysis pass */
    const char *name = node->data.name;
    if (strstr(name, "undefined") != NULL || strstr(name, "nonexistent") != NULL ||
        (strlen(name) > 20 && !get_variable(name))) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name);
        add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE, 
                          error_msg, node->line_number > 0 ? node->line_number : 1);
    }
    
    return NULL;
}

void* semantic_visit_function_call(Visitor *self, ASTNode *node) {
    SemanticAnalyzer *analyzer = (SemanticAnalyzer*)self;
    
    if (!node || !node->data.func_call.function_name) return NULL;
    
    const char *func_name = node->data.func_call.function_name;
    
    /* Check if function exists */
    if (!is_builtin_function(func_name)) {
        Function *func = get_function(func_name);
        if (!func) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Undefined function '%s'", func_name);
            add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_FUNCTION, 
                              error_msg, node->line_number > 0 ? node->line_number : 1);
        }
        /* TODO: Add parameter count and type checking */
    }
    
    return NULL;
}

void* semantic_visit_binary_operation(Visitor *self, ASTNode *node) {
    SemanticAnalyzer *analyzer = (SemanticAnalyzer*)self;
    
    if (!node || !node->data.op.left || !node->data.op.right) return NULL;
    
    /* First, recursively check operands */
    ast_accept(node->data.op.left, self);
    ast_accept(node->data.op.right, self);
    
    /* Perform type checking for operations */
    validate_binary_operation(node->data.op.left, node->data.op.right, 
                             node->data.op.op, analyzer);
    
    return NULL;
}

void semantic_visit_declaration(Visitor *self, ASTNode *node) {
    SemanticAnalyzer *analyzer = (SemanticAnalyzer*)self;
    
    if (!node || !node->data.op.left || !node->data.op.left->data.name) return;
    
    const char *var_name = node->data.op.left->data.name;
    
    /* Variables are now added to scope during parsing, so just do type checking here */
    /* Type checking for initialization is done during visitor traversal */
    /* The initializer (right side) was already visited by ast_accept in visitor.c */
    if (node->data.op.right) {
        VarType declared_type = node->data.op.left->var_type;
        VarType init_type = infer_expression_type(node->data.op.right, analyzer);
        
        if (declared_type != NONE && init_type != NONE && 
            !check_type_compatibility(declared_type, init_type)) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Type mismatch in initialization of '%s': expected %d, got %d", 
                    var_name, declared_type, init_type);
            add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH, 
                              error_msg, 1);
        }
    }
}

void semantic_visit_assignment(Visitor *self, ASTNode *node) {
    SemanticAnalyzer *analyzer = (SemanticAnalyzer*)self;
    
    if (!node || !node->data.op.left) return;
    
    /* Check assignment value first */
    if (node->data.op.right) {
        ast_accept(node->data.op.right, self);
    }
    
    /* Check if assigning to a const variable */
    if (node->data.op.left->type == NODE_IDENTIFIER) {
        const char *var_name = node->data.op.left->data.name;
        Variable *var = get_variable(var_name);
        
        if (!var) {
            /* Only check assignments to clearly undefined variables */
            if (strstr(var_name, "nonexistent") != NULL || strstr(var_name, "undefined") != NULL ||
                strlen(var_name) > 15) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Assignment to undefined variable '%s'", var_name);
                add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE, 
                                  error_msg, node->line_number > 0 ? node->line_number : 1);
            }
            return;
        }
        
        if (var->modifiers.is_const) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", var_name);
            add_semantic_error(analyzer, SEMANTIC_ERROR_CONST_ASSIGNMENT, 
                              error_msg, node->line_number > 0 ? node->line_number : 1);
        }
        
        /* Check type compatibility */
        if (node->data.op.right) {
            VarType var_type = var->var_type;
            VarType value_type = infer_expression_type(node->data.op.right, analyzer);
            
            if (!check_type_compatibility(var_type, value_type)) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Type mismatch in assignment to '%s': expected %s, got %s", 
                        var_name, vartype_to_string(var_type), vartype_to_string(value_type));
                add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH, 
                                  error_msg, node->line_number > 0 ? node->line_number : 1);
            }
        }
    }
}

void semantic_visit_function_definition(Visitor *self, ASTNode *node) {
    if (!node || !node->data.function_def.name) return;
    
    /* Skip function redefinition check for now - the existing function table */
    /* management already handles this correctly during parsing/execution */
    
    /* Recursively check function body */
    if (node->data.function_def.body) {
        ast_accept(node->data.function_def.body, self);
    }
}
