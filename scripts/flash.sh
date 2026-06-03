#!/usr/bin/env bash
# Build and flash watchy-autopilot — run in Terminal.app (not Cursor agent)
set -euo pipefail
cd "$(dirname "$0")/.."
PIO="${HOME}/.platformio/penv/bin/pio"
ENV="${1:-watchy-live}"
PORT="${UPLOAD_PORT:-}"

if [[ -z "${PORT}" ]]; then
  PORTS=($(ls /dev/cu.usbserial* /dev/cu.SLAB* 2>/dev/null || true))
  if [[ ${#PORTS[@]} -eq 1 ]]; then
    PORT="${PORTS[0]}"
  else
    PORT="/dev/cu.usbserial-56230044801"
  fi
fi

if [[ ! -x "${PIO}" ]]; then
  echo "PlatformIO missing. Run: ./scripts/setup-mac.sh" >&2
  exit 1
fi

echo "==> Stopping Docker containers that may hold ${PORT}..."
if command -v docker >/dev/null 2>&1 && docker ps -q 2>/dev/null | grep -q .; then
  docker stop $(docker ps -q) 2>/dev/null || true
  sleep 2
fi

echo "==> Building env=${ENV}..."
"${PIO}" run -e "${ENV}"

echo "==> Uploading to ${PORT}..."
"${PIO}" run -e "${ENV}" -t upload --upload-port "${PORT}"

echo "==> Done. Monitor: ${PIO} device monitor --port ${PORT}"
