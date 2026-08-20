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

for f in test_cases/*.brainrot; do
    echo "Running Valgrind on $f..."
    base=$(basename "$f" .brainrot)

    case "$base" in
        slorp_int)                            input="42" ;;
        slorp_short)                          input="69" ;;
        slorp_float)                          input="3.14" ;;
        slorp_double)                         input="3.141592" ;;
        slorp_char)                           input="c" ;;
        slorp_bool)                           input="1" ;;
        slorp_string)                         input="skibidi bop bop yes yes" ;;
        slorp_identity_char_array)             input="hello" ;;
        native_cstring_param_char_array)       input="hello" ;;
        native_char_array_access)              input="hello" ;;
        native_char_param_scalar)              input="c" ;;
        identity_string_use_after_free)        input="hello" ;;
        identity_ownership_nonstring_result)   input="hello" ;;
        native_call_self_init)                 input="42" ;;
        native_call_loop)                      input=$'1\n2\n3' ;;
        native_call_string_arg)                input=$'skibidi\nq' ;;
        native_call_do_while)                  input=$'5\n50\n6\n150' ;;
        *)                                     input="" ;;
    esac

    if [[ -n "$input" ]]; then
        echo "$input" | valgrind --leak-check=full --error-exitcode=100 "$TARGET" "$f"
    else
        valgrind --leak-check=full --error-exitcode=100 "$TARGET" "$f"
    fi

    valgrind_exit_code=$?  # Capture only valgrind’s exit code

    case $valgrind_exit_code in
        0|1) ;;
        100)
            echo "Valgrind detected memory issues in $f" >&2
            exit 1
            ;;
        *)
            echo "Valgrind failed while running $f (exit $valgrind_exit_code)" >&2
            exit 1
            ;;
    esac

    echo
done
