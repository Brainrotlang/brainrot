# Brainrot Programming Language

[![license](https://img.shields.io/badge/license-GPL-green)](https://raw.githubusercontent.com/araujo88/brainrot/main/LICENSE)
[![CI](https://github.com/araujo88/brainrot/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/araujo88/brainrot/actions/workflows/ci.yml)

Brainrot is a meme-inspired programming language that translates common programming keywords into internet slang and meme references. It's built using Flex (lexical analyzer) and Bison (parser generator), making it a fun way to learn about language processing and compiler design.

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
gcc -o brainrot lang.tab.c lex.yy.c ast.c -lfl
```

Alternatively, simply run:

```bash
make
```

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
./brainrot < hello.brainrot
```

## 🗪 Community

Join our community on [Discord](https://discord.gg/FjHhvBHSGj).

## 📚 Language Reference

### Keywords

| Brainrot   | C Equivalent | Implemented? |
| ---------- | ------------ | ------------ |
| skibidi    | void         | ✅           |
| rizz       | int          | ✅           |
| cap        | bool         | ✅           |
| cooked     | auto         | ❌           |
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
| giga       | long         | ❌           |
| smol       | short        | ✅           |
| nut        | signed       | ✅           |
| maxxing    | sizeof       | ✅           |
| salty      | static       | ❌           |
| gang       | struct       | ❌           |
| ohio       | switch       | ✅           |
| chungus    | union        | ❌           |
| nonut      | unsigned     | ✅           |
| schizo     | volatile     | ✅           |
| W          | true         | ✅           |
| L          | false        | ✅           |
| thicc      | long long    | ❌           |
| rant       | string type  | ❌           |

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
- No support for functions other than main
- Basic error reporting

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

## 🐛 Known Issues

1. Error reporting is minimal
2. No support for arrays
3. Limited string manipulation capabilities
4. No support for complex control structures

Please report any additional issues in the GitHub Issues section.
