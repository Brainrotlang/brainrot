#!/bin/bash

for f in test_cases/*.brainrot; do
    echo "Running Valgrind on $f..."
    base=$(basename "$f" .brainrot)

    case "$base" in
        slorp_int)    input="42" ;;
        slorp_short)  input="69" ;;
        slorp_float)  input="3.14" ;;
        slorp_double) input="3.141592" ;;
        slorp_char)   input="c" ;;
        slorp_bool)   input="1" ;;
        slorp_string) input="skibidi bop bop yes yes" ;;
        slorp_identity_char_array)       input="hello" ;;
        native_cstring_param_char_array) input="hello" ;;
        *)            input="" ;;
    esac

    if [[ -n "$input" ]]; then
        echo "$input" | valgrind --leak-check=full --error-exitcode=100 ./brainrot "$f"
    else
        valgrind --track-origins=yes --leak-check=full --error-exitcode=100 ./brainrot "$f"
    fi

    valgrind_exit_code=$?  # Capture only valgrind’s exit code

    if [[ $valgrind_exit_code -eq 100 ]]; then
        echo "Valgrind detected memory issues in $f"
        exit 1
    fi

    echo
done

