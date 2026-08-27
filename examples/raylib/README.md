# raylib examples

The first cursed game: Brainrot running a real [raylib](https://www.raylib.com/)
game loop through the optional `brainray` native module (Issue #208, Phase 5
"Road A").

## `ohio_engine.brainrot`

A bouncing "ABSOLUTE CINEMA" orb over a dark background with live FPS. Hold
**SPACE** to speed it up; **ESC** or the window's close button quits.

## Requirements

raylib is an **optional** dependency — `make`, `make test`, and `make valgrind`
do not build the module and do not need it installed.

You need a system raylib discoverable through `pkg-config`. Installation is
distribution-dependent (on Ubuntu, `libraylib-dev` is **not** an official
package — use the raylib PPA or a source build); the full, accurate setup for
Ubuntu, macOS, and source builds lives in the one canonical guide:
[`docs/brainray.md`](../../docs/brainray.md#installing-raylib). On macOS it's
just `brew install raylib`. Confirm it worked with:

```bash
pkg-config --exists raylib
```

## Two libraries, not one

`#cooked <raylib>` does **not** load the system `libraylib.so` directly. It
loads `brainray/raylib.so` — a Brainrot native module built by `make brainray`
that wraps raylib. So installing raylib alone is not enough; you also need the
`brainray/raylib.so` wrapper on the module path. See
[`docs/brainray.md`](../../docs/brainray.md#two-libraries-not-one) for the full
picture.

## Build and run

`#cooked <raylib>` resolves the module through the module search path, so point
`$BRAINROT_PATH` at the `brainray/` directory (or run `make play`, which builds
the binding and launches the example in one step):

```bash
make brainray
BRAINROT_PATH=brainray ./brainrot examples/raylib/ohio_engine.brainrot
```

Running `brainrot ohio_engine.brainrot` from inside this directory without
`BRAINROT_PATH` fails: nothing on the default module search path contains
`raylib.so`.

The example exits clean under the default sanitizer build: brainray brackets
raylib's own calls with LeakSanitizer's `__lsan_disable`/`__lsan_enable`, so the
graphics stack's process-lifetime globals aren't reported as leaks while
brainray's and the interpreter's own memory stays checked. See
[`docs/brainray.md`](../../docs/brainray.md#memory-leaks-what-brainray-does-and-doesnt-report).

## Notes

- This example lives in a subdirectory so the top-level `examples/*.brainrot`
  wasm/native comparison check (`tests/run_wasm_examples_check.mjs`) does not
  try to launch a window.
- Colors are passed as four separate `r, g, b, a` integers and textures would be
  integer handles — the native ABI does not carry C structs by value yet.
- Native `cap` (bool) results are landed in a `cap` variable before being used
  in a condition (see `test_cases/native_call_expr.brainrot`).

The full binding reference — every `rl_*` function, the ownership model, and how
to point the same recipe at another C library — is in
[`docs/brainray.md`](../../docs/brainray.md).
