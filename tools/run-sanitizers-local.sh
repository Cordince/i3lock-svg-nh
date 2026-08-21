#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
log=${SANITIZER_LOCAL_LOG:-/tmp/i3lock-svg-nh-local-sanitizers-$$.log}
tmp=${SANITIZER_LOCAL_TMP:-$(mktemp -d "${TMPDIR:-/tmp}/i3lock-sanitizers-local.XXXXXX")}
src="$tmp/src"

exec > >(tee "$log") 2>&1

echo "local sanitizer log: $log"
echo "libasan.so=$(gcc -print-file-name=libasan.so)"
echo "libasan.a=$(gcc -print-file-name=libasan.a)"
echo "libubsan.so=$(gcc -print-file-name=libubsan.so)"
echo "libubsan.a=$(gcc -print-file-name=libubsan.a)"

asan_a=$(gcc -print-file-name=libasan.a)
ubsan_a=$(gcc -print-file-name=libubsan.a)
asan_available=0
ubsan_available=0
test -r "$asan_a" && asan_available=1 || true
test -r "$ubsan_a" && ubsan_available=1 || true

if [ "$asan_available" -eq 0 ] && [ "$ubsan_available" -eq 0 ]; then
    echo "no readable static sanitizer runtime archives; local sanitizer execution unavailable"
    exit 77
fi

mkdir -p "$src"
tar \
    --exclude=.git \
    --exclude='*.o' \
    --exclude=i3lock \
    --exclude=scene-preview \
    --exclude=scene-pack \
    --exclude=scene-tests \
    --exclude=scene-x11-tests \
    -C "$root" -cf - . | tar -C "$src" -xf -

run_build() {
    name=$1
    cflags=$2
    ldflags=$3
    shift 3
    echo
    echo "$name build/test"
    (
        cd "$src"
        make clean
        make CFLAGS="$cflags" LDFLAGS="$ldflags"
        "$@" make test
        "$@" make xvfb-test
        make clean
    )
}

if [ "$asan_available" -eq 1 ]; then
    run_build \
        "ASan static runtime" \
        "-g -O1 -fsanitize=address -fno-omit-frame-pointer -static-libasan" \
        "-fsanitize=address -static-libasan" \
        env ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1:abort_on_error=1}"
else
    echo "ASan static runtime archive is not readable; skipping ASan"
fi

if [ "$ubsan_available" -eq 1 ]; then
    run_build \
        "UBSan static runtime" \
        "-g -O1 -fsanitize=undefined -fno-omit-frame-pointer -static-libubsan" \
        "-fsanitize=undefined -static-libubsan" \
        env UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:abort_on_error=1:print_stacktrace=1}"
else
    echo "UBSan static runtime archive is not readable; skipping UBSan"
fi

echo "local-sanitizers: ok"
