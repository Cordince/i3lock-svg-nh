#!/bin/sh
set -eu

run_clean() {
    make clean
}

echo "normal build/test"
run_clean
make
make test
make xvfb-test

echo "ASan build/test"
run_clean
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1:abort_on_error=1} \
    make CFLAGS="-g -O1 -fsanitize=address -fno-omit-frame-pointer" \
         LDFLAGS="-fsanitize=address"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1:abort_on_error=1} make test
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1:abort_on_error=1} make xvfb-test

echo "UBSan build/test"
run_clean
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:abort_on_error=1:print_stacktrace=1} \
    make CFLAGS="-g -O1 -fsanitize=undefined -fno-omit-frame-pointer" \
         LDFLAGS="-fsanitize=undefined"
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:abort_on_error=1:print_stacktrace=1} make test
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:abort_on_error=1:print_stacktrace=1} make xvfb-test

run_clean
echo "sanitizers-container: ok"
