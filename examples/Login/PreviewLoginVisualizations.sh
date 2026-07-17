#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
I3LOCK=${I3LOCK:-"$ROOT_DIR/i3lock"}
BACKGROUND="$SCRIPT_DIR/Login_Background_1920x1080_dark_letters.png"

VARIANTS=(
  "Generic|Login_i3lock.svg"
  "Bounce|Login_Bounce_i3lock.svg"
  "Pulse|Login_Pulse_i3lock.svg"
  "Bubble|Login_Bubble_i3lock.svg"
  "Underline sweep|Login_UnderlineSweep_i3lock.svg"
  "Orbit|Login_Orbit_i3lock.svg"
  "Radar|Login_Radar_i3lock.svg"
  "Equalizer|Login_Equalizer_i3lock.svg"
  "Glitch|Login_Glitch_i3lock.svg"
  "Scanlines|Login_Scanlines_i3lock.svg"
  "Constellation|Login_Constellation_i3lock.svg"
  "Circuit|Login_Circuit_i3lock.svg"
  "Spiral|Login_Spiral_i3lock.svg"
  "Shatter|Login_Shatter_i3lock.svg"
  "Neon box|Login_NeonBox_i3lock.svg"
  "Waveform|Login_Waveform_i3lock.svg"
  "Comet|Login_Comet_i3lock.svg"
  "Cascade|Login_Cascade_i3lock.svg"
  "Bloom|Login_Bloom_i3lock.svg"
  "Morse|Login_Morse_i3lock.svg"
  "Hex|Login_Hex_i3lock.svg"
)

usage() {
  printf 'Usage: %s [--list]\n' "$(basename "$0")"
  printf '\nRuns each Login i3lock visualization. Each preview locks the screen; unlock with your password to return to this script.\n'
  printf 'Set I3LOCK=/path/to/i3lock to override the binary.\n'
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

if [[ "${1:-}" == "--list" ]]; then
  for entry in "${VARIANTS[@]}"; do
    name=${entry%%|*}
    file=${entry#*|}
    printf '%-18s %s\n' "$name" "$file"
  done
  exit 0
fi

if [[ ! -x "$I3LOCK" ]]; then
  printf 'i3lock binary not found or not executable: %s\n' "$I3LOCK" >&2
  printf 'Build it first with: make\n' >&2
  exit 1
fi

if [[ ! -f "$BACKGROUND" ]]; then
  printf 'Background not found: %s\n' "$BACKGROUND" >&2
  exit 1
fi

printf 'Login visualization preview runner\n'
printf 'Each step locks the screen. Type your password to return, then continue to the next visualization.\n\n'

for index in "${!VARIANTS[@]}"; do
  entry=${VARIANTS[$index]}
  name=${entry%%|*}
  file=${entry#*|}
  svg="$SCRIPT_DIR/$file"

  if [[ ! -f "$svg" ]]; then
    printf 'Skipping missing SVG: %s\n' "$svg" >&2
    continue
  fi

  printf '[%02d/%02d] %s (%s)\n' "$((index + 1))" "${#VARIANTS[@]}" "$name" "$file"
  read -r -p 'Press Enter to lock and preview, or q then Enter to quit: ' answer
  if [[ "$answer" == "q" || "$answer" == "Q" ]]; then
    exit 0
  fi

  "$I3LOCK" -i "$BACKGROUND" -s "$svg"
  printf '\n'
done

printf 'Done.\n'
