/* stdrot/slorp.c – Input (read) functions for libstdrot.so
 *
 * All slorp_* functions call into lib/input.c which is compiled into this
 * .so, so the main binary has zero direct dependency on lib/input.c.
 */

#include "stdrot_api.h"
#include "lib/input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char slorp_char(char chr)
{
    input_status status = input_char(&chr);
    if (status == INPUT_SUCCESS)
        return chr;
    if (status == INPUT_INVALID_LENGTH) {
        fprintf(stderr, "Error: Invalid input length.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Error reading char: %d\n", status);
    exit(EXIT_FAILURE);
}

char *slorp_string(char *string, size_t size)
{
    size_t chars_read;
    input_status status = input_string(string, size, &chars_read);
    if (status == INPUT_SUCCESS)
        return string;
    if (status == INPUT_BUFFER_OVERFLOW) {
        fprintf(stderr, "Error: Input exceeded buffer size.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Error reading string: %d\n", status);
    exit(EXIT_FAILURE);
}

int slorp_int(int val)
{
    input_status status = input_int(&val);
    if (status == INPUT_SUCCESS)
        return val;
    if (status == INPUT_INTEGER_OVERFLOW) {
        fprintf(stderr, "Error: Integer value out of range.\n");
        exit(EXIT_FAILURE);
    }
    if (status == INPUT_CONVERSION_ERROR) {
        fprintf(stderr, "Error: Invalid integer format.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Error reading integer: %d\n", status);
    exit(EXIT_FAILURE);
}

short slorp_short(short val)
{
    input_status status = input_short(&val);
    if (status == INPUT_SUCCESS)
        return val;
    if (status == INPUT_SHORT_OVERFLOW) {
        fprintf(stderr, "Error: short value out of range.\n");
        exit(EXIT_FAILURE);
    }
    if (status == INPUT_CONVERSION_ERROR) {
        fprintf(stderr, "Error: Invalid short integer format.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Error reading short: %d\n", status);
    exit(EXIT_FAILURE);
}

float slorp_float(float var)
{
    input_status status = input_float(&var);
    if (status == INPUT_SUCCESS)
        return var;
    if (status == INPUT_FLOAT_OVERFLOW) {
        fprintf(stderr, "Error: Float value out of range.\n");
        exit(EXIT_FAILURE);
    }
    if (status == INPUT_CONVERSION_ERROR) {
        fprintf(stderr, "Error: Invalid float format.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Error reading float: %d\n", status);
    exit(EXIT_FAILURE);
}

double slorp_double(double var)
{
    input_status status = input_double(&var);
    if (status == INPUT_SUCCESS)
        return var;
    if (status == INPUT_DOUBLE_OVERFLOW) {
        fprintf(stderr, "Error: Double value out of range.\n");
        exit(EXIT_FAILURE);
    }
    if (status == INPUT_CONVERSION_ERROR) {
        fprintf(stderr, "Error: Invalid double format.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Error reading double: %d\n", status);
    exit(EXIT_FAILURE);
}

static StdrotValue stdrot_slorp(StdrotValue *args, int argc)
{
    if (argc <= 0) {
        return (StdrotValue){STDROT_NONE, {0}};
    }

    StdrotValue out = {STDROT_NONE, {0}};
    switch (args[0].type) {
    case STDROT_INT:
        out.type = STDROT_INT;
        out.val.i = slorp_int(args[0].val.i);
        break;
    case STDROT_FLOAT:
        out.type = STDROT_FLOAT;
        out.val.f = slorp_float(args[0].val.f);
        break;
    case STDROT_DOUBLE:
        out.type = STDROT_DOUBLE;
        out.val.d = slorp_double(args[0].val.d);
        break;
    case STDROT_SHORT:
        out.type = STDROT_SHORT;
        out.val.s = slorp_short(args[0].val.s);
        break;
    case STDROT_CHAR:
        out.type = STDROT_CHAR;
        out.val.c = slorp_char(args[0].val.c);
        break;
    case STDROT_STRING:
        if (args[0].val.str.data) {
            size_t size = args[0].val.str.len;
            if (size == 0) size = 1024;
            slorp_string(args[0].val.str.data, size);
            out.type = STDROT_STRING;
            out.val.str = args[0].val.str;
        }
        break;
    default:
        break;
    }
    return out;
}

STDROT_EXPORT("slorp", stdrot_slorp);
