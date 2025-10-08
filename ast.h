/* ast.h */

#ifndef AST_H
#define AST_H

#include "lib/hm.h"
#include "lib/arena.h"
#include "lib/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <setjmp.h>

#define MAX_VARS 100
#define MAX_ARGUMENTS 100
#define MAX_DIMENSIONS 10

/* Forward declarations */
typedef struct ASTNode ASTNode;
typedef struct StatementList StatementList;
typedef struct ArgumentList ArgumentList;
typedef struct CaseNode CaseNode;


/* Define Array Dimensions */
typedef struct
{
    int dimensions[MAX_DIMENSIONS];
    int num_dimensions;
    size_t total_size;
} ArrayDimensions;

/* Define TypeModifiers first */
typedef struct
{
    bool is_volatile;
    bool is_signed;
    bool is_unsigned;
    bool is_sizeof;
    bool is_const;
} TypeModifiers;

typedef struct JumpBuffer
{
    jmp_buf data;
    struct JumpBuffer *next;
} JumpBuffer;

typedef struct ExpressionList
{
    ASTNode *expr;
    struct ExpressionList *next;
    struct ExpressionList *prev;
} ExpressionList;

typedef enum
{
    VAR_INT,
    VAR_SHORT,
    VAR_FLOAT,
    VAR_DOUBLE,
    VAR_BOOL,
    VAR_CHAR,
    VAR_STRING,
    NONE,
} VarType;

typedef struct Parameter
{
    char *name;
    VarType type;
    TypeModifiers modifiers;
    struct Parameter *next;
} Parameter;

typedef struct Function
{
    char *name;
    VarType return_type;
    Parameter *parameters;
    ASTNode *body;
    struct Function *next;
} Function;

typedef struct
{
    bool has_value;
    union
    {
        int ivalue;
        float fvalue;
        double dvalue;
        bool bvalue;
        short svalue;
        char *strvalue;
    } value;
    VarType type;
} ReturnValue;

/* Symbol table structure */
typedef struct
{
    char *name;
    union
    {
        int ivalue;
        short svalue;
        bool bvalue;
        float fvalue;
        double dvalue;
        void *array_data;
        char *strvalue;
    } value;
    TypeModifiers modifiers;
    VarType var_type;
    bool is_array;
    int array_length; // lets keep it for now for backword compatibility
    ArrayDimensions array_dimensions;
} Variable;

typedef union
{
    VarType type;
    union
    {
        int ivalue;
        short svalue;
        bool bvalue;
        float fvalue;
        double dvalue;
        char *strvalue;
    };
} Value;

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
    OP_NEG,
    OP_POST_INC,
    OP_POST_DEC,
    OP_PRE_INC,
    OP_PRE_DEC,
    OP_ASSIGN,
} OperatorType;

/* AST node types */
typedef enum
{
    NODE_INT,
    NODE_SHORT,
    NODE_FLOAT,
    NODE_DOUBLE,
    NODE_CHAR,
    NODE_BOOLEAN,
    NODE_STRING,
    NODE_IDENTIFIER,
    NODE_ASSIGNMENT,
    NODE_DECLARATION,
    NODE_OPERATION,
    NODE_UNARY_OPERATION,
    NODE_FOR_STATEMENT,
    NODE_WHILE_STATEMENT,
    NODE_DO_WHILE_STATEMENT,
    NODE_PRINT_STATEMENT,
    NODE_ERROR_STATEMENT,
    NODE_STATEMENT_LIST,
    NODE_IF_STATEMENT,
    NODE_STRING_LITERAL,
    NODE_SWITCH_STATEMENT,
    NODE_CASE,
    NODE_DEFAULT_CASE,
    NODE_BREAK_STATEMENT,
    NODE_SIZEOF,
    NODE_ARRAY_ACCESS,
    NODE_FUNC_CALL,
    NODE_FUNCTION_DEF,
    NODE_RETURN,
} NodeType;

typedef struct
{
    char *name;
    ASTNode *index;
    ASTNode *indices[MAX_DIMENSIONS];
    int num_dimensions;
} Array;
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
    VarType var_type;
    bool already_checked;
    bool is_valid_symbol;
    bool is_array;
    int array_length;
    ArrayDimensions array_dimensions;
    union
    {
        short svalue;
        bool bvalue;
        int ivalue;
        float fvalue;
        double dvalue;
        char *strvalue;
        char *name;
        Array array;
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
        struct
        {
            ASTNode *expr;
        } sizeof_stmt;
        struct
        {
            char *name;
            VarType return_type;
            Parameter *parameters;
            ASTNode *body;
        } function_def;
        ASTNode *break_stmt;
    } data;
};

typedef struct Scope
{
    HashMap *variables;
    struct Scope *parent;
    bool is_function_scope;
} Scope;

/* Global variable declarations */
extern TypeModifiers current_modifiers;
extern Scope *current_scope;
extern Function *function_table;
extern ReturnValue current_return_value;
extern JumpBuffer *jump_buffer;
/* Function prototypes */
bool set_int_variable(const char *name, int value, TypeModifiers mods);
bool set_array_variable(char *name, int length, TypeModifiers mods, VarType type);
bool set_short_variable(const char *name, short value, TypeModifiers mods);
bool set_float_variable(const char *name, float value, TypeModifiers mods);
bool set_double_variable(const char *name, double value, TypeModifiers mods);
TypeModifiers get_variable_modifiers(const char *name);
void reset_modifiers(void);
TypeModifiers get_current_modifiers(void);
Variable *get_variable(const char *name);
Scope *create_scope(Scope *parent);
void enter_function_scope(Function *func, ArgumentList *args);
void exit_scope();
void enter_scope();
void free_scope(Scope *scope);
void add_variable_to_scope(const char *name, Variable *var);
Variable *variable_new(char *name);
Function *get_function(const char *name);
VarType get_function_return_type(const char *name);

/* Node creation functions */
ASTNode *create_int_node(int value);
ASTNode *create_array_declaration_node(char *name, int length, VarType type);
ASTNode *create_array_access_node(char *name, ASTNode *index);
ASTNode *create_short_node(short value);
ASTNode *create_float_node(float value);
ASTNode *create_double_node(double value);
ASTNode *create_char_node(char value);
ASTNode *create_boolean_node(bool value);
ASTNode *create_identifier_node(char *name);
ASTNode *create_assignment_node(char *name, ASTNode *expr);
ASTNode *create_declaration_node(char *name, ASTNode *expr);
ASTNode *create_operation_node(OperatorType op, ASTNode *left, ASTNode *right);
ASTNode *create_unary_operation_node(OperatorType op, ASTNode *operand);
ASTNode *create_for_statement_node(ASTNode *init, ASTNode *cond, ASTNode *incr, ASTNode *body);
ASTNode *create_while_statement_node(ASTNode *cond, ASTNode *body);
ASTNode *create_do_while_statement_node(ASTNode *cond, ASTNode *body);
ASTNode *create_function_call_node(char *func_name, ArgumentList *args);
ArgumentList *create_argument_list(ASTNode *expr, ArgumentList *existing_list);
ASTNode *create_print_statement_node(ASTNode *expr);
ASTNode *create_sizeof_node(ASTNode *node);
ASTNode *create_error_statement_node(ASTNode *expr);
ASTNode *create_statement_list(ASTNode *statement, ASTNode *next_statement);
ASTNode *create_if_statement_node(ASTNode *condition, ASTNode *then_branch, ASTNode *else_branch);
ASTNode *create_string_literal_node(char *string);
ASTNode *create_switch_statement_node(ASTNode *expression, CaseNode *cases);
CaseNode *create_case_node(ASTNode *value, ASTNode *statements);
CaseNode *create_default_case_node(ASTNode *statements);
CaseNode *append_case_list(CaseNode *list, CaseNode *case_node);
ASTNode *create_break_node(void);
ASTNode *create_default_node(VarType var_type);
ASTNode *create_return_node(ASTNode *expr);
ExpressionList *create_expression_list(ASTNode *expr);
ExpressionList *append_expression_list(ExpressionList *list, ASTNode *expr);
void free_expression_list(ExpressionList *list);
void populate_multi_array_variable(char *name, ExpressionList *list, int dimensions[], int num_dimensions);
void free_ast(void);

/* Evaluation and execution functions */
void *evaluate_array_access(ASTNode *node);
double evaluate_expression_double(ASTNode *node);
float evaluate_expression_float(ASTNode *node);
int evaluate_expression_int(ASTNode *node);
short evaluate_expression_short(ASTNode *node);
bool evaluate_expression_bool(ASTNode *node);
int evaluate_expression(ASTNode *node);
bool is_const_variable(const char *name);
void check_const_assignment(const char *name);
void execute_statement(ASTNode *node);
void execute_statements(ASTNode *node);
void execute_assignment(ASTNode *node);
void execute_for_statement(ASTNode *node);
void execute_while_statement(ASTNode *node);
void execute_do_while_statement(ASTNode *node);
void execute_if_statement(ASTNode *node);
void execute_yapping_call(ArgumentList *args);
void execute_yappin_call(ArgumentList *args);
void execute_baka_call(ArgumentList *args);
void execute_ragequit_call(ArgumentList *args);
void execute_chill_call(ArgumentList *args);
void execute_slorp_call(ArgumentList *args);
void reset_modifiers(void);
bool check_and_mark_identifier(ASTNode *node, const char *contextErrorMessage);
bool is_expression(ASTNode *node, VarType type);
void bruh();
size_t count_expression_list(ExpressionList *list);
size_t handle_sizeof(ASTNode *node);
size_t get_type_size(char *name);
void *handle_function_call(ASTNode *node);
ASTNode *create_multi_array_declaration_node(char *name, int dimensions[], int num_dimensions, VarType type);
bool set_multi_array_variable(const char *name, int dimensions[], int num_dimensions, TypeModifiers mods, VarType type);
ASTNode *create_array_access_node_single(char *name, ASTNode *index);
ASTNode *create_multi_array_access_node(char *name, ASTNode *indices[], int num_indices);

/* User-defined functions */
Function *create_function(char *name, VarType return_type, Parameter *params, ASTNode *body);
Parameter *create_parameter(char *name, VarType type, Parameter *next, TypeModifiers mods);
void execute_function_call(const char *name, ArgumentList *args);
ASTNode *create_function_def_node(char *name, VarType return_type, Parameter *params, ASTNode *body);
void handle_return_statement(ASTNode *expr);
void *handle_binary_operation(ASTNode *node);
void free_function_table(void);

extern TypeModifiers current_modifiers;

extern Arena arena;

#define ARENA_ALLOC(type) arena_alloc(&arena, sizeof(type))
#define ARENA_STRDUP(str) arena_strdup(&arena, str)

/* Macros for assigning specific fields to a node */
#define SET_DATA_INT(node, value) ((node)->data.ivalue = (value))
#define SET_DATA_SHORT(node, value) ((node)->data.svalue = (value))
#define SET_DATA_FLOAT(node, value) ((node)->data.fvalue = (value))
#define SET_DATA_DOUBLE(node, value) ((node)->data.dvalue = (value))
#define SET_DATA_BOOL(node, value) ((node)->data.bvalue = (value) ? 1 : 0)
#define SET_DATA_NAME(node, n) ((node)->data.name = ARENA_STRDUP(n))
#define SET_SIZEOF(node, n) ((node)->data.sizeof_stmt.expr = (n))
#define SET_DATA_OP(node, l, r, opr) \
    do                               \
    {                                \
        (node)->data.op.left = (l);  \
        (node)->data.op.right = (r); \
        (node)->data.op.op = (opr);  \
    } while (0)

#define SET_DATA_UNARY_OP(node, o, opr)   \
    do                                    \
    {                                     \
        (node)->data.unary.operand = (o); \
        (node)->data.unary.op = (opr);    \
    } while (0)

#define SET_DATA_FOR(node, i, c, inc, b)    \
    do                                      \
    {                                       \
        (node)->data.for_stmt.init = (i);   \
        (node)->data.for_stmt.cond = (c);   \
        (node)->data.for_stmt.incr = (inc); \
        (node)->data.for_stmt.body = (b);   \
    } while (0)

#define SET_DATA_WHILE(node, c, b)          \
    do                                      \
    {                                       \
        (node)->data.while_stmt.cond = (c); \
        (node)->data.while_stmt.body = (b); \
    } while (0)

#define SET_DATA_FUNC_CALL(node, func_name, args)                      \
    do                                                                 \
    {                                                                  \
        (node)->data.func_call.function_name = ARENA_STRDUP(func_name); \
        (node)->data.func_call.arguments = (args);                     \
    } while (0)

/* Macros for handling jump buffer */
#define PUSH_JUMP_BUFFER()                        \
    do                                            \
    {                                             \
        JumpBuffer *jb = SAFE_MALLOC(JumpBuffer); \
        jb->next = jump_buffer;                   \
        jump_buffer = jb;                         \
    } while (0)

#define POP_JUMP_BUFFER()                \
    do                                   \
    {                                    \
        JumpBuffer *jb = jump_buffer;    \
        jump_buffer = jump_buffer->next; \
        SAFE_FREE(jb);                   \
    } while (0)

#define LONGJMP()                                \
    do                                           \
    {                                            \
        if (jump_buffer != NULL)                 \
        {                                        \
            longjmp(jump_buffer->data, 1);       \
        }                                        \
        else                                     \
        {                                        \
            yyerror("No jump buffer available"); \
            exit(1);                             \
        }                                        \
    } while (0)

#define CURRENT_JUMP_BUFFER() (jump_buffer->data)

#define CLEAN_JUMP_BUFFER() \
    do                      \
    {                       \
        while (jump_buffer) \
        {                    \
            POP_JUMP_BUFFER(); \
        }                    \
    } while (0)

#define VART_TO_NODET(var_type) \
    ((var_type) == VAR_INT ? NODE_INT         \
     : (var_type) == VAR_SHORT ? NODE_SHORT   \
     : (var_type) == VAR_FLOAT ? NODE_FLOAT   \
     : (var_type) == VAR_DOUBLE ? NODE_DOUBLE \
     : (var_type) == VAR_BOOL ? NODE_BOOLEAN  \
     : (var_type) == VAR_CHAR ? NODE_CHAR     \
     : (var_type) == VAR_STRING ? NODE_STRING : (NodeType)-1)
#endif /* AST_H */
