/* Round-17 review, finding #4: validate_native_registry() (stdrot.c)
 * validated individual StdrotEntry fields but never the StdrotAPI table
 * itself -- a negative count would make its loop execute zero times,
 * silently accepting a nonsensical table as if it legitimately exported
 * no natives at all. Deliberately does NOT go through the normal
 * registry.c/linker-section self-registration mechanism -- this directly
 * implements stdrot_get_api_v3() to return a hand-crafted, deliberately
 * invalid StdrotAPI, exactly the kind of "v2-shaped but internally
 * nonsensical" table a hostile or corrupt third-party library could
 * return. */
#include "stdrot_api.h"

StdrotAPI stdrot_get_api_v3(void)
{
    StdrotAPI api;
    api.functions = NULL;
    api.count = -1;
    return api;
}
