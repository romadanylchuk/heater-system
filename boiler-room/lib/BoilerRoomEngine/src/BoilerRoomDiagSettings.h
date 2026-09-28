#pragma once
#include <stddef.h>
#include <stdint.h>
#include <SettingDescriptor.h>
#include "PumpRiseCheck.h"

// boiler-room pump-response diagnostics settings (stage 09, C7): B1 (P3 no
// flow), B3 (P1 charging) and B6 (P2 effect). Appended LAST (tables[5]) to
// BOILER_ROOM_SCHEMA so every earlier index stays put (additive, config version
// unchanged, D20). Key == nvsKey (<= 15 chars) in every row. The enables are
// Bool without SETTING_FLAG_HA_SWITCH, so they become config-category HA
// switches (D20). Ranges are the planner's.
constexpr const char* BR_KEY_B1_EN = "b1En";
constexpr const char* BR_KEY_B1_MIN_ON = "b1MinOn";
constexpr const char* BR_KEY_B1_DELTA = "b1Delta";
constexpr const char* BR_KEY_B1_MIN_RISE = "b1MinRise";
constexpr const char* BR_KEY_B3_EN = "b3En";
constexpr const char* BR_KEY_B3_MIN_ON = "b3MinOn";
constexpr const char* BR_KEY_B3_DELTA = "b3Delta";
constexpr const char* BR_KEY_B3_MIN_RISE = "b3MinRise";
constexpr const char* BR_KEY_B6_EN = "b6En";
constexpr const char* BR_KEY_B6_MIN_ON = "b6MinOn";
constexpr const char* BR_KEY_B6_DELTA = "b6Delta";
constexpr const char* BR_KEY_B6_MIN_RISE = "b6MinRise";

struct BoilerRoomDiagSettings {
    RiseCheckParams b1, b3, b6;
};

inline constexpr SettingDescriptor BOILER_ROOM_DIAG_SETTINGS[] = {
    boolSetting("b1En", "b1En", "Diag B1: P3 no-flow check", "Діаг. B1: немає потоку P3", "diag", true),
    intSetting("b1MinOn", "b1MinOn", "B1: P3 on at least", "B1: P3 увімкнений щонайменше",
        "min", "diag", 1, 60, 3),
    floatSetting("b1Delta", "b1Delta", "B1: T3 − T6 above", "B1: T3 − T6 більше",
        "°C", "diag", 5.0f, 40.0f, 15.0f, 0.5f),
    floatSetting("b1MinRise", "b1MinRise", "B1: T6 rise below", "B1: зростання T6 менше",
        "°C", "diag", 0.5f, 10.0f, 2.0f, 0.5f),
    boolSetting("b3En", "b3En", "Diag B3: P1 charging check", "Діаг. B3: заряд P1", "diag", true),
    intSetting("b3MinOn", "b3MinOn", "B3: P1 on at least", "B3: P1 увімкнений щонайменше",
        "min", "diag", 1, 60, 10),
    floatSetting("b3Delta", "b3Delta", "B3: T1 − T3 above", "B3: T1 − T3 більше",
        "°C", "diag", 3.0f, 40.0f, 10.0f, 0.5f),
    floatSetting("b3MinRise", "b3MinRise", "B3: T3 rise below", "B3: зростання T3 менше",
        "°C", "diag", 0.5f, 10.0f, 1.0f, 0.5f),
    boolSetting("b6En", "b6En", "Diag B6: P2 effect check", "Діаг. B6: дія P2", "diag", true),
    intSetting("b6MinOn", "b6MinOn", "B6: P2 on at least", "B6: P2 увімкнений щонайменше",
        "min", "diag", 1, 60, 5),
    floatSetting("b6Delta", "b6Delta", "B6: T1 − T2 above", "B6: T1 − T2 більше",
        "°C", "diag", 3.0f, 40.0f, 10.0f, 0.5f),
    floatSetting("b6MinRise", "b6MinRise", "B6: T2 rise below", "B6: зростання T2 менше",
        "°C", "diag", 0.5f, 10.0f, 2.0f, 0.5f),
};
inline constexpr SettingsTable BOILER_ROOM_DIAG_SETTINGS_TABLE = makeTable(BOILER_ROOM_DIAG_SETTINGS);

constexpr size_t BOILER_ROOM_DIAG_SETTING_COUNT = 12;
static_assert(sizeof(BOILER_ROOM_DIAG_SETTINGS) / sizeof(BOILER_ROOM_DIAG_SETTINGS[0]) ==
        BOILER_ROOM_DIAG_SETTING_COUNT,
    "BOILER_ROOM_DIAG_SETTING_COUNT out of sync with BOILER_ROOM_DIAG_SETTINGS");

// The descriptor defaults as a diag settings struct (read from the table rows).
BoilerRoomDiagSettings defaultBoilerRoomDiagSettings();
