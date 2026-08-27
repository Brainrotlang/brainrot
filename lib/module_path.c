/* lib/module_path.c - see module_path.h for the search order this
 * implements.
 *
 * Plain strdup/malloc/free throughout (never SAFE_MALLOC/SAFE_FREE), to
 * match lang.l's own #cooked subsystem: the path this returns is handed
 * straight to that subsystem's realpath()-produced buffers and freed the
 * same way they are, so mixing allocator bookkeeping here would just be
 * confusing, not incorrect.
 */

#include "module_path.h"
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* No configurable --prefix exists yet for `make install` either (it hard-
 * codes /usr/local/bin and /usr/local/lib, see the Makefile's install
 * target) -- this mirrors that same hardcoded prefix rather than inventing
 * configurability nothing else in the build has. */
#define BRAINROT_INSTALL_MODULE_DIR "/usr/local/lib/brainrot"

/* Directory containing the running executable (argv[0]), resolved once by
 * module_path_init(). NULL if argv[0] couldn't be resolved -- search tier
 * 3 (module_path.h) is then simply unavailable, not a fatal error. */
static char *exe_dir = NULL;

void module_path_init(const char *argv0)
{
    free(exe_dir);
    exe_dir = NULL;

    char *resolved = realpath(argv0, NULL);
    if (!resolved)
    {
        return;
    }
    char *dir = dirname(resolved); /* may alias resolved */
    exe_dir = strdup(dir);
    free(resolved);
}

void module_path_cleanup(void)
{
    free(exe_dir);
    exe_dir = NULL;
}

/* Ordered list of directories to search, most to least specific (see
 * module_path.h). Every entry is malloc'd; caller frees each entry and the
 * array itself via free_search_dirs(). Never fails: an unset $BRAINROT_PATH
 * or unresolvable exe_dir just means fewer entries, not an error -- the
 * hardcoded install dir alone guarantees at least one. */
static char **build_search_dirs(int *out_count)
{
    char **dirs = NULL;
    int count = 0;
    int capacity = 0;

#define PUSH_DIR(d)                                                            \
    do                                                                         \
    {                                                                          \
        if (count == capacity)                                                 \
        {                                                                      \
            capacity = capacity ? capacity * 2 : 4;                            \
            char **grown = realloc(dirs, sizeof(char *) * (size_t)capacity);   \
            if (!grown)                                                        \
            {                                                                  \
                fprintf(stderr, "out of memory\n");                            \
                exit(1);                                                       \
            }                                                                  \
            dirs = grown;                                                      \
        }                                                                      \
        char *copy = strdup(d);                                                \
        if (!copy)                                                             \
        {                                                                      \
            fprintf(stderr, "out of memory\n");                                \
            exit(1);                                                           \
        }                                                                      \
        dirs[count++] = copy;                                                  \
    } while (0)

    const char *env = getenv("BRAINROT_PATH");
    if (env && env[0])
    {
        char *env_copy = strdup(env);
        if (!env_copy)
        {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        char *save = NULL;
        for (char *tok = strtok_r(env_copy, ":", &save); tok;
             tok = strtok_r(NULL, ":", &save))
        {
            if (tok[0])
            {
                PUSH_DIR(tok);
            }
        }
        free(env_copy);
    }

    PUSH_DIR(BRAINROT_INSTALL_MODULE_DIR);

    if (exe_dir)
    {
        PUSH_DIR(exe_dir);
    }

#undef PUSH_DIR
    *out_count = count;
    return dirs;
}

static void free_search_dirs(char **dirs, int count)
{
    for (int i = 0; i < count; i++)
    {
        free(dirs[i]);
    }
    free(dirs);
}

char *module_path_resolve_prelude(const char *name)
{
    int count = 0;
    char **dirs = build_search_dirs(&count);

    char *found = NULL;
    for (int i = 0; i < count && !found; i++)
    {
        size_t len =
            strlen(dirs[i]) + 1 + strlen(name) + strlen(".brainrot") + 1;
        char *candidate = malloc(len);
        if (!candidate)
        {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        snprintf(candidate, len, "%s/%s.brainrot", dirs[i], name);

        struct stat st;
        if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode))
        {
            found = realpath(candidate, NULL);
        }
        free(candidate);
    }

    free_search_dirs(dirs, count);
    return found;
}
