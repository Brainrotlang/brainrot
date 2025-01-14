%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "lang.tab.h"

VarType current_var_type = NONE;

char *unescape_string(const char *src) {
    char *dest = malloc(strlen(src) + 1);
    if (!dest) return NULL;
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
                default: *d++ = *s; break;
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
"giga"           { return LONG; }
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

"🚽"[^
]*      ;
"W"              { yylval.ival = 1; return BOOLEAN; }
"L"              { yylval.ival = 0; return BOOLEAN; }
[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?[fF]? {
    char *endptr;
    if (strchr(yytext, 'f') || strchr(yytext, 'F')) {
        yylval.fval = strtof(yytext, &endptr);
        return FLOAT_LITERAL;
    } else {
        yylval.dval = strtod(yytext, &endptr);
        return DOUBLE_LITERAL;
    }
}
[0-9]+ {
    switch (current_var_type) {
        case VAR_SHORT: yylval.sval = (short)atoi(yytext); return SHORT_LITERAL;
        case VAR_INT:   yylval.ival = atoi(yytext); return INT_LITERAL;
        case VAR_FLOAT: yylval.fval = atof(yytext); return FLOAT_LITERAL;
        case VAR_DOUBLE: yylval.dval = atof(yytext); return DOUBLE_LITERAL;
        default:       yylval.ival = atoi(yytext); return INT_LITERAL;
    }
}
'.' { yylval.ival = yytext[1]; return CHAR; }
[a-zA-Z_][a-zA-Z0-9_]* { yylval.strval = strdup(yytext); return IDENTIFIER; }
"([^\\"]|\\.)*" {
    char *raw = strdup(yytext + 1);
    if (!raw) return -1;
    raw[strlen(raw) - 1] = '\0';
    yylval.strval = unescape_string(raw);
    free(raw);
    return STRING_LITERAL;
}
\'([^\\\']|\\.)\' {
    char c;
    if (yytext[1] == '\\') {
        switch (yytext[2]) {
            case 'n':  c = '\n'; break;
            case 't':  c = '\t'; break;
            case '\\': c = '\\'; break;
            case '\'': c = '\''; break;
            default: c = yytext[2]; break;
        }
    } else {
        c = yytext[1];
    }
    yylval.ival = c;
    return CHAR;
}

[ \t\n]+         ;
.                ;

%%

int yywrap(void) {
    return 1;
}
