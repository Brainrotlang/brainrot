/* stdrot.c - Standard Brainrot built-in functions implementation */

#include "stdrot.h"
#include "ast.h"
#include "lib/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* External function declarations */
extern void yyerror(const char *s);
extern void yapping(const char* format, ...);
extern void yappin(const char* format, ...);
extern void baka(const char* format, ...);
extern void ragequit(int exit_code);
extern void chill(unsigned int seconds);
extern char slorp_char(char chr);
extern char *slorp_string(char *string, size_t size);
extern int slorp_int(int val);
extern short slorp_short(short val);
extern float slorp_float(float var);
extern double slorp_double(double var);

extern int evaluate_expression_int(ASTNode *node);
extern float evaluate_expression_float(ASTNode *node);
extern double evaluate_expression_double(ASTNode *node);
extern short evaluate_expression_short(ASTNode *node);
extern bool evaluate_expression_bool(ASTNode *node);
extern bool is_expression(ASTNode *node, VarType type);
extern Variable *get_variable(const char *name);
extern TypeModifiers get_variable_modifiers(const char* name);
extern void *evaluate_multi_array_access(ASTNode *node);
extern bool set_int_variable(const char *name, int value, TypeModifiers mods);
extern bool set_float_variable(const char *name, float value, TypeModifiers mods);
extern bool set_double_variable(const char *name, double value, TypeModifiers mods);
extern bool set_short_variable(const char *name, short value, TypeModifiers mods);

/* Built-in function names */
const char* BUILTIN_FUNCTIONS[] = {
    "yapping",
    "yappin", 
    "baka",
    "ragequit",
    "chill",
    "slorp"
};

const int BUILTIN_FUNCTION_COUNT = sizeof(BUILTIN_FUNCTIONS) / sizeof(BUILTIN_FUNCTIONS[0]);

/* Check if a function name is a built-in function */
bool is_builtin_function(const char *func_name) {
    if (!func_name) return false;
    
    for (int i = 0; i < BUILTIN_FUNCTION_COUNT; i++) {
        if (strcmp(func_name, BUILTIN_FUNCTIONS[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Execute a built-in function call */
void execute_builtin_function(const char *func_name, ArgumentList *args) {
    if (!func_name) return;
    
    if (strcmp(func_name, "yapping") == 0) {
        execute_yapping_call(args);
    } else if (strcmp(func_name, "yappin") == 0) {
        execute_yappin_call(args);
    } else if (strcmp(func_name, "baka") == 0) {
        execute_baka_call(args);
    } else if (strcmp(func_name, "ragequit") == 0) {
        execute_ragequit_call(args);
    } else if (strcmp(func_name, "chill") == 0) {
        execute_chill_call(args);
    } else if (strcmp(func_name, "slorp") == 0) {
        execute_slorp_call(args);
    }
}

/* Built-in function implementations */

void execute_yapping_call(ArgumentList *args) {
    if (!args) {
        yyerror("No arguments provided for yapping function call");
        exit(EXIT_FAILURE);
    }

    ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_STRING_LITERAL) {
        yyerror("First argument to yapping must be a string literal");
        return;
    }

    const char *format = formatNode->data.name; // The format string
    char buffer[1024];                          // Buffer for the final formatted output
    int buffer_offset = 0;

    ArgumentList *cur = args->next;

    while (*format != '\0') {
        if (*format == '%' && cur != NULL) {
            // Start extracting the format specifier
            const char *start = format;
            format++; // Move past '%'

            // Extract until a valid specifier character is found
            while (strchr("diouxXfFeEgGaAcspnb%", *format) == NULL && *format != '\0') {
                format++;
            }

            if (*format == '\0') {
                yyerror("Invalid format specifier");
                exit(EXIT_FAILURE);
            }

            // Copy the format specifier into a temporary buffer
            char specifier[32];
            int length = format - start + 1;
            strncpy(specifier, start, length);
            specifier[length] = '\0';

            // Process the argument based on the format specifier
            ASTNode *expr = cur->expr;
            if (!expr) {
                yyerror("Invalid argument in yapping call");
                exit(EXIT_FAILURE);
            }

            if (*format == 'b') {
                // Handle boolean values
                bool val = evaluate_expression_bool(expr);
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, "%s", val ? "W" : "L");
            } else if (strchr("diouxX", *format)) {
                // Integer or unsigned integer
                volatile bool is_unsigned = (expr->type == NODE_IDENTIFIER &&
                                    get_variable_modifiers(expr->data.name).is_unsigned);
                
                if (is_unsigned) {
                    if (is_expression(expr, VAR_SHORT)) {
                        unsigned short val = evaluate_expression_short(expr);
                        buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                    } else {
                        unsigned int val = (unsigned int)evaluate_expression_int(expr);
                        buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                    }
                } else {
                    if (is_expression(expr, VAR_SHORT)) {
                        short val = evaluate_expression_short(expr);
                        buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                    } else {
                        int val = evaluate_expression_int(expr);
                        buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                    }
                }
            } else if (strchr("fFeEgGa", *format)) {
                // Floating-point numbers
                if (expr->type == NODE_ARRAY_ACCESS) {
                    // Special handling for array access
                    const char *array_name = expr->data.array.name;
                    void *element = evaluate_multi_array_access(expr);
                    if (!element) {
                        yyerror("Invalid array access in floating-point format specifier");
                        exit(EXIT_FAILURE);
                    }

                    Variable *var = get_variable(array_name);
                    if (var != NULL) {
                        if (var->var_type == VAR_FLOAT) {
                            float val = *(float*)element;
                            buffer_offset += snprintf(buffer + buffer_offset,
                                                      sizeof(buffer) - buffer_offset,
                                                      specifier, val);
                        } else if (var->var_type == VAR_DOUBLE) {
                            double val = *(double*)element;
                            buffer_offset += snprintf(buffer + buffer_offset,
                                                      sizeof(buffer) - buffer_offset,
                                                      specifier, val);
                        }
                        break;
                    }
                } else if (is_expression(expr, VAR_FLOAT)) {
                    float val = evaluate_expression_float(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                } else if (is_expression(expr, VAR_DOUBLE)) {
                    double val = evaluate_expression_double(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                } else {
                    yyerror("Invalid argument type for floating-point format specifier");
                    exit(EXIT_FAILURE);
                }
            } else if (*format == 'c') {
                // Character
                int val = evaluate_expression_int(expr);
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
            } else if (*format == 's') {
                // String
                const Variable *var = get_variable(expr->data.name);
                if (var != NULL) {
                    if (!var->is_array && var->var_type != VAR_STRING) {
                        yyerror("Invalid argument type for %s");
                        exit(EXIT_FAILURE);
                    }
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, var->value.array_data);
                } else if (expr->type != NODE_STRING_LITERAL) {
                    yyerror("Invalid argument type for %s");
                    exit(EXIT_FAILURE);
                } else {
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, expr->data.name);
                }
            } else {
                yyerror("Unsupported format specifier");
                exit(EXIT_FAILURE);
            }

            cur = cur->next; // Move to the next argument
            format++;        // Move past the format specifier
        } else {
            // Copy non-format characters to the buffer
            buffer[buffer_offset++] = *format++;
        }

        // Check for buffer overflow
        if (buffer_offset >= (int)sizeof(buffer)) {
            yyerror("Buffer overflow in yapping call");
            exit(EXIT_FAILURE);
        }
    }

    buffer[buffer_offset] = '\0'; // Null-terminate the string

    // Print the final formatted output
    yapping("%s", buffer);
}

void execute_yappin_call(ArgumentList *args) {
    if (!args) {
        yyerror("No arguments provided for yappin function call");
        exit(EXIT_FAILURE);
    }

    ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_STRING_LITERAL) {
        yyerror("First argument to yappin must be a string literal");
        exit(EXIT_FAILURE);
    }

    const char *format = formatNode->data.name; // The format string
    char buffer[1024];                          // Buffer for the final formatted output
    int buffer_offset = 0;

    ArgumentList *cur = args->next;

    while (*format != '\0') {
        if (*format == '%' && cur != NULL) {
            // Start extracting the format specifier
            const char *start = format;
            format++; // Move past '%'

            // Extract until a valid specifier character is found
            while (strchr("diouxXfFeEgGaAcspnb%", *format) == NULL && *format != '\0') {
                format++;
            }

            if (*format == '\0') {
                yyerror("Invalid format specifier");
                exit(EXIT_FAILURE);
            }

            // Copy the format specifier into a temporary buffer
            char specifier[32];
            int length = format - start + 1;
            strncpy(specifier, start, length);
            specifier[length] = '\0';

            // Process the argument based on the format specifier
            ASTNode *expr = cur->expr;
            if (!expr) {
                yyerror("Invalid argument in yappin call");
                exit(EXIT_FAILURE);
            }

            if (*format == 'b') {
                // Handle boolean values
                bool val = evaluate_expression_bool(expr);
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, "%s", val ? "W" : "L");
            } else if (strchr("diouxX", *format)) {
                if (is_expression(expr, VAR_SHORT)) {
                    short val = evaluate_expression_short(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                } else {
                    int val = evaluate_expression_int(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                }
            } else if (strchr("fFeEgGa", *format)) {
                // Handle floating-point numbers
                if (is_expression(expr, VAR_FLOAT)) {
                    float val = evaluate_expression_float(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                } else if (is_expression(expr, VAR_DOUBLE)) {
                    double val = evaluate_expression_double(expr);
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
                } else {
                    yyerror("Invalid argument type for floating-point format specifier");
                    exit(EXIT_FAILURE);
                }
            } else if (*format == 'c') {
                // Handle character values
                int val = evaluate_expression_int(expr);
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, val);
            } else if (*format == 's') {
                const Variable *var = get_variable(expr->data.name);
                if (var != NULL) {
                    if (!var->is_array && var->var_type != VAR_STRING) {
                        yyerror("Invalid argument type for %s");
                        exit(EXIT_FAILURE);
                    }
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, var->value.array_data);
                } else if (expr->type != NODE_STRING_LITERAL) {
                    yyerror("Invalid argument type for %s");
                    exit(EXIT_FAILURE);
                } else {
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, expr->data.name);
                }
            } else {
                yyerror("Unsupported format specifier");
                exit(EXIT_FAILURE);
            }

            cur = cur->next; // Move to the next argument
            format++;        // Move past the format specifier
        } else {
            // Copy non-format characters to the buffer
            buffer[buffer_offset++] = *format++;
        }

        // Check for buffer overflow
        if (buffer_offset >= (int)sizeof(buffer)) {
            yyerror("Buffer overflow in yappin call");
            exit(EXIT_FAILURE);
        }
    }

    buffer[buffer_offset] = '\0'; // Null-terminate the string

    // Print the final formatted output
    yappin("%s", buffer);
}

void execute_baka_call(ArgumentList *args) {
    if (!args) {
        baka("\n");
        return;
    }

    ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_STRING_LITERAL) {
        yyerror("First argument to baka must be a string literal");
        return;
    }

    baka(formatNode->data.name);
}

void execute_ragequit_call(ArgumentList *args) {
    if (!args) {
        yyerror("No arguments provided for ragequit function call");
        exit(EXIT_FAILURE);
    }

    ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_INT) {
        yyerror("First argument to ragequit must be an integer");
        exit(EXIT_FAILURE);
    }

    ragequit(formatNode->data.ivalue);
}

void execute_chill_call(ArgumentList *args) {
    if (!args) {
        yyerror("No arguments provided for chill function call");
        exit(EXIT_FAILURE);
    }

    ASTNode *formatNode = args->expr;
    if (formatNode->type != NODE_INT && !formatNode->modifiers.is_unsigned) {
        yyerror("First argument to chill must be an unsigned integer");
        exit(EXIT_FAILURE);
    }

    chill(formatNode->data.ivalue);
}

void execute_slorp_call(ArgumentList *args) {
    if (!args || args->expr->type != NODE_IDENTIFIER) {
        yyerror("slorp requires a variable identifier");
        return;
    }

    char *name = args->expr->data.name;
    Variable *var = get_variable(name);
    if (!var) {
        yyerror("Undefined variable");
        return;
    }

    switch (var->var_type) {
    case VAR_INT: {
        int val = 0;
        val = slorp_int(val);
        set_int_variable(name, val, var->modifiers);
        break;
    }
    case VAR_FLOAT: {
        float val = 0.0f;
        val = slorp_float(val);
        set_float_variable(name, val, var->modifiers);
        break;
    }
    case VAR_DOUBLE: {
        double val = 0.0;
        val = slorp_double(val);
        set_double_variable(name, val, var->modifiers);
        break;
    }
    case VAR_SHORT: {
        short val = 0;
        val = slorp_short(val);
        set_short_variable(name, val, var->modifiers);
        break;
    }
    case VAR_CHAR: {
        if (var->is_array) {
            char val[var->array_length];
            slorp_string(val, sizeof(val));
            strncpy(var->value.strvalue, val, var->array_length - 1);
            ((char *)var->value.strvalue)[var->array_length - 1] = '\0';
            return;
        }
        char val = 0;
        val = slorp_char(val);
        set_int_variable(name, val, var->modifiers);
        break;
    }
    case VAR_STRING: {
        slorp_string((char *)var->value.strvalue, strlen(var->value.strvalue));
        break;
    }
    default:
        yyerror("Unsupported type for slorp");
    }
}
