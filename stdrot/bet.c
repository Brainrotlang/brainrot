#include "stdrot_api.h"
#include <stdio.h>
#include <stdlib.h>

// Raw bet function: assert that condition is true
static void bet(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "Error: bet: assertion failed at line %d", g_exec_context.line_number);
        
        if (message) {
            fprintf(stderr, ": %s", message);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
}

// Wrapper for dynamic dispatch
StdrotValue stdrot_bet(StdrotValue *args, int argc) {
    if (argc < 1) {
        fprintf(stderr, "Error: bet: requires at least 1 argument\n");
        exit(1);
    }

    // First argument is the condition
    int condition = 0;
    if (args[0].type == STDROT_INT) {
        condition = args[0].val.i != 0;
    } else if (args[0].type == STDROT_BOOL) {
        condition = args[0].val.b;
    } else if (args[0].type == STDROT_DOUBLE) {
        condition = args[0].val.d != 0.0;
    } else if (args[0].type == STDROT_FLOAT) {
        condition = args[0].val.f != 0.0f;
    }

    // Optional second argument is the message
    const char *message = NULL;
    if (argc > 1 && args[1].type == STDROT_STRING) {
        message = args[1].val.str;
    }

    bet(condition, message);

    // Return empty value (assertion succeeded)
    StdrotValue result = {0};
    result.type = STDROT_INT;
    result.val.i = 0;
    return result;
}

// Register with auto-export macro
STDROT_EXPORT("bet", stdrot_bet);
