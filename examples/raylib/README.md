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

```bash
# Debian/Ubuntu
sudo apt-get install libraylib-dev
# macOS
brew install raylib
```

## Build and run

`#cooked <raylib>` resolves the module through the module search path, so point
`$BRAINROT_PATH` at the `brainray/` directory:

```bash
make brainray
BRAINROT_PATH=brainray ./brainrot examples/raylib/ohio_engine.brainrot
```

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
