#!/bin/bash

for f in test_cases/*.brainrot; do
    echo "Running Valgrind on $f..."
    if [[ $(basename "$f") == slorp_int* ]]; then
        echo "42" | valgrind --leak-check=full --error-exitcode=1 ./brainrot "$f"
    else
        valgrind --leak-check=full --error-exitcode=1 ./brainrot "$f"
    fi
    echo
done