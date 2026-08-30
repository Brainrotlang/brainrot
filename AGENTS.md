# AGENTS.md

Brainrot is a C-like esoteric language interpreter: Flex lexer (`lang.l`) + Bison
parser (`lang.y`) → AST (`ast.c/h`) → semantic analysis (`semantic_analyzer.c/h`)
→ tree-walking interpreter (`interpreter.c/h`, `visitor.c/h`). Builtins live in
`stdrot/`, compiled into `libstdrot.so` and dlopen'd at runtime.

## Setup / Build / Test

```bash
make            # regenerates lang.tab.c/lex.yy.c, builds libstdrot.so + ./brainrot
make test       # build, then run tests/test_brainrot.py against test_cases/*.brainrot
make valgrind   # build, then run run_valgrind_tests.sh over every test_cases/*.brainrot
make format     # clang-format all .c/.h files
make format-check # check formatting without modifying files (what CI's `lint` job runs)
make cppcheck   # static analysis (what CI's `static-analysis` job runs); needs cppcheck >= 2.13
make clean      # remove build artifacts (does NOT touch source)
```

Optional raylib bindings — never needed by `make`, `make test`, or
`make valgrind`, and only the last two lines require raylib installed:

```bash
make brainray-gen-sources # generate the raylib binding; needs NO raylib
make brainray             # hand-written module (Road A); needs raylib
make brainray-gen         # compile the generated binding + ABI drift check
```

Run a single program: `./brainrot path/to/file.brainrot`.
Run one pytest case: `cd tests && pytest -v -k <test_case_name>`.

## Project Structure

- `lang.l` / `lang.y` — lexer/grammar. Generated `lang.tab.c`, `lang.tab.h`,
  `lex.yy.c` are gitignored; never hand-edit or commit them.
- `ast.c/h`, `visitor.c/h`, `semantic_analyzer.c/h`, `interpreter.c/h` — core pipeline.
- `lib/` — internal utilities (arena allocator, hashmap, memory helpers).
- `stdrot/` — standard library builtins, one function family per file.
- `test_cases/*.brainrot` — fixtures; each needs a matching entry in
  `tests/expected_results.json` (see `tests/test_brainrot.py`).
- `examples/` — user-facing sample programs referenced from `README.md`.
- `docs/` — language and stdlib reference.

## Code Style

4-space indent, snake_case functions/vars, UPPER_SNAKE_CASE constants, ~80 col
lines, brace-on-own-line (Allman), enforced by `make format` (clang-format) and
gated in CI by the `lint` job (`make format-check`); never hand-format the
gitignored `lang.tab.c`/`lang.tab.h`/`lex.yy.c`.

```c
static int semantic_check_binop(ASTNode *node, SymbolTable *scope)
{
    if (node->type == AST_BINOP)
    {
        return check_type_compatibility(node->left, node->right, scope);
    }
    return 0;
}
```

## Testing Requirements

Every new language feature or bug fix needs: a `.brainrot` file in `test_cases/`,
a corresponding expected-output entry in `tests/expected_results.json`, and must
pass both `make test` and `make valgrind` — the codebase compiles with
`-fsanitize=address,undefined -Werror`, so warnings and sanitizer errors are
build failures, not suggestions. Memory safety is a recurring source of real
bugs here; treat any new leak/UB as a blocker, not a follow-up.

## Pull Requests

Every PR must follow `.github/PULL_REQUEST_TEMPLATE.md` (Description, Related
Issue, Type of Change, Checklist) — fill it in, don't strip it out.

## Boundaries

- **Always**: run `make test` and `make valgrind` before calling a change done.
- **Always**: run `make format-check` (or `make format` to fix) before opening
  a PR — the CI `lint` job blocks on any diff.
- **Always**: run `make cppcheck` before opening a PR — the CI
  `static-analysis` job blocks on any finding.
- **Always**: fill out `.github/PULL_REQUEST_TEMPLATE.md` in full when opening a PR.
- **Ask first**: changing existing keyword syntax/semantics in `lang.l`/`lang.y`
  (README's keyword table is a public compatibility surface).
- **Never**: commit generated files (`lang.tab.*`, `lex.yy.c`, `brainrot`,
  `libstdrot.so`) or disable `-Werror`/sanitizers to silence a warning. This
  covers generated *bindings* too (Appendix B Q7 in `docs/ROADMAP.md`): a
  binding generator's C output is derived and stays out of the repo, while a
  vendored, pinned API description it reads *in* (e.g. raylib's
  `raylib_api.json`) is an ordinary committed source file, not generated
  output.
