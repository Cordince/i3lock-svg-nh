#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

OUTDIR=${PHASE3_OUTDIR:-/tmp/i3lock-cordince-preview}
LOG=${PHASE3_LOG:-/tmp/i3lock-svg-nh-phase3-validation-$$.log}
mkdir -p "$OUTDIR"

exec > >(tee "$LOG") 2>&1

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

expect_fail() {
    echo
    echo "+ expect-fail: $*"
    if bash -lc "$*"; then
        echo "expected command to fail but it succeeded: $*" >&2
        exit 1
    fi
}

expect_timeout() {
    local seconds=$1
    shift
    echo
    echo "+ expect-timeout(${seconds}s): $*"
    set +e
    timeout "$seconds" "$@"
    local rc=$?
    set -e
    if [[ $rc -ne 124 ]]; then
        echo "expected timeout rc=124, got rc=$rc: $*" >&2
        exit 1
    fi
}

run_sanitizers_or_document_unavailable() {
    echo
    echo "+ sh tools/run-sanitizers-container.sh"
    set +e
    sh tools/run-sanitizers-container.sh
    local rc=$?
    set -e
    if [[ $rc -eq 0 ]]; then
        return
    fi
    if [[ $rc -ne 77 ]]; then
        echo "sanitizer container failed after becoming runnable; rc=$rc" >&2
        exit "$rc"
    fi

    echo "sanitizer container unavailable; checking local fallback tools"
    local ran_alt=0
    if command -v valgrind >/dev/null 2>&1; then
        ran_alt=1
        run valgrind --leak-check=full --error-exitcode=125 ./scene-tests
    else
        echo "valgrind: not found"
    fi
    if command -v scan-build >/dev/null 2>&1; then
        ran_alt=1
        run_sh "make clean && scan-build --status-bugs make && make test && make xvfb-test"
    else
        echo "scan-build: not found"
    fi
    if [[ $ran_alt -eq 0 ]]; then
        echo "no local sanitizer/static-analysis fallback is installed"
    fi
}

echo "phase3 validation log: $LOG"
run git status --short
run git log -3 --oneline

run make clean
run make
run make test
run make check
run make xvfb-test
run git diff --check

run ./scene-pack --check --input examples/Cordince/Cordince.scene-source.svg --print-assets
run ./scene-pack --force --input examples/Cordince/Cordince.scene-source.svg --output "$OUTDIR/Cordince.repacked.scene.svg" --print-assets
run cmp examples/Cordince/Cordince.scene.svg "$OUTDIR/Cordince.repacked.scene.svg"

run_sh "DISPLAY= ./i3lock --validate-scene examples/scene-v1.svg"
run_sh "DISPLAY= ./i3lock --validate-scene examples/Cordince/Cordince.scene.svg"
expect_fail "DISPLAY= ./i3lock --validate-scene examples/Cordince/Cordince.scene-source.svg"

run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --inspect
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --inspect-json "$OUTDIR/inspect.json"
run ./scene-preview --source examples/Cordince/Cordince.scene-source.svg --size 1920x1080 --inspect-json "$OUTDIR/source-inspect.json"

run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state locked --time-ms 0 --output "$OUTDIR/locked.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state input --time-ms 250 --output "$OUTDIR/input.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state input --trigger key --trigger-at-ms 100 --time-ms 100 --output "$OUTDIR/key-start.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state input --trigger key --trigger-at-ms 100 --time-ms 180 --output "$OUTDIR/key-mid.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state input --trigger key --trigger-at-ms 100 --time-ms 380 --output "$OUTDIR/key-end.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state input --trigger backspace --trigger-at-ms 100 --time-ms 100 --output "$OUTDIR/backspace-start.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state input --trigger backspace --trigger-at-ms 100 --time-ms 180 --output "$OUTDIR/backspace-mid.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state input --trigger backspace --trigger-at-ms 100 --time-ms 360 --output "$OUTDIR/backspace-end.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state verify --time-ms 0 --output "$OUTDIR/verify-0.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state verify --time-ms 300 --output "$OUTDIR/verify-300.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state verify --time-ms 600 --output "$OUTDIR/verify-600.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --state verify --time-ms 900 --output "$OUTDIR/verify-900.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --initial-state input --state fail --state-at-ms 100 --time-ms 100 --output "$OUTDIR/fail-start.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --initial-state input --state fail --state-at-ms 100 --time-ms 190 --output "$OUTDIR/fail-mid.png"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 --initial-state input --state fail --state-at-ms 100 --time-ms 520 --output "$OUTDIR/fail-end.png"

rm -rf "$OUTDIR/frames"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 \
    --initial-state locked \
    --state-change 500:input \
    --trigger-event 800:key \
    --trigger-event 1200:backspace \
    --state-change 1600:verify \
    --state-change 3000:fail \
    --duration-ms 4000 \
    --fps 60 \
    --output-dir "$OUTDIR/frames" \
    --contact-sheet "$OUTDIR/contact-sheet.png" \
    --contact-columns 6 \
    --contact-every 30

run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 1920x1080 \
    --benchmark --benchmark-frames 120 --benchmark-fps 60 --benchmark-warmup-frames 10 \
    --benchmark-json "$OUTDIR/benchmark-1920x1080.json"
run ./scene-preview --scene examples/Cordince/Cordince.scene.svg --size 3840x2160 \
    --benchmark --benchmark-frames 120 --benchmark-fps 60 --benchmark-warmup-frames 10 \
    --benchmark-json "$OUTDIR/benchmark-3840x2160.json"

run_sh "python3 - <<'PY'
from pathlib import Path
import base64, hashlib, re
png = Path('examples/Cordince/Cordince_Organ_Engineering_Background_1920x1080_dark_letters.png').read_bytes()
svg = Path('examples/Cordince/Cordince.scene.svg').read_text()
m = re.search(r'data:image/png;base64,([^\"\\']+)', svg)
assert m, 'embedded PNG not found'
embedded = base64.b64decode(m.group(1))
print('original-png-sha256=' + hashlib.sha256(png).hexdigest())
print('embedded-png-sha256=' + hashlib.sha256(embedded).hexdigest())
assert png == embedded
PY"

expect_timeout 3 xvfb-run --server-num=240 -a sh -c "sleep 1; ./i3lock -n -i examples/Cordince/Cordince_Organ_Engineering_Background_1920x1080_dark_letters.png -s examples/Cordince/Cordince_Organ_Engineering_i3lock.svg"

run_sanitizers_or_document_unavailable

run git diff --stat
run git status --short
run_sh "git ls-files --others --exclude-standard | while IFS= read -r f; do if [ -f \"\$f\" ]; then case \"\$f\" in *.png) bytes=\$(stat -c %s \"\$f\"); printf '%s bytes=%s lines=n/a\\n' \"\$f\" \"\$bytes\" ;; *) bytes=\$(stat -c %s \"\$f\"); lines=\$(wc -l < \"\$f\"); printf '%s bytes=%s lines=%s\\n' \"\$f\" \"\$bytes\" \"\$lines\" ;; esac; fi; done"

echo
echo "phase3 validation complete"
