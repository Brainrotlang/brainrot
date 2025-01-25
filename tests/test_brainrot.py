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
    elif example.startswith("slorp_string"):
        command = f"echo 'skibidi bop bop yes yes' | {brainrot_path} {example_file_path}"
    else:
        command = f"{brainrot_path} {example_file_path}"

    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=True)
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

if __name__ == "__main__":
    pytest.main(["-v", os.path.abspath(__file__)])
