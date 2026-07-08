#!/bin/sh
# Generated generic i3lock-svg-nh theme gallery asset. No external assets.
set -eu

THEME_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd "$THEME_DIR/../../../.." && pwd)
ALT_ROOT=$(CDPATH= cd "$THEME_DIR/../../.." && pwd)
if [ ! -x "$REPO_ROOT/i3lock" ] && [ -x "$ALT_ROOT/i3lock" ]; then
    REPO_ROOT="$ALT_ROOT"
fi

I3LOCK=${I3LOCK_BIN:-"$REPO_ROOT/i3lock"}
if [ ! -x "$I3LOCK" ]; then
    I3LOCK=$(command -v i3lock 2>/dev/null || true)
fi
if [ -z "$I3LOCK" ] || [ ! -x "$I3LOCK" ]; then
    echo "No executable i3lock found. Build ./i3lock or set I3LOCK_BIN." >&2
    exit 1
fi

SVG="$THEME_DIR/moon-garden.svg"
RES=""
if command -v xrandr >/dev/null 2>&1; then
    RES=$(xrandr 2>/dev/null | awk '/\*/ { print $1; exit }' || true)
fi

BG=""
if [ -n "$RES" ] && [ -f "$THEME_DIR/background-$RES.png" ]; then
    BG="$THEME_DIR/background-$RES.png"
    exec "$I3LOCK" -e -i "$BG" -s "$SVG" "$@"
fi

if [ -f "$THEME_DIR/background-1920x1080.png" ] && [ "$RES" = "1920x1080" ]; then
    BG="$THEME_DIR/background-1920x1080.png"
    exec "$I3LOCK" -e -i "$BG" -s "$SVG" "$@"
fi

BG="$THEME_DIR/tile-512.png"
exec "$I3LOCK" -e -t -i "$BG" -s "$SVG" "$@"
