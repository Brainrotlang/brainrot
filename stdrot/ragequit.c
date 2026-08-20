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

/* exit_code_params/seconds_params below declare a single STDROT_INT
 * parameter. execute_native_call()'s coerce_arg_to_param() (stdrot.c)
 * converts whatever numeric type the call site used (the semantic
 * analyzer's numeric-compatibility group accepts short/float/double/enum
 * for an rizz-typed parameter too -- see check_type_compatibility_ex())
 * to match that declaration *before* this wrapper ever sees it, so
 * args[0] is always STDROT_INT here for any call that passed semantic
 * analysis -- this wrapper doesn't need its own int/short/float/double
 * fan-out. */
static StdrotValue stdrot_ragequit(StdrotValue *args, int argc)
{
    int code = (argc > 0 && args[0].type == STDROT_INT) ? args[0].val.i : 0;
    ragequit(code);
    return (StdrotValue){STDROT_NONE, {0}};
}

static StdrotValue stdrot_chill(StdrotValue *args, int argc)
{
    unsigned int seconds = (argc > 0 && args[0].type == STDROT_INT)
                               ? (unsigned int)args[0].val.i
                               : 0;
    chill(seconds);
    return (StdrotValue){STDROT_NONE, {0}};
}

static const StdrotParam exit_code_params[] = {
    {STDROT_INT, NULL, 0},
};
static const StdrotParam seconds_params[] = {
    {STDROT_INT, NULL, 0},
};

STDROT_EXPORT_SIG("ragequit", stdrot_ragequit,
                  ((StdrotParam){STDROT_NONE, NULL, 0}), exit_code_params, 1, 1,
                  false);
STDROT_EXPORT_SIG("chill", stdrot_chill, ((StdrotParam){STDROT_NONE, NULL, 0}),
                  seconds_params, 1, 1, false);
