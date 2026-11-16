# AGENTS.md

Brainrot is a C-like esoteric language interpreter: Flex lexer (`lang.l`) + Bison
parser (`lang.y`) → AST (`ast.c/h`) → semantic analysis (`semantic_analyzer.c/h`)
→ tree-walking interpreter (`interpreter.c/h`, `visitor.c/h`). Builtins live in
`stdrot/`, compiled into `libstdrot.so` and dlopen'd at runtime by `stdrot.c`
(statically linked instead for the wasm and Windows builds).

## Setup / Build / Test

```bash
make            # regenerates lang.tab.c/lex.yy.c, builds libstdrot.so + ./brainrot
make test       # build, then run the pytest suite (tests/) against test_cases/*.brainrot
make valgrind   # build ./brainrot-valgrind (no sanitizers), run every fixture under Valgrind
make format     # clang-format all .c/.h files
make format-check # check formatting without modifying files (what CI's `lint` job runs)
make cppcheck   # static analysis, half of CI's `static-analysis` job; needs cppcheck >= 2.13
make tidy       # clang-tidy, the other half of CI's `static-analysis` job; needs clang-tidy-15
make clean      # remove build artifacts (does NOT touch source)
make help       # list every developer-facing target (wasm, windows, release, ...)
```

Optional raylib bindings — never needed by `make`, `make test`, or
`make valgrind`, and only the last two lines require raylib installed:

```bash
make rayrot-gen-sources # generate the raylib binding; needs NO raylib
make rayrot             # hand-written module (Road A); needs raylib
make rayrot-gen         # compile the generated binding + ABI drift check
```

Run a single program: `./brainrot path/to/file.brainrot` (from the repo root:
it loads `./libstdrot.so` relative to the current directory).

Run one pytest case from the repo root, the way `make test` runs the suite:

```bash
make tests/libstdrot.so
STDROT_LIB_PATH=$PWD/tests/libstdrot.so python3 -m pytest -v -k <test_case_name>
```

Without `STDROT_LIB_PATH` (e.g. `cd tests && pytest`), the interpreter falls
back to whatever `libstdrot.so` the dynamic linker finds — an installed copy in
`/usr/local/lib` if there is one — so the test runs against stale builtins. A
bare `pytest` of the *whole* suite also lacks the native-module fixtures
`make test` builds first (`badnatives`, `nativemodules`, ...) and fails those
cases spuriously; use `make test` for full runs.

## Project Structure

- `lang.l` / `lang.y` — lexer/grammar. Generated `lang.tab.c`, `lang.tab.h`,
  `lex.yy.c` are gitignored; never hand-edit or commit them.
- `ast.c/h`, `visitor.c/h`, `semantic_analyzer.c/h`, `interpreter.c/h` — core pipeline.
- `stdrot.c/h` — the native ABI: loads `libstdrot.so` and `#cooked` native
  modules, and marshals arguments/results between AST values and natives.
- `lib/` — internal utilities (arena allocator, hashmap, memory helpers).
- `stdrot/` — standard library builtins, one function family per file
  (registered in `stdrot/registry.c`, ABI in `stdrot/stdrot_api.h`).
- `rayrot/` — the optional raylib binding (`docs/rayrot.md`).
- `test_cases/*.brainrot` — fixtures; each needs a matching entry in
  `tests/expected_results.json` (see `tests/test_brainrot.py`), except
  `#cooked` helper modules that only other fixtures include.
- `tests/` — the pytest harness plus host-side C tests and crafted native
  modules that `make test` builds (`abi/`, `arena/`, `badnatives/`,
  `nativemodules/`, `print/`, ...), and the wasm runners (`run_wasm_*.mjs`).
- `examples/` — user-facing sample programs referenced from `README.md`.
- `docs/` — language and stdlib reference.

## Code Style

4-space indent, snake_case functions/vars, UPPER_SNAKE_CASE constants, ~80 col
lines, brace-on-own-line (Allman), enforced by `make format` (clang-format) and
gated in CI by the `lint` job (`make format-check`); never hand-format the
gitignored `lang.tab.c`/`lang.tab.h`/`lex.yy.c`. Use **clang-format-15** — the
version the `Makefile` pins (`CLANG_FORMAT ?= clang-format-15`) and CI installs;
other major versions disagree on cosmetic spacing (e.g. `(type)-1` casts) and
report spurious `format-check` diffs (#187).

```c
ASTNode *arena_alloc_astnode(void)
{
    ASTNode *node = ARENA_ALLOC(ASTNode);
    if (node)
    {
        memset(node, 0, sizeof(ASTNode));
    }
    return node;
}
```

## Testing Requirements

Every new language feature or bug fix needs: a `.brainrot` file in `test_cases/`,
a corresponding expected-output entry in `tests/expected_results.json`, and must
pass both `make test` and `make valgrind` — the interpreter compiles with
`-fsanitize=address,undefined -Werror`, so warnings and sanitizer errors are
build failures, not suggestions. `libstdrot.so` is built with `-Werror` but
without sanitizers, so for builtins valgrind is the memory-safety gate.
Valgrind cannot run an ASan binary; `make valgrind` builds and uses
`./brainrot-valgrind` (no sanitizers) — do the same for a one-off valgrind
run (`make brainrot-valgrind`). Memory safety is a recurring source of real
bugs here; treat any new leak/UB as a blocker, not a follow-up.

## Pull Requests

Every PR must follow `.github/PULL_REQUEST_TEMPLATE.md` (Description, Related
Issue, Type of Change, Checklist) — fill it in, don't strip it out.

## Boundaries

- **Always**: run `make test` and `make valgrind` before calling a change done.
- **Always**: run `make format-check` (or `make format` to fix) before opening
  a PR — the CI `lint` job blocks on any diff.
- **Always**: run `make cppcheck` and `make tidy` before opening a PR — the
  CI `static-analysis` job runs both and blocks on any finding.
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
