# Installation Guide

How to get a working `brainrot` interpreter on your machine and run your first
program. Two routes:

- **[Prebuilt binaries](#prebuilt-binaries)** — download and run, no toolchain
  needed. Fastest if you just want to *use* Brainrot.
- **[Build from source](#build-from-source)** — per-platform instructions
  (NixOS, Ubuntu/Debian, Arch, macOS, Windows).

If you want to *develop* Brainrot (run the test suite, the formatter, the wasm
or Windows builds), read the **[Building & Development Guide](building.md)**
instead — this page covers only getting a runnable interpreter.

> **What you end up with.** Brainrot is an *interpreter*: `brainrot file.brainrot`
> runs the program directly. On Linux/macOS it loads its standard library from a
> companion `libstdrot.so`/`.dylib`; the single-file Windows and wasm builds
> compile that library in. See [How the standard library is
> found](#how-the-standard-library-is-found) if you hit a "cannot load
> libstdrot" error.

---

## Prebuilt binaries

Every [GitHub release](https://github.com/Brainrotlang/brainrot/releases) with a
`v*` tag attaches ready-to-run archives. Pick the one for your platform:

| Platform | Asset | Contents |
| --- | --- | --- |
| Linux x86-64 | `brainrot-<tag>-linux-amd64.tar.gz` | `brainrot` + `libstdrot.so` |
| Linux ARM64 | `brainrot-<tag>-linux-arm64.tar.gz` | `brainrot` + `libstdrot.so` |
| macOS Intel | `brainrot-<tag>-darwin-amd64.tar.gz` | `brainrot` + `libstdrot.so` |
| macOS Apple Silicon | `brainrot-<tag>-darwin-arm64.tar.gz` | `brainrot` + `libstdrot.so` |
| Windows x86-64 | `brainrot-<tag>-windows-amd64.zip` | a single self-contained `brainrot.exe` |
| Browser / Node | `brainrot.wasm` + `brainrot.mjs` | the WebAssembly module (powers the playground) |

`SHA256SUMS.txt` carries a checksum for every asset — verify with
`sha256sum -c SHA256SUMS.txt` (or `shasum -a 256 -c` on macOS).

### Linux / macOS

```bash
tar -xzf brainrot-<tag>-linux-amd64.tar.gz
cd brainrot-<tag>-linux-amd64        # the archive extracts brainrot + libstdrot.so
./brainrot path/to/hello.brainrot
```

Keep `brainrot` and `libstdrot.so` in the same directory — the release binary
has an rpath (`$ORIGIN` / `@loader_path`) so it finds its sibling `libstdrot.so`
regardless of your working directory. Linux binaries need the OpenSSL runtime
(`libcrypto`) present, which nearly every distro ships by default.

macOS may quarantine a downloaded binary ("cannot be opened because the
developer cannot be verified"): clear it with
`xattr -d com.apple.quarantine brainrot libstdrot.so`.

### Windows

Unzip `brainrot-<tag>-windows-amd64.zip` and run the single executable — there
is no separate library to keep alongside it:

```bat
brainrot.exe path\to\hello.brainrot
```

The `.exe` is self-contained (the standard library is compiled in, and the
MinGW runtime is statically linked), so it imports only system DLLs and runs on
a clean Windows machine. Native `#cooked <name>` modules load from a
`<name>.dll` on `%BRAINROT_PATH%` (`;`-separated on Windows) — see the
[Building guide](building.md#windows) for how those are produced.

### WebAssembly

`brainrot.wasm` / `brainrot.mjs` are for embedding in a browser or Node (this is
what the online playground runs), not a CLI. See the
[Building guide](building.md#webassembly) for how the module is driven.

---

## Build from source

You need three tools everywhere — a C compiler (**GCC** or Clang), **Flex**, and
**Bison** — plus **OpenSSL** development headers (the standard library's
`gamba()` CSPRNG links `libcrypto`; a missing OpenSSL is a failed link, never a
`gamba`-less interpreter). Then:

```bash
git clone https://github.com/Brainrotlang/brainrot.git
cd brainrot
make                       # regenerates the lexer/parser, builds brainrot + libstdrot.so
./brainrot examples/hello_world.brainrot
```

`make` runs Flex and Bison for you — you do **not** need to invoke them by hand.
Per-platform dependency install below.

### NixOS

A flake is included, so no manual dependency install:

```bash
git clone https://github.com/Brainrotlang/brainrot.git
cd brainrot
nix develop            # drops you in a shell with gcc, flex, bison, openssl, …
make
./result/bin/brainrot examples/hello_world.brainrot   # or ./brainrot after make
```

To install it declaratively via a flake-based system config
(`/etc/nixos/flake.nix`), tracking the latest version:

```nix
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
          # For a specific user:
          users.users.username.packages = [ brainrot.packages.x86_64-linux.default ];
          # Or system-wide:
          environment.systemPackages = [ brainrot.packages.x86_64-linux.default ];
        }
      ];
    };
  };
}
```

Run `nix flake update` when you want the latest version, then
`sudo nixos-rebuild switch`.

### Ubuntu / Debian

```bash
sudo apt-get update
sudo apt-get install gcc flex bison libfl-dev libssl-dev
make
```

### Arch Linux

```bash
sudo pacman -S gcc flex bison openssl
make
```

### macOS (Homebrew)

```bash
brew install gcc flex bison openssl@3
make
```

Homebrew's OpenSSL is keg-only, but `make` locates it automatically (via
`brew --prefix openssl@3`) and **statically** links `libcrypto` into
`libstdrot.dylib`, so the result is self-contained.

If the build fails to find `libfl`, symlink it where the linker looks:

```bash
# Apple Silicon
find /opt/homebrew -name "libfl.*"
sudo ln -s /path/to/libfl.dylib /opt/homebrew/lib/libfl.dylib
# Intel
find /usr/local -name "libfl.*"
sudo ln -s /path/to/libfl.dylib /usr/local/lib/libfl.dylib
```

### Windows

The native Windows build uses the **MinGW-w64** toolchain under
[MSYS2](https://www.msys2.org/). From the **MINGW64** shell:

```bash
pacman -S mingw-w64-x86_64-gcc make bison flex
make windows        # produces a single self-contained brainrot.exe
./brainrot.exe examples/hello_world.brainrot
```

`make windows` (not plain `make`) is the Windows target — it compiles the
standard library in and statically links the runtime. Details, including native
`#cooked` modules, are in the [Building guide](building.md#windows).

---

## Installing system-wide

On Linux/macOS, install the interpreter and its library under `/usr/local`:

```bash
sudo make install      # /usr/local/bin/brainrot + /usr/local/lib/libstdrot.so
```

`libstdrot.so` goes in `/usr/local/lib`, which the dynamic linker searches by
default, so `brainrot file.brainrot` then works from anywhere. Remove it with:

```bash
sudo make uninstall
```

---

## Verifying the install

```bash
printf 'skibidi main {\n    yapping("it works\\n");\n    bussin 0;\n}\n' > hello.brainrot
brainrot hello.brainrot        # -> it works
```

More examples live in [`examples/`](../examples/README.md).

---

## The optional cursed game (raylib)

Brainrot ships a real raylib game loop through the optional `rayrot` binding
(`#cooked <raylib>`). raylib is **not** needed for a normal install or to run
the test suite. If you want it, its setup — installing a system raylib per OS,
the two-library model, and the `make rayrot` / `make play` targets — has a
dedicated guide: **[`docs/rayrot.md`](rayrot.md)**.

---

## How the standard library is found

On the native Linux/macOS builds, `brainrot` loads `libstdrot.so`/`.dylib` at
startup, in this order:

1. `$STDROT_LIB_PATH`, if set, names an exact library to load (used by the test
   harness; unset in normal use).
2. a cwd-relative `./libstdrot.so`.
3. the dynamic linker's normal search path — which finds it after
   `sudo make install` (it lives in `/usr/local/lib`), or, for a release
   archive, next to the binary via its baked-in rpath.

So a "Failed to load libstdrot.so" error means none of those found it: run from
the directory that holds it, set `STDROT_LIB_PATH`, or `make install`. The
Windows and wasm builds compile the library in, so this never applies to them.

---

## See also

- **[Building & Development Guide](building.md)** — full toolchain, every `make`
  target, the test suite, and the wasm/Windows builds, for contributors.
- **[Brainrot user guide](brainrot-user-guide.md)** and the [language
  reference](the-brainrot-programming-language.md) — how to write Brainrot.
- **[`docs/rayrot.md`](rayrot.md)** — the raylib binding and the cursed game.
