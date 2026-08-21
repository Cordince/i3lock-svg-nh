#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

outdir=${PHASE31_OUTDIR:-/tmp/i3lock-cordince-preview}
install_root=${PHASE31_INSTALL_ROOT:-/tmp/i3lock-svg-install}
log=${PHASE31_LOG:-/tmp/i3lock-svg-nh-phase3-1-validation-$$.log}
mkdir -p "$outdir"

exec > >(tee "$log") 2>&1

run() {
    echo
    echo "+ $*"
    "$@"
}

run_sh() {
    echo
    echo "+ $*"
    bash -lc "$*"
}

expect_timeout() {
    seconds=$1
    shift
    echo
    echo "+ expect-timeout(${seconds}s): $*"
    set +e
    timeout "$seconds" "$@"
    rc=$?
    set -e
    if [ "$rc" -ne 124 ]; then
        echo "expected timeout rc=124, got rc=$rc: $*" >&2
        exit 1
    fi
}

expect_fail() {
    echo
    echo "+ expect-fail: $*"
    set +e
    "$@"
    rc=$?
    set -e
    if [ "$rc" -eq 0 ]; then
        echo "expected command to fail: $*" >&2
        exit 1
    fi
}

echo "phase3.1 validation log: $log"

run git status --short
run git log -3 --oneline

run make clean
run make
run make check
run git diff --check

run ./scene-preview \
    --scene examples/Cordince/Cordince.scene.svg \
    --size 1920x1080 \
    --state verify \
    --benchmark \
    --benchmark-warmup-frames 120 \
    --benchmark-frames 600 \
    --benchmark-fps 60 \
    --benchmark-json "$outdir/final-benchmark-1080.json"

run ./scene-preview \
    --scene examples/Cordince/Cordince.scene.svg \
    --size 3840x2160 \
    --state verify \
    --benchmark \
    --benchmark-warmup-frames 120 \
    --benchmark-frames 600 \
    --benchmark-fps 60 \
    --benchmark-json "$outdir/final-benchmark-4k.json"

run make xvfb-test

run_sh "DISPLAY= ./i3lock --validate-scene examples/Cordince/Cordince.scene.svg"
run ./scene-preview \
    --scene examples/Cordince/Cordince.scene.svg \
    --size 1920x1080 \
    --inspect \
    --inspect-json /tmp/cordince-final-inspect.json

run ./scene-pack \
    --force \
    --input examples/Cordince/Cordince.scene-source.svg \
    --output /tmp/Cordince.scene.svg
run cmp examples/Cordince/Cordince.scene.svg /tmp/Cordince.scene.svg

run_sh "python3 - <<'PY'
from pathlib import Path
import base64, hashlib, re
original = Path('examples/Cordince/Cordince_Organ_Engineering_Background_1920x1080_dark_letters.png').read_bytes()
svg = Path('examples/Cordince/Cordince.scene.svg').read_text()
m = re.search(\"data:image/png;base64,([^\\\"']+)\", svg)
assert m, 'embedded PNG not found'
embedded = base64.b64decode(m.group(1))
print('original-png-sha256=' + hashlib.sha256(original).hexdigest())
print('embedded-png-sha256=' + hashlib.sha256(embedded).hexdigest())
assert original == embedded
PY"

expect_timeout 3 xvfb-run --server-num=241 -a sh -c "sleep 1; ./i3lock -n -i examples/Cordince/Cordince_Organ_Engineering_Background_1920x1080_dark_letters.png -s examples/Cordince/Cordince_Organ_Engineering_i3lock.svg"
expect_timeout 3 xvfb-run --server-num=242 -a sh -c "sleep 1; ./i3lock -n --scene-svg examples/Cordince/Cordince.scene.svg"

echo
echo "+ sanitizer runtime probe"
set +e
bash tools/run-sanitizers-local.sh
san_rc=$?
set -e
if [ "$san_rc" -eq 77 ]; then
    echo "local sanitizer execution unavailable"
elif [ "$san_rc" -ne 0 ]; then
    echo "local sanitizer execution failed rc=$san_rc" >&2
    exit "$san_rc"
fi

run_sh "rm -rf '$install_root'"
run make install DESTDIR="$install_root"
run_sh "find '$install_root' -type f | sort"
run test -x "$install_root/usr/bin/i3lock"
run test -x "$install_root/usr/bin/scene-preview"
run test -x "$install_root/usr/bin/scene-pack"
run test -f "$install_root/usr/share/man/man1/i3lock.1"
run test -f "$install_root/usr/share/man/man1/scene-preview.1"
run test -f "$install_root/usr/share/man/man1/scene-pack.1"
run test -f "$install_root/usr/share/doc/i3lock-svg-nh/scene-v1.md"
run test ! -e "$install_root/usr/bin/scene-tests"
run test ! -e "$install_root/usr/bin/scene-x11-tests"
run_sh "! find '$install_root' -path '*tests/fixtures*' -o -path '*i3lock-cordince-preview*' | grep ."

run_sh "bash -n tools/*.sh"
if command -v shellcheck >/dev/null 2>&1; then
    run shellcheck tools/*.sh
else
    echo "shellcheck: not installed"
fi

run ./scene-preview --version
run ./scene-pack --version

run make clean
run make
run make check
run make xvfb-test
run git diff --check

run git diff --stat
run git status --short

echo
echo "phase3.1 validation complete"
