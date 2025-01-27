%define parse.error verbose
%{
#include "ast.h"
#include "lib/mem.h"
#include "lib/input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>

int yylex(void);
int yylex_destroy(void);
void yyerror(const char *s);
void ragequit(int exit_code);
void yapping(const char* format, ...);
void yappin(const char* format, ...);
void baka(const char* format, ...);
char slorp_char(char chr);
char *slorp_string(char *string);
int slorp_int(int val);
short slorp_short(short val);
float slorp_float(float var);
double slorp_double(double var);
void cleanup();
TypeModifiers get_variable_modifiers(const char* name);
extern TypeModifiers current_modifiers;
extern VarType current_var_type;

extern int yylineno;
extern FILE *yyin;

/* Root of the AST */
ASTNode *root = NULL;
%}

%union {
    int ival;
    short sval;
    float fval;
    double dval;
    char cval;
    char *strval;
    ASTNode *node;
    CaseNode *case_node;
    ArgumentList *args;
    ExpressionList *expr_list;
    Parameter *param;
}

/* Define token types */
%token SKIBIDI RIZZ YAP BAKA MAIN BUSSIN FLEX CAP
%token PLUS MINUS TIMES DIVIDE MOD SEMICOLON COLON COMMA
%token LPAREN RPAREN LBRACE RBRACE
%token LT GT LE GE EQ NE EQUALS AND OR DEC INC
%token BREAK CASE DEADASS CONTINUE DEFAULT DO DOUBLE ELSE ENUM
%token EXTERN CHAD GIGACHAD FOR GOTO IF LONG SMOL SIGNED
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
%type <node> function_def
%type <node> function_def_list
%type <param> param_list params

%start program

/* Define precedence for operators */
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

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
    ;

function_def
    : type IDENTIFIER LPAREN params RPAREN LBRACE statements RBRACE
        { $$ = create_function_def_node($2, $1, $4, $7); SAFE_FREE($2); }
    ;

params
    : param_list
        { $$ = $1; }
    | /* empty */
        { $$ = NULL; }
    ;

param_list
    : type IDENTIFIER
        { $$ = create_parameter($2, $1, NULL); SAFE_FREE($2); }
    | param_list COMMA type IDENTIFIER 
        { $$ = create_parameter($4, $3, $1); SAFE_FREE($4); }
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
    | function_call SEMICOLON
        { $$ = $1; }
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
    ;

declaration:
    optional_modifiers type IDENTIFIER
        {
            $$ = create_declaration_node($3, create_default_node($2));
            SAFE_FREE($3);
        }
    | optional_modifiers type IDENTIFIER EQUALS expression
        {
            $$ = create_declaration_node($3, $5);
            SAFE_FREE($3);
        }
    | optional_modifiers type IDENTIFIER LBRACKET INT_LITERAL RBRACKET
        {
            Variable *var = variable_new($3);
            add_variable_to_scope($3, var);
            if (!set_array_variable($3, $5, get_current_modifiers(), $2)) {
                yyerror("Failed to create array");
                SAFE_FREE($3);
                YYABORT;
            }
            $$ = create_array_declaration_node($3, $5, $2);
            SAFE_FREE($3);
            SAFE_FREE(var);
        }
    | optional_modifiers type IDENTIFIER LBRACKET RBRACKET EQUALS array_init
        {
            Variable *var = variable_new($3);
            add_variable_to_scope($3, var);
            set_array_variable($3, count_expression_list($7), get_current_modifiers(), $2);
            populate_array_variable($3, $7);
            $$ = create_array_declaration_node($3, count_expression_list($7), $2);
            SAFE_FREE($3);
            SAFE_FREE(var);
            free_expression_list($7);
        }
    | optional_modifiers type IDENTIFIER LBRACKET INT_LITERAL RBRACKET EQUALS array_init
        {
            Variable *var = variable_new($3);
            add_variable_to_scope($3, var);
            ASTNode *node = create_array_declaration_node($3, $5, $2);
            set_array_variable($3, $5, get_current_modifiers(), $2);
            if ($8) {
                populate_array_variable($3, $8);
                free_expression_list($8);
            }
            $$ = node;
            SAFE_FREE($3);
            SAFE_FREE(var);
        }
    ;

array_init:
    LBRACE initializer_list RBRACE
        { $$ = $2; }
    ;

initializer_list:
    expression
        { $$ = create_expression_list($1); }
    | initializer_list COMMA expression
        { $$ = append_expression_list($1, $3); }
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
            $$ = create_function_call_node("slorp", create_argument_list($3, NULL)); 
        }
    | IDENTIFIER LPAREN arg_list RPAREN
        { 
            $$ = create_function_call_node($1, $3);
            SAFE_FREE($1);
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
    | BUSSIN LPAREN expression RPAREN
        { $$ = create_return_node($3); }
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
    | STRING_LITERAL     { $$ = create_string_literal_node($1); free($1);}
    ;

identifier:
      IDENTIFIER         
        { 
            $$ = create_identifier_node($1); 
            SAFE_FREE($1);  
        }
    ;

assignment:
      IDENTIFIER EQUALS expression
        { 
            $$ = create_assignment_node($1, $3); 
            SAFE_FREE($1);
        }
    | IDENTIFIER LBRACKET expression RBRACKET EQUALS expression
        {
            ASTNode *access = create_array_access_node($1, $3);
            ASTNode *node = SAFE_MALLOC(ASTNode);
            if (!node) {
                yyerror("Memory allocation failed");
                SAFE_FREE($1);
                exit(EXIT_FAILURE);
            }
            node->type = NODE_ASSIGNMENT;
            node->data.op.left = access;
            node->data.op.right = $6;
            node->data.op.op = OP_ASSIGN;
            $$ = node;
            SAFE_FREE($1);
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
      IDENTIFIER LBRACKET expression RBRACKET
        { 
            $$ = create_array_access_node($1, $3);
            SAFE_FREE($1);
        }
    ;



%%

int main(int argc, char *argv[]) {
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
    current_scope = create_scope(NULL);

    if (yyparse() == 0) {
        execute_statement(root);
    }

    fclose(source);
    free_ast(root);
    free_function_table();
    free_scope(current_scope);
    yylex_destroy();
    
    return 0;
}

void yyerror(const char *s) {
    extern char *yytext;
    fprintf(stderr, "Error: %s at line %d\n", s, yylineno - 1);
}

void ragequit(int exit_code) {
    cleanup();
    exit(exit_code);
}

void chill(unsigned int seconds) {
    sleep(seconds);
}

void yapping(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void yappin(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void baka(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

char slorp_char(char chr) {
    size_t chars_read;
    input_status status;

    status = input_char(&chr);
    if (status == INPUT_SUCCESS)
    {
        return chr;
    }
    else if (status == INPUT_INVALID_LENGTH)
    {
        fprintf(stderr, "Error: Invalid input length.\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        fprintf(stderr, "Error reading char: %d\n", status);
        exit(EXIT_FAILURE);
    }
}

char *slorp_string(char *string) {
    size_t chars_read;
    input_status status;

    status = input_string(string, sizeof(string), &chars_read);
    if (status == INPUT_SUCCESS)
    {
        return string;
    }
    else if (status == INPUT_BUFFER_OVERFLOW)
    {
        fprintf(stderr, "Error: Input exceeded buffer size.\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        fprintf(stderr, "Error reading string: %d\n", status);
        exit(EXIT_FAILURE);
    }
}

int slorp_int(int val) {
    input_status status;

    status = input_int(&val);
    if (status == INPUT_SUCCESS)
    {
        return val;
    }
    else if (status == INPUT_INTEGER_OVERFLOW)
    {
        fprintf(stderr, "Error: Integer value out of range.\n");
        exit(EXIT_FAILURE);
    }
    else if (status == INPUT_CONVERSION_ERROR)
    {
        fprintf(stderr, "Error: Invalid integer format.\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        fprintf(stderr, "Error reading integer: %d\n", status);
        exit(EXIT_FAILURE);
    }
    return 0;
}

short slorp_short(short val) {
    input_status status;

    status = input_short(&val);
    if (status == INPUT_SUCCESS)
    {
        return val;
    }
    else if (status == INPUT_SHORT_OVERFLOW)
    {
        fprintf(stderr, "Error: short value out of range.\n");
        exit(EXIT_FAILURE);
    }
    else if (status == INPUT_CONVERSION_ERROR)
    {
        fprintf(stderr, "Error: short integer format.\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        fprintf(stderr, "Error reading short: %d\n", status);
        exit(EXIT_FAILURE);
    }
    return 0;
}

float slorp_float(float var) {
    input_status status;

    status = input_float(&var);
    if (status == INPUT_SUCCESS)
    {
        return var;
    }
    else if (status == INPUT_FLOAT_OVERFLOW)
    {
        fprintf(stderr, "Error: Double value out of range.\n");
        exit(EXIT_FAILURE);
    }
    else if (status == INPUT_CONVERSION_ERROR)
    {
        fprintf(stderr, "Error: Invalid float format.\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        fprintf(stderr, "Error reading float: %d\n", status);
        exit(EXIT_FAILURE);
    }
}

double slorp_double(double var) {
    input_status status;

    status = input_double(&var);
    if (status == INPUT_SUCCESS)
    {
        return var;
    }
    else if (status == INPUT_DOUBLE_OVERFLOW)
    {
        fprintf(stderr, "Error: Double value out of range.\n");
        exit(EXIT_FAILURE);
    }
    else if (status == INPUT_CONVERSION_ERROR)
    {
        fprintf(stderr, "Error: Invalid double format.\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        fprintf(stderr, "Error reading double: %d\n", status);
        exit(EXIT_FAILURE);
    }
}

void cleanup() {
    // Free the AST
    free_ast(root);
    
    // Free the scope
    free_scope(current_scope);
    
    // Clean up flex's internal state
    yylex_destroy();
}

TypeModifiers get_variable_modifiers(const char* name) {
    TypeModifiers mods = {false, false, false, false};  // Default modifiers
    Variable *var = get_variable(name); 
    if (var != NULL) {
        return var->modifiers;
    }
    return mods;  // Return default modifiers if not found
}
