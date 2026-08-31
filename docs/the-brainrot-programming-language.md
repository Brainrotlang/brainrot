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
   - 7.8. Pointers and Call by Reference
   - 7.9. Structs (`gang`)
   - 7.10. Unions (`chungus`)
   - 7.11. Enums (`gyatt`)
   - 7.12. Type Aliases (`lit`)
   - 7.13. Modules (`#cooked`)
8. **Extended User Documentation**
   - 8.1. `yapping`
   - 8.2. `yappin`
   - 8.3. `baka`
   - 8.4. `ragequit`
   - 8.5. `chill`
   - 8.6. `slorp`
   - 8.7. `bet`
   - 8.8. `gamba`
   - 8.9. Strings: `yaplen`, `yapcat`, `yapcmp`, `yapidx`, `s[i]`, `s[i:j]`
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
- **OpenSSL** (`libcrypto`) — a **required** dependency of the standard
  library (`libstdrot.so`), which uses it for the cryptographically safe
  `gamba()` RNG (see [§8.8](#88-gamba)). The development headers are needed to
  build; on Linux the runtime `libcrypto` is also needed to run. Building
  without OpenSSL is a failed link, not a `gamba`-less interpreter.

Installation commands vary by platform:

### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install gcc flex bison libfl-dev libssl-dev
```

### Arch Linux

```bash
sudo pacman -S gcc flex bison openssl
```

### macOS (via Homebrew)

```bash
brew install gcc flex bison openssl@3
```

> **Note**: Homebrew's OpenSSL is keg-only, but `make` locates it
> automatically and statically links `libcrypto` into `libstdrot.so`, so the
> built standard library is self-contained.

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
- `!` Logical NOT. Yields a `cap`, whatever the operand was, and binds as
  tightly as unary minus — so `!a == b` is `(!a) == b` and `!a && b` is
  `(!a) && b`.

  The operand is judged in its own type rather than the surrounding
  context's, so `!0.5` is `L` (0.5 is true, and truncating it to `!0` would
  say otherwise) and a packed `smol` is read at its own width. It must be a
  scalar or a pointer: `!` on a `gang` or a `rant` is a semantic error,
  since neither has a scalar truth value. On a pointer it is the null check
  it looks like.

  Because `cap` and `rizz` do not silently convert in either direction in
  this language, `rizz k = !n;` is a type error exactly as `rizz k = W;`
  is, and a native expecting `rizz` rejects a `!` argument the same way.
  Untyped integer contexts — an array index, an `ohio` selector, an `edgy`
  or `goon` condition — do get C's 0/1.
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
- **`gamba`**: cryptographically safe random integers (OpenSSL `RAND_bytes`).
- **`yaplen`**: length of a `rant`, in bytes.
- **`yapcat`**: joins two `rant`s into a new one.
- **`yapcmp`**: lexicographic comparison, returning `-1`, `0` or `1`.
- **`yapidx`**: byte index of the first occurrence of one `rant` in another,
  or `-1`.
- **File I/O** — `crackopen`, `peaceout`, `skim`, `doomscroll`, `yapto`,
  `shitpost`, `zoink`, `whereami`, `throwback`, `itsjoever`, `bricked`,
  `bustcache`. Twelve operations behind a `SAUCE *` handle; see
  [`docs/file-io.md`](file-io.md) for the full reference.

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

#### Parameter types

Scalars (`rizz`, `chad`, `gigachad`, `cap`, `yap`, `smol`), pointers, `gang`/
`chungus` by value, `gyatt`, and `rant` (strings) may all be parameters.

A `rant` parameter takes its **own copy** of the string, so it behaves as an
ordinary local `rant`: assigning to it inside the callee does not disturb the
caller's variable.

```c
rizz load_sfx(rant path) {
    yapping("loading %s", path);
    bussin 1;
}

skibidi main {
    load_sfx("assets/sfx/jump.ogg");
    load_sfx("assets/sfx/bruh.ogg");
    bussin 0;
}
```

#### Current Limitations

- An **array** cannot be passed as an argument — a parameter can never be an
  array type, and array-to-pointer decay is not implemented. Pass an element
  (`f(arr[0])`) or its address (`f(&arr[0])`).
- Reassigning a `rant` — parameter or local — leaks its previous buffer
  ([#277](https://github.com/Brainrotlang/brainrot/issues/277)).
- Argument types are only checked against parameter types in specific cases
  (arrays, struct/union tags); a general type check for user-defined calls
  does not exist yet, so passing a `rizz` where a `chad` is declared is not
  diagnosed. A non-string argument to a `rant` parameter *is* refused, though
  — the call does not run — because binding one would hand the callee a `rant`
  with no buffer, which then gets used. (A refused call can still leave a
  `rant` *declaration target* empty, e.g. `rant r = f(1, 2);` on an arity
  mismatch; that is a separate, pre-existing property of failed calls.)

### 7.8. Pointers and Call by Reference

Brainrot supports C-style pointers with arbitrary indirection levels.

#### Declaration and basic usage

```c
rizz value = 10;
rizz *p = &value;
rizz **pp = &p;

*p = 20;
yapping("%d", value);   🚽 20
yapping("%d", **pp);    🚽 20
```

Supported forms:

- Pointer declaration: `rizz *p`, `rizz **pp`, ...
- Address-of: `&expr`
- Dereference: `*expr`
- Pointer assignment and comparison
- Pointer arithmetic: `pointer +/- integer`

#### Call by reference (pointer-based)

Use pointer parameters and pass addresses from the caller:

```c
rizz increment(rizz *n) {
    *n = *n + 1;
    bussin 0;
}

skibidi main {
    rizz x = 5;
    increment(&x);
    yapping("%d", x);  🚽 6
    bussin 0;
}
```

### 7.9. Structs (`gang`)

Use **`gang`** to define a struct type and declare struct variables. Structs group multiple fields of different types under a single name.

#### Struct Definition

Define a struct at the top level, outside of any function:

```c
gang Point {
    rizz x;
    rizz y;
    chad magnitude;
};
```

- **`gang TypeName { ... };`**: Defines a new struct type.
- Fields are declared using any supported type keyword (`rizz`, `chad`, `gigachad`, `smol`, `cap`, `yap`), or another already-defined `gang`/`chungus` type name for a nested field (see [Nesting](#nesting) below).
- The definition must end with `};`.

#### Struct Declaration

Declare a struct variable inside a function body:

```c
gang Point p;
```

All fields are zero-initialized by default.

#### Initializer Syntax

Initialize a struct at declaration time with a brace-enclosed list:

```c
gang Point q = {10, 20, 0.0};
```

Values are assigned to fields in order of declaration.

#### Member Access

Use `.` to read or write individual fields:

```c
p.x = 3;
p.y = 4;
p.magnitude = 5.0;
yapping("Point: %d %d %f", p.x, p.y, p.magnitude);
```

#### Nesting

A struct field can itself be another already-defined `gang` or `chungus`
type — structs and unions can nest inside each other in any combination,
to any depth:

```c
gang Point {
    rizz x;
    rizz y;
};

gang Line {
    gang Point start;   🚽 a struct field
    gang Point end;
};

skibidi main {
    gang Line l;
    l.start.x = 1;      🚽 chained member access
    l.start.y = 2;
    l.end.x = 10;
    l.end.y = 20;

    🚽 brace-init supports nested sub-initializers too
    gang Line m = { {5, 6}, {7, 8} };
}
```

Notes and limitations on nesting:

- The nested type must already be defined above the point where it's used
  as a field — there's no forward declaration.
- A struct/union **cannot embed itself by value** (`gang Foo { gang Foo x; };`
  is a compile-time error, same as C — it has no finite size). A
  self-referential **pointer** field (`gang Node { gang Node *next; };`) is
  allowed, and chained `.` access follows it like C's `->` — `a.next.val`
  reads/writes through the pointer, for any chain depth (`a.next.next.val`).
  Only a single level of indirection per hop is followed: a multi-level
  pointer field (`gang Node **next`) still needs an explicit dereference,
  and following a null pointer field is a runtime error.
- A nested struct/union field's brace-initializer must itself be a braced
  sub-initializer, matching the shape of the type — `gang Line m = { {5, 6},
  {7, 8} };`, **not** the flattened `gang Line m = {5, 6, 7, 8};`. The
  reverse (a braced sub-initializer for a plain scalar field) is likewise
  an error.

#### Full Example

```c
gang Point {
    rizz x;
    rizz y;
    chad magnitude;
};

skibidi main {
    gang Point p;
    p.x = 3;
    p.y = 4;
    p.magnitude = 5.0;
    yapping("Point: %d %d %f", p.x, p.y, p.magnitude);

    gang Point q = {10, 20, 0.0};
    yapping("Q: %d %d %f", q.x, q.y, q.magnitude);

    bussin 0;
}
```

Output:

```
Point: 3 4 5.0
Q: 10 20 0.0
```

**Arrays of structs.** A struct/union tag can be the element type of an array,
including multi-dimensional arrays (`gang Point pts[3];`, `gang Point
grid[2][2];`). Each element is a whole struct blob laid out with the tag's own
alignment, so indexing composes with member access: `pts[i].x`,
`grid[r][c].y`, and nested chains such as `lines[i].a.x`. An element is also a
by-value struct wherever a plain struct variable is — it can copy-initialize
another (`gang Point c = pts[i];`), be passed by value (`take(pts[i])`), or be
returned (`bussin pts[i];`).

```brainrot
gang Point {
    rizz x;
    chad y;
};

skibidi main {
    gang Point pts[3];
    rizz i;
    flex (i = 0; i < 3; i = i + 1) {
        pts[i].x = i * 10;
        pts[i].y = i + 0.5;
    }
    yapping("%d %.1f", pts[2].x, pts[2].y);
    bussin 0;
}
```

#### Current Limitations

- Whole-struct assignment to an existing array element (`pts[i] = c;`) is not
  supported, the same limitation plain struct variables have (`p = c;`); use
  copy-initialization or per-field assignment instead.
- A brace initializer for an array of structs (`gang Point pts[2] = {{1,
  2}, {3, 4}};`) is not yet supported; declare the array and assign elements.
- A field may itself be an array of structs/unions (`gang Pool { gang Entity
  es[8]; };`, including multi-dimensional), and its elements are accessed and
  assigned per field like any other (`pool.es[i].x = 1.0;`). The element type
  must already be defined, and a struct cannot contain an array of *itself*
  by value for the same reason it cannot contain one of itself directly — an
  array of pointers to itself (`gang Node *kids[2];`) is fine.
- Chained access follows a single-level pointer field per hop (`a.next.val`);
  a multi-level pointer field (`gang Node **next`) is not followed and must
  be dereferenced explicitly.
- A struct/union pointer supports arithmetic and indexing, scaled by the
  pointee's size: `p = p + 1`, `p = p - 1`, and `p[i].x` for both reading and
  assignment — so a function handed a bare `gang Entity *` can walk a whole
  pool. A pointer carries no extent, so — as in C — `p[i]` is **not**
  bounds-checked, unlike the array form (`pts[i]`) which is. Only
  single-level pointers and a single index are accepted: `gang E **pp` needs
  an explicit dereference, and `p[0][1]` would be indexing into a struct.
- Member access directly on a parenthesized expression (`(p + 1).x`) is not
  supported; assign first (`p = p + 1; p.x`) or index (`p[1].x`).
- A struct can be passed as a function parameter or returned from a
  function by value as a plain struct variable (`take(p)`, `bussin p;`), a
  by-value struct member-access sub-expression (`take(b.corner)`, `bussin
  b.corner;`), or a struct-returning call result (`take(make_point())`,
  `bussin make_point();`) of the exact matching type. By-value arguments
  and return values are deep-copied (C by-value semantics), never aliased.
  A function may instead return a **pointer to a struct** (`gang Point
  *f()`); that returns a pointer value — typically a pointer parameter or
  other storage that outlives the call — and reads/writes through it alias
  the pointee. Returning `&local` dangles once the call returns (the same
  undefined behavior as a scalar pointer return in C).

### 7.10. Unions (`chungus`)

Use **`chungus`** to define a union type and declare union variables. Like a
C union, all fields of a `chungus` share the same storage — writing one field
and reading another reinterprets the same underlying bytes.

#### Union Definition

Define a union at the top level, outside of any function:

```c
chungus Data {
    rizz i;
    chad f;
};
```

- **`chungus TypeName { ... };`**: Defines a new union type.
- Fields are declared using any supported type keyword, same as `gang`,
  including another already-defined `gang`/`chungus` type for a nested
  field — see [Nesting](#nesting-1) below.
- The definition must end with `};`.
- Struct and union tags share the same namespace, so a `gang` and a `chungus`
  cannot use the same name.

#### Union Declaration

Declare a union variable inside a function body:

```c
chungus Data d;
```

The storage is zero-initialized by default.

#### Initializer Syntax

Unlike `gang`, a `chungus` initializer must have **exactly one value** —
it initializes the first member, and the rest overlap it in memory:

```c
chungus Data d = {42};
```

Providing more than one value is an error:

```c
chungus Data d = {1, 2.0};  🚽 Error: Union initializer must have exactly one value
```

#### Member Access

Use `.` to read or write fields, same as `gang`. Since all fields overlap,
writing one field and reading another reinterprets the stored bytes:

```c
chungus Data d;
d.i = 1067614182;
yapping("As int: %d", d.i);
yapping("Reinterpreted as float: %.2f", d.f);
```

#### Nesting

Like `gang`, a `chungus` field can be another already-defined `gang` or
`chungus` type, and structs/unions can nest inside each other in any
combination:

```c
gang Point {
    rizz x;
    rizz y;
};

chungus Shape {
    gang Point pt;   🚽 struct nested inside a union
    rizz raw;
};

skibidi main {
    chungus Shape s;
    s.pt.x = 7;
    s.pt.y = 9;
}
```

The same rules as [`gang` nesting](#nesting) apply: the nested type must
already be defined, a union can't embed itself by value, and chained access
follows a single-level pointer field per hop (like C's `->`).

#### Current Limitations

- Chained access follows a single-level pointer field per hop (`a.next.val`);
  a multi-level pointer field (`gang Node **next`) is not followed and must
  be dereferenced explicitly.
- A union can be passed as a function parameter or returned from a
  function under the same rules as a struct (see [§7.9](#79-structs-gang)) —
  a plain variable, a by-value member-access sub-expression, or a
  union-returning call result of the exact matching type, deep-copied.

### 7.11. Enums (`gyatt`)

Use **`gyatt`** to define a named set of integer constants — like a C `enum`,
constants are unscoped, plain `int`-typed identifiers in the global
namespace (not accessed through the enum's tag), and freely interconvert
with `int` without a cast.

#### Enum Definition

Define an enum at the top level, outside of any function:

```c
gyatt Color {
    RED,
    GREEN,
    BLUE
};
```

- **`gyatt TypeName { ... };`**: Defines a new enum type and its constants.
- Each constant either gets an explicit value (`NAME = 5`) or auto-increments
  from the previous constant's value, starting at `0` for the first
  constant — same rules as C.
- Constant names share **one global namespace across every enum** in the
  program (matching C) — two different `gyatt` types can't both declare a
  constant with the same name, even if the enums themselves have different
  tags.
- The definition must end with `};`.

```c
gyatt Status {
    OK = 0,
    WARN = 5,
    ERR        🚽 auto-increments to 6
};
```

#### Enum Declaration

Declare an enum-typed variable inside a function body:

```c
gyatt Color favorite;
```

Uninitialized enum variables default to `0`, same as a plain `rizz`.

#### Using Enum Values

A constant is just an `int`-valued expression — use it directly, assign it,
compare it, switch on it, or print it with `%d`:

```c
skibidi main {
    gyatt Color favorite;
    favorite = GREEN;
    yapping("%d", favorite);   🚽 1

    ohio (favorite) {
        sigma rule GREEN:
            yapping("its green");
            bruh;
        based:
            yapping("dunno");
    }
}
```

#### Nesting

Like `gang`/`chungus`, an enum type can be used as a struct or union field:

```c
gang Shape {
    gyatt Color c;
    rizz sides;
};

skibidi main {
    gang Shape s;
    s.c = BLUE;
    yapping("%d %d", s.c, s.sides);
}
```

An enum's own body only ever holds `NAME [= INT]` constants — it can't nest
a struct/union/another enum inside it the way `gang`/`chungus` can nest each
other.

#### Functions

An enum type can be used as a function parameter or return type, passed and
returned exactly like `rizz` (by value, no restrictions):

```c
gyatt Color pick(rizz n) {
    edgy (n == 0) {
        bussin RED;
    }
    bussin BLUE;
}
```

#### `maxxing` (sizeof)

`maxxing` on an enum-typed variable or field returns `sizeof(int)`, same as
a `rizz`.

#### Current Limitations

- Enum constants are unscoped (C-style) — there's no `Color.RED`-style
  scoped access.
- An enum type can only be *defined* at the top level, outside of any
  function — the same restriction `gang`/`chungus` type definitions have.
  Declaring a *variable* of an already-defined enum type works inside a
  function body.

### 7.12. Type Aliases (`lit`)

Use **`lit`** to define a top-level alias for an existing type, like a C
`typedef`. Aliases have no runtime behavior: they expand to the aliased type
during parsing and then follow the same semantic rules as the original type.

```c
lit rizz Count;
lit rizz *IntPtr;
lit gang Point PointAlias;
lit gyatt Color Paint;
```

C-style anonymous aggregate typedefs are also supported:

```c
lit gang {
    rizz x;
    rizz y;
} Point;

lit chungus {
    rizz i;
    chad f;
} Data;
```

You can also provide an aggregate tag, matching C's
`typedef struct point { ... } Point;` form:

```c
lit gang point {
    rizz x;
    rizz y;
} Point;

lit gang Point {
    rizz x;
    rizz y;
} Point;
```

After an alias is defined, use it anywhere a type can be declared:

```c
Count n = 3;
IntPtr p = &n;
PointAlias pt = {1, 2};
Paint favorite = GREEN;
```

Pointer aliases compose with additional `*` declarators. For example,
`IntPtr *pp;` declares a pointer to an `IntPtr`, equivalent to `rizz **pp;`.

Aliases can be used for variables, scalar arrays, function parameters,
function return types, and struct/union fields. Aliases to named structs and
unions keep the underlying tag identity, so `PointAlias` still has the exact
type `gang Point`; anonymous aggregate aliases create a new aggregate type
whose public name is the alias; named inline aggregate aliases additionally
make the tag available through normal `gang tag`/`chungus tag` syntax.

Current limitations:

- `lit` declarations are top-level only.
- The alias target must already be defined; there are no forward typedefs or
  incomplete aggregate aliases such as `lit gang Ghost Alias;`.
- Array typedefs and function-pointer typedefs are not supported.
- Storage-class modifiers such as `salty` are not accepted on `lit`
  declarations; apply them where the alias is used instead. Repeating a
  width or signedness specifier the alias already has (`giga` on a `giga`
  alias, `nut` on a `nut` alias) is rejected as a conflict.
- Alias names are reserved type names and cannot be reused as variables,
  parameters, functions, enum constants, or other aliases. Aggregate tags
  (`gang`/`chungus`/`gyatt`) live in a separate namespace, so a typedef name can
  share a spelling with a tag in either declaration order, matching C
  typedef/tag behavior.

### 7.13. Modules (`#cooked`)

`#cooked` is Brainrot's `#include`. The quoted form splices another
`.brainrot` file's function and struct definitions into the current file at
the point of the directive, so a program can be split across multiple files.
The angle-bracket form names a module instead of a path, and can resolve to
either a `.brainrot` file (spliced in the same way) or a native `.so`
(dlopen'd and registered) — a native module's functions become ordinary
native calls once cooked, indistinguishable from the core standard
library's own.

#### Syntax

```c
#cooked "path/to/file.brainrot"
#cooked <module_name>
```

The directive must be alone on its line (leading whitespace is fine).

#### Path resolution

A relative quoted path is resolved relative to the directory of the file
*containing* the `#cooked` directive (like C's quote-form `#include`), not
the current working directory `brainrot` was invoked from. An absolute
quoted path is used as-is.

The angle-bracket form takes a bare module name (no `/`) and resolves it by
searching, in order:

1. Each directory in `$BRAINROT_PATH` (colon-separated), if set.
2. Exactly one of the following two — never both, so an install can never
   shadow a source build or vice versa:
   - The install module directory (`/usr/local/lib/brainrot`), if the
     running `brainrot` executable's own directory is the install bin
     directory (`/usr/local/bin`) — i.e. this *is* an installed binary.
   - Otherwise, a `stdrot/` directory next to the running executable
     itself, so an uninstalled build resolves a module sitting in its own
     source tree with no install step.

   "The running executable" means the actual binary that's executing, not
   `argv[0]` — a bare command name typed at a shell prompt (as opposed to
   `./brainrot` or an absolute path) carries no directory information at
   all, so this is resolved via the OS (`/proc/self/exe` on Linux,
   `_NSGetExecutablePath` on macOS) rather than guessed from `argv[0]` and
   the current working directory.

Within each directory, a `<module_name>.brainrot` file is checked before a
`<module_name>.so` file — one syntax, one search path, two possible artifact
kinds. The first directory containing either wins.

#### What can be included

A `#cooked`-included `.brainrot` file (either form) is spliced in as
top-level content, so it should contain only function and struct
definitions — **not** its own `skibidi main`. (A Brainrot program has
exactly one `main`; splicing in a second one is a parse error.)

A `#cooked <name>` that resolves to a native `.so` instead is dlopen'd
(`RTLD_LOCAL`, not the core library's `RTLD_GLOBAL`) and must export a
`StdrotAPI brainrot_module_init_v3(void)` entrypoint — the native counterpart
of the core standard library's own `stdrot_get_api_v3()`, built the exact
same way (`stdrot/registry.c`'s linker-section collection of every
`STDROT_EXPORT_SIG()` in the module, exported under the module-specific name
instead via `-DSTDROT_REGISTRY_ENTRYPOINT=brainrot_module_init_v3` — see
`tests/nativemodules/testnative.c` for a minimal example). Every cooked
module exports that *same* entrypoint name; that's fine because it's always
looked up by that specific module's own `dlopen` handle (never a
process-wide symbol search), and `RTLD_LOCAL` keeps it from ever entering
the global symbol scope in the first place — see `stdrot/registry.c`'s own
comment for why an *internal* cross-symbol call from inside a module (as
opposed to this handle-scoped lookup) is the actual hazard this design
avoids. Its exported functions become callable alongside the core
library's and any other already-cooked module's; a name colliding with
either is a load-time error naming the existing source.
Missing or ABI-incompatible `brainrot_module_init_v3()`, and a malformed
function table, are both load-time errors for the same reason a
`libstdrot.so` built against an incompatible ABI is (see `stdrot_load()`,
`stdrot.c`) — a native module is exactly as fragile as the core library.

#### Include-once and circular includes

Cooking the same artifact more than once (e.g. two modules that both
`#cooked` a shared third one) is a no-op after the first time — for a
`.brainrot` file this is handled automatically, and a native `.so` is simply
never `dlopen`'d twice.

Include-once being **structural rather than a convention** is the reason
Brainrot has no include guards and is not getting any: C needs
`#ifndef`/`#define`/`#endif` precisely because its `#include` will happily
splice a file twice, so every header has to defend itself and a forgotten
guard is a real bug. Here there is nothing to forget. A `.brainrot` file that `#cooked`s itself, directly or
through a cycle of other files, is a compile error that reports the include
chain instead of hanging — a native module can't form a cycle this way
(loading one never re-enters the lexer), so only the "already loaded, no-op"
half applies to it.

#### Example

`mathutils.brainrot`:

```c
rizz square(rizz n) {
    bussin n * n;
}
```

`main.brainrot`:

```c
#cooked "mathutils.brainrot"

skibidi main {
    yapping("%d", square(6));
    bussin 0;
}
```

Output:

```
36
```

## 8. Extended User Documentation

Every function below is a native call, and every native call is an ordinary
expression: it can be used in a declaration's initializer, a condition, a
binary operand, or an argument to another call, not just as its own bare
statement. `cap ok = bet(some_condition);` and `rizz n = slorp();` are both
valid; a discarded return value in statement position (`bet(some_condition);`
on its own line) is fine too. **One exception**: `slorp()`'s zero-argument
scalar form (see [§8.6](#86-slorp)) is not a general expression -- it only
resolves at a handful of specific typed sites, not as a condition, a binary
operand, or a variadic argument.

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
var_type slorp();
rant slorp(yap buffer[N]);
```

`slorp` reads user input and returns it. It has two shapes:

- **Scalar** -- `slorp()` takes no arguments. It is a desugaring, not a
  general type-inference feature: the semantic analyzer recognizes exactly
  four site shapes where `slorp()` is the *entire* expression --
    - a declaration's initializer (`rizz i = slorp();`), including each
      leaf of a braced array/matrix initializer (`rizz a[2] = { slorp(), 1
      };`) -- and, for a braced struct initializer (`gang Point p = {
      slorp(), 1 };`), each leaf against *that field's own type*, not one
      shared element type (`Point.x`'s type here, not `Point`'s), the same
      "entire expression" restriction applying leaf-by-leaf;
    - an assignment's right-hand side (`i = slorp();`, `*p = slorp();`);
    - a function's `bussin` return value (`rizz f() { bussin slorp(); }`);
    - a native or user-defined function's argument, when that parameter
      has a single fixed type (`bet(slorp());`, `takes_int(slorp());` --
      not `yapping`'s variadic tail, which has no single fixed type to
      give).
  At one of those four sites, the type already known there (the
  declaration's declared type, the assignment target's type, the
  function's return type, or the parameter's type) becomes `slorp()`'s
  type, and it desugars into an ordinary `slorp<T>(T) -> T` call under the
  hood. **`slorp()` used as a sub-expression of something larger at one of
  those sites is not resolved** -- `slorp() + 1`, `slorp() == 1`, and a
  `slorp()` argument to a variadic native (`yapping`, `baka`) all fail with
  the same diagnostic as no context at all, because nothing propagates a
  type into an arithmetic/comparison operand or a variadic slot. Supported
  scalar types are `rizz`, `smol`, `chad`, `gigachad`, `cap`, and `yap` (a
  single character); a scalar `rant` isn't supported this way (dynamic
  string allocation is a separate concern) -- use the buffer form below
  instead. If none of the four sites applies, or the type found there
  isn't one of the above, this is a semantic error: `cannot infer type for
  slorp(); use it in a typed context`.
- **Buffer** -- `slorp(buffer)`, where `buffer` is a `yap buffer[N]`
  character array, reads a line into that fixed-size buffer and returns it
  (usable as a `rant`). Unlike the scalar form, this one *is* an ordinary
  expression -- assign it, compare it, pass it anywhere a `rant` fits.

**Example**:

```c
skibidi main {
    yapping("Enter a number:");
    rizz num = slorp();
    yapping("You typed: %d", num);

    yap name[32];
    slorp(name);
    yapping("Hello, %s!", name);
    bussin 0;
}
```

**Deprecated form**: `rizz num; rizz i = slorp(num);` -- passing an
already-declared variable purely as a type witness -- and the bare
write-back statement `slorp(num);` (which assigns back into `num` with a
deprecation warning on stderr) both still work for one release, matching
the pre-#229 calling convention. Prefer the contextual `slorp()` form
above for new code.

```c
skibidi main {
    rizz num;
    slorp(num);   /* deprecated: still works, warns on stderr */
    yapping("You typed: %d", num);
    bussin 0;
}
```

### 8.7. `bet`

```c
cap bet(cap condition, rant message);  /* message is optional */
```

- Tests a condition and terminates the program if it's false.
- Similar to C's `assert()` macro, but designed for runtime checks in Brainrot.
- When the condition fails, prints an error message to `stderr` with the line number and optional custom message.
- Useful for verifying assumptions and catching bugs during development.
- On success, returns `W` -- usable directly, e.g. `cap ok = bet(x > 0);`.
- `condition` must be `cap`; `message`, if given, must be `rant`. Both are
  checked at semantic-analysis time -- `bet(1, "msg")` and
  `bet(W, some_int)` are rejected before the program runs.

**Example**:

```c
skibidi main {
    rizz x = 10;
    bet(x > 0, "x must be positive");
    yapping("x is positive");
    bussin 0;
}
```

**What happens when the assertion fails:**

```c
skibidi main {
    bet(L, "this assertion must fail");
    yapping("This won't print");
    bussin 0;
}
```

Output:

```
Error: bet: assertion failed at line 2: this assertion must fail
```

The program terminates immediately when a `bet` fails, preventing further execution.

---

### 8.8. `gamba`

```c
rizz gamba();          /* unbiased value in [0, INT_MAX]        */
rizz gamba(rizz n);    /* unbiased value in [0, n)              */
rizz gamba(rizz lo, rizz hi);  /* unbiased value in [lo, hi], inclusive */
```

`gamba` is Brainrot's cryptographically safe random number generator. It is a
standard-library builtin (no `#cooked`, no keyword) backed by OpenSSL's
`RAND_bytes`, so `libcrypto` is a **required** native build dependency of
`libstdrot.so`.

- **Unbiased.** Ranges use rejection sampling, never `gamba() % n`, so every
  value in the range is equally likely -- no modulo bias.
- **Honest failures.** If the CSPRNG fails, `gamba` aborts with an error
  rather than returning a look-alike `0`. It never falls back to C's
  `rand()`/`random()` and there is no seed function (`gamba_seed` would be a
  security bug -- OpenSSL seeds itself).
- **Range checks.** `gamba(n)` requires `n > 0` and `gamba(lo, hi)` requires
  `hi >= lo`; an invalid range aborts with an error, it does not wrap.
- The three integer forms take no argument, one argument (`n`), or two
  arguments (`lo, hi`); all arguments are `rizz` and the result is always
  `rizz`.

> A filling form `gamba_bytes(buf, n)` (raw random bytes into a buffer) is
> planned but not yet available. On the WebAssembly build, which is
> intentionally OpenSSL-free, `gamba` errors instead of returning a value.

**Example**:

```c
skibidi main {
    rizz roll = gamba(1, 6);
    yapping("you rolled %d", roll);

    rizz nonce = gamba();
    yapping("nonce %d", nonce);
    bussin 0;
}
```

---

### 8.9. Strings: `yaplen`, `yapcat`, `yapcmp`, `yapidx`, `s[i]`, `s[i:j]`

```c
rizz yaplen(rant s);                  /* length in BYTES                    */
rant yapcat(rant a, rant b);          /* a joined to b, as a new string     */
rizz yapcmp(rant a, rant b);          /* -1 if a < b, 0 if equal, 1 if a > b */
rizz yapidx(rant hay, rant needle);   /* byte index of needle, or -1        */
```

The v1 string library. All four are standard-library builtins -- no `#cooked`,
no keyword -- and all of them take and return ordinary `rant` and `rizz`
values.

**Everything is bytes, not characters.** A `rant` is a length-prefixed byte
buffer, so `yaplen("é")` is `2`, not `1`: that is two bytes of UTF-8.
Comparison and searching are byte-wise for the same reason. Codepoint-aware
variants are not part of v1.

Bytes compare as **unsigned**, which is what C's `strcmp` and Go's
`strings.Compare` do. Every non-ASCII byte therefore sorts *after* every ASCII
one.

> ### ⚠️ A `yap[N]` buffer always has length `N`
>
> This is the neighbouring gotcha, and it bites harder than the byte/character
> one. A `rant` carries a real length. A `yap[N]` character buffer — the thing
> [`slorp`](#86-slorp) fills, and the usual way a program reads input —
> reports its **declared capacity**, not the length of the text in it.
>
> ```c
> yap buf[32];
> slorp(buf);                        🚽 user types "hi"
>
> yaplen(buf)          🚽 32, not 2
> yapcmp(buf, "hi")    🚽 1  -- "buf is greater", because it is 32 long
> yapidx(buf, "i")     🚽 1  -- correct
> yapcat(buf, "!")     🚽 a real 33-byte string; prints as "hi" because
>                      🚽 yapping stops at the first NUL, so the "!" is
>                      🚽 sitting 31 bytes past it
> ```
>
> So measure, compare and join a **`rant`**, not a raw buffer — and note there
> is currently no way to convert one into the other: `rant r = buf;` is
> rejected outright with *"Type mismatch in initialization of 'r': expected
> string, got char"*. A `yap[N]` stays a `yap[N]`.
>
> `yapidx` is the one that still behaves, because a bounded scan finds the
> needle at the same offset whether or not the trailing bytes are counted. If
> you only need "where is it" or "does it contain it", it works on a buffer.
>
> This is a property of how character arrays are passed to builtins, not of
> these four functions; they are simply the first to read the stored length
> rather than treating the bytes as a C string. Pinned by
> `test_cases/string_stdlib_char_buffer.brainrot`, and expected to change.

**The builtins never modify their arguments; `yapcat` returns a new string.**
Neither argument is touched, and the result is independent of both -- storing
it and then calling `yapcat` again leaves the first result untouched. The same
goes for `s[i:j]`: a slice is a **copy, not a view**, so mutating either the
slice or the string it came from leaves the other alone.

That is a narrower claim than "strings are immutable", and deliberately so --
`s[i] = c` writes a byte in place (see below).

- `yaplen` reads the stored length, so it is O(1) and is correct for a string
  containing an embedded NUL. It is not `strlen`.
- `yapcat("", s)` and `yapcat(s, "")` both equal `s`.
- `yapcmp` returns exactly `-1`, `0` or `1` -- never some other nonzero number
  -- so `yapcmp(a, b) == -1` is safe to write. When one string is a prefix of
  the other, the **shorter sorts first**: `yapcmp("app", "apple")` is `-1`.
- `yapidx` returns the index of the *first* match. A needle that is not present
  gives `-1`, and an **empty needle gives `0`**, matching Go's
  `strings.Index` -- so `yapidx(h, n) >= 0` is a correct "contains" test for
  every needle, including an empty one.

#### Indexing and slicing: `s[i]` and `s[i:j]`

These are **syntax**, not builtins — no `#cooked`, no function call.

```c
rant s = "hello world";

yap  c   = s[0];        🚽 'h'   -- one byte, as a yap
rant sub = s[0:5];      🚽 "hello" -- a NEW rant
```

- **`s[i]` yields a `yap`** — the byte at offset `i`. Index in `[0, len)`;
  anything else is a runtime error.
- **`s[i]` is assignable**: `s[0] = 74;` writes that byte in place, exactly as
  `yap buf[0] = 74;` does. Writes are bounds-checked by the same rule as reads,
  so `s[yaplen(s)] = c` is refused rather than scribbling past the buffer. The
  string's length never changes — you are overwriting a byte, not splicing.
  This is the one way a `rant`'s contents can be modified; every builtin
  returns a new string instead.
- **`s[i:j]` yields a new `rant`** — the half-open range `[i, j)`, so its
  length is `j - i`. Requires `0 <= i <= j <= len`.
- **Both slice bounds are required.** `s[:j]`, `s[i:]` and `s[:]` are not v1
  syntax.
- **No negative indices.** `s[-1]` does not mean "the last byte"; it is simply
  out of range. Use `s[yaplen(s) - 1]`.
- **Out of range is an error, not a clamp.** The program stops where the
  mistake is rather than quietly returning something shorter than you asked
  for.
- Bounds are **bytes**, exactly as with `yaplen` — so a two-byte UTF-8
  character is two indices, and slicing between them yields half a character.

Note the deliberate asymmetry: `s[len]` is an error, but `s[len:len]` is
**legal** and gives the empty string, because a slice's upper bound is
exclusive and so `len` is a valid *boundary* even though it is not a valid
*index*. `s[i:i]` is likewise legal and empty, which makes it a usable
starting value when building a string up in a loop.

Only a **named** `rant` can be indexed or sliced — `s[0:2]` works,
`yapcat(a, b)[0:2]` does not parse. Bind the intermediate to a variable
first. (The grammar shares its `IDENTIFIER [` prefix with array access so the
two stay unambiguous; allowing an arbitrary expression base is a possible
later change.)

> **Not in v1:** there is no split, replace, trim, or case conversion yet, and
> no omitted slice bounds or negative indices. Those are tracked separately;
> v1 is the measure/join/compare/search/index core that the rest builds on.

**Example**:

```c
skibidi main {
    rant name = yapcat(yapcat("Big", " "), "Chungus");

    yapping("%s", name);              🚽 Big Chungus
    yapping("%d", yaplen(name));      🚽 11
    yapping("%d", yapidx(name, " ")); 🚽 3

    edgy (yapidx(name, "Chungus") >= 0) {
        yapping("certified");
    }

    edgy (yapcmp("Chad", "Chadwick") < 0) {
        yapping("Chad sorts first");
    }
    bussin 0;
}
```

See [`examples/string_toolkit.brainrot`](../examples/string_toolkit.brainrot)
for a longer worked example of the builtins, and
[`examples/string_parsing.brainrot`](../examples/string_parsing.brainrot) for
indexing and slicing used to split a delimited record.

---

## 9. Limitations

- No built-in support for increment/decrement (`++`, `--`).
- Functions other than `skibidi main` not fully supported (unless you add them).
- Complex data structures beyond basic structs, and advanced memory management are not fully supported.
- Struct/union function parameters and return values must be a plain
  variable of the exact matching type (see [§7.9](#79-structs-gang)); arrays
  can't be passed or returned by value at all, only via a pointer parameter.
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
