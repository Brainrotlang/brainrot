/* stdrot/file.c -- stdio.h, rebranded (#213).
 *
 * Twelve file operations behind a `SAUCE *` handle. Not gated behind
 * `#cooked`: file I/O is common enough to stay globally available, the same
 * way slorp is.
 *
 *   crackopen  fopen      peaceout   fclose     doomscroll fread
 *   shitpost   fwrite     skim       fgets      yapto      fprintf
 *   zoink      fseek      whereami   ftell      throwback  rewind
 *   itsjoever  feof       bricked    ferror     bustcache  fflush
 *
 * ── The live-handle registry, and why it is the whole point ────────────
 * This file implements the ownership model documented on STDROT_HANDLE
 * (stdrot_api.h), which is this project's answer to roadmap Appendix B Q6
 * ("Textures, sockets, and map entries all outlive statements... Handles
 * sidestep this by keeping ownership in C -- is that the general answer?").
 * Files are the simplest case to prove it on.
 *
 * The registry is not bookkeeping. A Brainrot `SAUCE *` is an address, and
 * a Brainrot program can produce an address that was never a file: a stale
 * token kept past peaceout(), a pointer from somewhere else entirely, the
 * result of arithmetic. Handing any of those to fclose()/fread() is
 * undefined behaviour that no amount of static typing prevents, because
 * the type system sees only "opaque pointer".
 *
 * So every entry point resolves its handle through resolve_handle(), which
 * accepts ONLY an address currently in this table. Use-after-release and
 * double-release become diagnostics instead of a double free(); a
 * fabricated token becomes an error instead of a wild fclose(). That is
 * what makes a handle genuinely safer than the raw pointer underneath it,
 * and it is why the ABI comment says retagging a pointer as a handle
 * without a registry behind it would be security theatre.
 *
 * ── No leaked FILE * on any exit path ─────────────────────────────────
 * A destructor closes everything still open when libstdrot.so is unloaded.
 * That covers the paths a program cannot clean up after: ragequit(), a
 * native that exit()s, or simply forgetting peaceout(). It is the same
 * mechanism yapcat.c uses for its scratch buffer, and it must be a
 * DESTRUCTOR rather than atexit() for the same reason: stdrot_unload()
 * dlclose()s this object, and an atexit handler registered from here could
 * be invoked after its own code was unmapped.
 */
#include "stdrot_api.h"
#include "stdrot_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Grows by doubling; there is no fixed ceiling on open files beyond what
   the OS itself enforces. */
static FILE **live_files = NULL;
static size_t live_count = 0;
static size_t live_capacity = 0;

/* doomscroll's read scratch. File-scope rather than function-static so the
   destructor below can release it: libstdrot.so is dlclose()d by
   stdrot_unload(), which unmaps the static itself, so a buffer merely left
   "still reachable" is genuinely unreachable by the time LeakSanitizer
   scans and is reported as a real leak. Same reasoning as yapcat.c's. */
static char *read_buf = NULL;
static size_t read_buf_size = 0;

static void registry_add(FILE *fp)
{
    if (live_count == live_capacity)
    {
        size_t next = live_capacity ? live_capacity * 2 : 8;
        /* sizeof a FILE* is exactly what is wanted here -- this array holds
           pointers, not FILE objects, whose size is not even knowable.
           bugprone-sizeof-expression flags every sizeof of a pointer-to-
           aggregate because the usual mistake is meaning the aggregate;
           that is not the case here. */
        // NOLINTNEXTLINE(bugprone-sizeof-expression)
        FILE **grown = realloc(live_files, next * sizeof(*live_files));
        if (!grown)
        {
            fprintf(stderr,
                    "Error: crackopen: out of memory tracking open "
                    "files at line %d\n",
                    g_exec_context.line_number);
            fclose(fp);
            exit(1);
        }
        live_files = grown;
        live_capacity = next;
    }
    live_files[live_count++] = fp;
}

static bool registry_remove(FILE *fp)
{
    for (size_t i = 0; i < live_count; i++)
    {
        if (live_files[i] == fp)
        {
            live_files[i] = live_files[--live_count];
            return true;
        }
    }
    return false;
}

static bool registry_contains(const FILE *fp)
{
    for (size_t i = 0; i < live_count; i++)
    {
        if (live_files[i] == fp)
            return true;
    }
    return false;
}

/* Runs when libstdrot.so is unloaded, while this object is still mapped.
   See the file comment for why a destructor and not atexit(). */
__attribute__((destructor)) static void file_release_all(void)
{
    for (size_t i = 0; i < live_count; i++)
        fclose(live_files[i]);
    free(live_files);
    live_files = NULL;
    live_count = 0;
    live_capacity = 0;
    free(read_buf);
    read_buf = NULL;
    read_buf_size = 0;
}

/* The one gate every operation below passes through. A handle that is not
   currently live is an error, never a pointer to act on -- see the file
   comment for why this is the load-bearing part of the design. */
static FILE *resolve_handle(const char *who, const StdrotValue *v)
{
    FILE *fp = (FILE *)v->val.handle.handle;
    if (!fp || !registry_contains(fp))
    {
        fprintf(stderr,
                "Error: %s: not an open SAUCE -- it was never opened, or "
                "was already closed with peaceout, at line %d\n",
                who, g_exec_context.line_number);
        exit(1);
    }
    return fp;
}

/* A `rant` is length-prefixed and NOT NUL-terminated, so paths and modes
   have to be copied into a terminated buffer before reaching libc. */
static bool to_cstr(const String s, char *out, size_t out_size)
{
    if (!s.data || s.len >= out_size)
        return false;
    memcpy(out, s.data, s.len);
    out[s.len] = '\0';
    return true;
}

/* crackopen(rant path, rant mode) -> SAUCE*
   Returns a null handle when the file cannot be opened, so `edgy (!f)`
   is the idiomatic check -- a null pointer is falsy in Brainrot. This is
   deliberately not an error: a missing file is an ordinary, expected
   outcome a program should be able to handle, unlike being handed a
   fabricated handle, which is a bug. */
static StdrotValue stdrot_crackopen(StdrotValue *args, int argc)
{
    (void)argc;
    char path[4096];
    char mode[16];
    StdrotValue out = {STDROT_HANDLE, {0}};
    out.val.handle.type_name = "SAUCE";
    out.val.handle.handle = NULL;

    if (!to_cstr(args[0].val.str, path, sizeof(path)) ||
        !to_cstr(args[1].val.str, mode, sizeof(mode)))
    {
        return out; /* unusable path/mode reads as "could not open" */
    }

    FILE *fp = fopen(path, mode);
    if (!fp)
        return out;

    registry_add(fp);
    out.val.handle.handle = fp;
    return out;
}

/* peaceout(SAUCE *f) -> rizz -- 0 on success, matching fclose. */
static StdrotValue stdrot_peaceout(StdrotValue *args, int argc)
{
    (void)argc;
    FILE *fp = resolve_handle("peaceout", &args[0]);
    registry_remove(fp);
    return (StdrotValue){STDROT_INT, {.i = fclose(fp)}};
}

/* doomscroll(SAUCE *f, rizz n) -> rant -- fread of up to n bytes.
   Binary-safe: the result's length is the byte count actually read, so
   embedded NULs survive, which is exactly what a rant's length prefix is
   for. Returns a short (or empty) rant at end of file. */
static StdrotValue stdrot_doomscroll(StdrotValue *args, int argc)
{
    (void)argc;
    FILE *fp = resolve_handle("doomscroll", &args[0]);
    int want = args[1].val.i;
    if (want < 0)
    {
        fprintf(stderr,
                "Error: doomscroll: byte count must not be negative "
                "(got %d) at line %d\n",
                want, g_exec_context.line_number);
        exit(1);
    }

    size_t need = (size_t)want + 1;
    if (need > read_buf_size)
    {
        char *grown = realloc(read_buf, need);
        if (!grown)
        {
            fprintf(stderr,
                    "Error: doomscroll: out of memory reading %d "
                    "bytes at line %d\n",
                    want, g_exec_context.line_number);
            exit(1);
        }
        read_buf = grown;
        read_buf_size = need;
    }

    size_t got = fread(read_buf, 1, (size_t)want, fp);
    read_buf[got] = '\0';
    /* The host deep-copies a returned STDROT_STRING before anything else
       runs, so handing back this reused scratch is safe -- the same
       contract yapcat.c relies on and documents at length. */
    return (StdrotValue){STDROT_STRING,
                         {.str = {.data = read_buf, .len = got}}};
}

/* shitpost(SAUCE *f, rant data) -> rizz -- fwrite, binary-safe.
   Writes data.len bytes, NOT up to a terminator, so a rant containing an
   embedded NUL round-trips through doomscroll unchanged. Returns the
   number of bytes written. */
static StdrotValue stdrot_shitpost(StdrotValue *args, int argc)
{
    (void)argc;
    FILE *fp = resolve_handle("shitpost", &args[0]);
    const String data = args[1].val.str;
    size_t len = data.data ? data.len : 0;
    size_t written = len ? fwrite(data.data, 1, len, fp) : 0;
    return (StdrotValue){STDROT_INT, {.i = (int)written}};
}

/* skim(SAUCE *f) -> rant -- one line, fgets-shaped.
   The trailing newline is stripped, so `yapping("%s", skim(f))` prints one
   line rather than two. At end of file the result is the empty string;
   pair it with itsjoever() to drive a read loop. */
static StdrotValue stdrot_skim(StdrotValue *args, int argc)
{
    (void)argc;
    FILE *fp = resolve_handle("skim", &args[0]);

    static char line[8192];
    if (!fgets(line, (int)sizeof(line), fp))
    {
        return (StdrotValue){STDROT_STRING, {.str = {.data = "", .len = 0}}};
    }
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
        line[--len] = '\0';
    return (StdrotValue){STDROT_STRING, {.str = {.data = line, .len = len}}};
}

/* yapto(SAUCE *f, rant fmt, ...) -> skibidi -- fprintf-shaped.
   Deliberately distinct from shitpost: shitpost is fwrite (size/count,
   binary-safe), yapto is formatted text. Mirrors yapping/yappin's
   relationship to stdout, and shares their exact formatter so the two
   cannot drift. No trailing newline is added -- a file writer should not
   decide line structure for you. */
static StdrotValue stdrot_yapto(StdrotValue *args, int argc)
{
    FILE *fp = resolve_handle("yapto", &args[0]);
    if (argc > 1 && args[1].type == STDROT_STRING)
    {
        stdrot_format_to_stream(fp, args[1].val.str.data, &args[2], argc - 2,
                                0);
    }
    return (StdrotValue){STDROT_NONE, {0}};
}

/* zoink(SAUCE *f, rizz offset, rizz whence) -> rizz -- fseek.
   `whence` takes libc's SEEK_SET/SEEK_CUR/SEEK_END values (0/1/2). Returns
   0 on success, matching fseek. */
static StdrotValue stdrot_zoink(StdrotValue *args, int argc)
{
    (void)argc;
    FILE *fp = resolve_handle("zoink", &args[0]);
    int whence = args[2].val.i;
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
    {
        fprintf(stderr,
                "Error: zoink: whence must be 0 (start), 1 (current) or "
                "2 (end), got %d at line %d\n",
                whence, g_exec_context.line_number);
        exit(1);
    }
    return (StdrotValue){STDROT_INT,
                         {.i = fseek(fp, (long)args[1].val.i, whence)}};
}

/* whereami(SAUCE *f) -> rizz -- ftell, or -1 on failure. */
static StdrotValue stdrot_whereami(StdrotValue *args, int argc)
{
    (void)argc;
    FILE *fp = resolve_handle("whereami", &args[0]);
    return (StdrotValue){STDROT_INT, {.i = (int)ftell(fp)}};
}

/* throwback(SAUCE *f) -> skibidi -- rewind. Also clears the error and EOF
   flags, exactly as rewind() does, which is why it is not just zoink(f, 0,
   0). */
static StdrotValue stdrot_throwback(StdrotValue *args, int argc)
{
    (void)argc;
    rewind(resolve_handle("throwback", &args[0]));
    return (StdrotValue){STDROT_NONE, {0}};
}

/* itsjoever(SAUCE *f) -> cap -- "is there nothing left?"
 *
 * LOOKAHEAD, deliberately NOT a bare feof(). This is the one place this
 * library departs from its C counterpart, and it is worth being explicit
 * about why.
 *
 * C's feof() reports whether a read has ALREADY failed, so the classic
 * `while (!feof(f))` runs one iteration too many: after the last line is
 * consumed the flag is still clear, the loop body runs again, and the
 * program processes one phantom empty record. That is the single most
 * common file-reading bug in C, and the idiom the issue documents --
 *
 *     goon (!itsjoever(f)) { rant line = skim(f); yapping("%s", line); }
 *
 * -- would print a spurious blank line at the end of every file.
 *
 * A name is a contract: `itsjoever` asks whether it is over, not whether
 * something already went wrong. So it peeks one byte and puts it back. A
 * loop written the obvious way then reads exactly the file's contents.
 *
 * The peek is invisible: ungetc() is guaranteed for one character, and it
 * also clears the EOF indicator, so the stream position and flags are the
 * same afterwards as before. Use bricked() for the "did something go
 * wrong" question -- that one IS a plain ferror(), because C's semantics
 * are the right ones there. */
static StdrotValue stdrot_itsjoever(StdrotValue *args, int argc)
{
    (void)argc;
    FILE *fp = resolve_handle("itsjoever", &args[0]);
    int c = getc(fp);
    if (c == EOF)
        return (StdrotValue){STDROT_BOOL, {.b = true}};
    ungetc(c, fp);
    return (StdrotValue){STDROT_BOOL, {.b = false}};
}

/* bricked(SAUCE *f) -> cap -- ferror. */
static StdrotValue stdrot_bricked(StdrotValue *args, int argc)
{
    (void)argc;
    FILE *fp = resolve_handle("bricked", &args[0]);
    return (StdrotValue){STDROT_BOOL, {.b = ferror(fp) != 0}};
}

/* bustcache(SAUCE *f) -> rizz -- fflush, 0 on success. */
static StdrotValue stdrot_bustcache(StdrotValue *args, int argc)
{
    (void)argc;
    FILE *fp = resolve_handle("bustcache", &args[0]);
    return (StdrotValue){STDROT_INT, {.i = fflush(fp)}};
}

/* Every handle parameter is {STDROT_HANDLE, "SAUCE", 0}: the kind tag is
   what stops a future socket handle being accepted here, since VAR_PTR
   alone cannot tell two opaque addresses apart. */
#define SAUCE_PARAM                                                            \
    {                                                                          \
        STDROT_HANDLE, "SAUCE", 0                                              \
    }

static const StdrotParam crackopen_params[] = {
    {STDROT_STRING, NULL, 0},
    {STDROT_STRING, NULL, 0},
};
static const StdrotParam handle_only_params[] = {SAUCE_PARAM};
static const StdrotParam handle_int_params[] = {
    SAUCE_PARAM,
    {STDROT_INT, NULL, 0},
};
static const StdrotParam handle_string_params[] = {
    SAUCE_PARAM,
    {STDROT_STRING, NULL, 0},
};
static const StdrotParam zoink_params[] = {
    SAUCE_PARAM,
    {STDROT_INT, NULL, 0},
    {STDROT_INT, NULL, 0},
};

STDROT_EXPORT_SIG("crackopen", stdrot_crackopen,
                  ((StdrotParam){STDROT_HANDLE, "SAUCE", 0}), crackopen_params,
                  2, 2, false);
STDROT_EXPORT_SIG("peaceout", stdrot_peaceout,
                  ((StdrotParam){STDROT_INT, NULL, 0}), handle_only_params, 1,
                  1, false);
STDROT_EXPORT_SIG("doomscroll", stdrot_doomscroll,
                  ((StdrotParam){STDROT_STRING, NULL, 0}), handle_int_params, 2,
                  2, false);
STDROT_EXPORT_SIG("shitpost", stdrot_shitpost,
                  ((StdrotParam){STDROT_INT, NULL, 0}), handle_string_params, 2,
                  2, false);
STDROT_EXPORT_SIG("skim", stdrot_skim, ((StdrotParam){STDROT_STRING, NULL, 0}),
                  handle_only_params, 1, 1, false);
STDROT_EXPORT_SIG_VARIADIC("yapto", stdrot_yapto,
                           ((StdrotParam){STDROT_NONE, NULL, 0}),
                           handle_string_params, 2, 2);
STDROT_EXPORT_SIG("zoink", stdrot_zoink, ((StdrotParam){STDROT_INT, NULL, 0}),
                  zoink_params, 3, 3, false);
STDROT_EXPORT_SIG("whereami", stdrot_whereami,
                  ((StdrotParam){STDROT_INT, NULL, 0}), handle_only_params, 1,
                  1, false);
STDROT_EXPORT_SIG("throwback", stdrot_throwback,
                  ((StdrotParam){STDROT_NONE, NULL, 0}), handle_only_params, 1,
                  1, false);
STDROT_EXPORT_SIG("itsjoever", stdrot_itsjoever,
                  ((StdrotParam){STDROT_BOOL, NULL, 0}), handle_only_params, 1,
                  1, false);
STDROT_EXPORT_SIG("bricked", stdrot_bricked,
                  ((StdrotParam){STDROT_BOOL, NULL, 0}), handle_only_params, 1,
                  1, false);
STDROT_EXPORT_SIG("bustcache", stdrot_bustcache,
                  ((StdrotParam){STDROT_INT, NULL, 0}), handle_only_params, 1,
                  1, false);
