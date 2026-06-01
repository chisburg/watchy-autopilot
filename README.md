# Watchy autopilot remote (Jubilon / Signal K)

Thin wrist remote for Raymarine autopilot via Signal K on the boat Pi. The watch shows confirmed state and sends intentions only — no autopilot logic on-device.

## Milestone status

| Mode | Env | What |
|------|-----|------|
| SIM | `watchy` | UI + buttons, fake state, no network |
| Live read | `watchy-live` | Auto WiFi profile (HOME/BOAT) + Signal K HTTP read |

See **`CONTEXT.md`** for current status, secrets setup, and button map.

## Secrets (WiFi passwords)

```bash
cp include/secrets.local.h.example include/secrets.local.h
# Edit passwords — file is gitignored
```

## Build & flash (Mac)

```bash
cd watchy-autopilot
~/.platformio/penv/bin/pio run -e watchy              # SIM
~/.platformio/penv/bin/pio run -e watchy-live -t upload  # live + Signal K read
~/.platformio/penv/bin/pio device monitor
```

Live mode uses env **`watchy-live`** (`SIM_MODE=0`).

## Pi: device token + WS test (M0)

On the boat Pi (`/home/pi/jubilon-signalk`):

1. Create a **device token** in Signal K admin (PUT requires token; GET does not).
2. Copy `scripts/signalk-ap-test.sh` to the repo and run:

```bash
chmod +x scripts/signalk-ap-test.sh
./scripts/signalk-ap-test.sh read
DEVICE_TOKEN=your-token ./scripts/signalk-ap-test.sh +1
```

## Button map

| Button | Action |
|--------|--------|
| UP | +1° |
| Double UP | +10° |
| DOWN | -1° |
| Double DOWN | -10° |
| Hold SELECT (MENU) 1.5 s | STANDBY |
| Double SELECT | AUTO |

SELECT = **MENU** button (bottom-left on Watchy).

## Next milestones

- **M2:** WiFi wake, read `steering.autopilot.*` + `navigation.headingMagnetic`
- **M3:** PUT ±1/±10 (pilot must be AUTO)
- **M4:** AUTO/STANDBY
- **M5:** Boat test, battery, reconnect

See `BRIEF.md` for full architecture and boat config.
