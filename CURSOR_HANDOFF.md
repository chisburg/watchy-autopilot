# Cursor handoff — watchy-autopilot (Mac USB / Pi-brygga)

**Datum:** 2026-06-03  
**För:** annan Cursor-session / annan dator  
**Läs även:** `CONTEXT.md`, `BRIEF.md`, `README.md`, **`docs/BOAT_TEST_2026-06-03.md`** (båttest + P70-verifiering)

---

## Situation (kort)

Användaren har **ny MacBook Air** (macOS 14.4.1) med projektet `watchy-autopilot`. **Watchy + kabel fungerar på Raspberry Pi** (USB-serial syns, ström OK). **Samma kabel direkt i Mac fungerar inte:** ingen lampa vid inkoppling, ingen `/dev/cu.usbserial*`, `system_profiler` visar ingen Silicon Labs / CP2104.

**Slutsats:** Troligen känt **Mac USB-C direkt-port-problem** (Espressif #10735, esptool #712: ESP32/CP2104 funkar på Linux/Pi men inte Mac direkt; hub/dock löser ofta). Användaren har **ingen USB-hub** och vill undvika “massa installation” på Pi.

**Mjukvara Mac:** PlatformIO OK (`~/.platformio/penv/bin/pio`), `espressif32@7.0.1` installerad, CP2104-drivrutin **activated enabled** (`com.silabs.cp210x 6.0.3`). `include/secrets.local.h` + `SK_DEVICE_TOKEN` OK enligt `./scripts/doctor.sh`.

**Skapat i denna session:** `scripts/monitor.sh` (auto-detect port, kör `pio device monitor`). Bakgrundsmonitor i Cursor kan ha körts — verifiera med `ls /dev/cu.*` innan monitor.

---

## Vad användaren försökte

- Serial monitor på Mac — port saknas
- `ls /dev/cu.usbserial*`, `system_profiler | grep silicon/cp210` → tomt
- Ny kabel testad — samma på Mac; **Pi OK**
- Trodde portar “saknas” på Mac permanent — förklarat: `usbserial` skapas bara när enhet syns i USB
- Vill **inte** installera hela PlatformIO på Pi om möjligt

---

## Rekommenderad väg framåt

### A) Mac USB (om användaren skaffar hub senare)

1. Powered USB-hub mellan Mac och Watchy (nätets vanligaste fix)
2. `ls /dev/cu.*` → ska visa `/dev/cu.usbserial-...`
3. `cd watchy-autopilot && ./scripts/monitor.sh`
4. Flash: `./scripts/flash.sh watchy-live`

`platformio.ini` har `upload_port = /dev/cu.usbserial-56230044801` — det var **gamla Macens** port; uppdatera till faktisk port eller `UPLOAD_PORT=...`.

### B) Pi som USB-brygga (nu, utan hub) — **minimal install**

**Serial monitor (räcker för loggar):**

```bash
# På Pi, Watchy inkopplad
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
sudo apt update && sudo apt install -y screen
screen /dev/ttyUSB0 115200   # byt port
# Avsluta: Ctrl+A, K, Y
```

**Flash utan PlatformIO på Pi:**

```bash
# Mac — bygg (ingen USB till Watchy behövs)
cd watchy-autopilot
~/.platformio/penv/bin/pio run -e watchy-live
scp .pio/build/watchy-live/firmware.bin pi@boat-pi:~/

# Pi — minimal
sudo apt install -y python3-esptool
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 write_flash 0x10000 ~/firmware.bin
```

Verifiera offset/partition om flash failar — annars använd `pio run -t upload` på Pi endast om full PIO installeras där.

**SSH från Mac:**

```bash
ssh pi@boat-pi   # eller openplotter@..., anpassa host
```

Pi-miljö enligt `CONTEXT.md`: `openplotter` / `boat-pi`, Signal K `192.168.1.105:3000`, repo `~/jubilon-signalk`.

---

## Kommandon — Mac diagnostik

```bash
cd /Users/christian/watchy-autopilot
./scripts/doctor.sh
ls /dev/cu.* 2>/dev/null
system_profiler SPUSBDataType 2>/dev/null | grep -i "silicon\|cp210"
systemextensionsctl list | grep -i silab
```

Förväntat **utan Watchy i Mac:** bara `cu.BLTH`, `cu.Bluetooth-Incoming-Port`.

---

## Projekt — tekniskt (oförändrat)

| Env | Syfte |
|-----|--------|
| `watchy` | SIM_MODE, UI test |
| `watchy-live` | WiFi + Signal K HTTP (`SIM_MODE=0`) |

Flash/monitor baud: **115200**. Knapplogik: `src/AutopilotWatchy.cpp` (inte README). `BRIEF.md` säger “flash från Mac” — **överstyr i praktiken av Pi-brygga** tills Mac USB fungerar.

---

## Gör inte

- Anta att Watchy är trasig — Pi-test motbevisar
- Köra `/dev/cu.usbserial-...` som shell-kommando (det är en sökväg)
- `ls /dev/cu.usbserial*` i zsh utan `2>/dev/null` eller bash `nullglob` (ger “no matches found”)
- Installera massa på Pi utan att fråga — minimum: `screen` (+ `python3-esptool` om flash)

---

## Båttest 2026-06-03 (sammanfattning)

- **Pi/N2K/P70:** OK när PUT skickas (`202` → SUCCESS i journal).
- **Bug på klockan under test:** `command failed — no vibration` vid HTTP 202 (gammal firmware).
- **Fix i repo:** `signalk_client.cpp` pollar `requestId`; `SK_PUT_SESSION_TIMEOUT_MS=12000`, poll 15 s.
- **Nästa steg:** **Flasha `watchy-live`** (Pi `/dev/ttyACM0` om Mac USB strular).
- **`createDevice: false`** på YDWG — sätt **inte** Act as N2K device på Jubilon.

---

## Öppna frågor till användaren

1. Exakt Pi-host (`boat-pi` / IP / user)?
2. Vad visar `ls /dev/ttyUSB*` på Pi med Watchy inkopplad?
3. Ska nästa steg vara **bara monitor** eller **flash watchy-live**?
4. Vill de köpa liten USB-hub för Mac senare?

---

## Filer touchade denna session

- `scripts/monitor.sh` (ny)
- `CURSOR_HANDOFF.md` (denna fil)
