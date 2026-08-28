# `brainray` — raylib bindings for Brainrot

`brainray` is Brainrot's binding to [raylib](https://www.raylib.com/), a simple
C library for videogames. It ships the language's first real native-library
binding and its first cursed game, `examples/raylib/ohio_engine.brainrot`
(Issue #208, Phase 5 "Road A").

It is a **hand-written native module**, not part of the core standard library.
raylib is an **optional dependency**: `make`, `make test`, `make valgrind`, and
`make wasm` neither build the module nor need raylib installed. Only
`make brainray` (and the `make play` convenience target) require it.

> **This page is the single source of truth for raylib setup.** The README,
> the Makefile's `brainray` error message, and `examples/raylib/README.md` all
> point here rather than repeating install steps.

## Two libraries, not one

There are **two different shared libraries** involved, and they are not
interchangeable:

| File | What it is | Who builds it |
| --- | --- | --- |
| `libraylib.so` | The **system raylib** C library | Your OS package / a source build of raylib |
| `brainray/raylib.so` | Brainrot's **native module** wrapping it | `make brainray` |

`#cooked <raylib>` does **not** load the system's `libraylib.so` directly.
Brainrot searches its module path for a `raylib.brainrot` file or a `raylib.so`
native module — so it needs the wrapper that `make brainray` produces:

```text
examples/raylib/ohio_engine.brainrot
        |  #cooked <raylib>   (Brainrot searches $BRAINROT_PATH for raylib.so)
        v
brainray/raylib.so            <-- built by `make brainray`
        |  raylib C API
        v
libraylib.so                  <-- the system raylib you install below
```

Installing `libraylib.so` alone is therefore **not** enough to run the example:
without `brainray/raylib.so`, `#cooked <raylib>` cannot resolve the module.

## Installing raylib

`make brainray` finds raylib through **`pkg-config`**, so whatever route you
pick must leave a working `raylib.pc`. Verify at any point with:

```bash
pkg-config --exists raylib && pkg-config --modversion raylib
```

### Ubuntu

There is **no `libraylib-dev` package in the official Ubuntu repositories** on
current releases (22.04 Jammy, 24.04 Noble) — `sudo apt-get install
libraylib-dev` fails with *"Unable to locate package libraylib-dev"*. Do not use
it on Ubuntu. Pick one of these instead. (Debian is different — see below.)

**Option A — the raylib PPA (quickest, recommended):** the community
`ppa:texus/raylib` provides version-numbered `libraylib<N>-dev` packages that
ship `raylib.pc`. The package name tracks the raylib series, and which series a
release carries differs — 22.04 Jammy currently has only `libraylib5-dev`
(raylib 5.x), while 24.04 Noble also publishes `libraylib6-dev` (raylib 6.x).
List what the PPA built for *your* release and install the newest it offers:

```bash
sudo add-apt-repository ppa:texus/raylib
sudo apt-get update
apt-cache search libraylib            # e.g. libraylib5-dev and/or libraylib6-dev
sudo apt-get install libraylib5-dev   # or libraylib6-dev where available
```

brainray only uses long-stable primitives, so any of these series is fine.

**Option B — build the latest raylib from source.** Install the build
dependencies, then build raylib **with CMake** (its CMake install is what
generates `raylib.pc`; raylib's plain `make install` does *not*, so pkg-config
would not find it):

```bash
sudo apt-get install build-essential git cmake libasound2-dev libx11-dev \
    libxrandr-dev libxi-dev libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev \
    libxinerama-dev libwayland-dev libxkbcommon-dev
git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git
cd raylib
cmake -B build -DBUILD_SHARED_LIBS=ON -DBUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
sudo ldconfig
```

Three things that will otherwise bite you (all verified against the `6.0` tag):

- **Pin a release tag** (`--branch 6.0`), not `master`. `master` is a
  development branch (`6.1-dev`) and can drift.
- **`-DBUILD_EXAMPLES=OFF` is required.** With the examples on, raylib's bundled
  demos fail to link — a private-dependency generator expression leaks a bare
  `-lglfw` / `$<BUILD_INTERFACE:pthread` onto the linker command line and the
  build dies before install. `-DBUILD_EXAMPLES=OFF` builds and installs the
  library and `raylib.pc` cleanly; you don't need the demos.
- raylib 6.0's CMake needs **CMake ≥ 3.25**. Ubuntu 22.04's apt `cmake` is 3.22
  and will refuse to configure — install a newer one first
  (`pip install --user cmake` or `sudo snap install cmake --classic`), or just
  use Option A.

### Debian

Debian is **not** Ubuntu here: Debian **testing** and **unstable** ship an
official `libraylib-dev` (raylib 6.x, `6.0+ds-2`, in `main`), which installs a
`raylib.pc`. On those releases the plain apt command is correct:

```bash
sudo apt install libraylib-dev
```

On Debian **stable** (bookworm), `libraylib-dev` is not available — use the
source build from Option B above (the CMake steps are distro-independent).

### macOS (Homebrew)

Homebrew's raylib ships a `raylib.pc`, so this just works:

```bash
brew install raylib
```

### Other platforms

Any raylib install is fine as long as `pkg-config --exists raylib` succeeds. The
official [raylib wiki](https://github.com/raysan5/raylib/wiki) covers Windows,
BSD, and prebuilt release binaries.

## Building the binding

Once `pkg-config --exists raylib` passes, build the module (produces
`brainray/raylib.so`):

```bash
make brainray
```

If raylib is not found, `make brainray` fails fast and points back to this page.

## Running the cursed game

`#cooked <raylib>` resolves the module through the module search path, so point
`$BRAINROT_PATH` at the `brainray/` directory. The canonical workflow from a
source checkout is:

```bash
# 1. install raylib for your OS (see above) and confirm pkg-config sees it
pkg-config --exists raylib
# 2. build the interpreter and the binding
make
make brainray
# 3. run the example (or just `make play`, which does 2+3)
BRAINROT_PATH=brainray ./brainrot examples/raylib/ohio_engine.brainrot
```

A window opens with a bouncing "ABSOLUTE CINEMA" orb and live FPS. Hold
**SPACE** to speed it up; **ESC** or the close button quits.

`BRAINROT_PATH` is required because Brainrot has to find `raylib.so` on its
module path. Running `./brainrot examples/raylib/ohio_engine.brainrot` **without**
it — or `cd examples/raylib && brainrot ohio_engine.brainrot` — fails with a
module-not-found error, because nothing on the default search path contains
`raylib.so`. If you `make install` Brainrot globally, the interpreter also
searches its install module directory, **`/usr/local/lib/brainrot`**. Note that
`make install` only installs `brainrot` and `libstdrot.so` (under
`/usr/local/{bin,lib}`) — it does **not** create or populate
`/usr/local/lib/brainrot`. To use the binding from an installed Brainrot, create
that directory and copy the module in yourself:

```bash
sudo install -Dm755 brainray/raylib.so /usr/local/lib/brainrot/raylib.so
```

Or just keep pointing `BRAINROT_PATH` at a directory that holds `raylib.so` so
`#cooked <raylib>` resolves.

## Memory leaks: what brainray does and doesn't report

The default `brainrot` is built with `-fsanitize=address`, so LeakSanitizer
checks for leaks at exit. raylib and the libraries it drives (GLFW, the GL
driver / Mesa, X11, fontconfig) allocate process-lifetime global state — a GL
context, the default font and shader, X11 and font caches — that they never
return to the allocator; the OS reclaims it when the process ends. Reported
verbatim, that would be a wall of "leaks" ([#267](https://github.com/Brainrotlang/brainrot/issues/267))
that no application can free and that brainray does not own.

So brainray brackets each raylib call with LeakSanitizer's allocator-scoped
`__lsan_disable()` / `__lsan_enable()` (see `brainray/raylib.c`): allocations
made *inside* a raylib call are excluded from the leak report on purpose, as
unowned graphics-stack state. This is deliberately narrow — **everything
brainray itself allocates stays fully checked**. The window-title copy and the
texture table are allocated outside those brackets, so a real leak in the
binding (say, forgetting to free the title in `rl_close_window`) is still
reported. The controls are weak symbols, inert when the interpreter carries no
sanitizer (`make release`), so the module loads either way.

The upshot: running the example — `make play`, or the plain command below —
exits clean under the default sanitizer build, with leak checking still live for
brainray's and the interpreter's own memory:

```bash
BRAINROT_PATH=brainray ./brainrot examples/raylib/ohio_engine.brainrot
```

(An LSan *suppression file* is not used here: under the default fast unwind the
leak stacks into the non-instrumented raylib `.so` are unsymbolized, so
name-based suppressions can't match them anyway. Bracketing the calls tags the
allocations at their source instead, which is reliable regardless of unwind.)

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
  integer **handle** (its index). `rl_load_texture` returns a handle, or `-1`
  if the load failed (missing/undecodable file) **or** the 256-slot table is
  full — a non-negative handle therefore always refers to a successfully loaded
  texture. `rl_draw_texture` / `rl_unload_texture` take one back.

  A live handle also implies a live GL context: `rl_close_window` unloads every
  still-live texture before the context is destroyed, so a handle from before a
  window was closed is never usable afterward. The handle is a plain index with
  **no generation counter** (Road A stays crude), so after `rl_unload_texture`
  the index is recycled — don't keep using a handle past its unload, or it will
  silently alias whatever loads into that slot next.

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
| `rl_draw_text_int(text, value, pad, x, y, size, r, g, b, a)` | `DrawText` | draws `text` followed by `value`; see below |
| `rl_measure_text_int(text, value, pad, size)` | `MeasureText` | returns `rizz` width of the same string |
| `rl_is_key_down(key)` | `IsKeyDown` | returns `cap`; `key` is a keycode int |
| `rl_is_key_pressed(key)` | `IsKeyPressed` | returns `cap` |
| `rl_load_texture(path)` | `LoadTexture` | returns integer handle or `-1` |
| `rl_draw_texture(handle, x, y, r, g, b, a)` | `DrawTexture` | `r,g,b,a` = tint |
| `rl_unload_texture(handle)` | `UnloadTexture` | |

### Drawing a number

Brainrot has no string concatenation and no `sprintf`, so `rl_draw_text` — which
only ever receives a literal — cannot render a score, a level or a countdown.
`rl_draw_text_int` closes that gap without opening a format-string hole: the
formatting is fixed at *one literal prefix followed by exactly one integer*, so
a Brainrot-supplied `"%s"` can never make the host read an argument that isn't
there.

```c
rl_draw_text_int("SCORE ", score, 6, 20, 20, 28, 245, 200, 90, 255);
```

`pad` is the minimum **field width** of the number, zero-padded — printf's
`%0*d`, not a count of digits:

| `value` | `pad` | drawn |
| --- | --- | --- |
| `450` | `0` | `SCORE 450` |
| `450` | `6` | `SCORE 000450` |
| `-450` | `6` | `SCORE -00450` |
| `0` | `6` | `SCORE 000000` |

The difference shows on negatives: `-450` at `pad 6` is six columns holding
five digits and a sign, not six digits plus a sign. Field width is the rule you
want here — keeping the column steady is what stops a HUD jittering as a score
grows, and `000450` and `-00450` take the same space.

`pad` is clamped to 32 so a wild value can't request an enormous allocation, and
an empty `text` is fine if you want the bare number.

`rl_measure_text_int` takes the same `text`, `value` and `pad` and reports the
width of the string the pair would draw, so numeric text can be centred the same
way a literal can:

```c
rizz w = rl_measure_text_int("FINAL SCORE ", score, 0, 36);
rl_draw_text_int("FINAL SCORE ", score, 0, cx - w / 2, 320, 36, 245, 200, 90, 255);
```

Unlike every other wrapper here, these two allocate: they build the joined
string on the heap and free it before returning. That allocation deliberately
happens *outside* the LeakSanitizer brackets described above, so a missed
`free()` here is still reported as brainray's own leak — and
`test_brainray_windowed_run_is_leak_clean` exercises both functions for exactly
that reason.

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
