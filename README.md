# Brainrot Programming Language

[![license](https://img.shields.io/badge/license-GPL-green)](https://raw.githubusercontent.com/araujo88/brainrot/main/LICENSE)
[![CI](https://github.com/araujo88/brainrot/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/araujo88/brainrot/actions/workflows/ci.yml)

Brainrot is a meme-inspired programming language that translates common programming keywords into internet slang and meme references. It's built using Flex (lexical analyzer) and Bison (parser generator), making it a fun way to learn about language processing and compiler design.

## History

The TRUE history behind the Brainrot programming language can be found [here](TRUTH.md).

## 🤔 What is Brainrot?

Brainrot is a C-like programming language where traditional keywords are replaced with popular internet slang. For example:

- `void` → `skibidi`
- `int` → `rizz`
- `for` → `flex`
- `return` → `bussin`

## 📋 Requirements

To build and run the Brainrot compiler, you'll need:

- GCC (GNU Compiler Collection)
- Flex (Fast Lexical Analyzer)
- Bison (Parser Generator)
- OpenSSL (`libcrypto`) — a **required** dependency of the standard library
  (`libstdrot.so`), which uses it for the cryptographically safe `gamba()` RNG.
  You need the development headers to build, and the runtime library
  (`libcrypto`) to run the interpreter on Linux. Building without OpenSSL is a
  failed link, not a `gamba`-less interpreter.

### Installation on Different Platforms

#### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install gcc flex bison libfl-dev libssl-dev
```

#### Arch Linux

```bash
sudo pacman -S gcc flex bison openssl
```

#### macOS (using Homebrew)

```bash
brew install gcc flex bison openssl@3
```

Homebrew's OpenSSL is keg-only, but `make` locates it automatically (via
`brew --prefix openssl@3`) and **statically** links `libcrypto` into
`libstdrot.so`, so the built standard library is self-contained.

Some macOS users are experiencing an error related to `libfl`. First, check if `libfl` is installed at:

```
/opt/homebrew/lib/libfl.dylib  # For Apple Silicon
/usr/local/lib/libfl.dylib  # For Intel Macs
```

And if not, you have to find it and symlink to it. Find it using:

```
find /opt/homebrew -name "libfl.*"  # For Apple Silicon
find /usr/local -name "libfl.*"  # For Intel Macs
```

And link it with:

```
sudo ln -s /path/to/libfl.dylib /opt/homebrew/lib/libfl.dylib  # For Apple Silicon
sudo ln -s /path/to/libfl.dylib /usr/local/lib/libfl.dylib  # For Intel Macs
```

#### For NixOS

```bash
git clone https://github.com/Brainrotlang/brainrot.git
cd brainrot
nix develop
# then create your .brainrot file
./result/bin/brainrot filename.brainrot
```

Or via a flake-based NixOS config (`/etc/nixos/flake.nix`), which always tracks the latest version:

```nix
# /etc/nixos/flake.nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    brainrot.url = "github:Brainrotlang/brainrot";
  };

  outputs = { nixpkgs, brainrot, ... }: {
    nixosConfigurations.your-hostname = nixpkgs.lib.nixosSystem {
      modules = [
        ./configuration.nix
        {
          # For a specific user
          users.users.username = {
            packages = [ brainrot.packages.x86_64-linux.default ];
          };
          # For system-wide installation
          environment.systemPackages = [
            brainrot.packages.x86_64-linux.default
          ];
        }
      ];
    };
  };
}
```

Run `nix flake update` whenever you want to pull the latest version, then rebuild with `sudo nixos-rebuild switch`.

## 🚀 Building the Compiler

1. Clone this repository:

```bash
git clone https://github.com/Brainrotlang/brainrot.git
cd brainrot
```

2. Generate the parser and lexer:

```bash
bison -d -Wcounterexamples lang.y -o lang.tab.c
flex -o lang.lex.c lang.l
```

3. Compile the compiler:

```bash
make
```

NOTE: The gcc version we use to test is v13 if you get any warnings remove `-Werror` flag from the Makefile

## Installation

```bash
sudo make install
```

### Prebuilt binaries

Each `v*` GitHub release attaches native archives plus the wasm module:

- `brainrot-<tag>-linux-amd64.tar.gz`, `linux-arm64`,
  `darwin-amd64`, `darwin-arm64` -- `brainrot` and `libstdrot.so`.
  Extract both files and run `./brainrot file.brainrot` from that
  directory. At startup the interpreter honors `STDROT_LIB_PATH` if set;
  otherwise it tries cwd-relative `./libstdrot.so`, then the dynamic
  linker's `libstdrot.so` search path. Release binaries include an rpath
  (`$ORIGIN` / `@loader_path`) so that second lookup can find the
  `libstdrot.so` next to `brainrot` when no cwd copy is loaded first.
- `brainrot.wasm` and `brainrot.mjs` -- same names as previous wasm-only
  releases, for the in-browser playground.
- `SHA256SUMS.txt` -- checksums for every attached file.

Windows is not a native target (`dlopen` / POSIX stdrot). A release can
also be cut from the Actions UI with a patch/minor/major bump over the
latest stable tag.

## Uninstall

```bash
sudo make uninstall
```

## 🌐 WebAssembly build

`make wasm` builds `brainrot.wasm` + `brainrot.mjs` using [Emscripten](https://emscripten.org/) — this is what powers the in-browser playground. Requires `emcc` on your `PATH` (install via [emsdk](https://emscripten.org/docs/getting_started/downloads.html)):

```bash
make wasm
```

The interpreter is statically linked for this target (no `libstdrot.so`, no `dlopen` — see `stdrot.c`'s `STDROT_STATIC` path), and takes its source file the same way the native binary does, via `argv[1]` written into Emscripten's in-memory filesystem before calling `callMain`.

Known platform-specific difference from the native build: `sizeof(giga)` is `4`, not `8` — wasm32 uses the ILP32 data model (`long` = 4 bytes) instead of native's LP64 ([#177](https://github.com/Brainrotlang/brainrot/issues/177)). Everything else, including stderr, matches native exactly.

`node tests/run_wasm_tests.mjs` runs the same fixtures as the native `pytest` suite against the wasm build (checking the one difference above against its wasm-correct value instead of native's), and `node tests/run_wasm_examples_check.mjs` diffs every `examples/*.brainrot` program's stdout against a real native run. Run both after `make && make wasm` to sanity-check a build.

## 🪟 Native Windows build

`make windows` builds a single, statically-linked `brainrot.exe` with the [MinGW-w64](https://www.mingw-w64.org/) toolchain (via [MSYS2](https://www.msys2.org/): `pacman -S mingw-w64-x86_64-gcc make bison flex`, from the *MINGW64* shell):

```bash
make windows
```

Like the wasm target, this is the `STDROT_STATIC` build — the core `stdrot` library is compiled straight into the binary, with no `libstdrot.so`/`dlopen`. It runs pure-Brainrot programs, `#cooked` **prelude** modules, **and** `#cooked <name>` **native modules**: those resolve to a `<name>.dll` and load via `LoadLibraryA`/`GetProcAddress` ([issue #337](https://github.com/Brainrotlang/brainrot/issues/337) Stages 1–2). A native module built for Windows is an ordinary `.dll` (see the `.dll` rule in the `Makefile`); it must be on `$BRAINROT_PATH` (`;`-separated on Windows) for `#cooked <name>` to find it. `gamba`'s CSPRNG is backed by `BCryptGenRandom` here (no OpenSSL dependency on Windows).

`chill()` calls `sleep()` under the hood, which blocks the JS thread it runs on rather than yielding — fine in a short-lived CLI run, but something a browser embedder (e.g. a playground) should account for (run in a Worker, expect the tab to be unresponsive for the duration) rather than assume it behaves like an async delay.

## 💻 Usage

1. Create a Brainrot source file (e.g., `hello.brainrot`):

```c
 skibidi main {
    yapping("Hello, World!");
    bussin 0;
}
```

2. Run your Brainrot program:

```bash
./brainrot hello.brainrot
```

Check out the [examples](examples/README.md):

- [Hello world](examples/hello_world.brainrot)
- [Fizz Buzz](examples/fizz_buzz.brainrot)
- [Bubble Sort](examples/bubble_sort.brainrot)
- [One-dimensional Heat Equation Solver](examples/heat_equation_1d.brainrot)
- [Fibonacci Sequence](examples/fibonacci.brainrot)
- [Modules (`#cooked`)](examples/modules.brainrot)
- [Named modules (`#cooked <name>`)](examples/modules_named.brainrot)
- [Ohio Engine — the first cursed game (raylib)](examples/raylib/ohio_engine.brainrot)
- [Ohio Engine II — the same loop through a *generated* binding](examples/raylib/ohio_engine_gen.brainrot)

### 🎮 The first cursed game

Yes, the joke language runs a real game loop. `examples/raylib/ohio_engine.brainrot`
calls [raylib](https://www.raylib.com/) through the optional `rayrot` native
module (`#cooked <raylib>`) to bounce an "ABSOLUTE CINEMA" orb around a window.

raylib is an **optional** dependency (not needed for `make`/`make test`), and
`#cooked <raylib>` loads the `rayrot/raylib.so` wrapper built by `make
rayrot` — **not** the system `libraylib.so` directly. First install a system
raylib (on Ubuntu it is **not** `apt-get install libraylib-dev`; that package
does not exist there — use the raylib PPA or a source build), then:

```bash
pkg-config --exists raylib     # confirm raylib is installed
make                           # build the interpreter
make rayrot                  # build the raylib binding (rayrot/raylib.so)
BRAINROT_PATH=rayrot ./brainrot examples/raylib/ohio_engine.brainrot
```

`make play` does the last two steps in one. See
[`docs/rayrot.md`](docs/rayrot.md) for the full raylib setup guide (Ubuntu
PPA vs. source build, macOS, the two-library model) and the binding reference.

### 🤖 …and the generated binding

The hand-written module above wraps about 20 raylib primitives and flattens
everything into scalars (`rl_draw_circle(640, 360, 100.0, 255, 0, 255, 255)`).
raylib has **617** functions, and hand-writing that many wrappers is not a
plan — so `rayrot/rayrot_gen.py` generates them from raylib's own published
API description:

```text
rayrot/raylib_api.json → rayrot-gen → { C adapters + ABI descriptors,
                                            a Brainrot prelude of gang types
                                            and gyatt constants, ABI tests }
```

That yields **378 functions and 16 struct types**, and the calls take real
aggregates by value instead of loose scalars:

```c
#cooked <raylibgen>

gang Vector2 pos;
gang Color orb;
rl_draw_circle_v(pos, 60.0, orb);   🚽 raylib's own DrawCircleV(Vector2, float, Color)
```

```bash
make rayrot-gen-sources   # generate the binding (raylib NOT required)
make rayrot-gen           # + compile it and run its ABI drift check
make play-gen               # + run examples/raylib/ohio_engine_gen.brainrot
```

Generating needs only Python and the pinned JSON; only *compiling* the result
needs raylib. Every generated `gang` has its size, alignment, and every field
offset `_Static_assert`ed against the real raylib headers, so a layout
disagreement is a build failure rather than a runtime corruption. What it
deliberately leaves out — struct returns most of all — is counted and reported
on every run; see [`docs/rayrot.md`](docs/rayrot.md).

## 🗪 Community

Join our community on:

- [Discord](https://discord.gg/FjHhvBHSGj)
- [Reddit](https://www.reddit.com/r/Brainrotlang/)

## 📚 Language Reference

### Keywords

| Brainrot   | C Equivalent | Implemented? |
| ---------- | ------------ | ------------ |
| skibidi    | void         | ✅           |
| rizz       | int          | ✅           |
| cap        | bool         | ✅           |
| flex       | for          | ✅           |
| bussin     | return       | ✅           |
| edgy       | if           | ✅           |
| amogus     | else         | ✅           |
| goon       | while        | ✅           |
| bruh       | break        | ✅           |
| grind      | continue     | ✅           |
| chad       | float        | ✅           |
| gigachad   | double       | ✅           |
| yap        | char         | ✅           |
| deadass    | const        | ✅           |
| sigma rule | case         | ✅           |
| based      | default      | ✅           |
| mewing     | do           | ✅           |
| gyatt      | enum         | ✅           |
| giga       | long         | ✅           |
| smol       | short        | ✅           |
| nut        | signed       | ✅           |
| maxxing    | sizeof       | ✅           |
| salty      | static       | ✅           |
| gang       | struct       | ✅           |
| ohio       | switch       | ✅           |
| chungus    | union        | ✅           |
| nonut      | unsigned     | ✅           |
| schizo     | volatile     | ✅           |
| W          | true         | ✅           |
| L          | false        | ✅           |
| thicc      | long long    | ✅           |
| rant       | string type  | ✅           |
| lit        | typedef      | ✅           |
| unc        | `__asm__`    | ❌           |

Every keyword above is implemented except `unc`. Two former entries were
removed rather than implemented: `whopper` (`extern`), because `#cooked`
splices source and native modules self-register, so there is no separate
compilation for it to bridge and no linker to inform; and `cringe` (`goto`).
Both had been reserved words that only ever produced a syntax error, so both
names are now ordinary identifiers.

### Preprocessor directives

| Brainrot | C Equivalent |
| -------- | ------------ |
| #cooked  | #include     |

`#cooked` is the only one, deliberately. Include guards are unnecessary
because `#cooked` is **include-once by construction**; named constants are
`gyatt` (enum) and `deadass` (const); and there are real functions rather than
function-like macros. Conditional compilation is left out because deciding
things before the program exists is a compile-and-link idea, and Brainrot
decides everything at run time — the real need behind it, adapting to an
optional module that may not be installed, is
[#331](https://github.com/Brainrotlang/brainrot/issues/331).

`#cooked "path/to/file.brainrot"` splices another Brainrot file's functions
and structs into the current one, resolved relative to the including file's
directory. `#cooked <name>` resolves `name` to either a `<name>.brainrot`
file (spliced the same way) or a `<name>.so` native module (`dlopen`'d and
registered) by searching `$BRAINROT_PATH`, then either the install module
directory or a `stdrot/` directory next to the running executable
(whichever applies — an install and a source build can't shadow each
other). See
[the language reference](docs/the-brainrot-programming-language.md#713-modules-cooked)
for details (path resolution, the module search path, include-once behavior,
circular-include detection).

### Builtin functions

Check the [user documentation](docs/the-brainrot-programming-language.md).

File I/O (`crackopen` / `peaceout` / `skim` / `yapto` / …) has its own
reference: [`docs/file-io.md`](docs/file-io.md). Like every other builtin
these are library functions rather than keywords, so they are **not** in the
keyword table above — the only thing file I/O adds to the grammar is the type
name `SAUCE`, used as `SAUCE *f`.

### Operators

The language supports basic arithmetic operators:

- `+` Addition
- `-` Subtraction
- `*` Multiplication
- `/` Division
- `=` Assignment
- `<` Less than
- `>` Greater than
- `&&` Logical AND
- `||` Logical OR

## ⚠️ Limitations

Current limitations include:

- Limited support for complex expressions
- Basic error reporting
- Structs/unions can be the element type of an array, including
  multi-dimensional ones (`gang Point pts[3];`, `gang Point grid[2][2];`);
  index-then-access composes (`pts[i].x`, `grid[r][c].y`, `lines[i].a.x`),
  and an element is a by-value struct anywhere a plain struct variable is.
  Whole-struct assignment to an existing element (`pts[i] = c;`) and array
  brace initializers (`gang Point pts[2] = {{1,2},{3,4}};`) are not yet
  supported — declare and assign per field, or copy-initialize
- Struct/union function arguments, return values, and copy-initializers
  accept a plain struct/union variable, an array element (`take(pts[i])`,
  `bussin pts[i];`, `gang Point c = pts[i];`), a by-value member-access
  sub-expression (`take(b.corner)`, `bussin b.corner;`, `gang Point c =
  b.corner;`), or a struct-returning call result (`take(make_point())`,
  `bussin make_point();`, `gang Point c = make_point();`) of the exact
  declared type, deep-copied (not aliased). A function may also return a
  **pointer to a struct** (`gang Point *f()`), returning a pointer value
  (a parameter or other storage that outlives the call) rather than a
  copy — returning `&local` dangles once the call returns, the same
  undefined behavior as a scalar pointer return in C
- Arrays cannot be passed or returned by value (only via a pointer
  parameter, which aliases the caller's array like in C)
- `ohio`/`based` (switch/default): `based` fires as soon as the interpreter's scan reaches it, so its position among the `sigma rule` cases matters — placing it before a case that would otherwise match currently pre-empts that match ([#179](https://github.com/Brainrotlang/brainrot/issues/179))

## 🗺️ Roadmap

Where Brainrot is headed — a native C ABI, generated library bindings (raylib
first), threads, hashmaps and sockets — is documented in the
[roadmap](docs/ROADMAP.md).

## 🔌 VSCode Extension

Brainrot has a Visual Studio Code extension to enhance your development experience with syntax highlighting and support for the Brainrot programming language. You can find it here:

[Brainrot VSCode Extension](https://github.com/araujo88/brainrot-vscode-support)

## 🤝 Contributing

Feel free to contribute to this project by:

1. Forking the repository
2. Creating a new branch for your feature
3. Submitting a pull request

## 📝 License

This project is licensed under the GPL License - see the LICENSE file for details.

## 🙏 Acknowledgments

- This project is created for educational purposes
- Inspired by meme culture and internet slang
- Built using Flex and Bison tools

## 🐛 Issues

Please report any additional issues in the GitHub Issues section.
