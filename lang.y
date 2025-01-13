%define parse.error verbose
%{
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>

int yylex(void);
void yyerror(const char *s);
void ragequit(int exit_code);
void yapping(const char* format, ...);
void yappin(const char* format, ...);
void baka(const char* format, ...);
TypeModifiers get_variable_modifiers(const char* name);
extern TypeModifiers current_modifiers;
extern VarType current_var_type;

/* Fix get_variable function: */
Value get_variable(char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(symbol_table[i].name, name) == 0) {
            if (symbol_table[i].modifiers.is_volatile) {
                asm volatile("" ::: "memory");
            }
            switch (symbol_table[i].var_type) {
                case VAR_INT:
                    return (Value) { .ivalue = symbol_table[i].value.ivalue };
                case VAR_SHORT:
                    return (Value) { .svalue = symbol_table[i].value.svalue };
                case VAR_FLOAT:
                    return (Value) { .fvalue = symbol_table[i].value.fvalue };
                case VAR_DOUBLE:
                    return (Value) { .dvalue = symbol_table[i].value.dvalue };
                case VAR_BOOL:
                    return (Value) { .bvalue = symbol_table[i].value.bvalue };
                case VAR_CHAR:
                    return (Value) { .ivalue = symbol_table[i].value.ivalue };
            }

        }
    }
    yyerror("Undefined variable");
    exit(1);
}

extern int yylineno;

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
}

/* Define token types */
%token SKIBIDI RIZZ YAP BAKA MAIN BUSSIN FLEX CAP
%token PLUS MINUS TIMES DIVIDE MOD SEMICOLON COLON COMMA
%token LPAREN RPAREN LBRACE RBRACE
%token LT GT LE GE EQ NE EQUALS AND OR DEC INC
%token BREAK CASE DEADASS CONTINUE DEFAULT DO DOUBLE ELSE ENUM
%token EXTERN CHAD GIGACHAD FOR GOTO IF LONG SMOL SIGNED
%token SIZEOF STATIC STRUCT SWITCH TYPEDEF UNION UNSIGNED VOID VOLATILE GOON 
%token <strval> IDENTIFIER
%token <ival> INT_LITERAL
%token <sval> SHORT_LITERAL
%token <strval> STRING_LITERAL
%token <cval> CHAR
%token <ival> BOOLEAN
%token <fval> FLOAT_LITERAL
%token <dval> DOUBLE_LITERAL

/* Declare types for non-terminals */
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

program:
    skibidi_function
        { root = $1; }
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
        { $$ = $1; }
    | while_statement
        { $$ = $1; }
    | do_while_statement
        { $$ = $1; }
    | function_call SEMICOLON
        { $$ = $1; }
    | error_statement SEMICOLON
        { $$ = $1; }
    | return_statement SEMICOLON
        { $$ = $1; }
    | if_statement
        { $$ = $1; }
    | switch_statement
        { $$ = $1; }
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

declaration:
    optional_modifiers RIZZ IDENTIFIER
        { 
            current_var_type = VAR_INT;
            $$ = create_assignment_node($3, create_int_node(0)); 
        }
    | optional_modifiers RIZZ IDENTIFIER EQUALS expression
        { 
            current_var_type = VAR_INT;
            $$ = create_assignment_node($3, $5); 
        }
    | optional_modifiers SMOL IDENTIFIER
        { 
            current_var_type = VAR_SHORT;
            $$ = create_assignment_node($3, create_short_node(0)); 
        }
    | optional_modifiers SMOL IDENTIFIER EQUALS expression
        { 
            current_var_type = VAR_SHORT;
            $$ = create_assignment_node($3, $5); 
        }
    | optional_modifiers CHAD IDENTIFIER
        { 
            current_var_type = VAR_FLOAT;
            $$ = create_assignment_node($3, create_float_node(0.0f)); 
        }
    | optional_modifiers CHAD IDENTIFIER EQUALS expression
        { 
            current_var_type = VAR_FLOAT;
            $$ = create_assignment_node($3, $5); 
        }
    | optional_modifiers GIGACHAD IDENTIFIER
        { 
            current_var_type = VAR_DOUBLE;
            $$ = create_assignment_node($3, create_double_node(0.0L)); 
        }
    | optional_modifiers GIGACHAD IDENTIFIER EQUALS expression
        { 
            current_var_type = VAR_DOUBLE;
            $$ = create_assignment_node($3, $5); 
        }
    |  optional_modifiers YAP IDENTIFIER
        { 
            current_var_type = VAR_CHAR;
            $$ = create_assignment_node($3, create_char_node(0)); 
        }
    | optional_modifiers YAP IDENTIFIER EQUALS expression
        { 
            current_var_type = VAR_CHAR;
            $$ = create_assignment_node($3, $5); 
        }
    | optional_modifiers CAP IDENTIFIER
        { 
            current_var_type = VAR_BOOL; 
            $$ = create_assignment_node($3, create_boolean_node(0)); 
        }
    | optional_modifiers CAP IDENTIFIER EQUALS expression
        { 
            current_var_type = VAR_BOOL;  
            $$ = create_assignment_node($3, $5); 
        }
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
    IDENTIFIER LPAREN arg_list RPAREN
      { $$ = create_function_call_node($1, $3); }
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
        { $$ = $2; }
    ;

expression:
      INT_LITERAL
        { $$ = create_int_node($1); }
    | FLOAT_LITERAL
        { $$ = create_float_node($1); } 
    | DOUBLE_LITERAL
        { $$ = create_double_node($1); } 
    | CHAR
        { $$ = create_char_node($1); }
    | SHORT_LITERAL
        { $$ = create_short_node($1); }
    | BOOLEAN
        { $$ = create_boolean_node($1); }
    | IDENTIFIER
        { $$ = create_identifier_node($1); }
    | SIZEOF LPAREN IDENTIFIER RPAREN
        { $$ = create_sizeof_node($3); }
    | IDENTIFIER EQUALS expression
        { $$ = create_assignment_node($1, $3); }
    | expression PLUS expression
        { $$ = create_operation_node(OP_PLUS, $1, $3); }
    | expression MINUS expression
        { $$ = create_operation_node(OP_MINUS, $1, $3); }
    | expression TIMES expression
        { $$ = create_operation_node(OP_TIMES, $1, $3); }
    | expression DIVIDE expression
        { $$ = create_operation_node(OP_DIVIDE, $1, $3); }
    | expression MOD expression
        { $$ = create_operation_node(OP_MOD, $1, $3); }
    | expression LT expression
        { $$ = create_operation_node(OP_LT, $1, $3); }
    | expression GT expression
        { $$ = create_operation_node(OP_GT, $1, $3); }
    | expression LE expression
        { $$ = create_operation_node(OP_LE, $1, $3); }
    | expression GE expression
        { $$ = create_operation_node(OP_GE, $1, $3); }
    | expression EQ expression
        { $$ = create_operation_node(OP_EQ, $1, $3); }
    | expression NE expression
        { $$ = create_operation_node(OP_NE, $1, $3); }
    | expression AND expression
        { $$ = create_operation_node(OP_AND, $1, $3); }
    | expression OR expression
        { $$ = create_operation_node(OP_OR, $1, $3); }
    | MINUS expression %prec UMINUS
        { $$ = create_unary_operation_node(OP_NEG, $2); }
    | LPAREN expression RPAREN
        { $$ = $2; }
    | STRING_LITERAL
        { $$ = create_string_literal_node($1); }
    | expression INC %prec LOWER_THAN_ELSE
          {
              $$ = create_unary_operation_node(OP_POST_INC, $1);  // Post-increment
          }
    | expression DEC %prec LOWER_THAN_ELSE
          {
              $$ = create_unary_operation_node(OP_POST_DEC, $1);  // Post-decrement
          }
    | INC expression %prec LOWER_THAN_ELSE
          {
              $$ = create_unary_operation_node(OP_PRE_INC, $2);   // Pre-increment
          }
    | DEC expression %prec LOWER_THAN_ELSE
          {
              $$ = create_unary_operation_node(OP_PRE_DEC, $2);   // Pre-decrement
          }
    ;


%%

int main(void) {
    if (yyparse() == 0) {
        execute_statement(root);
    }
    return 0;
}

void yyerror(const char *s) {
    extern char *yytext;
    fprintf(stderr, "Error: %s at line %d\n", s, yylineno - 1);
}

void ragequit(int exit_code) {
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

TypeModifiers get_variable_modifiers(const char* name) {
    TypeModifiers mods = {false, false, false};  // Default modifiers
    for (int i = 0; i < var_count; i++) {
        if (strcmp(symbol_table[i].name, name) == 0) {
            return symbol_table[i].modifiers;
        }
    }
    return mods;  // Return default modifiers if not found
}
