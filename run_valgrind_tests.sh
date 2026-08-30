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
        slorp_identity_char_array)             input="hello" ;;
        native_cstring_param_char_array)       input="hello" ;;
        native_char_array_access)              input="hello" ;;
        native_char_param_scalar)              input="c" ;;
        identity_string_use_after_free)        input="hello" ;;
        identity_ownership_nonstring_result)   input="hello" ;;
        string_stdlib_char_buffer)             input="hi" ;;
        *)            input="" ;;
    esac

    # --track-fds=yes reports file descriptors still open at exit. This is
    # NOT redundant with --leak-check: a FILE * that was never fclose()d
    # does not show up as "definitely lost", because glibc keeps every open
    # stream on its own internal list, so the allocation stays reachable
    # right up to process exit. Leak checking alone therefore reports a
    # clean bill of health for a program that leaked every file it opened
    # -- verified by mutation while adding file I/O (#213), where removing
    # the release path left "definitely lost: 0 bytes" and only the fd list
    # showed the two files still open.
    #
    # The output is captured rather than streamed because valgrind does not
    # count open descriptors as errors, so --error-exitcode never fires for
    # them; the check below is what turns the report into a gate.
    # stderr goes to a file and is echoed afterwards, rather than through a
    # `tee` process substitution: that runs asynchronously, so the log could
    # still be being written when the check below reads it.
    fd_log=$(mktemp)
    if [[ -n "$input" ]]; then
        echo "$input" | valgrind --leak-check=full --track-fds=yes --error-exitcode=100 ./brainrot "$f" 2>"$fd_log"
    else
        valgrind --track-origins=yes --leak-check=full --track-fds=yes --error-exitcode=100 ./brainrot "$f" 2>"$fd_log"
    fi

    valgrind_exit_code=$?  # Capture only valgrind’s exit code
    cat "$fd_log" >&2

    if [[ $valgrind_exit_code -eq 100 ]]; then
        echo "Valgrind detected memory issues in $f"
        rm -f "$fd_log"
        exit 1
    fi

    # Valgrind summarises as: "FILE DESCRIPTORS: N open (M std) at exit."
    # The M std ones are stdin/stdout/stderr and are always open; anything
    # beyond them is a resource the program opened and never released. A
    # leaked file reads as "5 open (3 std)" against a clean "3 open (3 std)"
    # -- verified both ways by mutation.
    fd_line=$(grep -oE 'FILE DESCRIPTORS: [0-9]+ open \([0-9]+ std\)' "$fd_log" | tail -1)
    if [[ -n "$fd_line" ]]; then
        fd_open=$(echo "$fd_line" | grep -oE '[0-9]+ open' | grep -oE '[0-9]+')
        fd_std=$(echo "$fd_line" | grep -oE '\([0-9]+ std' | grep -oE '[0-9]+')
        if (( fd_open > fd_std )); then
            echo "Valgrind found $((fd_open - fd_std)) file descriptor(s) left open in $f"
            grep -E 'FILE DESCRIPTORS|Open (file descriptor|AF_)' "$fd_log"
            rm -f "$fd_log"
            exit 1
        fi
    fi
    rm -f "$fd_log"

    echo
done

