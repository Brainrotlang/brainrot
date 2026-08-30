/* stdrot/yapcat.c -- yapcat(rant a, rant b) -> rant, concatenation.
 *
 * Returns a NEW string of length a.len + b.len; neither argument is
 * touched. `yapcat("", x)` and `yapcat(x, "")` are value-equal to x, and
 * freshly allocated rather than aliased -- the immutable/return-new model
 * this string library follows throughout (#251).
 *
 * The declared StdrotParams are enforced by enforce_arg_type() (stdrot.c)
 * before fn() is entered, so there is no argument-type check here.
 *
 * ── Why a file-static scratch buffer, and why that is safe ──────────────
 * This is the only one of the v1 builtins that has to produce storage the
 * caller did not supply, and the ABI leaves exactly one shape for that.
 *
 * A returned STDROT_STRING is MATERIALIZED by the host: finalize_native_
 * string_result() (stdrot.c) deep-copies .val.str.data into independent
 * memory immediately after this function returns and BEFORE releasing any
 * of the call's own argument scratch -- which is precisely what makes
 * returning borrowed or aliased storage safe, and is how slorp returns its
 * own argument's buffer. The host does NOT free what the native returned,
 * so a plain malloc() here would leak once per call.
 *
 * A second copy backs that one up: evaluate_expression_string() (ast.c)
 * safe_strdup()s every string expression, so `rant x = yapcat(...)` stores
 * private memory even if the first copy were removed. Mutation-testing
 * showed that second copy is in fact the load-bearing one for stored
 * results -- see string_stdlib_result_independence.brainrot, which records
 * which mutation flips it and which does not.
 *
 * The arena is not an option either: libstdrot.so links only this
 * directory's own sources plus lib/input.c (see STDROT_SRCS in the
 * Makefile) -- lib/arena.c is host-side and not in this object.
 *
 * So: one buffer, replaced as needed, reused across calls. It is safe
 * because the host's copy happens before anything else can run --
 * including any nested yapcat. Arguments are fully evaluated before the
 * outer call's fn() is entered, so in `yapcat(yapcat(a, b), c)` the inner
 * result is already an independent host-owned copy by the time the outer
 * call reuses this buffer. string_stdlib_result_independence.brainrot
 * pins the surviving-earlier-results property from the language side.
 *
 * The previous buffer is released at the top of each call rather than
 * left to accumulate, so at most one allocation is ever live -- and the
 * last one is released by a library DESTRUCTOR, which is load-bearing
 * rather than tidiness.
 *
 * "Still reachable through a static" is not good enough here, because the
 * static does not outlive the check: stdrot_unload() (registered with
 * atexit() in lang.y) dlclose()s this object, which unmaps `scratch`
 * itself, so by the time LeakSanitizer scans for roots the buffer is
 * genuinely unreachable and is reported as a 1-object direct leak. A
 * destructor is the correct hook precisely because it runs AS PART OF
 * that dlclose, while this code is still mapped -- unlike an atexit()
 * handler registered from here, which could be invoked after its own code
 * was unmapped.
 */
#include "stdrot_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *scratch = NULL;

/* Runs when libstdrot.so is unloaded (stdrot_unload()'s dlclose, or normal
   process teardown), while this object is still mapped. See the file
   comment for why the buffer cannot simply be left reachable. */
__attribute__((destructor)) static void yapcat_release_scratch(void)
{
    free(scratch);
    scratch = NULL;
}

StdrotValue stdrot_yapcat(StdrotValue *args, int argc)
{
    (void)argc;
    const char *ad = args[0].val.str.data;
    const char *bd = args[1].val.str.data;
    /* .data == NULL is representable by the String struct; treat such a
       value as empty rather than pairing a null buffer with a length. */
    size_t alen = ad ? args[0].val.str.len : 0;
    size_t blen = bd ? args[1].val.str.len : 0;

    /* Overflow would wrap the allocation size and then memcpy past it.
       Unreachable with real strings; checked because the consequence is a
       heap overflow rather than a wrong answer. Tested on the sum itself
       -- `alen > SIZE_MAX - blen - 1` reads more naturally but is wrong
       for blen == SIZE_MAX, where its own right-hand side wraps and the
       check passes. The `== SIZE_MAX` arm covers the +1 for the NUL. */
    size_t total = alen + blen;
    if (total < alen || total == (size_t)-1)
    {
        fprintf(stderr, "Error: yapcat: combined length overflows at line %d\n",
                g_exec_context.line_number);
        exit(1);
    }

    free(scratch);
    /* +1 for a NUL: a `rant` is length-prefixed and callers must use .len,
       but everything downstream that hands this to C (yapping's "%s",
       STDROT_CSTRING conversion) is happier with a terminator present,
       and it costs one byte. */
    scratch = malloc(total + 1);
    if (!scratch)
    {
        fprintf(stderr,
                "Error: yapcat: out of memory joining %zu bytes at "
                "line %d\n",
                total, g_exec_context.line_number);
        exit(1);
    }

    if (alen > 0)
        memcpy(scratch, ad, alen);
    if (blen > 0)
        memcpy(scratch + alen, bd, blen);
    scratch[total] = '\0';

    return (StdrotValue){.type = STDROT_STRING,
                         .val = {.str = {.data = scratch, .len = total}}};
}

static const StdrotParam yapcat_params[] = {
    {STDROT_STRING, NULL, 0},
    {STDROT_STRING, NULL, 0},
};
STDROT_EXPORT_SIG("yapcat", stdrot_yapcat,
                  ((StdrotParam){STDROT_STRING, NULL, 0}), yapcat_params, 2, 2,
                  false);
