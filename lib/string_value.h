#ifndef STRING_VALUE_H
#define STRING_VALUE_H

#include <stddef.h>

#define MAX_BUFFER_LEN 512

typedef struct {
    char *data;
    size_t len;
} String;

#define STRING_LITERAL(s) ((String){ .data = (s), .len = sizeof(s) - 1 })

#endif
