/* lib/module_path.h - Module search path for the angle-bracket form of the
 * #cooked directive (#cooked <name>, see lang.l). Distinct from the quoted
 * form (#cooked "path/to/file.brainrot"), which resolves relative to the
 * including file and never consults this search path.
 *
 * Search order, most to least specific:
 *   1. Each directory in $BRAINROT_PATH (colon-separated), in the order
 *      given.
 *   2. Exactly ONE of the following two, decided by which binary is
 *      actually running (see module_path_init) -- never both, so an
 *      install can never shadow a working tree or vice versa regardless of
 *      invocation-time cwd or $PATH:
 *        - the install module directory (module_path.c) if the running
 *          executable's own directory is the install bin directory, or
 *        - "stdrot/" next to the running executable otherwise (an
 *          uninstalled/source build resolves modules from here, matching
 *          the in-tree source layout stdrot/, brainray/, ... described in
 *          docs/ROADMAP.md's Phase 4).
 *
 * "The running executable" is resolved independently of argv[0]: argv[0]
 * for a bare, $PATH-resolved command name (e.g. typing "brainrot" after
 * `make install`) carries no directory component at all, so realpath() on
 * it would resolve against cwd instead of the actual installed binary. See
 * module_path.c's resolve_running_executable() for the platform-specific
 * mechanism this uses instead.
 */

#ifndef MODULE_PATH_H
#define MODULE_PATH_H

/* Called once from main(), before any #cooked <name> directive can be
 * lexed. Determines the running executable's own directory and whether it
 * is the installed one (see the search order above); harmless (search
 * tier 2 is simply unavailable) if the running executable can't be
 * determined on this platform. */
void module_path_init(void);

/* Frees state module_path_init() allocated. Idempotent. */
void module_path_cleanup(void);

/* Resolves `name` to an absolute path for "<name>.brainrot", searching the
 * path described above. Returns a malloc'd absolute path on success
 * (caller frees it), or NULL if no directory in the search path has a
 * matching, regular file. */
char *module_path_resolve_prelude(const char *name);

#endif
