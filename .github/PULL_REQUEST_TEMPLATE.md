## Description

<!-- Briefly describe your changes, including any relevant motivation or context. -->

## Related Issue

<!-- If this PR fixes an issue, include "Fixes #<issue_number>". -->

Fixes #<issue_number>

## Type of Change

- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to change)
- [ ] Documentation update
- [ ] Performance improvement
- [ ] Refactor

## Checklist

- [ ] My code follows the style guidelines of this project
- [ ] I have performed a self-review of my own code
- [ ] I have documented my changes in the code or documentation
- [ ] I have added tests that prove my change works, at the lowest appropriate layer (`test_cases/*.brainrot` for language-visible behavior; a host-side C test wired into `make test` for internal APIs Brainrot source cannot reach). Required for every bug fix, feature, refactor, performance change, and breaking change — not optional, not "if applicable". See CONTRIBUTING.md ("TESTS ARE MANDATORY").
- [ ] Those tests cover the happy path, the original bug (for fixes), error cases, edge cases, **and** adversarial cases. If the existing harness could not reach the behavior, I extended it or added a lower-level test.
- [ ] If this PR cannot affect program behavior (docs/comments/license only), I explained that in the Description instead of skipping the items above in silence
- [ ] I have run `make format-check` locally (or `make format` to fix)
- [ ] I have run the unit tests locally
- [ ] I have run the valgrind memory tests locally
- [ ] All new and existing tests pass
