/* ast.h */

#ifndef AST_H
#define AST_H

#include "lib/hm.h"
#include "lib/arena.h"
#include "lib/mem.h"
#include "lib/string_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>

#define MAX_VARS 100
#define MAX_ARGUMENTS 100
#define MAX_DIMENSIONS 10

/* Forward declarations */
typedef struct ASTNode ASTNode;
typedef struct StatementList StatementList;
typedef struct ArgumentList ArgumentList;
typedef struct CaseNode CaseNode;

typedef struct
{
    String name;
    int pointer_level;
} Declarator;

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
    bool is_long;
    bool is_long_long;
    bool is_static;
} TypeModifiers;

typedef struct JumpBuffer
{
    jmp_buf data;
    struct JumpBuffer *next;
} JumpBuffer;

typedef struct ExpressionList
{
    ASTNode *expr;                  /* scalar item; NULL when sublist is used */
    struct ExpressionList *sublist; /* nested brace-init, e.g. { {1,2}, 3 } */
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
    VAR_STRUCT,
    VAR_ENUM,
    /* An opaque native pointer (STDROT_PTR): a real, known expression
       category -- "this is a pointer, its base type is intentionally
       erased" -- not to be confused with NONE ("unknown, skip
       checking"). Deliberately its own VarType rather than reusing NONE:
       every existing "type == NONE, don't validate" shortcut throughout
       this analyzer (binary-op validation, declaration/assignment type
       checks, native-argument checks) would otherwise silently wave a
       real pointer expression through unchecked wherever it appears, and
       whatever consumes it downstream (a scalar evaluator, a native
       expecting a different type) would reinterpret its raw bits as
       whatever that context assumed instead. VAR_PTR only ever appears
       with pointer_level > 0; check_type_compatibility_ex() treats it as
       wildcard-compatible with any base type once pointer_level already
       matches, not as "give up checking." NOT the same guarantee as C's
       void* conversion rule, despite the surface resemblance: void* <->
       T* is sound for exactly one level of indirection, but opaque** <->
       T** is not (see check_type_compatibility_ex()'s own comment,
       semantic_analyzer.c, and STDROT_PTR's comment in stdrot_api.h, for
       why this is an intentional simplification -- the base type is
       erased uniformly at every depth, not just the outermost one). */
    VAR_PTR,
    /* A genuinely void expression -- a call whose descriptor return type
       is STDROT_NONE, known with total certainty to produce no value.
       Deliberately its own VarType, distinct from NONE, for exactly the
       reason VAR_PTR (above) is distinct from NONE: NONE means "I do not
       know what this expression returns" (epistemology), VAR_VOID means
       "I know precisely what it returns: nothing" (semantics). Those are
       not the same claim, and collapsing them into one sentinel meant
       every "type == NONE, fail open" shortcut in this analyzer also
       silently waved through an ACTUALLY KNOWN void expression wherever
       one was used as a value -- `rizz x = yapping("hi");` type-checked,
       because `yapping`'s return type read as NONE, and NONE always
       means "can't tell, don't block it." A void-typed expression should
       be rejected everywhere a real value is required, with the same
       confidence any other declared type mismatch is rejected -- not
       waved through because the checker mistook "certainly nothing" for
       "unknown." check_type_compatibility_ex() treats VAR_VOID as
       compatible with nothing (not even itself, since there's no context
       where consuming "no value" as a value is ever correct). */
    VAR_VOID,
    NONE,
} VarType;

typedef struct
{
    VarType type;
    int pointer_level;
    TypeModifiers modifiers;
    /* Borrowed tag names. Persisting users must copy these into their own
       storage, usually the arena. */
    String struct_name;
    String enum_name;
} TypeDescriptor;

/* A single field inside a struct definition */
typedef struct StructField
{
    String name;
    VarType type;
    String struct_name; /* nested struct/union tag; set whenever
                            type == VAR_STRUCT, including pointer-typed
                            fields (pointer_level > 0) — e.g. a
                            self-referential `gang Node *next;` field still
                            needs its tag recorded even though chaining `.`
                            through it isn't supported yet */
    String enum_name;   /* nested enum tag; set whenever type == VAR_ENUM */
    int pointer_level;
    size_t offset; /* byte offset within the struct blob */
    struct StructField *next;
} StructField;

/* A single named constant inside an enum body. has_explicit_value/value
   also serve as scratch state for the auto-increment pass. */
typedef struct EnumConstant
{
    String name;
    int value;
    bool has_explicit_value;
    struct EnumConstant *next;
} EnumConstant;

/* An enum definition (the "template"). Enum tags get their own registry,
   separate from struct/union's, matching C's distinct tag namespaces. */
typedef struct EnumDef
{
    String name;
    EnumConstant *constants;
    struct EnumDef *next_def;
} EnumDef;

typedef struct TypeAlias
{
    String name;
    VarType type;
    int pointer_level;
    TypeModifiers modifiers;
    /* Owned by the alias registry. */
    String struct_name;
    String enum_name;
    struct TypeAlias *next_def;
} TypeAlias;

/* A struct/union definition (the "template"). Struct and union tags share
   this same registry, matching C's tag-namespace rules. */
typedef struct StructDef
{
    String name;
    StructField *fields;
    size_t total_size; /* total byte size of one instance */
    bool is_union;     /* true: fields overlap at offset 0 (chungus/union) */
    struct StructDef *next_def;
} StructDef;

/* AST helper functions */
ASTNode *arena_alloc_astnode(void);

typedef struct Parameter
{
    String name;
    VarType type;
    String struct_name; /* nested struct/union tag; set whenever
                            type == VAR_STRUCT, including pointer-typed
                            fields (pointer_level > 0) — e.g. a
                            self-referential `gang Node *next;` field still
                            needs its tag recorded even though chaining `.`
                            through it isn't supported yet */
    String enum_name;   /* enum tag; set whenever type == VAR_ENUM */
    int pointer_level;
    TypeModifiers modifiers;
    struct Parameter *next;
} Parameter;

typedef struct Function
{
    String name;
    VarType return_type;
    int return_pointer_level;
    String return_struct_name; /* struct/union tag; set when
                                   return_type == VAR_STRUCT */
    String return_enum_name;   /* enum tag; set when return_type == VAR_ENUM */
    Parameter *parameters;
    ASTNode *body;
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
        String strvalue;
        uintptr_t pvalue;
    } value;
    VarType type;
    int pointer_level;
    /* struct/union tag, set when type == VAR_STRUCT; value.pvalue then
       points at a heap blob (malloc'd fresh in handle_return_statement,
       NOT scope-owned) that the caller must copy out of and free -- see
       the comment on handle_return_statement's VAR_STRUCT case. */
    String struct_name;
    String enum_name; /* enum tag, set when type == VAR_ENUM */
    /* Set when type == VAR_STRING: true iff value.strvalue.data is a
       heap buffer this ReturnValue is responsible for freeing once
       consumed. Two independent sources set it true, both meaning the
       same thing ("this specific buffer is freshly materialized and
       nothing else references it"): a native call, via the owns_string
       field of the NativeResult (stdrot.h) marshal_native_return_value()
       consumed to populate this slot (see execute_native_call()'s own
       comment for when that's true); and, since round 23, a Brainrot-
       defined function's own `bussin "some string";` return statement
       (handle_return_statement()'s VAR_STRING case), whose evaluate_
       expression_string() result is always an independently safe_
       strdup'd copy no one else holds. False for a native result that
       doesn't own its string (a literal, a live variable's own backing
       storage the ABI boundary is only borrowing), matching the
       existing, deliberately conservative default. */
    bool owns_strvalue;
} ReturnValue;

/* Symbol table structure */
typedef struct
{
    String name;
    union
    {
        int ivalue;
        short svalue;
        bool bvalue;
        float fvalue;
        double dvalue;
        void *array_data;
        String strvalue;
        uintptr_t pvalue;
    } value;
    TypeModifiers modifiers;
    VarType var_type;
    int pointer_level;
    bool is_array;
    int array_length; // lets keep it for now for backword compatibility
    ArrayDimensions array_dimensions;
    String struct_name; /* non-NULL when var_type == VAR_STRUCT */
    String enum_name;   /* non-NULL when var_type == VAR_ENUM */
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
        String strvalue;
        uintptr_t pvalue;
    };
    int pointer_level;
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
    OP_ADDRESS_OF,
    OP_DEREFERENCE,
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
    NODE_STRUCT_DEF,
    NODE_STRUCT_ACCESS,
    NODE_ENUM_DEF,
} NodeType;

typedef struct
{
    String name;
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
    int pointer_level;
    int array_length;
    ArrayDimensions array_dimensions;
    int line_number; /* Line number for error reporting */
    /* Diagnostic-only scratch field for a NODE_FUNC_CALL: the type
       propagate_contextual_call_type() (semantic_analyzer.c) found in this
       call's surrounding typed context, set whether or not that type was
       actually usable (a synthetic type-witness argument was attached).
       NONE until a context site has looked at this call. Lets semantic_
       visit_function_call()'s "cannot infer type" diagnostic pick a
       specific reason (e.g. a scalar rant needs a buffer instead) without
       var_type -- which every other pass treats as "this node's actual,
       resolved type" -- ever claiming a call resolved when no witness was
       attached. Never read by infer_expression_type()/anything else: a
       call's real type always comes from its (possibly still-empty)
       argument list via return_like_arg. */
    VarType contextual_type_hint;
    /* Array/struct declaration initializer values (e.g. the `{1, 2, 3}` in
       `rizz arr[3] = {1, 2, 3};`), carried from parse time to the runtime
       declaration visitor -- which allocates and populates storage in
       whatever scope is current at execution time, not parse time -- so
       function-local arrays/structs get their own per-call storage. NULL
       when the declaration has no braced initializer. Owned by a registry
       freed once in free_ast(); not owned by this node. */
    ExpressionList *pending_initializer;
    /* A struct declaration's initializer when it's a plain expression
       rather than a `{ ... }` list (e.g. the `make_point(1, 2)` in
       `gang Point r = make_point(1, 2);`, or another struct variable for
       copy-init) -- evaluated at runtime by the declaration visitor. NULL
       for the no-initializer and braced-initializer declaration forms. */
    ASTNode *struct_init_expr;
    /* Enum tag for a NODE_DECLARATION node with var_type == VAR_ENUM; not
       in the union below since it carries no blob/fields to go with it. */
    String enum_name;
    union
    {
        short svalue;
        bool bvalue;
        int ivalue;
        float fvalue;
        double dvalue;
        String strvalue;
        String name;
        Array array;
        struct
        {
            String struct_name; /* name of the struct type */
            String member_name; /* field being accessed     */
            ASTNode *object;    /* the struct-valued expr   */
        } struct_access;

        struct
        {
            String name;           /* struct tag               */
            StructField *fields;   /* linked list of fields    */
            int initializer_count; /* -1: no braced initializer;
                                       else count of values given */
        } struct_def;
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
            String function_name;
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
            String name;
            VarType return_type;
            String return_struct_name; /* struct/union tag; set when
                                           return_type == VAR_STRUCT */
            String return_enum_name;   /* enum tag; set when
                                           return_type == VAR_ENUM */
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
    String function_name;
} Scope;

/* Global variable declarations */
extern TypeModifiers current_modifiers;
extern Scope *current_scope;
extern HashMap *function_map;
extern ReturnValue current_return_value;
extern JumpBuffer *jump_buffer;
/* Function prototypes */
bool set_int_variable(const String name, int value, TypeModifiers mods);
bool set_array_variable(String name, int length, TypeModifiers mods,
                        VarType type);
bool set_short_variable(const String name, short value, TypeModifiers mods);
bool set_float_variable(const String name, float value, TypeModifiers mods);
bool set_double_variable(const String name, double value, TypeModifiers mods);
TypeModifiers get_variable_modifiers(const String name);
void reset_modifiers(void);
TypeModifiers get_current_modifiers(void);
Variable *get_variable(const String name);
Scope *create_scope(Scope *parent);
/* Returns false (and reports an error via yyerror) if argument binding
   failed -- mismatched arg count, an unsupported parameter type, or a
   struct argument that isn't a plain same-typed struct variable -- in
   which case no scope was left behind (any partially-created one is torn
   down) and the caller must not execute the function body. */
bool enter_function_scope(Function *func, ArgumentList *args);
void exit_scope();
void enter_scope();
void free_scope(Scope *scope);
void add_variable_to_scope(const String name, Variable *var);
Variable *variable_new(String name);
Function *get_function(const String name);
VarType get_function_return_type(const String name);

/* Node creation functions */
ASTNode *create_int_node(int value);
ASTNode *create_array_declaration_node(String name, int length, VarType type);
ASTNode *create_array_access_node(String name, ASTNode *index);
ASTNode *create_short_node(short value);
ASTNode *create_float_node(float value);
ASTNode *create_double_node(double value);
ASTNode *create_char_node(char value);
ASTNode *create_boolean_node(bool value);
ASTNode *create_identifier_node(String name);
ASTNode *create_identifier_node_ex(String name, int pointer_level);
ASTNode *create_assignment_node(String name, ASTNode *expr);
ASTNode *create_assignment_target_node(ASTNode *target, ASTNode *expr);
ASTNode *create_declaration_node(String name, ASTNode *expr);
ASTNode *create_declaration_node_ex(String name, ASTNode *expr,
                                    int pointer_level);
ASTNode *create_operation_node(OperatorType op, ASTNode *left, ASTNode *right);
ASTNode *create_unary_operation_node(OperatorType op, ASTNode *operand);
ASTNode *create_for_statement_node(ASTNode *init, ASTNode *cond, ASTNode *incr,
                                   ASTNode *body);
ASTNode *create_while_statement_node(ASTNode *cond, ASTNode *body);
ASTNode *create_do_while_statement_node(ASTNode *cond, ASTNode *body);
ASTNode *create_function_call_node(String func_name, ArgumentList *args);
ArgumentList *create_argument_list(ASTNode *expr, ArgumentList *existing_list);
ASTNode *create_print_statement_node(ASTNode *expr);
ASTNode *create_sizeof_node(ASTNode *node);
ASTNode *create_error_statement_node(ASTNode *expr);
ASTNode *create_statement_list(ASTNode *statement, ASTNode *next_statement);
ASTNode *create_if_statement_node(ASTNode *condition, ASTNode *then_branch,
                                  ASTNode *else_branch);
ASTNode *create_string_literal_node(String string);
ASTNode *create_switch_statement_node(ASTNode *expression, CaseNode *cases);
CaseNode *create_case_node(ASTNode *value, ASTNode *statements);
CaseNode *create_default_case_node(ASTNode *statements);
CaseNode *append_case_list(CaseNode *list, CaseNode *case_node);
ASTNode *create_break_node(void);
ASTNode *create_default_node(VarType var_type, int pointer_level);
ASTNode *create_return_node(ASTNode *expr);
ExpressionList *create_expression_list(ASTNode *expr);
ExpressionList *append_expression_list(ExpressionList *list, ASTNode *expr);
/* Wrap an already-built ExpressionList as a single circular-list-of-one
   node whose `sublist` (not `expr`) holds it — used for a braced nested
   initializer item, e.g. the `{1, 2}` in `Outer o = { {1, 2}, 3 };`. */
ExpressionList *create_expression_sublist(ExpressionList *sub);
/* Like append_expression_list(), but splices an already-built node
   (typically from create_expression_list() or create_expression_sublist())
   onto the end of `list` instead of allocating one from a raw ASTNode. */
ExpressionList *append_expression_list_node(ExpressionList *list,
                                            ExpressionList *node);
void free_expression_list(ExpressionList *list);
void populate_multi_array_variable(String name, ExpressionList *list,
                                   int dimensions[], int num_dimensions);
/* Attaches a braced initializer to an array/struct declaration node so the
   runtime declaration visitor can populate storage once it exists (see
   ASTNode.pending_initializer). Takes ownership of `list` via an internal
   registry freed in free_ast(); callers must not free it themselves. */
void set_declaration_pending_initializer(ASTNode *node, ExpressionList *list);
void free_ast(void);

/* Evaluation and execution functions */
void *evaluate_array_access(ASTNode *node);
double evaluate_expression_double(ASTNode *node);
float evaluate_expression_float(ASTNode *node);
int evaluate_expression_int(ASTNode *node);
short evaluate_expression_short(ASTNode *node);
bool evaluate_expression_bool(ASTNode *node);
int evaluate_expression(ASTNode *node);
bool is_const_variable(const String name);
void check_const_assignment(const String name);
void execute_statement(ASTNode *node);
void execute_statements(ASTNode *node);
void execute_assignment(ASTNode *node);
void execute_for_statement(ASTNode *node);
void execute_while_statement(ASTNode *node);
void execute_do_while_statement(ASTNode *node);
void execute_if_statement(ASTNode *node);
void reset_modifiers(void);
bool check_and_mark_identifier(ASTNode *node, const String contextErrorMessage);
bool is_expression(ASTNode *node, VarType type);
int get_expression_pointer_level(ASTNode *node);
uintptr_t evaluate_expression_pointer(ASTNode *node);
void *evaluate_lvalue_address(ASTNode *node);
void bruh();
size_t count_expression_list(ExpressionList *list);
size_t handle_sizeof(ASTNode *node);
size_t get_type_size(String name);
size_t get_type_size_for_descriptor(VarType type, int pointer_level,
                                    TypeModifiers mods);
void *handle_function_call(ASTNode *node);
ASTNode *create_multi_array_declaration_node(String name, int dimensions[],
                                             int num_dimensions, VarType type);
bool set_multi_array_variable(const String name, int dimensions[],
                              int num_dimensions, TypeModifiers mods,
                              VarType type);
ASTNode *create_array_access_node_single(String name, ASTNode *index);
ASTNode *create_multi_array_access_node(String name, ASTNode *indices[],
                                        int num_indices);

/* User-defined functions */
Function *create_function(String name, VarType return_type, Parameter *params,
                          ASTNode *body);
Function *create_function_ex(String name, VarType return_type,
                             int return_pointer_level, Parameter *params,
                             ASTNode *body);
Parameter *create_parameter(String name, VarType type, Parameter *next,
                            TypeModifiers mods);
Parameter *create_parameter_ex(String name, VarType type, int pointer_level,
                               Parameter *next, TypeModifiers mods);
void execute_function_call(const String name, ArgumentList *args);
ASTNode *create_function_def_node(String name, VarType return_type,
                                  Parameter *params, ASTNode *body);
ASTNode *create_function_def_node_ex(String name, VarType return_type,
                                     int return_pointer_level,
                                     Parameter *params, ASTNode *body);
/* Like create_function_def_node_ex(), for a struct/union-by-value return
   type (e.g. `gang Point make_point(...) { ... }`), which needs a tag name
   rather than a VarType. pointer_level > 0 (`gang Point *make_point()`) is
   rejected with a clear error -- see the comment in the implementation. */
ASTNode *create_function_def_node_struct(String name, String struct_name,
                                         int pointer_level, Parameter *params,
                                         ASTNode *body);
/* Enum-by-value return type. Unlike the struct variant above, no
   pointer_level restriction is needed -- an enum return is just an int. */
ASTNode *create_function_def_node_enum(String name, String enum_name,
                                       int pointer_level, Parameter *params,
                                       ASTNode *body);
void handle_return_statement(ASTNode *expr);
/* Frees current_return_value's struct blob/tag or owned VAR_STRING buffer
   (see handle_return_statement's VAR_STRUCT/VAR_STRING cases) if one is
   pending and unconsumed, and clears the slot. Idempotent -- safe to call
   whether or not there's anything to free. Must be called by whichever
   consumes or discards a struct- or owned-string-returning call's result:
   interpreter_visit_declaration's struct_init_expr handling,
   execute_function_call (for a leftover from a previous call), and any
   context that gets such a return but has nowhere to put it. */
void free_pending_return_value(void);
void *handle_binary_operation(ASTNode *node);
void free_function_table(void);
void free_static_variable_map(void);

/* Struct types */
void register_struct_def(StructDef *def);
StructDef *get_struct_def(const String name);
void free_struct_registry(void);
StructField *find_struct_field(StructDef *def, const String name);
size_t
compute_struct_layout(StructField *fields); /* fills offsets, returns total */
size_t compute_union_layout(
    StructField *fields); /* offset 0 for all fields, returns max size */
ASTNode *create_struct_def_node(String name, StructField *fields);
ASTNode *create_struct_access_node(ASTNode *object, String member);
void *evaluate_struct_member_address(ASTNode *node);
void populate_struct_variable(const String name, ExpressionList *list);
/* Checks a struct/union initializer's shape (bare value vs. `{ ... }`
   sub-initializer) against `def`, without needing any Variable/storage to
   exist yet -- safe to call at parse time. See its definition in ast.c. */
void validate_struct_initializer_shape(StructDef *def, ExpressionList *list);
/* Resolve a NODE_STRUCT_ACCESS node — including a chain such as `a.b.c`,
   via recursion — to the StructDef/base-address/field describing the
   *object* being accessed (i.e. `*field_out` is the field named by this
   node's own member_name; `(char *)*base_out + (*field_out)->offset` is
   its address). Returns false on any resolution failure (undefined
   variable, wrong type, unknown member, or chaining through a
   pointer-typed struct/union field, which isn't supported). When
   `report_errors` is true, failures are also reported via yyerror();
   pass false for speculative/best-effort callers (e.g. type inference)
   that shouldn't surface parse- or runtime-time diagnostics of their own. */
bool resolve_struct_access(ASTNode *node, StructDef **def_out, void **base_out,
                           StructField **field_out, bool report_errors);
/* Set when parsing produced a struct/union that's unusable (self-embedding
   by value, an unknown nested type, or — see populate_struct_fields() —
   a scalar/flattened value where a nested struct/union sub-initializer
   was required). main() checks this after yyparse() and, if set, exits the
   same way it does for a hard parse failure; see lang.y's struct_field
   for why we don't YYABORT for these instead. */
extern bool struct_def_had_error;
extern bool typedef_had_error;

/* Enum types (see the comment on EnumDef above for the registry split). */
void register_enum_def(EnumDef *def);
EnumDef *get_enum_def(const String name);
void free_enum_registry(void);
/* Fills in auto-incremented values and checks for a duplicate constant
   name, within `def` and against every already-registered enum. Returns
   false on a duplicate; `def` is still registered either way, matching
   struct_def_had_error's handling of a malformed struct/union. */
bool finalize_enum_constants(EnumDef *def);
/* Fallback lookup for a bare identifier that isn't a variable -- an
   unscoped enum constant. Returns NULL if none matches. */
EnumConstant *find_global_enum_constant(const String name);
ASTNode *create_enum_def_node(String name);

/* Typedef aliases (`lit`). */
TypeDescriptor make_type_descriptor(VarType type, int pointer_level,
                                    TypeModifiers modifiers);
/* Returned aggregate tag strings are borrowed from the alias registry. */
TypeDescriptor type_descriptor_from_alias(const TypeAlias *alias);
bool merge_type_modifiers(TypeModifiers base, TypeModifiers extra,
                          TypeModifiers *out, const String name);
/* Copies `name` and any aggregate tag strings from `descriptor`; callers keep
   ownership of their parser-token strings. */
bool register_type_alias(String name, TypeDescriptor descriptor);
TypeAlias *get_type_alias(const String name);
void free_type_alias_registry(void);

extern TypeModifiers current_modifiers;

extern Arena arena;

#define ARENA_ALLOC(type) arena_alloc(&arena, sizeof(type))
#define ARENA_ALLOC_ASTNODE() arena_alloc_astnode()
#define ARENA_STRDUP(str) arena_strdup(&arena, str)

/* Macros for assigning specific fields to a node */
#define SET_DATA_INT(node, value) ((node)->data.ivalue = (value))
#define SET_DATA_SHORT(node, value) ((node)->data.svalue = (value))
#define SET_DATA_FLOAT(node, value) ((node)->data.fvalue = (value))
#define SET_DATA_DOUBLE(node, value) ((node)->data.dvalue = (value))
#define SET_DATA_BOOL(node, value) ((node)->data.bvalue = (value) ? 1 : 0)
#define SET_DATA_NAME(node, n) ((node)->data.name = ARENA_STRDUP(n))
#define SET_SIZEOF(node, n) ((node)->data.sizeof_stmt.expr = (n))
#define SET_DATA_OP(node, l, r, opr)                                           \
    do                                                                         \
    {                                                                          \
        (node)->data.op.left = (l);                                            \
        (node)->data.op.right = (r);                                           \
        (node)->data.op.op = (opr);                                            \
    } while (0)

#define SET_DATA_UNARY_OP(node, o, opr)                                        \
    do                                                                         \
    {                                                                          \
        (node)->data.unary.operand = (o);                                      \
        (node)->data.unary.op = (opr);                                         \
    } while (0)

#define SET_DATA_FOR(node, i, c, inc, b)                                       \
    do                                                                         \
    {                                                                          \
        (node)->data.for_stmt.init = (i);                                      \
        (node)->data.for_stmt.cond = (c);                                      \
        (node)->data.for_stmt.incr = (inc);                                    \
        (node)->data.for_stmt.body = (b);                                      \
    } while (0)

#define SET_DATA_WHILE(node, c, b)                                             \
    do                                                                         \
    {                                                                          \
        (node)->data.while_stmt.cond = (c);                                    \
        (node)->data.while_stmt.body = (b);                                    \
    } while (0)

#define SET_DATA_FUNC_CALL(node, func_name, args)                              \
    do                                                                         \
    {                                                                          \
        (node)->data.func_call.function_name = ARENA_STRDUP(func_name);        \
        (node)->data.func_call.arguments = (args);                             \
    } while (0)

/* Macros for handling jump buffer */
#define PUSH_JUMP_BUFFER()                                                     \
    do                                                                         \
    {                                                                          \
        JumpBuffer *jb = SAFE_MALLOC(JumpBuffer);                              \
        jb->next = jump_buffer;                                                \
        jump_buffer = jb;                                                      \
    } while (0)

#define POP_JUMP_BUFFER()                                                      \
    do                                                                         \
    {                                                                          \
        JumpBuffer *jb = jump_buffer;                                          \
        jump_buffer = jump_buffer->next;                                       \
        SAFE_FREE(jb);                                                         \
    } while (0)

#define LONGJMP()                                                              \
    do                                                                         \
    {                                                                          \
        if (jump_buffer != NULL)                                               \
        {                                                                      \
            longjmp(jump_buffer->data, 1);                                     \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            yyerror("No jump buffer available");                               \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define CURRENT_JUMP_BUFFER() (jump_buffer->data)

#define CLEAN_JUMP_BUFFER()                                                    \
    do                                                                         \
    {                                                                          \
        while (jump_buffer)                                                    \
        {                                                                      \
            POP_JUMP_BUFFER();                                                 \
        }                                                                      \
    } while (0)

#define VART_TO_NODET(var_type)                                                \
    ((var_type) == VAR_INT      ? NODE_INT                                     \
     : (var_type) == VAR_SHORT  ? NODE_SHORT                                   \
     : (var_type) == VAR_FLOAT  ? NODE_FLOAT                                   \
     : (var_type) == VAR_DOUBLE ? NODE_DOUBLE                                  \
     : (var_type) == VAR_BOOL   ? NODE_BOOLEAN                                 \
     : (var_type) == VAR_CHAR   ? NODE_CHAR                                    \
     : (var_type) == VAR_STRING ? NODE_STRING                                  \
                                : (NodeType)-1)
#endif /* AST_H */
