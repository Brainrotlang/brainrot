%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "lib/mem.h"
#include "lib/arena.h"
#include "lang.tab.h"

VarType current_var_type = NONE;
bool giga_modifier_set = false;

char *unescape_string(const char *src) {
    // Allocate a buffer big enough for the worst case
    // (same length as src, since we only shrink on escapes)
    char *dest = malloc(strlen(src) + 1);
    char *d = dest;
    const char *s = src;

    while (*s) {
        if (*s == '\\') {
            s++;
            switch (*s) {
                case 'n':  *d++ = '\n'; break;
                case 't':  *d++ = '\t'; break;
                case '\\': *d++ = '\\'; break;
                case '"':  *d++ = '"'; break;
                // ... handle other escapes you care about ...
                default:
                    // If it's an unknown escape like \q,
                    // you might just copy it literally or skip it.
                    *d++ = *s;
                    break;
            }
            s++;
        } else {
            *d++ = *s++;
        }
    }
    *d = '\0';
    return dest;
}


extern int yylineno;
%}

%option yylineno

%%

"skibidi"        { return SKIBIDI; }
"bussin"         { return BUSSIN; }
"flex"           { return FLEX; }
"rizz"           { current_var_type = VAR_INT; return RIZZ; }
"main"           { return MAIN; }
"bruh"           { return BREAK; }
"sigma rule"     { return CASE; }
"yap"            { current_var_type = VAR_CHAR; return YAP; }
"deadass"        { return DEADASS; }
"grind"          { return CONTINUE; }
"based"          { return DEFAULT; }
"mewing"         { return DO; }
"gigachad"       { current_var_type = VAR_DOUBLE; return GIGACHAD; }
"gyatt"          { return ENUM; }
"whopper"        { return EXTERN; }
"chad"           { current_var_type = VAR_FLOAT; return CHAD; }
"cringe"         { return GOTO; }
"edgy"           { return IF; }
"amogus"         { return ELSE; }
"giga"           { giga_modifier_set = true; return LONG; }
"smol"           { current_var_type = VAR_SHORT; return SMOL; }
"nut"            { return SIGNED; }
"maxxing"        { return SIZEOF; }
"salty"          { return STATIC; }
"gang"           { return STRUCT; }
"ohio"           { return SWITCH; }
"chungus"        { return UNION; }
"nonut"          { return UNSIGNED; }
"schizo"         { return VOLATILE; }
"goon"           { return GOON; }
"baka"           { return BAKA; }
"slorp"          { return SLORP; }
"cap"            { current_var_type = VAR_BOOL; return CAP; }

"=="             { return EQ; }
"!="             { return NE; }
"<="             { return LE; }
">="             { return GE; }
"&&"             { return AND; }
"||"             { return OR; }
"<"              { return LT; }
">"              { return GT; }
"="              { return EQUALS; }

"+"              { return PLUS; }
"-"              { return MINUS; }
"*"              { return TIMES; }
"/"              { return DIVIDE; }
"%"              { return MOD; }
"++"             { return INC; }
"--"             { return DEC; }

"("              { return LPAREN; }
")"              { return RPAREN; }
"{"              { return LBRACE; }
"}"              { return RBRACE; }
";"              { return SEMICOLON; }
","              { return COMMA; }
":"              { return COLON; }
"["              { return LBRACKET; }
"]"              { return RBRACKET; }

"🚽"[^\n]*      ; /* Ignore single line comments */
"W"              { yylval.ival = 1; return BOOLEAN; }
"L"              { yylval.ival = 0; return BOOLEAN; }

[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?[LlFf]? {
    // handle giga modifer
    bool is_long = false;
    if (giga_modifier_set)
    {
        is_long = true;
        // giga modifer has been used.
        giga_modifier_set = false;
    }

    char *endptr;  /* Ensure endptr is declared as a pointer */
    int len = yyleng;
    if (len > 0 && (yytext[len-1] == 'f' || yytext[len-1] == 'F')) {
        yylval.fval = strtof(yytext, &endptr);
        return FLOAT_LITERAL;
    } else if (len > 0 && (yytext[len-1] == 'l' || yytext[len-1] == 'L' || is_long)) {
        /* Remove the trailing suffix if it is present */
        yytext[len-1] = '\0';
        yylval.ldval = strtold(yytext, &endptr);
        return LONG_DOUBLE_LITERAL;
    } else {
        yylval.dval = strtod(yytext, &endptr);
        return DOUBLE_LITERAL;
    }
}

[0-9]+([lL])? {
    // handle giga modifer
    bool is_long = false;
    if (giga_modifier_set)
    {
        is_long = true;
        // giga modifer has been used.
        giga_modifier_set = false;
    }

    int len = yyleng;
    if (len > 0 && (yytext[len-1] == 'l' || yytext[len-1] == 'L' || is_long)) {
        /* Remove the suffix before converting */
        yytext[len-1] = '\0';
        yylval.lval = strtol(yytext, NULL, 10);
        return LONG_LITERAL;
    }
    /* Peek at the next character */
    int next_char = input();
    /* Put it back into the input stream */
    unput(next_char);
    if (next_char == ']') {
        yylval.ival = atoi(yytext);
        return INT_LITERAL;
    }
    if (current_var_type == VAR_SHORT) {
        yylval.sval = (short)atoi(yytext);
        return SHORT_LITERAL;
    } else if (current_var_type == VAR_INT || current_var_type == NONE) {
        yylval.ival = atoi(yytext);
        return INT_LITERAL;
    } else {
        yylval.ival = atoi(yytext);
        return INT_LITERAL;
    }
}

'.' { yylval.ival = yytext[1]; return CHAR; }
[a-zA-Z_][a-zA-Z0-9_]* { yylval.strval = safe_strdup(yytext); return IDENTIFIER; }
\"([^\\\"]|\\.)*\" {
    // Strip the leading and trailing quotes:
    char *raw = (yytext + 1);
    raw[strlen(raw) - 1] = '\0';

    // Convert backslash escapes to real characters:
    char *unescaped = unescape_string(raw);

    yylval.strval = unescaped;  // Now it has real newlines, etc.
    return STRING_LITERAL;
}
\'([^\\\']|\\.)\' {
    // The text matched includes the leading and trailing single quotes, e.g. 'c'.
    // We'll decode the actual character inside.
    char c;
    
    if (yytext[1] == '\\') {
        // Handle escape sequences like '\n', '\t', '\\', '\''
        switch (yytext[2]) {
            case 'n':  c = '\n'; break;
            case 't':  c = '\t'; break;
            case '\\': c = '\\'; break;
            case '\'': c = '\''; break;
            // You can add more escapes if you need them
            default:
                // If unrecognized, just use the literal char
                c = yytext[2];
                break;
        }
    } else {
        // Normal single character, e.g. 'c'
        c = yytext[1];
    }
    
    yylval.ival = c;  // Put the character in the parser’s yylval
    return YAP; 
}

[ \t\n]+         ; /* Ignore whitespace */
.                { /* Ignore unrecognized characters */ }

%%

int yywrap(void) {
    return 1;
}
