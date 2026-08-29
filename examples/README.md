# Examples

This directory contains several example programs written in the Brainrot language variant demonstrated by the provided grammar. Each example highlights different language features.

---

## 1. Hello, World!

**File Name:** `hello_world.brainrot`

```c
skibidi main {
    yappin("Hello, World!\n");
    bussin 0;
}
```

### What It Does

- Prints `"Hello, World!"` to the standard output.
- Demonstrates a minimal working program in Brainraot language.
- Uses the `yappin` function to output text.
- Ends with `bussin 0;`, which acts like a `return 0;` in C.

---

## 2. FizzBuzz

**File Name:** `fizz_buzz.brainrot`

```c
skibidi main {
    nut rizz i;
    flex (i = 1; i <= 10; i = i + 1){
        edgy ( (i % 15) == 0 ) {
            yapping("FizzBuzz");
        } amogus edgy ( (i % 3) == 0 ) {
            yapping("Fizz");
        } amogus edgy ( (i % 5) == 0 ) {
            yapping("Buzz");
        } amogus {
            yapping("%d", i);
        }
    }
    bussin 0;
}
```

### What It Does

- Implements the classic FizzBuzz challenge for values of `i` from `1` to `15`.
- Prints:
  - **FizzBuzz** if a number is divisible by 15,
  - **Fizz** if divisible by 3,
  - **Buzz** if divisible by 5,
  - the number itself otherwise.
- Demonstrates:
  - Declarations (`nut rizz i;`) which sets a variable as signed int (because `nut` = `signed`, `rizz` = `int`).
  - `flex` loops (equivalent to `for` loops).
  - `edgy` (equivalent to `if`) statements and `amogus` (equivalent to `else`).
  - `yapping` for printing.

---

## 3. Bubble Sort

**File Name:** `bubble_sort.brainrot`

```c
skibidi main {
    rizz arr[10];
    rizz i;
    rizz j;
    rizz temp;

    🚽 Initialize array with some unsorted numbers
    arr[0] = 64;
    arr[1] = 34;
    arr[2] = 25;
    arr[3] = 12;
    arr[4] = 22;
    arr[5] = 11;
    arr[6] = 90;
    arr[7] = 42;
    arr[8] = 15;
    arr[9] = 77;

    🚽 Print original array
    yapping("Original array: ");
    flex (i = 0; i < 10; i = i + 1) {
        yapping("%d ", arr[i]);
    }

    🚽 Bubble sort
    flex (i = 0; i < 9; i = i + 1) {
        flex (j = 0; j < 9 - i; j = j + 1) {
            edgy (arr[j] > arr[j + 1]) {
                temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }

    🚽 Print sorted array
    yapping("Sorted array: ");
    flex (i = 0; i < 10; i = i + 1) {
        yapping("%d ", arr[i]);
    }

    bussin 0;
}
```

### What It Does

- Declares an integer array `arr` with 10 elements (`rizz` = `int`).
- Initializes and prints the unsorted array.
- Implements the **Bubble Sort** algorithm to sort the array in ascending order.
- Prints the sorted array.
- Demonstrates:
  - Array declarations and assignments (`arr[i] = some_value;`)
  - Nested `flex` loops for the sorting routine.
  - Conditionals with `edgy`/`amogus`.
  - Use of `yapping` to print results.

---

## 4. Simple 1D Heat Equation Simulation

**File Name:** `heat_equation_1d.brainrot`

```c
skibidi main {
    rizz N = 50;
    gigachad u[50];
    gigachad u_new[50];
    rizz i;
    rizz t;
    rizz timesteps = 100;

    🚽 Initialize array
    flex (i = 0; i < N; i = i + 1) {
        edgy ((i > 16) && (i < 33)) {
            u[i] = 100.0;
        } amogus {
            u[i] = 0.0;
        }
    }

    🚽 Time evolution using just comparisons
    flex (t = 0; t < timesteps; t = t + 1) {
        edgy (t % 10 == 0) {
            yappin("Timestep %d: ", t);
            flex (i = 0; i < N; i = i + 1) {
                yapping("%lf", u[i]);
            }
            yapping("");
        }

        🚽 Update interior points using only assignments
        flex (i = 1; i < N-1; i = i + 1) {
            edgy (u[i+1] > u[i]) {
                u_new[i] = u[i] + 1.0;  🚽 If right neighbor is higher, increase slightly
            } amogus edgy (u[i-1] > u[i]) {
                u_new[i] = u[i] + 1.0;  🚽 If left neighbor is higher, increase slightly
            } amogus edgy ((u[i-1] < u[i]) && (u[i+1] < u[i])) {
                u_new[i] = u[i] - 1.0;  🚽 If both neighbors are lower, decrease slightly
            } amogus {
                u_new[i] = u[i];       🚽 Otherwise keep same value
            }
        }

        u_new[0] = 0.0;
        u_new[N-1] = 0.0;

        flex (i = 0; i < N; i = i + 1) {
            u[i] = u_new[i];
        }
    }

    bussin 0;
}
```

### What It Does

- Demonstrates a **very** simplified 1D Heat Equation simulation or diffusion-like process (in a purely artificial sense).
- Initializes a 1D array `u` of size 50; elements from 17 through 32 are set to 100.0, others are 0.0.
- Runs 100 time steps of updates:
  - Every 10 steps, prints the state of the array.
  - At each step, updates `u[i]` based on comparisons with neighbors.
- Showcases:
  - Double-precision arrays with `gigachad` (mapped to `double`).
  - Conditionals (`edgy`/`amogus`) to handle different numeric comparisons.
  - Printing partial array states (`yappin`) and final newlines with `yapping`.

---

## 5. Two Sum leetCode problem

**File Name:** `twoSum.brainrot`

```c
skibidi main {
    rizz nums[] = {7, 11, 2, 15};
    rizz target = 9;
    rizz numsSize = maxxing(nums) / maxxing(nums[0]);
    rizz result[2];

    flex (rizz i = 0; i < numsSize - 1; i++) {
        flex (rizz j = i + 1; j < numsSize; j++) {
            edgy (nums[i] > nums[j]) {
                rizz temp = nums[i];
                nums[i] = nums[j];
                nums[j] = temp;
            }
        }
    }

    
    rizz left = 0;
    rizz right = numsSize -1;

    goon (left < right){
        rizz sum = nums[left] + nums[right];
        edgy (sum == target){
            result[0] = nums[left];
            result[1] = nums[right];
            bruh;
        } amogus edgy (sum < target){
            left++;
        } amogus {
            right--;
        }
    }

    edgy ( left < right){
        yappin("the two numbers are: %d and %d\n", result[0], result[1]);
    } amogus {
        yappign("No solution found\n");
    }

    bussin 0;
}

```

### What It Does

- Solves the classic Two Sum problem: find two numbers in the array `nums[]` that add up to the target value 9.
- The array is first sorted using a Bubble Sort algorithm (nested loops with `edgy`/`amogus` for comparison and swapping).
- The program uses a two-pointer technique (`left` and `right`) to find the pair that adds up to the `target` value.
- If the sum equals the target, the result is stored in the `result[]` array and printed.
- If no solution is found, it prints `"No solution found"`.
- Showcases:
    - Array manipulation.
    - Using a two-pointer approach to solve the problem efficiently.
    - Conditional checking with `edgy`/`amogus`.


## 6. Sieve of Eratosthenes
**File Name:** `sieve_of_eras.brainrot`
```c
skibidi main {
    rizz i;
    rizz p;
    cap prime[105];

    🚽 Initialize all numbers as prime (W = true)
    flex(i = 1; i <= 100; i = i + 1) {
        prime[i] = W;
    }

    🚽 Implement Sieve of Eratosthenes
    flex(p = 2; p * p <= 100; p = p + 1) {
        edgy(prime[p] == W) {
            flex(i = p * p; i <= 100; i = i + p) {
                prime[i] = L; 🚽 Mark multiples as not prime (L = false)
            }
        }
    }

    🚽 Print all prime numbers
    yapping("Prime numbers up to 100: ");
    flex(p = 2; p <= 100; p = p + 1) {
        edgy(prime[p]) { 
            yapping("%d ", p); 
        }
    }

    bussin 0;
}
```
- Declare the prime[105] array to mark prime numbers.
- Initialize all numbers as W (true/prime).
- Main loop of the Sieve of Eratosthenes to mark multiples of prime numbers as L (false/not prime).
- Print prime numbers from 2 to 100 using yapping.
- Showcases:
    - fill all prime array with true using `W`.
    - Using a two-pointer approach to solve the problem efficiently.
    - Conditional checking with `edgy`.


## Fibonacci Sequence
**File name:** `fibonacci.brainrot`

```
skibidi main{
    rizz first = 0;
    rizz second = 1;
    rizz next;  
    rizz count = 0;  
    rizz limit = 10;  

    yapping("%d", first);    
    yapping("%d", second);    
    count = count + 2;

    goon(count < limit){
        next = first + second;
        yapping("%d", next);

        first = second;
        second = next;
        
        count = count + 1;
    }

    bussin 0;
}
```
### What it does
- Prints the first 10 numbers of fibonacci sequence (`limit` variable).
- Initializes `first` and `second` terms with 0 and 1.
- Using a `goon` loop to find the next numbers, until limit.


## 8. Modules (`#cooked`)

**File Names:** `mathutils.brainrot`, `modules.brainrot`

`mathutils.brainrot`:

```c
rizz square(rizz n) {
    bussin n * n;
}

rizz cube(rizz n) {
    bussin n * n * n;
}
```

`modules.brainrot`:

```c
#cooked "mathutils.brainrot"

skibidi main {
    yapping("square(6) = %d", square(6));
    yapping("cube(3) = %d", cube(3));
    bussin 0;
}
```

Run with `./brainrot examples/modules.brainrot`.

### What It Does

- Demonstrates `#cooked` (Brainrot's `#include`): `modules.brainrot` splices
  in `mathutils.brainrot`'s functions and calls them from `main`.
- `mathutils.brainrot` is a module, not a standalone program — it has no
  `skibidi main` of its own, only function definitions, and can't be run
  directly.
- Showcases:
  - Splitting a program across multiple files.
  - Relative path resolution (`#cooked` paths resolve relative to the file
    containing the directive).

**File Name:** `modules_named.brainrot`

```c
#cooked <mathutils>

skibidi main {
    yapping("square(6) = %d", square(6));
    yapping("cube(3) = %d", cube(3));
    bussin 0;
}
```

Run with `BRAINROT_PATH=examples ./brainrot examples/modules_named.brainrot`.

### What It Does

- Demonstrates the angle-bracket form of `#cooked`: instead of a path,
  `<mathutils>` names a module and lets `$BRAINROT_PATH` (and the other
  module search-path tiers) find `mathutils.brainrot` for it.
- Reuses the same `mathutils.brainrot` module as the example above — the two
  `#cooked` forms splice in identical content, they just resolve the target
  file differently.

## 9. Ohio Engine — the first cursed game (raylib)

**File Name:** `raylib/ohio_engine.brainrot`

```c
#cooked <raylib>

skibidi main {
    rl_init_window(1280, 720, "Ohio Engine");
    rl_set_target_fps(60);

    rizz x = 640;
    rizz y = 360;
    rizz vx = 6;
    rizz vy = 5;

    🚽 A native cap result is landed in a cap variable first, then tested.
    cap running = W;

    goon (running) {
        cap boost = rl_is_key_down(32);   🚽 SPACE
        rizz step = 1;
        edgy (boost) { step = 2; }

        x = x + vx * step;
        y = y + vy * step;
        🚽 ... bounce off the walls (elided) ...

        rl_begin_drawing();
        rl_clear_background(20, 20, 20, 255);
        rl_draw_circle(x, y, 60.0, 255, 0, 255, 255);
        rl_draw_text("ABSOLUTE CINEMA", x - 130, y - 16, 32, 255, 255, 255, 255);
        rl_end_drawing();

        cap wants_close = rl_window_should_close();
        edgy (wants_close) { running = L; }
    }

    rl_close_window();
    bussin 0;
}
```

### What It Does

- Runs an actual raylib game loop: a bouncing "ABSOLUTE CINEMA" orb with live
  FPS. Hold **SPACE** to speed it up, **ESC** (or the window's close button)
  to quit.
- Demonstrates calling a real C library from Brainrot through the `brainray`
  native module, loaded with `#cooked <raylib>`.
- Colors are passed as four separate `r, g, b, a` integers and textures would
  be integer handles — the native ABI does not carry C structs by value yet
  (see [`raylib/README.md`](raylib/README.md) and
  [`docs/brainray.md`](../docs/brainray.md)).

**Requires raylib** (an optional dependency). Install a system raylib first —
on Ubuntu that is **not** `apt-get install libraylib-dev`; see the canonical
setup guide [`docs/brainray.md`](../docs/brainray.md#installing-raylib) (Ubuntu
PPA / source build, macOS `brew install raylib`). Then build the wrapper module
`brainray/raylib.so` and run (or just `make play`):

```bash
make brainray
BRAINROT_PATH=brainray ./brainrot examples/raylib/ohio_engine.brainrot
```

`#cooked <raylib>` loads `brainray/raylib.so`, not the system `libraylib.so`
directly, so `BRAINROT_PATH` must point at a directory containing it.
`make`, `make test`, and `make valgrind` do **not** build this module and do
not require raylib.

---

## 9b. Ohio Engine II — the same loop, generated (raylib)

**File Name:** `raylib/ohio_engine_gen.brainrot`

```c
#cooked <raylibgen>

skibidi main {
    rl_init_window(640, 360, "Ohio Engine II: Generated Boogaloo");

    gang Vector2 pos;
    pos.x = 320.0;
    pos.y = 180.0;

    gang Color orb;
    orb.r = 255;
    orb.g = 0;
    orb.b = 255;
    orb.a = 255;

    🚽 ... loop ...
    rl_draw_circle_v(pos, 60.0, orb);
}
```

**Description:** The same game loop as above, but through a binding nobody
wrote by hand.

The difference is the point. Road A's `rl_draw_circle(640, 360, 100.0, 255, 0,
255, 255)` flattens a position and a colour into seven loose scalars, because
that was all hand-written wrappers could carry. Here the same call passes real
aggregates by value — a `gang Vector2` and a `gang Color` — straight into
raylib's own `DrawCircleV(Vector2, float, Color)`.

Every function it calls is a generated adapter, and every `gang` it uses is a
generated declaration whose byte layout is `_Static_assert`ed against the real
raylib headers. It closes itself after 30 frames so it can run unattended.

```bash
make brainray-gen   # generate + compile the binding, then verify its ABI
make play-gen       # ... and run this example
```

**Key Concepts:**

- By-value struct arguments across the native ABI (`STDROT_STRUCT`).
- `#cooked <raylibgen>` resolves a generated *prelude*, which declares the
  `gang` types and `gyatt` constants and itself cooks the native module — so
  types and constants need no ABI support at all.
- Generating the binding needs only Python and the pinned
  `brainray/raylib_api.json`; only compiling it needs raylib.

See [`docs/brainray.md`](../docs/brainray.md#road-b--the-generated-binding) for
what the generator covers (378 of raylib's 617 functions) and what it
deliberately skips.

---

## 10. Array of Structs

**File Name:** `array_of_structs.brainrot`

```c
gang Player {
    rizz id;
    rizz score;
};

skibidi main {
    gang Player roster[4];
    rizz i;

    flex (i = 0; i < 4; i = i + 1) {
        roster[i].id = i;
        roster[i].score = ((i + 1) * 7) % 13;
    }

    rizz best = 0;
    flex (i = 0; i < 4; i = i + 1) {
        yapping("player %d scored %d", roster[i].id, roster[i].score);
        edgy (roster[i].score > roster[best].score) {
            best = i;
        }
    }

    yapping("top scorer: player %d (%d)", roster[best].id, roster[best].score);
    bussin 0;
}
```

### What It Does

- Declares `gang Player roster[4];` — an **array whose elements are whole
  structs**, laid out with the struct's own alignment.
- Index-then-access composes: `roster[i].id`, `roster[i].score`, and
  `roster[best].score` all address the right element's blob.
- Works with multi-dimensional arrays (`gang Point grid[2][2];`) and nested
  struct fields (`lines[i].a.x`) too. See
  [§7.9 of the language reference](../docs/the-brainrot-programming-language.md#79-structs-gang).

---

Feel free to explore and modify each example to learn more about how this language’s syntax and features work!
