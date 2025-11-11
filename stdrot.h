/* stdrot.h - Standard Brainrot built-in functions */

#ifndef STDROT_H
#define STDROT_H

#include "ast.h"
#include <stdbool.h>

/* Built-in function declarations */
void execute_yapping_call(ArgumentList *args);
void execute_yappin_call(ArgumentList *args);
void execute_baka_call(ArgumentList *args);
void execute_ragequit_call(ArgumentList *args);
void execute_chill_call(ArgumentList *args);
void execute_slorp_call(ArgumentList *args);

/* Check if a function name is a built-in function */
bool is_builtin_function(const char *func_name);

/* Execute a built-in function call */
void execute_builtin_function(const char *func_name, ArgumentList *args);

/* Built-in function names */
extern const char* BUILTIN_FUNCTIONS[];
extern const int BUILTIN_FUNCTION_COUNT;

#endif /* STDROT_H */
