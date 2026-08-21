#!/bin/sh
set -eu

expect_timeout() {
    server_num=$1
    shift
    set +e
    timeout 3 xvfb-run --server-num="$server_num" -a "$@"
    rc=$?
    set -e
    if [ "$rc" -ne 124 ]; then
        echo "preview smoke command exited unexpectedly with rc=$rc: $*" >&2
        exit 1
    fi
}

expect_timeout 210 ./scene-preview --scene examples/scene-v1.svg --size 640x360 --window --no-overlay
expect_timeout 220 ./scene-preview --source examples/Cordince/Cordince.scene-source.svg --size 640x360 --window --watch --no-overlay --reload-error-to-stderr
echo "scene-preview-window-smoke: ok"
