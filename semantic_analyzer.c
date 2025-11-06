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
    analyzer->symbol_table = NULL;
    analyzer->errors = NULL;
    analyzer->has_errors = false;
    analyzer->error_count = 0;
    analyzer->is_collecting_phase = false;
    analyzer->scope_depth = 0;  /* Start at global scope */
    analyzer->scope_depth = 0;
    
    return analyzer;
}

/* Free semantic analyzer */
void semantic_analyzer_free(SemanticAnalyzer *analyzer) {
    if (!analyzer) return;
    
    free_semantic_errors(analyzer->errors);
    free_symbol_table(analyzer->symbol_table);
    
    /* Free all semantic scopes */
    while (analyzer->current_scope) {
        exit_semantic_scope(analyzer);
    }
    
    SAFE_FREE(analyzer);
}

/* Main semantic analysis function - Simplified Working Version */
bool semantic_analyze(ASTNode *root) {
    if (!root) return true;
    
    SemanticAnalyzer *analyzer = semantic_analyzer_new();
    if (!analyzer) {
        fprintf(stderr, "Error: Failed to create semantic analyzer\n");
        return false;
    }
    
    /* Phase 1: Collect all declarations to build symbol table */
    analyzer->is_collecting_phase = true;
    analyzer->scope_depth = 0; /* Reset scope depth for collection */
    collect_declarations(analyzer, root);
    
    /* Phase 2: Perform semantic analysis with populated symbol table */
    analyzer->is_collecting_phase = false;
    analyzer->scope_depth = 0; /* Reset scope depth for analysis */
    
    /* Use scope-aware AST traversal for analysis */
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
                    fprintf(stderr, "Error: %s at line %d\n", error->message, error->line_number);
                } else {
                    fprintf(stderr, "Error: %s\n", error->message);
                }
                break;
        }
        
        error = error->next;
    }
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
    
    /* Skip checking during collection phase */
    if (analyzer->is_collecting_phase) return NULL;
    
    /* Check if variable is defined in symbol table first */
    const char *name = node->data.name;
    SymbolEntry *symbol = find_symbol(analyzer, name);
    
    /* If not found in symbol table, check if it's a scope error vs undefined variable */
    if (!symbol) {
        /* Check if variable exists at any scope level in symbol table */
        SymbolEntry *entry = analyzer->symbol_table;
        bool found_in_deeper_scope = false;
        
        while (entry) {
            if (strcmp(entry->name, name) == 0) {
                if (entry->scope_depth > analyzer->scope_depth) {
                    found_in_deeper_scope = true;
                    break;
                }
            }
            entry = entry->next;
        }
        
        if (found_in_deeper_scope) {
            /* Variable exists but in a deeper scope - scope error */
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Variable '%s' is out of scope", name);
            add_semantic_error(analyzer, SEMANTIC_ERROR_SCOPE_ERROR, 
                              error_msg, node->line_number > 0 ? node->line_number : 1);
        } else {
            /* Fallback to runtime variable lookup */
            Variable *var = get_variable(name);
            if (!var) {
                /* Be more conservative - only report obvious errors */
                /* Skip built-in functions, keywords, and potentially valid identifiers */
                if (!is_builtin_function(name) && 
                    strcmp(name, "ragequit") != 0 &&
                    strcmp(name, "yapping") != 0 &&
                    strcmp(name, "yappin") != 0 &&
                    strcmp(name, "baka") != 0 &&
                    strcmp(name, "chill") != 0 &&
                    strcmp(name, "slorp") != 0) {
                    /* Only report if we're sure it's an undefined variable */
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name);
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
        VarType declared_type = node->var_type; /* Type is stored in the declaration node itself */
        VarType init_type = infer_expression_type(node->data.op.right, analyzer);
        
        if (declared_type != NONE && init_type != NONE && 
            !check_type_compatibility(declared_type, init_type)) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Type mismatch in initialization of '%s': expected %s, got %s", 
                    var_name, vartype_to_string(declared_type), vartype_to_string(init_type));
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
        
        /* Skip checking during collection phase */
        if (analyzer->is_collecting_phase) return;
        
        /* Check if variable is defined in symbol table first */
        SymbolEntry *symbol = find_symbol(analyzer, var_name);
        
        /* Fallback to runtime scope if not found in symbol table */
        if (!symbol) {
            Variable *var = get_variable(var_name);
            if (!var) {
                /* Only report error for clearly undefined variables, not built-ins */
                if (!is_builtin_function(var_name) && 
                    strcmp(var_name, "ragequit") != 0 &&
                    strcmp(var_name, "yapping") != 0 &&
                    strcmp(var_name, "yappin") != 0 &&
                    strcmp(var_name, "baka") != 0 &&
                    strcmp(var_name, "chill") != 0 &&
                    strcmp(var_name, "slorp") != 0 &&
                    strcmp(var_name, "bussin") != 0) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), "Assignment to undefined variable '%s'", var_name);
                    add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE, 
                                      error_msg, node->line_number > 0 ? node->line_number : 1);
                }
                return;
            }
            
            /* Check const assignment using runtime variable */
            if (var->modifiers.is_const) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", var_name);
                add_semantic_error(analyzer, SEMANTIC_ERROR_CONST_ASSIGNMENT, 
                                  error_msg, node->line_number > 0 ? node->line_number : 1);
            }
        } else {
            /* Check const assignment using symbol table */
            if (symbol->is_const) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", var_name);
                add_semantic_error(analyzer, SEMANTIC_ERROR_CONST_ASSIGNMENT, 
                                  error_msg, node->line_number > 0 ? node->line_number : 1);
            }
        }
        
        /* TODO: Add type compatibility checking here */
        /* Type checking is complex with symbol table vs runtime scope mismatch */
        /* For now, focus on undefined variable detection */
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

/* Symbol table management functions */
void add_symbol(SemanticAnalyzer *analyzer, const char *name, VarType type, bool is_const, bool is_function, VarType return_type, int line_number) {
    if (!analyzer || !name) return;
    
    SymbolEntry *entry = SAFE_MALLOC(SymbolEntry);
    if (!entry) return;
    
    entry->name = safe_strdup(name);
    entry->type = type;
    entry->is_const = is_const;
    entry->is_function = is_function;
    entry->return_type = return_type;
    entry->line_number = line_number;
    entry->scope_depth = analyzer->scope_depth;
    entry->next = analyzer->symbol_table;
    
    analyzer->symbol_table = entry;
}

SymbolEntry* find_symbol(SemanticAnalyzer *analyzer, const char *name) {
    if (!analyzer || !name) return NULL;
    
    SymbolEntry *entry = analyzer->symbol_table;
    
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
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
    
    /* Prevent infinite recursion by limiting depth */
    static int depth = 0;
    if (depth > 1000) {
        fprintf(stderr, "Warning: Maximum recursion depth reached in collect_declarations\n");
        return;
    }
    depth++;
    
    switch (node->type) {
        case NODE_DECLARATION:
            if (node->data.op.left && node->data.op.left->data.name) {
                const char *var_name = node->data.op.left->data.name;
                VarType var_type = node->var_type; /* Type is stored in the declaration node itself */
                bool is_const = node->modifiers.is_const;
                
                add_symbol(analyzer, var_name, var_type, is_const, false, NONE, 
                          node->line_number > 0 ? node->line_number : 1);
            }
            /* Also collect declarations from the initialization expression */
            if (node->data.op.right) {
                collect_declarations(analyzer, node->data.op.right);
            }
            break;
            
        case NODE_FUNCTION_DEF:
            if (node->data.function_def.name) {
                /* Check if function already exists */
                SymbolEntry *existing = find_symbol(analyzer, node->data.function_def.name);
                if (existing && existing->is_function) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), "Function '%s' is already defined", node->data.function_def.name);
                    add_semantic_error(analyzer, SEMANTIC_ERROR_REDEFINITION, 
                                      error_msg, node->line_number > 0 ? node->line_number : 1);
                } else {
                    add_symbol(analyzer, node->data.function_def.name, NONE, false, true, 
                              node->data.function_def.return_type, 
                              node->line_number > 0 ? node->line_number : 1);
                }
            }
            
            /* Enter function scope for parameters and body */
            analyzer->scope_depth++;
            
            /* Collect function parameters as symbols */
            Parameter *param = node->data.function_def.parameters;
            while (param) {
                if (param->name) {
                    add_symbol(analyzer, param->name, param->type, false, false, NONE,
                              node->line_number > 0 ? node->line_number : 1);
                }
                param = param->next;
            }
            
            /* Recurse into function body */
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
            /* Don't recurse into condition, just the branches */
            if (node->data.if_stmt.then_branch) {
                collect_declarations(analyzer, node->data.if_stmt.then_branch);
            }
            if (node->data.if_stmt.else_branch) {
                collect_declarations(analyzer, node->data.if_stmt.else_branch);
            }
            break;
            
        case NODE_FOR_STATEMENT:
            /* Enter new scope for for loop (including init variable) */
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
            /* Enter new scope for while body */
            analyzer->scope_depth++;
            if (node->data.while_stmt.body) {
                collect_declarations(analyzer, node->data.while_stmt.body);
            }
            analyzer->scope_depth--;
            break;
            
        case NODE_DO_WHILE_STATEMENT:
            /* Enter new scope for do-while body */
            analyzer->scope_depth++;
            if (node->data.while_stmt.body) {
                collect_declarations(analyzer, node->data.while_stmt.body);
            }
            analyzer->scope_depth--;
            /* Don't recurse into condition - it's evaluated at global scope */
            break;
            
        case NODE_OPERATION:
            /* Only recurse for assignment operations that might contain declarations */
            if (node->data.op.op == OP_ASSIGN) {
                /* Skip recursion for assignments to avoid infinite loops */
                break;
            }
            /* For other operations, don't recurse to avoid expression traversal */
            break;
            
        default:
            /* For most node types, don't recurse to avoid infinite loops */
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
        
        case NODE_DECLARATION: {
            /* Use visitor method for declaration checking */
            semantic_visit_declaration((Visitor*)analyzer, node);
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
            if (node->data.op.left && node->data.op.left->data.name) {
                const char *var_name = node->data.op.left->data.name;
                VarType var_type = node->var_type;
                bool is_const = node->modifiers.is_const;
                
                add_semantic_variable(analyzer, var_name, var_type, is_const);
                
                /* Also analyze the initialization expression */
                if (node->data.op.right) {
                    semantic_analyze_node(analyzer, node->data.op.right);
                }
            }
            break;
        }
        
        case NODE_IDENTIFIER: {
            /* Check if identifier is defined when we encounter it */
            const char *name = node->data.name;
            SymbolEntry *symbol = NULL;
            
            if (!find_semantic_variable(analyzer, name, &symbol)) {
                /* Check if it's a built-in function or keyword */
                if (!is_builtin_function(name) && 
                    strcmp(name, "ragequit") != 0 &&
                    strcmp(name, "yapping") != 0 &&
                    strcmp(name, "yappin") != 0 &&
                    strcmp(name, "baka") != 0 &&
                    strcmp(name, "chill") != 0 &&
                    strcmp(name, "slorp") != 0 &&
                    strcmp(name, "bussin") != 0) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), "Undefined variable '%s'", name);
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
                    const char *var_name = node->data.op.left->data.name;
                    SymbolEntry *symbol = NULL;
                    
                    if (!find_semantic_variable(analyzer, var_name, &symbol)) {
                        char error_msg[256];
                        snprintf(error_msg, sizeof(error_msg), "Assignment to undefined variable '%s'", var_name);
                        add_semantic_error(analyzer, SEMANTIC_ERROR_UNDEFINED_VARIABLE, 
                                          error_msg, node->line_number > 0 ? node->line_number : 1);
                    } else if (symbol && symbol->is_const) {
                        char error_msg[256];
                        snprintf(error_msg, sizeof(error_msg), "Cannot assign to const variable '%s'", var_name);
                        add_semantic_error(analyzer, SEMANTIC_ERROR_CONST_ASSIGNMENT, 
                                          error_msg, node->line_number > 0 ? node->line_number : 1);
                    }
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

bool add_semantic_variable(SemanticAnalyzer *analyzer, const char *name, VarType type, bool is_const) {
    if (!analyzer || !analyzer->current_scope || !name) return false;
    
    /* Check if variable already exists in current scope */
    if (hm_get(analyzer->current_scope->variables, name, strlen(name) + 1)) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Variable '%s' already declared in current scope", name);
        add_semantic_error(analyzer, SEMANTIC_ERROR_REDEFINITION, error_msg, 1);
        return false;
    }
    
    /* Create symbol entry */
    SymbolEntry *entry = SAFE_MALLOC(SymbolEntry);
    if (!entry) return false;
    
    entry->name = safe_strdup(name);
    entry->type = type;
    entry->is_const = is_const;
    entry->is_function = false;
    entry->scope_depth = analyzer->scope_depth;
    entry->line_number = 1;
    entry->next = NULL;
    
    /* Add to current scope */
    hm_put(analyzer->current_scope->variables, name, strlen(name) + 1, entry, sizeof(SymbolEntry*));
    
    return true;
}

bool find_semantic_variable(SemanticAnalyzer *analyzer, const char *name, SymbolEntry **result) {
    if (!analyzer || !name || !result) return false;
    
    *result = NULL;
    
    /* Search through scope chain */
    SemanticScope *scope = analyzer->current_scope;
    while (scope) {
        SymbolEntry **entry_ptr = (SymbolEntry**)hm_get(scope->variables, name, strlen(name) + 1);
        if (entry_ptr && *entry_ptr) {
            *result = *entry_ptr;
            return true;
        }
        scope = scope->parent;
    }
    
    return false;
}
