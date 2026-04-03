/* stdrot.h – Standard Brainrot library interface (main binary side)
 *
 * stdrot.c provides:
 *   • Thin varargs stubs (yapping / yappin / baka) that forward to the .so
 *   • AST bridge functions (execute_*_call) that evaluate arguments and call
 *     the raw implementations in libstdrot.so
 *   • A loader (stdrot_load / stdrot_unload) that uses dlopen/dlsym to wire
 *     the stubs and read the function registry from the .so
 */

#ifndef STDROT_H
#define STDROT_H

#include "ast.h"
#include "stdrot/stdrot_api.h"
#include "lib/string_value.h"
#include <stdbool.h>
#include <stddef.h>

/* ── Loader lifecycle ────────────────────────────────────────────────────── *
 * Call stdrot_load() once at interpreter startup (interpreter_new).
 * Call stdrot_unload() at interpreter shutdown  (interpreter_free).
 */
void stdrot_load(void);
void stdrot_unload(void);

/* ── Runtime query / dispatch ────────────────────────────────────────────── */
bool is_builtin_function(const String func_name);
void execute_builtin_function(const String func_name, ArgumentList *args);
void execute_func_call(const String func_name, ArgumentList *args);

/* ── Stub functions (forward declarations for use by ast.c) ──────────────── */
void yapping(const String format, ...);
void yappin(const String format, ...);
void baka(const String format, ...);
void ragequit(int exit_code);
void chill(unsigned int seconds);
char slorp_char(char chr);
String slorp_string(String string, size_t size);
int slorp_int(int val);
short slorp_short(short val);
float slorp_float(float var);
double slorp_double(double var);

#endif /* STDROT_H */
