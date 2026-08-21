import subprocess
import json
import os
import pytest

# Get the absolute path to the directory containing the script
script_dir = os.path.dirname(__file__)

# Construct the full path to the JSON file with expected results
file_path = os.path.join(script_dir, "expected_results.json")

# Load expected results from the JSON file
with open(file_path, "r") as file:
    expected_results = json.load(file)

# Single source of truth for which fixtures read stdin and what to feed
# them -- an ordered list of [prefix, stdin] pairs, first startswith() match
# wins. Shared verbatim with tests/run_wasm_tests.mjs (Node): both harnesses
# load this same file rather than keeping their own copy, since a fixture
# added to only one of them previously shipped broken (PR #230's wasm job
# failed on 9 fixtures whose stdin only existed in this file's old inline
# elif-chain, not in the wasm runner's separate hardcoded table).
with open(os.path.join(script_dir, "stdin_fixtures.json"), "r") as file:
    stdin_by_prefix = json.load(file)

def stdin_for(example):
    for prefix, stdin in stdin_by_prefix:
        if example.startswith(prefix):
            return stdin
    return None

@pytest.mark.parametrize("example,expected_output", expected_results.items())
def test_brainrot_examples(example, expected_output):
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    example_file_path = os.path.abspath(os.path.join(script_dir, f"../test_cases/{example}.brainrot"))

    result = subprocess.run(
        [brainrot_path, example_file_path],
        input=stdin_for(example),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    # "ExitCode:N" asserts only the process's exit code -- for cases whose
    # observable behavior *is* the exit code (e.g. ragequit(1.5) truncating
    # to 1) and has no other way to surface a mismatch through stdout/stderr.
    if expected_output.startswith("ExitCode:"):
        expected_code = int(expected_output.split(":", 1)[1])
        assert result.returncode == expected_code, (
            f"Command for {example} exited {result.returncode}, expected {expected_code}\n"
            f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
        )
        return

    actual_output = result.stdout.strip() if result.stdout.strip() else result.stderr.strip()

    if "Stderr:" in expected_output and result.stdout.strip():
        actual_output = f"{result.stdout.strip()}\nStderr:\n{result.stderr.strip()}"

    assert actual_output == expected_output.strip(), (
        f"Output for {example} did not match.\n"
        f"Expected:\n{expected_output}\n"
        f"Actual:\n{actual_output}"
    )

    if "Error:" not in expected_output:
        assert result.returncode == 0, (
            f"Command for {example} failed with return code {result.returncode}\n"
            f"Stderr:\n{result.stderr}"
        )


# Regression test for the canonical `yap[N]` buffer form (#229/#230):
# `yap name[32]; slorp(name);` must not print the deprecated scalar
# write-back warning (execute_func_call(), stdrot.c) -- that warning is
# for the pre-#204 scalar witness convention (`rizz x; slorp(x);`), not
# for a char-array buffer slorp() already wrote into in place. The
# JSON-driven test above only inspects stderr when stdout is empty, so a
# stray warning alongside real stdout would otherwise go unnoticed.
def test_slorp_buffer_form_has_no_deprecation_warning():
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    example_file_path = os.path.abspath(
        os.path.join(script_dir, "../test_cases/slorp_buffer_no_deprecation_warning.brainrot"))

    result = subprocess.run(
        [brainrot_path, example_file_path],
        input="Chad\n",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    assert result.stdout.strip() == "hello Chad", (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert "deprecated" not in result.stderr, (
        f"yap[N] buffer form should not warn as deprecated\n"
        f"Stderr:\n{result.stderr}"
    )


# ── validate_native_registry() rejection tests ──────────────────────────────
# Each tests/badnatives/*.c file (built by `make badnatives`) registers
# exactly one deliberately malformed StdrotEntry; validate_native_registry()
# (stdrot.c) must reject it during stdrot_load(), before test_cases/
# trivial_no_op.brainrot's single statement ever runs. Unlike every other
# test in this file, these override STDROT_LIB_PATH per-subprocess (never
# the shared tests/libstdrot.so the rest of the suite uses) since the
# malformed registry must not be visible to any other test.
REGISTRY_REJECTION_CASES = [
    ("identity_non_any.so",
     "return_like_arg (0) names a parameter that isn't STDROT_ANY"),
    ("null_fn.so", "fn is NULL"),
    ("duplicate_name.so", "duplicate native export 'bad_duplicate'"),
    ("negative_pointer_level.so",
     "params[0].pointer_level (-1) must be >= 0"),
    ("pointer_level_without_ptr.so",
     "params[0].pointer_level (1) must be 0 when params[0].type isn't STDROT_PTR"),
    ("none_typed_param.so",
     "params[0].type must not be STDROT_NONE"),
    ("invalid_return_type.so",
     "return_type.type (999) is not a valid StdrotType"),
    ("invalid_param_type.so",
     "params[0].type (999) is not a valid StdrotType"),
    ("bad_api_table_negative_count.so",
     "registry function_count (-1) is not a plausible value"),
    ("bad_api_table_null_functions.so",
     "registry function_count (1) is > 0 but functions is NULL"),
]


@pytest.mark.parametrize("bad_lib,expected_message", REGISTRY_REJECTION_CASES)
def test_bad_registry_rejected_at_load(bad_lib, expected_message):
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    trivial_path = os.path.abspath(
        os.path.join(script_dir, "../test_cases/trivial_no_op.brainrot"))
    bad_lib_path = os.path.abspath(
        os.path.join(script_dir, "badnatives", bad_lib))

    assert os.path.exists(bad_lib_path), (
        f"{bad_lib_path} not found -- run `make badnatives` first")

    env = dict(os.environ, STDROT_LIB_PATH=bad_lib_path)
    result = subprocess.run([brainrot_path, trivial_path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env)

    assert result.returncode == 1, (
        f"Loading {bad_lib} should abort with exit code 1, got "
        f"{result.returncode}\nStdout:\n{result.stdout}\n"
        f"Stderr:\n{result.stderr}"
    )
    assert expected_message in result.stderr, (
        f"Expected stderr for {bad_lib} to contain {expected_message!r}\n"
        f"Actual stderr:\n{result.stderr}"
    )


# Round-16 review finding #1: tests/old_abi_sim/fake_pre_v2_registry.so
# (built by `make old-abi-sim`) simulates a libstdrot.so built before
# STDROT_ABI_VERSION/stdrot_get_api_v2() existed -- it exports only the
# OLD "stdrot_get_api" symbol, under the OLD {name, fn} layout. Proves
# stdrot_load() (stdrot.c) detects the missing versioned symbol and fails
# loudly instead of reinterpreting this .so's actual memory as the
# current ABI shape.
def test_old_abi_rejected_at_load():
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    trivial_path = os.path.abspath(
        os.path.join(script_dir, "../test_cases/trivial_no_op.brainrot"))
    old_abi_lib_path = os.path.abspath(
        os.path.join(script_dir, "old_abi_sim", "fake_pre_v2_registry.so"))

    assert os.path.exists(old_abi_lib_path), (
        f"{old_abi_lib_path} not found -- run `make old-abi-sim` first")

    env = dict(os.environ, STDROT_LIB_PATH=old_abi_lib_path)
    result = subprocess.run([brainrot_path, trivial_path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env)

    assert result.returncode == 1, (
        f"Loading a pre-ABI-versioning .so should abort with exit code "
        f"1, got {result.returncode}\nStdout:\n{result.stdout}\n"
        f"Stderr:\n{result.stderr}"
    )
    assert "stdrot_get_api_v2" in result.stderr, (
        f"Expected stderr to name the missing versioned entrypoint\n"
        f"Actual stderr:\n{result.stderr}"
    )


if __name__ == "__main__":
    pytest.main(["-v", os.path.abspath(__file__)])
