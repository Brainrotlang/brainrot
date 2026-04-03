/* stdrot/baka.c – Error output function for libstdrot.so
 *
 * Exports v_baka (va_list version).
 * The varargs wrapper baka() lives in stdrot.c (main binary).
 */

#include "stdrot_api.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* baka: print to stderr (no automatic newline, caller provides it) */
void v_baka(const char *fmt, va_list ap)
{
    vfprintf(stderr, fmt, ap);
    fflush(stderr);
}

static void process_baka_format(const char *format, const StdrotValue *args, int arg_count)
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

            while (strchr("-+ #0123456789.*", *format) != NULL) {
                format++;
            }

            if (*format == 'h' || *format == 'l') {
                char first = *format;
                format++;
                if (*format == first) {
                    format++;
                }
            } else if (strchr("jztL", *format) != NULL) {
                format++;
            }

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
                if (arg->type == STDROT_STRING && arg->val.str.data) {
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
    fprintf(stderr, "%s", buffer);
    fflush(stderr);
}

static StdrotValue stdrot_baka(StdrotValue *args, int arg_count)
{
    if (arg_count > 0 && args[0].type == STDROT_STRING && args[0].val.str.data) {
        process_baka_format(args[0].val.str.data, &args[1], arg_count - 1);
    }
    return (StdrotValue){STDROT_NONE, {0}};
}

STDROT_EXPORT("baka", stdrot_baka);
