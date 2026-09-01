/* rayrot/raylib.c – Hand-written raylib binding for Brainrot (Issue #208,
 * Phase 5 "Road A").
 *
 * This is a NATIVE MODULE, not part of the core libstdrot.so. It is built
 * into rayrot/raylib.so by the optional `make rayrot` target (which
 * links against a real raylib via pkg-config) and loaded at runtime by
 *
 *     #cooked <raylib>
 *
 * exactly like tests/nativemodules/testnative.c: compiled with
 * -DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init_v3 (Makefile) so this
 * file's STDROT_EXPORT_SIG() self-registrations are collected by
 * stdrot/registry.c and exported under the brainrot_module_init_v3() name
 * stdrot_load_module() (stdrot.c) looks for. raylib is therefore an
 * OPTIONAL dependency of this one target only -- `make` / `make test` /
 * `make valgrind` never build this file and do not need raylib installed.
 *
 * ── The Road A ABI trick ────────────────────────────────────────────────
 * The Brainrot native ABI (stdrot/stdrot_api.h, v2) marshals scalars,
 * C-strings, pointers and bools -- but NOT C structs by value. raylib is
 * "struct city" (Color, Vector2, Texture2D, ...), so every wrapper here
 * takes those aggregates apart into scalar arguments and rebuilds them on
 * the C side:
 *
 *   - a `Color` becomes four `rizz` (int) arguments r, g, b, a, reassembled
 *     by make_color() below;
 *   - a `Texture2D` (which cannot cross the boundary and whose lifetime
 *     outlives any statement) stays owned by C in g_textures[]; Brainrot
 *     only ever holds an integer HANDLE (its index), per the roadmap's
 *     "textures become integer handles, C owns the array" decision
 *     (Appendix B Q6). A live handle always implies a live GL context and a
 *     successful load: a failed load returns -1 without consuming a slot, and
 *     rl_close_window() unloads every still-live texture before the context
 *     dies. The handle is a plain index with no generation counter (Road A
 *     stays crude), so after rl_unload_texture() the freed index is recycled
 *     and a stale handle silently aliases the next load -- don't keep using a
 *     handle past its rl_unload_texture().
 *
 * By-value struct passing and a generated binding are Road B, a separate
 * follow-up that needs an ABI extension this file deliberately sidesteps.
 *
 * ── String ownership ─────────────────────────────────────────────────────
 * A STDROT_CSTRING argument is adapter-owned scratch, freed the instant the
 * wrapper returns (stdrot_api.h). Most raylib calls here consume the string
 * DURING the call (DrawText, MeasureText, LoadTexture) and are fine passing
 * it straight through. InitWindow() is the exception: raylib RETAINS the
 * title pointer (`CORE.Window.title = title;`, no copy), so br_init_window()
 * hands it a module-owned copy that lives until rl_close_window().
 *
 * ── Key codes ───────────────────────────────────────────────────────────
 * rl_is_key_down()/rl_is_key_pressed() take a raw integer keycode. Until a
 * generator can emit KEY_* / MOUSE_* as Brainrot constants (Road B), pass
 * the raylib integer directly, e.g. 32 = KEY_SPACE, 262/263/264/265 =
 * RIGHT/LEFT/DOWN/UP, 256 = KEY_ESCAPE.
 */
#include "stdrot_api.h"
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Texture handle table (C owns the real Texture2D objects) ───────────── */

#define RAYROT_MAX_TEXTURES 256

static Texture2D g_textures[RAYROT_MAX_TEXTURES];
static bool g_texture_used[RAYROT_MAX_TEXTURES];

/* ── Audio handle tables (C owns the real Music/Sound objects) ──────────── *
 * Same Road A trick as textures: Music and Sound are structs that cannot
 * cross the ABI and outlive any single statement, so C keeps them and
 * Brainrot holds an integer index. A failed load returns -1 without
 * consuming a slot, so a non-negative handle always means a real stream.
 *
 * MUSIC is streamed and SOUND is decoded whole into memory, which is a
 * choice the caller has to make rather than an implementation detail: an
 * 80-second background track as a Sound is tens of megabytes of PCM, and a
 * one-shot hit as a Music is a stream you have to remember to pump. */

#define RAYROT_MAX_MUSIC 16
#define RAYROT_MAX_SOUNDS 64

static Music g_music[RAYROT_MAX_MUSIC];
static bool g_music_used[RAYROT_MAX_MUSIC];
static Sound g_sounds[RAYROT_MAX_SOUNDS];
static bool g_sound_used[RAYROT_MAX_SOUNDS];

/* Is this handle a slot this module currently owns? Both predicates live
 * here, beside the tables they guard, so every wrapper validates the same
 * way -- an out-of-range or already-unloaded handle does nothing rather
 * than handing raylib a freed stream. */
static bool br_music_live(int handle)
{
    return handle >= 0 && handle < RAYROT_MAX_MUSIC && g_music_used[handle];
}

static bool br_sound_live(int handle)
{
    return handle >= 0 && handle < RAYROT_MAX_SOUNDS && g_sound_used[handle];
}

/* Module-owned copy of the window title. raylib's InitWindow() retains the
 * pointer it is given rather than copying it, so the adapter-owned
 * STDROT_CSTRING argument cannot be handed straight through -- see the
 * "String ownership" note in this file's header. */
static char *g_window_title = NULL;

/* ── LeakSanitizer: disclaiming the graphics stack's globals (issue #267) ── *
 * raylib and the libraries it drives (GLFW, the GL driver, X11, fontconfig)
 * allocate process-lifetime global state -- a GL context, the default
 * font/shader, X11 and font caches -- that they never return to the allocator;
 * the OS reclaims it at exit. That state is not rayrot's to free, but the
 * interpreter is built with -fsanitize=address, so LeakSanitizer reports every
 * one of those allocations when the demo exits (issue #267).
 *
 * Bracket each raylib call with LSan's allocator-scoped disable/enable so
 * allocations made *inside* raylib are excluded from the leak report on
 * purpose, as unowned. This is deliberately narrow: everything rayrot itself
 * allocates -- the window-title copy above, the texture table -- happens
 * OUTSIDE these brackets and stays fully tracked, so a real rayrot leak is
 * still caught (a regression here does not go dark). The pure input/query
 * getters (WindowShouldClose, IsKeyDown, GetScreenWidth, ...) are left
 * unbracketed because they poll rather than allocate persistent state.
 *
 * The controls are weak symbols: they bind to libasan in a sanitizer build and
 * are inert no-ops otherwise (e.g. `make release`), so the module loads either
 * way. */
__attribute__((weak)) void __lsan_disable(void);
__attribute__((weak)) void __lsan_enable(void);

static void br_lsan_ignore_begin(void)
{
    if (__lsan_disable)
    {
        __lsan_disable();
    }
}

static void br_lsan_ignore_end(void)
{
    if (__lsan_enable)
    {
        __lsan_enable();
    }
}

/* Run a void-returning raylib call with LSan leak-tracking suspended. */
#define BR_RAYLIB_VOID(call)                                                   \
    do                                                                         \
    {                                                                          \
        br_lsan_ignore_begin();                                                \
        call;                                                                  \
        br_lsan_ignore_end();                                                  \
    } while (0)

/* Reassemble a raylib Color from four consecutive int arguments starting at
 * args[base]. Each channel is clamped into the unsigned-char range so a
 * stray Brainrot value can't wrap unexpectedly. */
static Color make_color(const StdrotValue *args, int base)
{
    int chan[4];
    for (int i = 0; i < 4; i++)
    {
        int v = args[base + i].val.i;
        if (v < 0)
        {
            v = 0;
        }
        else if (v > 255)
        {
            v = 255;
        }
        chan[i] = v;
    }
    return (Color){(unsigned char)chan[0], (unsigned char)chan[1],
                   (unsigned char)chan[2], (unsigned char)chan[3]};
}

/* ── Window ──────────────────────────────────────────────────────────────── */

static StdrotValue br_init_window(StdrotValue *args, int argc)
{
    (void)argc;
    /* Give raylib a copy this module owns and keeps alive until
     * rl_close_window(), since InitWindow() retains the pointer and the
     * STDROT_CSTRING argument is freed the moment this call returns. On
     * allocation failure fall back to no title rather than a dangling one. */
    free(g_window_title);
    g_window_title = NULL;
    const char *title = args[2].val.cstr;
    if (title != NULL)
    {
        size_t n = strlen(title) + 1;
        g_window_title = malloc(n);
        if (g_window_title != NULL)
        {
            memcpy(g_window_title, title, n);
        }
    }
    BR_RAYLIB_VOID(InitWindow(args[0].val.i, args[1].val.i, g_window_title));
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_window_should_close(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){.type = STDROT_BOOL,
                         .val = {.b = WindowShouldClose()}};
}

static StdrotValue br_close_window(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    /* A live handle must imply a live GL context, so unload every texture this
     * module still owns before CloseWindow() destroys the context. Otherwise a
     * later InitWindow() + rl_draw_texture(old_handle) would feed raylib a GPU
     * id from a destroyed context. */
    for (int i = 0; i < RAYROT_MAX_TEXTURES; i++)
    {
        if (g_texture_used[i])
        {
            BR_RAYLIB_VOID(UnloadTexture(g_textures[i]));
            g_texture_used[i] = false;
        }
    }
    BR_RAYLIB_VOID(CloseWindow());
    free(g_window_title);
    g_window_title = NULL;
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_set_target_fps(StdrotValue *args, int argc)
{
    (void)argc;
    BR_RAYLIB_VOID(SetTargetFPS(args[0].val.i));
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_get_screen_width(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){.type = STDROT_INT, .val = {.i = GetScreenWidth()}};
}

static StdrotValue br_get_screen_height(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){.type = STDROT_INT, .val = {.i = GetScreenHeight()}};
}

/* ── Frame / drawing ─────────────────────────────────────────────────────── */

static StdrotValue br_begin_drawing(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    BR_RAYLIB_VOID(BeginDrawing());
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_end_drawing(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    BR_RAYLIB_VOID(EndDrawing());
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_clear_background(StdrotValue *args, int argc)
{
    (void)argc;
    BR_RAYLIB_VOID(ClearBackground(make_color(args, 0)));
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_get_frame_time(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){.type = STDROT_FLOAT, .val = {.f = GetFrameTime()}};
}

static StdrotValue br_draw_fps(StdrotValue *args, int argc)
{
    (void)argc;
    BR_RAYLIB_VOID(DrawFPS(args[0].val.i, args[1].val.i));
    return (StdrotValue){.type = STDROT_NONE};
}

/* ── Shapes ──────────────────────────────────────────────────────────────── */

static StdrotValue br_draw_circle(StdrotValue *args, int argc)
{
    (void)argc;
    BR_RAYLIB_VOID(DrawCircle(args[0].val.i, args[1].val.i, args[2].val.f,
                              make_color(args, 3)));
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_draw_rectangle(StdrotValue *args, int argc)
{
    (void)argc;
    BR_RAYLIB_VOID(DrawRectangle(args[0].val.i, args[1].val.i, args[2].val.i,
                                 args[3].val.i, make_color(args, 4)));
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_draw_line(StdrotValue *args, int argc)
{
    (void)argc;
    BR_RAYLIB_VOID(DrawLine(args[0].val.i, args[1].val.i, args[2].val.i,
                            args[3].val.i, make_color(args, 4)));
    return (StdrotValue){.type = STDROT_NONE};
}

/* ── Text ────────────────────────────────────────────────────────────────── */

static StdrotValue br_draw_text(StdrotValue *args, int argc)
{
    (void)argc;
    BR_RAYLIB_VOID(DrawText(args[0].val.cstr, args[1].val.i, args[2].val.i,
                            args[3].val.i, make_color(args, 4)));
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_measure_text(StdrotValue *args, int argc)
{
    (void)argc;
    return (StdrotValue){
        .type = STDROT_INT,
        .val = {.i = MeasureText(args[0].val.cstr, args[1].val.i)}};
}

/* Build "<text><number>" for the *_text_int wrappers below.
 *
 * Brainrot has no string concatenation and no sprintf, so a program that
 * wants to draw a score has no way to turn one into a drawable string --
 * rl_draw_text() only ever receives a literal. Rather than expose a general
 * format string (a Brainrot-supplied "%s" would read a nonexistent argument
 * and crash the host), the formatting is fixed here: a caller-supplied
 * literal prefix followed by exactly one integer.
 *
 * `pad` is the minimum **field width** of the number, zero-padded -- printf's
 * "%0*d", not a digit count. The distinction is visible on negatives: -450 at
 * pad 6 is "-00450", six columns holding five digits, not six digits plus a
 * sign. Field width is the right rule for the job, because holding a HUD
 * column steady is what stops it jittering as a score grows, and "000450" and
 * "-00450" occupy the same space. 0 means no padding, and the value is capped
 * so a wild `pad` cannot ask for an enormous allocation.
 *
 * Returns freshly allocated storage the caller frees, or NULL if the
 * allocation failed. This is rayrot's own memory, deliberately allocated
 * outside the LSan brackets so a leak here would still be reported. */
#define RAYROT_MAX_PAD 32

static char *br_format_text_int(const char *text, int value, int pad)
{
    if (text == NULL)
    {
        text = "";
    }
    if (pad < 0)
    {
        pad = 0;
    }
    else if (pad > RAYROT_MAX_PAD)
    {
        pad = RAYROT_MAX_PAD;
    }
    int n = snprintf(NULL, 0, "%s%0*d", text, pad, value);
    if (n < 0)
    {
        return NULL;
    }
    char *buf = malloc((size_t)n + 1);
    if (buf == NULL)
    {
        return NULL;
    }
    /* Cannot truncate: the buffer was sized by the identical format above.
     * Discard the count explicitly rather than by omission, so a future edit
     * that makes the two format strings diverge reads as the mistake it is. */
    (void)snprintf(buf, (size_t)n + 1, "%s%0*d", text, pad, value);
    return buf;
}

static StdrotValue br_draw_text_int(StdrotValue *args, int argc)
{
    (void)argc;
    char *s =
        br_format_text_int(args[0].val.cstr, args[1].val.i, args[2].val.i);
    if (s != NULL)
    {
        BR_RAYLIB_VOID(DrawText(s, args[3].val.i, args[4].val.i, args[5].val.i,
                                make_color(args, 6)));
        free(s);
    }
    return (StdrotValue){.type = STDROT_NONE};
}

/* The rl_measure_text() counterpart, so text with a number in it can be
 * centred the same way a literal can. Reports 0 if the string could not be
 * built, matching the "draw nothing" behaviour above. */
static StdrotValue br_measure_text_int(StdrotValue *args, int argc)
{
    (void)argc;
    char *s =
        br_format_text_int(args[0].val.cstr, args[1].val.i, args[2].val.i);
    int width = 0;
    if (s != NULL)
    {
        width = MeasureText(s, args[3].val.i);
        free(s);
    }
    return (StdrotValue){.type = STDROT_INT, .val = {.i = width}};
}

/* ── Input ───────────────────────────────────────────────────────────────── */

static StdrotValue br_is_key_down(StdrotValue *args, int argc)
{
    (void)argc;
    return (StdrotValue){.type = STDROT_BOOL,
                         .val = {.b = IsKeyDown(args[0].val.i)}};
}

static StdrotValue br_is_key_pressed(StdrotValue *args, int argc)
{
    (void)argc;
    return (StdrotValue){.type = STDROT_BOOL,
                         .val = {.b = IsKeyPressed(args[0].val.i)}};
}

/* ── Textures (integer handles; C owns the Texture2D objects) ────────────── */

static StdrotValue br_load_texture(StdrotValue *args, int argc)
{
    (void)argc;
    br_lsan_ignore_begin();
    Texture2D tex = LoadTexture(args[0].val.cstr);
    br_lsan_ignore_end();
    if (tex.id == 0)
    {
        /* Load failed (missing/undecodable file): raylib hands back a zeroed
         * Texture2D. Don't consume a slot; report the -1 failure sentinel so a
         * live handle always means a successful load. */
        return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
    }
    for (int i = 0; i < RAYROT_MAX_TEXTURES; i++)
    {
        if (!g_texture_used[i])
        {
            g_textures[i] = tex;
            g_texture_used[i] = true;
            return (StdrotValue){.type = STDROT_INT, .val = {.i = i}};
        }
    }
    /* Table full: nothing owns this texture, so unload it and report -1. */
    BR_RAYLIB_VOID(UnloadTexture(tex));
    return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
}

static StdrotValue br_draw_texture(StdrotValue *args, int argc)
{
    (void)argc;
    int handle = args[0].val.i;
    if (handle >= 0 && handle < RAYROT_MAX_TEXTURES && g_texture_used[handle])
    {
        BR_RAYLIB_VOID(DrawTexture(g_textures[handle], args[1].val.i,
                                   args[2].val.i, make_color(args, 3)));
    }
    return (StdrotValue){.type = STDROT_NONE};
}

/* DrawTextureRec: one sub-rectangle of a texture, which is what makes sprite
 * atlases and animation frames possible from Brainrot at all.
 *
 * Two aggregates come apart here rather than one. raylib's `rec` is a
 * Rectangle (four floats) and `position` is a Vector2 (two floats), so the
 * source box arrives as sx, sy, sw, sh and the destination corner as x, y --
 * six `chad` arguments, then the usual four-int Color tail.
 *
 * Note the float position, which differs from rl_draw_texture()'s int x, y.
 * That is not an inconsistency: it mirrors raylib, where DrawTexture() takes
 * ints and DrawTextureRec() takes a Vector2. Sub-pixel placement is what a
 * scrolling background wants anyway.
 *
 * A negative `sw` mirrors the sprite horizontally and a negative `sh`
 * vertically -- raylib's own idiom, since the source rectangle's sign decides
 * the texture coordinate order. That is the only way to face a sprite the
 * other direction until DrawTexturePro's rotation is exposed, so it is worth
 * knowing about rather than looking like a bug.
 *
 * The rectangle is handed to raylib unchanged: rayrot does not clamp it to
 * the texture, so sampling outside the image is raylib's business and not a
 * guarantee this wrapper makes. Handle validation matches rl_draw_texture --
 * an out-of-range or already-unloaded handle draws nothing rather than
 * feeding raylib a stale GPU id. */
static StdrotValue br_draw_texture_rec(StdrotValue *args, int argc)
{
    (void)argc;
    int handle = args[0].val.i;
    if (handle >= 0 && handle < RAYROT_MAX_TEXTURES && g_texture_used[handle])
    {
        Rectangle rec = {(float)args[1].val.f, (float)args[2].val.f,
                         (float)args[3].val.f, (float)args[4].val.f};
        Vector2 pos = {(float)args[5].val.f, (float)args[6].val.f};
        BR_RAYLIB_VOID(
            DrawTextureRec(g_textures[handle], rec, pos, make_color(args, 7)));
    }
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_unload_texture(StdrotValue *args, int argc)
{
    (void)argc;
    int handle = args[0].val.i;
    if (handle >= 0 && handle < RAYROT_MAX_TEXTURES && g_texture_used[handle])
    {
        BR_RAYLIB_VOID(UnloadTexture(g_textures[handle]));
        g_texture_used[handle] = false;
    }
    return (StdrotValue){.type = STDROT_NONE};
}

/* ── Audio ───────────────────────────────────────────────────────────────── *
 * rl_init_audio_device() must succeed before anything else here does
 * anything at all. raylib does not report failure from InitAudioDevice() --
 * on a machine with no sound device, or in a container, it logs and carries
 * on, and every later call silently does nothing. rl_is_audio_device_ready()
 * is the only way a Brainrot program can tell "playing quietly" from "not
 * playing", so it is exposed rather than left as a diagnostic.
 *
 * The one contract that bites: rl_update_music() MUST be called every frame
 * for every playing stream. It refills the decode buffer. Miss it and the
 * track plays for a fraction of a second and stops, with no error. */

static StdrotValue br_init_audio_device(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    BR_RAYLIB_VOID(InitAudioDevice());
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_is_audio_device_ready(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    return (StdrotValue){.type = STDROT_BOOL,
                         .val = {.b = IsAudioDeviceReady()}};
}

static StdrotValue br_close_audio_device(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    /* A live handle must imply a live audio device, exactly as a texture
     * handle implies a live GL context: unload everything this module still
     * owns before the device goes away, or a later init plus a stale handle
     * hands raylib a freed stream. */
    for (int i = 0; i < RAYROT_MAX_MUSIC; i++)
    {
        if (g_music_used[i])
        {
            BR_RAYLIB_VOID(UnloadMusicStream(g_music[i]));
            g_music_used[i] = false;
        }
    }
    for (int i = 0; i < RAYROT_MAX_SOUNDS; i++)
    {
        if (g_sound_used[i])
        {
            BR_RAYLIB_VOID(UnloadSound(g_sounds[i]));
            g_sound_used[i] = false;
        }
    }
    BR_RAYLIB_VOID(CloseAudioDevice());
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_load_music(StdrotValue *args, int argc)
{
    (void)argc;
    /* Refuse before raylib is asked, not after. LoadMusicStream() sets
     * frameCount from the FILE once the decoder opens it; attaching the
     * playback stream is a separate step that can leave buffer == NULL when
     * the device was never ready or is already closed. So frameCount > 0 is
     * not evidence of a usable stream, and a slot minted from one would
     * break this module's central invariant -- a live handle implies a live
     * device -- with a later rl_is_music_playing() reaching for a miniaudio
     * mutex that was destroyed by CloseAudioDevice() or never initialised.
     *
     * rl_load_sound() does not have that hole (LoadSoundFromWave leaves
     * frameCount == 0 when it cannot make a buffer) but is gated too, so the
     * rule is one rule rather than a property of two different raylib
     * functions that happen to differ. */
    if (!IsAudioDeviceReady())
    {
        return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
    }
    br_lsan_ignore_begin();
    Music m = LoadMusicStream(args[0].val.cstr);
    br_lsan_ignore_end();
    /* frameCount 0 means raylib could not decode it (missing file, or a
     * format this build has no decoder for). Don't consume a slot. */
    if (m.frameCount == 0)
    {
        BR_RAYLIB_VOID(UnloadMusicStream(m));
        return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
    }
    for (int i = 0; i < RAYROT_MAX_MUSIC; i++)
    {
        if (!g_music_used[i])
        {
            g_music[i] = m;
            g_music_used[i] = true;
            return (StdrotValue){.type = STDROT_INT, .val = {.i = i}};
        }
    }
    BR_RAYLIB_VOID(UnloadMusicStream(m));
    return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
}

static StdrotValue br_play_music(StdrotValue *args, int argc)
{
    (void)argc;
    if (br_music_live(args[0].val.i))
    {
        BR_RAYLIB_VOID(PlayMusicStream(g_music[args[0].val.i]));
    }
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_update_music(StdrotValue *args, int argc)
{
    (void)argc;
    if (br_music_live(args[0].val.i))
    {
        BR_RAYLIB_VOID(UpdateMusicStream(g_music[args[0].val.i]));
    }
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_stop_music(StdrotValue *args, int argc)
{
    (void)argc;
    if (br_music_live(args[0].val.i))
    {
        BR_RAYLIB_VOID(StopMusicStream(g_music[args[0].val.i]));
    }
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_set_music_volume(StdrotValue *args, int argc)
{
    (void)argc;
    if (br_music_live(args[0].val.i))
    {
        BR_RAYLIB_VOID(
            SetMusicVolume(g_music[args[0].val.i], (float)args[1].val.f));
    }
    return (StdrotValue){.type = STDROT_NONE};
}

/* `looping` is a plain field on the Music struct rather than a call, so it
 * cannot be reached from Brainrot at all without a setter. raylib defaults
 * it to true; a background track wants that and a jingle does not. */
static StdrotValue br_set_music_looping(StdrotValue *args, int argc)
{
    (void)argc;
    if (br_music_live(args[0].val.i))
    {
        g_music[args[0].val.i].looping = args[1].val.b;
    }
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_unload_music(StdrotValue *args, int argc)
{
    (void)argc;
    int handle = args[0].val.i;
    if (br_music_live(handle))
    {
        BR_RAYLIB_VOID(UnloadMusicStream(g_music[handle]));
        g_music_used[handle] = false;
    }
    return (StdrotValue){.type = STDROT_NONE};
}

/* Playback state, exposed so a program -- and a test -- can tell a stream
 * that is running from one that silently is not. Without these, "did the
 * music play?" has no answer inside Brainrot, and the rl_update_music()
 * contract below has no way to be verified rather than asserted. */
static StdrotValue br_is_music_playing(StdrotValue *args, int argc)
{
    (void)argc;
    bool playing = false;
    if (br_music_live(args[0].val.i))
    {
        playing = IsMusicStreamPlaying(g_music[args[0].val.i]);
    }
    return (StdrotValue){.type = STDROT_BOOL, .val = {.b = playing}};
}

static StdrotValue br_music_time_played(StdrotValue *args, int argc)
{
    (void)argc;
    float t = 0.0f;
    if (br_music_live(args[0].val.i))
    {
        t = GetMusicTimePlayed(g_music[args[0].val.i]);
    }
    return (StdrotValue){.type = STDROT_FLOAT, .val = {.f = t}};
}

static StdrotValue br_music_time_length(StdrotValue *args, int argc)
{
    (void)argc;
    float t = 0.0f;
    if (br_music_live(args[0].val.i))
    {
        t = GetMusicTimeLength(g_music[args[0].val.i]);
    }
    return (StdrotValue){.type = STDROT_FLOAT, .val = {.f = t}};
}

static StdrotValue br_is_sound_playing(StdrotValue *args, int argc)
{
    (void)argc;
    bool playing = false;
    if (br_sound_live(args[0].val.i))
    {
        playing = IsSoundPlaying(g_sounds[args[0].val.i]);
    }
    return (StdrotValue){.type = STDROT_BOOL, .val = {.b = playing}};
}

static StdrotValue br_load_sound(StdrotValue *args, int argc)
{
    (void)argc;
    /* Same gate as rl_load_music(); see the note there. */
    if (!IsAudioDeviceReady())
    {
        return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
    }
    br_lsan_ignore_begin();
    Sound snd = LoadSound(args[0].val.cstr);
    br_lsan_ignore_end();
    if (snd.frameCount == 0)
    {
        BR_RAYLIB_VOID(UnloadSound(snd));
        return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
    }
    for (int i = 0; i < RAYROT_MAX_SOUNDS; i++)
    {
        if (!g_sound_used[i])
        {
            g_sounds[i] = snd;
            g_sound_used[i] = true;
            return (StdrotValue){.type = STDROT_INT, .val = {.i = i}};
        }
    }
    BR_RAYLIB_VOID(UnloadSound(snd));
    return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
}

static StdrotValue br_play_sound(StdrotValue *args, int argc)
{
    (void)argc;
    if (br_sound_live(args[0].val.i))
    {
        BR_RAYLIB_VOID(PlaySound(g_sounds[args[0].val.i]));
    }
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_set_sound_volume(StdrotValue *args, int argc)
{
    (void)argc;
    if (br_sound_live(args[0].val.i))
    {
        BR_RAYLIB_VOID(
            SetSoundVolume(g_sounds[args[0].val.i], (float)args[1].val.f));
    }
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_unload_sound(StdrotValue *args, int argc)
{
    (void)argc;
    int handle = args[0].val.i;
    if (br_sound_live(handle))
    {
        BR_RAYLIB_VOID(UnloadSound(g_sounds[handle]));
        g_sound_used[handle] = false;
    }
    return (StdrotValue){.type = STDROT_NONE};
}

/* ── Signatures / self-registration ──────────────────────────────────────── *
 * Param descriptors let the semantic analyzer check arity and argument
 * types ahead of the call, exactly like a Brainrot-defined function. A
 * four-int Color tail is spelled out as four STDROT_INT params.
 *
 * P_* are plain brace initializers for the static param arrays (constant
 * expressions); R_* are compound literals for the `ret` argument of
 * STDROT_EXPORT_SIG (matching stdrot/yapping.c's inline style). */

#define P_INT                                                                  \
    {                                                                          \
        STDROT_INT, NULL, 0                                                    \
    }
#define P_FLOAT                                                                \
    {                                                                          \
        STDROT_FLOAT, NULL, 0                                                  \
    }
#define P_CSTRING                                                              \
    {                                                                          \
        STDROT_CSTRING, NULL, 0                                                \
    }
#define P_BOOL                                                                 \
    {                                                                          \
        STDROT_BOOL, NULL, 0                                                   \
    }
#define R_INT ((StdrotParam){STDROT_INT, NULL, 0})
#define R_FLOAT ((StdrotParam){STDROT_FLOAT, NULL, 0})
#define R_BOOL ((StdrotParam){STDROT_BOOL, NULL, 0})
#define R_NONE ((StdrotParam){STDROT_NONE, NULL, 0})

static const StdrotParam p_init_window[] = {P_INT, P_INT, P_CSTRING};
STDROT_EXPORT_SIG("rl_init_window", br_init_window, R_NONE, p_init_window, 3, 3,
                  false);

STDROT_EXPORT_SIG("rl_window_should_close", br_window_should_close, R_BOOL,
                  NULL, 0, 0, false);
STDROT_EXPORT_SIG("rl_close_window", br_close_window, R_NONE, NULL, 0, 0,
                  false);

static const StdrotParam p_set_target_fps[] = {P_INT};
STDROT_EXPORT_SIG("rl_set_target_fps", br_set_target_fps, R_NONE,
                  p_set_target_fps, 1, 1, false);

STDROT_EXPORT_SIG("rl_get_screen_width", br_get_screen_width, R_INT, NULL, 0, 0,
                  false);
STDROT_EXPORT_SIG("rl_get_screen_height", br_get_screen_height, R_INT, NULL, 0,
                  0, false);

STDROT_EXPORT_SIG("rl_begin_drawing", br_begin_drawing, R_NONE, NULL, 0, 0,
                  false);
STDROT_EXPORT_SIG("rl_end_drawing", br_end_drawing, R_NONE, NULL, 0, 0, false);

static const StdrotParam p_color4[] = {P_INT, P_INT, P_INT, P_INT};
STDROT_EXPORT_SIG("rl_clear_background", br_clear_background, R_NONE, p_color4,
                  4, 4, false);

STDROT_EXPORT_SIG("rl_get_frame_time", br_get_frame_time, R_FLOAT, NULL, 0, 0,
                  false);

static const StdrotParam p_draw_fps[] = {P_INT, P_INT};
STDROT_EXPORT_SIG("rl_draw_fps", br_draw_fps, R_NONE, p_draw_fps, 2, 2, false);

static const StdrotParam p_draw_circle[] = {P_INT, P_INT, P_FLOAT, P_INT,
                                            P_INT, P_INT, P_INT};
STDROT_EXPORT_SIG("rl_draw_circle", br_draw_circle, R_NONE, p_draw_circle, 7, 7,
                  false);

static const StdrotParam p_draw_rectangle[] = {P_INT, P_INT, P_INT, P_INT,
                                               P_INT, P_INT, P_INT, P_INT};
STDROT_EXPORT_SIG("rl_draw_rectangle", br_draw_rectangle, R_NONE,
                  p_draw_rectangle, 8, 8, false);

static const StdrotParam p_draw_line[] = {P_INT, P_INT, P_INT, P_INT,
                                          P_INT, P_INT, P_INT, P_INT};
STDROT_EXPORT_SIG("rl_draw_line", br_draw_line, R_NONE, p_draw_line, 8, 8,
                  false);

static const StdrotParam p_draw_text[] = {P_CSTRING, P_INT, P_INT, P_INT,
                                          P_INT,     P_INT, P_INT, P_INT};
STDROT_EXPORT_SIG("rl_draw_text", br_draw_text, R_NONE, p_draw_text, 8, 8,
                  false);

static const StdrotParam p_measure_text[] = {P_CSTRING, P_INT};
STDROT_EXPORT_SIG("rl_measure_text", br_measure_text, R_INT, p_measure_text, 2,
                  2, false);

static const StdrotParam p_draw_text_int[] = {
    P_CSTRING, P_INT, P_INT, P_INT, P_INT, P_INT, P_INT, P_INT, P_INT, P_INT};
STDROT_EXPORT_SIG("rl_draw_text_int", br_draw_text_int, R_NONE, p_draw_text_int,
                  10, 10, false);

static const StdrotParam p_measure_text_int[] = {P_CSTRING, P_INT, P_INT,
                                                 P_INT};
STDROT_EXPORT_SIG("rl_measure_text_int", br_measure_text_int, R_INT,
                  p_measure_text_int, 4, 4, false);

static const StdrotParam p_key[] = {P_INT};
STDROT_EXPORT_SIG("rl_is_key_down", br_is_key_down, R_BOOL, p_key, 1, 1, false);
STDROT_EXPORT_SIG("rl_is_key_pressed", br_is_key_pressed, R_BOOL, p_key, 1, 1,
                  false);

static const StdrotParam p_load_texture[] = {P_CSTRING};
STDROT_EXPORT_SIG("rl_load_texture", br_load_texture, R_INT, p_load_texture, 1,
                  1, false);

static const StdrotParam p_draw_texture[] = {P_INT, P_INT, P_INT, P_INT,
                                             P_INT, P_INT, P_INT};
STDROT_EXPORT_SIG("rl_draw_texture", br_draw_texture, R_NONE, p_draw_texture, 7,
                  7, false);

static const StdrotParam p_draw_texture_rec[] = {
    P_INT,   P_FLOAT, P_FLOAT, P_FLOAT, P_FLOAT, P_FLOAT,
    P_FLOAT, P_INT,   P_INT,   P_INT,   P_INT};
STDROT_EXPORT_SIG("rl_draw_texture_rec", br_draw_texture_rec, R_NONE,
                  p_draw_texture_rec, 11, 11, false);

static const StdrotParam p_handle[] = {P_INT};
static const StdrotParam p_handle_float[] = {P_INT, P_FLOAT};
static const StdrotParam p_handle_bool[] = {P_INT, P_BOOL};
static const StdrotParam p_path[] = {P_CSTRING};

STDROT_EXPORT_SIG("rl_init_audio_device", br_init_audio_device, R_NONE, NULL, 0,
                  0, false);
STDROT_EXPORT_SIG("rl_is_audio_device_ready", br_is_audio_device_ready, R_BOOL,
                  NULL, 0, 0, false);
STDROT_EXPORT_SIG("rl_close_audio_device", br_close_audio_device, R_NONE, NULL,
                  0, 0, false);

STDROT_EXPORT_SIG("rl_load_music", br_load_music, R_INT, p_path, 1, 1, false);
STDROT_EXPORT_SIG("rl_play_music", br_play_music, R_NONE, p_handle, 1, 1,
                  false);
STDROT_EXPORT_SIG("rl_update_music", br_update_music, R_NONE, p_handle, 1, 1,
                  false);
STDROT_EXPORT_SIG("rl_stop_music", br_stop_music, R_NONE, p_handle, 1, 1,
                  false);
STDROT_EXPORT_SIG("rl_set_music_volume", br_set_music_volume, R_NONE,
                  p_handle_float, 2, 2, false);
STDROT_EXPORT_SIG("rl_set_music_looping", br_set_music_looping, R_NONE,
                  p_handle_bool, 2, 2, false);
STDROT_EXPORT_SIG("rl_unload_music", br_unload_music, R_NONE, p_handle, 1, 1,
                  false);
STDROT_EXPORT_SIG("rl_is_music_playing", br_is_music_playing, R_BOOL, p_handle,
                  1, 1, false);
STDROT_EXPORT_SIG("rl_music_time_played", br_music_time_played, R_FLOAT,
                  p_handle, 1, 1, false);
STDROT_EXPORT_SIG("rl_music_time_length", br_music_time_length, R_FLOAT,
                  p_handle, 1, 1, false);
STDROT_EXPORT_SIG("rl_is_sound_playing", br_is_sound_playing, R_BOOL, p_handle,
                  1, 1, false);

STDROT_EXPORT_SIG("rl_load_sound", br_load_sound, R_INT, p_path, 1, 1, false);
STDROT_EXPORT_SIG("rl_play_sound", br_play_sound, R_NONE, p_handle, 1, 1,
                  false);
STDROT_EXPORT_SIG("rl_set_sound_volume", br_set_sound_volume, R_NONE,
                  p_handle_float, 2, 2, false);
STDROT_EXPORT_SIG("rl_unload_sound", br_unload_sound, R_NONE, p_handle, 1, 1,
                  false);

static const StdrotParam p_unload_texture[] = {P_INT};
STDROT_EXPORT_SIG("rl_unload_texture", br_unload_texture, R_NONE,
                  p_unload_texture, 1, 1, false);
