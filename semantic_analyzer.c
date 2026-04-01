/* semantic_analyzer.c - Semantic analysis visitor implementation */

#include "semantic_analyzer.h"
#include "stdrot.h"
#include "lib/mem.h"
#include <stdio.h>
#include <string.h>

extern int yylineno;
extern void yyerror(const  char *s);
extern String safe_strdup(const String *str);
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
    
    /* Initialize other visitor methods to NULL */
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
    
    analyzer->current_scope = NULL;
    analyzer->symbol_table = NULL;
    analyzer->errors = NULL;
    analyzer->has_errors = false;
    analyzer->error_count = 0;
    analyzer->is_collecting_phase = false;
    analyzer->scope_depth = 0;
    
    return analyzer;
}

/* Free semantic analyzer */
void semantic_analyzer_free(SemanticAnalyzer *analyzer) {
    if (!analyzer) return;
    
    free_semantic_errors(analyzer->errors);
    free_symbol_table(analyzer->symbol_table);
    
    while (analyzer->current_scope) {
        exit_semantic_scope(analyzer);
    }
    
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
    
    /* Phase 1: Collect all declarations */
    analyzer->is_collecting_phase = true;
    analyzer->scope_depth = 0;
    collect_declarations(analyzer, root);
    
    /* Phase 2: Perform semantic analysis */
    analyzer->is_collecting_phase = false;
    analyzer->scope_depth = 0;
    
    semantic_analyze_with_scope_tracking(analyzer, root);
    
    bool success = !analyzer->has_errors;
    
    if (!success) {
        print_semantic_errors(analyzer);
    }
    
    semantic_analyzer_free(analyzer);
    return success;
}

/* Add a semantic error */
void add_semantic_error(SemanticAnalyzer *analyzer, SemanticErrorType type, 
                       char *message, int line_number) {
    if (!analyzer || !message) return;
    
    SemanticError *error = SAFE_MALLOC(SemanticError);
    if (!error) return;
    
    error->type = type;
    String s = {
        .data = message,
        .len = MAX_BUFFER_LEN
    };
    error->message = safe_strdup(&s);
    error->line_number = (line_number > 0) ? line_number : 1;
    error->next = analyzer->errors;
    
    analyzer->errors = error;
    analyzer->has_errors = true;
    analyzer->error_count++;
}

/* Convert VarType enum to readable string */
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
    
    SemanticError *error = analyzer->errors;
    while (error) {
        /* Use simple, direct error messages */
        switch (error->type) {
            case SEMANTIC_ERROR_UNDEFINED_VARIABLE:
                if (error->line_number > 0) {
                    fprintf(stderr, "Error: Undefined variable at line %d\n", error->line_number);
                } else {
                    fprintf(stderr, "Error: Undefined variable\n");
                }
                break;
            case SEMANTIC_ERROR_UNDEFINED_FUNCTION:
                if (error->line_number > 0) {
                    fprintf(stderr, "Error: Undefined function at line %d\n", error->line_number);
                } else {
                    fprintf(stderr, "Error: Undefined function\n");
                }
                break;
            case SEMANTIC_ERROR_CONST_ASSIGNMENT:
                if (error->line_number > 0) {
                    fprintf(stderr, "Error: Cannot modify const variable at line %d\n", error->line_number);
                } else {
                    fprintf(stderr, "Error: Cannot modify const variable\n");
                }
                break;
            case SEMANTIC_ERROR_REDEFINITION:
                if (error->line_number > 0) {
                    fprintf(stderr, "Error: Function redefinition at line %d\n", error->line_number);
                } else {
                    fprintf(stderr, "Error: Function redefinition\n");
                }
                break;
            case SEMANTIC_ERROR_SCOPE_ERROR:
                if (error->line_number > 0) {
                    fprintf(stderr, "Error: Variable out of scope at line %d\n", error->line_number);
                } else {
                    fprintf(stderr, "Error: Variable out of scope\n");
                }
                break;
            default:
                if (error->line_number > 0) {
                    fprintf(stderr, "Error: %s at line %d\n", error->message.data, error->line_number);
                } else {
                    fprintf(stderr, "Error: %s\n", error->message.data);
                }
                break;
        }
        
        error = error->next;
    }
}

/* Type checking helper functions */
bool check_type_compatibility(VarType expected, VarType actual) {
    return check_type_compatibility_ex(expected, 0, actual, 0);
}

bool check_type_compatibility_ex(VarType expected, int expected_pointer_level, VarType actual, int actual_pointer_level) {
    if (expected_pointer_level != actual_pointer_level) return false;
    if (expected_pointer_level > 0) {
        return expected == actual;
    }
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

int infer_expression_pointer_level(ASTNode *node, SemanticAnalyzer *analyzer) {
    if (!node) return 0;

    switch (node->type) {
        case NODE_IDENTIFIER: {
            SymbolEntry *symbol = find_symbol(analyzer, node->data.name);
            if (symbol) return symbol->pointer_level;
            Variable *var = get_variable(node->data.name);
            return var ? var->pointer_level : node->pointer_level;
        }
        case NODE_ARRAY_ACCESS: {
            Variable *var = get_variable(node->data.array.name);
            return var ? var->pointer_level : node->pointer_level;
        }
        case NODE_UNARY_OPERATION:
            if (node->data.unary.op == OP_ADDRESS_OF) {
                return infer_expression_pointer_level(node->data.unary.operand, analyzer) + 1;
            }
            if (node->data.unary.op == OP_DEREFERENCE) {
                int operand_level = infer_expression_pointer_level(node->data.unary.operand, analyzer);
                return operand_level > 0 ? operand_level - 1 : 0;
            }
            return infer_expression_pointer_level(node->data.unary.operand, analyzer);
        case NODE_FUNC_CALL: {
            SymbolEntry *symbol = find_symbol(analyzer, node->data.func_call.function_name);
            if (symbol && symbol->is_function) return symbol->return_pointer_level;
            Function *func = get_function(node->data.func_call.function_name);
            return func ? func->return_pointer_level : 0;
        }
        case NODE_OPERATION:
            switch (node->data.op.op) {
                case OP_PLUS:
                case OP_MINUS: {
                    int left_level = infer_expression_pointer_level(node->data.op.left, analyzer);
                    int right_level = infer_expression_pointer_level(node->data.op.right, analyzer);
                    if (left_level > 0 && right_level == 0) return left_level;
                    if (right_level > 0 && left_level == 0 && node->data.op.op == OP_PLUS) return right_level;
                    return 0;
                }
                default:
                    return 0;
            }
        default:
            return node->pointer_level;
    }
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
            /* First try semantic analyzer symbol table */
            SymbolEntry *symbol = find_symbol(analyzer, node->data.name);
            if (symbol) {
                return symbol->type;
            }
            
            /* Fallback to runtime variable lookup */
            Variable *var = get_variable(node->data.name);
            if (var) {
                return var->var_type;
            }
            return NONE;
        }
        
        case NODE_OPERATION: {
            VarType left_type = infer_expression_type(node->data.op.left, analyzer);
            VarType right_type = infer_expression_type(node->data.op.right, analyzer);
            int left_pointer_level = infer_expression_pointer_level(node->data.op.left, analyzer);
            int right_pointer_level = infer_expression_pointer_level(node->data.op.right, analyzer);

            if (node->data.op.op == OP_EQ || node->data.op.op == OP_NE || node->data.op.op == OP_LT ||
                node->data.op.op == OP_GT || node->data.op.op == OP_LE || node->data.op.op == OP_GE ||
                node->data.op.op == OP_AND || node->data.op.op == OP_OR) {
                return VAR_BOOL;
            }

            if (left_pointer_level > 0 && right_pointer_level == 0) {
                return left_type;
            }
            if (right_pointer_level > 0 && left_pointer_level == 0 && node->data.op.op == OP_PLUS) {
                return right_type;
            }
            
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
            
            return left_type; /* Default to left operand type */
        }

        case NODE_UNARY_OPERATION:
            return infer_expression_type(node->data.unary.operand, analyzer);
        
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

        case NODE_STRUCT_ACCESS: {
            ASTNode *obj = node->data.struct_access.object;
            Variable *var = (obj->type == NODE_IDENTIFIER)
                            ? get_variable(obj->data.name) : NULL;
            if (!var || var->var_type != VAR_STRUCT) return NONE;
            StructDef *def = get_struct_def(var->struct_name);
            if (!def) return NONE;
            StructField *fld = find_struct_field(def,
                                   node->data.struct_access.member_name);
            return fld ? fld->type : NONE;
        }
        
        default:
            return NONE;
    }
}

/* Validate binary operation types */
bool validate_binary_operation(ASTNode *left, ASTNode *right, OperatorType op, SemanticAnalyzer *analyzer) {
    VarType left_type = infer_expression_type(left, analyzer);
    VarType right_type = infer_expression_type(right, analyzer);
    int left_pointer_level = infer_expression_pointer_level(left, analyzer);
    int right_pointer_level = infer_expression_pointer_level(right, analyzer);
    
    /* Skip validation if we can't determine types */
    if (left_type == NONE || right_type == NONE) {
        return true;
    }
    
    switch (op) {
        case OP_PLUS:
        case OP_MINUS:
            if (left_pointer_level > 0 || right_pointer_level > 0) {
                if (left_pointer_level > 0 && right_pointer_level == 0 &&
                    (right_type == VAR_INT || right_type == VAR_SHORT || right_type == VAR_CHAR || right_type == VAR_BOOL)) {
                    return true;
                }
                if (op == OP_PLUS && right_pointer_level > 0 && left_pointer_level == 0 &&
                    (left_type == VAR_INT || left_type == VAR_SHORT || left_type == VAR_CHAR || left_type == VAR_BOOL)) {
                    return true;
                }
                if (op == OP_MINUS && left_pointer_level > 0 && right_pointer_level > 0 &&
                    left_pointer_level == right_pointer_level && left_type == right_type) {
                    return true;
                }
                add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                                  "Invalid pointer arithmetic", 1);
                return false;
            }
            /* fallthrough */
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
                    char error_msg[MAX_BUFFER_LEN];
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
            if (left_pointer_level > 0 || right_pointer_level > 0) {
                return left_pointer_level == right_pointer_level && left_type == right_type;
            }
            /* Equality comparisons allow same or compatible types */
            return true; /* Be more lenient for equality */
            
        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE:
            if (left_pointer_level > 0 || right_pointer_level > 0) {
                return left_pointer_level == right_pointer_level && left_type == right_type;
            }
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
                    char error_msg[MAX_BUFFER_LEN];
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
    
    if (!node || !node->data.name.data) return NULL;
    
    if (analyzer->is_collecting_phase) return NULL;
    
    const String name = node->data.name;
    SymbolEntry *symbol = find_symbol(analyzer, name);
    
    if (!symbol) {
        SymbolEntry *entry = analyzer->symbol_table;
        bool found_in_deeper_scope = false;
        
        while (entry) {
            if (strcmp(entry->name.data, name.data) == 0) {
                if (entry->scope_depth > analyzer->scope_depth) {
                    found_in_deeper_scope = true;
                    break;
                }
            }
            entry = entry->next;
        }
        
        if (found_in_deeper_scope) {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg), "Variable '%s' is out of scope", name.data);
            add_semantic_error(analyzer, SEMANTIC_ERROR_SCOPE_ERROR, 
                              error_msg, node->line_number > 0 ? node->line_number : 1);
        } else {
            Variable *var = get_variable(name);
            if (!var) {
                if (!is_builtin_function(name)) {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name.data);
                    add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE, 
                                      error_msg, node->line_number > 0 ? node->line_number : 1);
                }
            }
        }
    }
    
    return NULL;
}

void* semantic_visit_function_call(Visitor *self, ASTNode *node) {
    SemanticAnalyzer *analyzer = (SemanticAnalyzer*)self;
    
    if (!node || !node->data.func_call.function_name.data) return NULL;
    
    const String func_name = node->data.func_call.function_name;
    
    if (!is_builtin_function(func_name)) {
        Function *func = get_function(func_name);
        if (!func) {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg), "Undefined function '%s'", func_name.data);
            add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_FUNCTION, 
                              error_msg, node->line_number > 0 ? node->line_number : 1);
        }
    }
    
    return NULL;
}

void* semantic_visit_binary_operation(Visitor *self, ASTNode *node) {
    SemanticAnalyzer *analyzer = (SemanticAnalyzer*)self;
    
    if (!node || !node->data.op.left || !node->data.op.right) return NULL;
    
    ast_accept(node->data.op.left, self);
    ast_accept(node->data.op.right, self);
    
    validate_binary_operation(node->data.op.left, node->data.op.right, 
                             node->data.op.op, analyzer);
    
    return NULL;
}

void semantic_visit_declaration(Visitor *self, ASTNode *node) {
    SemanticAnalyzer *analyzer = (SemanticAnalyzer*)self;
    
    if (!node || !node->data.op.left || !node->data.op.left->data.name.data) return;
    
    const String var_name = node->data.op.left->data.name;
    
    if (node->data.op.right) {
        VarType declared_type = node->var_type;
        int declared_pointer_level = node->pointer_level;
        VarType init_type = infer_expression_type(node->data.op.right, analyzer);
        int init_pointer_level = infer_expression_pointer_level(node->data.op.right, analyzer);
        
        if (declared_type != NONE && init_type != NONE && 
            !check_type_compatibility_ex(declared_type, declared_pointer_level, init_type, init_pointer_level)) {
            char error_msg[MAX_BUFFER_LEN];
            snprintf(error_msg, sizeof(error_msg), 
                    "Type mismatch in initialization of '%s': expected %s, got %s", 
                    var_name.data, vartype_to_string(declared_type), vartype_to_string(init_type));
            add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH, 
                              error_msg, 1);
        }
    }
}

void semantic_visit_assignment(Visitor *self, ASTNode *node) {
    SemanticAnalyzer *analyzer = (SemanticAnalyzer*)self;
    
    if (!node || !node->data.op.left) return;
    
    if (node->data.op.right) {
        ast_accept(node->data.op.right, self);
    }

    if (node->data.op.left->type == NODE_UNARY_OPERATION && node->data.op.left->data.unary.op == OP_DEREFERENCE) {
        ASTNode *operand = node->data.op.left->data.unary.operand;
        if (infer_expression_pointer_level(operand, analyzer) <= 0) {
            add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                              "Cannot dereference a non-pointer expression",
                              node->line_number > 0 ? node->line_number : 1);
            return;
        }
    }
    
    if (node->data.op.left->type == NODE_IDENTIFIER) {
        const String var_name = node->data.op.left->data.name;
        
        if (analyzer->is_collecting_phase) return;
        
        SymbolEntry *symbol = find_symbol(analyzer, var_name);
        
        if (!symbol) {
            Variable *var = get_variable(var_name);
            if (!var) {
                if (!is_builtin_function(var_name)) {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg), "Assignment to undefined variable '%s'", var_name.data);
                    add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE, 
                                      error_msg, node->line_number > 0 ? node->line_number : 1);
                }
                return;
            }
            
            if (var->modifiers.is_const) {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", var_name.data);
                add_semantic_error(analyzer, SEMANTIC_ERROR_CONST_ASSIGNMENT, 
                                  error_msg, node->line_number > 0 ? node->line_number : 1);
            }
        } else {
            if (symbol->is_const) {
                char error_msg[MAX_BUFFER_LEN];
                snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", var_name.data);
                add_semantic_error(analyzer, SEMANTIC_ERROR_CONST_ASSIGNMENT, 
                                  error_msg, node->line_number > 0 ? node->line_number : 1);
            }
        }
    }

    if (node->data.op.left->type != NODE_IDENTIFIER && 
        node->data.op.left->type != NODE_ARRAY_ACCESS &&
        node->data.op.left->type != NODE_STRUCT_ACCESS &&
        !(node->data.op.left->type == NODE_UNARY_OPERATION && 
          node->data.op.left->data.unary.op == OP_DEREFERENCE)) {
        add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                          "Left-hand side of assignment is not assignable",
                          node->line_number > 0 ? node->line_number : 1);
        return;
    }

    VarType target_type = infer_expression_type(node->data.op.left, analyzer);
    int target_pointer_level = infer_expression_pointer_level(node->data.op.left, analyzer);
    VarType value_type = infer_expression_type(node->data.op.right, analyzer);
    int value_pointer_level = infer_expression_pointer_level(node->data.op.right, analyzer);

    if ((target_pointer_level > 0 || value_pointer_level > 0) &&
        !check_type_compatibility_ex(target_type, target_pointer_level, value_type, value_pointer_level)) {
        add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                          "Assignment type mismatch",
                          node->line_number > 0 ? node->line_number : 1);
    }
}

void semantic_visit_function_definition(Visitor *self, ASTNode *node) {
    if (!node || !node->data.function_def.name.data) return;
    
    if (node->data.function_def.body) {
        ast_accept(node->data.function_def.body, self);
    }
}

/* Symbol table management functions */
void add_symbol(SemanticAnalyzer *analyzer, const String name, VarType type, int pointer_level, bool is_const, bool is_function, VarType return_type, int return_pointer_level, int line_number) {
    if (!analyzer || !name.data) return;
    
    SymbolEntry *entry = SAFE_MALLOC(SymbolEntry);
    if (!entry) return;
    
    entry->name = safe_strdup(&name);
    entry->type = type;
    entry->pointer_level = pointer_level;
    entry->is_const = is_const;
    entry->is_function = is_function;
    entry->return_type = return_type;
    entry->return_pointer_level = return_pointer_level;
    entry->line_number = line_number;
    entry->scope_depth = analyzer->scope_depth;
    entry->next = analyzer->symbol_table;
    
    analyzer->symbol_table = entry;
}

SymbolEntry* find_symbol(SemanticAnalyzer *analyzer, const String name) {
    if (!analyzer || !name.data) return NULL;
    
    SymbolEntry *entry = analyzer->symbol_table;
    
    while (entry) {
        if (strcmp(entry->name.data, name.data) == 0) {
            /* Check if this symbol is accessible from current scope */
            if (entry->scope_depth <= analyzer->scope_depth) {
                return entry; /* Symbol is accessible */
            }
        }
        entry = entry->next;
    }
    
    return NULL; /* Symbol not found or not accessible */
}

void free_symbol_table(SymbolEntry *symbols) {
    while (symbols) {
        SymbolEntry *next = symbols->next;
        SAFE_FREE(symbols->name);
        SAFE_FREE(symbols);
        symbols = next;
    }
}

/* Phase 1: Collect all declarations */
void collect_declarations(SemanticAnalyzer *analyzer, ASTNode *node) {
    if (!node || !analyzer) return;
    
    static int depth = 0;
    if (depth > 1000) {
        fprintf(stderr, "Warning: Maximum recursion depth reached in collect_declarations\n");
        return;
    }
    depth++;
    
    switch (node->type) {
        case NODE_DECLARATION:
            if (node->data.op.left && node->data.op.left->data.name.data) {
                const String var_name = node->data.op.left->data.name;
                VarType var_type = node->var_type;
                bool is_const = node->modifiers.is_const;
                
                add_symbol(analyzer, var_name, var_type, node->pointer_level, is_const, false, NONE, 0,
                          node->line_number > 0 ? node->line_number : 1);
            }
            if (node->data.op.right) {
                collect_declarations(analyzer, node->data.op.right);
            }
            break;
            
        case NODE_FUNCTION_DEF:
            if (node->data.function_def.name.data) {
                SymbolEntry *existing = find_symbol(analyzer, node->data.function_def.name);
                if (existing && existing->is_function) {
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg), "Function '%s' is already defined", node->data.function_def.name.data);
                    add_semantic_error(analyzer, SEMANTIC_ERROR_REDEFINITION, 
                                      error_msg, node->line_number > 0 ? node->line_number : 1);
                } else {
                    add_symbol(analyzer, node->data.function_def.name, NONE, 0, false, true, 
                              node->data.function_def.return_type, node->pointer_level,
                              node->line_number > 0 ? node->line_number : 1);
                }
            }
            
            analyzer->scope_depth++;
            
            Parameter *param = node->data.function_def.parameters;
            while (param) {
                if (param->name.data) {
                    add_symbol(analyzer, param->name, param->type, param->pointer_level, false, false, NONE, 0,
                              node->line_number > 0 ? node->line_number : 1);
                }
                param = param->next;
            }
            
            if (node->data.function_def.body) {
                collect_declarations(analyzer, node->data.function_def.body);
            }
            
            analyzer->scope_depth--;
            break;
            
        case NODE_STATEMENT_LIST:
            if (node->data.statements) {
                StatementList *current = node->data.statements;
                while (current) {
                    if (current->statement) {
                        collect_declarations(analyzer, current->statement);
                    }
                    current = current->next;
                }
            }
            break;
            
        case NODE_IF_STATEMENT:
            if (node->data.if_stmt.then_branch) {
                collect_declarations(analyzer, node->data.if_stmt.then_branch);
            }
            if (node->data.if_stmt.else_branch) {
                collect_declarations(analyzer, node->data.if_stmt.else_branch);
            }
            break;
            
        case NODE_FOR_STATEMENT:
            analyzer->scope_depth++;
            if (node->data.for_stmt.init && node->data.for_stmt.init->type == NODE_DECLARATION) {
                collect_declarations(analyzer, node->data.for_stmt.init);
            }
            if (node->data.for_stmt.body) {
                collect_declarations(analyzer, node->data.for_stmt.body);
            }
            analyzer->scope_depth--;
            break;
            
        case NODE_WHILE_STATEMENT:
            analyzer->scope_depth++;
            if (node->data.while_stmt.body) {
                collect_declarations(analyzer, node->data.while_stmt.body);
            }
            analyzer->scope_depth--;
            break;
            
        case NODE_DO_WHILE_STATEMENT:
            analyzer->scope_depth++;
            if (node->data.while_stmt.body) {
                collect_declarations(analyzer, node->data.while_stmt.body);
            }
            analyzer->scope_depth--;
            break;
            
        case NODE_OPERATION:
            if (node->data.op.op == OP_ASSIGN) {
                break;
            }
            break;
            
        default:
            break;
    }
    
    depth--;
}

/* Scope-aware semantic analysis that tracks scope depth during traversal */
void semantic_analyze_with_scope_tracking(SemanticAnalyzer *analyzer, ASTNode *node) {
    if (!node || !analyzer) return;
    
    /* Prevent infinite recursion */
    static int recursion_depth = 0;
    if (recursion_depth > 100) {
        fprintf(stderr, "Warning: Maximum recursion depth reached in scope tracking\n");
        return;
    }
    recursion_depth++;
    
    switch (node->type) {
        case NODE_STATEMENT_LIST: {
            /* Process each statement in the list */
            if (node->data.statements) {
                StatementList *current = node->data.statements;
                while (current) {
                    if (current->statement) {
                        semantic_analyze_with_scope_tracking(analyzer, current->statement);
                    }
                    current = current->next;
                }
            }
            break;
        }
        
        case NODE_FUNCTION_DEF: {
            /* Enter function scope */
            analyzer->scope_depth++;
            
            /* Process function body */
            if (node->data.function_def.body) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.function_def.body);
            }
            
            /* Exit function scope */
            analyzer->scope_depth--;
            break;
        }
        
        case NODE_FOR_STATEMENT: {
            /* Enter loop scope */
            analyzer->scope_depth++;
            
            /* Process all parts of the for loop */
            if (node->data.for_stmt.init) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.for_stmt.init);
            }
            if (node->data.for_stmt.cond) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.for_stmt.cond);
            }
            if (node->data.for_stmt.incr) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.for_stmt.incr);
            }
            if (node->data.for_stmt.body) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.for_stmt.body);
            }
            
            /* Exit loop scope */
            analyzer->scope_depth--;
            break;
        }
        
        case NODE_WHILE_STATEMENT: {
            /* Process condition at current scope */
            if (node->data.while_stmt.cond) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.while_stmt.cond);
            }
            
            /* Enter loop scope for body */
            analyzer->scope_depth++;
            if (node->data.while_stmt.body) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.while_stmt.body);
            }
            analyzer->scope_depth--;
            break;
        }
        
        case NODE_IF_STATEMENT: {
            /* Process condition at current scope */
            if (node->data.if_stmt.condition) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.if_stmt.condition);
            }
            
            /* Process then branch in new scope */
            analyzer->scope_depth++;
            if (node->data.if_stmt.then_branch) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.if_stmt.then_branch);
            }
            analyzer->scope_depth--;
            
            /* Process else branch in new scope */
            if (node->data.if_stmt.else_branch) {
                analyzer->scope_depth++;
                semantic_analyze_with_scope_tracking(analyzer, node->data.if_stmt.else_branch);
                analyzer->scope_depth--;
            }
            break;
        }
        
        case NODE_IDENTIFIER: {
            /* Use the visitor method for identifier checking */
            semantic_visit_identifier((Visitor*)analyzer, node);
            break;
        }
        
        case NODE_ASSIGNMENT: {
            /* Use the visitor method for assignment checking */
            semantic_visit_assignment((Visitor*)analyzer, node);
            break;
        }
        
        case NODE_FUNC_CALL: {
            /* Use the visitor method for function call checking */
            semantic_visit_function_call((Visitor*)analyzer, node);
            
            /* Also process function arguments */
            if (node->data.func_call.arguments) {
                ArgumentList *args = node->data.func_call.arguments;
                while (args) {
                    if (args->expr) {
                        semantic_analyze_with_scope_tracking(analyzer, args->expr);
                    }
                    args = args->next;
                }
            }
            break;
        }
        
        case NODE_OPERATION: {
            /* Process both operands */
            if (node->data.op.left) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.op.left);
            }
            if (node->data.op.right) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.op.right);
            }
            
            /* Use visitor method for operation checking */
            semantic_visit_binary_operation((Visitor*)analyzer, node);
            break;
        }

        case NODE_UNARY_OPERATION: {
            if (node->data.unary.operand) {
                semantic_analyze_with_scope_tracking(analyzer, node->data.unary.operand);
            }
            if (node->data.unary.op == OP_ADDRESS_OF) {
                ASTNode *operand = node->data.unary.operand;
                bool valid_lvalue = operand && (operand->type == NODE_IDENTIFIER || operand->type == NODE_ARRAY_ACCESS ||
                    (operand->type == NODE_UNARY_OPERATION && operand->data.unary.op == OP_DEREFERENCE));
                if (!valid_lvalue) {
                    add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                                      "Address-of requires an assignable expression",
                                      node->line_number > 0 ? node->line_number : 1);
                }
            } else if (node->data.unary.op == OP_DEREFERENCE) {
                if (infer_expression_pointer_level(node->data.unary.operand, analyzer) <= 0) {
                    add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                                      "Cannot dereference a non-pointer expression",
                                      node->line_number > 0 ? node->line_number : 1);
                }
            }
            break;
        }
        
        case NODE_DECLARATION: {
            /* Use visitor method for declaration checking */
            semantic_visit_declaration((Visitor*)analyzer, node);
            break;
        }

        case NODE_STRUCT_ACCESS: {
            /* Check the object is a known struct variable with that field */
            ASTNode *obj = node->data.struct_access.object;
            if (obj) semantic_analyze_with_scope_tracking(analyzer, obj);
            if (obj && obj->type == NODE_IDENTIFIER) {
                Variable *var = get_variable(obj->data.name);
                if (!var) break;
                if (var->var_type != VAR_STRUCT) {
                    add_semantic_error(analyzer, SEMANTIC_ERROR_TYPE_MISMATCH,
                        "Member access on non-struct variable",
                        node->line_number > 0 ? node->line_number : 1);
                    break;
                }
                StructDef *def = get_struct_def(var->struct_name);
                if (def && !find_struct_field(def,
                                node->data.struct_access.member_name)) {
                    char msg[MAX_BUFFER_LEN];
                    snprintf(msg, sizeof(msg),
                             "Struct '%s' has no member '%s'",
                             var->struct_name.data,
                             node->data.struct_access.member_name.data);
                    add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE,
                                       msg, node->line_number > 0 ? node->line_number : 1);
                }
            }
            break;
        }
        
        default:
            /* For other node types, only process simple operands to avoid crashes */
            break;
    }
    
    recursion_depth--;
}

/* Single-phase analysis with integrated scope management */
bool analyze_with_scopes(SemanticAnalyzer *analyzer, ASTNode *root) {
    if (!analyzer || !root) return true;
    
    /* Process the AST and manage scopes as we go */
    semantic_analyze_node(analyzer, root);
    
    return !analyzer->has_errors;
}

/* Process individual AST nodes with scope management */
void semantic_analyze_node(SemanticAnalyzer *analyzer, ASTNode *node) {
    if (!node || !analyzer) return;
    
    switch (node->type) {
        case NODE_STATEMENT_LIST: {
            /* Process each statement in the list */
            StatementList *current = node->data.statements;
            while (current) {
                if (current->statement) {
                    semantic_analyze_node(analyzer, current->statement);
                }
                current = current->next;
            }
            break;
        }
        
        case NODE_DECLARATION: {
            /* Add variable to current scope when we encounter declaration */
            if (node->data.op.left && node->data.op.left->data.name.data) {
                const String var_name = node->data.op.left->data.name;
                VarType var_type = node->var_type;
                bool is_const = node->modifiers.is_const;
                
                add_semantic_variable(analyzer, var_name, var_type, node->pointer_level, is_const);
                
                /* Also analyze the initialization expression */
                if (node->data.op.right) {
                    semantic_analyze_node(analyzer, node->data.op.right);
                }
            }
            break;
        }
        
        case NODE_IDENTIFIER: {
            /* Check if identifier is defined when we encounter it */
            const String name = node->data.name;
            SymbolEntry *symbol = NULL;
            
            if (!find_semantic_variable(analyzer, name, &symbol)) {
                /* Check if it's a built-in function or keyword */
                if (!is_builtin_function(name)){
                    char error_msg[MAX_BUFFER_LEN];
                    snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name.data);
                    add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE, 
                                      error_msg, node->line_number > 0 ? node->line_number : 1);
                }
            }
            break;
        }
        
        case NODE_ASSIGNMENT: {
            /* Check assignment target and value */
            if (node->data.op.left) {
                if (node->data.op.left->type == NODE_IDENTIFIER) {
                    const String var_name = node->data.op.left->data.name;
                    SymbolEntry *symbol = NULL;
                    
                    if (!find_semantic_variable(analyzer, var_name, &symbol)) {
                        char error_msg[MAX_BUFFER_LEN];
                        snprintf(error_msg, sizeof(error_msg), "Assignment to undefined variable '%s'", var_name.data);
                        add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE, 
                                          error_msg, node->line_number > 0 ? node->line_number : 1);
                    } else if (symbol && symbol->is_const) {
                        char error_msg[MAX_BUFFER_LEN];
                        snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", var_name.data);
                        add_semantic_error(analyzer, SEMANTIC_ERROR_CONST_ASSIGNMENT, 
                                          error_msg, node->line_number > 0 ? node->line_number : 1);
                    }
                } else if (node->data.op.left->type != NODE_ARRAY_ACCESS &&
                   node->data.op.left->type != NODE_STRUCT_ACCESS &&
                   !(node->data.op.left->type == NODE_UNARY_OPERATION && 
                     node->data.op.left->data.unary.op == OP_DEREFERENCE)) {
                } else {
                    /* Analyze left side (could be array access, etc.) */
                    semantic_analyze_node(analyzer, node->data.op.left);
                }
            }
            
            /* Analyze the right side (value being assigned) */
            if (node->data.op.right) {
                semantic_analyze_node(analyzer, node->data.op.right);
            }
            break;
        }
        
        case NODE_OPERATION: {
            /* Analyze both operands */
            if (node->data.op.left) {
                semantic_analyze_node(analyzer, node->data.op.left);
            }
            if (node->data.op.right) {
                semantic_analyze_node(analyzer, node->data.op.right);
            }
            break;
        }
        
        case NODE_IF_STATEMENT: {
            /* Analyze condition and branches */
            if (node->data.if_stmt.condition) {
                semantic_analyze_node(analyzer, node->data.if_stmt.condition);
            }
            
            /* Create new scope for then branch */
            enter_semantic_scope(analyzer, false);
            if (node->data.if_stmt.then_branch) {
                semantic_analyze_node(analyzer, node->data.if_stmt.then_branch);
            }
            exit_semantic_scope(analyzer);
            
            /* Create new scope for else branch if it exists */
            if (node->data.if_stmt.else_branch) {
                enter_semantic_scope(analyzer, false);
                semantic_analyze_node(analyzer, node->data.if_stmt.else_branch);
                exit_semantic_scope(analyzer);
            }
            break;
        }
        
        case NODE_FOR_STATEMENT: {
            /* Enter new scope for the for loop */
            enter_semantic_scope(analyzer, false);
            
            /* Analyze init, condition, increment, and body */
            if (node->data.for_stmt.init) {
                semantic_analyze_node(analyzer, node->data.for_stmt.init);
            }
            if (node->data.for_stmt.cond) {
                semantic_analyze_node(analyzer, node->data.for_stmt.cond);
            }
            if (node->data.for_stmt.incr) {
                semantic_analyze_node(analyzer, node->data.for_stmt.incr);
            }
            if (node->data.for_stmt.body) {
                semantic_analyze_node(analyzer, node->data.for_stmt.body);
            }
            
            exit_semantic_scope(analyzer);
            break;
        }
        
        case NODE_WHILE_STATEMENT: {
            /* Analyze condition */
            if (node->data.while_stmt.cond) {
                semantic_analyze_node(analyzer, node->data.while_stmt.cond);
            }
            
            /* Enter new scope for loop body */
            enter_semantic_scope(analyzer, false);
            if (node->data.while_stmt.body) {
                semantic_analyze_node(analyzer, node->data.while_stmt.body);
            }
            exit_semantic_scope(analyzer);
            break;
        }
        
        case NODE_FUNC_CALL: {
            /* Analyze function arguments */
            ArgumentList *args = node->data.func_call.arguments;
            while (args) {
                if (args->expr) {
                    semantic_analyze_node(analyzer, args->expr);
                }
                args = args->next;
            }
            break;
        }

        case NODE_UNARY_OPERATION: {
            if (node->data.unary.operand) {
                semantic_analyze_node(analyzer, node->data.unary.operand);
            }
            if (node->data.unary.op == OP_ADDRESS_OF) {
                ASTNode *operand = node->data.unary.operand;
                bool valid_lvalue = operand && (operand->type == NODE_IDENTIFIER || operand->type == NODE_ARRAY_ACCESS ||
                    (operand->type == NODE_UNARY_OPERATION && operand->data.unary.op == OP_DEREFERENCE));
                if (!valid_lvalue) {
                    add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                                      "Address-of requires an assignable expression",
                                      node->line_number > 0 ? node->line_number : 1);
                }
            } else if (node->data.unary.op == OP_DEREFERENCE) {
                if (infer_expression_pointer_level(node->data.unary.operand, analyzer) <= 0) {
                    add_semantic_error(analyzer, SEMANTIC_ERROR_INVALID_OPERATION,
                                      "Cannot dereference a non-pointer expression",
                                      node->line_number > 0 ? node->line_number : 1);
                }
            }
            break;
        }
        
        default:
            /* For other node types, recursively visit children if they exist */
            if (node->data.op.left) semantic_analyze_node(analyzer, node->data.op.left);
            if (node->data.op.right) semantic_analyze_node(analyzer, node->data.op.right);
            break;
    }
}

/* Semantic scope management functions */
SemanticScope* create_semantic_scope(SemanticScope *parent, bool is_function_scope) {
    SemanticScope *scope = SAFE_MALLOC(SemanticScope);
    if (!scope) return NULL;
    
    scope->variables = hm_new();
    scope->functions = hm_new();
    scope->parent = parent;
    scope->is_function_scope = is_function_scope;
    scope->depth = parent ? parent->depth + 1 : 0;
    
    return scope;
}

void free_semantic_scope(SemanticScope *scope) {
    if (!scope) return;
    
    if (scope->variables) {
        hm_free(scope->variables);
    }
    if (scope->functions) {
        hm_free(scope->functions);
    }
    SAFE_FREE(scope);
}

void enter_semantic_scope(SemanticAnalyzer *analyzer, bool is_function_scope) {
    if (!analyzer) return;
    
    SemanticScope *new_scope = create_semantic_scope(analyzer->current_scope, is_function_scope);
    if (!new_scope) return;
    
    analyzer->current_scope = new_scope;
    analyzer->scope_depth++;
}

void exit_semantic_scope(SemanticAnalyzer *analyzer) {
    if (!analyzer || !analyzer->current_scope) return;
    
    SemanticScope *old_scope = analyzer->current_scope;
    analyzer->current_scope = old_scope->parent;
    analyzer->scope_depth--;
    
    free_semantic_scope(old_scope);
}

bool add_semantic_variable(SemanticAnalyzer *analyzer, const String name, VarType type, int pointer_level, bool is_const) {
    if (!analyzer || !analyzer->current_scope || !name.data) return false;
    
    /* Check if variable already exists in current scope */
    if (hm_get(analyzer->current_scope->variables, name.data, name.len + 1)) {
        char error_msg[MAX_BUFFER_LEN];
        snprintf(error_msg, sizeof(error_msg), "Variable '%s' already declared in current scope", name.data);
        add_semantic_error(analyzer, SEMANTIC_ERROR_REDEFINITION, error_msg, 1);
        return false;
    }
    
    /* Create symbol entry */
    SymbolEntry *entry = SAFE_MALLOC(SymbolEntry);
    if (!entry) return false;
    
    entry->name = safe_strdup(&name);
    entry->type = type;
    entry->pointer_level = pointer_level;
    entry->is_const = is_const;
    entry->is_function = false;
    entry->return_type = NONE;
    entry->return_pointer_level = 0;
    entry->scope_depth = analyzer->scope_depth;
    entry->line_number = 1;
    entry->next = NULL;
    
    /* Add to current scope */
    hm_put(analyzer->current_scope->variables, name.data, name.len + 1, entry, sizeof(SymbolEntry*));
    
    return true;
}

bool find_semantic_variable(SemanticAnalyzer *analyzer, const String name, SymbolEntry **result) {
    if (!analyzer || !name.data || !result) return false;
    
    *result = NULL;
    
    /* Search through scope chain */
    SemanticScope *scope = analyzer->current_scope;
    while (scope) {
        SymbolEntry **entry_ptr = (SymbolEntry**)hm_get(scope->variables, name.data, name.len + 1);
        if (entry_ptr && *entry_ptr) {
            *result = *entry_ptr;
            return true;
        }
        scope = scope->parent;
    }
    
    return false;
}
