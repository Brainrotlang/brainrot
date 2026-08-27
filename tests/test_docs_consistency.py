"""Developer-UX regression guards for the raylib/brainray docs and the
Makefile's self-documenting `help` target.

Two things have gone stale before and should not silently regress:

1. `make help` must keep listing the developer-facing targets. `help` is now
   generated from the `## ` annotations on each target line, so the only way it
   loses a target is if someone adds a target without annotating it -- this test
   asserts the essential set stays represented.

2. `sudo apt-get install libraylib-dev` is WRONG on Ubuntu (no such official
   package on 22.04/24.04; see docs/brainray.md). It must never come back as a
   copy-pasteable instruction: neither inside a fenced code block of any tracked
   Markdown file, nor anywhere in the Makefile. Prose that mentions the command
   to tell readers NOT to run it (inline `code`, not a fenced block) is fine.
"""

import os
import re
import subprocess

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

# Targets a contributor is expected to be able to discover via `make help`.
# Deliberately excludes internal fixture builders (badnatives, nativemodules,
# old-abi-sim, ensure-stdrot) and pattern rules, which are not run by hand.
ESSENTIAL_HELP_TARGETS = [
    "all",
    "lib",
    "debug",
    "release",
    "brainray",
    "play",
    "abi-check",
    "wasm",
    "wasm-test",
    "test",
    "valgrind",
    "install",
    "uninstall",
    "clean",
    "check-deps",
    "rebuild",
    "format",
    "format-check",
    "cppcheck",
    "tidy",
    "help",
]

# The known-bad apt instruction, in the forms someone might paste.
BAD_APT_PATTERN = re.compile(r"apt(?:-get)?\s+install\s+.*\blibraylib-dev\b")


def _tracked_files():
    out = subprocess.run(
        ["git", "ls-files"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    return out.stdout.split()


def test_make_help_lists_developer_targets():
    result = subprocess.run(
        ["make", "help"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    help_text = result.stdout
    missing = [t for t in ESSENTIAL_HELP_TARGETS if not re.search(
        rf"^\s+{re.escape(t)}\s", help_text, re.MULTILINE)]
    assert not missing, (
        f"`make help` no longer lists: {missing}. Add a `## description` to "
        f"each such target so the self-documenting help picks it up."
    )


def test_no_bad_ubuntu_raylib_command_in_code_blocks():
    """The bad apt command must not appear as an instruction (fenced code block
    in Markdown, or anywhere in the Makefile)."""
    offenders = []
    for rel in _tracked_files():
        path = os.path.join(REPO_ROOT, rel)
        try:
            with open(path, "r", encoding="utf-8") as fh:
                lines = fh.readlines()
        except (UnicodeDecodeError, OSError):
            continue

        if os.path.basename(rel) == "Makefile":
            for i, line in enumerate(lines, 1):
                if BAD_APT_PATTERN.search(line):
                    offenders.append(f"{rel}:{i}: {line.strip()}")
            continue

        if not rel.endswith(".md"):
            continue

        in_code_block = False
        for i, line in enumerate(lines, 1):
            if line.lstrip().startswith("```"):
                in_code_block = not in_code_block
                continue
            if in_code_block and BAD_APT_PATTERN.search(line):
                offenders.append(f"{rel}:{i}: {line.strip()}")

    assert not offenders, (
        "Found the known-bad Ubuntu raylib command presented as an "
        "instruction. `libraylib-dev` is not an official Ubuntu package; see "
        "docs/brainray.md. Offending lines:\n" + "\n".join(offenders)
    )
