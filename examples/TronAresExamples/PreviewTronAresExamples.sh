#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
I3LOCK=${I3LOCK:-"$ROOT_DIR/i3lock"}
ANIMATION_FPS=${ANIMATION_FPS:-24}
BACKGROUND=${BACKGROUND:-"$SCRIPT_DIR/TronAres_Background_1920x1080.png"}

VARIANTS=(
  "Progress core|TronAres_ProgressCore_i3lock.svg"
  "Digitization complete|TronAres_DigitizationComplete_i3lock.svg"
  "Transfer initiated|TronAres_TransferInitiated_i3lock.svg"
  "Laser calibration|TronAres_LaserCalibration_i3lock.svg"
  "Command stack|TronAres_CommandStack_i3lock.svg"
  "Target array|TronAres_TargetArray_i3lock.svg"
  "Vehicle wireframe|TronAres_VehicleWireframe_i3lock.svg"
  "Red horizon map|TronAres_RedHorizonMap_i3lock.svg"
  "Touch ripple|TronAres_TouchRipple_i3lock.svg"
  "Identity disc|TronAres_IdentityDisc_i3lock.svg"
  "Warning bands|TronAres_WarningBands_i3lock.svg"
  "Lab deck|TronAres_LabDeck_i3lock.svg"
  "Data spine|TronAres_DataSpine_i3lock.svg"
  "System bloom|TronAres_SystemBloom_i3lock.svg"
  "Calibration matrix|TronAres_CalibrationMatrix_i3lock.svg"
)

usage() {
  printf 'Usage: %s [--list] [variant-name-or-svg]\n' "$(basename "$0")"
  printf '\nRuns TRON Ares-style LOGIN i3lock visualizations in no-fork mode.\n'
  printf 'Unlock with your password to return to this script.\n'
  printf 'Set I3LOCK=/path/to/i3lock to override the binary.\n'
  printf 'Set ANIMATION_FPS=24 to control live idle animation speed.\n'
  printf 'Set BACKGROUND=/path/to.png to override the shared background.\n'
}

list_variants() {
  for entry in "${VARIANTS[@]}"; do
    name=${entry%%|*}
    file=${entry#*|}
    printf '%-24s %s\n' "$name" "$file"
  done
}

resolve_variant() {
  local wanted=$1
  local entry name file slug

  for entry in "${VARIANTS[@]}"; do
    name=${entry%%|*}
    file=${entry#*|}
    slug=${file#TronAres_}
    slug=${slug%_i3lock.svg}
    if [[ "$wanted" == "$name" || "$wanted" == "$file" || "$wanted" == "$slug" ]]; then
      printf '%s\n' "$entry"
      return 0
    fi
  done

  return 1
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

if [[ "${1:-}" == "--list" ]]; then
  list_variants
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

if [[ -n "${1:-}" ]]; then
  entry=$(resolve_variant "$1") || {
    printf 'Unknown variant: %s\n\n' "$1" >&2
    list_variants >&2
    exit 1
  }
  name=${entry%%|*}
  file=${entry#*|}
  svg="$SCRIPT_DIR/$file"
  printf '%s (%s)\n' "$name" "$file"
  exec "$I3LOCK" -n --animation-fps "$ANIMATION_FPS" -i "$BACKGROUND" -s "$svg"
fi

printf 'TRON Ares examples preview runner\n'
printf 'Each step runs i3lock -n. Type your password to return, then continue to the next visualization.\n\n'

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

  "$I3LOCK" -n --animation-fps "$ANIMATION_FPS" -i "$BACKGROUND" -s "$svg"
  printf '\n'
done

printf 'Done.\n'
