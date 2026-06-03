#!/usr/bin/env bash
# Copy watchy-live firmware to Pi (Tailscale boat-pi or local IP).
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=".pio/build/watchy-live"
PI_HOST="${PI_HOST:-pi@boat-pi}"
PI_DIR="${PI_DIR:-~/watchy-flash}"

for f in firmware.bin bootloader.bin partitions.bin; do
  if [[ ! -f "${BUILD}/${f}" ]]; then
    echo "Missing ${BUILD}/${f} — run: pio run -e watchy-live" >&2
    exit 1
  fi
done

echo "==> Copy to ${PI_HOST}:${PI_DIR}/"
ssh "${PI_HOST}" "mkdir -p ${PI_DIR}"
scp "${BUILD}/firmware.bin" "${BUILD}/bootloader.bin" "${BUILD}/partitions.bin" \
  "${PI_HOST}:${PI_DIR}/"

echo ""
echo "OK. On Pi:"
echo "  ls -la ${PI_DIR}/"
echo "  # flash (Watchy on /dev/ttyACM0):"
echo "  python3 -m esptool --chip esp32 --port /dev/ttyACM0 --baud 115200 \\"
echo "    write_flash 0x1000 ${PI_DIR}/bootloader.bin \\"
echo "    0x8000 ${PI_DIR}/partitions.bin \\"
echo "    0x10000 ${PI_DIR}/firmware.bin"
