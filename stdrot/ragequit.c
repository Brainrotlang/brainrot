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

/* exit_code_params/seconds_params below declare STDROT_INT, but the
 * semantic analyzer's numeric-compatibility group (see
 * check_type_compatibility_ex()) also accepts VAR_FLOAT/VAR_DOUBLE/VAR_ENUM
 * as an rizz-typed parameter's argument -- ragequit(1.5)/chill(1.5) are
 * statically accepted, so the runtime has to actually convert them rather
 * than silently keeping the argc==0 default of 0, or a truncated exit code
 * / sleep duration disagrees with what the analyzer promised was checked.
 * Enum constants already arrive as STDROT_INT (see
 * ast_expr_to_stdrot_value()'s NODE_IDENTIFIER case), so only float/double
 * need converting here. */
static StdrotValue stdrot_ragequit(StdrotValue *args, int argc)
{
    int code = 0;
    if (argc > 0)
    {
        if (args[0].type == STDROT_INT)
            code = args[0].val.i;
        else if (args[0].type == STDROT_SHORT)
            code = args[0].val.s;
        else if (args[0].type == STDROT_FLOAT)
            code = (int)args[0].val.f;
        else if (args[0].type == STDROT_DOUBLE)
            code = (int)args[0].val.d;
    }
    ragequit(code);
    return (StdrotValue){STDROT_NONE, {0}};
}

static StdrotValue stdrot_chill(StdrotValue *args, int argc)
{
    unsigned int seconds = 0;
    if (argc > 0)
    {
        if (args[0].type == STDROT_INT)
            seconds = (unsigned int)args[0].val.i;
        else if (args[0].type == STDROT_SHORT)
            seconds = (unsigned int)args[0].val.s;
        else if (args[0].type == STDROT_FLOAT)
            seconds = (unsigned int)args[0].val.f;
        else if (args[0].type == STDROT_DOUBLE)
            seconds = (unsigned int)args[0].val.d;
    }
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
