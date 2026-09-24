# Heater System — Project Brief

Two independent controllers on **Kincony KC868-A6 (ESP32)** boards, written in **C++** with **PlatformIO** (VS Code, Windows).

- **Boiler room controller** — solid-fuel boiler + 500 L accumulator, pumps P1/P2/P3.
- **Home controller** — mixing valve K1, home pump P4, DHW diverter K2.

The controllers do **not** talk to each other directly. Each one connects to the local Wi-Fi, publishes to **Home Assistant over MQTT**, has its own **web UI** and its own **SSD1306 OLED**. All control logic runs locally; HA is only for monitoring and remote signals. Every threshold below is **configurable from the web UI** (stored in flash) unless stated otherwise.

---

## 1. Project structure

Two separate PlatformIO projects sharing common libraries:

```
heater-system/
├── common/lib/          ← shared: WiFi+AP portal, MQTT+HA discovery, WebUI+auth, OTA,
│                          OLED pages, DS18B20 bus+assignment, Config, EventLog, RTC/NTP,
│                          Relay (PCF8574) with lock/anti-seize
├── common/web/          ← shared web UI base (style.css from D:/home/boiler/data/)
├── boiler-room/         ← PlatformIO project #1
│   ├── platformio.ini   ← lib_deps = symlink://../common/lib/<Lib>
│   └── src/
└── home-heating/        ← PlatformIO project #2
    ├── platformio.ini
    └── src/
```

- Toolchain follows the existing project `D:/home/boiler/`:
  - `platform = espressif32`, `board = esp32dev`, `framework = arduino`
  - `board_build.partitions = min_spiffs.csv`, `board_build.filesystem = littlefs`, `flash_mode = dio`
  - environments `release` / `debug` (`CORE_DEBUG_LEVEL=4`, `-DDEBUG_BUILD`) / `native` (unit tests)
  - monitor 115200, upload 921600
- Libraries (same set, minus BLE):
  - `paulstoffregen/OneWire`, `milesburton/DallasTemperature` — DS18B20
  - `olikraus/U8g2` — SSD1306
  - `ottowinter/AsyncMqttClient-esphome` — MQTT
  - `esp32async/ESPAsyncWebServer`, `esp32async/AsyncTCP` — web UI / REST
  - `bblanchon/ArduinoJson`
  - `ayushsharma82/ElegantOTA` (async mode)
  - **new:** PCF8574 relay driver and DS1307 RTC (small in-house drivers or `adafruit/RTClib`)
- Architecture follows the same pattern: `AppState` as the single source of truth, a controller for the logic, views (Display / Web / MQTT) that only read the state, and hardware/service layers. The web UI is a SPA served from LittleFS (`data/`).
- Upload: USB first, then OTA.

---

## 2. Hardware (KC868-A6) — pin/address map to verify against board docs

| Function | Resource |
|---|---|
| I²C bus | SDA GPIO4, SCL GPIO15 |
| Relays (6) | PCF8574 @ 0x24 |
| Digital inputs (6) | PCF8574 @ 0x22 (not used yet) |
| OLED SSD1306 | I²C @ 0x3C |
| RTC DS1307Z (fitted) | I²C @ 0x68 |
| DS18B20 1-Wire bus | **GPIO32** (one bus, daisy-chained per room, 4.7 kΩ pull-up on board) |
| DI1 | Reserved for the physical factory reset (closed at power-on, held 10 s) |

The PCF8574 is **not reset when the ESP32 restarts** (watchdog, OTA, reset button): the relays keep their last state until firmware writes to them. The firmware therefore writes **all relays OFF as the very first action** in `setup()`. The relay active level (whether HIGH means ON or OFF) must be checked once on the real board.

| Relay | Boiler room | Home |
|---|---|---|
| R1 | P1 charging pump | K2 diverter |
| R2 | P2 return-protection pump | K1 motor power |
| R3 | P3 supply pump | K1 direction (NO → OPEN, NC → CLOSE) |
| R4 | spare | P4 home pump |
| R5–R6 | spare | spare |

---

## 3. Boiler room controller

### 3.1 Hydraulic diagram

```
                  T1 (flow)
        ┌──────────●──────────┬───────────────────┐
        │                     │                   ▼
        │                     │           ┌───────────────┐     T6
        │                     │           │ ● T3  top     ├─────●───[P3]────────►  to HOME (supply)
  ┌─────┴──────┐              │           │               │
  │   SOLID-   │            [P2]          │ ● T4  middle  │   ACCUMULATOR
  │   FUEL     │          boiler loop     │               │   500 L
  │   BOILER   │        (return protect)  │ ● T5  bottom  │◄───────────────────  from HOME (return)
  └─────┬──────┘              │           └───────┬───────┘
        │                     ▼                   │
        └──────────●──────────┴───────[P1]◄───────┘
                  T2 (return)
```

### 3.2 I/O

| ID | Type | Description |
|---|---|---|
| T1 | DS18B20 | Boiler flow |
| T2 | DS18B20 | Boiler return |
| T3 / T4 / T5 | DS18B20 | Accumulator top / middle / bottom |
| T6 | DS18B20 | Supply to home, after accumulator (display/monitoring) |
| P1 | Relay | Charging pump, accumulator bottom → boiler return |
| P2 | Relay | Boiler bypass pump (cold-return protection) |
| P3 | Relay | Supply pump, accumulator top → home |

The boiler has its own burn regulator; the controller handles pumps only.

### 3.3 P1 — accumulator charging

```
P1 ON   when  T1 > T3 + ΔON    AND  T1 > T1_min
P1 OFF  when  T1 < T3 + ΔOFF   OR   T1 < T1_min − T1_min_hyst
```

| Setting | Default |
|---|---|
| ΔON | 5 °C |
| ΔOFF | 2 °C |
| T1_min | 60 °C |
| T1_min_hyst | 2 °C |

**Overheat:** when T1 > 90 °C, P1 is forced ON and an alarm is raised. The alarm clears automatically once T1 < 90 − 3 °C. Both values are configurable.

### 3.4 P2 — cold-return protection (independent of P1)

```
P2 OFF  when  T2 ≥ T2_off
P2 ON   when  T2 < T2_off − T2_hyst   AND  T1 > T1_burn
```

| Setting | Default | Range |
|---|---|---|
| T2_off | 60 °C | 55–65 °C |
| T2_hyst | 3 °C | 1–5 °C |
| T1_burn | 40 °C | configurable — P2 only runs while the boiler is burning |

### 3.5 P3 — supply to home (demand / offer logic)

Input from HA: switch **"Home: no need"**, set by an HA automation from the home controller's "no need" signal.

```
NORMAL (P3 ON) ──flag set──► OFF (P3 OFF)
OFF ──T3 ≥ T3_offer AND wait elapsed──► OFFER:
      P3 ON, boiler CLEARS the flag itself, ignores the flag for offer_window
OFFER ──flag stays cleared──► NORMAL
OFFER ──flag set again after offer_window──► OFF, start offer_wait timer
```

| Setting | Default |
|---|---|
| T3_offer | 60 °C |
| offer_window | 10 min |
| offer_wait (0 = no wait) | 60 min |

- **Flag expiry:** if MQTT or HA is lost, the flag is ignored and P3 works in NORMAL. After a reboot the saved flag is not applied until MQTT reconnects and HA sends the current value.
- **Anti-freeze (P3):** has its own enable switch on the web UI and in HA (**enabled by default**); there is no date range.
  - If P3 has been OFF for `af_interval` (default 30 min), P3 runs for `af_duration` (default 60 s).
  - It overrides the "no need" state and bypasses the relay lock.
  - The timer restarts whenever P3 runs for any reason.

### 3.6 Stored energy

```
E [kWh] = V × 1.163 Wh/(L·°C) × (avg(T3,T4,T5) − T_base) / 1000
```

| Setting | Default |
|---|---|
| V | 500 L |
| T_base | 30 °C |

The value is shown on the display, the web UI and in HA. Below T_base it is shown as 0 kWh (never negative).

### 3.7 Sensor failure (boiler)

The boiler loop has **gravity circulation**, so all relays OFF is safe for short restarts and updates. A sensor fault can last much longer, so the rules below apply while it lasts.

| Fault | Behaviour |
|---|---|
| **T1** (boiler flow) | P1 **forced ON**: overheat can't be seen, so heat removal comes first |
| **T2** (boiler return) | P2 ON **only while T1 > T1_burn** (boiler burning) |
| **T1 + T2** | P1 ON and P2 ON (boiler state unknown) |
| **T3** (accumulator top) | P1 follows **T1 alone**: ON when T1 > T1_min, OFF when T1 < T1_min − hyst; overheat override stays active. P3 offer logic disabled → P3 NORMAL. |
| **T4 / T5** | No effect on pumps. Stored energy uses the working sensors and is marked "estimated"; "n/a" if T3, T4 and T5 have all failed. |
| **T6** | No effect on pumps (display only). Diagnostic B1 is paused. |
| **Any fault** | Alarm + event log entry + OLED alarm page + HA. |

### 3.8 Display pages (rotating)

| Page | Content |
|---|---|
| 1 | T1, T2, P1, P2 |
| 2 | T3, stored energy (kWh) |
| 3 | T6, P3 |
| 4 | Wi-Fi, MQTT status, IP address |
| 5 | Sensors: found on bus / assigned / missing (T1–T6 status) |
| 6 | Alarms |

---

## 4. Home controller

### 4.1 Hydraulic diagram

```
  FROM BOILER ROOM  (P3, T6)
  SUPPLY ═════════╦════════════════════════════════╗
                  ║ 1                              ●  H3  hot supply in (K1.3)
                  ║                                ║ 3
            ┌─────╨──────┐                  ┌──────╨───────┐        2
            │     K2     │                  │      K1      ├◄───●──────────────┐
            │  diverter  │                  │ mixing valve │   H1  radiator     │
            └──┬──────┬──┘                  │  3-point     │   return (K1.2)    │
              2│     3│ short pipe          └──────┬───────┘                    │
               ▼      │ (bypass)                  1│ mixed water                │
       ┌────────────┐ │                          [P4]  home pump                │
       │  DHW tank  │ │                            │                            │
       │ ● H4       │ │                            ●  H2  radiator supply       │
       │   (coil)   │ │                           ─┴─ check valve               │
       └─────┬──────┘ │                            ▼                            │
             │        │                     ┌─────────────┐                     │
             │        │                     │  RADIATORS  │                     │
             │        │                     └──────┬──────┘                     │
             │        │                            ├────────────────────────────┘
  RETURN ════╩════════╩════════════════════════════╩════════════════════════════════
  TO BOILER ROOM  (accumulator bottom)
```

Heating priority over DHW is **hydraulic** — no firmware logic.

### 4.2 I/O

| ID | Type | Description |
|---|---|---|
| H1 | DS18B20 | Radiator return (K1 port 2) |
| H2 | DS18B20 | Mixed supply after P4 (controlled value) |
| H3 | DS18B20 | Hot supply from boiler room (K1 port 3) |
| H4 | DS18B20 | DHW tank |
| R1 | Relay | K2 diverter. De-energized: 1-2 → **tank**. Energized: 1-3 → **bypass**. |
| R2 | Relay | K1 motor power |
| R3 | Relay | K1 direction: NO → OPEN, NC → CLOSE (de-energized = CLOSE). This wiring makes OPEN and CLOSE at the same time physically impossible. Direction is changed only while R2 is OFF, with a short pause. |
| R4 | Relay | P4 home pump |

### 4.3 Heating enable and setpoint

- **Heating enabled:** a switch on the web UI and in HA (turned off in summer). When it is off, P4 stays OFF and K1 stays closed.
- **H2_set:** the target radiator supply temperature, set on the web UI and in HA. Default **40 °C**, range **30–75 °C**.

### 4.4 P4 — home pump

```
P4 ON   when  heating enabled  AND  H3 ≥ H2_set
P4 OFF  when  H3 < H2_set continuously for p4_off_delay   OR  heating disabled
```

| Setting | Default |
|---|---|
| p4_off_delay | 5 min |

### 4.5 K1 — 3-point mixing valve (120 s full travel)

1. **Calculate (feed-forward):**
   - `x = (H2_set − H1) / (H3 − H1)`, clamped to 0…100 %.
   - K1 is driven to x %, with its position estimated from run time.
2. **Correct (feedback):**
   - Every `k1_period`, compare H2 with H2_set.
   - If |error| > `k1_deadband`, send an OPEN or CLOSE pulse (set direction on R3, then R2 ON for the pulse) whose length is proportional to the error (`k1_gain` s/°C, capped at `k1_max_pulse`).
3. **Recalibrate:** full CLOSE (travel + margin) on boot and whenever P4 stops, giving a known 0 % position.
4. **Details:**
   - If H3 − H1 ≤ 2 °C, K1 goes fully open (nothing to mix).
   - The minimum pulse is 1 s (shorter pulses are skipped).
   - The pause between direction changes is 1 s.
   - When the estimate reaches 0 % or 100 %, K1 drives 10 % of the travel extra into the end stop to resync.

| Setting | Default |
|---|---|
| k1_travel | 120 s |
| k1_period | 30 s |
| k1_deadband | 1 °C |
| k1_gain | 2 s per °C of error |
| k1_max_pulse | 10 s |

Example: H2_set = 50 °C, H2 = 46 °C → error 4 °C → OPEN pulse 4 × 2 = 8 s (≈ 7 % of travel). The next check is 30 s later, which gives the water time to reach H2. The defaults are a starting point and get tuned on the real system.

### 4.6 K2 — DHW tank charging

```
To TANK    when  H3 > H4 + Δ   AND  H3 ≥ H3_min   AND  H4 < H4_max
To BYPASS  otherwise   (tank charging resumes when H4 ≤ H4_max − H4_hyst)
```

| Setting | Default |
|---|---|
| Δ | 3 °C (hysteresis 2 °C → stop at H3 ≤ H4 + 1) |
| H3_min | 65 °C (hysteresis 3 °C → stop below 62) |
| H4_max | 70 °C |
| H4_hyst | 3 °C |

### 4.7 "No need" signal to HA

- **ON** when K2 is on BYPASS **AND** P4 is OFF (after its delay).
- **OFF** as soon as K2 goes to the tank or P4 starts.
- Published as an HA binary sensor. An HA automation copies it to the boiler controller's "Home: no need" switch.

### 4.8 Sensor failure (home)

The goal is to keep the house heated whenever possible, so a single failed sensor switches K1 to a degraded mode instead of stopping the heating. All of these modes require heating to be enabled, and every fault raises an alarm.

| Fault | Action |
|---|---|
| H3 (hot supply) | K1 moves to a fixed start position of 30 % (configurable). P4 is forced ON. K1 then runs on **feedback only**, from H2. |
| H2 (radiator supply) | P4 runs by its normal H3 rule. K1 runs on **feed-forward only**: `x = (H2_set − H1) / (H3 − H1)`, with no correction. |
| H1 (radiator return) | Feed-forward is skipped. K1 runs on feedback only, from H2. |
| H4 (DHW tank) | K2 → bypass. |
| Two or more of H1/H2/H3 | K1 goes to a fixed 30 % and P4 ON if H3 is OK, or OFF if H3 has failed. |

### 4.9 Display pages (rotating)

| Page | Content |
|---|---|
| 1 | H2, H3, K1 %, P4 |
| 2 | H3, H4, K2 |
| 3 | Wi-Fi, MQTT status, IP address |
| 4 | Sensors: found on bus / assigned / missing (H1–H4 status) |
| 5 | Alarms |

---

## 5. Common features (both controllers)

### Relay protection

- **Minimum ON / minimum OFF time:** 60 s by default, configurable (0–600 s).
  - Applies to the pumps and to K2.
  - K1 uses its own rule instead: direction (R3) changes only while power (R2) is OFF, with a short pause.
- **Bypassed by:** overheat, sensor-fault safe states and anti-freeze.
- Delayed or blocked switching is recorded in the event log, once per delay.

### Anti-seize

- Any pump or valve idle for `as_interval` gets exercised:
  - pumps run for `as_duration`
  - K2 is toggled
  - K1 makes a full stroke, **only while P4 is OFF**
- Can be enabled per output. The runs start at `as_time`.
- Without a valid clock it uses 7 days of uptime instead; a run that was blocked happens at the next opportunity.

| Setting | Default |
|---|---|
| as_interval | 7 days |
| as_duration | 30 s |
| as_time | 10:00 |

### Sensor assignment (web UI)

- **Scan:** all DS18B20s on the single 1-Wire bus are listed with their address and live temperature. You can identify a sensor by warming it in your hand.
- **Assign:** a dropdown maps each physical address to a logical sensor (T1–T6 or H1–H4). The mapping is saved in flash.
- New sensors that are not assigned yet are flagged. Assigned sensors that go missing raise an alarm.
- There is no per-sensor calibration offset.
- All sensors are read every 2 s (12-bit). A sensor counts as faulty after 3 bad readings in a row (−127, missing, or 85.0 °C right after power-up) and recovers after 3 good ones.

### Event log

- The last **50 events** are kept in flash and shown in the web UI.
- **Events recorded:**
  - relay ON/OFF changes with the reason
  - alarms raised and cleared
  - sensor faults
  - Wi-Fi/MQTT connects and disconnects
  - config changes
  - reboots
  - anti-seize and anti-freeze runs
  - relay-lock delays
- The same event from the same source is written at most once a minute.
- Each event is also published to HA as a non-retained `<prefix>/event` message.

### Time

- The **DS1307 RTC is the main clock** (stored in UTC). It is corrected from **NTP** on connect and every 24 h.
- Time zone configurable, default Europe/Kyiv with automatic summer/winter time.
- Without a valid RTC or NTP, events are stamped with uptime and marked as such.

### Network

- **Wi-Fi client** on the local network.
- **AP setup mode** only when **no Wi-Fi is saved** (first start or after a factory reset). The AP is open (`BoilerRoom-Setup` / `HomeHeating-Setup`). While in setup mode, the OLED shows the AP name, "open, no password" and `192.168.4.1`. If the saved network is down, the controller just keeps retrying in the background.
- **Wi-Fi settings page** in the normal web UI (admin), so the network can be changed before changing the router.
- **Names:** `boiler-room.local` / `home-heating.local` (mDNS). A DHCP reservation on the router is recommended for a fixed address.
- **Signal strength** on the OLED network page and in HA; disconnects longer than 30 s are logged.
- **MQTT** with **Home Assistant auto-discovery**:
  - sensors, relay states and alarms are published
  - HA switches: settings, "Heating enabled", "Home: no need", "Anti-freeze enable"
- **LWT** availability topic `<prefix>/status` (online/offline, retained). Broker host/port/user/password set on the web UI.
- States retained, published on change plus a full refresh every 60 s.
- HA entity types: temperatures/energy/RSSI as `sensor`; pumps, valves, alarms, warnings, no-need as `binary_sensor`; the three flags as `switch`; every setting as `number` with `entity_category: config`.
- The control logic keeps running with Wi-Fi, MQTT or HA offline.

### Web UI

- **Login:** `admin/admin` by default, changeable in the web UI. Session cookie valid 24 h; no separate API token.
- **Language:** English and Ukrainian, with a switch in the navigation bar (remembered in the browser, default English). The OLED and HA entity names stay English.
- The status page refreshes every 2 s.
- **Pages:**
  - Status
  - Settings (all thresholds)
  - Sensors (assignment)
  - Event log
  - Network/MQTT
  - System (time, OTA, reboot)

### Web UI visual style

The web UI copies the look and structure of the existing `D:/home/boiler/data/` UI. Its stylesheet has been copied to `common/web/style.css` as the shared base for both controllers.

- **Technology:** a vanilla-JS SPA (`index.html` + `app.js` + `style.css`) with no framework. It is served from LittleFS (`data/`) and talks to the controller over a REST API (`/api/state`, `/api/log`, `/api/config`, …), refreshing periodically.
- **Theme:** dark only, with a system font stack at a 14 px base size.

| Token | Color | Use |
|---|---|---|
| Background | `#0f1117` | page, temperature tiles, inputs |
| Surface | `#1a1d2e` | nav, cards |
| Border | `#2d3250` | card and input borders |
| Divider | `#1e2235` | table and relay rows |
| Text | `#e2e8f0` | primary text |
| Muted | `#94a3b8` / `#64748b` / `#475569` | secondary text, labels, hints |
| Accent | `#f97316` | brand, primary button, active tab |
| OK / ON | `#22c55e` | running pump, connected |
| Info / cool | `#60a5fa` | cool temperatures, valve/bypass states |
| Warning | `#f59e0b` | diagnostics warnings |
| Error / hot | `#ef4444` | alarms, faults, hot temperatures |

- **Existing components that are reused:**
  - top `nav` with the brand and page links
  - `.card` with an uppercase `h3` heading
  - `.grid-2/3/4` responsive grids
  - `.temp-card`, with value classes `hot / warm / normal / cool / na`
  - `.relay-row` with `.relay-dot.on`
  - `.status-pill` for Wi-Fi, MQTT and time status
  - `.alarm-banner`
  - `.log-table` with `.relay-chip`
  - `.tabs` for settings sections
  - `.form-section`, `.form-group` and `.form-grid`
  - `.btn-primary / -secondary / -danger`
  - `.auth-card` for the login page
  - `.pw-wrap` for show/hide on password fields
- **New components needed:**
  - **Sensor assignment table:** address, live temperature and an assignment dropdown, with badges for new and missing sensors, and a Scan button.
  - **Valve position bar:** K1 position in %, with OPEN/CLOSE activity shown.
  - **Accumulator view:** T3, T4 and T5 stacked, with the stored energy in kWh.
  - **Diagnostic warnings:** amber `.warn-banner` / chips, shown next to the red alarm banner.
  - **Mode states:** P3 NORMAL / OFF / OFFER, K2 TANK / BYPASS, anti-freeze and anti-seize runs, shown with badges like the old `.phase-badge`.
- **Not carried over:**
  - the BLE/API sensor source forms, curve editor, display button panel and test page
  - the old first-boot `/setup` flow: the default login here is `admin/admin`, changeable on the web UI

### OTA

- Firmware upload through the web UI, plus PlatformIO network upload (espota).

### Display

- SSD1306 pages rotate automatically, every **5 s** by default (configurable).
- On a new alarm the display jumps to the alarm page for about 30 s, then resumes rotation; the alarm page stays in the rotation while an alarm is active.
- Burn-in protection: content shifts 1–2 px each rotation cycle; reduced brightness by default (configurable).

---

## 6. Diagnostics (pump response checks)

Each check compares a pump command with the expected temperature response. A failed check raises a **warning** only: it goes to the event log and is published as an HA binary sensor. It never changes the control logic.

- Each check can be enabled or disabled on the web UI.
- Every threshold and time window below is a configurable default.
- Checks are paused while a sensor they use is faulty.
- A warning clears automatically once the condition is no longer true.
- Each sensor keeps a short history for trend calculations: the last 60 min, one sample every 30 s, in RAM only.
- All four checks are enabled by default.

| ID | Controller | Warning | Condition (all must be true) | Likely cause |
|---|---|---|---|---|
| **B1** | Boiler | P3 no flow | P3 ON ≥ 3 min · T3 − T6 > 15 °C · T6 rose < 2 °C since P3 started | Pump stuck, air lock, closed valve, fuse/relay |
| **B3** | Boiler | P1 not charging | P1 ON ≥ 10 min · T1 > T3 + 10 °C · T3 rose < 1 °C in that time | P1 stuck, air, blocked filter |
| **B6** | Boiler | P2 no effect | P2 ON ≥ 5 min · T1 > T2 + 10 °C · T2 rose < 2 °C since P2 started | P2 stuck, relay fault |
| **H1** | Home | P4 no flow | P4 ON ≥ 5 min · K1 position > 30 % · H3 > H1 + 10 °C · H2 − H1 < 2 °C | P4 stuck, air in radiators |

## 7. Open items

- Tune the K1 correction (`k1_gain`, `k1_max_pulse`, `k1_period`) on the real system, using the K1 tuning tools:
  - K1 position, H2 error and pulses sent to HA
  - a daily pulse counter
  - a step test on the web page that measures dead time/response and suggests settings (applied only by the user)
  - a tuning guide in `docs/`
- Write the HA automation that links the home "no need" signal to the boiler's "Home: no need" switch (YAML example provided during implementation).
