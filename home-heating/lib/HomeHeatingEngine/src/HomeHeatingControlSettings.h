#pragma once
#include <stddef.h>
#include <stdint.h>
#include <SettingDescriptor.h>

// home-heating controller settings (stage 08, C2): H2 setpoint + P4 off-delay,
// K1 mixing valve and K2 DHW diverter. Appended LAST (tables[4]) to
// HOME_HEATING_SCHEMA so every earlier index stays put (additive, config
// version unchanged, no migration). Key == nvsKey (<= 15 chars) in every row.
// Ranges were chosen by the planner (D19); defaults are the architecture's.
constexpr const char* HH_KEY_H2_SET = "h2Set";
constexpr const char* HH_KEY_P4_OFF_DELAY = "p4OffDelay";
constexpr const char* HH_KEY_K1_TRAVEL = "k1Travel";
constexpr const char* HH_KEY_K1_PERIOD = "k1Period";
constexpr const char* HH_KEY_K1_DEADBAND = "k1Deadband";
constexpr const char* HH_KEY_K1_GAIN = "k1Gain";
constexpr const char* HH_KEY_K1_MAX_PULSE = "k1MaxPulse";
constexpr const char* HH_KEY_K1_MIN_PULSE = "k1MinPulse";
constexpr const char* HH_KEY_K1_RESYNC = "k1Resync";
constexpr const char* HH_KEY_K1_SMALL_DIFF = "k1SmallDiff";
constexpr const char* HH_KEY_K1_FAIL_POS = "k1FailPos";
constexpr const char* HH_KEY_K1_FF_STEP = "k1FfStep";
constexpr const char* HH_KEY_K2_DELTA = "k2Delta";
constexpr const char* HH_KEY_K2_DELTA_HYST = "k2DeltaHyst";
constexpr const char* HH_KEY_K2_H3_MIN = "k2H3Min";
constexpr const char* HH_KEY_K2_H3_MIN_HYST = "k2H3MinHyst";
constexpr const char* HH_KEY_K2_H4_MAX = "k2H4Max";
constexpr const char* HH_KEY_K2_H4_MAX_HYST = "k2H4MaxHyst";
constexpr const char* HH_KEY_HEATING_ENABLED = "heatingEnabled";  // existing setting (HOME_HEATING_SETTINGS)

struct HomeHeatingSettings {
    bool heatingEnabled;                 // from the existing "heatingEnabled" row
    float h2Set;
    uint32_t p4OffDelayMin;
    uint32_t k1TravelS, k1PeriodS;
    float k1Deadband, k1Gain;
    uint32_t k1MaxPulseS;
    float k1MinPulseS;
    uint32_t k1ResyncPct;
    float k1SmallDiff;
    uint32_t k1FailPosPct, k1FfStepPct;
    float k2Delta, k2DeltaHyst, k2H3Min, k2H3MinHyst, k2H4Max, k2H4MaxHyst;
};

inline constexpr SettingDescriptor HOME_HEATING_CONTROL_SETTINGS[] = {
    floatSetting("h2Set", "h2Set", "Radiator supply setpoint H2", "Уставка подачі радіаторів H2",
        "°C", "heating", 30.0f, 75.0f, 40.0f, 0.5f),
    intSetting("p4OffDelay", "p4OffDelay", "P4 off delay", "Затримка вимкнення P4",
        "min", "heating", 0, 60, 5),
    intSetting("k1Travel", "k1Travel", "K1 full travel time", "Повний хід K1",
        "s", "k1", 30, 300, 120),
    intSetting("k1Period", "k1Period", "K1 control period", "Період регулювання K1",
        "s", "k1", 10, 300, 30),
    floatSetting("k1Deadband", "k1Deadband", "K1 deadband", "Зона нечутливості K1",
        "°C", "k1", 0.2f, 5.0f, 1.0f, 0.1f),
    floatSetting("k1Gain", "k1Gain", "K1 gain", "Коефіцієнт K1",
        "s/°C", "k1", 0.5f, 10.0f, 2.0f, 0.5f),
    intSetting("k1MaxPulse", "k1MaxPulse", "K1 max pulse", "Макс. імпульс K1",
        "s", "k1", 1, 60, 10),
    floatSetting("k1MinPulse", "k1MinPulse", "K1 min pulse", "Мін. імпульс K1",
        "s", "k1", 0.5f, 5.0f, 1.0f, 0.5f),
    intSetting("k1Resync", "k1Resync", "K1 end-stop overdrive", "Додатковий хід K1 в упор",
        "%", "k1", 5, 25, 10),
    floatSetting("k1SmallDiff", "k1SmallDiff", "K1 open fully if H3−H1 below",
        "K1 повністю відкритий, якщо H3−H1 менше", "°C", "k1", 0.5f, 10.0f, 2.0f, 0.5f),
    intSetting("k1FailPos", "k1FailPos", "K1 fail-safe position", "Аварійне положення K1",
        "%", "k1", 0, 100, 30),
    intSetting("k1FfStep", "k1FfStep", "K1 feed-forward re-apply step", "Крок повторного прямого керування K1",
        "%", "k1", 1, 25, 5),
    floatSetting("k2Delta", "k2Delta", "DHW: charge if H3 above H4 by", "ГВП: нагрів, якщо H3 вище H4 на",
        "°C", "k2", 1.0f, 15.0f, 3.0f, 0.5f),
    floatSetting("k2DeltaHyst", "k2DeltaHyst", "DHW differential hysteresis", "Гістерезис диференціалу ГВП",
        "°C", "k2", 0.5f, 10.0f, 2.0f, 0.5f),
    intSetting("k2H3Min", "k2H3Min", "DHW: minimum supply H3", "ГВП: мінімальна подача H3",
        "°C", "k2", 40, 80, 65),
    floatSetting("k2H3MinHyst", "k2H3MinHyst", "DHW H3 minimum hysteresis", "Гістерезис мінімуму H3 ГВП",
        "°C", "k2", 1.0f, 10.0f, 3.0f, 0.5f),
    intSetting("k2H4Max", "k2H4Max", "DHW: tank maximum H4", "ГВП: максимум бака H4",
        "°C", "k2", 40, 80, 70),
    floatSetting("k2H4MaxHyst", "k2H4MaxHyst", "DHW H4 maximum hysteresis", "Гістерезис максимуму H4 ГВП",
        "°C", "k2", 1.0f, 10.0f, 3.0f, 0.5f),
};
inline constexpr SettingsTable HOME_HEATING_CONTROL_SETTINGS_TABLE = makeTable(HOME_HEATING_CONTROL_SETTINGS);

constexpr size_t HOME_HEATING_CONTROL_SETTING_COUNT = 18;
static_assert(sizeof(HOME_HEATING_CONTROL_SETTINGS) / sizeof(HOME_HEATING_CONTROL_SETTINGS[0]) ==
        HOME_HEATING_CONTROL_SETTING_COUNT,
    "HOME_HEATING_CONTROL_SETTING_COUNT out of sync with HOME_HEATING_CONTROL_SETTINGS");

// The descriptor defaults as a settings struct (read from the table rows);
// heatingEnabled = true (the existing row's default).
HomeHeatingSettings defaultHomeHeatingSettings();

// Ordering guard, applied every tick before the logic sees the values:
// k2DeltaHyst <= k2Delta - 0.5 (k2Delta >= 1, so the result stays >= 0.5) and
// k1MaxPulseS >= ceil(k1MinPulseS). Everything else is returned unchanged.
HomeHeatingSettings guardHomeHeatingSettings(HomeHeatingSettings s);

uint32_t k1TravelMs(const HomeHeatingSettings& s);     // k1TravelS * 1000
uint32_t k1OverdriveMs(const HomeHeatingSettings& s);  // travelMs * k1ResyncPct / 100
uint32_t k1RecalMs(const HomeHeatingSettings& s);      // travelMs + overdriveMs (also the anti-seize stroke, D16)
