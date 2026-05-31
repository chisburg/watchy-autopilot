# Brief — Watchy autopilot-fjärrkontroll för Signal K / Raymarine (Jubilon)

## Mål

Bygg en tunn armbands-fjärrkontroll för Raymarine-autopilot via Signal K på Jubilon. Klockan ska inte innehålla autopilotlogik — bara visa data, skicka intentioner, visa bekräftad state.

**Inte:** Pebble (parkeras — telefonberoende), egen HTTP-server på Pi, fullständig instrumentapp.

## Båtmiljö (verifierat)

| Komponent | Värde |
|-----------|-------|
| Pi | openplotter, 192.168.1.105, Signal K v2.27.0 :3000 |
| Båt-WiFi | SSID Jubilon J92 8-) |
| YDWG | 192.168.1.200, UDP 1457, RAW, Both, createDevice: true |
| Autopilot-plugin | @signalk/signalk-autopilot v2.5.0, raymarineN2K, device 204 |
| v2 API | enableV2API: true, autopilot-id raymarineN2K |
| Auth | GET ok utan token; PUT kräver device token (401) |
| Verifierat | Auto/standby via UDP+createDevice (2026-05-26) |
| Config på Pi (ej i git) | `/home/pi/.signalk/` |
| Repo | `/home/pi/jubilon-signalk` |

## Hårdvara — Watchy (användaren har)

- Watchy (ESP32, e-ink 200×200, 4 knappar, vibration, 200 mAh)
- WiFi + BLE direkt — ingen telefon
- Flashas från Mac via USB (inte från Pi)
- Användaren har Docker + InkWatchy dev container (vsc-inkwatchy-master, ~2 år gammal). Alternativ: nytt enkelt PlatformIO-projekt med officiellt Watchy library.

## Arkitektur

```
Watchy ──WiFi (wake-on-knapp)──► http://192.168.1.105:3000 (Signal K WebSocket)
                                      │
                                      ▼
                              signalk-autopilot (raymarineN2K)
                                      │
                                      ▼
                              YDWG UDP :1457 ──► Raymarine N2K
```

All applogik på Watchy. Pi kör bara Signal K (som idag). Ingen ny Pi-tjänst i första iterationen.

Senare (valfritt): BLE → hub vid rodret om WiFi/batteri inte räcker.

## API — samma som signalk-autopilot-webappen

WebSocket till `/signalk/v1/stream?subscribe=none`

PUT (v1 paths):

| Action | Path | value |
|--------|------|-------|
| AUTO | steering.autopilot.state | "auto" |
| STANDBY | steering.autopilot.state | "standby" |
| +1° / -1° / +10° / -10° | steering.autopilot.actions.adjustHeading | 1, -1, 10, -10 |

Raymarine-driver accepterar endast exakt ±1 och ±10. Kräver pilot i auto eller wind.

WebSocket PUT-format:

```json
{
  "context": "vessels.self",
  "requestId": "<uuid-v4>",
  "put": { "path": "steering.autopilot.actions.adjustHeading", "value": 1 }
}
```

Bekräftelse: requestId + state: COMPLETED + statusCode: 200, plus delta på paths.

Läs (delta-prenumeration):

- `steering.autopilot.state`
- `steering.autopilot.target.headingMagnetic` (rad → grader)
- `navigation.headingMagnetic` (rad → grader)

## UI

```
335°          ← target (störst)
AUTO          ← state uppercase
HDG 328°      ← current heading
```

State-mappning från Signal K:

| SK value | Display |
|----------|---------|
| standby | STANDBY |
| auto | AUTO |
| wind | WIND |
| route | TRACK |
| okänd/saknas | UNKNOWN |

Vid timeout (>3 s utan delta): **NO SK LINK**

I WIND: heading-target kan saknas — visa `---°` tills AWA-stöd ev. läggs till.

## Knappar

| Knapp | Action |
|-------|--------|
| UP | +1° |
| DOUBLE UP | +10° |
| DOWN | -1° |
| DOUBLE DOWN | -10° |
| HOLD SELECT 1.5 s | STANDBY |
| DOUBLE SELECT | AUTO |

## Feedback

- Kort vibration = kommando skickat
- Längre vibration = COMPLETED 200
- Ingen lokal gissning av target/state

## Batteri / WiFi-strategi

Inte alltid-på WebSocket (tömer 200 mAh på ~1–2 h).

Wake-on-knapp:

1. Knapp → WiFi på → WS → PUT → svar → ev. läs delta → WiFi av
2. Valfritt: håll WiFi varm 30–60 s efter första tryck för snabbare följd-kommandon

Latens: ~1–4 s per wake; uppföljande tryck inom varm session ~0.5–1 s.

## SIM_MODE

`SIM_MODE=true` i firmware/config:

- Logga knapptryck, fake target ±1/±10, fake state
- Ingen trafik till Signal K / Raymarine
- Samma UI som live

## Milestones

| # | Vad | Var |
|---|-----|-----|
| 0 | Device token + scripts/signalk-ap-test.sh | Pi |
| 1 | UI + knappar + SIM_MODE | Watchy |
| 2 | WiFi → läs live state/target/hdg | Watchy |
| 3 | Skicka ±1/±10 (pilot i AUTO via p70s/webapp) | Watchy + Pi |
| 4 | AUTO/STANDBY (hold/double SELECT) | Watchy |
| 5 | Båttest, batteri, reconnect | Båt |

M6 AUTO/STANDBY efter ±1 fungerar. M3 ±1 kräver pilot i AUTO — sätt via p70s tills M4.

## Utvecklingsmiljö

| Var | Roll |
|-----|------|
| Mac | Bygga/flasha Watchy (USB). Docker/InkWatchy dev container ELLER nytt PlatformIO-projekt |
| Pi / Cursor Remote SSH | Signal K, token, testskript, repo jubilon-signalk |
| Watchy | Hela autopilot-appen |

Ny projektmapp: `watchy-autopilot/` i repot.

## Avvisade alternativ (beslut)

- Pebble Steel — kräver telefon (Rebble/PKJS), för svajigt
- RCU-1 + WG-1 — bra men fickkontroll, inte armband; direkt N2K, inte Signal K
- Garmin m.fl. — stängda, dålig lokal HTTP till SK
- Pi som BLE-hub — senare opt-in, inte start

## Referenser

- Plugin JS (kommandon): `@signalk/signalk-autopilot/public-src/js/signalk-autopilot.js` på Pi
- Agentkontext båt: AGENTS.md i repot
- InkWatchy: https://github.com/Szybet/InkWatchy
- Watchy docs: https://watchy.sqfmi.com/docs/getting-started

Brief skapad 2026-05-31. Pebble-plan ersatt av Watchy + WiFi wake.
