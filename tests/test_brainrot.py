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

@pytest.mark.parametrize("example,expected_output", expected_results.items())
def test_brainrot_examples(example, expected_output):
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    example_file_path = os.path.abspath(os.path.join(script_dir, f"../test_cases/{example}.brainrot"))

    if example.startswith("slorp_int"):
        command = f"echo '42' | {brainrot_path} {example_file_path}"
    elif example.startswith("slorp_short"):
        command = f"echo '69' | {brainrot_path} {example_file_path}"
    elif example.startswith("slorp_float"):
        command = f"echo '3.14' | {brainrot_path} {example_file_path}"
    elif example.startswith("slorp_double"):
        command = f"echo '3.141592' | {brainrot_path} {example_file_path}"
    elif example.startswith("slorp_char"):
        command = f"echo 'c' | {brainrot_path} {example_file_path}"
    elif example.startswith("slorp_bool"):
        command = f"echo '1' | {brainrot_path} {example_file_path}"
    elif example.startswith("slorp_string"):
        command = f"echo 'skibidi bop bop yes yes' | {brainrot_path} {example_file_path}"
    elif example in ("slorp_identity_char_array", "native_cstring_param_char_array",
                      "native_char_array_access",
                      "identity_string_use_after_free",
                      "identity_ownership_nonstring_result"):
        command = f"echo 'hello' | {brainrot_path} {example_file_path}"
    elif example == "native_char_param_scalar":
        command = f"echo 'c' | {brainrot_path} {example_file_path}"
    elif example in ("native_call_self_init", "native_sizeof_no_execution"):
        command = f"echo '42' | {brainrot_path} {example_file_path}"
    elif example == "native_call_loop":
        command = f"printf '1\\n2\\n3\\n' | {brainrot_path} {example_file_path}"
    elif example == "native_call_string_arg":
        command = f"printf 'skibidi\\nq\\n' | {brainrot_path} {example_file_path}"
    elif example == "native_call_do_while":
        command = f"printf '5\\n50\\n6\\n150\\n' | {brainrot_path} {example_file_path}"
    else:
        command = f"{brainrot_path} {example_file_path}"

    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=True)

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
