#ifndef STRING_VALUE_H
#define STRING_VALUE_H

#include <stddef.h>

#define MAX_BUFFER_LEN 512

typedef struct {
    char *data;
    size_t len;
} String;

#endif
