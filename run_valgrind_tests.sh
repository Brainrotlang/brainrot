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

    # Valgrind lists each descriptor still open at exit. Only the ones this
    # program opened itself count: a CI runner hands its child unrelated
    # inherited descriptors (GitHub Actions passes several), and valgrind
    # labels those "<inherited from parent>" on the following line. The
    # summary count cannot be used for this -- it lumps inherited ones in
    # with real leaks, which is exactly how the first version of this check
    # failed CI on a fixture that opens no files at all.
    #
    # A genuinely leaked file looks like:
    #     Open file descriptor 4: /tmp/whatever.txt
    #        at 0x...: open (open64.c:41)
    # so the discriminator is the absence of the inherited marker.
    stray_fds=$(awk '
        /Open file descriptor [0-9]+:/ { pending = 1; next }
        pending { if ($0 !~ /inherited from parent/) count++; pending = 0 }
        END { print count + 0 }
    ' "$fd_log")
    if (( stray_fds > 0 )); then
        echo "Valgrind found $stray_fds file descriptor(s) left open in $f"
        grep -E 'FILE DESCRIPTORS|Open (file descriptor|AF_)' "$fd_log"
        rm -f "$fd_log"
        exit 1
    fi
    rm -f "$fd_log"

    echo
done

