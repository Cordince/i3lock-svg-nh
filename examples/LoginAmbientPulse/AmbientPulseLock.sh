#!/bin/bash
set -euo pipefail

DIR=/home/nharrington/projects/i3lock-svg-nh
IMG=$DIR/examples/LoginAmbientPulse
FPS=${ANIMATION_FPS:-24}

exec "$DIR/i3lock" \
  -n \
  --animation-fps "$FPS" \
  -i "$IMG/AmbientPulse_Background_1920x1080.png" \
  -s "$IMG/AmbientPulse_i3lock.svg"
