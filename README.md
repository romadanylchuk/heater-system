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
│   │   └── CoreEsp32/          ESP32-only adapters for CoreEngine (NVS, RTC, SNTP, watchdog, DI1)
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
