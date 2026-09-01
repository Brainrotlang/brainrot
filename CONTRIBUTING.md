# Contributing to Brainrot

Welcome to Brainrot! We're excited that you want to contribute. This document
provides guidelines and information for contributing to the project.

---

# TESTS ARE MANDATORY

**Read this before you open a pull request.**

Every pull request that can change how Brainrot lexes, parses, type-checks,
interprets, links, or otherwise behaves **must include tests that prove the
change works.** This is not optional. This is not "nice to have." This is not
"if applicable." This is the contributing guide, and it is the rule.

This applies to **all** such PRs, including:

- Bug fixes
- New features
- Breaking changes
- Performance improvements
- Refactors
- Interpreter, lexer, parser, AST, semantic-analyzer, or visitor changes
- Standard library (`stdrot/`) changes
- Test-harness, Makefile, or CI changes that affect how programs are built or
  run

**You cannot claim this document said nothing about tests.** It says it here,
in the first section, in plain language: **you must prove your change works
with tests.** A PR that asks reviewers to take your word for it is not ready
for review and will be sent back.

"I ran it on my machine" is not a test. "The old suite still passes" is not
proof of a *new* fix or feature. Add a regression that would have **failed
before your change** and **pass after it**, at the layer that can actually
exercise the broken contract (see below).

## What your tests must cover

For the behavior you are changing, include all of the following that apply —
and if you think one does not apply, you are probably wrong:

1. **Happy path.** The intended use actually works.
2. **The bug itself (fixes).** A regression that reproduced the failure
   *before* the patch and now asserts the correct result. Put that test at
   the **lowest appropriate layer that can exercise the broken contract**:
   - Prefer `test_cases/*.brainrot` for language-visible behavior (anything
     a Brainrot program can observe).
   - Internal runtime/library behavior that cannot be reached from Brainrot
     source must be tested with a host-side C test that calls the API
     directly, built with the same ASan/UBSan flags as the rest of the tree,
     and **wired into `make test`**. `arena_reset()` is the textbook case:
     it currently has no Brainrot-level caller, so a `.brainrot` fixture
     cannot prove the fix. The proper test is a host binary that exercises
     the arena API (the same pattern as
     `tests/abi/struct_layout_abi_check.c`, which `make test` already runs
     via the `abi-check` target).
   If you cannot show the bug at *some* layer `make test` runs, you have
   not proven the fix.
3. **Error and rejection cases.** Invalid input is rejected with the expected
   diagnostic. Follow `semantic_error_*.brainrot` and `*_fail.brainrot`.
4. **Edge cases.** Empty input, zero, one, max/min, nested, already-at-bound,
   mixed types, the value next to the one you care about.
5. **Adversarial cases.** Inputs chosen to break the implementation, not to
   flatter it: aliasing, double evaluation, out-of-bounds, type confusion,
   use-after-free-shaped programs, the neighbor of the case you fixed, the
   thing a hostile user would try after reading your patch. If you can imagine
   a way the fix is incomplete, that is a test you still owe.

If the existing harness cannot test the behavior, extend the harness or add
an appropriate lower-level test. "Hard to test" means you have not figured
out how to test it yet. It is not a waiver.

## The only exception

The **only** PRs that may omit new test fixtures are changes that **cannot
affect program behavior**: prose-only documentation, comments with no code
change, license / Code of Conduct / issue or PR templates, and this file.

If you are unsure whether your change can affect behavior, **it can — add
tests.** A documentation PR that also touches `lang.l`, `lang.y`, any `.c` /
`.h`, `stdrot/`, the test harness, or CI scripts is **not** documentation-only.

## How to add the tests

**Language-visible behavior** (a Brainrot program can hit it):

1. Add one or more programs under `test_cases/<descriptive_name>.brainrot`.
2. Add the expected stdout, stderr, and/or exit behavior to
   `tests/expected_results.json`, keyed by the fixture basename (no
   `.brainrot` suffix).
3. Name failure fixtures like the existing suite:
   `*_fail.brainrot`, `semantic_error_*.brainrot`.

**Internal runtime/library contracts** that Brainrot source cannot reach:

1. Add a host-side C test (a small binary that calls the API directly).
2. Build it with the same `$(CFLAGS)` as the rest of the tree (ASan/UBSan
   included).
3. Wire it into `make test` so it actually runs — follow
   `tests/abi/struct_layout_abi_check.c` and the `abi-check` Makefile
   target. A C file that is not a `make test` dependency is not a test.

Then, for every change:

4. Run `make test`. Your new tests must pass. Nothing else may break.
5. Run `make valgrind` (or `./run_valgrind_tests.sh`) for anything that
   executes as a Brainrot program. Host-side C tests are covered by the
   sanitizers `make test` already compiles them with.

Existing tests continuing to pass is **necessary and not sufficient.**

---

## Code of Conduct

By participating in this project, you are expected to uphold our [Code of Conduct](CODE_OF_CONDUCT.md).

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/yourusername/Brainrot.git`
3. Create a branch for your changes: `git checkout -b feature/your-feature-name`

## Development Environment

### Prerequisites

- C compiler (gcc recommended)
- Flex and Bison
- Valgrind
- clang-format
- cppcheck (>= 2.13)
- clang-tidy-15
- Make

### Building the Project

```bash
make clean
make
```

### Running Tests

The test suite can be run using:

```bash
make test
```

### Running Memory Leak Tests

Memory leak tests are run using Valgrind against the non-sanitized binary (`brainrot-valgrind`):

```bash
make valgrind
```

Alternatively, build the Valgrind binary and run the script directly:

```bash
make brainrot-valgrind
chmod +x run_valgrind_tests.sh
./run_valgrind_tests.sh
```

### Formatting

The CI `lint` job enforces the style in `.clang-format` via `make
format-check` (fails on any diff, doesn't modify files). Before opening a PR:

```bash
make format-check   # verify only
make format          # apply formatting in-place
```

`make format-check`/`make format` never touch generated Flex/Bison output
(`lang.tab.c`, `lang.tab.h`, `lex.yy.c`).

### Static Analysis

The CI `static-analysis` job runs cppcheck via `make cppcheck` (needs cppcheck
>= 2.13 — Ubuntu 22.04's apt package, 2.7, is too old). Before opening a PR:

```bash
make cppcheck
```

Justified suppressions live in `cppcheck-suppressions.txt`; add a new one only
with a comment explaining why the finding is a false positive, not to silence
a real issue.

The CI `static-analysis` job also runs clang-tidy via `make tidy` (needs
clang-tidy-15). Checks are configured in `.clang-tidy` — a small, curated
allowlist, deliberately not `checks=*`. Before opening a PR:

```bash
make tidy
```

A finding that's a real false positive for this codebase (not just
inconvenient) gets a `// NOLINT(check-name)` or `// NOLINTNEXTLINE(check-name)`
comment explaining why, right next to the flagged line — same bar as a
cppcheck suppression, just inline instead of in a separate file. If an entire
check turns out to be structurally noisy for this codebase rather than
occasionally wrong, drop it from `.clang-tidy`'s `Checks:` list instead of
NOLINT-ing every hit.

## Project Structure

- `ast.h` / `ast.c`: Abstract Syntax Tree implementation
- `lang.y`: Bison grammar file
- `lang.l`: Flex lexer file
- `examples/`: Example Brainrot programs
- `tests/`: Test suite

## Adding New Features

1. First, check existing issues and PRs to avoid duplicate work
2. Create an issue discussing the feature before implementing
3. Follow the existing code style
4. Add tests that **prove** the feature works — happy path, error cases, edge
   cases, and adversarial cases. See **TESTS ARE MANDATORY** at the top of
   this document. A feature PR without those tests will not be merged.
5. Add example usage in `examples/`

## Testing Guidelines

The policy is **TESTS ARE MANDATORY** at the top of this document. That
section is the rule. These bullets are the file locations, not a weaker
substitute:

1. **Every** bug fix, feature, refactor, performance change, and breaking
   change that can affect behavior must include tests that prove it. Not just
   new features.
2. Language-visible fixtures go in `test_cases/*.brainrot`, with expected
   output in `tests/expected_results.json`.
3. Internal runtime/library contracts that Brainrot source cannot reach get
   a host-side C test wired into `make test` (see
   `tests/abi/struct_layout_abi_check.c`).
4. Tests must cover happy path, the original bug (for fixes), error
   conditions, edge cases, **and** adversarial cases.
5. `make test` and `make valgrind` must both pass.

## Pull Request Process

1. **Add tests that prove the change works** (happy path, the bug you fixed,
   errors, edges, adversarial). Do this before asking for review. See
   **TESTS ARE MANDATORY**. PRs that skip this will be closed or sent back
   until the tests exist.
2. Update documentation as needed
3. Ensure `make test` and `make valgrind` both pass
4. Update CHANGELOG.md if applicable
5. Reference any related issues

Every PR must use `.github/PULL_REQUEST_TEMPLATE.md` — GitHub pre-fills it
automatically when you open a PR. Fill in every section (Description, Related
Issue, Type of Change, Checklist) rather than deleting or stripping it out.

## Style Guide

### C Code Style

- Use 4 spaces for indentation
- Maximum line length of 80 characters
- Function names use snake_case
- Constants use UPPER_SNAKE_CASE
- Add comments for complex logic
- Include parameter documentation for functions
- Enforced by `.clang-format` (Allman braces) — run `make format` before
  committing; CI's `lint` job rejects unformatted code

### Grammar Style

- Token names should be descriptive
- Use consistent naming patterns for similar concepts
- Document grammar rules with examples

## Documentation

- Keep README.md updated with new features
- Document all public functions
- Include examples for new features
- Use clear, concise language

## Bug Reports

When filing a bug report, include:

1. Brainrot version
2. Operating system
3. Complete error message
4. Minimal reproduction code
5. Expected vs actual behavior

## Getting Help

If you need help, you can:

1. Check existing issues
2. Create a new issue with your question
3. Add [HELP WANTED] tag for implementation assistance

## License

By contributing, you agree that your contributions will be licensed under the same terms as the main project.

## Acknowledgments

Thank you to all contributors who help make Brainrot better!
