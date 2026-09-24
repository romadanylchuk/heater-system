#pragma once
#include <unity.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/HwEngine/src/HwSettings.h"
#include "../lib/NetEngine/src/HaEntityRegistry.h"
#include "../lib/NetEngine/src/HaLinkGate.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/RecordingEventSink.h"

// Native-safe Unity tests for stage 04 phase 4: HaEntityRegistry (D8/D9) and
// HaLinkGate::haGatedFlag (D12). Header-only, run via NetSuite.h. Test names
// are prefixed ha_ (D24).
//
// A local schema (COMMON_SETTINGS_TABLE + HW_SETTINGS_TABLE + a dedicated
// test table with an HA_SWITCH bool, a plain bool, a Float with step 0.5, a
// Text and a SECRET int) plus a 2-relay/2-sensor test HwProjectConfig, kept
// local to this file so it never affects other suites.

namespace {

inline constexpr SettingDescriptor HA_TEST_SETTINGS[] = {
    boolSetting("haSwitchBool", "haSwBl", "HA switch bool", "HA-перемикач",
        "test", false, SETTING_FLAG_HA_SWITCH),
    boolSetting("plainBool", "plnBl", "Plain bool", "Звичайний bool",
        "test", false),
    floatSetting("stepFloat", "stepFl", "Step float", "Кроковий float",
        nullptr, "test", 0.0f, 10.0f, 2.5f, 0.5f),
    textSetting("plainText", "plnTxt", "Plain text", "Звичайний текст",
        "test", 0, 16, ""),
    intSetting("secretInt", "secInt", "Secret int", "Секретний int",
        nullptr, "test", 0, 100, 0, 1, SETTING_FLAG_SECRET),
};
inline constexpr SettingsTable HA_TEST_SETTINGS_TABLE = makeTable(HA_TEST_SETTINGS);
inline constexpr SettingsTable HA_TEST_SCHEMA_TABLES[] = {
    COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE, HA_TEST_SETTINGS_TABLE};
inline constexpr ConfigSchema HA_TEST_SCHEMA = {"test-ha", 1, HA_TEST_SCHEMA_TABLES, 3, nullptr, 0};

inline constexpr RelayChannelDesc HA_TEST_RELAYS[] = {
    {0, "P1", RelayRole::Pump, true, true},
    {1, "K2", RelayRole::Diverter, true, true},
};
inline constexpr LogicalSensorDesc HA_TEST_SENSORS[] = {
    {"T1", "sensorT1"},
    {"T2", "sensorT2"},
};
inline constexpr HwProjectConfig HA_TEST_HW = {
    HA_TEST_RELAYS, 2, HA_TEST_SENSORS, 2, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};

// A2 total selectable entities from HA_TEST_SCHEMA + HA_TEST_HW with no custom
// entities: 4 HW_SETTINGS numbers, haSwitchBool/plainBool/stepFloat (plainText
// and secretInt excluded), 2 logical sensors, 2 relays, rssi.
constexpr size_t HA_TEST_BASE_COUNT = 4 + 3 + 2 + 2 + 1;

// Custom entity whose availability/state depends on CommonState, for the
// custom-binary-sensor coverage.
bool customFlagState(const CommonState& state, char* out, size_t cap) {
    if (!state.network.wifiConnected) {
        return false;  // unavailable while Wi-Fi is down
    }
    snprintf(out, cap, "%s", state.relays.on[1] ? "ON" : "OFF");
    return true;
}
constexpr HaCustomEntity HA_TEST_CUSTOM_BINARY = {
    "custom_flag", "Custom flag", HaComponent::BinarySensor, nullptr, nullptr, nullptr, nullptr, customFlagState};

// Config fixture: a fresh ConfigEngine over HA_TEST_SCHEMA for each test.
struct HaFixture {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config;

    HaFixture() : config(store, events) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HA_TEST_SCHEMA, 0)));
    }
};

int findKey(const HaEntityRegistry& reg, const char* key) {
    return reg.findByKey(key, strlen(key));
}

}  // namespace

static void ha_test_registry_selects_entities() {
    HaFixture f;
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(reg.build(f.config, HA_TEST_HW, nullptr, 0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HA_TEST_BASE_COUNT), static_cast<int>(reg.count()));

    // Never exposed: every common connection/access setting (all Text, or Int
    // with NO_HA), plus this file's plain Text and SECRET int.
    const char* excluded[] = {"wifi_ssid", "wifi_pass", "mqtt_host", "mqtt_port", "mqtt_user", "mqtt_pass",
        "web_user", "web_pass", "tz", "ntp_server", "plain_text", "secret_int"};
    for (const char* key : excluded) {
        TEST_ASSERT_EQUAL_INT(-1, findKey(reg, key));
    }

    // HW_SETTINGS numbers.
    int idxLock = findKey(reg, "relay_lock");
    TEST_ASSERT_TRUE(idxLock >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Number), static_cast<int>(reg.entity(idxLock).component));
    TEST_ASSERT_TRUE(reg.entity(idxLock).configCategory);
    TEST_ASSERT_TRUE(findKey(reg, "as_interval") >= 0);
    TEST_ASSERT_TRUE(findKey(reg, "as_time") >= 0);
    TEST_ASSERT_TRUE(findKey(reg, "as_duration") >= 0);

    // HA_SWITCH bool -> switch, not config category.
    int idxSwitch = findKey(reg, "ha_switch_bool");
    TEST_ASSERT_TRUE(idxSwitch >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Switch), static_cast<int>(reg.entity(idxSwitch).component));
    TEST_ASSERT_FALSE(reg.entity(idxSwitch).configCategory);

    // Plain bool (no HA_SWITCH) -> switch, config category.
    int idxPlainBool = findKey(reg, "plain_bool");
    TEST_ASSERT_TRUE(idxPlainBool >= 0);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HaComponent::Switch), static_cast<int>(reg.entity(idxPlainBool).component));
    TEST_ASSERT_TRUE(reg.entity(idxPlainBool).configCategory);

    // Float -> number, config category.
    int idxStepFloat = findKey(reg, "step_float");
    TEST_ASSERT_TRUE(idxStepFloat >= 0);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HaComponent::Number), static_cast<int>(reg.entity(idxStepFloat).component));
    TEST_ASSERT_TRUE(reg.entity(idxStepFloat).configCategory);

    // Logical sensors, relays, common entities.
    TEST_ASSERT_TRUE(findKey(reg, "temp_t1") >= 0);
    TEST_ASSERT_TRUE(findKey(reg, "temp_t2") >= 0);
    int idxRelayP1 = findKey(reg, "relay_p1");
    TEST_ASSERT_TRUE(idxRelayP1 >= 0);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HaComponent::BinarySensor), static_cast<int>(reg.entity(idxRelayP1).component));
    TEST_ASSERT_TRUE(findKey(reg, "relay_k2") >= 0);
    TEST_ASSERT_TRUE(findKey(reg, "rssi") >= 0);
}

static void ha_test_keys_snake_case_unique() {
    HaFixture f;
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(reg.build(f.config, HA_TEST_HW, nullptr, 0)));

    // camelCase settings snake-converted correctly.
    TEST_ASSERT_TRUE(findKey(reg, "ha_switch_bool") >= 0);
    TEST_ASSERT_TRUE(findKey(reg, "plain_bool") >= 0);
    TEST_ASSERT_TRUE(findKey(reg, "step_float") >= 0);
    TEST_ASSERT_TRUE(findKey(reg, "as_interval") >= 0);
    TEST_ASSERT_TRUE(findKey(reg, "as_duration") >= 0);

    // All keys unique (build() would have rejected duplicates, but verify
    // pairwise too).
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HA_TEST_BASE_COUNT), static_cast<int>(reg.count()));
    for (size_t i = 0; i < reg.count(); ++i) {
        for (size_t j = i + 1; j < reg.count(); ++j) {
            TEST_ASSERT_TRUE(strcmp(reg.entity(i).key, reg.entity(j).key) != 0);
        }
    }
}

static void ha_test_duplicate_custom_key_rejected() {
    HaFixture f;
    HaEntityRegistry reg;
    // "relay_p1" collides with the relay-derived key.
    HaCustomEntity dup = {
        "relay_p1", "Dup", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr, customFlagState};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HaRegistryStatus::DuplicateKey), static_cast<int>(reg.build(f.config, HA_TEST_HW, &dup, 1)));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(reg.count()));
}

static void ha_test_bad_custom_key_rejected() {
    HaFixture f;
    HaEntityRegistry reg;
    HaCustomEntity bad = {
        "Bad Key!", "Bad", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr, customFlagState};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HaRegistryStatus::BadKey), static_cast<int>(reg.build(f.config, HA_TEST_HW, &bad, 1)));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(reg.count()));
}

static void ha_test_too_many_entities() {
    HaFixture f;
    HaEntityRegistry reg;

    static constexpr size_t EXTRA = 90;  // HA_TEST_BASE_COUNT (12) + 90 > HA_MAX_ENTITIES (96)
    static char keys[EXTRA][8];
    static HaCustomEntity extras[EXTRA];
    for (size_t i = 0; i < EXTRA; ++i) {
        snprintf(keys[i], sizeof(keys[i]), "c%03zu", i);
        extras[i] = HaCustomEntity{
            keys[i], "Extra", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr, customFlagState};
    }

    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::TooManyEntities),
        static_cast<int>(reg.build(f.config, HA_TEST_HW, extras, EXTRA)));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(reg.count()));
}

static void ha_test_format_state_rules() {
    HaFixture f;
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(reg.build(f.config, HA_TEST_HW, nullptr, 0)));

    CommonState state{};
    char buf[32];

    // Int -> plain integer text (relayLock default = 60).
    int idxLock = findKey(reg, "relay_lock");
    TEST_ASSERT_TRUE(idxLock >= 0);
    TEST_ASSERT_TRUE(reg.formatState(idxLock, state, f.config, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("60", buf);

    // Float, step 0.5 -> 1 decimal (default 2.5).
    int idxFloat = findKey(reg, "step_float");
    TEST_ASSERT_TRUE(idxFloat >= 0);
    TEST_ASSERT_TRUE(reg.formatState(idxFloat, state, f.config, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("2.5", buf);

    // Bool -> ON/OFF (default false).
    int idxBool = findKey(reg, "plain_bool");
    TEST_ASSERT_TRUE(idxBool >= 0);
    TEST_ASSERT_TRUE(reg.formatState(idxBool, state, f.config, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("OFF", buf);

    // Sensor: Ok -> %.1f, else "None" (unavailable).
    int idxT1 = findKey(reg, "temp_t1");
    TEST_ASSERT_TRUE(idxT1 >= 0);
    state.sensors.sensor[0].state = SensorState::Ok;
    state.sensors.sensor[0].tempC = 21.34f;
    TEST_ASSERT_TRUE(reg.formatState(idxT1, state, f.config, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("21.3", buf);
    state.sensors.sensor[0].state = SensorState::Fault;
    TEST_ASSERT_FALSE(reg.formatState(idxT1, state, f.config, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("None", buf);

    // Relay -> ON/OFF.
    int idxRelay = findKey(reg, "relay_p1");
    TEST_ASSERT_TRUE(idxRelay >= 0);
    state.relays.on[0] = true;
    TEST_ASSERT_TRUE(reg.formatState(idxRelay, state, f.config, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("ON", buf);

    // RSSI -> None while Wi-Fi is down, dBm text otherwise.
    int idxRssi = findKey(reg, "rssi");
    TEST_ASSERT_TRUE(idxRssi >= 0);
    state.network.wifiConnected = false;
    TEST_ASSERT_FALSE(reg.formatState(idxRssi, state, f.config, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("None", buf);
    state.network.wifiConnected = true;
    state.network.wifiRssi = -55;
    TEST_ASSERT_TRUE(reg.formatState(idxRssi, state, f.config, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("-55", buf);
}

static void ha_test_custom_binary_sensor_state() {
    HaFixture f;
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(f.config, HA_TEST_HW, &HA_TEST_CUSTOM_BINARY, 1)));

    int idx = findKey(reg, "custom_flag");
    TEST_ASSERT_TRUE(idx >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::BinarySensor), static_cast<int>(reg.entity(idx).component));

    CommonState state{};
    char buf[16];
    state.network.wifiConnected = false;
    TEST_ASSERT_FALSE(reg.formatState(idx, state, f.config, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("None", buf);

    state.network.wifiConnected = true;
    state.relays.on[1] = true;
    TEST_ASSERT_TRUE(reg.formatState(idx, state, f.config, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("ON", buf);
}

static void ha_test_find_by_key() {
    HaFixture f;
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(reg.build(f.config, HA_TEST_HW, nullptr, 0)));

    TEST_ASSERT_TRUE(findKey(reg, "relay_lock") >= 0);
    TEST_ASSERT_EQUAL_INT(-1, findKey(reg, "relay_loc"));    // prefix, not exact
    TEST_ASSERT_EQUAL_INT(-1, findKey(reg, "unknown_key"));  // never registered
    TEST_ASSERT_EQUAL_INT(-1, reg.findByKey(nullptr, 0));
}

static void ha_test_gated_flag() {
    NetworkStatus net{};
    net.mqttConnected = false;
    TEST_ASSERT_FALSE(haGatedFlag(true, net));   // mqtt down: gated off even if persisted true
    net.mqttConnected = true;
    TEST_ASSERT_FALSE(haGatedFlag(false, net));  // not persisted: stays off
    TEST_ASSERT_TRUE(haGatedFlag(true, net));    // both true
}

// Runs every test in this suite. Call from runNetSuite().
inline void runHaSuite() {
    RUN_TEST(ha_test_registry_selects_entities);
    RUN_TEST(ha_test_keys_snake_case_unique);
    RUN_TEST(ha_test_duplicate_custom_key_rejected);
    RUN_TEST(ha_test_bad_custom_key_rejected);
    RUN_TEST(ha_test_too_many_entities);
    RUN_TEST(ha_test_format_state_rules);
    RUN_TEST(ha_test_custom_binary_sensor_state);
    RUN_TEST(ha_test_find_by_key);
    RUN_TEST(ha_test_gated_flag);
}
