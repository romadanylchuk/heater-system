#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonSettings.h>
#include <ConfigSchema.h>
#include <DisplaySettings.h>
#include <HwSettings.h>
#include <SettingDescriptor.h>

// boiler-room's settings schema: the common table, the shared HW table (relay
// lock + anti-seize interval/time/duration, stage 03), and boiler-room's own
// table -- the persisted HA switch "homeNoNeed" (boiler-room P3), the T1..T6
// sensor-address mappings and the P1..P3 anti-seize enables. Real controller
// settings/AppState land in stage 07. Nothing is deployed yet, so the config
// version stays 1: the HW table and the new project keys are purely additive,
// and BoilerRoomSetting::HomeNoNeed shifts from index COMMON_SETTING_COUNT to
// HW_SETTINGS_END, which only reads through the enum, never a stored literal.
constexpr uint16_t BOILER_ROOM_CONFIG_VERSION = 1;

enum class BoilerRoomSetting : uint16_t {
    HomeNoNeed = HW_SETTINGS_END,
    SensorT1,
    SensorT2,
    SensorT3,
    SensorT4,
    SensorT5,
    SensorT6,
    AsEnP1,
    AsEnP2,
    AsEnP3,
};

inline constexpr SettingDescriptor BOILER_ROOM_SETTINGS[] = {
    boolSetting("homeNoNeed", "homeNoNeed", "Home: no need", "Дім: не потрібно", "flags", false,
        SETTING_FLAG_HA_SWITCH),
    textSetting("sensorT1", "sensT1", "Sensor T1 address", "Адреса датчика T1", "sensors", 0, 16, ""),
    textSetting("sensorT2", "sensT2", "Sensor T2 address", "Адреса датчика T2", "sensors", 0, 16, ""),
    textSetting("sensorT3", "sensT3", "Sensor T3 address", "Адреса датчика T3", "sensors", 0, 16, ""),
    textSetting("sensorT4", "sensT4", "Sensor T4 address", "Адреса датчика T4", "sensors", 0, 16, ""),
    textSetting("sensorT5", "sensT5", "Sensor T5 address", "Адреса датчика T5", "sensors", 0, 16, ""),
    textSetting("sensorT6", "sensT6", "Sensor T6 address", "Адреса датчика T6", "sensors", 0, 16, ""),
    boolSetting("asEnP1", "asEnP1", "Anti-seize P1", "Антизаклинювання P1", "antiSeize", true),
    boolSetting("asEnP2", "asEnP2", "Anti-seize P2", "Антизаклинювання P2", "antiSeize", true),
    boolSetting("asEnP3", "asEnP3", "Anti-seize P3", "Антизаклинювання P3", "antiSeize", true),
};

// The display table (stage 06) is appended LAST so project enum indices never
// shift; purely additive, config version unchanged, no migration.
inline constexpr SettingsTable BOILER_ROOM_TABLES[] = {
    COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE, makeTable(BOILER_ROOM_SETTINGS), DISPLAY_SETTINGS_TABLE};
inline constexpr ConfigSchema BOILER_ROOM_SCHEMA = {
    "boiler-room", BOILER_ROOM_CONFIG_VERSION, BOILER_ROOM_TABLES, 4, nullptr, 0};
