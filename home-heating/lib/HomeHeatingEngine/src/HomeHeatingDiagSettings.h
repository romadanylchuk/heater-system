#pragma once
#include <stddef.h>
#include <stdint.h>
#include <SettingDescriptor.h>
#include "P4FlowCheck.h"

// home-heating diagnostics settings (stage 09, C10): the H1 P4 no-flow check
// and the K1 step-test pulse. Appended LAST (tables[5]) to HOME_HEATING_SCHEMA
// so every earlier index stays put (additive, config version unchanged, D20).
// Key == nvsKey (<= 15 chars) in every row. The enable is Bool without
// SETTING_FLAG_HA_SWITCH, so it becomes a config-category HA switch (D20).
// Ranges are the planner's; k1StepPulse sits in group "k1" next to the tuning
// parameters.
constexpr const char* HH_KEY_H1_EN = "h1En";
constexpr const char* HH_KEY_H1_MIN_ON = "h1MinOn";
constexpr const char* HH_KEY_H1_K1_MIN = "h1K1Min";
constexpr const char* HH_KEY_H1_DELTA = "h1Delta";
constexpr const char* HH_KEY_H1_MIN_DIFF = "h1MinDiff";
constexpr const char* HH_KEY_K1_STEP_PULSE = "k1StepPulse";

struct HomeHeatingDiagSettings {
    P4FlowParams h1;
    uint32_t k1StepPulseS;
};

inline constexpr SettingDescriptor HOME_HEATING_DIAG_SETTINGS[] = {
    boolSetting("h1En", "h1En", "Diag H1: P4 no-flow check", "Діаг. H1: немає потоку P4", "diag", true),
    intSetting("h1MinOn", "h1MinOn", "H1: P4 on at least", "H1: P4 увімкнений щонайменше",
        "min", "diag", 1, 60, 5),
    intSetting("h1K1Min", "h1K1Min", "H1: K1 open above", "H1: K1 відкритий більше",
        "%", "diag", 5, 95, 30),
    floatSetting("h1Delta", "h1Delta", "H1: H3 − H1 above", "H1: H3 − H1 більше",
        "°C", "diag", 3.0f, 40.0f, 10.0f, 0.5f),
    floatSetting("h1MinDiff", "h1MinDiff", "H1: H2 − H1 below", "H1: H2 − H1 менше",
        "°C", "diag", 0.5f, 10.0f, 2.0f, 0.5f),
    intSetting("k1StepPulse", "k1StepPulse", "K1 step-test pulse", "Імпульс тесту K1",
        "s", "k1", 2, 30, 10),
};
inline constexpr SettingsTable HOME_HEATING_DIAG_SETTINGS_TABLE = makeTable(HOME_HEATING_DIAG_SETTINGS);

constexpr size_t HOME_HEATING_DIAG_SETTING_COUNT = 6;
static_assert(sizeof(HOME_HEATING_DIAG_SETTINGS) / sizeof(HOME_HEATING_DIAG_SETTINGS[0]) ==
        HOME_HEATING_DIAG_SETTING_COUNT,
    "HOME_HEATING_DIAG_SETTING_COUNT out of sync with HOME_HEATING_DIAG_SETTINGS");

// The descriptor defaults as a diag settings struct (read from the table rows).
HomeHeatingDiagSettings defaultHomeHeatingDiagSettings();
