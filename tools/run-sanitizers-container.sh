#!/bin/sh
set -eu

runtime=
if command -v podman >/dev/null 2>&1; then
    runtime=podman
elif command -v docker >/dev/null 2>&1; then
    runtime=docker
else
    echo "no podman or docker found; sanitizer container cannot run" >&2
    exit 127
fi

tag=i3lock-svg-nh-sanitizers:local
log=/tmp/i3lock-svg-nh-sanitizers-$$.log
tmp=/tmp/i3lock-svg-nh-sanitizers-$$.step.log
echo "sanitizer log: $log"

if ! "$runtime" info >"$tmp" 2>&1; then
    {
        echo "$runtime was found, but it is not usable in this environment."
        echo "Sanitizer container execution is unavailable; project sanitizers did not run."
        cat "$tmp"
    } | tee "$log"
    rm -f "$tmp"
    exit 77
fi

if ! "$runtime" build -f tools/Containerfile.sanitizers -t "$tag" . >"$tmp" 2>&1; then
    cat "$tmp" | tee "$log"
    rm -f "$tmp"
    exit 1
fi
cat "$tmp" | tee "$log"
if ! "$runtime" run --rm "$tag" >"$tmp" 2>&1; then
    cat "$tmp" | tee -a "$log"
    rm -f "$tmp"
    exit 1
fi
cat "$tmp" | tee -a "$log"
rm -f "$tmp"
