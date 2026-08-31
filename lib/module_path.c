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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h> /* GetModuleFileNameA, GetFileAttributesA */
/* No <libgen.h>/<unistd.h>: dirname(), readlink(), realpath() are all POSIX.
 * The directory split, the running-executable probe, and canonicalization
 * each get a Win32 path below. $BRAINROT_PATH is ';'-separated here, matching
 * the platform's own PATH convention rather than POSIX ':'. */
#define MODULE_PATH_LIST_SEP ";"
#else
#include <unistd.h>
#define MODULE_PATH_LIST_SEP ":"
#endif

/* MODULE_NATIVE_LOADER / MODULE_NATIVE_SUFFIX come from module_path.h -- the
 * single definition shared with stdrot.c and lang.l. */

#if defined(__APPLE__) && defined(__MACH__)
#include <mach-o/dyld.h>
#endif

/* Canonical absolute path of an EXISTING file/dir, malloc'd, or NULL if it
 * doesn't exist or can't be resolved -- the realpath() contract the callers
 * below rely on (a missing path yields NULL, never a made-up string). On
 * Windows realpath() doesn't exist; _fullpath() normalizes but does not
 * require existence, so gate it on GetFileAttributesA to match. */
static char *path_canonical(const char *path)
{
#if defined(_WIN32)
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
    {
        return NULL;
    }
    return _fullpath(NULL, path, 0); /* malloc'd; caller free()s */
#else
    return realpath(path, NULL);
#endif
}

/* No configurable --prefix exists yet for `make install` either (it hard-
 * codes /usr/local/bin and /usr/local/lib, see the Makefile's install
 * target) -- these mirror that same hardcoded prefix rather than inventing
 * configurability nothing else in the build has. */
#define BRAINROT_INSTALL_BIN_DIR "/usr/local/bin"
#define BRAINROT_INSTALL_MODULE_DIR "/usr/local/lib/brainrot"

/* Directory containing the actual running executable, or NULL if it
 * couldn't be determined -- search tier 2 (module_path.h) is then simply
 * unavailable, not a fatal error. */
static char *exe_dir = NULL;

/* Whether exe_dir is the install bin directory -- decides which HALF of
 * search tier 2 applies (module_path.h): never both, so the two can't
 * shadow each other. */
static bool exe_is_installed = false;

/* Returns a malloc'd, canonical absolute path to the running executable,
 * or NULL if it can't be determined on this platform. Deliberately not
 * argv[0]-based: argv[0] is whatever the exec caller chose to pass, which
 * for a bare $PATH-resolved command name (typing "brainrot" after `make
 * install`, rather than "./brainrot" or an absolute path) has no directory
 * component at all -- realpath() on a bare name resolves against cwd, not
 * the $PATH entry the shell actually executed. /proc/self/exe (Linux) and
 * _NSGetExecutablePath (macOS) both name the process's own binary
 * directly, with no argv[0]/cwd ambiguity to get wrong. */
static char *resolve_running_executable(void)
{
#if defined(_WIN32)
    /* GetModuleFileNameA(NULL, ...) names the process's own image directly,
     * the Win32 analogue of /proc/self/exe -- no argv[0]/cwd ambiguity. */
    char buf[4096];
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) /* 0 = error; == size = truncated */
    {
        return NULL;
    }
    return path_canonical(buf);
#elif defined(__APPLE__) && defined(__MACH__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
    {
        return NULL; /* path longer than buf; not worth a heap retry here */
    }
    return path_canonical(buf);
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
    {
        return NULL;
    }
    buf[len] = '\0';
    return path_canonical(buf);
#endif
}

void module_path_init(void)
{
    free(exe_dir);
    exe_dir = NULL;
    exe_is_installed = false;

    char *exe_path = resolve_running_executable();
    if (!exe_path)
    {
        return;
    }
    /* Directory component = everything before the last path separator. Done
     * in-place instead of via POSIX dirname() (libgen.h), which doesn't exist
     * on Windows. exe_path is an absolute, canonical path from
     * resolve_running_executable(), so it always has a separator; '\\' is only
     * checked on Windows, where GetModuleFileNameA yields backslashes. */
    char *sep = strrchr(exe_path, '/');
#if defined(_WIN32)
    char *bslash = strrchr(exe_path, '\\');
    if (bslash && (!sep || bslash > sep))
    {
        sep = bslash;
    }
#endif
    if (sep)
    {
        *sep = '\0';
    }
    exe_dir = strdup(sep ? exe_path : ".");
    free(exe_path);
    if (!exe_dir)
    {
        return;
    }

    /* BRAINROT_TEST_INSTALL_BIN_DIR is a test-only seam (mirrors
     * stdrot.c's STDROT_LIB_PATH): the real install location is a single
     * hardcoded system path nothing in this build can point elsewhere, so
     * a test proving the install-vs-in-tree split actually picks the
     * install side needs a stand-in it can control instead of writing
     * into /usr/local. Unset in normal use, so this changes nothing for
     * anyone not explicitly opting into it. */
    const char *install_bin_dir = getenv("BRAINROT_TEST_INSTALL_BIN_DIR");
    if (!install_bin_dir || !install_bin_dir[0])
    {
        install_bin_dir = BRAINROT_INSTALL_BIN_DIR;
    }

    char *resolved_install_bin_dir = path_canonical(install_bin_dir);
    if (resolved_install_bin_dir)
    {
        exe_is_installed = strcmp(exe_dir, resolved_install_bin_dir) == 0;
        free(resolved_install_bin_dir);
    }
}

void module_path_cleanup(void)
{
    free(exe_dir);
    exe_dir = NULL;
    exe_is_installed = false;
}

/* Ordered list of directories to search, most to least specific (see
 * module_path.h). Every entry is malloc'd; caller frees each entry and the
 * array itself via free_search_dirs(). Never fails: an unset $BRAINROT_PATH
 * or unresolvable exe_dir just means fewer entries, not an error. */
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
        for (char *tok = strtok_r(env_copy, MODULE_PATH_LIST_SEP, &save); tok;
             tok = strtok_r(NULL, MODULE_PATH_LIST_SEP, &save))
        {
            if (tok[0])
            {
                PUSH_DIR(tok);
            }
        }
        free(env_copy);
    }

    if (exe_is_installed)
    {
        /* BRAINROT_TEST_INSTALL_MODULE_DIR mirrors
         * BRAINROT_TEST_INSTALL_BIN_DIR above -- a test proving this
         * branch actually FINDS a module (not just that it correctly
         * skips the in-tree one) needs a stand-in it controls instead of
         * writing into /usr/local/lib/brainrot. */
        const char *install_module_dir =
            getenv("BRAINROT_TEST_INSTALL_MODULE_DIR");
        if (!install_module_dir || !install_module_dir[0])
        {
            install_module_dir = BRAINROT_INSTALL_MODULE_DIR;
        }
        PUSH_DIR(install_module_dir);
    }
    else if (exe_dir)
    {
        size_t len = strlen(exe_dir) + strlen("/stdrot") + 1;
        char *in_tree_dir = malloc(len);
        if (!in_tree_dir)
        {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        snprintf(in_tree_dir, len, "%s/stdrot", exe_dir);
        PUSH_DIR(in_tree_dir);
        free(in_tree_dir);
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

/* Builds "<dir>/<name><suffix>", stats it, and returns a malloc'd realpath
 * if it exists and is a regular file, else NULL. */
static char *resolve_candidate(const char *dir, const char *name,
                               const char *suffix)
{
    size_t len = strlen(dir) + 1 + strlen(name) + strlen(suffix) + 1;
    char *candidate = malloc(len);
    if (!candidate)
    {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    snprintf(candidate, len, "%s/%s%s", dir, name, suffix);

    char *found = NULL;
    struct stat st;
    if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode))
    {
        found = path_canonical(candidate);
    }
    free(candidate);
    return found;
}

char *module_path_resolve(const char *name, ModuleArtifactKind *out_kind)
{
    int count = 0;
    char **dirs = build_search_dirs(&count);

    char *found = NULL;
    for (int i = 0; i < count && !found; i++)
    {
        found = resolve_candidate(dirs[i], name, ".brainrot");
        if (found)
        {
            *out_kind = MODULE_ARTIFACT_PRELUDE;
            break;
        }
#ifdef MODULE_NATIVE_LOADER
        /* A native module -- ".so" on POSIX, ".dll" on Windows. Only where a
         * loader actually exists (MODULE_NATIVE_LOADER, module_path.h); a wasm
         * STDROT_STATIC build skips this entirely rather than advertising a
         * module it could never load. */
        found = resolve_candidate(dirs[i], name, MODULE_NATIVE_SUFFIX);
        if (found)
        {
            *out_kind = MODULE_ARTIFACT_NATIVE;
        }
#endif
    }

    free_search_dirs(dirs, count);
    return found;
}
