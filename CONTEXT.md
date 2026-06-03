# Kontext — watchy-autopilot

Projektfil för dig och AI-assistenter. Uppdatera när milestones, miljö eller båttest ändras.

**Senast:** 2026-06-03 — M3/M4-fix: `signalk_client.cpp` pollar PENDING (HTTP 202) tills COMPLETED 200; längre session-PUT-timeout.

## Vad vi bygger

En **tunn Watchy-armbandsfjärr** för Raymarine-autopilot via **Signal K** på Jubilon-båtens Pi.

- Klockan visar **bekräftad state** (AUTO/STANDBY/WIND + target heading)
- Klockan skickar **intentioner** (±1°, ±10°, lägesbyte) — ingen autopilotlogik på enheten
- **Inte** Pebble, ingen egen Pi-server, ingen full instrumentapp

## Miljöer

| | Hemma (test) | Båt |
|---|--------------|-----|
| WiFi SSID | `8-)_IoT` → **HOME** | `Jubilon J92 8-)` → **BOAT** |
| Pi / Signal K | `http://192.168.1.105:3000` | samma IP i båtnät |
| Pi kör | Ofta sim / utan YDWG | Live N2K via YDWG (Raymarine p70) |
| Signal K | Native `systemctl` (ej Docker) | |

Watchy väljer profil via **cached profile först**, sedan loop över `SK_PROFILES` — inte RSSI-scan trots kommentar i `network_profiles.h`.

## Hemligheter

WiFi-lösenord i **`include/secrets.local.h`** (gitignored). BOAT-lösenord inlagt 2026-06-02.

```bash
cp include/secrets.local.h.example include/secrets.local.h
# redigera — committa aldrig secrets.local.h
```

Device token: `SK_DEVICE_TOKEN` i samma fil — samma token som Pi curl-test.

## Bygglägen (PlatformIO)

| Env | Syfte |
|-----|--------|
| `watchy` | SIM_MODE — knappar/UI utan nätverk (default) |
| `watchy-live` | WiFi + HTTP GET/PUT Signal K (`SIM_MODE=0`) |

```bash
~/.platformio/penv/bin/pio run -e watchy-live -t upload
~/.platformio/penv/bin/pio device monitor --port /dev/cu.usbserial-56230044801 --baud 115200 \
  | tee ~/Desktop/watchy-boat-test.log
```

## Knapplogik (fysisk layout)

**Kod (watchy-live) — källan till sanningen:**

| Knapp | Kort | Håll ~1,5 s | Dubbel |
|-------|------|-------------|--------|
| UP / DOWN | ±1° | ±10° | — |
| SELECT/MENU | *(ingen PUT)* `wake screen` | Från **STANDBY** → AUTO; från AUTO/WIND → STANDBY | AUTO↔WIND *(ej från STANDBY)* |
| BACK | sleep | | |

README/BRIEF kan avvika (t.ex. “dubbel = AUTO”) — följ `AutopilotWatchy.cpp` (`menuCmdFromLongHold`, `menuCmdFromPressCount`).

Skärm: klocka, batteri %, target (3 siffror), state, nere vänster: `HOME` / `BOAT` / `NO SK`.

## Båttest 2026-06-02 (M5)

### Symptom (Watchy)

- Profil **BOAT** syntes på skärmen — WiFi till båtnät OK.
- ~**2 av 20** kommandon upplevdes som lyckade; ofta **ingen vibration** trots p70-läge ändrat.
- **Mjölkig / ghosting** e-paper (ibland OK) — full refresh under WiFi/batteribelastning; ingen skärmuppdatering under aktiv WiFi-session (medvetet i kod).
- **Ingen Mac-serial logg** sparad från båten (`tee` kördes inte).

### Pi-loggar (sparade på `boat-pi`)

| Fil | Innehåll |
|-----|----------|
| `~/watchy-pi-signalk-boat-evening.log` | **244 rader, 17:00–19:16 BST** — rätt fönster |
| `~/watchy-pi-signalk.log` | 18:30–20:00 — nästan tomt |
| `~/watchy-pi-curl-auto.log` | 5× AUTO hemma: alla **HTTP 202 + PENDING** |
| `~/watchy-pi-curl-adj.log` | ±1 i STANDBY: **COMPLETED + HTTP 400** (korrekt) |

**Tidslinje (Pi = BST, +01:00):**

| Tid | Händelse |
|-----|----------|
| **18:11–18:24** | Aktiv PUT-period på båt-WiFi: `state` → **HTTP 202** + plugin **SUCCESS ~1 s**; `adjustHeading` 200 i AUTO/WIND, 400 i standby. YDWG UP ~18:15–18:25. |
| 18:30–19:14 | Inga fler PUT i journal |
| **19:15:17** | YDWG DOWN; WiFi Pi: `Jubilon J92 8-)` → `8-)_IoT` (hemma) |
| 19:15 | Signal K omstart |

**Tolkning:** Loggad autopilot-trafik = **~18:11–18:24**, inte 19:00–19:30. Klockfoton ~19:07 kan vara efter Pi lämnat båtnät eller utan journal-PUT.

### Rotorsak (bekräftad)

Firmware (`signalk_client.cpp` → `parseHttpPutResponse`) kräver på **första** HTTP-svar:

- status **200**
- JSON `"state":"COMPLETED"`

Pi svarar på **autopilot state PUT** med:

- **HTTP 202** + `"state":"PENDING"` + `requestId`/`href`
- Plugin **SUCCESS** ~1 s senare (syns i journal på båten)

→ Klockan räknar **misslyckande** → ingen vibration, ingen säker display-uppdatering — **även när p70 går AUTO**.

Hemma utan YDWG: samma **202/PENDING** på AUTO (curl 2026-06-02). Sim kan kännas “snabbare” om svar ibland är synkront 200.

**±1 i STANDBY:** `COMPLETED` + **400** — förväntat; pilot måste vara i **AUTO** (eller WIND för vissa kommandon).

### Firmware-fix (implementerad 2026-06-03)

1. Efter PUT: **202/PENDING** → polla `href` eller `GET /signalk/v1/requests/{requestId}` var **600 ms** tills `COMPLETED` eller **15 s**.
2. Lyckat = slutligt `COMPLETED` + `statusCode` 200 → vibration + refresh (oförändrat i `AutopilotWatchy.cpp`).
3. Session PUT-timeout **12 s** (`SK_PUT_SESSION_TIMEOUT_MS`); poll-timeout **15 s**.
4. `[AP]`-logg: `HTTP PUT ->` kod, `requestId`, `poll #N PENDING/COMPLETED`, `SK message` vid fail.

Övrigt (senare): display efter session, batteri under WiFi, knapp-dokumentation = kod.

## Milestones

| # | Status | Beskrivning |
|---|--------|-------------|
| M0 | Script | Token + test på Pi (`signalk-ap-wind-test.sh` / curl; `signalk-ap-test.sh` saknas ibland i tree) |
| M1 | **Klar** | UI + knappar + SIM_MODE |
| M2 | **Klar** | WiFi HOME/BOAT + HTTP GET autopilot |
| M3 | **Klar (kod)** | PUT ±1 — poll efter PENDING; båt re-test (M6) |
| M4 | **Klar (kod)** | PUT state — samma poll; båt re-test (M6) |
| M5 | **Testad** | Båttest 2026-06-02; Pi-loggar analyserade |
| M6 | — | Flasha fix; `pio monitor \| tee` + Pi `journalctl` parallellt |

## Viktiga filer

| Fil | Roll |
|-----|------|
| `src/AutopilotWatchy.cpp` | UI, knappar, session, vibration, display-safe |
| `src/signalk_client.cpp` | WiFi, HTTP PUT/GET, **parseHttpPutResponse** ← rotorsak |
| `include/network_profiles.h` | HOME/BOAT SSID, host |
| `include/secrets.local.h` | WiFi + `SK_DEVICE_TOKEN` |
| `include/config.h` | `SK_PUT_SESSION_TIMEOUT_MS=12000`, poll 15 s, `ACTIVE_SESSION_MS=20000` |
| `lib/Watchy/src/Watchy.cpp` | Hoppar display-init på knappväckning |
| `BRIEF.md` | API, Raymarine, N2K |
| `scripts/flash.sh` | Bygg + flash |

## Repo & hårdvara

- GitHub: `chisburg/watchy-autopilot`
- Watchy V2, USB `/dev/cu.usbserial-56230044801`
- Pi: `openplotter` / `boat-pi`, repo `~/jubilon-signalk`

## Nästa steg

1. Flasha `watchy-live`, testa hemma (curl visar 202 — ska bli “lyckat” efter poll i serial).
2. Verifiera `[AP] HTTP PUT PENDING requestId=...` → `poll #N COMPLETED 200` + vibration.
3. **Nästa båt:** `pio monitor | tee` + Pi `journalctl -u signalk -f`; curl AUTO på båt-WiFi med pilot AUTO; polla en requestId manuellt som referens.
4. Verifiera display efter lyckad poll (`refreshDisplaySafe`).
