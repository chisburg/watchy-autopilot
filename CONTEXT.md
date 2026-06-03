# Kontext — watchy-autopilot

Projektfil för dig och AI-assistenter. Uppdatera när milestones, miljö eller båttest ändras.

**Senast:** 2026-06-03 — M3/M4-fix + heap-säker PUT/poll; `WIFI_DISPLAY_SETTLE_MS`; serial `[PWR]` (vbat, pct, rssi, connect_ms).

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
| M6 | **Flash Mac klar** | Båt/hemma serial + P70 — se checklista nedan |

## Viktiga filer

| Fil | Roll |
|-----|------|
| `src/AutopilotWatchy.cpp` | UI, knappar, session, vibration, display-safe |
| `src/signalk_client.cpp` | WiFi, HTTP PUT/GET, lightweight parse PUT/poll |
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

## Session 2026-06-03 — kodstatus (komplettering)

| Punkt | Status i repo |
|-------|----------------|
| `SK_PUT_SESSION_TIMEOUT_MS` | **12000** (inte 3 s — det var gammal flash) |
| `SK_PUT_POLL_TIMEOUT_*` | **15000**; poll var **600 ms** |
| `ACTIVE_SESSION_MS` | **20000** (> poll-timeout) |
| `adjustHeading` ±1/±10 | Samma `httpPut()` + poll som `state` |
| HTTP ≠ 200/202 på PUT | Avbryts **innan** parse → `put fail`, ingen success-vibration |
| `COMPLETED` + `statusCode` 200 | Enda `put ok` / vibration |
| `COMPLETED` + t.ex. 400 i body | `CompletedFail` → `put fail` |
| `after state=` direkt efter PUT | **Oförändrat** — `logState("after")` + ev. `refreshFromSignalK()` kan läsa SK före N2K hunnit uppdatera; UI kan visa gammalt läge tills session end / display |
| `fetchAutopilot` GET | Stack `SK_GET_BODY_MAX` (3072) + lightweight parse — **samma mönster som PUT** |
| Git commit/push | **Ej gjort** i denna session |
| Pi `createDevice` | **false** på båt — config i jubilon-signalk, inte watchy |

## M6 — verifiering (efter flash)

**Mac:**

```bash
cd watchy-autopilot
./scripts/monitor.sh | tee ~/Desktop/watchy-test.log
```

**Pi (parallellt):**

```bash
journalctl -u signalk -f | grep -iE 'PUT|SUCCESS|autopilot'
```

**Ett tryck AUTO (P70 i STANDBY):**

| Förväntat serial | Fel om saknas |
|------------------|----------------|
| `HTTP PUT -> 202` | |
| `HTTP PUT PENDING requestId=` | Gammal binär / parse |
| `HTTP PUT poll #N COMPLETED 200` | Poll fail |
| `put ok` | |
| Vibration (2× kort) | `command failed` |
| **Ej** `CORRUPT HEAP` | Heap-fix |

**Wake screen (mjölkig skärm):** `grep '\[PWR\]'` — jämför `post settle` vs `display done` vbat.

**±1 i STANDBY:** `put fail`, ingen success-vibration (HTTP 400 eller COMPLETED≠200).

**Acceptans båt:** ett tryck AUTO/STANDBY → vibration + P70; skärm skarp eller dokumenterad vbat-sänkning.

## Serial — ström/display (`[PWR]`)

Sök: `grep '\[PWR\]' watchy-boat.log`

| Tag | Betydelse |
|-----|-----------|
| `wake` / `setup` | Basnivå vid start |
| `wifi ok` + `connect_ms` | Anslutningstid (båt ofta längre) |
| `sk sync start` / `done` | Före/efter GET autopilot |
| `session start` / `end` | WiFi-session |
| `put start` / `ok` / `fail` | PUT-belastning |
| `pre display wifi off` / `post settle` | Före/efter 1 s paus före e-paper |
| `display done` | Efter full refresh |

**Mjölkig skärm:** jämför `vbat` på `post settle` vs `display done` (hemma vs båt). Sjunker >0,1 V under en väckning → underspänning.
