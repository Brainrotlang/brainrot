# `brainray` — raylib bindings for Brainrot

`brainray` is Brainrot's binding to [raylib](https://www.raylib.com/), a simple
C library for videogames. It ships the language's first real native-library
binding and its first cursed game, `examples/raylib/ohio_engine.brainrot`
(Issue #208, Phase 5 "Road A").

It is a **hand-written native module**, not part of the core standard library.
raylib is an **optional dependency**: `make`, `make test`, and `make valgrind`
neither build the module nor need raylib installed.

## Building

You need a real raylib discoverable through `pkg-config`:

```bash
# Debian/Ubuntu
sudo apt-get install libraylib-dev
# macOS
brew install raylib
```

Then build the module (produces `brainray/raylib.so`):

```bash
make brainray
```

## Running the cursed game

`#cooked <raylib>` resolves the module through the module search path, so point
`$BRAINROT_PATH` at the `brainray/` directory:

```bash
BRAINROT_PATH=brainray ./brainrot examples/raylib/ohio_engine.brainrot
```

A window opens with a bouncing "ABSOLUTE CINEMA" orb and live FPS. Hold
**SPACE** to speed it up; **ESC** or the close button quits.

## How it works — the Road A ABI trick

The Brainrot native ABI marshals scalars, C-strings, pointers, and bools, but
**not C structs by value**. raylib is full of small structs (`Color`,
`Vector2`, `Texture2D`, …), so every wrapper takes those aggregates apart into
scalar arguments and rebuilds them on the C side:

- A raylib **`Color`** becomes four `rizz` (int) arguments `r, g, b, a`. So
  `ClearBackground(BLACK)` in C is `rl_clear_background(0, 0, 0, 255)` in
  Brainrot.
- A **`Texture2D`** cannot cross the boundary and outlives any single
  statement, so C keeps it in an internal table and Brainrot only ever holds an
  integer **handle** (its index). `rl_load_texture` returns a handle (or `-1`
  if the table is full); `rl_draw_texture` / `rl_unload_texture` take one back.

By-value struct passing and a `raylib_api.json`-driven **generated** binding are
"Road B" — a separate follow-up that needs an ABI extension this module
deliberately sidesteps.

## Function reference

Colors are always the trailing `r, g, b, a` integers (0–255, clamped).

| Brainrot | raylib | Notes |
| --- | --- | --- |
| `rl_init_window(width, height, title)` | `InitWindow` | `title` is a string |
| `rl_window_should_close()` | `WindowShouldClose` | returns `cap` (bool) |
| `rl_close_window()` | `CloseWindow` | |
| `rl_set_target_fps(fps)` | `SetTargetFPS` | |
| `rl_get_screen_width()` | `GetScreenWidth` | returns `rizz` |
| `rl_get_screen_height()` | `GetScreenHeight` | returns `rizz` |
| `rl_begin_drawing()` | `BeginDrawing` | |
| `rl_end_drawing()` | `EndDrawing` | |
| `rl_clear_background(r, g, b, a)` | `ClearBackground` | |
| `rl_get_frame_time()` | `GetFrameTime` | returns `chad` (float) seconds |
| `rl_draw_fps(x, y)` | `DrawFPS` | |
| `rl_draw_circle(x, y, radius, r, g, b, a)` | `DrawCircle` | `radius` is `chad` |
| `rl_draw_rectangle(x, y, w, h, r, g, b, a)` | `DrawRectangle` | |
| `rl_draw_line(x1, y1, x2, y2, r, g, b, a)` | `DrawLine` | |
| `rl_draw_text(text, x, y, size, r, g, b, a)` | `DrawText` | `text` is a string |
| `rl_measure_text(text, size)` | `MeasureText` | returns `rizz` width |
| `rl_is_key_down(key)` | `IsKeyDown` | returns `cap`; `key` is a keycode int |
| `rl_is_key_pressed(key)` | `IsKeyPressed` | returns `cap` |
| `rl_load_texture(path)` | `LoadTexture` | returns integer handle or `-1` |
| `rl_draw_texture(handle, x, y, r, g, b, a)` | `DrawTexture` | `r,g,b,a` = tint |
| `rl_unload_texture(handle)` | `UnloadTexture` | |

### Key codes

`rl_is_key_down` / `rl_is_key_pressed` take a raw raylib integer keycode. Until
a generator can emit `KEY_*` as Brainrot constants (Road B), pass the integer
directly. Common ones:

| Key | Code |
| --- | --- |
| SPACE | 32 |
| ESCAPE | 256 |
| RIGHT | 262 |
| LEFT | 263 |
| DOWN | 264 |
| UP | 265 |

## Pointing it at another library later

`brainray` is one native module built from one hand-written `.c` file linked
against one C library. Any other C library follows the same recipe: a new
`brainX/` directory with a `<lib>.c` of `STDROT_EXPORT_SIG` wrappers, a `.so`
built with `-DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init`, and a
`#cooked <lib>`. Road B replaces the hand-written wrappers with generated ones.
