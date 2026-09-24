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
│   │   └── RelayBoot/          Arduino-only: writes the relay all-OFF byte at boot
│   ├── web/                    shared web UI sources (style.css, lang/); assembled into <project>/data/
│   ├── scripts/                PlatformIO pre-scripts (fw_version.py, web_assemble.py)
│   └── test/                   CommonSuite.h — native tests shared by both projects
├── boiler-room/                PlatformIO project (src/, web/, test/)
└── home-heating/                PlatformIO project (src/, web/, test/)
```

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
