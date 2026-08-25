#!/bin/sh
set -eu

SCENE=${1:-examples/nature.scene.svg}
DURATION=${I3LOCK_CPU_MEASURE_SECONDS:-20}
IDLE_WARMUP=${I3LOCK_CPU_IDLE_WARMUP_SECONDS:-4}
IDLE_SAMPLE=${I3LOCK_CPU_IDLE_SAMPLE_SECONDS:-10}
SCREEN=${I3LOCK_CPU_MEASURE_SCREEN:-1920x1080x24}
SERVER_NUM=${I3LOCK_CPU_MEASURE_SERVER_NUM:-260}

read_proc_ticks() {
    pid=$1
    total=0
    for proc_pid in "$pid" $(pgrep -P "$pid" 2>/dev/null || true); do
        [ -r "/proc/$proc_pid/stat" ] || continue
        ticks=$(awk '{ print $14 + $15 }' "/proc/$proc_pid/stat" 2>/dev/null || printf '0')
        total=$((total + ticks))
    done
    printf '%s\n' "$total"
}

if [ "${1:-}" = "__idle-window" ]; then
    label=$2
    scene=$3
    warmup=$4
    sample=$5
    shift 5

    ./i3lock -n "$@" --scene-svg "$scene" &
    lock_pid=$!
    trap 'kill "$lock_pid" 2>/dev/null || true' EXIT INT TERM

    sleep "$warmup"
    start_ticks=$(read_proc_ticks "$lock_pid")
    sleep "$sample"
    end_ticks=$(read_proc_ticks "$lock_pid")

    kill "$lock_pid" 2>/dev/null || true
    wait "$lock_pid" 2>/dev/null || true
    trap - EXIT INT TERM

    delta_ticks=$((end_ticks - start_ticks))
    [ "$delta_ticks" -ge 0 ] || delta_ticks=0
    hz=$(getconf CLK_TCK)
    awk -v label="$label" \
        -v sample="$sample" \
        -v delta="$delta_ticks" \
        -v hz="$hz" \
        'BEGIN { printf "%s idle-window seconds=%s cpu=%.2f%% ticks=%d\n", label, sample, (delta / hz) / sample * 100.0, delta }'
    exit 0
fi

run_mode() {
    label=$1
    shift

    set +e
    xvfb-run --server-num="$SERVER_NUM" --server-args="-screen 0 $SCREEN" -a \
        /usr/bin/time -f "$label elapsed=%e user=%U sys=%S cpu=%P maxrss=%M" \
        timeout "$DURATION" ./i3lock -n "$@" --scene-svg "$SCENE"
    rc=$?
    set -e

    if [ "$rc" -ne 124 ]; then
        echo "$label exited unexpectedly with rc=$rc" >&2
        exit "$rc"
    fi
}

run_idle_window() {
    label=$1
    shift

    xvfb-run --server-num="$SERVER_NUM" --server-args="-screen 0 $SCREEN" -a \
        sh "$0" __idle-window "$label" "$SCENE" "$IDLE_WARMUP" "$IDLE_SAMPLE" "$@"
}

run_mode baseline
run_mode low-cpu --low-cpu
run_mode extreme-low-cpu --extreme-low-cpu

run_idle_window baseline
run_idle_window low-cpu --low-cpu
run_idle_window extreme-low-cpu --extreme-low-cpu
