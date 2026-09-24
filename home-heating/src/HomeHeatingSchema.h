#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonSettings.h>
#include <ConfigSchema.h>
#include <SettingDescriptor.h>

// home-heating's minimal settings schema (stage 02): the common table plus the
// persisted HA switch "heatingEnabled" (home-heating P4, home-heating-p4: OFF -> P4 OFF
// and K1 closed; default ON). Real controller settings/AppState land in stage 08.
// Nothing is deployed yet, so the config version stays 1 (no migration needed).
constexpr uint16_t HOME_HEATING_CONFIG_VERSION = 1;

enum class HomeHeatingSetting : uint16_t {
    HeatingEnabled = COMMON_SETTING_COUNT,
};

inline constexpr SettingDescriptor HOME_HEATING_SETTINGS[] = {
    boolSetting("heatingEnabled", "heatingEn", "Heating enabled", "Опалення увімкнено", "flags", true,
        SETTING_FLAG_HA_SWITCH),
};

inline constexpr SettingsTable HOME_HEATING_TABLES[] = {COMMON_SETTINGS_TABLE, makeTable(HOME_HEATING_SETTINGS)};
inline constexpr ConfigSchema HOME_HEATING_SCHEMA = {
    "home-heating", HOME_HEATING_CONFIG_VERSION, HOME_HEATING_TABLES, 2, nullptr, 0};
