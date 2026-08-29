/* Round-17 review, finding #4: same as bad_api_table_negative_count.c,
 * but the other half of the same table-level hole -- a positive count
 * paired with a NULL functions pointer, which validate_native_registry()
 * (stdrot.c) previously trusted unconditionally before ever indexing
 * into it: `functions[i]` for i in [0, count) would have dereferenced a
 * NULL pointer immediately, crashing the "customs checkpoint" itself
 * instead of rejecting the malformed table it was supposed to be
 * checking. */
#include "stdrot_api.h"

StdrotAPI stdrot_get_api_v3(void)
{
    StdrotAPI api;
    api.functions = NULL;
    api.count = 1;
    return api;
}
