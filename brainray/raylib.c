/* brainray/raylib.c – Hand-written raylib binding for Brainrot (Issue #208,
 * Phase 5 "Road A").
 *
 * This is a NATIVE MODULE, not part of the core libstdrot.so. It is built
 * into brainray/raylib.so by the optional `make brainray` target (which
 * links against a real raylib via pkg-config) and loaded at runtime by
 *
 *     #cooked <raylib>
 *
 * exactly like tests/nativemodules/testnative.c: compiled with
 * -DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init (Makefile) so this
 * file's STDROT_EXPORT_SIG() self-registrations are collected by
 * stdrot/registry.c and exported under the brainrot_module_init() name
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
 *     (Appendix B Q6).
 *
 * By-value struct passing and a generated binding are Road B, a separate
 * follow-up that needs an ABI extension this file deliberately sidesteps.
 *
 * ── Key codes ───────────────────────────────────────────────────────────
 * rl_is_key_down()/rl_is_key_pressed() take a raw integer keycode. Until a
 * generator can emit KEY_* / MOUSE_* as Brainrot constants (Road B), pass
 * the raylib integer directly, e.g. 32 = KEY_SPACE, 262/263/264/265 =
 * RIGHT/LEFT/DOWN/UP, 256 = KEY_ESCAPE.
 */
#include "stdrot_api.h"
#include <raylib.h>

/* ── Texture handle table (C owns the real Texture2D objects) ───────────── */

#define BRAINRAY_MAX_TEXTURES 256

static Texture2D g_textures[BRAINRAY_MAX_TEXTURES];
static bool g_texture_used[BRAINRAY_MAX_TEXTURES];

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
    InitWindow(args[0].val.i, args[1].val.i, args[2].val.cstr);
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
    CloseWindow();
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_set_target_fps(StdrotValue *args, int argc)
{
    (void)argc;
    SetTargetFPS(args[0].val.i);
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
    BeginDrawing();
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_end_drawing(StdrotValue *args, int argc)
{
    (void)args;
    (void)argc;
    EndDrawing();
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_clear_background(StdrotValue *args, int argc)
{
    (void)argc;
    ClearBackground(make_color(args, 0));
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
    DrawFPS(args[0].val.i, args[1].val.i);
    return (StdrotValue){.type = STDROT_NONE};
}

/* ── Shapes ──────────────────────────────────────────────────────────────── */

static StdrotValue br_draw_circle(StdrotValue *args, int argc)
{
    (void)argc;
    DrawCircle(args[0].val.i, args[1].val.i, args[2].val.f,
               make_color(args, 3));
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_draw_rectangle(StdrotValue *args, int argc)
{
    (void)argc;
    DrawRectangle(args[0].val.i, args[1].val.i, args[2].val.i, args[3].val.i,
                  make_color(args, 4));
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_draw_line(StdrotValue *args, int argc)
{
    (void)argc;
    DrawLine(args[0].val.i, args[1].val.i, args[2].val.i, args[3].val.i,
             make_color(args, 4));
    return (StdrotValue){.type = STDROT_NONE};
}

/* ── Text ────────────────────────────────────────────────────────────────── */

static StdrotValue br_draw_text(StdrotValue *args, int argc)
{
    (void)argc;
    DrawText(args[0].val.cstr, args[1].val.i, args[2].val.i, args[3].val.i,
             make_color(args, 4));
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_measure_text(StdrotValue *args, int argc)
{
    (void)argc;
    return (StdrotValue){
        .type = STDROT_INT,
        .val = {.i = MeasureText(args[0].val.cstr, args[1].val.i)}};
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
    for (int i = 0; i < BRAINRAY_MAX_TEXTURES; i++)
    {
        if (!g_texture_used[i])
        {
            g_textures[i] = LoadTexture(args[0].val.cstr);
            g_texture_used[i] = true;
            return (StdrotValue){.type = STDROT_INT, .val = {.i = i}};
        }
    }
    /* Table full: -1 is the "no handle" sentinel. */
    return (StdrotValue){.type = STDROT_INT, .val = {.i = -1}};
}

static StdrotValue br_draw_texture(StdrotValue *args, int argc)
{
    (void)argc;
    int handle = args[0].val.i;
    if (handle >= 0 && handle < BRAINRAY_MAX_TEXTURES && g_texture_used[handle])
    {
        DrawTexture(g_textures[handle], args[1].val.i, args[2].val.i,
                    make_color(args, 3));
    }
    return (StdrotValue){.type = STDROT_NONE};
}

static StdrotValue br_unload_texture(StdrotValue *args, int argc)
{
    (void)argc;
    int handle = args[0].val.i;
    if (handle >= 0 && handle < BRAINRAY_MAX_TEXTURES && g_texture_used[handle])
    {
        UnloadTexture(g_textures[handle]);
        g_texture_used[handle] = false;
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

static const StdrotParam p_unload_texture[] = {P_INT};
STDROT_EXPORT_SIG("rl_unload_texture", br_unload_texture, R_NONE,
                  p_unload_texture, 1, 1, false);
