%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include "ast.h"
#include "lib/mem.h"
#include "lib/arena.h"
#include "lib/string_value.h"
#include "lang.tab.h"

VarType current_var_type = NONE;

/*
 * #cooked (Brainrot's #include): splices another .brainrot file's tokens
 * into the stream at the point of the directive, via flex's native
 * multi-buffer stack. This subsystem owns its own strings via plain
 * strdup/malloc/free (never safe_malloc/SAFE_FREE) to keep allocator
 * bookkeeping unambiguous.
 */
#define MAX_INCLUDE_DEPTH 32
#define MAX_COOKED_FILES  128

typedef struct {
    FILE *file;
    char *filename;
    int saved_lineno;
} IncludeFrame;

static IncludeFrame include_stack[MAX_INCLUDE_DEPTH];
static int include_stack_top = 0;

static char *visited_paths[MAX_COOKED_FILES];
static int visited_count = 0;

/* Canonical path of whichever file is currently being lexed; used for
 * relative #cooked resolution and error messages. Owned by this subsystem. */
char *current_filename = NULL;

/* Fatal #cooked error exit. exit() skips the normal <<EOF>> unwind that
 * closes one file and pops one flex buffer per open #cooked file, so
 * without this, both the open FILE*s and yypush_buffer_state's internal
 * allocations for every still-open ancestor would never be freed/closed,
 * and LeakSanitizer would flag them.
 *
 * This closes everything itself (innermost file + every ancestor) rather
 * than leaving the innermost file for lang.y's cleanup(): yypop_buffer_state
 * resets the global yyin to whatever file each popped-to buffer belongs to,
 * which after a full unwind is the same FILE* this loop already closed —
 * cleanup()'s own "close yyin if still open" step would then double-close
 * it. Nulling yyin here (both before and after the unwind) makes that step
 * a safe no-op. include_stack's filename strings, current_filename, and
 * visited_paths are still freed afterwards by cooked_cleanup() via
 * atexit(cleanup) as usual. */
static void cooked_die(void) {
    if (yyin) { fclose(yyin); yyin = NULL; }
    while (include_stack_top > 0) {
        include_stack_top--;
        if (include_stack[include_stack_top].file) {
            fclose(include_stack[include_stack_top].file);
        }
        free(include_stack[include_stack_top].filename);
        yypop_buffer_state();
    }
    yyin = NULL; /* undo yypop_buffer_state's restore of an already-closed FILE* */
    exit(1);
}

/* Returns a malloc'd copy of path's basename, for diagnostics: printing the
 * full resolved path would embed a machine-specific absolute directory
 * (this repo can be checked out anywhere), which makes error output
 * non-reproducible across machines/CI. Caller frees the result. */
static char *basename_copy(const char *path) {
    char *copy = strdup(path);
    if (!copy) { fprintf(stderr, "out of memory\n"); exit(1); }
    char *base = basename(copy); /* may alias copy, or point to static storage */
    char *result = strdup(base);
    free(copy);
    if (!result) { fprintf(stderr, "out of memory\n"); exit(1); }
    return result;
}

static char *extract_quoted_filename(const char *text) {
    const char *open_quote = strchr(text, '"');
    if (!open_quote) return NULL;
    const char *close_quote = strchr(open_quote + 1, '"');
    if (!close_quote || close_quote == open_quote + 1) return NULL;
    size_t len = (size_t)(close_quote - open_quote - 1);
    char *name = malloc(len + 1);
    if (!name) { fprintf(stderr, "out of memory\n"); exit(1); }
    memcpy(name, open_quote + 1, len);
    name[len] = '\0';
    return name;
}

/* Resolves `raw` relative to the directory of current_filename (or as-is if
 * absolute), then canonicalizes it. Returns a libc-malloc'd absolute path;
 * exits with a diagnostic on failure. */
static char *resolve_cooked_path(const char *raw) {
    char *joined;
    if (raw[0] == '/') {
        joined = strdup(raw);
        if (!joined) { fprintf(stderr, "out of memory\n"); exit(1); }
    } else {
        char *dir_copy = strdup(current_filename);
        if (!dir_copy) { fprintf(stderr, "out of memory\n"); exit(1); }
        char *dir = dirname(dir_copy); /* may alias dir_copy; read before freeing */
        size_t len = strlen(dir) + 1 + strlen(raw) + 1;
        joined = malloc(len);
        if (!joined) { fprintf(stderr, "out of memory\n"); exit(1); }
        snprintf(joined, len, "%s/%s", dir, raw);
        free(dir_copy);
    }

    char *resolved = realpath(joined, NULL);
    if (!resolved) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: cannot open #cooked file \"%s\": %s\n",
                base, yylineno, raw, strerror(errno));
        free(base);
        free(joined);
        cooked_die();
    }
    free(joined);
    return resolved;
}

static void handle_cooked_directive(const char *text) {
    char *raw_name = extract_quoted_filename(text);
    if (!raw_name) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: malformed #cooked directive "
                "(expected: #cooked \"path/to/file.brainrot\")\n",
                base, yylineno);
        free(base);
        cooked_die();
    }

    char *resolved = resolve_cooked_path(raw_name);
    free(raw_name);

    if (strcmp(current_filename, resolved) == 0) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: #cooked includes itself\n", base, yylineno);
        free(base);
        free(resolved);
        cooked_die();
    }
    for (int i = 0; i < include_stack_top; i++) {
        if (strcmp(include_stack[i].filename, resolved) == 0) {
            fprintf(stderr, "Error: circular #cooked include detected:\n");
            for (int j = 0; j < include_stack_top; j++) {
                char *base = basename_copy(include_stack[j].filename);
                fprintf(stderr, "  %s\n", base);
                free(base);
            }
            char *base = basename_copy(resolved);
            fprintf(stderr, "  %s  <-- already included above\n", base);
            free(base);
            free(resolved);
            cooked_die();
        }
    }
    for (int i = 0; i < visited_count; i++) {
        if (strcmp(visited_paths[i], resolved) == 0) {
            /* Already included elsewhere in this compilation: no-op. */
            free(resolved);
            return;
        }
    }
    if (include_stack_top >= MAX_INCLUDE_DEPTH) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: #cooked nesting exceeds max depth of %d "
                "(possible runaway include chain)\n",
                base, yylineno, MAX_INCLUDE_DEPTH);
        free(base);
        free(resolved);
        cooked_die();
    }
    if (visited_count >= MAX_COOKED_FILES) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: too many distinct #cooked files (max %d)\n",
                base, yylineno, MAX_COOKED_FILES);
        free(base);
        free(resolved);
        cooked_die();
    }

    FILE *inc = fopen(resolved, "r");
    if (!inc) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: cannot open #cooked file \"%s\": %s\n",
                base, yylineno, resolved, strerror(errno));
        free(base);
        free(resolved);
        cooked_die();
    }

    include_stack[include_stack_top].file = yyin;
    include_stack[include_stack_top].filename = current_filename;
    include_stack[include_stack_top].saved_lineno = yylineno;
    include_stack_top++;

    visited_paths[visited_count] = strdup(resolved);
    if (!visited_paths[visited_count]) { fprintf(stderr, "out of memory\n"); exit(1); }
    visited_count++;

    current_filename = resolved; /* ownership transferred */
    yylineno = 1;
    yypush_buffer_state(yy_create_buffer(inc, YY_BUF_SIZE));
}

/* Called once from main() before yyparse(). */
void cooked_init(const char *initial_filename) {
    char *resolved = realpath(initial_filename, NULL);
    if (!resolved) {
        fprintf(stderr, "Error: cannot resolve source file '%s': %s\n",
                initial_filename, strerror(errno));
        exit(1);
    }
    current_filename = resolved;
    include_stack_top = 0;
    visited_count = 0;
}

/* Called once from cleanup(); closes any files left open by an abnormal
 * exit (e.g. a parse error partway through an included file) and frees all
 * strings owned by this subsystem. Does not touch yyin/the innermost file —
 * that remains lang.y's cleanup() responsibility. */
void cooked_cleanup(void) {
    for (int i = 0; i < include_stack_top; i++) {
        if (include_stack[i].file) {
            fclose(include_stack[i].file);
        }
        free(include_stack[i].filename);
    }
    include_stack_top = 0;

    free(current_filename);
    current_filename = NULL;

    for (int i = 0; i < visited_count; i++) {
        free(visited_paths[i]);
    }
    visited_count = 0;
}

static String copy_bytes(const char *src, size_t len) {
    String s;
    s.data = safe_malloc(len + 1);
    if (!s.data) { fprintf(stderr, "out of memory\n"); exit(1); }
    memcpy(s.data, src, len);
    s.data[len] = '\0';
    s.len = len;
    return s;
}

static String unescape_string(const char *src, size_t len) {
    char *dest = safe_malloc(len + 1);
    if (!dest) { fprintf(stderr, "out of memory\n"); exit(1); }

    char *d = dest;
    const char *s = src;
    const char *end = src + len;

    while (s < end) {
        if (*s == '\\') {
            s++;
            if (s >= end) break;
            switch (*s) {
                case 'n':  *d++ = '\n'; break;
                case 't':  *d++ = '\t'; break;
                case '\\': *d++ = '\\'; break;
                case '"':  *d++ = '"';  break;
                case '\'': *d++ = '\''; break;
                case 'r':  *d++ = '\r'; break;
                case '0':  *d++ = '\0'; break;
                default:   *d++ = *s;   break;
            }
            s++;
        } else {
            *d++ = *s++;
        }
    }

    String out;
    out.len = (size_t)(d - dest);
    // removed realloc — incompatible with safe_malloc blocks
    out.data = dest;
    out.data[out.len] = '\0';
    return out;
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
"thicc"          { return LONG_LONG; }
"smol"           { current_var_type = VAR_SHORT; return SMOL; }
"nut"            { return SIGNED; }
"maxxing"        { return SIZEOF; }
"salty"          { return STATIC; }
"gang"           { return STRUCT; }
"."              { return DOT; }
"ohio"           { return SWITCH; }
"chungus"        { return UNION; }
"nonut"          { return UNSIGNED; }
"schizo"         { return VOLATILE; }
"goon"           { return GOON; }
"baka"           { return BAKA; }
"slorp"          { return SLORP; }
"cap"            { current_var_type = VAR_BOOL; return CAP; }
"rant"           { current_var_type = VAR_STRING; return RANT; }

"=="             { return EQ; }
"!="             { return NE; }
"<="             { return LE; }
">="             { return GE; }
"&&"             { return AND; }
"||"             { return OR; }
"&"              { return AMPERSAND; }
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

^[ \t]*"#cooked"[ \t]+\"[^"\n]*\"[ \t]*\n? {
    handle_cooked_directive(yytext);
}
^[ \t]*"#cooked"[^\n]*\n? {
    char *base = basename_copy(current_filename);
    fprintf(stderr, "Error: %s:%d: malformed #cooked directive "
            "(expected: #cooked \"path/to/file.brainrot\")\n",
            base, yylineno);
    free(base);
    cooked_die();
}

<<EOF>> {
    if (yyin) { fclose(yyin); yyin = NULL; }
    if (include_stack_top == 0) {
        yyterminate();
    } else {
        include_stack_top--;
        free(current_filename);
        current_filename = include_stack[include_stack_top].filename;
        yylineno = include_stack[include_stack_top].saved_lineno;
        yypop_buffer_state();
    }
}

"W"              { yylval.ival = 1; return BOOLEAN; }
"L"              { yylval.ival = 0; return BOOLEAN; }
[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?[LlFf]? {
    char *endptr;
    if (strchr(yytext, 'f') || strchr(yytext, 'F')) {
        yylval.fval = strtof(yytext, &endptr);
        return FLOAT_LITERAL;
    } else if (strchr(yytext, 'L') || strchr(yytext, 'l')) {
        yylval.dval = strtod(yytext, &endptr);
        return DOUBLE_LITERAL;
    } else {
        yylval.dval = strtod(yytext, &endptr);
        return DOUBLE_LITERAL;
    }
}
[0-9]+ {
    int next_char = input();    // Peek at the next character
    unput(next_char);           // Put it back into the input stream

    if (next_char == ']') {
        // If the next character is ']', treat this numeric literal as an integer.
        yylval.ival = atoi(yytext);
        return INT_LITERAL;
    }

    // Otherwise, follow the existing type-based logic.
    if (current_var_type == VAR_SHORT) {
        yylval.sval = (short)atoi(yytext);
        return SHORT_LITERAL;
    } else if (current_var_type == VAR_INT || current_var_type == NONE) {
        yylval.ival = atoi(yytext);
        return INT_LITERAL;
    } else {
        // Default behavior for unexpected types
        yylval.ival = atoi(yytext);
        return INT_LITERAL;
    }
}

'.' { yylval.ival = yytext[1]; return CHAR; }
[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.strval = copy_bytes(yytext, (size_t)yyleng);
    return IDENTIFIER;
}
\"([^\\\"]|\\.)*\" {
    size_t raw_len = (size_t)yyleng - 2;   // strip the surrounding quotes
    yylval.strval = unescape_string(yytext + 1, raw_len);
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
