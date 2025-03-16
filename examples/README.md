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


Feel free to explore and modify each example to learn more about how this language’s syntax and features work!
