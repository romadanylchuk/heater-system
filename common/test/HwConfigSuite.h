#pragma once
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "../lib/CoreEngine/src/Command.h"
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/CoreEngine/src/ConfigSchema.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/CoreEngine/src/SettingDescriptor.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/HwEngine/src/HwSettings.h"

// Native-safe Unity tests for HW_SETTINGS, HwConfig's descriptor validation and
// the sensor/rescan Command builders (stage 03 phase 1). Header-only so both
// project test wrappers can include and run it via CommonSuite.h/HwSuite.h.
// Test names are prefixed hwcfg_ (D24).

static void hwcfg_test_settings_values() {
    TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(HW_SETTING_COUNT));
    TEST_ASSERT_EQUAL_UINT32(COMMON_SETTING_COUNT + 4, static_cast<uint32_t>(HW_SETTINGS_END));

    const SettingDescriptor& lock = HW_SETTINGS[static_cast<size_t>(HwSetting::RelayLock) - COMMON_SETTING_COUNT];
    TEST_ASSERT_EQUAL_STRING("relayLock", lock.key);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lock.minValue);
    TEST_ASSERT_EQUAL_FLOAT(600.0f, lock.maxValue);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, lock.defaultValue);

    const SettingDescriptor& interval =
        HW_SETTINGS[static_cast<size_t>(HwSetting::AsInterval) - COMMON_SETTING_COUNT];
    TEST_ASSERT_EQUAL_STRING("asInterval", interval.key);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, interval.minValue);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, interval.maxValue);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, interval.defaultValue);

    const SettingDescriptor& time = HW_SETTINGS[static_cast<size_t>(HwSetting::AsTime) - COMMON_SETTING_COUNT];
    TEST_ASSERT_EQUAL_STRING("asTime", time.key);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, time.minValue);
    TEST_ASSERT_EQUAL_FLOAT(1439.0f, time.maxValue);
    TEST_ASSERT_EQUAL_FLOAT(600.0f, time.defaultValue);

    const SettingDescriptor& duration =
        HW_SETTINGS[static_cast<size_t>(HwSetting::AsDuration) - COMMON_SETTING_COUNT];
    TEST_ASSERT_EQUAL_STRING("asDuration", duration.key);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, duration.minValue);
    TEST_ASSERT_EQUAL_FLOAT(300.0f, duration.maxValue);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, duration.defaultValue);
}

namespace {
inline constexpr SettingDescriptor HWCFG_TEST_PROJECT_SETTINGS[] = {
    boolSetting("testFlag", "testFlag", "Test flag", "Тест", "test", false),
};
inline constexpr SettingsTable HWCFG_TEST_PROJECT_TABLE = makeTable(HWCFG_TEST_PROJECT_SETTINGS);
inline constexpr SettingsTable HWCFG_TEST_SCHEMA_TABLES[] = {
    COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE, HWCFG_TEST_PROJECT_TABLE};
inline constexpr ConfigSchema HWCFG_TEST_SCHEMA = {"test-hw", 1, HWCFG_TEST_SCHEMA_TABLES, 3, nullptr, 0};
}  // namespace

static void hwcfg_test_schema_with_hw_table_validates() {
    TEST_ASSERT_TRUE(ConfigEngine::validateSchema(HWCFG_TEST_SCHEMA));
}

static void hwcfg_test_valid_pump_only_config() {
    static constexpr RelayChannelDesc relays[] = {{0, "P1", RelayRole::Pump, true, true}};
    static constexpr AntiSeizeOutputDesc antiSeize[] = {
        {"P1", AntiSeizeKind::Pump, 0, "asEnP1", NO_RELAY_CHANNEL}};
    static constexpr HwProjectConfig cfg = {
        relays, 1, nullptr, 0, antiSeize, 1, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};
    TEST_ASSERT_TRUE(validateHwConfig(cfg));
}

static void hwcfg_test_valid_k1_config() {
    static constexpr RelayChannelDesc relays[] = {
        {0, "K2", RelayRole::Diverter, true, true},
        {1, "K1 power", RelayRole::K1Power, false, false},
        {2, "K1 direction", RelayRole::K1Direction, false, false},
        {3, "P4", RelayRole::Pump, true, true},
    };
    static constexpr AntiSeizeOutputDesc antiSeize[] = {
        {"K1", AntiSeizeKind::ValveStroke, 1, "asEnK1", 3},
    };
    static constexpr HwProjectConfig cfg = {relays, 4, nullptr, 0, antiSeize, 1, {true, 1, 2}};
    TEST_ASSERT_TRUE(validateHwConfig(cfg));
}

static void hwcfg_test_duplicate_channel_rejected() {
    static constexpr RelayChannelDesc relays[] = {
        {0, "P1", RelayRole::Pump, true, true},
        {0, "P2", RelayRole::Pump, true, true},
    };
    static constexpr HwProjectConfig cfg = {
        relays, 2, nullptr, 0, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};
    TEST_ASSERT_FALSE(validateHwConfig(cfg));
}

static void hwcfg_test_channel_out_of_range_rejected() {
    static constexpr RelayChannelDesc relays[] = {{RELAY_CHANNEL_COUNT, "P1", RelayRole::Pump, true, true}};
    static constexpr HwProjectConfig cfg = {
        relays, 1, nullptr, 0, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};
    TEST_ASSERT_FALSE(validateHwConfig(cfg));
}

static void hwcfg_test_k1_role_without_k1_present_rejected() {
    static constexpr RelayChannelDesc relays[] = {{1, "K1 power", RelayRole::K1Power, false, false}};
    static constexpr HwProjectConfig cfg = {
        relays, 1, nullptr, 0, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};
    TEST_ASSERT_FALSE(validateHwConfig(cfg));
}

static void hwcfg_test_lockable_k1_channel_rejected() {
    static constexpr RelayChannelDesc relays[] = {
        {1, "K1 power", RelayRole::K1Power, true, false},   // lockable -- invalid for K1
        {2, "K1 direction", RelayRole::K1Direction, false, false},
    };
    static constexpr HwProjectConfig cfg = {relays, 2, nullptr, 0, nullptr, 0, {true, 1, 2}};
    TEST_ASSERT_FALSE(validateHwConfig(cfg));
}

static void hwcfg_test_valve_stroke_without_k1_rejected() {
    static constexpr RelayChannelDesc relays[] = {{0, "P1", RelayRole::Pump, true, true}};
    static constexpr AntiSeizeOutputDesc antiSeize[] = {
        {"K1", AntiSeizeKind::ValveStroke, 1, "asEnK1", NO_RELAY_CHANNEL}};
    static constexpr HwProjectConfig cfg = {
        relays, 1, nullptr, 0, antiSeize, 1, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};
    TEST_ASSERT_FALSE(validateHwConfig(cfg));
}

static void hwcfg_test_anti_seize_channel_not_in_relays_rejected() {
    static constexpr RelayChannelDesc relays[] = {{0, "P1", RelayRole::Pump, true, true}};
    static constexpr AntiSeizeOutputDesc antiSeize[] = {
        {"P2", AntiSeizeKind::Pump, 1, "asEnP2", NO_RELAY_CHANNEL}};  // channel 1 not configured
    static constexpr HwProjectConfig cfg = {
        relays, 1, nullptr, 0, antiSeize, 1, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};
    TEST_ASSERT_FALSE(validateHwConfig(cfg));
}

static void hwcfg_test_duplicate_setting_key_rejected() {
    static constexpr LogicalSensorDesc sensors[] = {{"T1", "sensorT1"}, {"T2", "sensorT1"}};
    static constexpr HwProjectConfig cfg = {
        nullptr, 0, sensors, 2, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};
    TEST_ASSERT_FALSE(validateHwConfig(cfg));
}

static void hwcfg_test_too_many_sensors_rejected() {
    static constexpr LogicalSensorDesc sensors[] = {
        {"T1", "sensorT1"}, {"T2", "sensorT2"}, {"T3", "sensorT3"}, {"T4", "sensorT4"},
        {"T5", "sensorT5"}, {"T6", "sensorT6"}, {"T7", "sensorT7"}, {"T8", "sensorT8"},
        {"T9", "sensorT9"},
    };
    static constexpr HwProjectConfig cfg = {
        nullptr, 0, sensors, 9, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};
    TEST_ASSERT_FALSE(validateHwConfig(cfg));
}

static void hwcfg_test_make_assign_sensor_copies_address() {
    uint8_t addr[8] = {0x28, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    Command cmd = makeAssignSensor(3, addr, EventReason::Web, 11);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::AssignSensor), static_cast<int>(cmd.type));
    TEST_ASSERT_EQUAL_UINT16(3, cmd.settingIndex);
    TEST_ASSERT_EQUAL_UINT32(11, cmd.id);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(addr, cmd.address, 8);
}

static void hwcfg_test_make_assign_sensor_null_address_is_none() {
    Command cmd = makeAssignSensor(3, nullptr, EventReason::Web, 11);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::None), static_cast<int>(cmd.type));
}

static void hwcfg_test_make_clear_sensor() {
    Command cmd = makeClearSensor(2, EventReason::Web, 5);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::ClearSensor), static_cast<int>(cmd.type));
    TEST_ASSERT_EQUAL_UINT16(2, cmd.settingIndex);
    TEST_ASSERT_EQUAL_UINT32(5, cmd.id);
}

static void hwcfg_test_make_rescan_one_wire() {
    Command cmd = makeRescanOneWire(EventReason::Web, 9);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::RescanOneWire), static_cast<int>(cmd.type));
    TEST_ASSERT_EQUAL_UINT32(9, cmd.id);
}

// Runs every test in this suite. Call from runHwSuite().
inline void runHwConfigSuite() {
    RUN_TEST(hwcfg_test_settings_values);
    RUN_TEST(hwcfg_test_schema_with_hw_table_validates);
    RUN_TEST(hwcfg_test_valid_pump_only_config);
    RUN_TEST(hwcfg_test_valid_k1_config);
    RUN_TEST(hwcfg_test_duplicate_channel_rejected);
    RUN_TEST(hwcfg_test_channel_out_of_range_rejected);
    RUN_TEST(hwcfg_test_k1_role_without_k1_present_rejected);
    RUN_TEST(hwcfg_test_lockable_k1_channel_rejected);
    RUN_TEST(hwcfg_test_valve_stroke_without_k1_rejected);
    RUN_TEST(hwcfg_test_anti_seize_channel_not_in_relays_rejected);
    RUN_TEST(hwcfg_test_duplicate_setting_key_rejected);
    RUN_TEST(hwcfg_test_too_many_sensors_rejected);
    RUN_TEST(hwcfg_test_make_assign_sensor_copies_address);
    RUN_TEST(hwcfg_test_make_assign_sensor_null_address_is_none);
    RUN_TEST(hwcfg_test_make_clear_sensor);
    RUN_TEST(hwcfg_test_make_rescan_one_wire);
}
