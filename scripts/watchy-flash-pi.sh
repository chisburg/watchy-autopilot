#!/usr/bin/env bash
# Flash watchy-live from ~/watchy-flash/ (copied from Mac via copy-firmware-to-pi.sh)
set -euo pipefail

DIR="${WATCHY_FLASH_DIR:-$HOME/watchy-flash}"
PORT="${UPLOAD_PORT:-/dev/ttyACM0}"
BAUD="${BAUD:-115200}"

for f in firmware.bin bootloader.bin partitions.bin; do
  if [[ ! -f "${DIR}/${f}" ]]; then
    echo "Missing ${DIR}/${f}" >&2
    echo "Copy from Mac: ./scripts/copy-firmware-to-pi.sh" >&2
    exit 1
  fi
done

if [[ ! -e "${PORT}" ]]; then
  echo "Port ${PORT} not found. Plug Watchy into Pi USB." >&2
  ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true
  exit 1
fi

if ! python3 -c "import esptool" 2>/dev/null; then
  echo "Installing esptool..."
  sudo apt-get update -qq
  sudo apt-get install -y python3-esptool
fi

echo "==> Flash ${PORT} from ${DIR}"
python3 -m esptool --chip esp32 --port "${PORT}" --baud "${BAUD}" \
  write_flash -z \
  0x1000 "${DIR}/bootloader.bin" \
  0x8000 "${DIR}/partitions.bin" \
  0x10000 "${DIR}/firmware.bin"

echo "==> Done. Monitor:"
echo "  screen ${PORT} ${BAUD}"
