%define parse.error verbose
%{
#include "ast.h"
#include "visitor.h"
#include "semantic_analyzer.h"
#include "interpreter.h"
#include "stdrot.h"
#include "lib/mem.h"
#include "lib/string_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

int yylex(void);
int yylex_destroy(void);
void yyerror(const char *s);
void cleanup();
TypeModifiers get_variable_modifiers(const String name);
extern TypeModifiers current_modifiers;
extern VarType current_var_type;

extern int yylineno;
extern FILE *yyin;

/* #cooked (#include) support, implemented in lang.l */
extern char *current_filename;
void cooked_init(const char *initial_filename);
void cooked_cleanup(void);

/* Root of the AST */
ASTNode *root = NULL;
static bool parse_error_already_reported = false;

/* Tag of the struct/union currently being defined, used to reject direct
   self-embedding (e.g. `gang Foo { gang Foo x; };`). Empty when not inside
   a struct_def. Struct/union bodies never nest syntactically, so a single
   slot is sufficient. */
static String current_struct_def_name = {0};

/* struct_def_had_error (declared extern in ast.h, defined in ast.c) is set
   when a struct/union field declaration is invalid (self-embedding by
   value, or an unknown nested type) in a way that made the layout just
   computed for that struct/union meaningless — or, from
   populate_struct_fields() in ast.c, when a nested struct/union field's
   initializer wasn't itself a braced sub-initializer. We don't YYABORT for
   this (see struct_field's comment for why) — parsing finishes normally
   and main() treats it exactly like a hard parse failure afterward. */

/* Global interpreter for cleanup */
static Interpreter *global_interpreter = NULL;
%}


%union {
    int ival;
    short sval;
    float fval;
    double dval;
    char cval;
    String strval;
    ASTNode *node;
    CaseNode *case_node;
    ArgumentList *args;
    ExpressionList *expr_list;
    Parameter *param;
    ArrayDimensions array_dims;
    Array array;
    Declarator declarator;
    EnumConstant *econst;
}

/* Define token types */
%token SKIBIDI RIZZ YAP BAKA MAIN BUSSIN FLEX CAP RANT
%token PLUS MINUS TIMES DIVIDE MOD SEMICOLON COLON COMMA
%token AMPERSAND
%token LPAREN RPAREN LBRACE RBRACE
%token LT GT LE GE EQ NE EQUALS AND OR DEC INC
%token BREAK CASE DEADASS CONTINUE DEFAULT DO DOUBLE ELSE ENUM
%token EXTERN CHAD GIGACHAD FOR GOTO IF LONG SMOL SIGNED LONG_LONG
%token SIZEOF STATIC STRUCT SWITCH TYPEDEF UNION UNSIGNED VOID VOLATILE GOON 
%token LBRACKET RBRACKET
%token <strval> IDENTIFIER
%token <ival> INT_LITERAL
%token <sval> SHORT_LITERAL
%token <strval> STRING_LITERAL
%token <cval> CHAR
%token <ival> BOOLEAN
%token <fval> FLOAT_LITERAL
%token <dval> DOUBLE_LITERAL
%token SLORP
%token DOT

%destructor { SAFE_FREE($$.data); } IDENTIFIER STRING_LITERAL
%destructor { SAFE_FREE($$.name); } declarator

%type <node>  struct_def struct_access
%type <param> struct_field_list struct_field   /* reuse Parameter as field carrier */
%type <ival>  struct_or_union
%type <expr_list> struct_initializer_list struct_initializer_item
%type <node>  enum_def
%type <econst> enum_constant_list enum_constant

/* Declare types for non-terminals */
%type <ival> type
%type <node> program skibidi_function
%type <node> statements statement
%type <node> declaration
%type <node> expression
%type <node> for_statement
%type <node> while_statement
%type <node> do_while_statement
%type <node> function_call
%type <args> arg_list argument_list
%type <node> error_statement
%type <node> return_statement
%type <node> init_expr condition increment
%type <node> if_statement
%type <node> switch_statement break_statement
%type <case_node> case_list case_clause
%type <node> binary_operation unary_operation parentheses
%type <node> array_access
%type <node> assignment
%type <node> literal identifier sizeof_expression
%type <expr_list> array_init initializer_list
%type <expr_list> row_list row
%type <node> function_def
%type <node> function_def_list
%type <param> param_list params
%type <array_dims> dimensions
%type <array_dims> dimensions_or_unsized
%type <array> multi_dimension_access
%type <declarator> declarator
%type <ival> pointer_stars
%type <node> assignment_target

%start program

/* Define precedence for operators */
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%left DOT
%right EQUALS           /* Assignment operator */
%left OR                /* Logical OR */
%left AND               /* Logical AND */
%nonassoc EQ NE         /* Equality operators */
%nonassoc LT GT LE GE DEC INC   /* Relational operators */
%left PLUS MINUS        /* Addition and subtraction */
%left TIMES DIVIDE MOD  /* Multiplication, division, modulo */
%right UMINUS           /* Unary minus */

%%

program
    : function_def_list skibidi_function
        { root = create_statement_list($2, $1); }
    ;

function_def_list
    : /* empty */
        { $$ = NULL; }
    | function_def_list function_def
        { $$ = create_statement_list($2, $1); }
    | function_def_list struct_def
        { $$ = $1; (void)$2; }
    | function_def_list enum_def
        { $$ = $1; (void)$2; }
    ;

struct_or_union
    : STRUCT { $$ = 0; }
    | UNION  { $$ = 1; }
    ;

struct_def
    : struct_or_union IDENTIFIER
        {
            /* Mid-rule: remember the tag being defined so struct_field can
               reject direct self-embedding while the body is parsed. */
            SAFE_FREE(current_struct_def_name.data);
            current_struct_def_name = safe_strdup(&$2);
        }
      LBRACE struct_field_list RBRACE SEMICOLON
        {
            /* Build StructField list from Parameter list */
            StructField *fields = NULL, *tail = NULL;
            Parameter *p = $5;
            while (p) {
                StructField *f = SAFE_MALLOC(StructField);
                f->name          = safe_strdup(&p->name);
                f->type          = p->type;
                f->struct_name   = safe_strdup(&p->struct_name);
                f->enum_name     = safe_strdup(&p->enum_name);
                f->pointer_level = p->pointer_level;
                f->offset        = 0; /* filled by compute_struct_layout */
                f->next          = NULL;
                if (!tail) { fields = tail = f; }
                else        { tail->next = f; tail = f; }
                p = p->next;
            }
            bool is_union = $1 != 0;
            size_t total = is_union ? compute_union_layout(fields)
                                    : compute_struct_layout(fields);
            StructDef *def = SAFE_MALLOC(StructDef);
            def->name       = safe_strdup(&$2);
            def->fields     = fields;
            def->total_size = total;
            def->is_union   = is_union;
            register_struct_def(def);
            $$ = create_struct_def_node($2, fields);
            SAFE_FREE($2);
            SAFE_FREE(current_struct_def_name.data);
            current_struct_def_name = (String){0};
        }
    ;

struct_field_list
    : struct_field
        { $$ = $1; }
    | struct_field_list struct_field
        {
            /* append $2 to the end of $1 */
            Parameter *tail = $1;
            while (tail->next) tail = tail->next;
            tail->next = $2;
            $$ = $1;
        }
    ;

struct_field
    : type declarator SEMICOLON
        {
            /* Round-21 review, finding #3 -- a by-value (pointer_level
               == 0) VAR_VOID field (`skibidi hole;` inside a struct) is
               not a real type: get_type_size_for_descriptor() (ast.c)
               returns 0 for it, and get_struct_field_size()'s own
               defensive "an unexpectedly zero-sized field still needs
               SOME layout size" fallback (there to protect against a
               genuinely-should-have-been-resolved nested struct lookup
               failing) would otherwise silently hand it sizeof(int) of
               storage for a type that was never valid to declare at
               all. A pointer field (`skibidi *p;`, void*) is a real,
               differently-sized type (sizeof(uintptr_t), already
               handled by get_struct_field_size()'s own pointer_level
               check) and is NOT rejected here. Uses the same delayed-
               error mechanism (struct_def_had_error) as the "Unknown
               struct/union type"/"Unknown enum type" checks elsewhere
               in this file, not YYABORT -- see struct_field's sibling
               alternative's own comment for why. */
            if ($1 == VAR_VOID && $2.pointer_level == 0)
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg),
                        "Struct/union field '%s' cannot have type void",
                        $2.name.data);
                yyerror(msg);
                struct_def_had_error = true;
            }
            $$ = create_parameter_ex($2.name, $1, $2.pointer_level, NULL,
                                     (TypeModifiers){0});
            SAFE_FREE($2.name);
        }
    | struct_or_union IDENTIFIER declarator SEMICOLON
        {
            /* A struct/union embedding itself BY VALUE has no finite size
               (offset/self-check is only meaningful here, synchronously
               with layout computation — deferring it to semantic analysis
               would let a forward-referenced field's *layout* be silently
               wrong even once the error is reported). A self-referential
               POINTER field is fine (linked-list pattern) since its size
               (sizeof(uintptr_t)) doesn't depend on the pointee being
               complete yet.

               Note: we deliberately don't YYABORT here. Bison does not run
               destructors for the current rule's RHS on YYABORT, and this
               action owns values that still need the normal cleanup below.
               Instead we record the error and let parsing finish normally;
               main() checks struct_def_had_error after yyparse() and exits
               the same way it does for a hard parse failure. */
            bool is_self = current_struct_def_name.data &&
                          strcmp($2.data, current_struct_def_name.data) == 0;
            if (is_self && $3.pointer_level == 0)
            {
                yyerror("A struct/union cannot contain itself by value "
                       "(use a pointer field instead)");
                struct_def_had_error = true;
            }
            else if (!is_self && !get_struct_def($2))
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg),
                         "Unknown struct/union type '%s'", $2.data);
                yyerror(msg);
                struct_def_had_error = true;
            }
            $$ = create_parameter_ex($3.name, VAR_STRUCT, $3.pointer_level,
                                     NULL, (TypeModifiers){0});
            /* Parameter is arena-allocated like its other String fields
               (see create_parameter_ex's ARENA_STRDUP(name)); use the arena
               here too so this copy is reclaimed in bulk instead of leaking
               (StructField, built from this Parameter below, makes its own
               heap-owned safe_strdup copy that free_struct_registry frees). */
            $$->struct_name = ARENA_STRDUP($2);
            SAFE_FREE($3.name);
            SAFE_FREE($2);
        }
    | ENUM IDENTIFIER declarator SEMICOLON
        {
            /* Nested enum field (e.g. `gang Foo { gyatt Color c; };`) --
               same "must already be defined above" ordering rule as
               struct/union fields. */
            if (!get_enum_def($2))
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg), "Unknown enum type '%s'",
                        $2.data);
                yyerror(msg);
                struct_def_had_error = true;
            }
            $$ = create_parameter_ex($3.name, VAR_ENUM, $3.pointer_level,
                                     NULL, (TypeModifiers){0});
            $$->enum_name = ARENA_STRDUP($2);
            SAFE_FREE($3.name);
            SAFE_FREE($2);
        }
    ;

/* Enum tag defined at top level. Auto-increment and duplicate-name
   checking happen in ast.c, not inline here, same as struct layout. */
enum_def:
    ENUM IDENTIFIER LBRACE enum_constant_list RBRACE SEMICOLON
        {
            EnumDef *def = SAFE_MALLOC(EnumDef);
            def->name = safe_strdup(&$2);
            def->constants = $4;
            def->next_def = NULL;
            if (get_enum_def($2))
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg), "Enum '%s' is already defined",
                        $2.data);
                yyerror(msg);
                struct_def_had_error = true;
            }
            else if (!finalize_enum_constants(def))
            {
                struct_def_had_error = true;
            }
            register_enum_def(def);
            $$ = create_enum_def_node($2);
            SAFE_FREE($2);
        }
    ;

enum_constant_list:
    enum_constant
        { $$ = $1; }
    | enum_constant_list COMMA enum_constant
        {
            EnumConstant *tail = $1;
            while (tail->next) tail = tail->next;
            tail->next = $3;
            $$ = $1;
        }
    ;

enum_constant:
    IDENTIFIER
        {
            EnumConstant *c = SAFE_MALLOC(EnumConstant);
            c->name = safe_strdup(&$1);
            c->value = 0;
            c->has_explicit_value = false;
            c->next = NULL;
            $$ = c;
            SAFE_FREE($1.data);
        }
    | IDENTIFIER EQUALS INT_LITERAL
        {
            EnumConstant *c = SAFE_MALLOC(EnumConstant);
            c->name = safe_strdup(&$1);
            c->value = $3;
            c->has_explicit_value = true;
            c->next = NULL;
            $$ = c;
            SAFE_FREE($1.data);
        }
    | IDENTIFIER EQUALS MINUS INT_LITERAL
        {
            EnumConstant *c = SAFE_MALLOC(EnumConstant);
            c->name = safe_strdup(&$1);
            c->value = -$4;
            c->has_explicit_value = true;
            c->next = NULL;
            $$ = c;
            SAFE_FREE($1.data);
        }
    ;

function_def
    : type declarator LPAREN params RPAREN LBRACE statements RBRACE
        { $$ = create_function_def_node_ex($2.name, $1, $2.pointer_level, $4, $7); SAFE_FREE($2.name); }
    | struct_or_union IDENTIFIER declarator LPAREN params RPAREN LBRACE statements RBRACE
        {
            $$ = create_function_def_node_struct($3.name, $2, $3.pointer_level, $5, $8);
            SAFE_FREE($2);
            SAFE_FREE($3.name);
        }
    | ENUM IDENTIFIER declarator LPAREN params RPAREN LBRACE statements RBRACE
        {
            if (!get_enum_def($2))
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg), "Unknown enum type '%s'",
                        $2.data);
                yyerror(msg);
                struct_def_had_error = true;
            }
            $$ = create_function_def_node_enum($3.name, $2, $3.pointer_level, $5, $8);
            SAFE_FREE($2);
            SAFE_FREE($3.name);
        }
    ;

params
    : param_list
        { $$ = $1; }
    | /* empty */
        { $$ = NULL; }
    ;

param_list
    : optional_modifiers type declarator
        {
            /* Round-21 review, finding #3 -- a named by-value (pointer_
               level == 0) void parameter is nonsense (an empty params
               list, already its own grammar production, is how "no
               parameters" is spelled); a void* parameter (pointer_level
               > 0) is a real, ordinary pointer type and is NOT rejected
               here. */
            if ($2 == VAR_VOID && $3.pointer_level == 0)
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg),
                        "Parameter '%s' cannot have type void", $3.name.data);
                yyerror(msg);
                parse_error_already_reported = true;
                SAFE_FREE($3.name);
                YYABORT;
            }
            $$ = create_parameter_ex($3.name, $2, $3.pointer_level, NULL, get_current_modifiers());
            SAFE_FREE($3.name);
        }
    | param_list COMMA optional_modifiers type declarator
        {
            if ($4 == VAR_VOID && $5.pointer_level == 0)
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg),
                        "Parameter '%s' cannot have type void", $5.name.data);
                yyerror(msg);
                parse_error_already_reported = true;
                SAFE_FREE($5.name);
                YYABORT;
            }
            $$ = create_parameter_ex($5.name, $4, $5.pointer_level, $1, get_current_modifiers());
            SAFE_FREE($5.name);
        }
    | optional_modifiers struct_or_union IDENTIFIER declarator
        {
            $$ = create_parameter_ex($4.name, VAR_STRUCT, $4.pointer_level, NULL, get_current_modifiers());
            $$->struct_name = ARENA_STRDUP($3);
            SAFE_FREE($3);
            SAFE_FREE($4.name);
        }
    | param_list COMMA optional_modifiers struct_or_union IDENTIFIER declarator
        {
            $$ = create_parameter_ex($6.name, VAR_STRUCT, $6.pointer_level, $1, get_current_modifiers());
            $$->struct_name = ARENA_STRDUP($5);
            SAFE_FREE($5);
            SAFE_FREE($6.name);
        }
    | optional_modifiers ENUM IDENTIFIER declarator
        {
            if (!get_enum_def($3))
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg), "Unknown enum type '%s'",
                        $3.data);
                yyerror(msg);
                struct_def_had_error = true;
            }
            $$ = create_parameter_ex($4.name, VAR_ENUM, $4.pointer_level, NULL, get_current_modifiers());
            $$->enum_name = ARENA_STRDUP($3);
            SAFE_FREE($3);
            SAFE_FREE($4.name);
        }
    | param_list COMMA optional_modifiers ENUM IDENTIFIER declarator
        {
            if (!get_enum_def($5))
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg), "Unknown enum type '%s'",
                        $5.data);
                yyerror(msg);
                struct_def_had_error = true;
            }
            $$ = create_parameter_ex($6.name, VAR_ENUM, $6.pointer_level, $1, get_current_modifiers());
            $$->enum_name = ARENA_STRDUP($5);
            SAFE_FREE($5);
            SAFE_FREE($6.name);
        }
    ;

pointer_stars:
      /* empty */
        { $$ = 0; }
    | pointer_stars TIMES
        { $$ = $1 + 1; }
    ;

declarator:
    pointer_stars IDENTIFIER
        {
            $$.name = $2;
            $$.pointer_level = $1;
        }
    ;


skibidi_function:
    SKIBIDI MAIN LBRACE statements RBRACE
        { $$ = $4; }
    ;

statements:
      /* empty */
        { $$ = NULL; }
    | statements statement
        { $$ = create_statement_list($2, $1); }
    ;

statement:
      declaration SEMICOLON
        { $$ = $1; }
    | for_statement
        { $$ = $1;  }
    | while_statement
        { $$ = $1;  }
    | do_while_statement
        { $$ = $1;  }
    | error_statement SEMICOLON
        { $$ = $1; }
    | return_statement SEMICOLON
        { $$ = $1; }
    | if_statement
        { $$ = $1;  }
    | switch_statement
        { $$ = $1;  }
    | break_statement SEMICOLON
        { $$ = $1; }
    | expression SEMICOLON
        { $$ = $1; }
    ;

switch_statement:
    SWITCH LPAREN expression RPAREN LBRACE case_list RBRACE
        { $$ = create_switch_statement_node($3, $6); }
    ;

case_list:
      /* empty */
        { $$ = NULL; }
    | case_list case_clause
        { $$ = append_case_list($1, $2); }
    ;

case_clause:
    CASE expression COLON statements
        { $$ = create_case_node($2, $4); }
    | DEFAULT COLON statements
        { $$ = create_default_case_node($3); }
    ;

break_statement:
    BREAK
        { $$ = create_break_node(); }
    ;  

if_statement:
      IF LPAREN expression RPAREN LBRACE statements RBRACE %prec LOWER_THAN_ELSE
        { $$ = create_if_statement_node($3, $6, NULL); }
    | IF LPAREN expression RPAREN LBRACE statements RBRACE ELSE if_statement %prec ELSE
        { $$ = create_if_statement_node($3, $6, $9); }
    | IF LPAREN expression RPAREN LBRACE statements RBRACE ELSE LBRACE statements RBRACE %prec ELSE
        { $$ = create_if_statement_node($3, $6, $10); }
    ;

type:
    RIZZ        { $$ = VAR_INT; }
    | CHAD      { $$ = VAR_FLOAT; }
    | GIGACHAD  { $$ = VAR_DOUBLE; }
    | SMOL      { $$ = VAR_SHORT; }
    | YAP       { $$ = VAR_CHAR; }
    | CAP       { $$ = VAR_BOOL; }
    | RANT      { $$ = VAR_STRING; }
    | SKIBIDI   { $$ = VAR_VOID; }
    ;

declaration:
    optional_modifiers type declarator
        {
            $$ = create_declaration_node_ex($3.name, create_default_node($2, $3.pointer_level), $3.pointer_level);
            SAFE_FREE($3.name);
        }
    | optional_modifiers type declarator EQUALS expression
        {
            $$ = create_declaration_node_ex($3.name, $5, $3.pointer_level);
            SAFE_FREE($3.name);
        }
    | optional_modifiers type declarator dimensions
        {
            /* Storage is allocated at runtime by the declaration visitor
               (see interpreter_visit_declaration), not here -- see the
               comment in create_multi_array_declaration_node(). */
            $$ = create_multi_array_declaration_node($3.name, $4.dimensions, $4.num_dimensions, $2);
            $$->pointer_level = $3.pointer_level;
            $$->modifiers = get_current_modifiers();
            SAFE_FREE($3.name);
        }
    | optional_modifiers type declarator dimensions EQUALS array_init
        {
            $$ = create_multi_array_declaration_node($3.name, $4.dimensions, $4.num_dimensions, $2);
            $$->pointer_level = $3.pointer_level;
            $$->modifiers = get_current_modifiers();
            set_declaration_pending_initializer($$, $6);
            SAFE_FREE($3.name);
        }
    | optional_modifiers type declarator dimensions_or_unsized EQUALS array_init
        {
            ArrayDimensions dims = $4;
            if (dims.num_dimensions == 0) {
                size_t n = count_expression_list($6);
                dims.dimensions[0] = (int)n;
                dims.num_dimensions = 1;
            } else if (dims.dimensions[0] == 0 && dims.num_dimensions >= 2) {
                /* Partially unsized: infer first dimension by dividing total inits by product of trailing dims */
                size_t total_inits = count_expression_list($6);
                size_t trailing = 1;
                for (int i = 1; i < dims.num_dimensions; i++) trailing *= (size_t)dims.dimensions[i];
                if (trailing == 0) { yyerror("Invalid array dimensions"); YYABORT; }
                if (total_inits % trailing != 0) { yyerror("Initializer count does not match array dimensions"); YYABORT; }
                size_t first = total_inits / trailing;
                dims.dimensions[0] = (int)first;
            }
            int tmp_dims[MAX_DIMENSIONS];
            for (int i = 0; i < dims.num_dimensions; i++) tmp_dims[i] = dims.dimensions[i];
            $$ = create_multi_array_declaration_node($3.name, tmp_dims, dims.num_dimensions, $2);
            $$->pointer_level = $3.pointer_level;
            $$->modifiers = get_current_modifiers();
            set_declaration_pending_initializer($$, $6);
            SAFE_FREE($3.name);
        }
    | optional_modifiers struct_or_union IDENTIFIER declarator
        {
            /* Variable creation + blob allocation happens at runtime, in
               interpreter_visit_declaration, in whatever scope is current
               at execution time -- see the comment on
               create_multi_array_declaration_node() for why (the exact
               same reasoning applies to struct/union locals). */
            StructDef *def = get_struct_def($3);

            $$ = create_declaration_node_ex($4.name,
                     create_struct_def_node($3, def ? def->fields : NULL),
                     $4.pointer_level);
            $$->var_type = VAR_STRUCT;
            if ($$->data.op.right)
                $$->data.op.right->data.name = ARENA_STRDUP($3);
            SAFE_FREE($3);
            SAFE_FREE($4.name);
        }
    | optional_modifiers struct_or_union IDENTIFIER declarator EQUALS LBRACE struct_initializer_list RBRACE
        {
            StructDef *def = get_struct_def($3);
            /* Shape-only check (bare value vs. `{ ... }` for a nested
               struct/union field); needs no runtime storage, so it can
               still run here at parse time, unlike the actual value
               population which is deferred (see pending_initializer). */
            validate_struct_initializer_shape(def, $7);

            $$ = create_declaration_node_ex($4.name,
                     create_struct_def_node($3, def ? def->fields : NULL),
                     $4.pointer_level);
            $$->var_type = VAR_STRUCT;
            if ($$->data.op.right) {
                $$->data.op.right->data.name = ARENA_STRDUP($3);
                $$->data.op.right->data.struct_def.initializer_count =
                    (int)count_expression_list($7);
            }
            set_declaration_pending_initializer($$, $7);
            SAFE_FREE($3);
            SAFE_FREE($4.name);
        }
    | optional_modifiers struct_or_union IDENTIFIER declarator EQUALS expression
        {
            /* Plain-expression struct initializer: a function call
               returning a struct by value (e.g. `gang Point r =
               make_point(1, 2);`) or another struct variable to copy-init
               from. Evaluated at runtime by interpreter_visit_declaration
               once storage exists -- see struct_init_expr. */
            StructDef *def = get_struct_def($3);

            $$ = create_declaration_node_ex($4.name,
                     create_struct_def_node($3, def ? def->fields : NULL),
                     $4.pointer_level);
            $$->var_type = VAR_STRUCT;
            if ($$->data.op.right)
                $$->data.op.right->data.name = ARENA_STRDUP($3);
            $$->struct_init_expr = $6;
            SAFE_FREE($3);
            SAFE_FREE($4.name);
        }
    | optional_modifiers ENUM IDENTIFIER declarator
        {
            /* Enum variable, e.g. `gyatt Color c;`. Unlike struct/union,
               an enum variable is just a plain int at runtime (no blob),
               so it's routed through the ordinary scalar declaration path
               in interpreter_visit_declaration -- see the VAR_ENUM case
               there. */
            if (!get_enum_def($3))
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg), "Unknown enum type '%s'",
                        $3.data);
                yyerror(msg);
                struct_def_had_error = true;
            }
            $$ = create_declaration_node_ex(
                $4.name, create_default_node(VAR_ENUM, $4.pointer_level),
                $4.pointer_level);
            $$->var_type = VAR_ENUM;
            $$->enum_name = ARENA_STRDUP($3);
            SAFE_FREE($3);
            SAFE_FREE($4.name);
        }
    | optional_modifiers ENUM IDENTIFIER declarator EQUALS expression
        {
            if (!get_enum_def($3))
            {
                char msg[MAX_BUFFER_LEN];
                snprintf(msg, sizeof(msg), "Unknown enum type '%s'",
                        $3.data);
                yyerror(msg);
                struct_def_had_error = true;
            }
            $$ = create_declaration_node_ex($4.name, $6, $4.pointer_level);
            $$->var_type = VAR_ENUM;
            $$->enum_name = ARENA_STRDUP($3);
            SAFE_FREE($3);
            SAFE_FREE($4.name);
        }
    ;

array_init:
    LBRACE initializer_list RBRACE
        { $$ = $2; }
    | LBRACE row_list RBRACE
        { $$ = $2; }
    ;

dimensions:
    LBRACKET INT_LITERAL RBRACKET
        {
            $$.dimensions[0] = $2;
            $$.num_dimensions = 1;
        }
    | dimensions LBRACKET INT_LITERAL RBRACKET
        {
            if ($1.num_dimensions >= MAX_DIMENSIONS) {
                yyerror("Maximum array dimensions exceeded");
                YYABORT;
            }
            $$.dimensions[$1.num_dimensions] = $3;
            $$.num_dimensions = $1.num_dimensions + 1;
        }
    ;

dimensions_or_unsized:
    LBRACKET RBRACKET
        {
            $$.num_dimensions = 0; /* marker for unsized 1D array to infer */
        }
    | LBRACKET RBRACKET dimensions
        {
            /* Partially unsized: infer first dimension later */
            $$.dimensions[0] = 0;
            for (int i = 0; i < $3.num_dimensions; i++) {
                $$.dimensions[i + 1] = $3.dimensions[i];
            }
            $$.num_dimensions = $3.num_dimensions + 1;
        }
    ;

initializer_list:
    expression
        { $$ = create_expression_list($1); }
    | initializer_list COMMA expression
        { $$ = append_expression_list($1, $3); }
    ;

/* Struct/union initializers get their own list nonterminal (rather than
   reusing initializer_list) so a braced item can denote a nested
   struct/union sub-initializer without creating a grammar ambiguity with
   array_init's row_list (2D array literals), which also wraps
   initializer_list in LBRACE...RBRACE. */
struct_initializer_list:
    struct_initializer_item
        { $$ = $1; }
    | struct_initializer_list COMMA struct_initializer_item
        { $$ = append_expression_list_node($1, $3); }
    ;

struct_initializer_item:
    expression
        { $$ = create_expression_list($1); }
    | LBRACE struct_initializer_list RBRACE
        { $$ = create_expression_sublist($2); }
    ;

row_list:
    row
        { $$ = $1; }
    | row_list COMMA row
        {
            ExpressionList *acc = $1;
            ExpressionList *start = $3;
            if (start) {
                ExpressionList *cur = start;
                do {
                    acc = append_expression_list(acc, cur->expr);
                    cur = cur->next;
                } while (cur != start);
                /* Free the temporary 'start' list nodes now that we've copied exprs */
                free_expression_list(start);
            }
            $$ = acc;
        }
    ;

row:
    LBRACE initializer_list RBRACE
        { $$ = $2; }
    ;

optional_modifiers:
      /* empty */
        { /* No action needed */ }
    | optional_modifiers modifier
        { /* No action needed */ }
    ;

modifier:
    VOLATILE
        { current_modifiers.is_volatile = true; }
    | STATIC
        { current_modifiers.is_static = true; }
    | LONG
        { current_modifiers.is_long = true; }
    | LONG_LONG
        { current_modifiers.is_long_long = true; }
    | SIGNED
        { current_modifiers.is_signed = true; }
    | UNSIGNED 
        { current_modifiers.is_unsigned = true; }
    | DEADASS
        { current_modifiers.is_const = true; }
    | CAP
        { current_var_type = VAR_BOOL; } 
    ;

for_statement:
    FLEX LPAREN init_expr SEMICOLON condition SEMICOLON increment RPAREN LBRACE statements RBRACE
        {
            $$ = create_for_statement_node($3, $5, $7, $10);
        }
    ;

while_statement:
    GOON LPAREN expression RPAREN LBRACE statements RBRACE
        {
            $$ = create_while_statement_node($3, $6);
        }
    ;

do_while_statement:
    DO LBRACE statements RBRACE GOON LPAREN expression RPAREN SEMICOLON
        {
            $$ = create_do_while_statement_node($7, $3);
        }


init_expr:
      declaration
        { $$ = $1; }
    | expression
        { $$ = $1; }
    ;

condition:
    expression
        { $$ = $1; }
    ;

increment:
    expression
        { $$ = $1; }
    ;

function_call:
    SLORP LPAREN identifier RPAREN
        {
            $$ = create_function_call_node(
                (String){ .data = "slorp", .len = sizeof("slorp") - 1 },
                create_argument_list($3, NULL)
            );
        }
    | SLORP LPAREN RPAREN
        {
            /* Zero-argument contextual form (issue #229): the semantic
             * analyzer resolves the input type from the surrounding
             * expression context (declaration/assignment/return/typed
             * argument) and rewrites this node's argument list in place
             * -- see propagate_contextual_call_type() in
             * semantic_analyzer.c. */
            $$ = create_function_call_node(
                (String){ .data = "slorp", .len = sizeof("slorp") - 1 },
                NULL
            );
        }
    | IDENTIFIER LPAREN arg_list RPAREN
        { 
            $$ = create_function_call_node($1, $3);
            SAFE_FREE($1.data);
        }
    ;

arg_list
    : /* empty */
      {
        $$ = NULL; /* No arguments */
      }
    | argument_list
      { 
        $$ = $1; 
      }
    ;

argument_list
    : expression
      {
        /*
         * Single-argument list
         * We'll create a linked list (or array) of argument nodes
         */
        $$ = create_argument_list($1, NULL);
      }
    | argument_list COMMA expression
      {
        /*
         * Append the new expression to the existing argument list
         */
        $$ = create_argument_list($3, $1);
      }
    ;

error_statement:
    BAKA LPAREN expression RPAREN
        { $$ = create_error_statement_node($3); }
    ;

return_statement:
    BUSSIN expression
        { $$ = create_return_node($2); }
    ;

expression:
      literal
    | identifier
    | assignment
    | binary_operation
    | unary_operation
    | parentheses
    | array_access
    | sizeof_expression
    | function_call
    | struct_access
    ;

sizeof_expression:
        SIZEOF LPAREN expression RPAREN{ $$ = create_sizeof_node($3); }
    ;
literal:
      INT_LITERAL        { $$ = create_int_node($1); }
    | FLOAT_LITERAL      { $$ = create_float_node($1); }
    | DOUBLE_LITERAL     { $$ = create_double_node($1); }
    | CHAR               { $$ = create_char_node($1); }
    | SHORT_LITERAL      { $$ = create_short_node($1); }
    | BOOLEAN            { $$ = create_boolean_node($1); }
    | STRING_LITERAL     { $$ = create_string_literal_node($1); SAFE_FREE($1.data);}
    ;

identifier:
      IDENTIFIER         
        { 
            $$ = create_identifier_node($1); 
            SAFE_FREE($1);  
        }
    ;

assignment:
      assignment_target EQUALS expression
        { 
            $$ = create_assignment_target_node($1, $3);
        }
    ;

assignment_target:
      identifier
        { $$ = $1; }
    | array_access
        { $$ = $1; }
    | struct_access
            { $$ = $1; }
    | TIMES assignment_target %prec UMINUS
        { $$ = create_unary_operation_node(OP_DEREFERENCE, $2); }
    ;

multi_dimension_access:
    LBRACKET expression RBRACKET
        {
            $$.indices[0] = $2;
            $$.num_dimensions = 1;
        }
    | multi_dimension_access LBRACKET expression RBRACKET
        {
            if ($1.num_dimensions >= MAX_DIMENSIONS) {
                yyerror("Too many array indices");
                YYABORT;
            }
            $$ = $1;
            $$.indices[$$.num_dimensions] = $3;
            $$.num_dimensions++;
        }
    ;

binary_operation:
      expression PLUS expression       { $$ = create_operation_node(OP_PLUS, $1, $3); }
    | expression MINUS expression      { $$ = create_operation_node(OP_MINUS, $1, $3); }
    | expression TIMES expression      { $$ = create_operation_node(OP_TIMES, $1, $3); }
    | expression DIVIDE expression     { $$ = create_operation_node(OP_DIVIDE, $1, $3); }
    | expression MOD expression        { $$ = create_operation_node(OP_MOD, $1, $3); }
    | expression LT expression         { $$ = create_operation_node(OP_LT, $1, $3); }
    | expression GT expression         { $$ = create_operation_node(OP_GT, $1, $3); }
    | expression LE expression         { $$ = create_operation_node(OP_LE, $1, $3); }
    | expression GE expression         { $$ = create_operation_node(OP_GE, $1, $3); }
    | expression EQ expression         { $$ = create_operation_node(OP_EQ, $1, $3); }
    | expression NE expression         { $$ = create_operation_node(OP_NE, $1, $3); }
    | expression AND expression        { $$ = create_operation_node(OP_AND, $1, $3); }
    | expression OR expression         { $$ = create_operation_node(OP_OR, $1, $3); }
    ;

unary_operation:
      MINUS expression %prec UMINUS    { $$ = create_unary_operation_node(OP_NEG, $2); }
    | TIMES expression %prec UMINUS
        { $$ = create_unary_operation_node(OP_DEREFERENCE, $2); }
    | AMPERSAND expression %prec UMINUS
        { $$ = create_unary_operation_node(OP_ADDRESS_OF, $2); }
    | INC expression %prec LOWER_THAN_ELSE
        { $$ = create_unary_operation_node(OP_PRE_INC, $2); }
    | DEC expression %prec LOWER_THAN_ELSE
        { $$ = create_unary_operation_node(OP_PRE_DEC, $2); }
    | expression INC %prec LOWER_THAN_ELSE
        { $$ = create_unary_operation_node(OP_POST_INC, $1); }
    | expression DEC %prec LOWER_THAN_ELSE
        { $$ = create_unary_operation_node(OP_POST_DEC, $1); }
    ;

parentheses:
      LPAREN expression RPAREN         { $$ = $2; }
    ;

array_access:
    IDENTIFIER multi_dimension_access
        {
            ASTNode *node = create_multi_array_access_node($1, $2.indices, $2.num_dimensions);
            SAFE_FREE($1);
            $$ = node;
        }
    ;

struct_access:
    expression DOT IDENTIFIER
        {
            $$ = create_struct_access_node($1, $3);
            SAFE_FREE($3);
        }
    ;
%%

int main(int argc, char *argv[]) {
    /* Register cleanup function to be called on exit */
    atexit(cleanup);
    atexit(stdrot_unload);
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <sourcefile>\n", argv[0]);
        return 1;
    }

    FILE *source = fopen(argv[1], "r");
    if (!source) {
        perror("Cannot open source file");
        return 1;
    }

    yyin = source;
    cooked_init(argv[1]);
    current_scope = create_scope(NULL);

    /* Phase 0: Load standard library (needed for semantic analysis) */
    stdrot_load();

    /* Phase 1: Parse the source code to build AST */
    if (yyparse() != 0 || struct_def_had_error) {
        if (!parse_error_already_reported) {
            fprintf(stderr, "Parsing failed\n");
        }
        return 1;
    }

    /* Phase 2: Semantic Analysis and Type Checking */
    if (!semantic_analyze(root)) {
        return 1;
    }

    /* Phase 3: Execution */
    global_interpreter = interpreter_new();
    if (!global_interpreter) {
        fprintf(stderr, "Failed to create interpreter\n");
        return 1;
    }

    interpret(root, global_interpreter);
    interpreter_free(global_interpreter);
    global_interpreter = NULL;

    /* Note: cleanup and stdrot_unload are called via atexit */
    
    return 0;
}

void yyerror(const char *s) {
    /* Note: yyerror is called both by bison's own parse-error handling and
     * directly from other code (e.g. ast.c) as a general error reporter, so
     * its message format is a de facto stable interface many existing
     * test_cases/expected_results.json entries depend on verbatim — not
     * safe to change without touching every call site. current_filename is
     * available (see cooked_init/cooked_cleanup in lang.l) for a future,
     * more surgical pass at multi-file error attribution.
     *
     * Known pre-existing quirk, unrelated to #cooked but made much more
     * likely by it: the `yylineno - 1` below is a heuristic that assumes
     * bison's one-token lookahead has already advanced past the error line,
     * which is wrong for a syntax error on line 1 of *any* file (prints
     * "line 0"). #cooked resets yylineno to 1 for every included file (see
     * handle_cooked_directive in lang.l), so a syntax error on an included
     * file's first line hits this every time. Fixing it properly needs
     * filename+line attribution on ASTNode, which is the same follow-up
     * noted above — not fixed here. */
    fprintf(stderr, "Error: %s at line %d\n", s, yylineno - 1);
}

void cleanup() {
    static bool cleaned = false;
    if (cleaned) return;  // Prevent double cleanup
    cleaned = true;
    
    // Free the global interpreter if it exists
    if (global_interpreter) {
        interpreter_free(global_interpreter);
        global_interpreter = NULL;
    }
    
    // Close input file if still open
    if (yyin && yyin != stdin) {
        fclose(yyin);
        yyin = NULL;
    }

    // Close/free any files and strings left by an aborted #cooked include chain
    cooked_cleanup();

    // Free the AST
    free_ast();
    
    // Free the scope
    if (current_scope) {
        free_scope(current_scope);
        current_scope = NULL;
    }

    free_function_table();

    free_static_variable_map();

    free_struct_registry();

    free_enum_registry();

    CLEAN_JUMP_BUFFER();
    
    // Clean up flex's internal state
    yylex_destroy();
}

TypeModifiers get_variable_modifiers(const String name) {
    TypeModifiers mods = {false, false, false, false, false, false, false, false};  // Default modifiers
    Variable *var = get_variable(name); 
    if (var != NULL) {
        return var->modifiers;
    }
    return mods;  // Return default modifiers if not found
}
