# Brainrot Programming Language

[![license](https://img.shields.io/badge/license-GPL-green)](https://raw.githubusercontent.com/Brainrotlang/brainrot/main/LICENSE)
[![CI](https://github.com/Brainrotlang/brainrot/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Brainrotlang/brainrot/actions/workflows/ci.yml)

Brainrot is a meme-inspired programming language that translates common programming keywords into internet slang and meme references. It's built using Flex (lexical analyzer) and Bison (parser generator), making it a fun way to learn about language processing and compiler design.

## History

The TRUE history behind the Brainrot programming language can be found [here](TRUTH.md).

## 🤔 What is Brainrot?

Brainrot is a C-like programming language where traditional keywords are replaced with popular internet slang. For example:

- `void` → `skibidi`
- `int` → `rizz`
- `for` → `flex`
- `return` → `bussin`

## 📦 Installation

**Prebuilt binaries.** Every [release](https://github.com/Brainrotlang/brainrot/releases)
attaches ready-to-run archives for Linux (amd64/arm64), macOS (Intel & Apple
Silicon), and Windows (amd64), plus the wasm module — download, extract, run.

**From source.** You need a C compiler (GCC or Clang), **Flex**, **Bison**, and
**OpenSSL** development headers (the standard library's `gamba()` CSPRNG links
`libcrypto`):

```bash
git clone https://github.com/Brainrotlang/brainrot.git
cd brainrot
make                       # regenerates the lexer/parser, builds ./brainrot + libstdrot.so
./brainrot examples/hello_world.brainrot
sudo make install          # optional: install under /usr/local
```

Per-platform dependency setup (**NixOS**, **Ubuntu/Debian**, **Arch**,
**macOS**, **Windows**), prebuilt-binary details, and troubleshooting live in
the **[Installation Guide](docs/installation.md)**.

## 🛠️ Building & contributing

`make` regenerates the lexer/parser and builds the interpreter, with `-Werror`
and the address/undefined-behavior sanitizers on. The full contributor
workflow — every `make` target, the `pytest` and Valgrind suites,
`clang-format`/`cppcheck`, and the **WebAssembly** (`make wasm`) and **native
Windows** (`make windows`) builds — is in the **[Building & Development
Guide](docs/building.md)**, alongside [`AGENTS.md`](AGENTS.md) and
[`CONTRIBUTING.md`](CONTRIBUTING.md).

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
