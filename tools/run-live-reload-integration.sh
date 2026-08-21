#!/usr/bin/env bash
set -euo pipefail

if ! command -v xdotool >/dev/null 2>&1; then
    echo "xdotool is required for live reload integration test" >&2
    exit 77
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/i3lock-live-reload.XXXXXX")
pid=
cleanup() {
    if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT

write_png() {
    python3 - "$1" "$2" "$3" "$4" <<'PY'
import struct
import sys
import zlib

path = sys.argv[1]
r = int(sys.argv[2])
g = int(sys.argv[3])
b = int(sys.argv[4])
w = h = 8
raw = b''.join(b'\x00' + bytes([r, g, b, 255]) * w for _ in range(h))

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

png = (
    b'\x89PNG\r\n\x1a\n' +
    chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
    chunk(b'IDAT', zlib.compress(raw, 9)) +
    chunk(b'IEND', b'')
)
open(path, 'wb').write(png)
PY
}

write_source() {
    local path=$1
    local overlay=$2
    local verify=$3
    local opacity=1
    if [ "$overlay" = none ]; then
        overlay="#000000"
        opacity=0
    fi
    cat >"$path" <<EOF
<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8" viewBox="0 0 8 8">
  <metadata><scene xmlns="urn:i3lock-svg-nh:scene:1" version="1" fit="pixel" background="#000000">
    <layer ref="base" states="*" static="true"/>
    <layer ref="overlay" states="*"/>
    <layer ref="verify" states="verify"/>
    <layer ref="key-flash" trigger="key">
      <timeline duration-ms="2000" restart="true">
        <keyframe at="0" opacity="1"/>
        <keyframe at="1" opacity="1"/>
      </timeline>
    </layer>
  </scene></metadata>
  <g id="base"><image href="bg.png" x="0" y="0" width="8" height="8"/></g>
  <g id="overlay"><rect x="0" y="0" width="4" height="8" fill="$overlay" fill-opacity="$opacity"/></g>
  <g id="verify"><rect x="7" y="7" width="1" height="1" fill="$verify"/></g>
  <g id="key-flash"><rect x="1" y="1" width="1" height="1" fill="#ffff00"/></g>
</svg>
EOF
}

assert_pixel() {
    python3 - "$1" "$2" "$3" "$4" "$5" "$6" <<'PY'
import struct
import sys
import zlib

path, x, y, er, eg, eb = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
data = open(path, 'rb').read()
assert data[:8] == b'\x89PNG\r\n\x1a\n'
pos = 8
w = h = color_type = None
idat = b''
while pos < len(data):
    n = struct.unpack('>I', data[pos:pos + 4])[0]
    kind = data[pos + 4:pos + 8]
    payload = data[pos + 8:pos + 8 + n]
    pos += 12 + n
    if kind == b'IHDR':
        w, h, bit_depth, color_type, _, _, _ = struct.unpack('>IIBBBBB', payload)
        assert bit_depth == 8
    elif kind == b'IDAT':
        idat += payload
    elif kind == b'IEND':
        break
raw = zlib.decompress(idat)
bpp = 4 if color_type == 6 else 3
stride = w * bpp
rows = []
prev = [0] * stride
idx = 0
for _ in range(h):
    filt = raw[idx]
    idx += 1
    row = list(raw[idx:idx + stride])
    idx += stride
    recon = []
    for i, value in enumerate(row):
        left = recon[i - bpp] if i >= bpp else 0
        up = prev[i]
        upper_left = prev[i - bpp] if i >= bpp else 0
        if filt == 0:
            out = value
        elif filt == 1:
            out = (value + left) & 255
        elif filt == 2:
            out = (value + up) & 255
        elif filt == 3:
            out = (value + ((left + up) // 2)) & 255
        elif filt == 4:
            p = left + up - upper_left
            pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
            pred = left if pa <= pb and pa <= pc else up if pb <= pc else upper_left
            out = (value + pred) & 255
        else:
            raise AssertionError(f'unsupported PNG filter {filt}')
        recon.append(out)
    rows.append(recon)
    prev = recon
off = x * bpp
r, g, b = rows[y][off:off + 3]
if (r, g, b) != (er, eg, eb):
    raise SystemExit(f'{path} pixel {x},{y} expected {(er, eg, eb)} got {(r, g, b)}')
PY
}

wait_file() {
    local path=$1
    for _ in $(seq 1 100); do
        [ -s "$path" ] && return 0
        sleep 0.05
    done
    echo "timed out waiting for $path" >&2
    exit 1
}

wait_window() {
    for _ in $(seq 1 100); do
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "scene-preview exited before creating a window" >&2
            cat "$tmp/stderr" >&2 || true
            exit 1
        fi
        local found
        found=$(xdotool search --name "i3lock SVG scene preview" 2>/dev/null | tail -n1 || true)
        if [ -n "$found" ]; then
            printf '%s\n' "$found"
            return 0
        fi
        sleep 0.05
    done
    echo "timed out waiting for scene-preview window" >&2
    cat "$tmp/stderr" >&2 || true
    exit 1
}

preview_running() {
    if ! kill -0 "$pid" 2>/dev/null; then
        return 1
    fi
    if [ -r "/proc/$pid/stat" ]; then
        local state
        state=$(awk '{print $3}' "/proc/$pid/stat" 2>/dev/null || printf '?')
        if [ "$state" = Z ]; then
            return 1
        fi
    fi
    return 0
}

capture() {
    local num=$1
    local path
    path=$(printf "%s/shots/scene-preview-screenshot-%06d.png" "$tmp" "$num")
    xdotool key --window "$win" s 2>/dev/null
    wait_file "$path"
    echo "$path"
}

atomic_source() {
    local overlay=$1
    local verify=$2
    write_source "$tmp/source.svg.tmp" "$overlay" "$verify"
    mv "$tmp/source.svg.tmp" "$tmp/source.svg"
}

atomic_png() {
    write_png "$tmp/bg.png.tmp" "$1" "$2" "$3"
    mv "$tmp/bg.png.tmp" "$tmp/bg.png"
}

if ldd ./scene-preview 2>/dev/null | grep -q libpam; then
    echo "scene-preview links PAM" >&2
    exit 1
fi
if nm -D ./scene-preview 2>/dev/null | grep -E 'xcb_grab_(keyboard|pointer)|pam_' >/dev/null; then
    echo "scene-preview imports grab or PAM symbols" >&2
    exit 1
fi
if grep -E 'XCB_CW_OVERRIDE_REDIRECT|xcb_grab_keyboard|xcb_grab_pointer|pam_' scene_preview.c >/dev/null; then
    echo "scene-preview source contains lock-only grab/PAM behavior" >&2
    exit 1
fi

mkdir "$tmp/shots"
write_png "$tmp/bg.png" 255 0 0
write_source "$tmp/source.svg" none "#ffffff"

sleep 1
SCENE_PREVIEW_TEST_STATS="$tmp/stats.json" \
    ./scene-preview --source "$tmp/source.svg" --size 8x8 --window --watch --no-overlay \
    --reload-error-to-stderr --screenshot-dir "$tmp/shots" 2>"$tmp/stderr" &
pid=$!
win=$(wait_window)

shot=$(capture 1)
assert_pixel "$shot" 0 0 255 0 0
assert_pixel "$shot" 6 6 255 0 0

atomic_source "#0000ff" "#ffffff"
sleep 0.35
shot=$(capture 2)
assert_pixel "$shot" 0 0 0 0 255
assert_pixel "$shot" 6 6 255 0 0

atomic_png 0 255 0
sleep 0.35
shot=$(capture 3)
assert_pixel "$shot" 0 0 0 0 255
assert_pixel "$shot" 6 6 0 255 0

printf '<svg xmlns="http://www.w3.org/2000/svg"><metadata>' >"$tmp/source.svg.tmp"
mv "$tmp/source.svg.tmp" "$tmp/source.svg"
sleep 0.35
shot=$(capture 4)
assert_pixel "$shot" 0 0 0 0 255
assert_pixel "$shot" 6 6 0 255 0
grep -q 'reload failed:' "$tmp/stderr"

xdotool key --window "$win" 3 2>/dev/null
atomic_source "#0000ff" "#00ffff"
sleep 0.35
shot=$(capture 5)
assert_pixel "$shot" 7 7 0 255 255

xdotool key --window "$win" a 2>/dev/null
sleep 0.05
shot=$(capture 6)
assert_pixel "$shot" 1 1 255 255 0

atomic_source "#0000ff" "#00ffff"
sleep 0.35
shot=$(capture 7)
assert_pixel "$shot" 1 1 0 0 255

for i in $(seq 1 100); do
    if [ $((i % 2)) -eq 0 ]; then
        atomic_source "#0000ff" "#00ffff"
    else
        atomic_source "#0000cc" "#00ffff"
    fi
    sleep 0.16
done

xdotool windowfocus "$win" 2>/dev/null || true
xdotool key --clearmodifiers --window "$win" Escape 2>/dev/null || true
xdotool key --clearmodifiers Escape 2>/dev/null || true
for _ in $(seq 1 40); do
    if ! preview_running; then
        wait "$pid" || exit 1
        pid=
        break
    fi
    sleep 0.05
done
if [ -n "${pid:-}" ]; then
    xdotool windowclose "$win" 2>/dev/null || true
    xdotool windowkill "$win" 2>/dev/null || true
    for _ in $(seq 1 100); do
        if ! preview_running; then
            wait "$pid" || exit 1
            pid=
            break
        fi
        sleep 0.05
    done
fi
if [ -n "${pid:-}" ]; then
    echo "scene-preview did not close" >&2
    ps -p "$pid" -o pid,stat,cmd >&2 || true
    cat "$tmp/stderr" >&2 || true
    exit 1
fi
wait_file "$tmp/stats.json"

python3 - "$tmp/stats.json" <<'PY'
import json
import sys

stats = json.load(open(sys.argv[1]))
assert stats["reload_success"] >= 104, stats
assert stats["reload_failure"] >= 1, stats
assert stats["renderer_create"] == stats["renderer_destroy"], stats
assert stats["xml_parse_total"] == stats["renderer_create"], stats
assert stats["rsvg_handle_total"] == stats["renderer_create"], stats
assert stats["x_pixmap_alloc_total"] == 0, stats
assert stats["max_dependencies"] <= 4, stats
assert stats["max_watches"] <= 4, stats
assert stats["current_watches"] == 0, stats
assert stats["current_open_fds"] <= 5, stats
assert not stats["need_reload"], stats
print("live-reload-stats:", stats)
PY

echo "scene-preview-live-reload: ok"
