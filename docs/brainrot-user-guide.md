# Brainrot User Guide

# 1. Introduction

This language (informally called **Brainrot**) allows you to write a "main" function using the keyword `skibidi main`, declare integer variables with `rizz`, and use specialized keywords for loops (`goon` for while, `flex` for for-loops), conditionals (`edgy` for if, `amogus` for else), and more. It also includes three built-in print/error functions—`yapping`, `yappin`, and `baka`—to handle common output scenarios.

Below, you'll find a reference for each core feature, along with short code snippets illustrating proper usage.

---

# 2. Program Structure

A valid program must contain at least one **main function** defined by:

```c
skibidi main {
    🚽 ... statements ...
}
```

- **`skibidi main { ... }`**: The entry point of your program. Code within these braces executes first.
- The curly braces `{ }` enclose a series of **statements**.

### Minimal Example

```c
skibidi main {
    🚽 This is a simple program
    yapping("Hello from Brainrot!");
}
```

When run, it prints:

```
Hello from Brainrot!
```

_(followed by a newline due to `yapping`)_

---

# 3. Variables and Declarations

Use **`rizz`** as the type for declaring integer variables. For instance:

```c
rizz i = 10;
rizz count;
```

- **`rizz i = 10;`**: Declares an integer variable `i` with initial value 10.
- **`rizz count;`**: Declares an integer variable `count` (automatically initialized to 0 if your implementation sets a default, or remain uninitialized if your grammar does so—check your usage).

### Basic Assignment

```c
i = i + 1;
count = 42;
```

- This reassigns the variable using the typical `=` operator.

### Pointers (C-style)

Brainrot now supports C-style pointers with any level of indirection:

```c
rizz x = 10;
rizz *p = &x;
rizz **pp = &p;
```

- `*` in declarations means pointer type (`rizz *p`, `rizz **pp`, ...)
- `&expr` gets an address (address-of)
- `*expr` dereferences a pointer

Examples:

```c
yapping("%d", *p);     🚽 10
*p = 15;
yapping("%d", x);      🚽 15
yapping("%d", **pp);   🚽 15
```

---

# 4. Expressions and Statements

**Expressions** can be numeric literals (`42`), identifiers (`i`), operations (`i + 1`, `i < 5`), or function calls (`yapping(...)`). **Statements** include declarations, assignments, loops, conditionals, and so on.

A **statement** often ends with a **semicolon** `;` unless it is a compound statement (like `{ ... }`).

### Increment and Decrement Operators (`++`, `--`)

In addition to basic arithmetic and logical expressions, you can also use **increment** (`++`) and **decrement** (`--`) operators in Brainrot.

- **Pre-Increment (`++i`)**: Increments the value of `i` by 1 before it is used in an expression.
- **Post-Increment (`i++`)**: Uses the current value of `i`, then increments it by 1.
- **Pre-Decrement (`--i`)**: Decrements the value of `i` by 1 before it is used in an expression.
- **Post-Decrement (`i--`)**: Uses the current value of `i`, then decrements it by 1.

Pointer-specific unary operations are also available:

- `&value` (address-of)
- `*ptr` (dereference)

You can use dereference on the left-hand side of assignment:

```c
rizz n = 5;
rizz *p = &n;
*p = 42;
```

After this, `n` is `42`.

You can use these operators in expressions to simplify code and make it more concise.

Examples of valid statements:

```c
rizz i = 1;
i = i + 1;
yapping("%d", i);
```

---

# 5. Conditionals (`edgy` / `amogus`)

Use **`edgy (expression) { ... }`** to define an **if** statement. Optional **`amogus`** handles the else part.

```c
edgy (i < 5) {
    yapping("i is less than 5");
}
amogus {
    yapping("i is 5 or more");
}
```

- **`edgy`**: The "if" keyword.
- **`amogus`**: The "else" keyword.

You can nest these if you want multiple branches.

---

# 6. Loops

## 6.1. `goon` (While Loop)

Use **`goon (condition) { ... }`** as a while loop:

```c
goon (i < 5) {
    yapping("Inside while loop, i=%d", i);
    i = i + 1;
}
```

- Executes the body repeatedly **while** `(condition)` is **true**.
- Checks `(i < 5)` each iteration and stops once `i >= 5`.

## 6.2. `flex` (For Loop)

Use **`flex (init_expr; condition; increment) { ... }`** to define a for loop:

```c
flex (rizz j = 0; j < 3; j = j + 1) {
    yapping("j = %d", j);
}
```

- **`init_expr`**: A declaration or expression to initialize loop variables (e.g., `rizz j = 0`).
- **`condition`**: Checked each iteration (e.g., `j < 3`).
- **`increment`**: Executed at the end of each iteration (e.g., `j = j + 1`).

## 6.3. `mewing-goon` (Do While Loop)

Use **`mewing { ... } goon (condition)`** as a do-while loop:

```c
mewing {
    yapping("Inside while loop");
} goon (L)
```

---

# 7. Switch Statements (`ohio`)

Switch statements use the keyword **`ohio`**:

```c
ohio (expr) {
    sigma rule 1:
        yapping("Case 1");
        bruh;
    sigma rule 2:
        yapping("Case 2");
        bruh;
    based:
        yapping("Default case");
}
```

- **`ohio (expr)`**: The `switch` statement, evaluating `expr`.
- **`sigma rule`**: The `case` keyword (e.g., `sigma rule 1:` for `case 1:`).
- **`based`**: The `default` keyword.
- **`bruh`**: The `break` statement, optionally used to exit the switch after a case.

---

# 8. Structs (`gang`)

Use **`gang`** to define a struct type and declare struct variables.

### Struct Definition

Define a struct outside of `skibidi main` (at the top level):

```c
gang Point {
    rizz x;
    rizz y;
    chad magnitude;
};
```

- **`gang TypeName { ... };`**: Defines a new struct type.
- Fields are declared using any supported type keyword (`rizz`, `chad`, `gigachad`, `smol`, `cap`, `yap`).
- The definition must end with `};`.

### Struct Declaration

Declare a struct variable inside a function:

```c
gang Point p;
```

This allocates storage for all fields, zero-initialized.

### Initializer Syntax

You can initialize a struct at declaration time with a brace-enclosed list:

```c
gang Point q = {10, 20, 0.0};
```

Values are assigned to fields in order of declaration.

### Member Access

Use `.` to read or write individual fields:

```c
p.x = 3;
p.y = 4;
p.magnitude = 5.0;
yapping("Point: %d %d %f", p.x, p.y, p.magnitude);
```

### Full Example

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
}
```

Output:
```
Point: 3 4 5.0
Q: 10 20 0.0
```

### Keyword Reference

| Brainrot | C equivalent |
|----------|-------------|
| `gang`   | `struct`    |
| `.`      | `.`         |

### Current Limitations

- Nested struct access (`p.inner.x`) is not yet supported.
- Structs cannot be passed as function parameters or returned from functions.

---

# 9. Return Statements (`bussin`)

To return from **`skibidi main`** (or any function, if you support them), use **`bussin expression`**:

```c
bussin 0;
```

- This signals that your program (or function) finishes execution and returns the given value.

---

# 9.1 Call by Reference (via pointers)

Brainrot uses pointer-based call by reference, just like C.

```c
rizz bump(rizz *v) {
    *v = *v + 1;
    bussin 0;
}

skibidi main {
    rizz x = 10;
    bump(&x);
    yapping("%d", x);  🚽 11
    bussin 0;
}
```

For multi-level reference passing, use `**`, `***`, etc.

---

# 10. Built-In Functions

Brainrot includes some built-in functions for convenience:

| Function     | Destination | Auto Newline | Typical Usage                                                         |
| ------------ | ----------- | ------------ | --------------------------------------------------------------------- |
| **yapping**  | `stdout`    | Yes, always  | Quick line-based printing (adds `\n` automatically)                   |
| **yappin**   | `stdout`    | No           | Precise control over spacing/newlines                                 |
| **baka**     | `stderr`    | No           | Log errors or warnings, typically no extra newline                    |
| **ragequit** | -           | -            | Terminates program execution immediately with the provided exit code. |
| **chill**    | -           | -            | Sleeps for an integer number of seconds.                              |
| **slorp**    | `stdin`     | -            | Reads user input.                                                     |
| **bet**      | `stderr`    | No           | Tests conditions and terminates with error message if false.          |

## 10.1. yapping

**Prototype**

```c
void yapping(const char* format, ...);
```

**Key Points**

- Behaves like `printf`, but **always** appends a newline afterward.
- If your format string itself ends with `\n`, you effectively get **two** line breaks.

### Example

```c
yapping("Hello %s", "User");
🚽 Prints => "Hello User" + newline
```

## 10.2. yappin

**Prototype**

```c
void yappin(const char* format, ...);
```

**Key Points**

- Behaves like `printf` with **no** additional newline.
- You must manually include `\n` if you want line breaks.

### Example

```c
yappin("Hello ");
🚽 Still on same line
yappin("World!\n");  🚽 One newline here
```

## 10.3. baka

**Prototype**

```c
void baka(const char* format, ...);
```

**Key Points**

- Writes to **`stderr`** by default (often used for errors).
- No automatic newline (unless your code or `format` includes it).

### Example

```c
baka("Error: something went wrong at %s\n", location);
```

## 10.4. ragequit

**Prototype**

```c
void ragequit(int exit_code);
```

**Key Points**

- Terminates program execution immediately with the provided exit code.
- Behaves like `exit(exit_code)`, but with more drama.

### Example

```c
rizz i = 1;

edgy (i == 1) {
    yapping("Exiting with ragequit(1)");
    ragequit(1);  🚽 Program ends here with exit code 1
}
amogus {
    ragequit(0);  🚽 Exit code 0
}
```

## 10.5. chill

**Prototype**

```c
void chill(unsigned int seconds);
```

**Key Points**

- Sleeps for a specified number of seconds (must be an unsigned integer).

### Example

```c
skibidi main {
    yapping("I'll chill for 2 seconds ...");
    chill(2); 🚽 sleep for 2 seconds
    yapping("Ok imma head out");
    bussin 0;
}
```

## 10.6. slorp

**Prototype**

```c
void slorp(var_type var_name);
```

**Key Points**

- Reads user input (similar to C's `scanf` but safer).

### Example

```c
skibidi main {
    rizz num;
    yapping("Enter a number:");
    slorp(num);
    yapping("You typed: %d", num);
    bussin 0;
}
```

## 10.7. bet

**Prototype**

```c
void bet(int condition, const char* message);
```

**Key Points**

- Tests a condition and terminates the program if it's false.
- Similar to C's `assert()` macro.
- When the condition fails, prints an error message with the line number and optional custom message.

### Example

```c
skibidi main {
    rizz x = 10;
    bet(x > 0, "x must be positive");
    yapping("x is positive");
    bussin 0;
}
```

### Failed Assertion Example

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

---

# 11. Example Program

Below is a short **full** example showing variable declarations, loops, conditionals, and printing:

```c
skibidi main {
    🚽 Declare a variable and initialize
    rizz i = 1;

    🚽 While loop (goon) runs while i < 5
    goon (i < 5) {
        🚽 Print a message with a newline automatically
        yapping("AAAAAH A GOONIN LOOP, i=%d", i);

        🚽 Alternatively, print precisely (no auto newline)
        yappin("Just i => %d\n", i);

        🚽 Increment i
        i = i + 1;
    }

    edgy (i == 5) {
        🚽 If i is exactly 5, print a message
        yapping("We ended with i=5");
    }
    amogus {
        🚽 Otherwise, do something else
        baka("Unexpected i value: %d\n", i);
    }

    🚽 Return from main
    bussin 0;
}
```

---

## 12. Additional Notes

- **Keywords** like `skibidi`, `rizz`, `goon`, `flex`, `edgy`, `amogus`, `gang`, etc., are specialized synonyms for standard concepts (`main`, `int`, `while`, `for`, `if`, `else`, `struct`, etc.).
- **Syntax** is otherwise quite C-like: `;` to end statements, braces `{ }` to define blocks, parentheses `( )` around conditions.
- **Expressions** accept typical operators (`+`,`++`, `-`,`--`, `*`, `/`, `%`, relational, logical) plus the assignment operator `=`, matching standard precedence rules.
- **Escapes in strings** (`"\n"`, `"\t"`, etc.) may require an unescape function in your lexer, so check that it's converting them into real newlines or tabs at runtime.
````
