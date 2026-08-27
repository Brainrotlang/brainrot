%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include "ast.h"
#include "lib/mem.h"
#include "lib/arena.h"
#include "lib/module_path.h"
#include "lib/string_value.h"
#include "lang.tab.h"
#include "stdrot.h"

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

/* Fatal #cooked error exit. Closes the innermost (currently active) file
 * immediately, since cleanup() won't run until exit()'s atexit chain fires.
 * Everything else — every ancestor FILE*, flex's pushed buffers, and all
 * strings this subsystem owns — is unwound uniformly by cooked_cleanup()
 * (see below), which atexit(cleanup) already calls on every exit path, not
 * just this one. That matters: a plain bison syntax error while a #cooked
 * file is still on the stack (e.g. malformed content in an included file)
 * exits via main()'s ordinary `return 1`, never touching this function at
 * all, and still needs the exact same unwind. */
static void cooked_die(void) {
    if (yyin) { fclose(yyin); yyin = NULL; }
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

/* Extracts the bare name out of the angle-bracket form, #cooked <name> --
 * NULL only for a malformed match (empty <>), which the empty-name check
 * in handle_cooked_module_directive() below reports with a clearer message
 * than "malformed directive" would. */
static char *extract_angle_name(const char *text) {
    const char *open_angle = strchr(text, '<');
    if (!open_angle) return NULL;
    const char *close_angle = strchr(open_angle + 1, '>');
    if (!close_angle) return NULL;
    size_t len = (size_t)(close_angle - open_angle - 1);
    char *name = malloc(len + 1);
    if (!name) { fprintf(stderr, "out of memory\n"); exit(1); }
    memcpy(name, open_angle + 1, len);
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

    /* fopen("r") on a directory succeeds on Linux, then flex's own read
     * fails later with an opaque "input in flex scanner failed" — reject it
     * here with a clear diagnostic instead. */
    struct stat st;
    if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: #cooked \"%s\" is not a regular file\n",
                base, yylineno, raw);
        free(base);
        free(resolved);
        cooked_die();
    }
    return resolved;
}

/* Shared tail of both #cooked forms once each has independently resolved
 * `resolved` to an absolute path -- the quoted form via resolve_cooked_path()
 * (relative to the including file), the angle-bracket form via
 * module_path_resolve_prelude() (via the module search path, module_path.h).
 * From here on the two forms are indistinguishable: self/circular/visited
 * checks, depth limit, and splicing the file in via flex's buffer stack.
 * Takes ownership of `resolved` (frees it on every path, including error
 * paths that call cooked_die()). */
static void splice_cooked_file(char *resolved) {
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
            /* include_stack holds ancestors only — the file that's actually
             * performing this doomed #cooked (current_filename) is the last
             * link in the chain before it cycles back to `resolved`. */
            char *cur_base = basename_copy(current_filename);
            fprintf(stderr, "  %s\n", cur_base);
            free(cur_base);
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

    /* Do the fallible visited-set copy *before* touching include_stack: if
     * it fails, yyin/current_filename must still describe the parent only
     * (not yet duplicated into include_stack), so cooked_die()'s "close
     * yyin" is closing a file cooked_cleanup() doesn't also know about —
     * otherwise this would double-close the same FILE* once here and once
     * more when cooked_cleanup() walks the ancestor it was already
     * recorded as. */
    char *visited_copy = strdup(resolved);
    if (!visited_copy) {
        fclose(inc);
        fprintf(stderr, "out of memory\n");
        free(resolved);
        cooked_die();
    }

    include_stack[include_stack_top].file = yyin;
    include_stack[include_stack_top].filename = current_filename;
    include_stack[include_stack_top].saved_lineno = yylineno;
    include_stack_top++;

    visited_paths[visited_count] = visited_copy;
    visited_count++;

    current_filename = resolved; /* ownership transferred */
    yylineno = 1; /* NOTE: a syntax error on this file's own line 1 will
                     print "line 0" — see the comment on yyerror in lang.y. */
    yypush_buffer_state(yy_create_buffer(inc, YY_BUF_SIZE));
}

static void handle_cooked_directive(const char *text) {
    char *raw_name = extract_quoted_filename(text);
    if (!raw_name) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: malformed #cooked directive "
                "(expected: #cooked \"path/to/file.brainrot\" or "
                "#cooked <module_name>)\n",
                base, yylineno);
        free(base);
        cooked_die();
    }

    char *resolved = resolve_cooked_path(raw_name);
    free(raw_name);
    splice_cooked_file(resolved);
}

/* Whether `resolved` (an absolute path) has already been cooked earlier in
 * this compilation -- shared by splice_cooked_file()'s own visited-path
 * loop (a ".brainrot" file, either quoted-form or angle-bracket-resolved)
 * and handle_cooked_native_module() below (a dlopen'd ".so"): the same
 * include-once behavior applies to both artifact kinds, they just have
 * different things to do on a first visit (splice tokens vs. dlopen). */
static bool already_visited(const char *resolved) {
    for (int i = 0; i < visited_count; i++) {
        if (strcmp(visited_paths[i], resolved) == 0) {
            return true;
        }
    }
    return false;
}

/* Records `resolved` as visited. Callers must already have checked
 * visited_count < MAX_COOKED_FILES (splice_cooked_file() and
 * handle_cooked_native_module() each report that error themselves, with
 * their own file:line context). */
static void mark_visited(const char *resolved) {
    char *copy = strdup(resolved);
    if (!copy) {
        fprintf(stderr, "out of memory\n");
        cooked_die();
    }
    visited_paths[visited_count] = copy;
    visited_count++;
}

/* A `name` resolving to a native ".so" module (module_path.h's
 * MODULE_ARTIFACT_NATIVE): unlike a ".brainrot" prelude, there are no
 * tokens to splice and so no self/circular-include concept (dlopen'ing a
 * .so doesn't re-enter the lexer) -- only include-once dedup applies.
 * Takes ownership of `resolved`. */
static void handle_cooked_native_module(const char *name, char *resolved) {
    if (already_visited(resolved)) {
        /* Already loaded elsewhere in this compilation: no-op, matching
           the ".brainrot" prelude's own include-once behavior. */
        free(resolved);
        return;
    }
    if (visited_count >= MAX_COOKED_FILES) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: too many distinct #cooked "
                "files/modules (max %d)\n", base, yylineno, MAX_COOKED_FILES);
        free(base);
        free(resolved);
        cooked_die();
    }

    stdrot_load_module(name, resolved); /* exits with a diagnostic on failure */
    mark_visited(resolved);
    free(resolved);
}

/* Angle-bracket form: #cooked <name>. Resolves `name` via the module
 * search path (module_path.h) to either a ".brainrot" prelude -- spliced in
 * exactly like the quoted form, same self/circular/visited handling, same
 * include-once behavior -- or a native ".so" module, dlopen'd and
 * registered via handle_cooked_native_module() above. */
static void handle_cooked_module_directive(const char *text) {
    char *name = extract_angle_name(text);
    if (!name) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: malformed #cooked directive "
                "(expected: #cooked \"path/to/file.brainrot\" or "
                "#cooked <module_name>)\n",
                base, yylineno);
        free(base);
        cooked_die();
    }
    if (name[0] == '\0') {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: #cooked <>: module name must not "
                "be empty\n", base, yylineno);
        free(base);
        free(name);
        cooked_die();
    }
    if (strchr(name, '/')) {
        char *base = basename_copy(current_filename);
        fprintf(stderr, "Error: %s:%d: #cooked <%s>: module name must not "
                "contain '/' -- use #cooked \"path\" to include a file by "
                "path\n", base, yylineno, name);
        free(base);
        free(name);
        cooked_die();
    }

    ModuleArtifactKind kind;
    char *resolved = module_path_resolve(name, &kind);
    if (!resolved) {
        char *base = basename_copy(current_filename);
#ifdef STDROT_STATIC
        fprintf(stderr, "Error: %s:%d: cannot find module '%s' (looked "
                "for '%s.brainrot' via $BRAINROT_PATH, then either the "
                "install module directory or a 'stdrot' directory next to "
                "this executable, whichever applies)\n", base, yylineno,
                name, name);
#else
        fprintf(stderr, "Error: %s:%d: cannot find module '%s' (looked "
                "for '%s.brainrot' or '%s.so' via $BRAINROT_PATH, then "
                "either the install module directory or a 'stdrot' "
                "directory next to this executable, whichever applies)\n",
                base, yylineno, name, name, name);
#endif
        free(base);
        free(name);
        cooked_die();
    }

    if (kind == MODULE_ARTIFACT_NATIVE) {
        handle_cooked_native_module(name, resolved);
        free(name);
        return;
    }

    free(name);
    splice_cooked_file(resolved);
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

/* Called once from cleanup(), on every exit path (normal completion, a
 * plain bison syntax error, or a #cooked-specific fatal error via
 * cooked_die()). Closes every ancestor FILE* left open by an abnormal exit
 * (e.g. a parse error partway through an included file) and unwinds every
 * buffer this subsystem pushed via yypush_buffer_state — flex's own
 * yylex_destroy() only frees whichever buffer is current at the time it's
 * called, not the whole push stack, so without this, every ancestor's
 * yy_create_buffer allocation leaks under LeakSanitizer whenever we exit
 * before naturally popping back out through <<EOF>> (e.g. a syntax error
 * inside an included file). Also frees every string this subsystem owns.
 *
 * Does not touch yyin/the innermost file itself — lang.y's cleanup() closes
 * that before calling here. yypop_buffer_state resets the global yyin to
 * whichever file each popped-to buffer belongs to, which after a full
 * unwind is the same FILE* this loop already closed itself (for ancestors)
 * or that cleanup() already closed (for the innermost file) — nulling yyin
 * after the loop prevents either from being double-closed by whatever runs
 * next. */
void cooked_cleanup(void) {
    while (include_stack_top > 0) {
        include_stack_top--;
        if (include_stack[include_stack_top].file) {
            fclose(include_stack[include_stack_top].file);
        }
        free(include_stack[include_stack_top].filename);
        yypop_buffer_state();
    }
    yyin = NULL;

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

"skibidi"        { current_var_type = VAR_VOID; return SKIBIDI; }
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
"lit"            { return TYPEDEF; }
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

^[ \t]*"#cooked"[ \t]+\"[^"\n]*\"[ \t]* {
    /* Deliberately doesn't consume the trailing newline: with %option
     * yylineno, a match containing '\n' bumps yylineno before this action
     * runs, so diagnostics inside handle_cooked_directive would report the
     * *next* line instead of the directive's own line. Leaving '\n'
     * unmatched lets the ordinary whitespace rule below consume it
     * afterward, on its own turn. */
    handle_cooked_directive(yytext);
}
^[ \t]*"#cooked"[ \t]+"<"[^>\n]*">"[ \t]* {
    /* Same yylineno-ordering rationale as the quoted form above -- don't
     * consume the trailing newline here either. */
    handle_cooked_module_directive(yytext);
}
^[ \t]*"#cooked"[^\n]* {
    char *base = basename_copy(current_filename);
    fprintf(stderr, "Error: %s:%d: malformed #cooked directive "
            "(expected: #cooked \"path/to/file.brainrot\" or "
            "#cooked <module_name>)\n",
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
    if (get_type_alias(yylval.strval)) {
        return TYPE_NAME;
    }
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
