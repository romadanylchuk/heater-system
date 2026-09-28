# heater-system

Two independent KC868-A6 (ESP32) controllers, sharing one PlatformIO toolchain:

- `boiler-room/` — solid-fuel boiler + accumulator controller.
- `home-heating/` — home mixing-valve / DHW controller.

## Layout

```
heater-system/
├── common/
│   ├── platformio-common.ini   shared envs (release/debug/native), included by both projects
│   ├── lib/
│   │   ├── BoardConfig/        header-only, pure pin/address map + relay-mask helper (native + ESP32)
│   │   ├── RelayBoot/          Arduino-only: writes the relay all-OFF byte at boot
│   │   ├── CoreEngine/         pure, native-testable firmware core (see "Core engine" below)
│   │   ├── CoreEsp32/          ESP32-only adapters for CoreEngine (NVS, RTC, SNTP, watchdog, DI1)
│   │   ├── HwEngine/           pure, native-testable relay/K1/DS18B20/anti-seize logic + SensorHistory (see "Hardware services" below)
│   │   ├── HwEsp32/            ESP32-only adapters for HwEngine (PCF8574, OneWire/DallasTemperature)
│   │   ├── NetEngine/          pure, native-testable Wi-Fi/MQTT/HA/OTA logic (see "Connectivity" below)
│   │   ├── NetEsp32/           ESP32-only Wi-Fi/mDNS/MQTT/OTA adapters, web server, ConnectivityServices
│   │   ├── DisplayEngine/      pure, native-testable OLED frame/pages/scheduler (see "OLED display" below)
│   │   └── DisplayEsp32/       ESP32-only SSD1306 driver (U8g2, 100 kHz) + DisplayServices
│   ├── web/                    shared web UI sources (style.css, lang/); assembled into <project>/data/
│   ├── scripts/                PlatformIO pre-scripts (fw_version.py, web_assemble.py)
│   └── test/                   CommonSuite.h — native tests shared by both projects
├── docs/                       owner docs: HA "no need" automation YAML, K1 tuning guide (k1-tuning-guide.md)
├── boiler-room/                PlatformIO project (src/, web/, test/)
│   └── lib/BoilerRoomEngine/   pure, native-testable pump controller + views (see "Boiler-room controller" below)
└── home-heating/               PlatformIO project (src/, web/, test/)
    └── lib/HomeHeatingEngine/  pure, native-testable P4/K2/K1 controller + views (see "Home-heating controller" below)
```

## Core engine (stage 02)

Both controllers share one firmware core, split across two libraries by hardware dependency:

- **`common/lib/CoreEngine`** — pure C++ (`platforms: *`, `frameworks: *`), no `Arduino.h`/`Wire.h`/
  `nvs.h`/FreeRTOS/`esp_*` includes anywhere, so it builds and unit-tests on the host (`pio test -e
  native`). It holds the descriptor-driven settings engine (load/clamp/debounce/save, versioning +
  migration, factory reset, JSON backup export/import), the persistent event log (50-entry ring,
  CRC-guarded slot persistence, 60 s per-type+source rate limit), the common `AppState` part
  (`CommonState`) and command model, the single-writer `CoreRuntime`, the DI1 factory-reset gate
  state machine, and pure time logic (DS1307 BCD codec, civil/epoch conversion, POSIX-TZ
  plausibility, `TimeKeeper`). Storage and time sit behind the `KvStore`/`Clock` interfaces so
  native tests use in-memory fakes (`common/test/fakes/`).
- **`common/lib/CoreEsp32`** — ESP32-only adapters (`frameworks: arduino`, `platforms: espressif32`):
  `NvsKvStore` (IDF `nvs.h`), `FreeRtosCommandQueue`, `Ds1307Rtc` (I2C), `TimeService` (RTC + SNTP +
  TZ), `Watchdog` (task WDT + reset-cause capture), `FactoryResetInput` (DI1 boot countdown), and the
  `CoreServices` facade both `main.cpp` files construct and drive.

Both projects build with **`-std=gnu++17`** in every env (release/debug/native), so the ESP32
toolchain (gcc 8.4, default `gnu++11`) and the host gcc compile the same language level.

### NVS namespaces

- **`cfg`** — every resettable value: all settings (Wi-Fi, MQTT, web login, time zone, NTP server,
  controller settings) plus the config-version key `cfgVer`. A factory reset erases this namespace
  only (`nvs_erase_all()` on the `cfg` handle — never `nvs_flash_erase()`), so it is safe to add any
  future resettable data here.
- **`evlog`** — the 50 event-log slots (`ev00`..`ev49`), one NVS blob per slot. It survives a
  factory reset.

### Backup JSON shape

```json
{"type":"boiler-room","format":1,"configVersion":1,"fwVersion":"...","settings":{"<key>":<number|bool|string>,...}}
```

`type` is the controller type (`boiler-room`/`home-heating`); import refuses a file for the other
type, or one with an unknown `format`, with zero changes. Wi-Fi settings are excluded from export
(`SETTING_FLAG_NO_BACKUP`); MQTT and web-login secrets are included in plain text.

### Event catalogue

Common event types/sources live in `common/lib/CoreEngine/src/EventTypes.h`. Project-specific
controllers define their own event types starting at `EVENT_TYPE_PROJECT_BASE` (1000) so they never
collide with the shared catalogue.

### DI1 factory-reset procedure

Holding DI1 (PCF8574 @ `0x22`, bit `FACTORY_RESET_INPUT`) closed through power-on for 10 s wipes the
`cfg` namespace (all settings, including Wi-Fi/MQTT/login — back to `admin`/`admin`) and requests
setup-AP mode; the event log is kept. Releasing DI1 before the 10 s hold aborts the reset with no
changes. This runs from `CoreServices::begin()`, strictly **after** the SAFETY relay-all-OFF
statements in `setup()`, so relays stay OFF for the whole countdown.

**Board-check item:** `INPUT_ACTIVE_LOW` in `common/lib/BoardConfig/src/BoardConfig.h` (default
`true`, marked `TODO(board-check)`) assumes the KC868-A6 opto inputs pull the PCF8574 pin low when
closed — add this to the bring-up checklist below and confirm DI1's polarity on real hardware before
relying on the factory-reset gate.

### Watchdog

The IDF task watchdog is reconfigured to a **15 s** timeout (`Watchdog::TIMEOUT_S`) and subscribes
the loop task; `CoreServices::tick()` feeds it every ~1 s pass (and `FactoryResetInput` feeds it
every 20 ms poll during the DI1 countdown). A watchdog reboot is captured as `ResetCause` via
`esp_reset_reason()` and logged as a `Reboot` event on the next boot.

## Hardware services (stage 03)

Both controllers share a second pair of libraries, split the same way as the core (by hardware
dependency), that owns relays, the K1 motor-valve driver, DS18B20 sensors and the anti-seize
scheduler:

- **`common/lib/HwEngine`** — pure C++ (`platforms: *`, `frameworks: *`, native-testable). It holds
  `RelayBank` (per-channel requested/actual state, the min ON/OFF lock, the safety bypass), `K1Driver`
  (the power + direction pulse state machine), the DS18B20 codec/debounce/mapping (`SensorService`),
  `AntiSeizeScheduler`, and `HwRuntime`, the pure orchestrator that ties them together behind two
  small hardware interfaces, `RelayPort` and `OneWireBus`.
- **`common/lib/HwEsp32`** — ESP32-only adapters (`frameworks: arduino`, `platforms: espressif32`):
  `Pcf8574RelayPort` (writes the relay byte over `TwoWire`), `DallasOneWireBus` (DS18B20 bus scan/
  conversion/scratchpad read over `OneWire`/`DallasTemperature`), and the `HardwareServices` facade
  both `main.cpp` files construct next to `CoreServices` and drive from `loop()`.

### Relays: slots, lock and bypass

Each relay channel has three request slots, arbitrated as `safety ?: exercise ?: control`:

- **control** — the controller's demand (default OFF).
- **exercise** — anti-seize only; never overrides a controller ON demand (a pump exercise only ever
  requests ON).
- **safety** — bypasses the minimum ON/OFF lock entirely; used for fail-safes.

Pump/K2 channels are `lockable`: once actual state changes, the channel delays (does not drop) new
requests for `relayLock` seconds (0–600 s, default 60, setting `relayLock`) unless the request comes
from the safety slot. A delayed request is applied when the lock expires if it is still requested
(the final requested state wins; a request withdrawn during the delay switches nothing), and
`RelayLockDelay` is logged once per delay episode. Boot counts as an OFF transition, so a pump cannot switch ON in under `relayLock`
seconds after a (e.g. watchdog) reboot. K1's two channels are never lockable — `K1Driver` owns their
timing itself.

**Event-log exception for K1:** every actual pump/K2 relay change is logged as `RelayChanged` (with
its reason), but K1's power (R2) and direction (R3) relays are deliberately **not** logged as
`RelayChanged`. K1 is driven with short feedback pulses (typically every ~30 s), which would flush
the 50-entry event log within minutes and add needless NVS flash writes. K1 anti-seize strokes are
still visible through `AntiSeizeRun`, and stage 08 (home-heating K1 control) provides the
replacement visibility: last-pulse and daily pulse counters.

### K1 motor-valve driver

`K1Driver` runs a power (R2) + direction (R3, energised = OPEN) pulse state machine with a 1 s dead
time around every direction change (power OFF → ≥1 s → direction change → ≥1 s → power ON) and never
changes direction while power is ON. A new pulse replaces the current/pending one: same direction
while running extends the end time without a power cycle; opposite direction stops power first, then
runs the reversal sequence. Timing resolution is the ~100 ms fast sub-tick (`HW_FAST_TICK_MS`) that
`loop()` runs between the ~1 s core ticks, so pulses are accurate to roughly +0/+100 ms (up to ~300 ms
on a tick that also reads DS18B20 scratchpads or runs a bus scan).

### DS18B20 sensors

`SensorService` runs a non-blocking 2 s bus cycle (request conversion, wait ~800 ms, read every
device), decodes scratchpads (CRC8 + range check), and debounces each mapped ("logical") sensor
3 good/3 bad samples before flipping between `Fault`/`Ok`. A decoded exact 85.0 °C reading is treated
as the DS18B20 power-on sentinel (bad) unless it plausibly continues the last good reading (D14) — it
is not a general jump/frozen-value check. An assigned sensor is flagged `missing` when either (a) it
was absent from the latest bus scan and has not produced a good reading since, or (b) it is in
`Fault` and its last bad reading was `NoResponse` (the device did not answer, e.g. unplugged or a
shorted bus). A sensor in `Fault` because of CRC errors, out-of-range values or the 85.0 °C power-on
value is `Fault` but **not** `missing`. `missing` raises a common hardware alarm
(`AlarmStatus.activeMask` bits 24–31, one bit per logical sensor index — controller alarm tables use
bits 0–23). Bus scans keep only valid DS18B20 ROMs (family code 0x28 + CRC8); other or corrupted
ROMs are dropped and neither take one of the 12 device slots nor trigger the overflow warning.

### Sensor mapping, settings and backup

Each logical sensor ("T1".."T6" on boiler-room, "H1".."H4" on home-heating) maps to a 16-hex-char
Text setting holding its DS18B20 ROM address (`""` = unassigned). These, plus the shared relay-lock
and anti-seize settings (`relayLock`, `asInterval`, `asTime`, `asDuration` — one `HW_SETTINGS` table
used by both projects) and each project's per-output anti-seize enable flags, are ordinary
`ConfigEngine` settings: they persist to NVS, round-trip through JSON backup export/import, and reset
on a factory reset exactly like every other setting, with no dedicated persistence code. Sensor
assignment can also be changed directly with the `AssignSensor`/`ClearSensor`/`RescanOneWire`
commands (validated: an address maps to at most one logical sensor, and only a valid DS18B20 ROM —
family code + CRC8 — is accepted).

### Anti-seize

Configured pump/diverter/valve outputs run a periodic exercise once every `asInterval` days, starting
at `asTime` (minutes after local midnight, needs valid time-of-day), for `asDuration` seconds (pumps/
K2) or one open+close stroke (K1). A run is blocked or aborted if the safety slot is active on its
channel, the output is disabled/inhibited, or (for K1) a controller pulse is already moving the
valve — a controller pulse counts as "moved" for the anti-seize timer either way, so it never fights
a controller in normal use. If the K1 stroke duration becomes invalid mid-stroke, the close leg is
not started: the run is aborted (not counted as a completed stroke) and a `DiagnosticWarning` (code
`DIAG_CODE_ANTISEIZE_K1_STROKE`) is logged.

**Known limitation:** anti-seize "last run" timestamps are monotonic-time only (not persisted to
NVS), so they reset to "just ran" on every reboot. A device that reboots more often than `asInterval`
days will exercise less often than configured; this is deliberate for stage 03 (see the plan's
Decision Log D19) and may be revisited later if it matters in practice.

### Main loop (D23)

`loop()` runs every `HW_FAST_TICK_MS` (100 ms): each pass calls `hw.fastTick()` (K1 pulse timing +
the relay output write), and once ≥ `CORE_LOOP_PERIOD_MS` (1 s) has passed since the last slow pass it
first calls `core.tick()` (drains commands, feeds the 15 s watchdog) then `hw.tick()` (sensor bus
cycle, anti-seize scheduling). `setup()` keeps `Wire.begin` + `RelayBoot::forceAllOff` as its first two
statements, unchanged from stage 02; `hw.begin(Wire)` runs after `core.begin(Wire)` because
`HwRuntime::begin()` reads settings that `core.begin()` has just loaded from NVS. The relay port
writes the all-OFF byte on every `fastTick()` until `hw.begin()` has completed successfully, so the
relays stay in the SAFETY state through any startup delay or hardware-init failure.

## Connectivity (stage 04)

Wi-Fi, the setup access point, mDNS, MQTT with Home Assistant discovery, web OTA, espota and OTA
rollback. Split the same way as the core and the hardware services:

- **`common/lib/NetEngine`** is pure C++ and native-tested. It holds every decision:
  - the Wi-Fi supervisor, the MQTT session and backoff;
  - the HA entity registry and the discovery/state/event payload builders;
  - the inbound command parser, the throttled publisher;
  - the OTA session guard, boot classification and health check;
  - the version/JSON builders;
  - `ConnectivityRuntime`, the orchestrator behind the `WifiPort`, `MqttTransport`, `OtaPlatform`
    and `KvStore` interfaces.
- **`common/lib/NetEsp32`** is ESP32-only glue: `EspWifiPort` (WiFi + ESPmDNS),
  `AsyncMqttTransport`, `EspOtaPlatform` (esp_ota_* + the strong `verifyRollbackLater()`),
  `EspotaService` (ArduinoOTA), `WebServerHost` (the `AsyncWebServer`, admin auth, ElegantOTA,
  the setup/Wi-Fi/version endpoints) and the **`ConnectivityServices`** facade that both `main.cpp`
  files construct next to `CoreServices`/`HardwareServices`.

**Purity rule.** `NetEngine`, `CoreEngine` and `HwEngine` must never include Arduino, WiFi, `esp_*`,
FreeRTOS, AsyncMqttClient, ElegantOTA, ESPAsyncWebServer, NVS or LittleFS headers. This check must
print nothing (comment lines aside):

```sh
grep -rEn "Arduino\.h|WiFi|esp_|freertos|FreeRTOS|AsyncMqtt|ElegantOTA|ESPAsync|nvs\.h|LittleFS" \
  common/lib/NetEngine common/lib/CoreEngine common/lib/HwEngine
```

**Control never depends on the network.** Wi-Fi, MQTT and HA only post commands to the core's
command queue and read `CommonState`. No network call blocks the loop, except espota's own upload,
which feeds the watchdog while it runs. A router or broker outage leaves heating control untouched.

### Main loop with connectivity

`setup()` still starts with `Wire.begin` + `RelayBoot::forceAllOff` as its first two statements. Then
it calls `core.begin(Wire); hw.begin(Wire); net.begin();`, so connectivity starts after the core and
hardware are running. In `loop()`:

- The ~1 s slow pass runs `core.tick()` (drains commands, feeds the 15 s watchdog), then `hw.tick()`,
  then `net.tick()`.
- Every 100 ms pass runs `net.fastTick()` and then `hw.fastTick()`. The network side drains OTA
  start/end signals and applies the relay inhibit before the relay port is written.

### Identities (never plain `boiler` / `home`)

| | boiler-room | home-heating |
|---|---|---|
| MQTT prefix, hostname, discovery node id | `boiler-room` | `home-heating` |
| mDNS name | `boiler-room.local` | `home-heating.local` |
| HA `unique_id` prefix / device identifier | `boiler_room` | `home_heating` |
| HA device name / model | `Boiler room` / `KC868-A6 boiler-room` | `Home heating` / `KC868-A6 home-heating` |
| Setup AP SSID (open) | `BoilerRoom-Setup` | `HomeHeating-Setup` |

They are declared in `boiler-room/src/BoilerRoomNet.h` and `home-heating/src/HomeHeatingNet.h`.

### Wi-Fi and the setup access point

- **The setup AP starts only when no Wi-Fi SSID is saved.** That happens at first boot, after a
  factory reset, or when the SSID is cleared at runtime. The AP is open, runs at `192.168.4.1` and
  serves `http://192.168.4.1/setup`. The page asks for the **admin web login**, which is
  `admin`/`admin` after a factory reset. It can scan for networks and save an SSID and password.
- After a save, the AP stays up until the station joins, plus 10 s so the page can show the new IP.
  It stays up at most 120 s after the save. After that the device is station-only.
- **Reconnect.** Link loss triggers an immediate attempt. Retries then run every 10 s, growing by 5 s
  per failure up to 30 s, forever. Outages longer than 30 s are logged (`WifiDisconnected`,
  `WifiConnected`).
- **A saved but unreachable network never brings the AP back.** A router outage therefore never opens
  an access point. The consequence: **if the Wi-Fi password was typed wrong, the device will not
  return to the setup AP.** It retries the wrong credentials forever. The fix is a **factory reset**
  (DI1 procedure above). It erases the settings, including Wi-Fi, so the setup AP comes back on the
  next boot. You can also re-submit the setup page during the 120 s join window while the AP is still
  up, or use the Wi-Fi page if the device is reachable on another network.
- The password must be empty (open network), 8–63 characters, or 64 hex characters. SSID and password
  are applied together in one loop tick, so only one reconnect follows.

### Admin login on every endpoint

Every stage-04 HTTP endpoint requires the admin web login (`webUser`/`webPass` settings, HTTP digest
auth). This includes setup-AP mode.

| Route | Purpose |
|---|---|
| `GET /setup` | setup page (scan + manual SSID + password) |
| `GET /api/wifi/status` | mode, SSID, IP, RSSI, setup AP state (no passwords) |
| `GET /api/wifi/scan`, `POST /api/wifi/scan` | cached scan result / request a scan (202) |
| `POST /api/wifi` | form `ssid`, `pass`: 200, 400 if invalid, 503 if a change is still pending |
| `GET /api/version` | firmware + web image version, mismatch flag |
| `GET /` | 302 to `/setup` only while the setup AP is active, else 404 (stage 05 serves the SPA here) |
| `/update`, `/update/...`, `/ota/*` | ElegantOTA, guarded before the upload body is accepted |

A credential change takes effect within ~1 s for new requests and for espota.

### MQTT and Home Assistant

MQTT is disabled while `mqttHost` is empty. The default port is 1883. Reconnect backoff is
5 → 10 → 20 → 40 → 60 s (cap), reset on connect. Only transitions are logged.

| Topic | Content |
|---|---|
| `<prefix>/status` | `online` (retained on connect), LWT `offline` (retained, qos 1) |
| `<prefix>/<key>/state` | entity state (retained), re-published every 60 s and on change |
| `<prefix>/<key>/set` | commands (one subscription `<prefix>/+/set`) |
| `<prefix>/event` | event log entries as JSON (not retained, only while connected) |
| `homeassistant/<component>/<prefix>/<key>/config` | discovery (retained) |

Entity rules (generated from the settings schema and the HW descriptor):

- Keys are snake_case: `relay_lock`, `temp_t1`, `relay_p1`, `relay_k1_power`.
- Int/Float settings become a `number` (config category, min/max/step/unit from the descriptor).
- Bool settings with the HA-switch flag (`homeNoNeed`, `heatingEnabled`) become a dashboard
  `switch`.
- **Deviation from the architecture:** a plain Bool setting (e.g. `asEnP1`) becomes a config-category
  `switch`, not a 0..1 number.
- Text, secret, no-backup and no-HA settings are never exposed: Wi-Fi, MQTT host/port/user/pass,
  web login, time zone and NTP.
- Logical sensors become a temperature `sensor` (`None` while faulty). Relays become a
  `binary_sensor` (`running` for pumps). Every project also gets an `rssi` diagnostic sensor.
- Incoming `/set` payloads are parsed (numbers or `ON/OFF/1/0/true/false`) and posted to the command
  queue. The config engine clamps them. The real value is always echoed back. A full queue drops the
  command and raises a diagnostic warning.
- **No-need gating:** `haGatedFlag(persisted, state.network)` is true only while the effective MQTT
  link is up (Wi-Fi up AND the broker connected). The persisted switch is untouched. Stage 07
  controllers read the gated value, so a lost HA link never leaves "home: no need" in force.

### OTA

- **Web OTA:** ElegantOTA at `http://<prefix>.local/update` (admin digest auth). It accepts a
  firmware image (`.pio/build/release/firmware.bin`) or a LittleFS image (`littlefs.bin` from
  `-t buildfs`).
- **espota** (port 3232). **The espota password is the web password** (`webPass`). It starts on the
  first Wi-Fi link-up, is recreated when the web password changes, and is disabled while that
  password is empty. Add this to your local `platformio.ini` override, and **do not commit it**:

  ```ini
  upload_protocol = espota
  upload_port     = boiler-room.local   ; or home-heating.local
  upload_flags    = --auth=<web password>
  ```

- **Relays are OFF during any update.** On OTA start all relays switch OFF above the safety slot and
  K1 is cancelled; sensors keep running. On failure, or 60 s without progress, control resumes. The
  relay lock counts from the OTA switch-off, so pumps may return up to `relayLock` later. Web and
  espota sessions exclude each other.
- **Rollback and health check.** A new firmware image boots in trial mode (`verifyRollbackLater()`
  returns true). It is confirmed after at least 60 s uptime, 50 slow ticks and 10 completed sensor
  read cycles. A cycle counts even with zero or faulty sensors, so missing sensors never cause a
  rollback. On confirmation `OtaUpdate(3)` is logged. If the image is not healthy within 300 s, it
  logs `OtaUpdate(4)` plus a diagnostic warning, forces relays OFF and rolls back. A crash or reset
  before confirmation is rolled back by the bootloader, and the next boot logs `OtaRollback`.
- **Bootloader requirement:** rollback needs the rollback-enabled bootloader. PlatformIO's USB upload
  flashes it; OTA never updates the bootloader. **Flash each board over USB at least once** with this
  build.
- The partition table stays `min_spiffs.csv`.

### Version and web-image mismatch

`web_assemble.py` writes a plain `data/version.txt` (the FW version plus a newline) next to
`version.json.gz`. At boot the firmware mounts LittleFS without formatting, reads it, and unmounts.
`GET /api/version` reports `project`, `fw`, `web`, `mismatch` and the OTA state (`inProgress`, `pendingVerify`, `bootOutcome`). The mismatch flag is set when a web version
exists and differs from the firmware's. A missing file gives `web = ""` and no mismatch.

### Stage-05 extension points

- `ConnectivityServices::server()`: the same `AsyncWebServer` on port 80, for more routes and
  middleware.
- `ConnectivityServices::adminAuth()`: the same admin credential check.

`GET /` currently answers 404 outside setup-AP mode until stage 05 serves the SPA there.

### Connectivity bring-up checklist (hardware only, NOT TESTED)

Native tests cover every decision (the fakes drive AP → join → MQTT → discovery → OTA → reboot
request). The items below need a real board, network, broker and Home Assistant. They are **NOT
TESTED** until done on hardware.

- **K1-free items (1–7):** these need no K1 motor and no sensors, so they can be done on a bare
  board. Item 5 on home-heating also checks that K1 is idle during an update.
- **Wiring-dependent item (8)** needs connected pumps.

1. **Setup AP.** Factory-reset, then boot. `BoilerRoom-Setup` / `HomeHeating-Setup` appears (open).
   Connect and open `http://192.168.4.1/` (302 to `/setup` after the admin login `admin`/`admin`).
   Scan, pick the network, save. The page shows the new IP, and the AP disappears within ~10 s of the
   join (at most 120 s).
2. **Wrong password.** Save a deliberately wrong password. The AP goes away after 120 s and never comes
   back, and the device keeps retrying. Recover with the DI1 factory reset, and confirm the AP returns.
3. **Reconnect.** Power-cycle the router. Heating control keeps running. A `WifiDisconnected` event
   appears after 30 s, and the device rejoins by itself within ≤ 30 s of the router being back.
4. **mDNS, MQTT and HA discovery.** Check that `ping boiler-room.local` / `home-heating.local`
   resolves. Set `mqttHost`. The device appears in HA as "Boiler room" / "Home heating" with the
   expected entities, and no Wi-Fi/MQTT/password entity. `<prefix>/status` shows `online`, and
   `offline` after pulling power. Toggle `home_no_need` / `heating_enabled` from HA: the value
   persists and is echoed back. Send `abc` to a number's `/set`: it is echoed back unchanged.
5. **Web OTA and auth.** `/update` without credentials gets a 401. An authenticated upload of a new
   `firmware.bin` switches all relays OFF during the upload, the device reboots, and
   `/api/version` shows the new FW. Upload a `littlefs.bin`: `mismatch` becomes false when the
   versions match.
6. **espota.** `pio run -t upload` with the local espota override and `--auth=<web password>`
   succeeds; a wrong password is refused. Change the web password, and the old one is refused on the
   next upload.
7. **Rollback.** Flash (via OTA) a test build that never becomes healthy, for example one that calls
   `abort()` 20 s after boot. The board must boot back into the previous image and log `OtaRollback`.
   Also check that a normal OTA logs `OtaUpdate(3)` about 60 s after the reboot. This needs the
   USB-flashed bootloader.
8. **Relays during OTA (with pumps wired).** Start an OTA while a pump runs. It stops at the upload
   start. On an aborted upload (disconnect mid-way), control resumes after ≤ 60 s plus the relay lock.
   After that aborted upload, a new web upload (and an espota upload) works without a reboot. An
   empty `/ota/upload` after `/ota/start` logs `OtaUpdate(2)` and does not reboot.

## Web UI (stage 05)

Stage 05 adds the admin web UI (a small single-page app) to both controllers. It **replaces the
stage-04 digest login**: every route in "Admin login on every endpoint" above is now protected by the
session login described here, and `GET /` serves the SPA.

### URLs

| URL | What |
|---|---|
| `http://boiler-room.local/`, `http://home-heating.local/` (or the IP) | the SPA (from LittleFS) |
| `http://192.168.4.1/` while the setup AP (`BoilerRoom-Setup` / `HomeHeating-Setup`) is up | the SPA, which opens `#setup` after login; without a web image, the rescue page |
| `/setup`, `/update` | the built-in **rescue page** (PROGMEM, always present) |

SPA pages: `#status`, `#settings`, `#sensors`, `#log`, `#network`, `#system` and `#setup`. The
language (English / Ukrainian) switch is in the header and is remembered by the browser.

**Live header and polling.** The header (controller name, time, Wi-Fi/MQTT, alarm banner and the
amber warning chips, including the firmware/web version-mismatch chip) is shown on every page, so
the SPA polls `GET /api/state` every 2 s on **every** page, not only on Status (a deliberate
choice: alarms stay visible wherever you are). Polling stops while the browser tab is hidden and
resumes when it is shown again. Page-specific polls (the Sensors table, the Network page's Wi-Fi
status) run only while that page is open; the log and settings load when their page opens.

### Login, session, CSRF and the OTA grant

- **Login:** `POST /api/login` (`user`, `pass`) with the `webUser`/`webPass` settings (default
  `admin`/`admin`: change it). A success sets the cookie `hsid` (`HttpOnly; SameSite=Strict;
  Max-Age=86400`) and returns a CSRF token.
- **Sessions:** at most 4 are kept in RAM, and the oldest is dropped when a 5th login happens. Each
  one lasts a **fixed 24 h** from login (not extended by activity, and immune to NTP clock jumps). A
  reboot ends all sessions.
- **CSRF:** every POST, and every `/ota/*` request, must send `X-CSRF-Token`. A page reload gets the
  token back from `GET /api/session`. A bad token gives 403 `csrf`, and the SPA refreshes it once.
- **Changing the login or password** (System → Web access, a backup import that changes them, or a
  factory reset) **ends every session**, including your own; sign in again with the new values.
- **Brute-force throttle:** 5 consecutive failures (login or OTA password) lock both for 30 s
  (429 with `retryS`). The throttle is **global**, not per client.
- **OTA grant:** a web update needs the password again. `POST /api/ota/grant` (`pass`) gives the
  session a 120 s grant. `GET /ota/start?mode=fr|fs` and `POST /ota/upload` need session + CSRF +
  grant, checked before any body byte is written to flash. An espota session gives 409. Logout
  clears the grant.
- **Logout:** `POST /api/logout` ends the session on the controller and clears the cookie.

### System page

- **Clock:** local time (or "time not set"), time source, RTC state and last NTP sync. The time zone
  and NTP server are edited below it.
- **Device:** uptime and the last restart reason (from the newest `reboot` log event; "unknown" when
  the log no longer holds it).
- **Versions:** firmware and web versions from `GET /api/version`; a warning when they differ, when
  the web version is missing, or when the last update was rolled back.
- **Web access:** the `webUser`/`webPass` fields. A blank password field means "unchanged". Change
  one of them per save (a save that changes both is refused): save the password, sign in with it,
  then change the login. The sign-in page then states which credential is new and which is
  unchanged.
- **Update:** choose *Firmware* (`.pio/build/release/firmware.bin`) or *Filesystem image*
  (`.pio/build/release/littlefs.bin` from `-t buildfs`), enter the password again, and watch the
  progress bar. **All relays switch OFF during the update.** After success the page waits for the
  controller to come back and reloads (sign in again: sessions do not survive the reboot).
- **Backup:** *Download backup* warns that **the file contains passwords in plain text** and saves
  `<project>-backup-YYYYMMDD.json`. *Restore from file* refuses files over 8192 bytes before
  uploading, and the controller also refuses a backup of the other project ("file is from …").
- **Factory reset:** type `RESET` to confirm. The controller erases all settings, restarts and opens
  its setup AP (`BoilerRoom-Setup` / `HomeHeating-Setup`, `http://192.168.4.1/`).

### API routes

Access: **P** public, **R** session, **W** session + CSRF, **O** session + CSRF + OTA grant.
Write routes answer 202 `{"ok":true,"id":N}`; poll `GET /api/cmd?id=N` for the result.

| Route | Access | Purpose |
|---|---|---|
| `POST /api/login` | P | `user`, `pass`: 200 `{csrf}`, 401, 429 |
| `POST /api/logout` | W | end the session |
| `GET /api/session` | R | current CSRF token |
| `GET /api/version` | P | `project`, `fw`, `web`, `mismatch`, OTA state |
| `GET /api/state`, `/api/sensors`, `/api/config`, `/api/schema`, `/api/log` | R | snapshots (secrets never included) |
| `GET /api/cmd?id=N` | R | command result |
| `POST /api/config` | W | one `key`, `value` per request (Wi-Fi keys refused) |
| `POST /api/sensors/assign`, `/clear`, `/rescan` | W | sensor mapping |
| `GET /api/wifi/status`, `GET /api/wifi/scan` | R | Wi-Fi state, last scan |
| `POST /api/wifi/scan`, `POST /api/wifi` | W | start a scan; save `ssid` + `pass` together |
| `POST /api/backup/export`, then `GET /api/backup/export` | W / R | 202 while pending, then the file once |
| `POST /api/backup/import` | W | raw `application/json` body, at most 8192 B (413 above) |
| `POST /api/factory-reset` | W | form `confirm=RESET` |
| `POST /api/ota/grant` | W | `pass`: 200 `{ttlS}`, 401, 429 |
| `POST /api/project/cmd` | W | form `op` (1–255): project command (home-heating step test, stage 09) |
| `GET /ota/start?mode=fr\|fs`, `POST /ota/upload` | O | ElegantOTA backend (409 during espota) |
| `GET /setup`, `GET /update` | P | rescue page |

Form bodies above 1024 B get 413 before they are parsed.

### Web image: `uploadfs` and the size budget

```sh
pio run -d boiler-room -e release -t uploadfs     # or -t buildfs, then upload littlefs.bin from System
pio run -d home-heating -e release -t uploadfs
```

`web_assemble.py` gzips `common/web` + `<project>/web`, checks that `lang/en.json` and `lang/uk.json`
have the same keys, and fails the build if the gzipped total exceeds 64 KiB or the 4 KiB-block total
exceeds 100 KiB (of the 128 KiB LittleFS partition). At the end of stage 05 the gzipped total is
about 42 KB gzipped (42,353 B for boiler-room, 42,363 B for home-heating) and 68 KiB
(69,632 B) of 4 KiB blocks for each project. Upload the filesystem image together with the
firmware of the same version, or the UI shows the version-mismatch warning.

### Rescue page and captive portal

- The **rescue page** is built into the firmware (no LittleFS needed). It offers login, Wi-Fi setup
  (scan, manual SSID, save) and firmware/filesystem upload with password re-entry. It is served at
  `/setup`, `/update`, at `/` when the web image is missing, and at `/` during a filesystem update.
- **Captive portal:** while the setup AP is active, a DNS server answers every name with
  `192.168.4.1`, and unknown URLs redirect there, so phones open the login page on their own. It
  never runs in normal (station) mode.

### Known limitations

- HTTP only (no TLS). Use it on a trusted network.
- The SPA sends no MD5 digest with web OTA uploads (the ESP-IDF image check still validates
  firmware images).
- The MQTT password cannot be cleared to empty from the UI (a blank secret means "unchanged").
- Sessions have a fixed 24 h lifetime and are lost on reboot; the login throttle is global, so a
  stranger's failed attempts can delay your login by 30 s.
- ESPAsyncWebServer buffers request **headers** without a size limit before any route or guard
  runs. Only bodies are capped (1024 B for forms, 8192 B for backups). A client on the LAN could
  exhaust the heap with huge headers and crash or restart the controller.
- The login and the password are changed one at a time: the System page refuses a save that
  changes both. Save the password first, sign in with it, then change the login. After each save the
  sign-in page says which credential is new (the one just typed) and that the other is unchanged.
- Only one admin account; there are no roles or API tokens.

### Web UI bring-up checklist (hardware only, NOT TESTED)

These steps need real boards; none of them has been run yet.

- [ ] `uploadfs` both projects; `http://<project>.local/` shows the login page; no mismatch warning.
- [ ] Login with the default password, change it on System → Web access; every open tab returns to
      the login page; the new password works; espota uses the new password.
- [ ] 5 wrong passwords give "try again in 30 s"; a correct one works after 30 s.
- [ ] Status, Log and Sensors refresh live; assign, move and clear a DS18B20 from Sensors.
- [ ] Settings: change a number out of range and see "clamped to X"; the value persists over a reboot.
- [ ] Network: scan, pick a network, save; the controller reconnects; MQTT status is shown.
- [ ] Setup AP: with no Wi-Fi, the phone opens the captive page at `192.168.4.1`; `#setup` saves Wi-Fi.
- [ ] Web OTA firmware: wrong password refused; progress bar; relays OFF during the upload; the page
      reloads after the reboot; the trial boot is confirmed after about 60 s.
- [ ] Web OTA filesystem: `/` shows the rescue page during the upload; the SPA returns afterwards.
- [ ] Start espota, then a web OTA: the web upload gets "another update in progress" (409).
- [ ] Backup: download (file name with the date, plain-text warning shown), import it back, import a
      file of the other project (refused, "belongs to another controller"), and a 9 KB file (refused
      in the browser).
- [ ] Factory reset with `RESET`: the controller restarts into `BoilerRoom-Setup` / `HomeHeating-Setup`.
- [ ] Remove the LittleFS image (flash an empty one): `/` serves the rescue page and can upload the image.
- [ ] Headers of several KB from a LAN client: the controller survives or recovers by watchdog.

## OLED display (stage 06)

Both controllers drive the on-board SSD1306 128×64 OLED (I²C `0x3C`) as a read-only view of
`CommonState`. The display never writes AppState and never makes a control decision. The code is
split the usual way:

- `common/lib/DisplayEngine` is pure and native-tested. It holds the frame model and fonts, text
  fitting and formatting, the pixel shift, the dirty tile-row scheduler, the screen state machine,
  the page registry, the shared pages and the special screens.
- `common/lib/DisplayEsp32` holds `Ssd1306Panel` (a U8g2 full-buffer driver with a custom I²C byte
  callback) and the `DisplayServices` facade that both `main.cpp` files construct.

### Pages, rotation and pixel shift

- **Rotation order:** controller pages (stages 07/08, in registration order), then **Network**
  (Wi-Fi state, SSID, RSSI, IP, MQTT), then **Sensors** (status of each logical sensor), then
  **Alarms**. Alarms is in the rotation only while an alarm is active; it lists 5 labels per
  sub-page, and sub-pages change every 2.5 s.
- **Dwell:** each page stays for `dispRotateS` seconds. The Alarms page stays long enough to show
  all its sub-pages. A changed period applies from the next page change.
- **Burn-in pixel shift:** the whole picture moves by 1 px on an 8-step ring and never more than
  2 px. It moves once per rotation period, on its own timer, so it also moves with a single page,
  during an alarm hold and on the setup/OTA screens.
- **Fonts:** fixed-width `5x8`, `6x10` and `10x20` (ASCII only). Characters that are not ASCII are
  shown as `?`, and text that is too long is cut with `~`.

### Alarm jump and special screens

- **Alarm jump:** a **new** alarm bit (including alarms already active at boot) jumps to the Alarms
  page and holds it for 30 s. Another new bit restarts the 30 s. Clearing bits never jumps. The hold
  ends after 30 s or when no alarm is left, and rotation then resumes at page 0.
- **Priority:** reset result (first 3 s after boot) > OTA ("UPDATING") > setup mode > alarm hold >
  rotation.
- **Setup mode** (no Wi-Fi saved) shows `SETUP MODE`, the AP name, `(open, no password)` and
  `Open: 192.168.4.1`. When an alarm is active, every
  3rd slot shows the Alarms page instead.
- **DI1 factory reset:** while DI1 is held at boot, the panel shows the countdown (seconds left and a
  progress bar). "Reset confirmed" or "Reset aborted" then stays for 3 s after the loop starts.
- **Boot splash:** project name, firmware version and "starting..." from the first moments of
  `setup()`.

### Settings (group "display", HA-exposed)

| Key | Range | Default | Effect |
|-----|-------|---------|--------|
| `dispRotateS` | 2–60 s | 5 | page rotation period and pixel-shift step |
| `dispBright` | 1–100 % | 30 | SSD1306 contrast 3–255, applied live; the panel is never switched off |

Both settings were appended as the last settings table, so no existing setting index or NVS key
moved, and the config version stays 1.

### Non-blocking design

- **Loop task only.** All display I²C runs on the loop task, like every other `Wire` user (relay
  PCF8574, DI1 PCF8574, DS1307). There is no display task and no mutex.
- **Last in the pass.** `display.fastTick(millis() - start)` is the **last** call of every 100 ms
  pass, after `hw.fastTick()`, so relay and K1 writes in that pass are never delayed.
- **Row budget.** The frame is diffed against a 1 KB shadow in 128-byte tile rows. Each pass sends at
  most 2 changed rows (about 30–32 ms). It sends only 1 row once 40 ms of the pass has been spent, and
  none once 80 ms has. A full repaint takes ≤ 400 ms; a live-value update usually takes 1–2 rows.
- **100 kHz bus.** The stock U8g2 I²C driver calls `Wire.setClock(400000)` on every transfer and
  `Wire.begin()` on init, and never restores the clock. That would move the standard-mode PCF8574s
  and DS1307 onto a fast-mode bus. `Ssd1306Panel` uses its own byte callback, which never calls
  `setClock` or `begin`, so the whole bus stays at the 100 kHz `Wire.begin` default. The firmware
  sources contain no `setClock` call.
- **Boot order.** `setup()` still starts with `Wire.begin` + `RelayBoot::forceAllOff`. Then come
  `display.beginEarly(Wire)` (probe, splash, reset-countdown hook), `core.begin(Wire)`,
  `hw.begin(Wire)`, `web.attach()`, `net.begin()` and `display.begin()`.

### Missing or failing display

- **Absent at boot.** `beginEarly` probes `0x3C` with an address-only write (~0.1 ms). With no ACK,
  the display draws nothing, the countdown hook does nothing, and boot and control continue
  unchanged.
- **Lost at runtime.** After 3 consecutive failed row sends, the display is marked unavailable. A
  single glitch while a relay switches does not count.
- **Re-probe.** While unavailable, the panel is re-probed every 30 s. When it answers, it is
  re-initialised and fully repainted.
- **Logging.** One serial line per available/unavailable transition, and **one** `DiagnosticWarning`
  event per boot (source `EVENT_SOURCE_DIAG_BASE + DIAG_CODE_DISPLAY_MISSING` = `0x0506`).

### Page API for stages 07/08

```cpp
void renderT1Page(const CommonState& s, DisplayFrame& f, void* ctx);  // DisplayPageRenderFn
display.addPage({"T1", renderT1Page, &ctx});  // in setup(), BEFORE display.begin()
```

- `addPage` returns false after `begin()` or when the registry (8 pages) is full.
- Renderers are pure. They fill the `DisplayFrame` with `setTitle`/`addBody`/`addText`/`setBar`,
  inside the 126×62 content box.
- To get real alarm labels, pass an `AlarmDescriptor` table **indexed by alarm bit** (entry `i` =
  bit `i`, `i < 24`) to the `DisplayServices` constructor. Bits 24–31 are always shown as
  "Sensor <name> missing".

## Boiler-room controller (stage 07)

The `boiler-room` firmware runs the three pumps: **P1** (boiler → accumulator charging), **P2**
(boiler return protection) and **P3** (supply to the house). The logic lives in the project-local
library `boiler-room/lib/BoilerRoomEngine`. It is pure C++ with no `Arduino.h`, `Wire.h`,
`millis()`, NVS or network calls. Time is injected as monotonic ms, and the whole library, including
the runtime adapter and the views, is native-tested.

- `BoilerLoopLogic` handles the overheat latch, P1 and P2. `SupplyLogic` holds the P3 state machine,
  anti-freeze and the overheat dump. `computeStoredEnergy` gives the stored energy.
  `BoilerRoomController::update()` combines them.
- `BoilerRoomRuntime` is the adapter. It reads `CommonState`, the settings and the gated
  "Home: no need" flag. It maps each pump decision onto the `RelayBank` slots and writes the
  controller status slice (`BoilerRoomStatus`) and alarm bits 0–23. It also self-clears the flag
  and logs events.
- **Control never depends on Wi-Fi, MQTT or HA.** The only network input is the "Home: no need"
  flag, and it only ever reaches the logic through `haGatedFlag()`. While MQTT is down, the flag
  is ignored and P3 runs NORMAL.
- The **loop order** in the ~1 s slow block is `core.tick(); hw.tick(); ctl.tick(); net.tick();
  web.tick();`. The controller decides on this pass's sensor data, and HA and web publish the new
  status in the same pass. Relay requests reach the port at the next `hw.fastTick()`, within
  100 ms. `setup()` still starts with `Wire.begin` + `RelayBoot::forceAllOff`, so all relays are
  OFF at boot.

### Pump rules

T1 is the boiler flow, T2 the boiler return, T3/T4/T5 the accumulator top/middle/bottom, and T6
the supply (diagnostic). All thresholds are settings (see the table below).

| Pump | Normal rule (control slot, lock applies) | Hysteresis / notes |
|------|------------------------------------------|--------------------|
| P1 | ON when `T1 > T3 + p1DeltaOn` **and** `T1 > p1T1Min` | OFF when `T1 < T3 + p1DeltaOff` or `T1 < p1T1Min − p1Hyst` |
| P1 (T3 failed) | ON when `T1 > p1T1Min` (reason `charge_t1`) | OFF when `T1 < p1T1Min − p1Hyst` |
| P2 | ON when `T2 < p2T2Off − p2Hyst` **and** the boiler is burning | OFF when `T2 ≥ p2T2Off` or burning ends |
| P2 (T1 failed) | ON when `T2 < p2T2Off − p2Hyst` (reason `return_t2`) | OFF when `T2 ≥ p2T2Off` |
| P2 (T2 failed) | ON while the boiler is burning (reason `burn_gate`) | — |
| P3 | the NORMAL/OFF/OFFER state machine (below) | — |

- **Burning** is a latch: set at `T1 > p2T1Burn`, cleared at `T1 < p2T1Burn − p2Hyst`, and
  false whenever T1 is not Ok.
- **Overheat** is a latch: set at `T1 > ohOn`, cleared at `T1 < ohClear`. While it is set, **P1 is
  forced ON** and **P3 runs the heat dump**, both on the safety slot. The "Home: no need" flag is
  left untouched.
- **Ordering guard.** Every tick, `p1DeltaOff` is limited to `p1DeltaOn − 0.5` and `ohClear` to
  `ohOn − 1`, so a band can never invert. The web UI shows the stored raw values.
- **Rule continuity.** Rule states keep their hysteresis while a safety state overrides the output.
  After an overheat clears, P1 continues from its real rule state.

### Sensor fail-safes (Failed vs Unknown)

- **Failed** is a sensor in `Fault` **or `Unassigned`**. An unassigned sensor counts as not working.
- **Unknown** (`Pending`, the first seconds after boot or a reassignment) means **hold**: any pump
  whose rule needs that sensor stays OFF on the control slot (reason `wait`) and no alarm is raised.
  A pending T3 makes P3 NORMAL.

| Condition | Result | Slot |
|-----------|--------|------|
| T1 failed | P1 forced ON (`t1_fault`); P3 is **not** forced | safety |
| T1 + T2 failed | P2 forced ON (`t1t2_fault`) | safety |
| T2 failed | P2 follows the burning latch (`burn_gate`) | control |
| T1 failed, T2 Ok | P2 uses the T2-only rule (`return_t2`) | control |
| T3 failed | P1 uses the T1-only rule; P3 stays NORMAL (no offer) | control |
| T1 pending during an overheat | the latch is **held** until T1 is Ok again | — |
| T1 failed during an overheat | the latch clears; P1 stays forced through `t1_fault` and the dump ends | — |
| settings missing at boot (`[ctl] settings missing`) | P1 safety ON, P2/P3 OFF, status not ready | safety |

### Relay lock vs bypass

The **safety** slot bypasses the relay min-ON/min-OFF lock. It is used **only** by `overheat` (P1),
`t1_fault` (P1), `t1t2_fault` (P2), `anti_freeze` (P3) and `dump` (P3). Every other state, including
`return_t2`, goes through the **control** slot and respects the lock. On each tick the control slot
already holds the normal request underneath, so when a safety state ends, the lock governs the
switch back. The exercise slot stays owned by anti-seize.

### P3 supply: NORMAL / OFF / OFFER

The house controller (via HA) sets the "Home: no need" flag (`homeNoNeed`) when it needs no heat.

- **Gates** (checked first, from any mode): MQTT down or T3 not Ok → **NORMAL**. A running offer
  window is abandoned, the flag is not cleared, and an armed wait keeps counting.
- **NORMAL** → **OFF** when the flag is set.
- **OFF** → **NORMAL** when the flag is cleared (the house needs heat again).
- **OFF** → **OFFER** when `T3 ≥ p3T3Offer`, the wait has elapsed and there is no overheat. On
  entry, the controller **clears the flag itself** (once per OFFER entry, `ConfigChanged` with reason
  Logic) and P3 runs.
- **OFFER** → **OFF** when the `p3OfferWin` window ends and the flag is set again. This arms the
  `p3OfferWait` wait. If the flag is still cleared when the window ends → **NORMAL**.
- The wait is armed **only** on OFFER → OFF. `p3OfferWait = 0` means no wait. An armed wait is not
  restarted or cancelled by NORMAL or by an MQTT loss.
- **During the overheat dump** the state machine keeps running and its timers count in wall time.
  Only OFF → OFFER is suppressed while overheat is latched. After the clear, P3 returns to the
  state-machine output (OFF with the flag still set), and the next tick may enter OFFER.
- After a reboot, MQTT is not connected yet, so P3 starts NORMAL.

> **HA automation note.** The controller clears "Home: no need" when it makes an offer. If the house
> still needs no heat, **the HA automation must set "no need" again** after the controller clears
> it, otherwise P3 stays ON. The offer then ends with the window, and the next offer waits
> `p3OfferWait` minutes.

### Anti-freeze

- When `afEnable` is on and the P3 relay has been **actually OFF** for `afInterval` minutes, P3 runs
  for `afDuration` seconds on the safety slot (an `AntiFreezeRun` event is logged).
- The idle timer is **shared**. It is derived from the P3 relay's last ON time, so any P3 run
  resets it: NORMAL, OFFER, the dump, anti-freeze or anti-seize. After a reboot it counts from
  boot, so the relay reads as "just ran" (accepted).
- Turning `afEnable` off stops a run at once. When a run ends, the control slot holds the
  state-machine output; if the min-ON lock is longer than the run, P3 stays ON until the lock
  expires.
- `afEnable` is a Bool in group `flags` with an HA switch, next to "Home: no need" on the Modes tab.

### Stored energy

`E = V · 1.163 · (avg − accTBase) / 1000` kWh, averaged over the accumulator layers T3/T4/T5 whose
state is Ok, and clamped at ≥ 0. With all three layers it is **exact**. With one or two it is
**estimated** (`~` on the OLED, `energy_estimated` ON in HA). With none it is **n/a**: JSON `null`,
HA unavailable, never 0. A pending layer counts as not working.

### Alarm bits and events

The controller owns alarm bits 0–23 and never touches the stage-03 bits 24–31 ("Sensor <name>
missing"). A sensor condition is alarmed once: bit 24+i when the sensor is missing, otherwise
bit 8+i.

| Bit | Key | Label |
|-----|-----|-------|
| 0 | `overheat` | Boiler overheat |
| 1 | `t1_failsafe` | T1 fail: P1 forced |
| 2 | `t1t2_failsafe` | T1+T2 fail: P2 forced |
| 3 | `t2_failsafe` | T2 fail: P2 burn gate |
| 4 | `t3_failsafe` | T3 fail: no offer |
| 8–13 | `t1_fault` … `t6_fault` | T<n> fault/unassigned (Fault without missing, or Unassigned) |

Events are rate-limited by the existing limiter (60 s per type and source):

- Alarm bit edges are logged as `AlarmRaised` / `AlarmCleared` (source `EVENT_SOURCE_ALARM_BASE +
  bit`).
- `BR_EVENT_P3_MODE` (project type 0) is logged on each P3 mode change; the source differs per
  target mode.
- `BR_EVENT_PUMP_REQUEST` (project type 1, source = relay channel) is logged when a pump's requested
  ON or safety flips.
- `AntiFreezeRun` is logged at each anti-freeze start.
- The flag self-clear is logged by the settings engine as `ConfigChanged` (reason Logic).
- Actual relay switches are still logged by `RelayBank`.

### Settings (appended as the last table; config version stays 1)

| Key | Group | Range | Default | Meaning |
|-----|-------|-------|---------|---------|
| `p1DeltaOn` | p1 | 2–20 °C (0.5) | 5 | P1 ON differential T1 − T3 |
| `p1DeltaOff` | p1 | 0–15 °C (0.5) | 2 | P1 OFF differential (guarded ≤ ON − 0.5) |
| `p1T1Min` | p1 | 40–80 °C | 60 | P1 minimum T1 |
| `p1Hyst` | p1 | 1–10 °C (0.5) | 2 | T1_min hysteresis |
| `ohOn` | p1 | 80–95 °C | 90 | overheat above |
| `ohClear` | p1 | 70–94 °C | 87 | overheat clears below (guarded ≤ ohOn − 1) |
| `p2T2Off` | p2 | 55–65 °C | 60 | P2 OFF at return T2 |
| `p2Hyst` | p2 | 1–5 °C (0.5) | 3 | P2 and burning-latch hysteresis |
| `p2T1Burn` | p2 | 20–60 °C | 40 | boiler counts as burning above T1 |
| `p3T3Offer` | p3 | 40–85 °C | 60 | offer heat at T3 |
| `p3OfferWin` | p3 | 1–60 min | 10 | offer window |
| `p3OfferWait` | p3 | 0–480 min | 60 | wait between offers (0 = none) |
| `afEnable` | flags | Bool, HA switch | on | anti-freeze enable |
| `afInterval` | p3 | 5–240 min | 30 | P3 idle interval before an anti-freeze run |
| `afDuration` | p3 | 10–600 s | 60 | anti-freeze run time |
| `accVolume` | accumulator | 100–2000 L | 500 | accumulator volume |
| `accTBase` | accumulator | 10–60 °C | 30 | energy base temperature |

The existing `homeNoNeed` ("Home: no need", group `flags`, HA switch) is the P3 flag.

### Home Assistant entities

These come on top of the common entities (sensors, relays, settings, switches):

| Key | Type | Notes |
|-----|------|-------|
| `energy` | sensor, kWh, `energy_storage` / `measurement` | unavailable when n/a or not ready |
| `energy_estimated` | binary sensor, diagnostic | ON when estimated; unavailable when n/a |
| `p3_mode` | sensor | `normal` / `off` / `offer` |
| `anti_freeze_run` | binary sensor, `running` | ON during an anti-freeze run |
| `alarm_overheat` | binary sensor, `heat` | bit 0 |
| `alarm_t1_failsafe`, `alarm_t1t2_failsafe`, `alarm_t2_failsafe`, `alarm_t3_failsafe` | binary sensor, `problem` | bits 1–4 |
| `alarm_t1_fault` … `alarm_t6_fault` | binary sensor, `problem`, diagnostic | bits 8–13 |

The status entities are unavailable until the controller has started (`bindBoilerRoomHaStatus` in
`setup()` and the first `ctl.tick()`).

### OLED pages and `/api/state` "ctl"

- **Pages**, in rotation before Network/Sensors/Alarms:
  - **Boiler**: T1, T2, then `P1`/`P2` `ON|OFF <reason>`. ON/OFF is the actual relay, and `!` marks
    a safety (lock-bypass) request.
  - **Accu**: T3, T4, T5 and `E x.x kWh`, `E ~x.x kWh est` or `E n/a`.
  - **Supply**: T6, P3, `Mode NORMAL|OFF|OFFER [<m>m]`, `AF RUN` / `AF in <m>m` / `AF off`, and
    `No-need set|clear|ign` (`ign` = saved but ignored while MQTT is down).
- The Alarms page uses the labels above (bits 0–13).
- `/api/state` carries a `"ctl"` member, added through `WebServices::setStateExtension()`.
  home-heating never sets it. The member is `{"ok":false}` until the controller is ready:

```json
"ctl":{"ok":true,
 "pumps":[{"n":"P1","on":true,"sf":false,"r":"charge","as":false}, … P2, P3],
 "p3":{"mode":"off","af":false,"dump":false,"noOffer":false,"saved":true,"flag":true,"link":true,
       "winS":0,"waitS":1234,"afInS":900,"idleS":300},
 "energy":{"q":"est","kwh":18.6},
 "oh":false,"t6":true}
```

In each `pumps[]` entry:
- `on`/`sf`/`r` are the controller's request (`sf` = safety slot);
- `as` means anti-seize is running;
- the actual relay state stays in the base `relays[]`.

`t6` is true only while T6 is Ok; stage 09 uses it.

### Boiler-room controller checks (hardware only, NOT TESTED)

None of these has been run on a real board yet.

- [ ] **Boot.** At power-on, all relays stay OFF (`RelayBoot` first), then the controller takes over.
      P3 starts NORMAL (MQTT not up yet) once its relay lock allows.
- [ ] **Fresh install.** After a factory reset, T1..T6 are unassigned. P1 is forced ON (T1 failed),
      P2 is forced ON (T1 + T2 failed), and alarms 1, 2, 4 and 8–13 are shown
      (P3 stays NORMAL because T3 failed).
- [ ] **T1 fail-safe.** Unplug the T1 probe. Within about 6 s, P1 switches ON with a `!` on the
      Boiler page, even inside a relay lock. The alarms "Sensor T1 missing" and "T1 fail: P1
      forced" appear. Plug it back in: the controller returns to the normal rule.
- [ ] **Overheat.** Heat T1 above `ohOn` (or lower `ohOn` temporarily). P1 and P3 both run on the
      safety slot (`overheat`, `dump`) and "Boiler overheat" is raised. A set "Home: no need" flag
      stays set. Below `ohClear`, the alarm clears and P3 goes back to OFF.
- [ ] **T2-only fault.** With T1 working, unplug the T2 probe. "T2 fail: P2 burn gate" is raised
      and P2 follows the burn gate (`burn_gate`: ON while T1 > `p2T1Burn`, OFF below it minus the
      hysteresis) on the control slot, so the relay lock is still respected (no `!`).
- [ ] **T3-only fault.** Unplug the T3 probe. "T3 fail: no offer" is raised, P1 runs by T1 only
      (`charge_t1`), P3 stays NORMAL and never enters OFFER, and the energy shows `~` (estimated).
      Overheat protection still works on T1.
- [ ] **OTA during a forced state.** Start an OTA update while P1 is forced (overheat, or T1
      unplugged). During the update all relays stay OFF (the OTA inhibit beats the safety slot).
      After the update, or after its rollback, the forced state resumes on its own (P1 ON with `!`).
- [ ] **No-need flag via HA.** Turn on the "Home: no need" switch in HA: P3 goes OFF. When T3 reaches
      `p3T3Offer`, the controller enters OFFER, P3 runs, and the switch turns itself **off** in HA.
      Turn it on again from HA (or by the automation): at the end of the window P3 goes OFF and the
      `p3OfferWait` wait starts.
- [ ] **MQTT loss.** Stop the broker with the flag set: P3 returns to NORMAL within a tick, and the
      Supply page shows `No-need ign`.
- [ ] **Anti-freeze.** With P3 OFF and `afInterval` set low (e.g. 5 min), P3 runs for `afDuration`
      seconds. `anti_freeze_run` turns ON in HA, and an `AntiFreezeRun` event appears in the log.
- [ ] **Pages and alarm texts.** The Boiler, Accu and Supply pages rotate before Network and
      Sensors. Values update live and no row is clipped. Every raised alarm shows its label on the
      Alarms page.
- [ ] **Energy.** With T3–T5 assigned, `energy` in HA matches the Accu page. Unplug T4: the value
      shows `~` and `energy_estimated` turns ON.
- [ ] **Watchdog.** Let the controller run for several hours with pages rotating and HA connected.
      There are no watchdog resets, and the `debug` loop timing stays well under 100 ms.

## Home-heating controller (stage 08)

The `home-heating` firmware runs the heating pump **P4**, the DHW diverter **K2** (R1 relay: de-energised =
**TANK**, energised = **BYPASS**) and the radiator mixing valve **K1** (3-point motor valve, ~120 s full
travel, no position feedback). It also computes the **"no need"** signal that HA copies to the boiler room.
The logic lives in the project-local library `home-heating/lib/HomeHeatingEngine`. Like stage 07, it is pure
C++ with no `Arduino.h`, `Wire.h`, `millis()`, NVS or network calls. Time is injected as monotonic ms, and the
whole library, including the runtime adapter and the views, is native-tested.

- `P4Logic`, `K2Logic`, `NoNeed`, `K1Math` (position estimate, feed-forward target, move planners) and
  `K1Logic` (the K1 state machine) are small pure units. `HomeHeatingController::update()` combines them in
  the order: classify sensors → fail mode → P4 → K1 → K2 → no-need → alarms.
- `HomeHeatingRuntime` is the adapter. It reads `CommonState` and the settings, drives P4/K2 through the
  `RelayBank` **control slot** and K1 only through `K1Driver::requestPulse(..., K1Owner::Control)`. It
  integrates `K1Driver::takeMotion()`, sets the anti-seize K1 stroke length, writes the status slice
  (`HomeHeatingStatus`), owns alarm bits 0–23 and logs the controller events.
- **Control never depends on Wi-Fi, MQTT or HA.** "No need" is only an output.
- The **loop order** in the ~1 s slow block is `core.tick(); hw.tick(); ctl.tick(); net.tick(); web.tick();`
  (same as stage 07). `setup()` still starts with `Wire.begin` + `RelayBoot::forceAllOff`, and `ctl.begin()`
  runs after `hw.begin(Wire)`.

H1 is the radiator return (K1 port 2), H2 the radiator supply after P4 (the controlled value, `h2Set`), H3 the
hot supply in from the boiler room (K1 port 3), H4 the DHW tank. All thresholds are settings (table below).

### P4 heating pump

First matching rule wins:

| # | Condition | P4 | Reason |
|---|-----------|----|--------|
| 1 | heating disabled (`heatingEnabled` off) | OFF | `heating_off` |
| 2 | H3 failed, fail mode Multi | OFF | `multi_fault_off` |
| 2 | H3 failed, otherwise | **ON** (forced) | `h3_fault` |
| 3 | H3 unknown (pending) | OFF at once | `sensor_wait` |
| 4 | two or more of H1–H3 failed, H3 Ok (D12) | **ON** (forced) | `multi_fault` |
| 5 | `H3 ≥ h2Set` | ON | `demand` |
| 6 | was ON and the last demand is less than `p4OffDelay` min ago | ON | `off_delay` |
| 7 | otherwise | OFF | `supply_cold` |

Every ON rule except `off_delay` counts as demand and restarts the off delay. The delay also counts from boot.

### K1 mixing valve

- **Position estimate.** No feedback exists, so the controller integrates the actual power-ON time per
  direction reported by `K1Driver::takeMotion()` (any owner, including anti-seize strokes and cancelled or
  partial runs), clamped to 0–100 %. It is unknown at boot.
- **Recalibration.** A CLOSE of `k1Travel × (1 + k1Resync/100)` (default 132 s) sets the estimate to 0 %. It runs
  at boot (regardless of heating), when the P4 request falls, and when heating is disabled. Completion is
  measured by the actual CLOSE time, so an OTA cancel or an anti-seize stroke can never fake it; any OPEN
  motion during recal restarts the count. Recal replaces a running control pulse, never an anti-seize stroke.
  K1 control starts only after recal ends.
- **K1 needs P4 actually running** (request ON and relay ON, D10). While the relay lock delays a P4 start, K1
  stays closed.
- **Feed-forward (FF).** `x = (h2Set − H1) / (H3 − H1) × 100 %`, clamped; when `H3 − H1 ≤ k1SmallDiff`
  (including negative) → 100 %. FF is applied as one move when it is first needed (P4 start, mode entry, end
  of an OTA inhibit) and whenever `x` has moved by `k1FfStep` % or more since the last FF (A1). That period
  skips feedback.
- **Feedback.** Every `k1Period` s, with K1 idle: `err = h2Set − H2`; inside `k1Deadband` → no move; else a
  pulse of `min(k1Gain × |err|, k1MaxPulse)` s, OPEN when H2 is too cold, CLOSE when too hot. Feedback trims
  persist between FF moves. A busy K1 defers the evaluation (periods never stack).
- **End-stop resync.** Any move that ends at 0 % or 100 % adds `k1Resync` % of travel. A move toward an end
  the estimate already sits at is skipped (no endless pulses when H2 stays low at 100 %).
- **Minimum pulse.** Moves shorter than `k1MinPulse` s are skipped (FF and feedback alike).
- **OTA.** No K1 commands while the relays are inhibited. The cancelled part of a run counts as real motion.
  After the inhibit FF is re-applied and an unfinished recal resumes.
- **Anti-seize.** The K1 anti-seize stroke length is set to the recal time (`setK1StrokeMs`) whenever the
  setting changes; the estimate follows the stroke.

K1 modes (`k1_mode`): `recal`, `closed` (heating off, or P4 not running), `wait` (a needed sensor is pending:
hold position), `normal` (FF + feedback), `fb_only`, `ff_only`, `failpos_fb`, `failpos_fixed`.

### K2 DHW diverter

TANK (charging the DHW tank) only while all three hysteresis latches are true, otherwise BYPASS:

| Latch | Becomes true | Becomes false |
|-------|--------------|---------------|
| delta | `H3 > H4 + k2Delta` | `H3 ≤ H4 + k2Delta − k2DeltaHyst` |
| H3 min | `H3 ≥ k2H3Min` | `H3 < k2H3Min − k2H3MinHyst` |
| H4 max | `H4 ≤ k2H4Max − k2H4MaxHyst` | `H4 ≥ k2H4Max` |

- Bypass reason priority: `h4_full` → `h3_low` → `delta_low`; TANK reason `charging`.
- H4 failed → BYPASS `h4_fault`; H3 failed → BYPASS `h3_fault` (D13). H3 or H4 pending → K2 **holds** its
  previous request (`sensor_wait`; TANK at boot). The latches re-initialise from the first all-Ok reading.
- K2 is independent of `heatingEnabled`. Guard: `k2DeltaHyst` is limited to `k2Delta − 0.5`.

### "No need" (A2)

"No need" is ON only when **all** of these hold:
- H1–H4 are all known (none pending);
- not (heating enabled and H3 failed) — while H3 is faulted, no-need is never sent;
- K2 requested BYPASS **and** the K2 relay is actually energised;
- P4 requested OFF **and** the P4 relay is actually OFF **and** the P4 off delay has elapsed.

It turns ON only on actual + requested relays and turns OFF as soon as a request changes. During a P4/K2
anti-seize exercise the request stands in for the relay, so a 30 s exercise does not flap the signal. During
OTA all relays drop, so K2 reads TANK and no-need goes OFF (fail toward supplying, accepted).

### Sensor fail-safes (Failed vs Unknown)

- **Failed** = `Fault` **or `Unassigned`** (A4). **Unknown** = `Pending`: P4 OFF `sensor_wait`, K1 `wait`
  (H1/H2) or closed, K2 holds, no-need OFF, no alarm.
- Fail mode from H1–H3: two or more failed → **Multi**; else H3, H2, H1 (one failed) or none.

| Condition | P4 | K1 | K2 |
|-----------|----|----|----|
| H3 failed | forced ON (`h3_fault`) | moves to `k1FailPos` (30 %), then feedback on H2 (`failpos_fb`) | BYPASS `h3_fault` |
| H2 failed | normal rule | FF only (`ff_only`) | normal |
| H1 failed | normal rule | feedback only (`fb_only`) | normal |
| ≥ 2 of H1–H3 failed, H3 Ok | forced ON (`multi_fault`, D12) | fixed at `k1FailPos` (`failpos_fixed`), even with P4 OFF | normal |
| ≥ 2 of H1–H3 failed, H3 failed | OFF (`multi_fault_off`) | fixed at `k1FailPos` | BYPASS `h3_fault` |
| H4 failed | — | — | BYPASS `h4_fault` |
| heating disabled | OFF (`heating_off`) | closed (alarms stay) | K2 rules still apply |
| settings missing at boot (`[ctl] settings missing: outputs held off`) | OFF | no command | TANK (relay OFF) |

After a factory reset all four sensors are unassigned: Multi, P4 OFF, K1 at 30 %, K2 BYPASS, alarms 3, 4 and
8–11 (mask `0x0F18`).

### Relay lock, slots and OTA

- **All home outputs use the control slot with the 60 s min-ON/min-OFF lock (A5)**, including the fail-safe
  forced ones. Home heating has no heat-removal safety duty, so the safety slot is never used. The exercise
  slot and the OTA inhibit stay owned by anti-seize and OTA.
- The K1 relays are driven only by `HwRuntime`'s `K1Driver` (interlocked power + direction).

### Alarm bits and events

The controller owns alarm bits 0–23 and never touches the stage-03 bits 24–31 ("Sensor <name> missing"). A
sensor condition is alarmed once: bit 24+i when the sensor is missing, otherwise bit 8+i. Fail-mode bits are
separate, so a missing H3 shows both "Sensor H3 missing" and "H3 fail: K1 fixed". Unknown raises nothing, and
the alarms do not depend on `heatingEnabled`.

| Bit | Key | Label |
|-----|-----|-------|
| 0 | `h3_failsafe` | H3 fail: K1 fixed |
| 1 | `h2_failsafe` | H2 fail: K1 FF only |
| 2 | `h1_failsafe` | H1 fail: K1 FB only |
| 3 | `multi_failsafe` | Sensors fail: K1 fix |
| 4 | `h4_failsafe` | H4 fail: K2 bypass |
| 8–11 | `h1_fault` … `h4_fault` | H<n> fault/unassigned (Fault without missing, or Unassigned) |

Events (reason Logic), rate-limited by the existing limiter (60 s per type and source; sources differ per
target value so an ON→OFF pair is never swallowed):

- `HH_EVENT_P4_REQUEST` (project type 0, value = P4 reason, aux = on) and `HH_EVENT_K2_REQUEST` (type 1,
  value = K2 reason, aux = bypass) when the request **flips** (not on reason-only changes).
- `HH_EVENT_FAILSAFE` (type 2, value = new fail mode, aux = old), `HH_EVENT_K1_RECAL` (type 3, 1 start /
  0 end), `HH_EVENT_NO_NEED` (type 4, new 0/1).
- Alarm bit edges → `AlarmRaised` / `AlarmCleared` (source `EVENT_SOURCE_ALARM_BASE + bit`). No per-pulse K1
  events. Actual relay switches are still logged by `RelayBank`.
- Settings missing at boot → one `DiagnosticWarning` (source `DIAG_BASE + 32`) per boot plus a Serial line.

### Settings (appended as the last table; config version stays 1)

| Key | Group | Range | Default | Meaning |
|-----|-------|-------|---------|---------|
| `h2Set` | heating | 30–75 °C (0.5) | 40 | radiator supply setpoint H2 |
| `p4OffDelay` | heating | 0–60 min | 5 | P4 off delay |
| `k1Travel` | k1 | 30–300 s | 120 | K1 full travel time |
| `k1Period` | k1 | 10–300 s | 30 | K1 control period |
| `k1Deadband` | k1 | 0.2–5 °C (0.1) | 1 | feedback deadband |
| `k1Gain` | k1 | 0.5–10 s/°C (0.5) | 2 | feedback gain |
| `k1MaxPulse` | k1 | 1–60 s | 10 | max feedback pulse (guarded ≥ min pulse) |
| `k1MinPulse` | k1 | 0.5–5 s (0.5) | 1 | min pulse (shorter moves skipped) |
| `k1Resync` | k1 | 5–25 % | 10 | end-stop overdrive |
| `k1SmallDiff` | k1 | 0.5–10 °C (0.5) | 2 | K1 fully open if H3 − H1 below |
| `k1FailPos` | k1 | 0–100 % | 30 | fail-safe position |
| `k1FfStep` | k1 | 1–25 % | 5 | FF re-apply step |
| `k2Delta` | k2 | 1–15 °C (0.5) | 3 | DHW: charge if H3 above H4 by |
| `k2DeltaHyst` | k2 | 0.5–10 °C (0.5) | 2 | differential hysteresis (guarded ≤ `k2Delta` − 0.5) |
| `k2H3Min` | k2 | 40–80 °C | 65 | DHW: minimum supply H3 |
| `k2H3MinHyst` | k2 | 1–10 °C (0.5) | 3 | H3 minimum hysteresis |
| `k2H4Max` | k2 | 40–80 °C | 70 | DHW: tank maximum H4 |
| `k2H4MaxHyst` | k2 | 1–10 °C (0.5) | 3 | H4 maximum hysteresis |

The existing `heatingEnabled` ("Heating enabled", group `flags`, HA switch) enables heating (P4/K1). No new
Bool setting was added.

### Home Assistant entities

These come on top of the common entities (H1–H4 temperatures, `relay_p4`, `relay_k2`, the settings including
the `heating_enabled` switch and the `h2_set` number):

| Key | Type | Notes |
|-----|------|-------|
| `no_need` | binary sensor | the "no need" signal (copied to the boiler room by the automation below) |
| `p4_reason`, `k2_reason` | sensor | reason keys above |
| `k2_mode` | sensor | `tank` / `bypass` (request) |
| `k1_position` | sensor, %, `measurement` | whole percent; unavailable while the position is unknown |
| `k1_mode` | sensor | K1 mode key |
| `failsafe` | sensor, diagnostic | `none` / `h1` / `h2` / `h3` / `multi` |
| `alarm_h3_failsafe`, `alarm_h2_failsafe`, `alarm_h1_failsafe`, `alarm_multi_failsafe`, `alarm_h4_failsafe` | binary sensor, `problem` | bits 0–4 |
| `alarm_h1_fault` … `alarm_h4_fault` | binary sensor, `problem`, diagnostic | bits 8–11 |

The status entities are unavailable until the controller has started (`bindHomeHeatingHaStatus` in `setup()`
and the first `ctl.tick()`).

### HA automation: home "no need" → boiler room

[`docs/ha-home-no-need-automation.yaml`](docs/ha-home-no-need-automation.yaml) copies the home's **current**
"No need" state to the boiler-room "Home: no need" switch. It is not edge-only, because the boiler room clears
that switch by itself when it OFFERs heat (stage 07). It runs on any change of the home sensor, on any state
change of the boiler-room switch (turning off re-asserts after the self-clear; coming back from `unavailable`
corrects a stale persisted flag at once), at HA start and every 5 minutes. If the home
sensor is `unavailable` or `unknown`, the boiler-room switch is turned **OFF** (A3: keep supplying heat). The
switch is written only when it differs, so there are no redundant MQTT writes and no command loop. The default entity IDs are
`binary_sensor.home_heating_no_need` and `switch.boiler_room_home_no_need`; adjust them to your HA (see the
comment in the file).

### OLED pages and `/api/state` "ctl"

- **Pages**, in rotation before Network/Sensors/Alarms:
  - **Heating**: `H2 41.2 set 40.0`, `H3 70.1`, `K1 35% >` (`>` opening, `<` closing) / `K1 recal` /
    `K1 ?`, `P4 ON|OFF <reason>` (actual relay), and the fail-mode alarm label when a fail-safe is active.
  - **DHW**: H3, H4, `K2 TANK|BYP <reason>` (actual relay), `No need: yes|no`.
  - Temperatures show `--.-` when not Ok; `starting...` until the controller is ready.
- The Alarms page uses the labels above (bits 0–11).
- `/api/state` carries a `"ctl"` member (`WebServices::setStateExtension()`), `{"ok":false}` until ready:

```json
"ctl":{"ok":true,"en":true,"set":40,
 "p4":{"on":true,"r":"demand","dlyS":0,"as":false},
 "k2":{"byp":false,"r":"charging","as":false},
 "k1":{"pos":35,"mv":1,"m":"normal","ff":42,"as":false},
 "fs":"none","nn":false}
```

`p4.on`/`k2.byp` are the controller's requests (the actual relays stay in the base `relays[]`); `as` means
anti-seize is running; `k1.pos` is `null` while unknown and `k1.ff` is `null` until an FF target was applied.

### Home-heating controller checks (hardware only, NOT TESTED)

None of these has been run on a real board yet.

- [ ] **Boot recal.** At power-on all relays stay OFF (`RelayBoot` first). K1 then drives CLOSE for about
      132 s (`K1 recal` on the Heating page, `k1_mode` = `recal`), after which the position shows `0%`.
- [ ] **P4 start after the lock.** With H3 ≥ `h2Set`, P4 switches ON only after the 60 s boot relay lock;
      K1 stays closed until the P4 relay is actually ON, then makes one FF move and trims every `k1Period` s.
- [ ] **H3 fail-safe.** Unplug H3: P4 forced ON (`h3_fault`, respecting the lock), K1 moves to 30 % and then
      follows H2, K2 goes BYPASS, "Sensor H3 missing" and "H3 fail: K1 fixed" are shown, no-need stays OFF.
- [ ] **H4 fail-safe.** Unplug H4: K2 goes BYPASS (`h4_fault`) and "H4 fail: K2 bypass" is raised.
- [ ] **Heating off.** Turn `heating_enabled` off: P4 goes OFF (after the lock), K1 recalibrates and stays
      closed; alarms, if any, stay.
- [ ] **No need.** With heating off (or H3 < `h2Set`) and K2 in BYPASS (e.g. tank full), `no_need` turns ON
      in HA once P4 is off and `p4OffDelay` (5) min have passed since the last demand, and turns OFF as soon
      as P4 ON or K2 TANK is requested.
- [ ] **Automation round-trip.** With the YAML installed, the boiler-room "Home: no need" switch follows
      `no_need`. When the boiler room OFFERs and clears the switch, the automation sets it again within
      seconds while the home still has no need. Power off the home controller: the switch turns OFF.
- [ ] **OTA.** During an OTA update all relays drop and K1 stops; afterwards FF is re-applied and an
      unfinished recal resumes; the position estimate stays plausible.
- [ ] **Anti-seize.** A K1 anti-seize stroke moves the position estimate; P4/K2 exercises do not flap
      `no_need`.
- [ ] **Pages.** The Heating and DHW pages rotate before Network and Sensors, update live, no row is
      clipped, and every raised alarm shows its label on the Alarms page.
- [ ] **Watchdog.** Several hours of running with pages rotating and HA connected: no watchdog resets, and
      the `debug` loop timing stays well under 100 ms.

## Diagnostics & K1 tuning (stage 09)

Stage 09 adds **pump-response diagnostics** to both controllers and **K1 tuning aids** to home heating. The
diagnostics only raise **warnings**: they never switch a relay, change a control decision or raise an alarm.
If their settings table is missing or mistyped, diagnostics (and the step test) are off and control runs
unchanged (one `DiagnosticWarning`, source `EVENT_SOURCE_DIAG_BASE + 33`, value = the diag key index).

- `SensorHistory` (in `common/lib/HwEngine`, pure) keeps the last 60 min of each sensor at 30 s (NaN for a
  sample that was not Ok). The step test uses it for its "steady" check.
- boiler-room: `PumpRiseCheck`, `BoilerRoomDiagSettings`, `BoilerRoomDiagnostics` in `BoilerRoomEngine`, run by
  `BoilerRoomRuntime` after the control step.
- home-heating: `P4FlowCheck`, `HomeHeatingDiagSettings`, `HomeHeatingDiagnostics`, `K1PulseCounter` and
  `K1StepTest` in `HomeHeatingEngine`, run by `HomeHeatingRuntime`.
- The owner guide for K1 tuning is [`docs/k1-tuning-guide.md`](docs/k1-tuning-guide.md).

### Pump checks (B1, B3, B6, H1)

| Check | Project | Pump | Warns when (all true) | Defaults |
|-------|---------|------|------------------------|----------|
| **B1** P3 no flow | boiler-room | P3 | P3 ON ≥ `b1MinOn` min, T3 − T6 > `b1Delta`, and T6 rose less than `b1MinRise` since P3 started | 3 min, 15 °C, 2 °C |
| **B3** P1 not charging | boiler-room | P1 | P1 ON ≥ `b3MinOn` min, T1 − T3 > `b3Delta`, and T3 rose less than `b3MinRise` since P1 started | 10 min, 10 °C, 1 °C |
| **B6** P2 no effect | boiler-room | P2 | P2 ON ≥ `b6MinOn` min, T1 − T2 > `b6Delta`, and T2 rose less than `b6MinRise` since P2 started | 5 min, 10 °C, 2 °C |
| **H1** P4 no flow | home-heating | P4 | P4 ON ≥ `h1MinOn` min, K1 position > `h1K1Min` %, H3 > H1 + `h1Delta`, and H2 − H1 < `h1MinDiff` | 5 min, 30 %, 10 °C, 2 °C |

- **"ON" is the actual relay (A1).** The timer and the baseline start at the relay's actual ON edge (a
  request delayed by the relay lock does not count). The baseline is snapshotted at that edge. A 30 s
  anti-seize exercise never reaches a window. Pump OFF → tracking stops; the next ON starts over.
- **B1/B3/B6 are start-up checks (A2).** "Rose" is measured since the pump started; a pump that stops moving
  water after it already made the rise is not detected.
- **H1 is instantaneous (A3):** no rise term; it uses the K1 position estimate and pauses while the estimate is
  unknown (e.g. during recalibration). In the 30 % fail-safe position it never triggers (30 is not > 30).
- **Pause on a sensor problem (A4).** While a sensor the check uses (the two temperatures of a B-check, H1–H3
  for H1) is Failed, Unassigned or Pending-unknown, the check pauses: the warning clears and the timer and
  baseline reset; the check restarts at recovery. A sensor in its short debounce still reads Ok and does not
  pause.
- A warning **auto-clears** as soon as its condition is gone. Disabling a check clears it on the next tick; the
  check keeps tracking while disabled, so re-enabling it raises at once if the condition still holds.

### Warning bits, events and HA entities

The checks own bits 0–7 of `diag.warningMask` (shown as `"warnings"` in `/api/state` and as amber chips in the
web header, label `warn.<bit>`); the other bits are left alone.

| Project | Bit | Web label | HA binary sensor (device class `problem`) |
|---------|-----|-----------|--------------------------------------------|
| boiler-room | 0 | P3 no flow (B1) | `binary_sensor.boiler_room_warn_p3_no_flow` |
| boiler-room | 1 | P1 not charging (B3) | `binary_sensor.boiler_room_warn_p1_not_charging` |
| boiler-room | 2 | P2 no effect (B6) | `binary_sensor.boiler_room_warn_p2_no_effect` |
| home-heating | 0 | P4 no flow (H1) | `binary_sensor.home_heating_warn_p4_no_flow` |

Events (reason Logic):

| Event | Type | Source | Value / aux |
|-------|------|--------|-------------|
| `BR_EVENT_DIAG_WARNING` ("Pump diagnostic") | 1002 | `0x1040 + bit×2 + raised` | bit / 1 raised, 0 cleared |
| `HH_EVENT_DIAG_WARNING` ("Pump diagnostic") | 1005 | `0x1040 + bit×2 + raised` | bit / 1 raised, 0 cleared |
| `HH_EVENT_STEP_START` | 1006 | `0x1050` | pulse s / H2 at start |
| `HH_EVENT_STEP_RESULT` | 1007 | `0x1051` | dead time s (−1 when H2 never moved) / response °C/s (0 for no response) |
| `HH_EVENT_STEP_ABORT` | 1008 | `0x1052` | abort code / elapsed s |

The existing limiter (60 s per type and source) applies: a raise → clear → raise of the same check within 60 s
logs only the first raise and the clear. `diag.warningMask` and HA stay authoritative.

### Settings (appended as the last table; config version stays 1)

boiler-room, group `diag` (the enables are HA config switches, the rest HA numbers, e.g. `b1_en`,
`b1_min_on`):

| Key | Range | Default | Meaning |
|-----|-------|---------|---------|
| `b1En` | Bool | on | B1: P3 no-flow check |
| `b1MinOn` | 1–60 min | 3 | B1: P3 on at least |
| `b1Delta` | 5–40 °C (0.5) | 15 | B1: T3 − T6 above |
| `b1MinRise` | 0.5–10 °C (0.5) | 2 | B1: T6 rise below |
| `b3En` | Bool | on | B3: P1 charging check |
| `b3MinOn` | 1–60 min | 10 | B3: P1 on at least |
| `b3Delta` | 3–40 °C (0.5) | 10 | B3: T1 − T3 above |
| `b3MinRise` | 0.5–10 °C (0.5) | 1 | B3: T3 rise below |
| `b6En` | Bool | on | B6: P2 effect check |
| `b6MinOn` | 1–60 min | 5 | B6: P2 on at least |
| `b6Delta` | 3–40 °C (0.5) | 10 | B6: T1 − T2 above |
| `b6MinRise` | 0.5–10 °C (0.5) | 2 | B6: T2 rise below |

home-heating:

| Key | Group | Range | Default | Meaning |
|-----|-------|-------|---------|---------|
| `h1En` | diag | Bool | on | H1: P4 no-flow check |
| `h1MinOn` | diag | 1–60 min | 5 | H1: P4 on at least |
| `h1K1Min` | diag | 5–95 % | 30 | H1: K1 open above |
| `h1Delta` | diag | 3–40 °C (0.5) | 10 | H1: H3 − H1 above |
| `h1MinDiff` | diag | 0.5–10 °C (0.5) | 2 | H1: H2 − H1 below |
| `k1StepPulse` | k1 | 2–30 s | 10 | K1 step-test OPEN pulse |

The step-test thresholds (steady bands, 0.5 °C move, 0.2 °C / 2 min settle, 10 min cap, 2 °C H3 abort) are
fixed constants in `K1StepTest.h`, not settings.

### Home-heating tuning entities (HA)

On top of `warn_p4_no_flow` (above) and the settings:

| Key | Type | Notes |
|-----|------|-------|
| `h2_error` | sensor, °C, `measurement` | `H2 − h2Set`, 1 decimal (positive = H2 above the setpoint, too warm); unavailable unless H2 is Ok and heating is enabled |
| `k1_last_pulse` | sensor, s, `measurement` | commanded length of the last control pulse; unavailable until the first one |
| `k1_last_pulse_dir` | sensor, diagnostic | `open` / `close` / `none` |
| `k1_pulses_today` | sensor, diagnostic, `total_increasing` | K1 runs since local midnight |
| `k1_pulses_yesterday` | sensor, diagnostic | K1 runs of the previous local day; unavailable until the first rollover |

Entity totals: boiler-room 18 custom entities (67 in the full registry), home-heating 22 custom (65), both
≤ 96.

### K1 step test (summary)

A web-only tool on the home-heating status page ("K1 tuning" panel). Start issues one K1 OPEN pulse of
`k1StepPulse` s under the Control owner, holds K1 feed-forward/feedback, and watches H2: dead time = first
0.5 °C move, settled = H2 within 0.2 °C for 2 min (or H2 at 10 min), response R = settled rise per pulse
second. It suggests `k1Period = round(1.5 × dead time)` (10–300) and `k1Gain = 0.5 / R` rounded to 0.5
(0.5–10), i.e. half the error corrected per period. **Apply** writes `k1Period`, then `k1Gain`, through
`POST /api/config`; nothing is applied automatically.

- **Start conditions** (first failing is shown, key `st.blk`): `unavailable`, `running`, `heating_off`, `ota`,
  `p4_off`, `anti_seize`, `sensors` (H1–H3 not Ok), `k1_mode` (not `normal`), `k1_unknown`, `k1_busy`,
  `k1_headroom` (position + `100 × k1StepPulse / k1Travel` must stay < 100 %), `history` (< 5 min of history,
  so no test in the first ~5 min after boot), `h3_unsteady` (±1 °C over 5 min), `h2_unsteady` (±0.5 °C).
- **Aborts** (`res.ab`): `cancel`, `heating_off`, `ota`, `p4_off`, `anti_seize`, `recal`, `sensors`, `k1_mode`,
  `h3_changed` (H3 moved > 2 °C from its start value). A reboot also ends the test.
- **Outcomes** (`res.o`): `result` (with a suggestion), `no_response` (no 0.5 °C move in 10 min, or no positive
  rise; no suggestion), `aborted`.
- When the test ends, the hold is released: FF is re-applied once K1 is idle, feedback resumes a period later.
- The test, its live values and the last result are **RAM only** (gone after a reboot). No HA controls.

See [`docs/k1-tuning-guide.md`](docs/k1-tuning-guide.md) for when to run it, how to read it and manual tuning.

### K1 pulse counter (RAM only, A9)

`K1PulseCounter` counts every energisation of the K1 power relay from idle (`K1Driver::runStarts()`), whatever
the owner (control, recal, anti-seize, step test); extending a run in the same direction is not a new count.
It is a relay-wear indicator. "Today" rolls into "yesterday" at local midnight (`LocalTimeInfo` from
`HardwareServices::localTime()`); a gap of more than one day gives yesterday = 0; with no valid time the count
keeps accumulating in today. The counters are **not persisted**: after a reboot today restarts at 0 and
yesterday is n/a until the first rollover. The **last pulse** (`k1_last_pulse*`) is only the last
Control-issued command (FF, feedback or step test), never recal or anti-seize.

### Project command route: `POST /api/project/cmd`

Access W (session + CSRF). Form field `op` (1–255): missing → 400 `missing`, non-numeric or out of range →
400 `bad_index`; otherwise 202 `{"ok":true,"id":N}` (or 503 `busy`), result via `GET /api/cmd?id=N`. It posts
a `CommandType::Project` command; `CoreRuntime` passes it to the handler set with
`CoreServices::setProjectCommandHandler()`.

| Project | op | Result |
|---------|----|--------|
| home-heating | 1 = step-test start | `ok` (latched for the next tick) or `rejected` (not ready, running/pending, or blocked) |
| home-heating | 2 = step-test cancel | `ok` (running or pending start cancelled) or `unchanged` (nothing to cancel) |
| home-heating | other | `invalid` |
| boiler-room | any | `invalid` (no handler registered) |

### `/api/state` "ctl" additions (home-heating)

After `"nn"` the home-heating `"ctl"` gains the following members, bringing `/api/state` to a worst case of
about 2.1 KB (measured by a test; up from 1.87 KB before stage 09; boiler-room `"ctl"` is unchanged, its
warnings travel in the base `"warnings"`):

```json
"tu":{"err":-0.4,"lpd":1,"lps":4.5,"pt":12,"py":7},
"st":{"run":false,"blk":"none","el":0,"ps":10,"dt":null,
      "res":{"o":"result","ab":null,"dt":22.0,"r":0.250,"p":33,"g":2.0}}
```

- `tu.err` H2 error (null unless valid), `lpd` last pulse dir (1 / −1 / 0), `lps` its length s, `pt` pulses
  today, `py` yesterday (null until the first rollover).
- `st.run` running, `blk` block key, `el` elapsed s, `ps` pulse s, `dt` live dead time (null until seen),
  `res` the last result (null until a test has ended): `o` outcome, `ab` abort key (null unless aborted), `dt`
  dead time (null if none), `r` response °C/s, `p`/`g` suggestion (only for a `result` with a valid
  suggestion).

### Diagnostics & tuning checks (hardware only, NOT TESTED)

None of these has been run on a real board yet.

- [ ] **B1.** Close a valve on P3's loop while P3 runs with a hot accumulator (T3 − T6 > 15 °C): "P3 no flow
      (B1)" appears in the web header and `warn_p3_no_flow` turns ON after 3 min; opening the valve (T6 rises
      2 °C) clears it.
- [ ] **B3 not raised when cold.** A P1 run with a cold boiler (T1 − T3 ≤ 10 °C) raises no B3.
- [ ] **Pause.** Unplug T6 while B1 is active: the warning clears; after re-plugging, the 3 min timer starts
      over.
- [ ] **H1.** Run P4 with a closed radiator valve (K1 > 30 %, H3 > H1 + 10 °C): "P4 no flow (H1)" and
      `warn_p4_no_flow` after 5 min.
- [ ] **Step test.** On a steady evening (≥ 5 min after boot, H2 near the setpoint) the panel shows no block
      reason; Start runs one OPEN pulse, the result is plausible (dead time 10–60 s), and Apply writes both
      `k1Period` and `k1Gain` (visible in Settings and HA).
- [ ] **Cancel.** Cancel mid-test: the result shows `aborted` / `cancelled`, and K1 resumes (FF move, then
      feedback).
- [ ] **Pulse counter.** `k1_pulses_today` increments on every K1 move (control, recal, anti-seize), and rolls
      into `k1_pulses_yesterday` at local midnight.
- [ ] **HA.** The new entities appear: the three boiler-room `warn_*` binary sensors and the `b*` settings; the
      home-heating `warn_p4_no_flow`, `h2_error`, `k1_last_pulse`, `k1_last_pulse_dir`, `k1_pulses_today`,
      `k1_pulses_yesterday` and the `h1*` / `k1_step_pulse` settings.

## Prerequisites

- PlatformIO (VS Code extension `platformio.platformio-ide`, or the `pio` CLI).
- A host C/C++ compiler (`gcc`/`g++`) is required for `pio test -e native` (the `native` platform
  compiles and runs tests on the host, not on the ESP32). It is **not** required for ESP32 builds
  (`release`/`debug`), which use PlatformIO's bundled xtensa toolchain.
  - If missing on Windows: `winget install BrechtSanders.WinLibs.POSIX.UCRT`, or install MSYS2 and
    `pacman -S mingw-w64-ucrt-x86_64-gcc`.
  - The WinGet WinLibs package (gcc 16.1.0) installs into
    `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin`
    and adds that folder to the **user** `PATH`. Terminals and VS Code windows opened before the
    install do not see it — close and reopen them, then check with `gcc --version`.
  - If `gcc` is still not found, prepend the folder for the current session:

    ```powershell
    # PowerShell
    $env:Path = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;" + $env:Path
    gcc --version
    ```

    ```sh
    # Git Bash
    export PATH="$(cygpath -u "$LOCALAPPDATA/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"):$PATH"
    gcc --version
    ```

    To make it permanent, add the same folder to the user `PATH` (Settings → System → About →
    Advanced system settings → Environment Variables).

## Commands

Run from the repo root (`-d <project>` points PlatformIO at that project's `platformio.ini`):

```sh
# ESP32 builds
pio run -d boiler-room -e release
pio run -d boiler-room -e debug
pio run -d home-heating -e release
pio run -d home-heating -e debug

# Native unit tests (project suite + shared common/test suite)
pio test -d boiler-room -e native
pio test -d home-heating -e native

# Filesystem image (regenerates data/ from common/web + <project>/web, then builds/uploads it)
pio run -d boiler-room -e release -t buildfs
pio run -d boiler-room -e release -t uploadfs
pio run -d home-heating -e release -t buildfs
pio run -d home-heating -e release -t uploadfs
```

## Shared library linking

Shared libs (`common/lib/BoardConfig`, `common/lib/RelayBoot`) are linked into each project via
`lib_deps = symlink://../common/lib/<Lib>` in `common/platformio-common.ini`. PlatformIO 6.x
implements `symlink://` with `.pio-link` metadata files rather than OS symlinks, so it works on
Windows without admin rights. If a project ever reports the library as not found, switch that
project's `lib_deps` to `lib_extra_dirs = ../common/lib` plus the plain library name, and note the
change here.

Status: **`symlink://` in use**, no fallback needed so far.

## Build flags vs. the reference project

The reference project (`D:/home/boiler/platformio.ini`) builds with `-fpermissive`. This project
drops it (the workflow profile is `type_system: strict`, and no dependency has needed it during a
`release` build). If a third-party library in `lib_deps` ever fails to compile without it,
`-fpermissive` will be re-added to `[env:release]`/`[env:debug]` in `common/platformio-common.ini`
and that will be recorded here with the library name.

Status: **no `-fpermissive` needed.**

## Firmware version

`common/scripts/fw_version.py` runs as a `pre:` extra script on `release`/`debug`. It calls
`git describe --tags --always --dirty` in the project directory and injects the result as the
`FW_VERSION` build-time string (and into `env["HEATER_FW_VERSION"]` for later scripts). With no
commits/tags yet (this repo was created with `git init` only, no commit), it falls back to
`"unknown"` — this is expected and does not fail the build.

## Web files

`common/scripts/web_assemble.py` runs as a `pre:` extra script on `release`/`debug`, after
`fw_version.py`, but only when the build target is `buildfs`, `uploadfs`, or `uploadfsota` — a
plain `release`/`debug` build never touches `data/`.

- Sources: `common/web/` (shared) and `<project>/web/` (per-project). Every visible file from both
  is gzipped into `<project>/data/<relpath>.gz`. Hidden files/dirs (e.g. `.gitkeep`) are skipped.
- **Project files override common ones by path** — if both trees have the same relative path, the
  project's copy wins.
- `<project>/data/` is wiped and regenerated from scratch on every run, so it never accumulates
  stale files. It is git-ignored — never edit it directly.
- `data/version.json.gz` is always generated (not sourced from `web/`) with the shape
  `{"project": "<project folder name>", "fw": "<FW_VERSION>"}`.
- `data/version.txt` is always generated too: the plain, uncompressed FW version plus a newline,
  read by the firmware at boot for the web/FW mismatch check. A web source with either name loses
  to the generated file (a warning is printed).

Commands:

```sh
pio run -d <project> -e release -t buildfs    # regenerate data/ only
pio run -d <project> -e release -t uploadfs   # regenerate data/ and flash it
```

## Board bring-up checklist (KC868-A6 relay polarity)

The KC868-A6 relay board's active level cannot be measured by an agent. `RELAY_ACTIVE_LOW` in
`common/lib/BoardConfig/src/BoardConfig.h` defaults to `true` (all-OFF byte = `0xFF`), marked
`TODO(board-check)`. Confirm it on real hardware before connecting any load:

1. Disconnect all loads (pumps / valve motors) from R1–R6.
2. Flash the `release` env to the board, open the serial monitor at 115200.
3. Power-cycle and press EN (reset) several times. All six relay LEDs must be OFF right after
   reset and stay OFF. The serial banner shows the project name and firmware version.
4. If the relays click ON / the LEDs light at boot, set `RELAY_ACTIVE_LOW = false` in
   `BoardConfig.h`, rebuild, and repeat from step 2.
5. Once confirmed, replace the `TODO(board-check)` comment with `// Verified on board <date>`.
6. A `[boot] ... all-OFF write failed` line on the serial monitor means the PCF8574 relay
   expander did not ACK — check the I²C wiring/address (`0x24`) before trusting the relay state.

### DI1 input polarity (factory-reset gate)

`INPUT_ACTIVE_LOW` in `common/lib/BoardConfig/src/BoardConfig.h` defaults to `true` (DI1 reads
"closed" when the PCF8574 @ `0x22` bit is `0`), marked `TODO(board-check)`. Confirm on real
hardware:

1. Flash `release`, open the serial monitor at 115200.
2. With DI1 **not** wired/closed, power-cycle — the serial log must show no `[factory-reset] DI1
   held` lines (the countdown never starts).
3. Close DI1 (short the input to its active level) and power-cycle. The serial log should print
   `[factory-reset] DI1 held, N s left` counting down from 10; releasing it before 0 must stop the
   countdown with no config change, and holding it to 0 must log a `FactoryReset` event and reset
   the web login to `admin`/`admin`.
4. If the countdown never starts while DI1 is actually closed (or starts while it is open), set
   `INPUT_ACTIVE_LOW = false`, rebuild, and repeat from step 2.
5. Once confirmed, replace the `TODO(board-check)` comment with `// Verified on board <date>`.

### Hardware services checks (stage 03: DS18B20 bus, K1 wiring, PCF8574 re-assert)

None of the following can be verified without the real board/sensors/motor. **NOT TESTED** until
done on hardware:

1. **DS18B20 bus scan.** With every sensor wired to `ONE_WIRE_PIN` (32), power-cycle and check the
   `[hw] ... devicesFound=N` boot line: `N` must equal the number of physically wired sensors. If it
   is lower, check bus wiring/pull-up (4.7 kΩ to 3.3 V) and re-run; if it is truncated at 12, see
   `state.oneWire.overflow` (more than `ONE_WIRE_MAX_DEVICES` = 12 devices on one bus is not
   supported).
2. **K1 direction/power wiring (home-heating only).** With the valve motor connected, issue a K1
   `Open` pulse (e.g. via a short-duration anti-seize test run or a future controller command) and
   confirm: R3 (direction) energises first, the valve does not move until **after** R2 (power)
   energises roughly 1 s later, and the valve actually moves **open** when R3 is energised — not
   closed. If the motion is reversed, swap the R3 wiring polarity (do not change the code's `Close` /
   `Open` mapping, since `K1Wiring`/`RelayRole` assume R3-energised = OPEN throughout the codebase).
3. **PCF8574 re-assert / IO error visibility.** With the relay expander running normally, disconnect
   its I²C wiring briefly. The serial log must show exactly one `[hw]`/diagnostic line for the
   failure transition (not a flood — `HwRuntime` logs once per ok→fail transition), and reconnecting
   must clear the error on the next ~100 ms fast tick (no reboot needed). `state.relays.ioError` /
   `state.relays.ioErrorCount` (surfaced once a UI reads them, stage 05+) are the fields to watch if
   testing via the web API instead of serial.

### OLED display checks (stage 06, hardware only, NOT TESTED)

None of these has been run on a real board yet.

- [ ] **Boot splash.** At power-on the panel shows the project name, `fw <version>` and
      "starting..." without first flashing random pixels. The serial log shows
      `[display] pages=2 available=1`.
- [ ] **DI1 countdown screen.** Hold DI1 closed and power-cycle. "FACTORY RESET / DI1 held" appears
      and the big seconds number and the bar count down once per second, in step with the
      `[factory-reset] DI1 held, N s left` lines. Release it early: "Reset aborted" stays for about
      3 s after boot. Hold it to 0: "Reset confirmed". In both cases the relays stay OFF throughout.
- [ ] **Display timing.** Flash the `debug` env. Every 60 s the serial log prints
      `[display] max tick <us> us`. It must stay well under 60 000 µs (two rows are about 30–32 ms), and
      relay/K1 behaviour must be unchanged while pages rotate.
- [ ] **Missing display.** Unplug the OLED (or boot without it). Boot and control continue with no
      delay. The Log page shows exactly **one** "Diagnostic warning" event for this boot, and the
      serial log has one `[display] ... not responding` / `not found` line, not a flood. Plug it back
      in: within about 30 s the log shows `[display] recovered` and the panel repaints.
- [ ] **Brightness.** The panel is readable at the 30 % default `dispBright`. Changing `dispBright`
      in the web Settings (Display tab) or from HA changes the contrast within a second.
- [ ] **Bus clock.** With a logic analyser on SDA/SCL, all traffic (relay PCF8574, DI1, DS1307,
      OLED) runs at ≈100 kHz. The display must never switch the bus to 400 kHz.
- [ ] **Alarm jump.** Unplug an assigned DS18B20. The Alarms page appears with
      "Sensor <name> missing", holds for 30 s, then rotation resumes at the first page.
- [ ] **Single-row updates.** The panel runs in horizontal addressing mode and only changed tile rows
      are sent. Watch the Network and Sensors pages for several minutes while pages rotate and after
      a pixel-shift step: live values (RSSI, sensor status) must update in place, with no shifted
      rows, stale fragments or garbage pixels.
- [ ] **Setup-mode screen.** Clear the saved Wi-Fi (or boot a fresh board). The panel shows
      `SETUP MODE`, the AP SSID, `(open, no password)` and `Open: 192.168.4.1`, readable and not
      clipped. With an alarm active, every 3rd slot shows the Alarms page.
- [ ] **OTA screen.** Start a web upload and an espota update. The panel switches to "UPDATING" with
      the OTA source for the whole update, including while an alarm hold is active.
