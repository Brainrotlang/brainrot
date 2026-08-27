/* lib/module_path.h - Module search path for the angle-bracket form of the
 * #cooked directive (#cooked <name>, see lang.l). Distinct from the quoted
 * form (#cooked "path/to/file.brainrot"), which resolves relative to the
 * including file and never consults this search path.
 *
 * Search order, most to least specific:
 *   1. Each directory in $BRAINROT_PATH (colon-separated), in the order
 *      given.
 *   2. The install module directory (BRAINROT_INSTALL_MODULE_DIR,
 *      module_path.c).
 *   3. The directory containing the running `brainrot` executable itself
 *      (argv[0], see module_path_init) -- lets an uninstalled build resolve
 *      a module sitting next to the binary in the build tree. This can
 *      never shadow a real install: an installed binary's own directory
 *      (e.g. /usr/local/bin) never contains a module artifact, so this
 *      tier simply misses there and changes nothing for an installed
 *      binary. See docs/ROADMAP.md Appendix B, Q11.
 */

#ifndef MODULE_PATH_H
#define MODULE_PATH_H

/* Called once from main(), before any #cooked <name> directive can be
 * lexed, with the program's own argv[0]. Resolves and remembers the
 * executable's directory for search tier 3 above; harmless (that tier is
 * simply unavailable) if argv[0] can't be resolved. */
void module_path_init(const char *argv0);

/* Frees state module_path_init() allocated. Idempotent. */
void module_path_cleanup(void);

/* Resolves `name` to an absolute path for "<name>.brainrot", searching the
 * path described above. Returns a malloc'd absolute path on success
 * (caller frees it), or NULL if no directory in the search path has a
 * matching, regular file. */
char *module_path_resolve_prelude(const char *name);

#endif
