---
name: fix-bug
description: Use when diagnosing and fixing a bug in the Brainrot interpreter — crashes, wrong output, memory errors (ASan/UBSan/Valgrind), or a failing test_cases fixture. Covers root-causing across the lexer/parser/AST/semantic-analyzer/interpreter pipeline.
---

## When to Use

The user reports incorrect output, a crash, a compiler warning, a sanitizer
error, a Valgrind leak, or a failing pytest case for a `.brainrot` program.

## Step-by-Step Instructions

1. **Reproduce minimally.** Write or reduce a `.brainrot` snippet that triggers
   the bug; run it directly: `./brainrot repro.brainrot`. Compare against a
   `git stash`-clean build if unsure whether it's a regression.
2. **Reproduce under sanitizers/Valgrind first** if the symptom is a
   crash/garbage output — the project's `CFLAGS` already build with
   `-fsanitize=address,undefined`, so a plain `make && ./brainrot repro.brainrot`
   often surfaces the exact line. For leaks specifically, use
   `valgrind --leak-check=full --track-origins=yes ./brainrot repro.brainrot`.
3. **Locate the layer**: use the error/stack trace to figure out whether the
   bug is in lexing (`lang.l`), grammar (`lang.y`), AST construction (`ast.c`),
   semantic checks (`semantic_analyzer.c`), or execution
   (`interpreter.c`/`visitor.c`/`stdrot/*.c`). Add `fprintf(stderr, ...)` trace
   points if the sanitizer trace isn't enough — don't leave them in the fix.
4. **Fix at the root cause**, not the symptom. Check `TRUTH.md`-adjacent
   history: `git log -p -- <file>` for the surrounding function to see if this
   area has had prior memory-safety fixes (it has — see commit
   `254f89b "fix: multiple memory safety bugs..."` for the pattern to follow:
   ownership/lifetime bugs around `String`/pointer handling).
5. **Add a regression test**: put the reducing fixture in `test_cases/` (name
   it for the bug, e.g. `division_by_zero.brainrot` style) and add its correct
   expected output to `tests/expected_results.json` so it can't regress silently.
6. **Verify the full suite still passes**: `make test && make valgrind`. Fixing
   one bug must not reintroduce another — the whole point of these two gates.
   Also run `make format-check` — CI's `lint` job blocks on any diff.
7. **Open the PR using `.github/PULL_REQUEST_TEMPLATE.md`**: fill in every
   section (Description, Related Issue, Type of Change, Checklist) — don't
   submit a bare description.

## Conventions and Best Practices

- Prefer the smallest correct fix; don't refactor unrelated code in a bug-fix
  change.
- If the bug is a missing NULL-check/bounds-check, check whether the same
  pattern exists elsewhere in the same file (semantic analyzer and interpreter
  both walk the same AST shape) — sanitizer bugs here tend to be systemic, not
  one-off. Mention it to the user if you spot more of the same class you didn't
  fix.
- Never suppress a warning or sanitizer error by adding a cast, disabling a
  flag, or wrapping in an ignore — fix the underlying issue (see
  [AGENTS.md](../../../AGENTS.md) boundaries).

## Important Notes

- `-Werror` means any new compiler warning is a build failure — treat it as
  part of the bug, not noise to silence.
- If the bug can't be reproduced, say so explicitly and ask for a minimal
  `.brainrot` repro rather than guessing at a fix.
