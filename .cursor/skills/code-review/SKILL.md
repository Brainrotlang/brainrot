---
name: code-review
description: Use when reviewing a diff, branch, or pull request in this repo (C interpreter + pytest suite) — checks memory safety, grammar/AST consistency, test coverage, and style before it's called ready to merge.
---

## When to Use

The user asks for a review of a diff, branch, or PR, or asks "is this ready to
merge" / "what's missing before I open a PR."

## Step-by-Step Instructions

1. **Get the diff**: `git diff main...HEAD` (or the range given). Read every
   changed hunk, not just the summary.
2. **Check it builds clean**: `make clean && make`. The project uses
   `-Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined` — any
   warning is a blocking issue, not a nit.
3. **Check it's tested**: `make test && make valgrind`. If the change touches
   `lang.l`/`lang.y`/`ast.c`/`semantic_analyzer.c`/`interpreter.c`/`stdrot/*`
   and no `test_cases/*.brainrot` + `tests/expected_results.json` entry was
   added or updated, flag that as missing coverage.
4. **Memory safety pass** (this codebase's biggest recurring bug class — see
   `254f89b`): for every new/changed allocation, ownership transfer, or
   pointer field, check there's a matching free/lifetime story and no
   use-after-free, double-free, or missing NULL-check. Pay special attention
   to `String`/`ASTNode`/symbol-table entries crossing function boundaries.
5. **Grammar/AST consistency**: if `lang.y` changed, check the corresponding
   AST node, semantic-analyzer case, and interpreter/visitor case were all
   updated together — a token added in only one layer is a half-finished
   feature.
6. **Style pass**: 4-space indent, snake_case, Allman braces, ~80 cols
   (`make format` should produce no diff). Check new public functions in
   headers are documented per `CONTRIBUTING.md`.
7. **Docs**: if a keyword/builtin's implementation status changed, check
   `README.md`'s table and `docs/the-brainrot-programming-language.md` were
   updated to match.
8. **PR hygiene**: check the change follows `.github/PULL_REQUEST_TEMPLATE.md`
   (description, linked issue, type of change, checklist) if reviewing an
   actual PR.

## Conventions and Best Practices

- Prioritize correctness and memory safety over style — this is a C project
  built with sanitizers on by default; a leak or UB bug is a blocker, a
  formatting nit is not.
- Distinguish clearly between **blocking** issues (crashes, leaks, missing
  `-Werror` compliance, missing tests for behavior changes) and
  **suggestions** (naming, minor duplication) in your feedback.
- Don't request unrelated refactors or scope expansion in a review — flag them
  as optional follow-ups instead.

## Important Notes

- If `make test` or `make valgrind` can't be run in the current environment,
  say so explicitly rather than assuming the change passes.
- A grammar change with no shift/reduce-conflict check (`-Wcounterexamples`
  output) reviewed is an incomplete review — call it out if the PR doesn't
  show a clean build log.
