/* Host ASan/UBSan test for #269: stdrot's print formatters must not overflow
 * their fixed 1024-byte stack buffer when a formatted argument is longer than
 * it fits.
 *
 * `buffer_offset += snprintf(...)` accumulates snprintf's RETURN value (the
 * length it WOULD have written), so a long %s pushed buffer_offset past
 * sizeof(buffer) and the terminating `buffer[buffer_offset] = '\0'` wrote out
 * of bounds -- a stack-buffer overflow.
 *
 * This has to be a host test, not a `.brainrot` fixture:
 *   - the write is identical in stdout either way (fprintf stops at snprintf's
 *     own NUL at index 1023), so a .brainrot fixture's output can't tell fixed
 *     from broken;
 *   - Valgrind does not detect stack-buffer overflows (only ASan's redzones
 *     do); and
 *   - `make test`'s ASan build never sees this write because libstdrot.so is
 *     built without sanitizers.
 * So the sources are #included and compiled WITH -fsanitize=address here, and
 * baka's formatter (static, reachable only via stdrot_baka) comes along too.
 * Before the clamp fix this aborts under ASan; after it, it exits 0.
 */

#include "stdrot_api.h"

#include <stdio.h>
#include <string.h>

/* Unity-include the two implementations under sanitizers. */
#include "baka.c"
#include "yapping.c"

int main(void)
{
    /* A formatted argument well past the 1024-byte internal buffer. */
    static char big[2048];
    memset(big, 'A', 1100);
    big[1100] = '\0';

    StdrotValue sarg;
    memset(&sarg, 0, sizeof(sarg));
    sarg.type = STDROT_STRING;
    sarg.val.str.data = big;
    sarg.val.str.len = 1100;

    FILE *devnull = fopen("/dev/null", "w");
    if (!devnull)
        return 2;

    /* yapping / yappin / yapto path: the public formatter, both with and
       without the trailing newline. */
    stdrot_format_to_stream(devnull, "%s", &sarg, 1, 1);
    stdrot_format_to_stream(devnull, "%s", &sarg, 1, 0);
    /* Overflow reached across several arguments, not just one giant one. */
    StdrotValue three[3] = {sarg, sarg, sarg};
    stdrot_format_to_stream(devnull, "%s%s%s", three, 3, 1);

    /* baka path: its formatter is static, reached through stdrot_baka. It
       writes to stderr, so redirect that to /dev/null to keep the test log
       clean. */
    if (!freopen("/dev/null", "w", stderr))
    {
        fclose(devnull);
        return 2;
    }
    StdrotValue bargs[2];
    memset(bargs, 0, sizeof(bargs));
    bargs[0].type = STDROT_STRING;
    bargs[0].val.str.data = (char *)"%s";
    bargs[0].val.str.len = 2;
    bargs[1] = sarg;
    stdrot_baka(bargs, 2);

    fclose(devnull);
    printf("print overflow check: buffer stayed in bounds\n");
    return 0;
}
