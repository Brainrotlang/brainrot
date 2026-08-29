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
  texture. `rl_draw_texture`, `rl_draw_texture_rec` and
  `rl_unload_texture` take one back — every function that consumes a handle
  validates it the same way, so this list is the complete census and any new
  handle-taking wrapper belongs in it.

  A live handle also implies a live GL context: `rl_close_window` unloads every
  still-live texture before the context is destroyed, so a handle from before a
  window was closed is never usable afterward. The handle is a plain index with
  **no generation counter** (Road A stays crude), so after `rl_unload_texture`
  the index is recycled — don't keep using a handle past its unload, or it will
  silently alias whatever loads into that slot next.

This module keeps that flattened, hand-written shape on purpose. The
`raylib_api.json`-driven **generated** binding is Road B, and it lives
alongside rather than replacing it — see the next section.

## Road B — the generated binding

Everything above is Road A: ~20 wrappers, written by hand, scalars only.
raylib has **617** functions. Writing that many by hand is how this project
dies, so `brainray/brainray_gen.py` writes them instead:

```text
brainray/raylib_api.json → brainray-gen → { raylibgen_native.c    C adapters + descriptors
                                            raylibgen.brainrot    gang types + gyatt constants
                                            raylibgen_abi_check.c _Static_assert layout tests }
```

```bash
make brainray-gen-sources   # generate (Python + the pinned JSON; no raylib)
make brainray-gen           # + compile, and run the ABI drift check
make play-gen               # + run examples/raylib/ohio_engine_gen.brainrot
```

Current output: **378 of 617 functions, 16 of 35 struct types, 305
constants.**

### It coexists with Road A

Road B's binding is a *separate module under a separate name*
(`#cooked <raylibgen>`, in `brainray/generated/`), so Road A's
`brainray/raylib.so` and its `#cooked <raylib>` keep working untouched. Both
export `rl_`-prefixed names, so loading both at once would be rejected as a
duplicate export — use one or the other.

### Two halves, and why there's no new ABI for types

`#cooked <raylibgen>` resolves to the generated **prelude**, not to a `.so`: a
`<name>.brainrot` wins over a `<name>.so` in the same directory
(`module_path_resolve()`), and a prelude may itself `#cooked` a native module.
So the binding is split in two:

| Half | Artifact | Carries |
| --- | --- | --- |
| Types + constants | `raylibgen.brainrot` (prelude) | `gang Vector2 { chad x; chad y; }`, `gyatt KeyboardKey { KEY_SPACE = 32, ... }` |
| Functions | `raylibgen_native.so` | 378 `STDROT_EXPORT_SIG` adapters |

That split is why Road B needed **no** ABI extension for types or constants.
`StdrotAPI` carries a function table and nothing else, and Phase 4 left
type/constant registration deliberately undesigned — the prelude sidesteps the
question entirely by shipping them as ordinary generated Brainrot source.

### Layout correctness is testable, so it's tested

The binding memcpy's Brainrot `gang` bytes straight into real C structs, which
is only sound if three independent things agree about layout. Each pair is
checked:

| Pair | Where |
| --- | --- |
| generator's model ↔ real raylib headers | `raylibgen_abi_check.c` — `_Static_assert` on `sizeof`, `_Alignof`, and every `offsetof`. Building it *is* the check. |
| generator's model ↔ Brainrot's `compute_struct_layout()` | `tests/test_brainray_gen.py` — runs the interpreter, compares `maxxing()` per type. Needs no raylib. |
| generator's model ↔ known-good constants | `tests/test_brainray_gen.py` — hardcoded raylib layouts, so a generator bug can't agree with itself. |

### What it deliberately leaves out

Every skip is counted and printed on each run, and `--strict` (which
`make brainray-gen-sources` uses) fails the build on any skip reason that
isn't one of these known gaps — so an upstream schema change is a red build,
not a quietly smaller binding.

| Skipped | Count | Why |
| --- | --- | --- |
| struct returns | 113 | `STDROT_STRUCT` is argument-direction only; returning an aggregate needs an ownership model (roadmap Appendix B Q6). This is the single biggest cost — it drops every `Vector2 GetMousePosition(void)`-shaped function. |
| unsupported param types | 102 | Mostly `Image`/`Font`/`Model`/`Sound`/`Shader` — structs that own or point at memory, i.e. resource handles in disguise (Q6 again). |
| structs with pointer/array fields | 19 | Same reason; not emitted as by-value `gang`s. |
| `const char *` returns | 14 | No return-side marshalling exists for a C string. |
| callback params | 7 | Function pointers aren't expressible in the ABI yet (`npc`, Phase 9b). |
| varargs | 2 | `TraceLog`, `TextFormat`. |

### Known fidelity note: unsigned bytes

Brainrot has no unsigned struct-field spelling (`nonut yap r;` is a parse
error), so `Color`'s `unsigned char` components are emitted as signed `yap`.
The **bytes are correct** — that's what crosses the ABI, and raylib sees
exactly what you set — but reading a component back in Brainrot shows the
signed interpretation: `col.r = 200;` then printing `col.r` gives `-56`.
Widening the field to `rizz` would fix the display and break the layout, so
this is documented rather than worked around.

### Pointing the generator at a different library

`brainray_gen.py` has no raylib-specific logic beyond its CLI defaults. It
needs a JSON description with `functions` (`name`/`returnType`/`params`),
`structs` (`name`/`fields`), `enums` (`name`/`values`), and optionally
`aliases`/`callbacks` — then:

```bash
python3 brainray/brainray_gen.py --api path/to/sdl_api.json \
    --outdir brainsdl/generated --header SDL3/SDL.h \
    --module-name sdlgen --library SDL3 --strict
```

The pieces you should not need to touch are the scalar map, the layout
computation, the emitters, and the skip bookkeeping. If a new library forces a
change there, that's a bug in the abstraction, not in the library.

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
| `rl_draw_texture_rec(handle, sx, sy, sw, sh, x, y, r, g, b, a)` | `DrawTextureRec` | one sub-rectangle; see below |
| `rl_unload_texture(handle)` | `UnloadTexture` | |
| `rl_init_audio_device()` | `InitAudioDevice` | see below |
| `rl_is_audio_device_ready()` | `IsAudioDeviceReady` | returns `cap` |
| `rl_close_audio_device()` | `CloseAudioDevice` | unloads every live stream first |
| `rl_load_music(path)` | `LoadMusicStream` | streamed; returns handle or `-1` |
| `rl_play_music(handle)` | `PlayMusicStream` | |
| `rl_update_music(handle)` | `UpdateMusicStream` | **every frame, or it stops** |
| `rl_stop_music(handle)` | `StopMusicStream` | |
| `rl_set_music_volume(handle, vol)` | `SetMusicVolume` | `vol` is `chad`, 1.0 is full |
| `rl_set_music_looping(handle, on)` | `music.looping` | `on` is `cap`; raylib defaults to true |
| `rl_is_music_playing(handle)` | `IsMusicStreamPlaying` | returns `cap` |
| `rl_music_time_played(handle)` | `GetMusicTimePlayed` | returns `chad` seconds |
| `rl_music_time_length(handle)` | `GetMusicTimeLength` | returns `chad` seconds |
| `rl_unload_music(handle)` | `UnloadMusicStream` | |
| `rl_load_sound(path)` | `LoadSound` | decoded whole; returns handle or `-1` |
| `rl_play_sound(handle)` | `PlaySound` | |
| `rl_is_sound_playing(handle)` | `IsSoundPlaying` | returns `cap` |
| `rl_set_sound_volume(handle, vol)` | `SetSoundVolume` | `vol` is `chad` |
| `rl_unload_sound(handle)` | `UnloadSound` | |

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

### Sprite atlases and flipping

`rl_draw_texture` blits a whole image, which means one file per animation frame
and no way to face a sprite the other direction. `rl_draw_texture_rec` takes a
source rectangle instead, so a single texture can hold a strip of frames:

```c
🚽 frame `i` of a 64x64 strip, drawn at (px, py)
rl_draw_texture_rec(atlas, i * 64.0, 0.0, 64.0, 64.0, px, py, 255, 255, 255, 255);
```

Two aggregates come apart here rather than one: raylib's `rec` is a `Rectangle`
(four floats) and `position` is a `Vector2` (two floats), so the source box is
`sx, sy, sw, sh` and the destination corner is `x, y` — six `chad` arguments,
then the usual four-int `Color`.

Note that this position is **floating point**, where `rl_draw_texture`'s `x, y`
are integers. That mirrors raylib rather than contradicting it: `DrawTexture`
takes ints and `DrawTextureRec` takes a `Vector2`. Sub-pixel placement is what
a smoothly scrolling background wants anyway.

**A negative `sw` mirrors the sprite horizontally, and a negative `sh` mirrors
it vertically** — raylib's own idiom, since the sign of the source rectangle
decides the order of the texture coordinates:

```c
🚽 same frame, facing the other way
rl_draw_texture_rec(atlas, 0.0, 0.0, -64.0, 64.0, px, py, 255, 255, 255, 255);
```

Until `DrawTexturePro`'s scaling and rotation are exposed, that is the only way
to flip a sprite, so it is worth knowing rather than looking like a bug.

The rectangle is passed to raylib unchanged: brainray does **not** clamp it to
the texture, so what happens when it reaches outside the image is raylib's
behaviour and not a guarantee this binding makes. Handle validation matches
`rl_draw_texture` — an out-of-range, negative, or already-unloaded handle draws
nothing rather than handing raylib a stale GPU id.

### Audio

Nothing here does anything until `rl_init_audio_device()` has succeeded — and
raylib does not tell you when it hasn't. On a machine with no sound device, or
in a container, `InitAudioDevice()` logs a warning and carries on, after which
every load and every play silently does nothing. `rl_is_audio_device_ready()` is
the only way a Brainrot program can tell *playing quietly* from *not playing*:

```c
rl_init_audio_device();
cap ready = rl_is_audio_device_ready();
edgy (!ready) { yapping("no audio device; running silent"); }
```

#### Music must be pumped every frame

`rl_update_music()` refills the decode buffer. **Miss it and the track plays for
a fraction of a second and stops**, with no error and no diagnostic — it is the
one contract in this binding that fails silently and looks like a broken file.

```c
rizz track = rl_load_music("assets/music/tung-tung.ogg");
rl_set_music_looping(track, W);
rl_play_music(track);

goon (running) {
    rl_update_music(track);      🚽 every frame, not once
    ...
}
```

Measured rather than asserted: over the same two seconds of wall clock, a pumped
stream reported 1.60 s of playback and an unpumped one reported 0.00 s. That
comparison is `test_brainray_audio_stream_advances_only_when_pumped`.

#### Music or Sound is a real choice

| | `rl_load_music` | `rl_load_sound` |
| --- | --- | --- |
| Decoding | streamed, a buffer at a time | whole file into memory at load |
| Needs pumping | **yes**, every frame | no |
| Suits | a background track | a short one-shot, like the bat connecting |

An 80-second stereo track loaded as a `Sound` is tens of megabytes of PCM; a
one-shot loaded as `Music` is a stream you have to remember to pump. Neither
mistake produces an error, which is why the distinction is spelled out here
rather than left to the function names.

#### Handles

Same model as textures. `Music` and `Sound` are structs that cannot cross the
ABI, so C owns them and Brainrot holds an integer index — 16 music slots, 64
sound slots. A failed load returns `-1` without consuming a slot, so a
non-negative handle always means a decodable file.

Both loaders refuse before they reach raylib if `rl_is_audio_device_ready()` is
false, so a non-negative handle also always implies a **live device**. Without
that gate `LoadMusicStream` can report a frame count from the file while the
playback stream behind it was never attached, and a later query would reach for
a miniaudio mutex that `CloseAudioDevice` had already destroyed.

`rl_close_audio_device` unloads every stream this module still owns before the
device goes away, exactly as `rl_close_window` does for textures.

As with textures the handle is a plain index with **no generation counter**, so
after an unload the index is **recycled** — a stale handle silently aliases
whatever loads into that slot next. Don't keep using a handle past its unload.
(Until something else takes the slot the wrappers do check ownership and do
nothing, but that is a temporary courtesy, not the contract.)

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
built with `-DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init_v3`, and a
`#cooked <lib>`. That is the Road A recipe, and it is the right one for a
handful of primitives.

For anything larger, generate instead — see "Pointing the generator at a
different library" above. The generator is the reusable part of this whole
directory; `raylib_api.json` is just its first input.
