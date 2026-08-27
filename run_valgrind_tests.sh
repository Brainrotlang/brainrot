#!/bin/bash

TARGET="${1:-./brainrot-valgrind}"

if ! command -v valgrind >/dev/null 2>&1; then
    echo "Error: valgrind is not installed or not in PATH" >&2
    exit 1
fi

if [[ ! -x "$TARGET" ]]; then
    echo "Error: Valgrind target '$TARGET' is not executable" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

python3 - "$TARGET" "$SCRIPT_DIR" << 'EOF'
import json
import os
import subprocess
import sys

target = sys.argv[1]
script_dir = sys.argv[2]
test_cases_dir = os.path.join(script_dir, "test_cases")

with open(os.path.join(script_dir, "tests", "stdin_fixtures.json"), "r") as f:
    stdin_fixtures = json.load(f)

with open(os.path.join(script_dir, "tests", "expected_results.json"), "r") as f:
    expected_results = json.load(f)

def get_stdin(base):
    for prefix, inp in stdin_fixtures:
        if base.startswith(prefix):
            return inp
    return None

def get_expected_exit(base):
    exp = expected_results.get(base, "")
    if exp.startswith("ExitCode:"):
        return int(exp.split(":", 1)[1])
    return 0

files = sorted(
    [
        os.path.join(test_cases_dir, f)
        for f in os.listdir(test_cases_dir)
        if f.endswith(".brainrot")
    ]
)

for file_path in files:
    base = os.path.splitext(os.path.basename(file_path))[0]
    print(f"Running Valgrind on test_cases/{base}.brainrot...")
    inp = get_stdin(base)
    expected_exit = get_expected_exit(base)

    cmd = [
        "valgrind",
        "--track-origins=yes",
        "--leak-check=full",
        "--error-exitcode=100",
        target,
        file_path,
    ]

    proc = subprocess.run(
        cmd,
        input=inp.encode() if inp is not None else None,
    )

    exit_code = proc.returncode

    if exit_code == 100:
        print(
            f"Valgrind detected memory issues in test_cases/{base}.brainrot",
            file=sys.stderr,
        )
        sys.exit(1)

    if exit_code not in (0, 1) and exit_code != expected_exit:
        print(
            f"Valgrind failed while running test_cases/{base}.brainrot (exit {exit_code})",
            file=sys.stderr,
        )
        sys.exit(1)

    print()
EOF
