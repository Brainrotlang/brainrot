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

/* The two artifact kinds a resolved module name can be -- see
 * module_path_resolve()'s own comment for how the choice between them is
 * made. */
typedef enum
{
    MODULE_ARTIFACT_PRELUDE, /* a .brainrot file, spliced in like a #cooked
                                "path" include */
    MODULE_ARTIFACT_NATIVE,  /* a .so, dlopen'd via brainrot_module_init()
                                (native builds only -- see module_path.c) */
} ModuleArtifactKind;

/* Resolves `name` to an absolute path, searching the path described above.
 * Within each directory, a "<name>.brainrot" prelude is checked before a
 * "<name>.so" native module -- "one syntax, one search path, two possible
 * artifact kinds" (#207), not two independent searches. Returns a malloc'd
 * absolute path on success (caller frees it) and sets *out_kind, or
 * returns NULL (leaving *out_kind untouched) if no directory in the search
 * path has either. The native (".so") form is never resolved in a
 * STDROT_STATIC (wasm) build, which has no dynamic loader to dlopen it
 * with -- see module_path.c. */
char *module_path_resolve(const char *name, ModuleArtifactKind *out_kind);

#endif
