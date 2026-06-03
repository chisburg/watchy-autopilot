#!/usr/bin/env bash
# Quick readiness check — no installs.
set -uo pipefail
cd "$(dirname "$0")/.."
PIO="${HOME}/.platformio/penv/bin/pio"
OK=0
FAIL=0

check() {
  if "$@"; then
    echo "  OK   $*"
    OK=$((OK + 1))
  else
    echo "  FAIL $*"
    FAIL=$((FAIL + 1))
  fi
}

echo "watchy-autopilot doctor"
echo ""

echo "[PlatformIO]"
if [[ -x "${PIO}" ]]; then
  echo "  OK   ${PIO} ($("${PIO}" --version))"
else
  echo "  FAIL ${PIO} missing — run: ./scripts/setup-mac.sh"
  FAIL=$((FAIL + 1))
fi

echo "[Project]"
test -f platformio.ini && echo "  OK   platformio.ini" || { echo "  FAIL platformio.ini"; FAIL=$((FAIL + 1)); }
test -f include/secrets.local.h && echo "  OK   include/secrets.local.h" || { echo "  FAIL include/secrets.local.h — cp example or run setup-mac.sh"; FAIL=$((FAIL + 1)); }
if [[ -f include/secrets.local.h ]]; then
  grep -q 'SK_DEVICE_TOKEN ""' include/secrets.local.h 2>/dev/null && \
    echo "  WARN SK_DEVICE_TOKEN empty (live PUT will fail)" || echo "  OK   SK_DEVICE_TOKEN set"
fi

echo "[USB]"
PORTS=($(ls /dev/cu.usbserial* /dev/cu.SLAB* 2>/dev/null || true))
if [[ ${#PORTS[@]} -eq 0 ]]; then
  echo "  WARN no /dev/cu.usbserial* — plug in Watchy or install CP2104 driver"
else
  echo "  OK   ports: ${PORTS[*]}"
fi

echo "[Docker]"
if command -v docker >/dev/null 2>&1; then
  if docker ps -q 2>/dev/null | grep -q .; then
    echo "  WARN Docker containers running — may block serial (run: docker stop \$(docker ps -q))"
  else
    echo "  OK   no running containers"
  fi
else
  echo "  OK   docker not installed"
fi

echo "[Git]"
test -d .git && echo "  OK   .git present" || echo "  WARN no .git — run setup-mac.sh"

echo ""
if [[ ${FAIL} -eq 0 ]]; then
  echo "Ready to flash (if USB + token OK)."
  exit 0
fi
echo "${FAIL} check(s) failed. Run: ./scripts/setup-mac.sh"
exit 1
