# The Brainrot Programming Language

A Meme-Fueled Journey into Compiler Design, Internet Slang, and Skibidi Toilets

## Table of Contents

1. **Foreword**
2. **Introduction**
3. **What Is Brainrot?**
4. **Installation and Requirements**
5. **Building the Compiler**
6. **Basic Usage**
7. **Language Reference**
   - 7.1. Keywords
   - 7.2. Operators
   - 7.3. Control Flow (if, for, while, do-while, switch)
   - 7.4. Declarations and Variables (`rizz`)
   - 7.5. Return Statements (`bussin`)
   - 7.6. Built-In Functions
   - 7.7. User Defined Functions
8. **Extended User Documentation**
   - 8.1. `yapping`
   - 8.2. `yappin`
   - 8.3. `baka`
   - 8.4. `ragequit`
   - 8.5. `chill`
9. **Limitations**
10. **Known Issues**
11. **Cultural Context: The Rise of ‘Brain Rot’**
12. **Meme Culture, Oxford Word of the Year, and Brainrot**
13. **Contributing**
14. **License**
15. **Closing Thoughts**

---

## 1. Foreword

**“What if there was a programming language that replaced every single keyword with internet slang?”** That single question captures the essence of Brainrot: a meme-inspired, _C-like_ language that breaks all expectations (and possibly your sanity). Originally built as a playful experiment, Brainrot demonstrates that, with enough Flex, Bison, and questionable design decisions, you can turn your wildest meme dreams into compilable code.

---

## 2. Introduction

Brainrot might not be the language you _asked_ for, but it might just be the language you _need_—especially if you’re looking for a hilarious way to learn about lexical analysis and parsing. The entire approach is to replace traditional C keywords with slang from TikTok, Gen Z memes, and beyond:

- `skibidi` for `void`
- `rizz` for `int`
- `flex` for `for`
- `bussin` for `return`
- `goon` for `while`
- `mewing` for 'do'
- and so on...

What’s the result? A language that looks thoroughly bizarre yet compiles into something resembling real (albeit comedic) logic. It’s a testament to how robust compiler design is—once you set up the grammar, your code can say practically anything it wants, so long as it follows syntactic rules.

---

## 3. What Is Brainrot?

**Brainrot** is a **meme-inspired programming language**, described by some as “the supposed deterioration of a person’s mental or intellectual state.” Of course, that’s part of the joke! The real intent is to offer an irreverent but educational environment for exploring how compilers work, how tokens are defined, and how parse trees are built. Instead of standard C, you’ll be greeted by keywords like:

- **`skibidi main`**: The entry point (like `int main()`).
- **`flex (i = 0; i < 10; i = i + 1)`**: A `for` loop, but more ridiculous.
- **`bussin 0;`**: The `return 0;` you’re used to—but with none of the seriousness.

Everything is overshadowed by the comedic vibe that references modern internet slang. The “brain rot” concept stands for the comedic notion that these memes can degrade your intellectual faculties—yet ironically, you still have to know how compilers work to build Brainrot.

---

## 4. Installation and Requirements

To compile Brainrot from source, you’ll need:

- **GCC** (GNU Compiler Collection)
- **Flex** (Fast Lexical Analyzer)
- **Bison** (Parser Generator)

Installation commands vary by platform:

### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install gcc flex bison libfl-dev
```

### Arch Linux

```bash
sudo pacman -S gcc flex bison
```

### macOS (via Homebrew)

```bash
brew install gcc flex bison
```

> **Note**: If you encounter `libfl` issues on macOS, you may need to locate and symlink `libfl.dylib` manually, as outlined in the README.

---

## 5. Building the Compiler

1. **Clone** the repository:
   ```bash
   git clone https://github.com/araujo88/brainrot.git
   cd brainrot
   ```
2. **Generate** the parser and lexer:
   ```bash
   bison -d -Wcounterexamples lang.y -o lang.tab.c
   flex -o lang.lex.c lang.l
   ```
3. **Compile**:
   ```bash
   gcc -o brainrot lang.tab.c lex.yy.c ast.c -lfl
   ```
4. Alternatively, run:
   ```bash
   make
   ```
   This will produce the `brainrot` executable if everything goes smoothly.

---

## 6. Basic Usage

To run your first Brainrot program:

1. Create a file (e.g., `hello.brainrot`):
   ```c
   skibidi main {
       yapping("Hello, World!");
       bussin 0;
   }
   ```
2. **Execute** it:
   ```bash
   ./brainrot hello.brainrot
   ```
   The compiler interprets the code, prints “Hello, World!”, and ends with `bussin 0` (akin to `return 0;`).

---

## 7. Language Reference

### 7.1. Keywords

Brainrot replaces familiar C keywords with meme-inspired slang:

| Brainrot   | C Equivalent |
| ---------- | ------------ |
| skibidi    | void         |
| rizz       | int          |
| cap        | bool         |
| cooked     | auto         |
| flex       | for          |
| bussin     | return       |
| edgy       | if           |
| amogus     | else         |
| goon       | while        |
| bruh       | break        |
| grind      | continue     |
| chad       | float        |
| gigachad   | double       |
| yap        | char         |
| deadass    | const        |
| sigma rule | case         |
| based      | default      |
| mewing     | do           |
| gyatt      | enum         |
| whopper    | extern       |
| cringe     | goto         |
| giga       | long         |
| smol       | short        |
| nut        | signed       |
| maxxing    | sizeof       |
| salty      | static       |
| gang       | struct       |
| ohio       | switch       |
| chungus    | union        |
| nonut      | unsigned     |
| schizo     | volatile     |
| W          | true         |
| L          | false        |
| thicc      | long long    |
| rant       | string type  |
| lit        | typedef      |

### 7.2. Operators

Brainrot supports common arithmetic and logical operators:

- `+` Addition
- `-` Subtraction
- `*` Multiplication
- `/` Division
- `%` Modulus
- `<`, `>`, `<=`, `>=`, `==`, `!=`
- `=` Assignment
- `&&` Logical AND
- `||` Logical OR
- `!` Logical NOT (depending on grammar rules)
- `++` Increment:
  - Pre-Increment (`++i`): Increments the value of `i` by 1 before it is used in an expression.
  - Post-Increment (`i++`): Uses the current value of `i`, then increments it by 1.
- `--` Decrement:
  - Pre-Decrement (`--i`): Decrements the value of `i` by 1 before it is used in an expression.
  - Post-Decrement (`i--`): Uses the current value of `i`, then decrements it by 1.

### 7.3. Control Flow

1. **If/Else**
   ```c
   edgy (condition) {
       // if-true block
   }
   amogus {
       // else block
   }
   ```
2. **While**
   ```c
   goon (i < 5) {
       // loop body
   }
   ```
3. **Do-While**
   ```c
   mewing {
       // loop body
   } goon (i < 5);
   ```
4. **For**
   ```c
   flex (init_expr; condition; increment) {
       // loop body
   }
   ```
5. **Switch**
   ```c
   ohio (expression) {
       sigma rule value:
           // case body
           bruh;
       based:
           // default case
   }
   ```

### 7.4. Declarations and Variables (`rizz`)

- **`rizz i = 0;`** declares an integer variable `i`, assigned 0.
- **`i = i + 1;`** increments i by 1, following typical C expression syntax.

### 7.5. Return Statements (`bussin`)

- **`bussin expression;`** to end the main function (or any function, if you extend the language).
- Example:
  ```c
  bussin 0;
  ```

### 7.6. Built-In Functions

- **`yapping`**: prints text **and** automatically appends a newline.
- **`yappin`**: prints text **without** adding a newline.
- **`baka`**: prints to `stderr`, typically used for errors/warnings.
- **`ragequit`**: terminates program execution immediately with the provided exit code.
- **`chill`**: sleep for a integer number of seconds.
- **`slorp`**: reads user input, similar to `scanf` but safe.

---

### 7.7. User Defined Function

Defining a function in brainrot follows the same pattern as the C programming language: `return_type func_name(param_type param_name) {}`

#### Example:

```c
cap is_prime(rizz n) {
    edgy(n < 2) {
        bussin L;
    }
    flex(rizz i = 2; i * i <= n; i++) {
        edgy(n % i == 0) {
            bussin L;
        }
    }
    bussin W;

}
```

#### BreakDown

- **Function definition**:
  - `cap`: return type (bool)
  - `is_prime`: function name
  - `n`: parameter

#### Usage Example:

```c
cap isPrime = is_prime(11)

```

## 8. Extended User Documentation

### 8.1. `yapping`

```c
void yapping(const char* format, ...);
```

- Similar to `printf`, but **always** appends its own newline after printing.
- If you include `\n` in `format`, expect **two** line breaks in total.

**Example**:

```c
yapping("Value: %d", 10);
// Output => "Value: 10\n"
```

### 8.2. `yappin`

```c
void yappin(const char* format, ...);
```

- Similar to `printf` but **no** extra newline is added.
- Perfect for building partial lines or for more granular control of output formatting.

**Example**:

```c
yappin("Hello ");
yappin("World!\n");
// Output => "Hello World!\n"
```

### 8.3. `baka`

```c
void baka(const char* format, ...);
```

- Prints error messages to **`stderr`**.
- Does **not** automatically add a newline (unless your format string includes one).
- Great for logs, warnings, and error messages.

**Example**:

```c
baka("Error: undefined variable %s\n", varName);
```

### 8.4. `ragequit`

```c
void ragequit(int exit_code);
```

- Terminates program execution immediately with the provided exit code.
- No additional output is printed unless explicitly added before the ragequit call.

**Example**:

```c
ragequit(1);
```

### 8.5. `chill`

```c
void chill(unsigned int seconds);
```

- Sleeps for a specified number of seconds (must be an unsigned integer)

**Example**:

```c
chill(2);
```

### 8.6. `slorp`

```c
void slorp(var_type var_name);
```

- Reads user input

**Example**:

```c
skibidi main {
    rizz num;
    yapping("Enter a number:");
	slorp(num);
    yapping("You typed: %d", num);
	bussin 0;
}
```

---

## 9. Limitations

- No built-in support for increment/decrement (`++`, `--`).
- Functions other than `skibidi main` not fully supported (unless you add them).
- Arrays, complex data structures, and advanced memory management are absent.
- Error reporting is minimal, typically halting on the first serious parse error.

---

## 10. Known Issues

- Some macOS users must manually manage `libfl` symlinks.
- Minimal string manipulation: no standard library for string operations.
- Grammar conflicts can arise if you expand the language significantly.
- The language’s comedic nature may cause colleagues to question your sanity.

---

## 11. Cultural Context: The Rise of ‘Brain Rot’

The term **"brain rot"** was declared Oxford Word of the Year 2024, symbolizing the phenomenon of “the supposed deterioration of a person’s mental or intellectual state” due to low-value or meme-saturated online content. Brainrot the language playfully leans into this concept, intentionally using the so-called “nonsensical” or “trivial” memes to highlight a bit of self-awareness about how internet culture shapes our speech and thinking.

---

## 12. Meme Culture, Oxford Word of the Year, and Brainrot

- The language’s name, “Brainrot,” resonates with the 2024 Word of the Year conversation.
- Memes like **“Skibidi Toilet,”** **“Only in Ohio,”** and **“rizz”** are central to Gen Z and Gen Alpha humor. Brainrot references them liberally as a whimsical statement on how quickly online slang evolves—and how easily it can be turned into code.
- The unstoppable spread of these memes ironically parallels the unstoppable creativity and chaos that emerges from community-driven language development.

---

## 13. Contributing

If you want to add new slang or expand Brainrot:

1. **Fork** the GitHub repository.
2. **Create** a new branch for your changes.
3. **Edit** the grammar (`lang.y`) and lexer (`lang.l`) to support the new token or feature.
4. **Submit** a Pull Request with a clear description of your changes.

All contributions, even more memes, are welcome—just be prepared for the comedic consequences!

---

## 14. License

This project is licensed under the **GPL License**. See the `LICENSE` file in the repository for more details. Essentially, you’re free to modify and distribute Brainrot, so long as you keep it open-source and credit the original authors.

---

## 15. Closing Thoughts

Brainrot is a testament to the fact that compiler design can be both educational and thoroughly _unserious_. Whether you’re an aspiring language implementer, a meme connoisseur, or just someone who thought “C needed more spice,” Brainrot might be the ideal playground for you. Code in Brainrot, add your own slang, or show it off to your friends to watch them recoil in confusion and laughter.

### “Just because you _can_ do something doesn’t mean you _should_—but in Brainrot’s case, maybe you _really should._”

Happy coding, and remember: if your mind starts to go blank from all the memes, that’s not a bug—it’s Brainrot by design!
