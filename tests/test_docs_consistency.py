"""Developer-UX regression guards for the raylib/brainray docs and the
Makefile's self-documenting `help` target.

Two things have gone stale before and should not silently regress:

1. `make help` must keep listing the developer-facing targets. `help` is now
   generated from the `## ` annotations on each target line, so the only way it
   loses a target is if someone adds a target without annotating it -- this test
   asserts the essential set stays represented.

2. `apt[-get] install libraylib-dev` is WRONG *on Ubuntu* (no such official
   package on 22.04/24.04; see docs/brainray.md). It is *correct* on Debian
   testing/unstable, which do ship an official `libraylib-dev`. So the guard is
   scoped to the invariant we actually care about: an **Ubuntu-headed** section
   must never present that command as a fenced install step, and the Makefile
   must never hardcode it. A Debian-headed section using it is fine, and so is
   prose that mentions it to tell readers NOT to run it (inline `code`).
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
    """`apt install libraylib-dev` must not appear as a fenced install step under
    an Ubuntu heading, and must not appear anywhere in the Makefile. It is left
    alone under a Debian heading (Debian testing/unstable really ship it)."""
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
        heading = ""  # nearest preceding Markdown heading
        for i, line in enumerate(lines, 1):
            if line.lstrip().startswith("```"):
                in_code_block = not in_code_block
                continue
            if not in_code_block and line.lstrip().startswith("#"):
                heading = line.lower()
                continue
            if not (in_code_block and BAD_APT_PATTERN.search(line)):
                continue
            # Only an error when the governing section is Ubuntu-facing. A
            # Debian section (its official package IS libraylib-dev) is fine.
            if "ubuntu" in heading and "debian" not in heading:
                offenders.append(f"{rel}:{i} (under '{heading.strip()}'): "
                                 f"{line.strip()}")

    assert not offenders, (
        "Found `apt install libraylib-dev` as a fenced install step under an "
        "Ubuntu heading (or in the Makefile). It is not an official Ubuntu "
        "package on 22.04/24.04; see docs/brainray.md. Offending lines:\n"
        + "\n".join(offenders)
    )


# Every function brainray exports is spelled `STDROT_EXPORT_SIG("rl_...", ...)`.
BRAINRAY_EXPORT_PATTERN = re.compile(r'STDROT_EXPORT_SIG\(\s*"(rl_[a-z0-9_]+)"')

# A row of the function-reference table in docs/brainray.md, e.g.
#     | `rl_draw_fps(x, y)` | `DrawFPS` | |
# Anchored to the leading pipe so a passing mention in prose or in a fenced
# code block cannot satisfy the check -- the point is that the function is
# *documented in the table*, which is the only place a reader can look it up.
BRAINRAY_TABLE_ROW_PATTERN = re.compile(
    r"^\s*\|\s*`(rl_[a-z0-9_]+)\(", re.MULTILINE)


def test_brainray_docs_list_every_exported_function():
    """docs/brainray.md calls itself "the single source of truth" for the
    binding, and README.md plus examples/raylib/README.md both defer to it
    rather than repeating the function list. That only holds if the table
    actually keeps up: a wrapper added to brainray/raylib.c without a matching
    row is undiscoverable, because there is nowhere else to look it up.

    Deliberately checks for a *table row*, not merely for the name appearing
    somewhere in the file. A prose mention, a line in a fenced example, or a
    half-deleted row would all satisfy "the name is in the document" while
    leaving the reference table incomplete, which is the failure this guards."""
    with open(os.path.join(REPO_ROOT, "brainray", "raylib.c")) as f:
        exported = set(BRAINRAY_EXPORT_PATTERN.findall(f.read()))
    assert exported, "no STDROT_EXPORT_SIG entries found in brainray/raylib.c"

    with open(os.path.join(REPO_ROOT, "docs", "brainray.md")) as f:
        documented = set(BRAINRAY_TABLE_ROW_PATTERN.findall(f.read()))
    assert documented, (
        "no function-reference table rows found in docs/brainray.md -- the "
        "table format changed and this guard needs updating with it")

    missing = sorted(exported - documented)
    assert not missing, (
        "brainray exports these functions but docs/brainray.md's function "
        "reference table has no row for them:\n  "
        + "\n  ".join(missing)
        + "\nAdd a row to the table in docs/brainray.md -- it is the only "
          "place the binding is documented."
    )

    # The reverse direction: a row for a function that no longer exists sends
    # a reader looking for something that isn't there.
    stale = sorted(documented - exported)
    assert not stale, (
        "docs/brainray.md's function reference table has rows for functions "
        "brainray does not export:\n  "
        + "\n  ".join(stale)
        + "\nRemove the stale rows, or restore the wrappers."
    )
