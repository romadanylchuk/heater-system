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
│   │   ├── HwEngine/           pure, native-testable relay/K1/DS18B20/anti-seize logic (see "Hardware services" below)
│   │   └── HwEsp32/            ESP32-only adapters for HwEngine (PCF8574, OneWire/DallasTemperature)
│   ├── web/                    shared web UI sources (style.css, lang/); assembled into <project>/data/
│   ├── scripts/                PlatformIO pre-scripts (fw_version.py, web_assemble.py)
│   └── test/                   CommonSuite.h — native tests shared by both projects
├── boiler-room/                PlatformIO project (src/, web/, test/)
└── home-heating/                PlatformIO project (src/, web/, test/)
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
