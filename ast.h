/* ast.h */

#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_VARS 100

/* Forward declarations */
typedef struct ASTNode ASTNode;
typedef struct StatementList StatementList;
typedef struct ArgumentList ArgumentList;
typedef struct CaseNode CaseNode;

/* Define TypeModifiers first */
typedef struct
{
    bool is_volatile;
    bool is_signed;
    bool is_unsigned;
    bool is_boolean;
    bool is_sizeof;
} TypeModifiers;

/* Symbol table structure */
typedef struct
{
    char *name;
    union
    {
        int ivalue;
        float fvalue;
        double dvalue;
    } value;
    bool is_float;
    bool is_double;
    TypeModifiers modifiers;
} variable;

/* Operator types */
typedef enum
{
    OP_PLUS,
    OP_MINUS,
    OP_TIMES,
    OP_DIVIDE,
    OP_MOD,
    OP_LT,
    OP_GT,
    OP_LE,
    OP_GE,
    OP_EQ,
    OP_NE,
    OP_AND,
    OP_OR,
    OP_NEG
} OperatorType;

/* AST node types */
typedef enum
{
    NODE_INT,
    NODE_FLOAT,
    NODE_DOUBLE,
    NODE_CHAR,
    NODE_BOOLEAN,
    NODE_IDENTIFIER,
    NODE_ASSIGNMENT,
    NODE_OPERATION,
    NODE_UNARY_OPERATION,
    NODE_FOR_STATEMENT,
    NODE_WHILE_STATEMENT,
    NODE_PRINT_STATEMENT,
    NODE_ERROR_STATEMENT,
    NODE_STATEMENT_LIST,
    NODE_IF_STATEMENT,
    NODE_STRING_LITERAL,
    NODE_SWITCH_STATEMENT,
    NODE_CASE,
    NODE_DEFAULT_CASE,
    NODE_BREAK_STATEMENT,
    NODE_FUNC_CALL,
    NODE_SIZEOF
} NodeType;

/* Rest of the structure definitions */
struct StatementList
{
    ASTNode *statement;
    struct StatementList *next;
};

typedef struct
{
    ASTNode *condition;
    ASTNode *then_branch;
    ASTNode *else_branch;
} IfStatementNode;

struct CaseNode
{
    ASTNode *value;
    ASTNode *statements;
    struct CaseNode *next;
};

struct ArgumentList
{
    struct ASTNode *expr;
    struct ArgumentList *next;
};

/* AST node structure */
struct ASTNode
{
    NodeType type;
    TypeModifiers modifiers;
    bool alreadyChecked;
    bool isValidSymbol;
    union
    {
        int ivalue;
        float fvalue;
        double dvalue;
        char *name;
        struct
        {
            ASTNode *left;
            ASTNode *right;
            OperatorType op;
        } op;
        struct
        {
            ASTNode *operand;
            OperatorType op;
        } unary;
        struct
        {
            ASTNode *init;
            ASTNode *cond;
            ASTNode *incr;
            ASTNode *body;
        } for_stmt;
        struct
        {
            ASTNode *cond;
            ASTNode *body;
        } while_stmt;
        struct
        {
            char *function_name;
            ArgumentList *arguments;
        } func_call;
        StatementList *statements;
        IfStatementNode if_stmt;
        struct
        {
            ASTNode *expression;
            CaseNode *cases;
        } switch_stmt;
        ASTNode *break_stmt;
    } data;
};

/* Global variable declarations */
extern TypeModifiers current_modifiers;
extern variable symbol_table[MAX_VARS];
extern int var_count;

/* Function prototypes */
bool set_int_variable(char *name, int value, TypeModifiers mods);
bool set_float_variable(char *name, float value, TypeModifiers mods);
bool set_double_variable(char *name, double value, TypeModifiers mods);
TypeModifiers get_variable_modifiers(const char *name);
void reset_modifiers(void);
TypeModifiers get_current_modifiers(void);

/* Node creation functions */
ASTNode *create_int_node(int value);
ASTNode *create_float_node(float value);
ASTNode *create_double_node(double value);
ASTNode *create_char_node(char value);
ASTNode *create_boolean_node(int value);
ASTNode *create_identifier_node(char *name);
ASTNode *create_assignment_node(char *name, ASTNode *expr);
ASTNode *create_operation_node(OperatorType op, ASTNode *left, ASTNode *right);
ASTNode *create_unary_operation_node(OperatorType op, ASTNode *operand);
ASTNode *create_for_statement_node(ASTNode *init, ASTNode *cond, ASTNode *incr, ASTNode *body);
ASTNode *create_while_statement_node(ASTNode *cond, ASTNode *body);
ASTNode *create_function_call_node(char *func_name, ArgumentList *args);
ArgumentList *create_argument_list(ASTNode *expr, ArgumentList *existing_list);
ASTNode *create_print_statement_node(ASTNode *expr);
ASTNode *create_sizeof_node(char *identifier);
ASTNode *create_error_statement_node(ASTNode *expr);
ASTNode *create_statement_list(ASTNode *statement, ASTNode *next_statement);
ASTNode *create_if_statement_node(ASTNode *condition, ASTNode *then_branch, ASTNode *else_branch);
ASTNode *create_string_literal_node(char *string);
ASTNode *create_switch_statement_node(ASTNode *expression, CaseNode *cases);
CaseNode *create_case_node(ASTNode *value, ASTNode *statements);
CaseNode *create_default_case_node(ASTNode *statements);
CaseNode *append_case_list(CaseNode *list, CaseNode *case_node);
ASTNode *create_break_node(void);

/* Evaluation and execution functions */
double evaluate_expression_double(ASTNode *node);
float evaluate_expression_float(ASTNode *node);
int evaluate_expression_int(ASTNode *node);
int evaluate_expression(ASTNode *node);
bool is_double_expression(ASTNode *node);
bool is_float_expression(ASTNode *node);
void execute_statement(ASTNode *node);
void execute_statements(ASTNode *node);
void execute_assignment(ASTNode *node);
void execute_for_statement(ASTNode *node);
void execute_while_statement(ASTNode *node);
void execute_yapping_call(ArgumentList *args);
void execute_yappin_call(ArgumentList *args);
void execute_baka_call(ArgumentList *args);
void execute_ragequit_call(ArgumentList *args);
void free_ast(ASTNode *node);
void reset_modifiers(void);
bool check_and_mark_identifier(ASTNode *node, const char *contextErrorMessage);

extern TypeModifiers current_modifiers;

#endif /* AST_H */
