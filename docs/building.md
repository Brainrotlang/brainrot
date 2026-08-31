# Building & Development Guide

For contributors hacking on the interpreter itself — building from source,
running the full test and quality suite, and the wasm/Windows targets. If you
only want to *install and run* Brainrot, see the
**[Installation Guide](installation.md)** instead.

- [How the build fits together](#how-the-build-fits-together)
- [Developer prerequisites by platform](#developer-prerequisites-by-platform)
- [First build](#first-build)
- [Make targets](#make-targets)
- [Running the tests](#running-the-tests)
- [Formatting & static analysis](#formatting--static-analysis)
- [The wasm build](#webassembly)
- [The Windows build](#windows)
- [Before you open a PR](#before-you-open-a-pr)

---

## How the build fits together

Brainrot is a C interpreter with a Flex/Bison front end:

```
lang.l  (Flex lexer)  ─┐
lang.y  (Bison parser) ─┤─► generated lang.tab.c / lang.tab.h / lex.yy.c
                        │
        ast.c/.h ──► semantic_analyzer.c/.h ──► interpreter.c/.h + visitor.c/.h
                        │
        stdrot/*.c  ──► libstdrot.so   (the standard library / builtins)
        lib/*.c     ──► arena allocator, hashmap, module search path, …
```

- The generated `lang.tab.*` and `lex.yy.c` are **gitignored** — `make`
  regenerates them from `lang.l`/`lang.y`. Never hand-edit or commit them.
- Builtins (`yapping`, `gamba`, file I/O, …) live one family per file under
  `stdrot/`, compiled into `libstdrot.so` and `dlopen`'d at runtime.

There are **three build configurations**, and they differ mainly in how the
standard library is linked:

| Config | Target | Standard library | Native `#cooked` modules |
| --- | --- | --- | --- |
| Native (Linux/macOS) | `make` | separate `libstdrot.so`, `dlopen`'d | `.so` via `dlopen` |
| WebAssembly | `make wasm` | compiled in (`-DSTDROT_STATIC`) | none (no loader) |
| Windows | `make windows` | compiled in (`-DSTDROT_STATIC`) | `.dll` via `LoadLibraryA` |

The `MODULE_NATIVE_LOADER` / `MODULE_NATIVE_SUFFIX` macros in
[`lib/module_path.h`](../lib/module_path.h) are the single source of truth for
"can this build load a native module, and with what extension."

---

## Developer prerequisites by platform

Beyond the runtime build tools, the full dev workflow wants Python (pytest),
Valgrind, `clang-format-15`, and `cppcheck >= 2.13`. Emscripten and MSYS2 are
only needed for the wasm and Windows targets respectively.

### Ubuntu / Debian

```bash
sudo apt-get update
sudo apt-get install \
  gcc flex bison libfl-dev libssl-dev \
  python3 python3-venv valgrind clang-format-15 cppcheck
```

Ubuntu's repo `cppcheck` can be older than the required 2.13 — check with
`cppcheck --version` and build a newer one from source if so (CI uses ≥ 2.13).

### Arch Linux

```bash
sudo pacman -S gcc flex bison openssl python valgrind clang cppcheck
```

### macOS (Homebrew)

```bash
brew install gcc flex bison openssl@3 python cppcheck llvm
```

`make` auto-locates keg-only `openssl@3` and statically links `libcrypto`. If
the link can't find `libfl`, symlink it (see the [Installation
Guide](installation.md#macos-homebrew)). Valgrind is unavailable on recent
macOS — run the Valgrind suite on Linux (or in CI).

### NixOS

```bash
nix develop        # provides the toolchain declared in flake.nix
```

### Optional: wasm and Windows toolchains

- **wasm** — the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
  (`emcc` on your `PATH`) and Node (to run the wasm test harness).
- **Windows** — [MSYS2](https://www.msys2.org/) with
  `pacman -S mingw-w64-x86_64-gcc make bison flex`, driven from the MINGW64
  shell. (Or cross-compile from Linux with a `x86_64-w64-mingw32-gcc`
  toolchain by passing `CC=`.)

---

## First build

```bash
git clone https://github.com/Brainrotlang/brainrot.git
cd brainrot
make            # regenerates lang.tab.*/lex.yy.c, builds libstdrot.so + ./brainrot
./brainrot examples/hello_world.brainrot
```

The default build turns on `-Werror` **and** the address/undefined-behavior
sanitizers (`-fsanitize=address,undefined`), so a warning or a sanitizer report
is a build/test failure, not a suggestion. `make help` lists every target.

---

## Make targets

Run `make help` for the annotated list. The ones that matter day to day:

**Build**

| Target | What it does |
| --- | --- |
| `make` / `make all` | interpreter + `libstdrot.so` (sanitizers on) — the default |
| `make lib` | only `libstdrot.so` |
| `make debug` | rebuild with `-g` (sanitizers on) for GDB |
| `make release` | sanitizer-free, rpath'd build for shipping |
| `make rebuild` | `clean` then a full rebuild |
| `make clean` | remove build artifacts (never touches source) |

**Test & quality** — see the sections below.

| Target | What it does |
| --- | --- |
| `make test` | build, then run the pytest suite |
| `make valgrind` | run every `test_cases/*.brainrot` under Valgrind |
| `make format` / `make format-check` | apply / check `clang-format` |
| `make cppcheck` / `make tidy` | static analysis (cppcheck ≥ 2.13 / clang-tidy-15) |
| `make arena-check` / `make abi-check` | host-side unit tests (allocator; struct/union ABI) |

**Other targets**

| Target | What it does |
| --- | --- |
| `make wasm` / `make wasm-test` | the WebAssembly module / its test build |
| `make windows` / `make windows-test-module` | the native Windows `.exe` / a test module DLL |
| `make rayrot` / `make rayrot-gen` | the raylib binding (hand-written / generated) — see [`rayrot.md`](rayrot.md) |
| `make play` / `make play-gen` | build the binding and run the cursed game (needs raylib + a display) |
| `make install` / `make uninstall` | install under `/usr/local` (needs root) |
| `make check-deps` | verify the required tools are present |

---

## Running the tests

The suite is Python (`pytest`) driving `test_cases/*.brainrot` against the built
interpreter, each with an expected-output entry in
`tests/expected_results.json`.

```bash
make test          # builds first, then runs pytest
```

To run pytest directly (e.g. a single case), use a virtualenv so you match CI:

```bash
python3 -m venv .venv
.venv/bin/pip install -r tests/requirements.txt
cd tests && ../.venv/bin/pytest -v -k <test_case_name>
```

Some tests exercise the native-module loader and the ABI-rejection paths, which
need extra fixture libraries. Build them (as CI does) and point
`STDROT_LIB_PATH` at the test-augmented library:

```bash
make tests/libstdrot.so badnatives nativemodules old-abi-sim
export STDROT_LIB_PATH="$PWD/tests/libstdrot.so"
cd tests && ../.venv/bin/pytest -v
```

> `STDROT_LIB_PATH` is also the way to run `brainrot` from a directory that
> doesn't contain `libstdrot.so` (a plain `make` build has no rpath) — point it
> at the freshly built library.

**Valgrind** (Linux): every fixture must be leak- and error-clean.

```bash
make valgrind
```

---

## Formatting & static analysis

CI blocks on all three — run them before pushing:

```bash
make format-check     # clang-format-15, no diffs allowed (the `lint` job)
make cppcheck         # cppcheck >= 2.13 (the `static-analysis` job)
make tidy             # clang-tidy-15
```

`make format` rewrites files in place to fix formatting. Never hand-format the
generated `lang.tab.*` / `lex.yy.c`.

---

## WebAssembly

```bash
make wasm            # brainrot.wasm + brainrot.mjs (needs emcc on PATH)
```

The wasm build statically links the standard library (`-DSTDROT_STATIC`, no
`libstdrot.so`, no `dlopen`) and takes its source file via `argv[1]` written
into Emscripten's in-memory filesystem before `callMain`. It's what powers the
in-browser playground; `brainrot.wasm`/`brainrot.mjs` keep those exact names so
existing playground URLs keep working.

Sanity-check a wasm build against native:

```bash
node tests/run_wasm_tests.mjs           # same fixtures as pytest, on the wasm build
node tests/run_wasm_examples_check.mjs  # diffs every examples/*.brainrot vs a native run
```

One deliberate platform difference: `sizeof(giga)` is `4`, not `8` — wasm32 is
ILP32 (`long` = 4 bytes) vs native's LP64
([#177](https://github.com/Brainrotlang/brainrot/issues/177)). Everything else,
stderr included, matches native.

`chill()` maps to `sleep()`, which blocks the thread it runs on rather than
yielding — fine in a short CLI run, but a browser embedder (e.g. a playground)
should run it in a Worker and expect the tab to be unresponsive for the
duration, not treat it as an async delay.

---

## Windows

```bash
make windows         # a single self-contained brainrot.exe (MinGW-w64, MSYS2)
```

Like wasm, this is a `-DSTDROT_STATIC` build — the core standard library is
compiled in, so there is no `libstdrot.so`/`dlopen`. `-static` bakes in the
MinGW runtime, so the `.exe` imports only system DLLs and runs on a clean
Windows box. `gamba`'s CSPRNG uses `BCryptGenRandom` (no OpenSSL on Windows).

Native `#cooked <name>` modules **do** work on Windows: they resolve to a
`<name>.dll` and load via `LoadLibraryA`/`GetProcAddress`. A module is an
ordinary DLL (see the `tests/nativemodules/%.dll` rule in the `Makefile`), and
it must sit on `%BRAINROT_PATH%` (`;`-separated on Windows) for `#cooked <name>`
to find it. `make windows-test-module` builds one and the CI `windows` job runs
it end to end. Background: [issue #337](https://github.com/Brainrotlang/brainrot/issues/337).

---

## Before you open a PR

Per [`AGENTS.md`](../AGENTS.md) and [`CONTRIBUTING.md`](../CONTRIBUTING.md):

1. **Tests are mandatory.** Every feature/fix/refactor needs a
   `test_cases/*.brainrot` fixture (plus a `tests/expected_results.json` entry)
   or a host-side C test — happy path, the original bug, error and edge cases.
2. `make test` **and** `make valgrind` both pass.
3. `make format-check` and `make cppcheck` are clean (CI blocks on both).
4. Fill in `.github/PULL_REQUEST_TEMPLATE.md` in full.

Changing existing keyword syntax/semantics in `lang.l`/`lang.y` is a public
compatibility surface (the README keyword table) — discuss it in an issue first.

---

## See also

- [`AGENTS.md`](../AGENTS.md) — the condensed contributor cheat-sheet.
- [`CONTRIBUTING.md`](../CONTRIBUTING.md) — full contribution process.
- [`docs/ROADMAP.md`](ROADMAP.md) — where the language is headed.
- [`docs/rayrot.md`](rayrot.md) — the raylib binding and generator.
