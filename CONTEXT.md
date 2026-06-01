# Kontext — watchy-autopilot

Projektfil för dig och AI-assistenter. Uppdatera när milestones eller miljö ändras.

## Vad vi bygger

En **tunn Watchy-armbandsfjärr** för Raymarine-autopilot via **Signal K** på Jubilon-båtens Pi.

- Klockan visar **bekräftad state** (AUTO/STANDBY/WIND + target heading)
- Klockan skickar **intentioner** (±1°, ±10°, lägesbyte) — ingen autopilotlogik på enheten
- **Inte** Pebble, ingen egen Pi-server, ingen full instrumentapp

## Miljöer

| | Hemma (test) | Båt |
|---|--------------|-----|
| WiFi SSID | `8-)_IoT` | `Jubilon J92 8-)` |
| Pi / Signal K | `http://192.168.1.105:3000` | samma IP i båtnät |
| Pi kör | Signal K med simulerade värden | Live N2K via YDWG |

Watchy väljer **automatiskt** profil (HOME/BOAT) via WiFi-scan vid knapptryck.

## Hemligheter

WiFi-lösenord ligger i **`include/secrets.local.h`** (gitignored).

```bash
cp include/secrets.local.h.example include/secrets.local.h
# redigera lösenord — committa aldrig secrets.local.h
```

## Bygglägen (PlatformIO)

| Env | Syfte |
|-----|--------|
| `watchy` | SIM_MODE — knappar/UI utan nätverk (default) |
| `watchy-live` | WiFi + HTTP-läsning från Signal K |

```bash
~/.platformio/penv/bin/pio run -e watchy-live -t upload
~/.platformio/penv/bin/pio device monitor
```

## Knapplogik (fysisk layout)

| Knapp | Kort | Håll | Dubbel |
|-------|------|------|--------|
| UP (höger upp) | ±1° | ±10° | — |
| DOWN (höger ner) | ±1° | ±10° | — |
| SELECT/MENU (vänster ner) | STANDBY→AUTO | STANDBY | AUTO↔WIND |
| BACK (vänster upp) | ignoreras → sleep | | |

Skärm: klocka (24h), batteri %, target heading, state. Nere vänster: `SIM` / `HOME` / `BOAT` / `NO SK`.

## Milestones

| # | Status | Beskrivning |
|---|--------|-------------|
| M0 | Script klart | Device token + `scripts/signalk-ap-test.sh` på Pi |
| M1 | **Klar** | UI + knappar + SIM_MODE |
| M2 | **Pågår** | WiFi auto-profil + läs live state/target |
| M3 | — | PUT ±1 / ±10 |
| M4 | — | PUT AUTO / STANDBY / WIND |
| M5 | — | Båttest, batteri, reconnect |

## Viktiga filer

| Fil | Roll |
|-----|------|
| `src/AutopilotWatchy.cpp` | UI, knappar, session |
| `src/signalk_client.cpp` | WiFi-profil + HTTP GET vessels.self |
| `include/network_profiles.h` | SSID, host, port (inga lösenord) |
| `include/secrets.local.h` | WiFi-lösenord (lokal, gitignored) |
| `include/config.h` | Timing, debug, SIM_MODE |
| `platformio.ini` | Env: watchy / watchy-live |
| `BRIEF.md` | Full teknisk brief + båt-API |
| `scripts/flash.sh` | Bygg + flash |

## Repo & hårdvara

- GitHub: `chisburg/watchy-autopilot`
- Watchy V2, USB `/dev/cu.usbserial-56230044801`
- Pi repo (båt): `/home/pi/jubilon-signalk`

## Nästa steg (typiskt)

1. `secrets.local.h` på plats
2. Flasha `watchy-live`, verifiera `HOME` + live-värden hemma
3. M3/M4: WebSocket PUT med device token
4. M5: test på båten med `BOAT`-profil
