# Watchy autopilot remote (Jubilon / Signal K)

Thin wrist remote for Raymarine autopilot via Signal K on the boat Pi. The watch shows confirmed state and sends intentions only — no autopilot logic on-device.

## Milestone 1 (current): SIM_MODE

- UI: target heading (large), state, current HDG
- Buttons: UP/DOWN ±1°, double ±10°, hold SELECT 1.5 s → STANDBY, double SELECT → AUTO
- `SIM_MODE=1`: local fake state, serial logging, no WiFi/Signal K

## Hardware / tooling (verified 2026-05-31)

| Item | Value |
|------|-------|
| USB serial | `/dev/cu.usbserial-56230044801` (CH340 — Watchy connected) |
| PlatformIO | `~/.platformio/penv/bin/pio` v6.1.17 |
| Default board | `watchy` → **SQFMI Watchy V2.0** (ESP32, 200×200 e-ink) |
| InkWatchy Docker | `vsc-inkwatchy-master` (~2 yr old); holds serial while running |

**Watchy revision:** PlatformIO target is V2.0. If you have V1/V1.5/V3, switch env in `platformio.ini` (`watchy-v10`, `watchy-v15`, `watchy-v3`). At runtime, `getBoardRevision()` can confirm after flash.

**Recommendation:** Use **PlatformIO on Mac** for this project (official `sqfmi/Watchy` library). Stop the InkWatchy container before upload if the serial port is busy.

## Build & flash (Mac)

```bash
cd watchy-autopilot
~/.platformio/penv/bin/pio run                    # build SIM_MODE
~/.platformio/penv/bin/pio run -t upload          # flash (stop Docker first)
~/.platformio/penv/bin/pio device monitor         # serial log
```

Live mode (later): build with `-DSIM_MODE=0` in `platformio.ini`.

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
