#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonSettings.h>
#include <ConfigSchema.h>
#include <DisplaySettings.h>
#include <HwSettings.h>
#include <SettingDescriptor.h>

// home-heating's settings schema: the common table, the shared HW table (relay
// lock + anti-seize interval/time/duration, stage 03), and home-heating's own
// table -- the persisted HA switch "heatingEnabled" (home-heating P4: OFF -> P4
// OFF and K1 closed; default ON), the H1..H4 sensor-address mappings and the
// P4/K2/K1 anti-seize enables. Real controller settings/AppState land in stage
// 08. Nothing is deployed yet, so the config version stays 1: the HW table and
// the new project keys are purely additive, and
// HomeHeatingSetting::HeatingEnabled shifts from index COMMON_SETTING_COUNT to
// HW_SETTINGS_END, which only reads through the enum, never a stored literal.
constexpr uint16_t HOME_HEATING_CONFIG_VERSION = 1;

enum class HomeHeatingSetting : uint16_t {
    HeatingEnabled = HW_SETTINGS_END,
    SensorH1,
    SensorH2,
    SensorH3,
    SensorH4,
    AsEnP4,
    AsEnK2,
    AsEnK1,
};

inline constexpr SettingDescriptor HOME_HEATING_SETTINGS[] = {
    boolSetting("heatingEnabled", "heatingEn", "Heating enabled", "Опалення увімкнено", "flags", true,
        SETTING_FLAG_HA_SWITCH),
    textSetting("sensorH1", "sensH1", "Sensor H1 address", "Адреса датчика H1", "sensors", 0, 16, ""),
    textSetting("sensorH2", "sensH2", "Sensor H2 address", "Адреса датчика H2", "sensors", 0, 16, ""),
    textSetting("sensorH3", "sensH3", "Sensor H3 address", "Адреса датчика H3", "sensors", 0, 16, ""),
    textSetting("sensorH4", "sensH4", "Sensor H4 address", "Адреса датчика H4", "sensors", 0, 16, ""),
    boolSetting("asEnP4", "asEnP4", "Anti-seize P4", "Антизаклинювання P4", "antiSeize", true),
    boolSetting("asEnK2", "asEnK2", "Anti-seize K2", "Антизаклинювання K2", "antiSeize", true),
    boolSetting("asEnK1", "asEnK1", "Anti-seize K1", "Антизаклинювання K1", "antiSeize", true),
};

// The display table (stage 06) is appended LAST so project enum indices never
// shift; purely additive, config version unchanged, no migration.
inline constexpr SettingsTable HOME_HEATING_TABLES[] = {
    COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE, makeTable(HOME_HEATING_SETTINGS), DISPLAY_SETTINGS_TABLE};
inline constexpr ConfigSchema HOME_HEATING_SCHEMA = {
    "home-heating", HOME_HEATING_CONFIG_VERSION, HOME_HEATING_TABLES, 4, nullptr, 0};
