#!/usr/bin/env bash
# Build and flash watchy-autopilot — run in Terminal.app (not Cursor agent)
set -euo pipefail
cd "$(dirname "$0")/.."
PIO="${HOME}/.platformio/penv/bin/pio"
PORT="${UPLOAD_PORT:-/dev/cu.usbserial-56230044801}"

echo "==> Stopping Docker containers that may hold ${PORT}..."
if docker ps -q 2>/dev/null | grep -q .; then
  docker stop $(docker ps -q) 2>/dev/null || true
  sleep 2
fi

echo "==> Building..."
"${PIO}" run

echo "==> Uploading to ${PORT}..."
"${PIO}" run -t upload --upload-port "${PORT}"

echo "==> Done. Monitor: ${PIO} device monitor --port ${PORT}"
