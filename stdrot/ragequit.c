/* stdrot/ragequit.c – Process control functions for libstdrot.so */

#include "stdrot_api.h"
#include <stdlib.h>
#include <unistd.h>

/* ragequit: exit with a code.
 * cleanup() is registered via atexit() in main() so it runs automatically. */
void ragequit(int exit_code)
{
    exit(exit_code);
}

/* chill: suspend execution for the given number of seconds */
void chill(unsigned int seconds)
{
    sleep(seconds);
}

static StdrotValue stdrot_ragequit(StdrotValue *args, int argc)
{
    int code = 0;
    if (argc > 0) {
        if (args[0].type == STDROT_INT) code = args[0].val.i;
        else if (args[0].type == STDROT_SHORT) code = args[0].val.s;
    }
    ragequit(code);
    return (StdrotValue){STDROT_NONE, {0}};
}

static StdrotValue stdrot_chill(StdrotValue *args, int argc)
{
    unsigned int seconds = 0;
    if (argc > 0) {
        if (args[0].type == STDROT_INT) seconds = (unsigned int)args[0].val.i;
        else if (args[0].type == STDROT_SHORT) seconds = (unsigned int)args[0].val.s;
    }
    chill(seconds);
    return (StdrotValue){STDROT_NONE, {0}};
}

STDROT_EXPORT("ragequit", stdrot_ragequit);
STDROT_EXPORT("chill", stdrot_chill);
