#!/usr/bin/env bash
# Serial monitor — auto-detect Watchy USB port. Run in Terminal.app.
set -euo pipefail
cd "$(dirname "$0")/.."
PIO="${HOME}/.platformio/penv/bin/pio"
BAUD="${BAUD:-115200}"

if [[ ! -x "${PIO}" ]]; then
  echo "PlatformIO missing. Run: ./scripts/setup-mac.sh" >&2
  exit 1
fi

PORT="${UPLOAD_PORT:-}"
if [[ -z "${PORT}" ]]; then
  shopt -s nullglob
  CANDIDATES=(/dev/cu.usbserial* /dev/cu.SLAB* /dev/cu.wchusbserial* /dev/cu.usbmodem*)
  if [[ ${#CANDIDATES[@]} -eq 1 ]]; then
    PORT="${CANDIDATES[0]}"
  elif [[ ${#CANDIDATES[@]} -gt 1 ]]; then
    echo "Multiple ports: ${CANDIDATES[*]}" >&2
    echo "Set: export UPLOAD_PORT=/dev/cu.usbserial-XXXX" >&2
    exit 1
  fi
fi

if [[ -z "${PORT}" ]] || [[ ! -e "${PORT}" ]]; then
  echo "No Watchy USB port found." >&2
  echo "  ls /dev/cu.* — should show /dev/cu.usbserial-..." >&2
  echo "  Plug in Watchy (data cable), try another port/cable." >&2
  system_profiler SPUSBDataType 2>/dev/null | grep -i "silicon\|cp210" || true
  exit 1
fi

echo "==> Monitor ${PORT} @ ${BAUD} (Ctrl+C to quit)"
exec "${PIO}" device monitor --port "${PORT}" --baud "${BAUD}"
