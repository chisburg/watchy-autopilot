#!/usr/bin/env bash
# Signal K autopilot WebSocket smoke test (Milestone 0)
# Run on Pi: ./scripts/signalk-ap-test.sh
# Requires: websocat (apt install websocat) or wscat (npm i -g wscat)

set -euo pipefail

SK_HOST="${SK_HOST:-192.168.1.105}"
SK_PORT="${SK_PORT:-3000}"
SK_URL="ws://${SK_HOST}:${SK_PORT}/signalk/v1/stream?subscribe=none"
TOKEN_FILE="${TOKEN_FILE:-/home/pi/.signalk/accessTokens.json}"
DEVICE_TOKEN="${DEVICE_TOKEN:-}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [read|standby|auto|wind|+1|-1|+10|-10]

Environment:
  SK_HOST       Signal K host (default: 192.168.1.105)
  SK_PORT       Signal K port (default: 3000)
  DEVICE_TOKEN  Device token for PUT (or read from TOKEN_FILE)
  TOKEN_FILE    Token store (default: /home/pi/.signalk/accessTokens.json)

Examples:
  $(basename "$0") read
  DEVICE_TOKEN=xxx $(basename "$0") +1
  $(basename "$0") auto
EOF
}

load_token() {
  if [[ -n "$DEVICE_TOKEN" ]]; then
    return 0
  fi
  if [[ ! -f "$TOKEN_FILE" ]]; then
    echo "No DEVICE_TOKEN and missing $TOKEN_FILE" >&2
    echo "Create a device token in Signal K admin UI first." >&2
    exit 1
  fi
  DEVICE_TOKEN="$(TOKEN_FILE="$TOKEN_FILE" python3 - <<'PY'
import json, os, sys
path = os.environ["TOKEN_FILE"]
with open(path) as f:
    data = json.load(f)
tokens = data if isinstance(data, list) else data.get("tokens", [])
for t in tokens:
    if isinstance(t, dict) and t.get("token"):
        print(t["token"])
        break
else:
    sys.exit("No token found in accessTokens.json")
PY
)"
}

uuid() {
  python3 -c 'import uuid; print(uuid.uuid4())'
}

put_msg() {
  local path="$1"
  local value="$2"
  local rid
  rid="$(uuid)"
  cat <<JSON
{"context":"vessels.self","requestId":"${rid}","put":{"path":"${path}","value":${value}}}
JSON
}

ws_send() {
  local msg="$1"
  if command -v websocat >/dev/null 2>&1; then
    printf '%s\n' "$msg" | websocat -n1 "$SK_URL"
  elif command -v wscat >/dev/null 2>&1; then
    printf '%s\n' "$msg" | wscat -c "$SK_URL" -x
  else
    echo "Install websocat or wscat" >&2
    exit 1
  fi
}

ws_put() {
  local path="$1"
  local value="$2"
  load_token
  local msg rid
  msg="$(put_msg "$path" "$value")"
  rid="$(echo "$msg" | python3 -c 'import json,sys; print(json.load(sys.stdin)["requestId"])')"
  echo "PUT $path = $value (requestId=$rid)"
  local resp
  resp="$(ws_send "$msg")"
  echo "$resp"
  echo "$resp" | grep -q "\"requestId\":\"${rid}\"" || {
    echo "WARN: response missing requestId" >&2
  }
}

ws_subscribe_read() {
  local sub
  sub='{"context":"vessels.self","subscribe":[{"path":"steering.autopilot.state","policy":"instant"},{"path":"steering.autopilot.target.headingMagnetic","policy":"instant"}]}'
  echo "Subscribing to autopilot paths on $SK_URL (5s)..."
  if command -v websocat >/dev/null 2>&1; then
    (printf '%s\n' "$sub"; sleep 5) | websocat "$SK_URL"
  elif command -v wscat >/dev/null 2>&1; then
    (printf '%s\n' "$sub"; sleep 5) | wscat -c "$SK_URL"
  fi
}

cmd="${1:-read}"
case "$cmd" in
  read)
    ws_subscribe_read
    ;;
  standby)
    ws_put "steering.autopilot.state" '"standby"'
    ;;
  auto)
    ws_put "steering.autopilot.state" '"auto"'
    ;;
  wind)
    ws_put "steering.autopilot.state" '"wind"'
    ;;
  +1)
    ws_put "steering.autopilot.actions.adjustHeading" 1
    ;;
  -1)
    ws_put "steering.autopilot.actions.adjustHeading" -1
    ;;
  +10)
    ws_put "steering.autopilot.actions.adjustHeading" 10
    ;;
  -10)
    ws_put "steering.autopilot.actions.adjustHeading" -10
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    echo "Unknown command: $cmd" >&2
    usage
    exit 1
    ;;
esac
