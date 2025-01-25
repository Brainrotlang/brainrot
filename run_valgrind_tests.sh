#!/bin/bash

for f in test_cases/*.brainrot; do
    echo "Running Valgrind on $f..."
    valgrind --leak-check=full --error-exitcode=1 ./brainrot "$f"
    echo
done