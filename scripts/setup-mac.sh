#!/usr/bin/env bash
# One-time Mac setup for watchy-autopilot — run in Terminal.app (not Cursor agent).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
PIO="${HOME}/.platformio/penv/bin/pio"
PY="${HOME}/.platformio/penv/bin/python3"

echo "==> watchy-autopilot setup (${ROOT})"

# --- PlatformIO ---
if [[ ! -x "${PIO}" ]]; then
  echo "==> Installing PlatformIO Core..."
  if [[ ! -x "${PY}" ]]; then
    curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py | python3 -
  fi
  if [[ ! -x "${PIO}" ]] && [[ -x "${PY}" ]]; then
    echo "==> Standard install failed — trying patched wheel (no .vscode templates)..."
    TMP="$(mktemp -d)"
    trap 'rm -rf "${TMP}"' EXIT
    "${PY}" -m pip download -q -d "${TMP}" platformio==6.1.19
    WHL_IN="$(ls "${TMP}"/platformio-6.1.19-*.whl | head -1)"
    WHL_OUT="${TMP}/platformio-6.1.19-py3-none-any.whl"
    "${PY}" <<PY
import zipfile
whl_in = "${WHL_IN}"
whl_out = "${WHL_OUT}"
with zipfile.ZipFile(whl_in) as zin, zipfile.ZipFile(whl_out, "w", zipfile.ZIP_DEFLATED) as zout:
    for item in zin.infolist():
        if "/.vscode/" in item.filename:
            continue
        zout.writestr(item, zin.read(item.filename))
print("patched wheel:", whl_out)
PY
    "${PY}" -m pip install --force-reinstall --no-deps "${WHL_OUT}"
  fi
fi

if [[ ! -x "${PIO}" ]]; then
  echo "ERROR: PlatformIO not found at ${PIO}" >&2
  echo "Run this script in Terminal.app (Cursor agent cannot create .vscode paths)." >&2
  exit 1
fi
echo "==> PlatformIO: $("${PIO}" --version)"

# --- PATH hint ---
if ! grep -q '.platformio/penv/bin' "${HOME}/.zshrc" 2>/dev/null; then
  echo 'export PATH="$HOME/.platformio/penv/bin:$PATH"' >> "${HOME}/.zshrc"
  echo "==> Added PlatformIO to ~/.zshrc"
fi

# --- secrets.local.h ---
if [[ ! -f include/secrets.local.h ]]; then
  cp include/secrets.local.h.example include/secrets.local.h
  echo "==> Created include/secrets.local.h — edit SK_DEVICE_TOKEN before live flash"
fi

# --- git (if missing) ---
if [[ ! -d .git ]]; then
  echo "==> Initializing git and fetching main..."
  git init -q
  git remote add origin https://github.com/chisburg/watchy-autopilot.git 2>/dev/null || \
    git remote set-url origin https://github.com/chisburg/watchy-autopilot.git
  git fetch -q origin main
  git checkout -q -B main origin/main 2>/dev/null || git checkout -q main 2>/dev/null || true
fi

# --- Docker (optional) ---
if command -v docker >/dev/null 2>&1 && docker ps -q 2>/dev/null | grep -q .; then
  echo "==> Stopping Docker containers (can hold USB serial)..."
  docker stop $(docker ps -q) 2>/dev/null || true
fi

# --- USB port ---
PORTS=($(ls /dev/cu.usbserial* /dev/cu.SLAB* 2>/dev/null || true))
if [[ ${#PORTS[@]} -eq 1 ]]; then
  export UPLOAD_PORT="${PORTS[0]}"
  echo "==> Watchy port: ${UPLOAD_PORT}"
elif [[ ${#PORTS[@]} -gt 1 ]]; then
  echo "==> Multiple serial ports: ${PORTS[*]}"
  echo "    Set: export UPLOAD_PORT=/dev/cu.usbserial-XXXX"
else
  echo "==> No Watchy USB port yet (plug in via USB, install CP2104 if needed)"
fi

# --- First build (downloads ESP32 toolchain) ---
echo "==> Building watchy-live (first run may take several minutes)..."
"${PIO}" run -e watchy-live

echo ""
echo "Setup OK."
echo "  Flash:  ./scripts/flash.sh"
echo "  Live:   UPLOAD_PORT=... ./scripts/flash.sh watchy-live"
echo "  Monitor: ${PIO} device monitor --port \${UPLOAD_PORT:-/dev/cu.usbserial-...} --baud 115200 | tee ~/Desktop/watchy-test.log"
if [[ -f include/secrets.local.h ]] && grep -q 'SK_DEVICE_TOKEN ""' include/secrets.local.h; then
  echo "  WARNING: Set SK_DEVICE_TOKEN in include/secrets.local.h (copy from Mac 1 or Pi)"
fi
