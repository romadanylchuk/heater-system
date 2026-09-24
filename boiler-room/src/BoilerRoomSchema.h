#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonSettings.h>
#include <ConfigSchema.h>
#include <SettingDescriptor.h>

// boiler-room's minimal settings schema (stage 02): the common table plus the
// persisted HA switch "homeNoNeed" (boiler-room P3, boiler-room-p3-supply). The
// "heatingEnabled" switch belongs to home-heating (P4) and lives in HomeHeatingSchema.h.
// Real controller settings/AppState land in stage 07. Nothing is deployed yet, so the
// config version stays 1 (no migration for the removed key).
constexpr uint16_t BOILER_ROOM_CONFIG_VERSION = 1;

enum class BoilerRoomSetting : uint16_t {
    HomeNoNeed = COMMON_SETTING_COUNT,
};

inline constexpr SettingDescriptor BOILER_ROOM_SETTINGS[] = {
    boolSetting("homeNoNeed", "homeNoNeed", "Home: no need", "Дім: не потрібно", "flags", false,
        SETTING_FLAG_HA_SWITCH),
};

inline constexpr SettingsTable BOILER_ROOM_TABLES[] = {COMMON_SETTINGS_TABLE, makeTable(BOILER_ROOM_SETTINGS)};
inline constexpr ConfigSchema BOILER_ROOM_SCHEMA = {
    "boiler-room", BOILER_ROOM_CONFIG_VERSION, BOILER_ROOM_TABLES, 2, nullptr, 0};
