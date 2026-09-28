#include <unity.h>
#include <BoardConfig.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <DisplaySettings.h>
#include <EventLog.h>
#include <HaDiscovery.h>
#include <HaEntityRegistry.h>
#include <HomeHeatingControlSettings.h>
#include <HomeHeatingDiagSettings.h>
#include <HomeHeatingHa.h>
#include <HwConfig.h>
#include <HwRuntime.h>
#include <HwSettings.h>
#include <LocalTime.h>
#include <NetIdentity.h>
#include <RelayMask.h>
#include <stdio.h>
#include <string.h>
#include "../../../common/test/fakes/FakeClock.h"
#include "../../../common/test/fakes/FakeOneWireBus.h"
#include "../../../common/test/fakes/FakeRelayPort.h"
#include "../../../common/test/fakes/MemoryKvStore.h"
#include "../../src/HomeHeatingHardware.h"
#include "../../src/HomeHeatingNet.h"
#include "../../src/HomeHeatingSchema.h"

void setUp() {}
void tearDown() {}

static void test_relay_channel_count_is_six() {
    TEST_ASSERT_EQUAL_UINT8(6, RELAY_CHANNEL_COUNT);
}

static void test_home_heating_schema_is_valid() {
    TEST_ASSERT_TRUE(ConfigEngine::validateSchema(HOME_HEATING_SCHEMA));
}

static void test_heating_enabled_persists_across_reboot() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());

    size_t heatingIdx = static_cast<size_t>(HomeHeatingSetting::HeatingEnabled);
    {
        ConfigEngine engine(cfgStore, log);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
            static_cast<int>(engine.begin(HOME_HEATING_SCHEMA, 0)));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(heatingIdx), engine.indexOf("heatingEnabled"));
        TEST_ASSERT_TRUE(engine.getBool(heatingIdx));  // default ON
        engine.setNumber(heatingIdx, 0, EventReason::Web, 0);  // false
        engine.flushNow();
    }

    ConfigEngine engine2(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine2.begin(HOME_HEATING_SCHEMA, 0)));
    TEST_ASSERT_FALSE(engine2.getBool(heatingIdx));
    // homeNoNeed is a boiler-room setting, not part of the home-heating schema.
    TEST_ASSERT_EQUAL_INT(-1, engine2.indexOf("homeNoNeed"));
}

static void test_home_heating_hw_descriptor_is_valid() {
    TEST_ASSERT_TRUE(validateHwConfig(HOME_HEATING_HW));
    TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(HOME_HEATING_HW.relayCount));
    TEST_ASSERT_TRUE(HOME_HEATING_HW.k1.present);
    TEST_ASSERT_EQUAL_UINT8(1, HOME_HEATING_HW.k1.powerChannel);
    TEST_ASSERT_EQUAL_UINT8(2, HOME_HEATING_HW.k1.directionChannel);
    TEST_ASSERT_EQUAL_UINT8(3, HOME_HEATING_HW.antiSeize[2].blockWhileOnChannel);  // K1 blocked by P4
}

static void test_home_heating_hw_keys_resolve_in_schema() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(HOME_HEATING_SCHEMA, 0)));

    TEST_ASSERT_TRUE(engine.indexOf(HW_KEY_RELAY_LOCK) >= 0);
    TEST_ASSERT_TRUE(engine.indexOf(HW_KEY_AS_INTERVAL) >= 0);
    TEST_ASSERT_TRUE(engine.indexOf(HW_KEY_AS_TIME) >= 0);
    TEST_ASSERT_TRUE(engine.indexOf(HW_KEY_AS_DURATION) >= 0);

    for (size_t i = 0; i < HOME_HEATING_HW.sensorCount; ++i) {
        int idx = engine.indexOf(HOME_HEATING_HW.sensors[i].settingKey);
        TEST_ASSERT_TRUE(idx >= 0);
        const SettingDescriptor* d = engine.descriptor(static_cast<size_t>(idx));
        TEST_ASSERT_NOT_NULL(d);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingType::Text), static_cast<int>(d->type));
        TEST_ASSERT_EQUAL_UINT8(16, d->maxLen);
    }

    for (size_t i = 0; i < HOME_HEATING_HW.antiSeizeCount; ++i) {
        int idx = engine.indexOf(HOME_HEATING_HW.antiSeize[i].enableKey);
        TEST_ASSERT_TRUE(idx >= 0);
        TEST_ASSERT_TRUE(engine.getBool(static_cast<size_t>(idx)));  // enables default ON
    }
}

static void test_home_heating_hw_runtime_starts_with_real_schema_and_descriptor() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HOME_HEATING_SCHEMA, 0)));

    CommonState state{};
    FakeOneWireBus bus;
    FakeRelayPort port;
    HwRuntime hw(state, config, log, port, bus);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(hw.begin(HOME_HEATING_HW, 0)));

    hw.fastTick(100);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(port.written.size()));
    TEST_ASSERT_EQUAL_UINT8(RELAY_ALL_OFF_BYTE, port.written[0]);  // nothing requested ON yet

    TEST_ASSERT_EQUAL_UINT8(4, state.sensors.count);
    TEST_ASSERT_TRUE(state.k1.present);
}

static int findHaKey(const HaEntityRegistry& reg, const char* key) {
    return reg.findByKey(key, strlen(key));
}

static void test_net_identity_valid() {
    TEST_ASSERT_TRUE(validateNetIdentity(HOME_HEATING_NET));
    TEST_ASSERT_EQUAL_STRING("home-heating", HOME_HEATING_NET.prefix);
    TEST_ASSERT_EQUAL_STRING("home_heating", HOME_HEATING_NET.uniquePrefix);
    TEST_ASSERT_EQUAL_STRING("HomeHeating-Setup", HOME_HEATING_NET.apSsid);
}

static void test_ha_registry_builds_for_project() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HOME_HEATING_SCHEMA, 0)));

    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, HOME_HEATING_HW, nullptr, 0)));

    int heating = findHaKey(reg, "heating_enabled");
    TEST_ASSERT_TRUE(heating >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Switch), static_cast<int>(reg.entity(heating).component));
    TEST_ASSERT_FALSE(reg.entity(heating).configCategory);  // HA_SWITCH -> dashboard switch

    int lock = findHaKey(reg, "relay_lock");
    TEST_ASSERT_TRUE(lock >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Number), static_cast<int>(reg.entity(lock).component));

    const char* sensors[] = {"temp_h1", "temp_h2", "temp_h3", "temp_h4"};
    for (const char* key : sensors) {
        int i = findHaKey(reg, key);
        TEST_ASSERT_TRUE_MESSAGE(i >= 0, key);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Sensor), static_cast<int>(reg.entity(i).component));
    }
    const char* relays[] = {"relay_k2", "relay_k1_power", "relay_k1_direction", "relay_p4"};
    for (const char* key : relays) {
        int i = findHaKey(reg, key);
        TEST_ASSERT_TRUE_MESSAGE(i >= 0, key);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(HaComponent::BinarySensor), static_cast<int>(reg.entity(i).component));
    }
    TEST_ASSERT_TRUE(findHaKey(reg, "as_en_k1") >= 0);
    TEST_ASSERT_TRUE(findHaKey(reg, "rssi") >= 0);

    const char* excluded[] = {"mqtt_port", "wifi_ssid", "web_pass", "sensor_h1", "mqtt_host", "mqtt_user",
        "mqtt_pass", "web_user", "wifi_pass", "tz", "ntp_server", "home_no_need"};
    for (const char* key : excluded) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, findHaKey(reg, key), key);
    }
}

static void test_discovery_payloads_fit() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HOME_HEATING_SCHEMA, 0)));
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, HOME_HEATING_HW, nullptr, 0)));
    TEST_ASSERT_TRUE(reg.count() > 0);

    static char payload[HA_DISCOVERY_PAYLOAD_MAX];
    char topic[HA_TOPIC_MAX];
    for (size_t i = 0; i < reg.count(); ++i) {
        const HaEntity& e = reg.entity(i);
        size_t n = buildDiscoveryPayload(reg, i, config, HOME_HEATING_NET, "1.2.3-test", payload, sizeof(payload));
        TEST_ASSERT_TRUE_MESSAGE(n > 0 && n < HA_DISCOVERY_PAYLOAD_MAX, e.key);

        TEST_ASSERT_TRUE(buildDiscoveryTopic(HOME_HEATING_NET, e, topic, sizeof(topic)));
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(topic, "/home-heating/"), topic);
        TEST_ASSERT_TRUE(buildStateTopic(HOME_HEATING_NET, e, topic, sizeof(topic)));
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(topic, "home-heating/", 13), topic);
        if (reg.isCommandable(i)) {
            TEST_ASSERT_TRUE(buildCommandTopic(HOME_HEATING_NET, e, topic, sizeof(topic)));
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(topic, "home-heating/", 13), topic);
        }
        // Never the generic reference-project names.
        TEST_ASSERT_NULL_MESSAGE(strstr(payload, "\"boiler/"), e.key);
        TEST_ASSERT_NULL_MESSAGE(strstr(payload, "\"home/"), e.key);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "home_heating_"), e.key);  // unique_id prefix
    }
    TEST_ASSERT_TRUE(buildAvailabilityTopic(HOME_HEATING_NET, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("home-heating/status", topic);
    TEST_ASSERT_TRUE(buildEventTopic(HOME_HEATING_NET, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("home-heating/event", topic);
    TEST_ASSERT_TRUE(buildCommandSubscription(HOME_HEATING_NET, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("home-heating/+/set", topic);
}

// Stage 06: the display table is appended LAST (tables[3]) -- its settings
// resolve with their defaults and clamps, are HA-exposed, and the project's
// own enum indices did not shift.
static void test_display_settings_resolve_in_schema() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HOME_HEATING_SCHEMA, 0)));

    int rotIdx = config.indexOf(DISPLAY_KEY_ROTATE_S);
    int brightIdx = config.indexOf(DISPLAY_KEY_BRIGHTNESS);
    TEST_ASSERT_TRUE(rotIdx >= 0);
    TEST_ASSERT_TRUE(brightIdx >= 0);
    TEST_ASSERT_EQUAL_INT32(5, config.getInt(static_cast<size_t>(rotIdx)));
    TEST_ASSERT_EQUAL_INT32(30, config.getInt(static_cast<size_t>(brightIdx)));

    config.setNumber(static_cast<size_t>(rotIdx), 0, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT32(2, config.getInt(static_cast<size_t>(rotIdx)));
    config.setNumber(static_cast<size_t>(rotIdx), 500, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT32(60, config.getInt(static_cast<size_t>(rotIdx)));

    const SettingDescriptor* first = config.descriptor(static_cast<size_t>(HomeHeatingSetting::HeatingEnabled));
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_STRING("heatingEnabled", first->key);

    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, HOME_HEATING_HW, nullptr, 0)));
    TEST_ASSERT_TRUE(findHaKey(reg, "disp_rotate_s") >= 0);
    TEST_ASSERT_TRUE(findHaKey(reg, "disp_bright") >= 0);
}

// Stage 08: the controller settings table is appended LAST (tables[4]).
static void test_control_table_is_last() {
    TEST_ASSERT_EQUAL_UINT32(6, static_cast<uint32_t>(HOME_HEATING_SCHEMA.tableCount));   // stage 09: diag = [5]
    TEST_ASSERT_EQUAL_PTR(HOME_HEATING_CONTROL_SETTINGS, HOME_HEATING_SCHEMA.tables[4].items);
    TEST_ASSERT_EQUAL_UINT32(
        HOME_HEATING_CONTROL_SETTING_COUNT, static_cast<uint32_t>(HOME_HEATING_SCHEMA.tables[4].count));
    TEST_ASSERT_EQUAL_UINT16(1, HOME_HEATING_CONFIG_VERSION);

    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HOME_HEATING_SCHEMA, 0)));

    size_t total = 0;
    for (size_t t = 0; t < HOME_HEATING_SCHEMA.tableCount; ++t) {
        total += HOME_HEATING_SCHEMA.tables[t].count;
    }
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(total), static_cast<uint32_t>(config.count()));

    // Earlier indices did not move.
    const SettingDescriptor* first = config.descriptor(static_cast<size_t>(HomeHeatingSetting::HeatingEnabled));
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_STRING("heatingEnabled", first->key);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HomeHeatingSetting::HeatingEnabled), config.indexOf(HH_KEY_HEATING_ENABLED));
    TEST_ASSERT_TRUE(config.indexOf(DISPLAY_KEY_ROTATE_S) >= 0);
    TEST_ASSERT_TRUE(config.indexOf(DISPLAY_KEY_BRIGHTNESS) >= 0);
    TEST_ASSERT_TRUE(config.indexOf(DISPLAY_KEY_ROTATE_S) < config.indexOf(HH_KEY_H2_SET));
}

// Stage 09: the diagnostics settings table is appended LAST (tables[5], D20).
static void test_diag_table_is_last() {
    TEST_ASSERT_EQUAL_UINT32(6, static_cast<uint32_t>(HOME_HEATING_SCHEMA.tableCount));
    TEST_ASSERT_EQUAL_PTR(HOME_HEATING_DIAG_SETTINGS, HOME_HEATING_SCHEMA.tables[5].items);
    TEST_ASSERT_EQUAL_UINT32(6, static_cast<uint32_t>(HOME_HEATING_SCHEMA.tables[5].count));
    TEST_ASSERT_EQUAL_UINT32(
        HOME_HEATING_DIAG_SETTING_COUNT, static_cast<uint32_t>(HOME_HEATING_SCHEMA.tables[5].count));
    TEST_ASSERT_EQUAL_UINT16(1, HOME_HEATING_CONFIG_VERSION);

    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HOME_HEATING_SCHEMA, 0)));

    size_t total = 0;
    for (size_t t = 0; t < HOME_HEATING_SCHEMA.tableCount; ++t) {
        total += HOME_HEATING_SCHEMA.tables[t].count;
    }
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(total), static_cast<uint32_t>(config.count()));

    // The diag keys occupy the trailing indices; earlier indices did not move.
    const size_t firstDiag = total - HOME_HEATING_DIAG_SETTING_COUNT;
    TEST_ASSERT_TRUE(config.indexOf(HH_KEY_K2_H4_MAX_HYST) < static_cast<int>(firstDiag));
    const char* keys[] = {HH_KEY_H1_EN, HH_KEY_H1_MIN_ON, HH_KEY_H1_K1_MIN, HH_KEY_H1_DELTA, HH_KEY_H1_MIN_DIFF,
        HH_KEY_K1_STEP_PULSE};
    for (size_t k = 0; k < HOME_HEATING_DIAG_SETTING_COUNT; ++k) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(firstDiag + k), config.indexOf(keys[k]), keys[k]);
        const SettingDescriptor* d = config.descriptor(firstDiag + k);
        TEST_ASSERT_NOT_NULL_MESSAGE(d, keys[k]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(keys[k], d->nvsKey, keys[k]);   // key == nvsKey
        TEST_ASSERT_TRUE_MESSAGE(strlen(d->nvsKey) <= 15, keys[k]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(k == 5 ? "k1" : "diag", d->group, keys[k]);
    }
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HomeHeatingSetting::HeatingEnabled), config.indexOf(HH_KEY_HEATING_ENABLED));

    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, HOME_HEATING_HW, nullptr, 0)));
    TEST_ASSERT_TRUE(reg.count() <= HA_MAX_ENTITIES);
}

// The plan's C2 table (literal values): key, type, min, max, default.
struct ControlKeyExpectation {
    const char* key;
    SettingType type;
    float min;
    float max;
    float def;
};

static void test_control_keys_min_max() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HOME_HEATING_SCHEMA, 0)));

    const ControlKeyExpectation expected[] = {
        {HH_KEY_H2_SET, SettingType::Float, 30, 75, 40},
        {HH_KEY_P4_OFF_DELAY, SettingType::Int, 0, 60, 5},
        {HH_KEY_K1_TRAVEL, SettingType::Int, 30, 300, 120},
        {HH_KEY_K1_PERIOD, SettingType::Int, 10, 300, 30},
        {HH_KEY_K1_DEADBAND, SettingType::Float, 0.2f, 5, 1},
        {HH_KEY_K1_GAIN, SettingType::Float, 0.5f, 10, 2},
        {HH_KEY_K1_MAX_PULSE, SettingType::Int, 1, 60, 10},
        {HH_KEY_K1_MIN_PULSE, SettingType::Float, 0.5f, 5, 1},
        {HH_KEY_K1_RESYNC, SettingType::Int, 5, 25, 10},
        {HH_KEY_K1_SMALL_DIFF, SettingType::Float, 0.5f, 10, 2},
        {HH_KEY_K1_FAIL_POS, SettingType::Int, 0, 100, 30},
        {HH_KEY_K1_FF_STEP, SettingType::Int, 1, 25, 5},
        {HH_KEY_K2_DELTA, SettingType::Float, 1, 15, 3},
        {HH_KEY_K2_DELTA_HYST, SettingType::Float, 0.5f, 10, 2},
        {HH_KEY_K2_H3_MIN, SettingType::Int, 40, 80, 65},
        {HH_KEY_K2_H3_MIN_HYST, SettingType::Float, 1, 10, 3},
        {HH_KEY_K2_H4_MAX, SettingType::Int, 40, 80, 70},
        {HH_KEY_K2_H4_MAX_HYST, SettingType::Float, 1, 10, 3},
    };
    TEST_ASSERT_EQUAL_UINT32(HOME_HEATING_CONTROL_SETTING_COUNT, sizeof(expected) / sizeof(expected[0]));
    for (const ControlKeyExpectation& e : expected) {
        int idx = config.indexOf(e.key);
        TEST_ASSERT_TRUE_MESSAGE(idx >= 0, e.key);
        const size_t i = static_cast<size_t>(idx);
        const SettingDescriptor* d = config.descriptor(i);
        TEST_ASSERT_NOT_NULL_MESSAGE(d, e.key);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.type), static_cast<int>(d->type), e.key);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.key, d->nvsKey, e.key);  // key == nvsKey
        TEST_ASSERT_TRUE_MESSAGE(strlen(d->nvsKey) <= 15, e.key);
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(e.min, d->minValue, e.key);
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(e.max, d->maxValue, e.key);
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(e.def, config.getNumber(i), e.key);

        // Out-of-range writes clamp to the row's bounds.
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(ConfigStatus::Clamped),
            static_cast<int>(config.setNumber(i, e.max + 100.0f, EventReason::Web, 0)), e.key);
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(e.max, config.getNumber(i), e.key);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(ConfigStatus::Clamped),
            static_cast<int>(config.setNumber(i, e.min - 100.0f, EventReason::Web, 0)), e.key);
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(e.min, config.getNumber(i), e.key);
    }
}

static void test_control_settings_ha_entities() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HOME_HEATING_SCHEMA, 0)));
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, HOME_HEATING_HW, nullptr, 0)));
    TEST_ASSERT_TRUE(reg.count() <= HA_MAX_ENTITIES);

    int h2 = findHaKey(reg, "h2_set");
    TEST_ASSERT_TRUE(h2 >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Number), static_cast<int>(reg.entity(h2).component));
    TEST_ASSERT_TRUE(findHaKey(reg, "k2_h4_max_hyst") >= 0);
    TEST_ASSERT_TRUE(findHaKey(reg, "k1_ff_step") >= 0);
}

// Stage 09 phase 10 (D21): the full registry -- settings + sensors + relays +
// common + the 22 home-heating custom entities -- builds within the budget, the
// diag settings are exposed, and every discovery payload fits.
static void test_full_registry_with_custom_entities_fits() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HOME_HEATING_SCHEMA, 0)));
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, HOME_HEATING_HW, HOME_HEATING_HA_ENTITIES, HOME_HEATING_HA_ENTITY_COUNT)));
    TEST_ASSERT_EQUAL_UINT32(22, HOME_HEATING_HA_ENTITY_COUNT);
    TEST_ASSERT_TRUE(reg.count() <= HA_MAX_ENTITIES);
    char msg[48];
    snprintf(msg, sizeof(msg), "home-heating HA entities = %u", static_cast<unsigned>(reg.count()));
    TEST_MESSAGE(msg);

    const int en = findHaKey(reg, "h1_en");
    TEST_ASSERT_TRUE(en >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Switch), static_cast<int>(reg.entity(en).component));
    TEST_ASSERT_TRUE(reg.entity(en).configCategory);
    const char* numbers[] = {"h1_min_on", "k1_step_pulse"};
    for (const char* k : numbers) {
        const int i = findHaKey(reg, k);
        TEST_ASSERT_TRUE_MESSAGE(i >= 0, k);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(HaComponent::Number), static_cast<int>(reg.entity(i).component), k);
    }
    const char* custom[] = {"warn_p4_no_flow", "h2_error", "k1_last_pulse", "k1_last_pulse_dir", "k1_pulses_today",
        "k1_pulses_yesterday"};
    for (const char* k : custom) {
        TEST_ASSERT_TRUE_MESSAGE(findHaKey(reg, k) >= 0, k);
    }

    static char payload[HA_DISCOVERY_PAYLOAD_MAX];
    char topic[HA_TOPIC_MAX];
    for (size_t i = 0; i < reg.count(); ++i) {
        const HaEntity& e = reg.entity(i);
        const size_t n = buildDiscoveryPayload(reg, i, config, HOME_HEATING_NET, "1.2.3-test", payload, sizeof(payload));
        TEST_ASSERT_TRUE_MESSAGE(n > 0 && n < HA_DISCOVERY_PAYLOAD_MAX, e.key);
        TEST_ASSERT_TRUE_MESSAGE(buildDiscoveryTopic(HOME_HEATING_NET, e, topic, sizeof(topic)), e.key);
        TEST_ASSERT_TRUE_MESSAGE(buildStateTopic(HOME_HEATING_NET, e, topic, sizeof(topic)), e.key);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_relay_channel_count_is_six);
    RUN_TEST(test_home_heating_schema_is_valid);
    RUN_TEST(test_heating_enabled_persists_across_reboot);
    RUN_TEST(test_home_heating_hw_descriptor_is_valid);
    RUN_TEST(test_home_heating_hw_keys_resolve_in_schema);
    RUN_TEST(test_home_heating_hw_runtime_starts_with_real_schema_and_descriptor);
    RUN_TEST(test_net_identity_valid);
    RUN_TEST(test_ha_registry_builds_for_project);
    RUN_TEST(test_discovery_payloads_fit);
    RUN_TEST(test_display_settings_resolve_in_schema);
    RUN_TEST(test_control_table_is_last);
    RUN_TEST(test_diag_table_is_last);
    RUN_TEST(test_control_keys_min_max);
    RUN_TEST(test_control_settings_ha_entities);
    RUN_TEST(test_full_registry_with_custom_entities_fits);
    return UNITY_END();
}
