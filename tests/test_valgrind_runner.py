"""Self-tests for run_valgrind_tests.sh's pass/fail gate (#203).

The runner used to fail only on Valgrind's --error-exitcode=100, so a child that
crashed (SIGSEGV/SIGABRT -> exit 128+N) or a mis-invocation that ran nothing was
reported green -- that is how #186 stayed hidden. These tests drive the actual
script against tiny crafted targets in a temp directory (via the TESTCASES_DIR
override the script exposes) and assert it:

  * passes a clean target (exit 0),
  * passes a target that exits non-zero the way a Brainrot parse/semantic error
    does -- Valgrind forwards that exit with no leaks, and the runner must NOT
    treat it as a failure, and
  * fails a target killed by a signal (SIGABRT here -> 134), the crash class the
    old exit-100-only gate let through.

Skipped when valgrind or a C compiler is unavailable (same tools the Valgrind CI
job and `make valgrind` already require).
"""

import os
import shutil
import subprocess
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNNER = REPO_ROOT / "run_valgrind_tests.sh"

_CC = shutil.which("cc") or shutil.which("gcc")

pytestmark = pytest.mark.skipif(
    shutil.which("valgrind") is None or _CC is None,
    reason="run_valgrind_tests.sh self-test needs valgrind and a C compiler",
)


def _compile(src: str, out: pathlib.Path) -> None:
    subprocess.run(
        [_CC, "-x", "c", "-o", str(out), "-"],
        input=src,
        text=True,
        check=True,
    )


def _run_runner(target: pathlib.Path, cases_dir: pathlib.Path):
    """Invoke the runner over a one-fixture temp dir against `target`."""
    env = {**os.environ, "TESTCASES_DIR": str(cases_dir)}
    return subprocess.run(
        ["bash", str(RUNNER), str(target)],
        env=env,
        capture_output=True,
        text=True,
    )


@pytest.fixture
def cases_dir(tmp_path):
    d = tmp_path / "cases"
    d.mkdir()
    # The crafted targets ignore argv, so the fixture's contents don't matter;
    # it only has to exist so the glob has one entry to iterate.
    (d / "probe.brainrot").write_text("skibidi main { bussin 0; }\n")
    return d


def test_clean_target_passes(tmp_path, cases_dir):
    target = tmp_path / "clean"
    _compile("int main(void) { return 0; }", target)
    result = _run_runner(target, cases_dir)
    assert result.returncode == 0, result.stdout + result.stderr


def test_application_error_exit_passes(tmp_path, cases_dir):
    # A non-zero exit with no Valgrind-detected error is how a Brainrot parse or
    # semantic error fixture behaves; the runner must forward it, not fail.
    target = tmp_path / "apperr"
    _compile("int main(void) { return 1; }", target)
    result = _run_runner(target, cases_dir)
    assert result.returncode == 0, result.stdout + result.stderr


def test_signal_crash_fails(tmp_path, cases_dir):
    # SIGABRT -> Valgrind exits 134; the old exit-100-only gate passed this.
    target = tmp_path / "crash"
    _compile("#include <stdlib.h>\nint main(void) { abort(); }", target)
    result = _run_runner(target, cases_dir)
    assert result.returncode != 0
    assert "crashed" in result.stdout
