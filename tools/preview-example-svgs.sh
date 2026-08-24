#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd "$SCRIPT_DIR/.." && pwd)
PREVIEW=${SCENE_PREVIEW_BIN:-"$REPO_ROOT/scene-preview"}
SIZE=${SCENE_PREVIEW_SIZE:-1280x720}

if [ ! -x "$PREVIEW" ]; then
    echo "No executable scene-preview found. Build ./scene-preview or set SCENE_PREVIEW_BIN." >&2
    exit 1
fi

for svg in "$REPO_ROOT"/examples/*.svg; do
    [ -e "$svg" ] || {
        echo "No SVG examples found in $REPO_ROOT/examples" >&2
        exit 1
    }

    rel=${svg#"$REPO_ROOT"/}
    echo "Previewing $rel"
    "$PREVIEW" --scene "$svg" --size "$SIZE" --window "$@"
done
