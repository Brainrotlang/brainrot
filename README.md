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

### Installation on Different Platforms

#### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install gcc flex bison libfl-dev
```

#### Arch Linux

```bash
sudo pacman -S gcc flex bison
```

#### macOS (using Homebrew)

```bash
brew install gcc flex bison
```

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
| gyatt      | enum         | ❌           |
| whopper    | extern       | ❌           |
| cringe     | goto         | ❌           |
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
| lit        | typedef      | ❌           |

### Preprocessor directives

| Brainrot   | C Equivalent | Implemented? |
| ---------- | ------------ | ------------ |
| #cooked     | #include    | ✅           |
| #edgydef    | #ifdef      | ❌           |
| #edgyndef   | #ifndef     | ❌           |
| #endedgy    | #endif      | ❌           |
| #slaps      | #define     | ❌           |
| #aura       | #pragma     | ❌           |

`#cooked "path/to/file.brainrot"` splices another Brainrot file's functions
and structs into the current one, resolved relative to the including file's
directory. See [the language reference](docs/the-brainrot-programming-language.md#710-modules-cooked)
for details (path resolution, include-once behavior, circular-include
detection).

### Builtin functions

Check the [user documentation](docs/the-brainrot-programming-language.md).

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
- No support for arrays in user-defined functions
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
