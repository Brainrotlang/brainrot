# Contributing to Brainrot

Welcome to Brainrot! We're excited that you want to contribute. This document provides guidelines and information for contributing to the project.

## Code of Conduct

By participating in this project, you are expected to uphold our Code of Conduct (follows standard open source practices).

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

Make `run_valgrind_tests.sh` executable:

```bash
sudo chmod +x run_valgrind_tests.sh
```

Run:

```bash
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
4. Add appropriate tests in `tests/`
5. Add example usage in `examples/`

## Testing Guidelines

1. All new features must include tests
2. Test files go in `tests/`
3. Expected outputs should be added to `expected_results.json`
4. Tests should cover:
   - Happy path
   - Error conditions
   - Edge cases

## Pull Request Process

1. Update documentation as needed
2. Add or update tests
3. Ensure all tests pass
4. Update CHANGELOG.md if applicable
5. Reference any related issues

Example PR format:

```markdown
## Description

Brief description of changes

## Related Issue

Fixes #<issue_number>

## Type of Change

- [ ] Bug fix
- [ ] New feature
- [ ] Documentation update
- [ ] Performance improvement
```

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
