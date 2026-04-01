/* stdrot/yapping.c – Print functions for libstdrot.so
 *
 * Exports v_yapping and v_yappin (va_list versions).
 * The varargs wrappers yapping() / yappin() live in stdrot.c (main binary)
 * as thin stubs that forward to these, avoiding duplicate symbol issues.
 */

#include "stdrot_api.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* yapping: print with trailing newline → stdout */
void v_yapping(const char *fmt, va_list ap)
{
    vprintf(fmt, ap);
    putchar('\n');
    fflush(stdout);
}

/* yappin: print without trailing newline → stdout */
void v_yappin(const char *fmt, va_list ap)
{
    vprintf(fmt, ap);
    fflush(stdout);
}

/* Format string processing for yapping with StdrotValue arguments */
static void process_yapping_format(const char *format, const StdrotValue *args, int arg_count, int add_newline)
{
    char buffer[1024];
    int buffer_offset = 0;
    int arg_idx = 0;

    while (*format != '\0' && buffer_offset < (int)sizeof(buffer) - 1) {
        if (*format == '%' && arg_idx < arg_count) {
            const char *start = format;
            format++;

            if (*format == '%') {
                buffer[buffer_offset++] = '%';
                format++;
                continue;
            }
            
            /* Skip flags, width, precision */
            while (strchr("-+ #0123456789.*", *format) != NULL) {
                format++;
            }

            /* Skip length modifiers (h, hh, l, ll, j, z, t, L) */
            if (*format == 'h' || *format == 'l') {
                char first = *format;
                format++;
                if (*format == first) {
                    format++;
                }
            } else if (strchr("jztL", *format) != NULL) {
                format++;
            }
            
            /* Get the conversion specifier */
            char spec = *format;
            if (spec == '\0') break;
            
            char specifier[32];
            size_t length = (size_t)(format - start + 1);

            if (length >= sizeof(specifier))
                length = sizeof(specifier) - 1;

            memcpy(specifier, start, length);
            specifier[length] = '\0';
            
            const StdrotValue *arg = &args[arg_idx];
            
            if (spec == 'b') {
                bool b = false;
                if (arg->type == STDROT_BOOL) b = arg->val.b;
                else if (arg->type == STDROT_INT) b = (arg->val.i != 0);
                else if (arg->type == STDROT_SHORT) b = (arg->val.s != 0);
                buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, "%s", b ? "W" : "L");
            } else if (strchr("diouxX", spec)) {
                if (arg->type == STDROT_INT) {
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, arg->val.i);
                } else if (arg->type == STDROT_SHORT) {
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, arg->val.s);
                } else if (arg->type == STDROT_BOOL) {
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, (int)arg->val.b);
                }
            } else if (strchr("fFeEgGaA", spec)) {
                if (arg->type == STDROT_FLOAT) {
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, arg->val.f);
                } else if (arg->type == STDROT_DOUBLE) {
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, specifier, arg->val.d);
                }
            } else if (spec == 'c') {
                if (arg->type == STDROT_CHAR) {
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, "%c", arg->val.c);
                } else if (arg->type == STDROT_INT) {
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, "%c", arg->val.i);
                }
            } else if (spec == 's') {
                if (arg->type == STDROT_STRING) {
                    buffer_offset += snprintf(buffer + buffer_offset, sizeof(buffer) - buffer_offset, "%s", arg->val.str.data);
                }
            }
            
            arg_idx++;
            format++;
        } else {
            buffer[buffer_offset++] = *format++;
        }
    }
    
    buffer[buffer_offset] = '\0';
    if (add_newline) {
        printf("%s\n", buffer);
    } else {
        printf("%s", buffer);
    }
    fflush(stdout);
}

/* StdrotValue wrapper for yapping (with format string processing) */
static StdrotValue stdrot_yapping(StdrotValue *args, int arg_count)
{
    if (arg_count > 0 && args[0].type == STDROT_STRING) {
        process_yapping_format(args[0].val.str.data, &args[1], arg_count - 1, 1);
    }
    return (StdrotValue){STDROT_NONE, {0}};
}

/* StdrotValue wrapper for yappin (with format string processing) */
static StdrotValue stdrot_yappin(StdrotValue *args, int arg_count)
{
    if (arg_count > 0 && args[0].type == STDROT_STRING) {
        process_yapping_format(args[0].val.str.data, &args[1], arg_count - 1, 0);
    }
    return (StdrotValue){STDROT_NONE, {0}};
}

STDROT_EXPORT("yapping", stdrot_yapping);
STDROT_EXPORT("yappin", stdrot_yappin);

