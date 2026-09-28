#include <unity.h>
#include <BoardConfig.h>
#include <BoilerRoomControlSettings.h>
#include <BoilerRoomHa.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <DisplaySettings.h>
#include <EventLog.h>
#include <HaDiscovery.h>
#include <HaEntityRegistry.h>
#include <HaLinkGate.h>
#include <HwConfig.h>
#include <HwRuntime.h>
#include <HwSettings.h>
#include <LocalTime.h>
#include <NetIdentity.h>
#include <RelayMask.h>
#include <string.h>
#include "../../../common/test/fakes/FakeClock.h"
#include "../../../common/test/fakes/FakeOneWireBus.h"
#include "../../../common/test/fakes/FakeRelayPort.h"
#include "../../../common/test/fakes/MemoryKvStore.h"
#include "../../src/BoilerRoomHardware.h"
#include "../../src/BoilerRoomNet.h"
#include "../../src/BoilerRoomSchema.h"

void setUp() {}
void tearDown() {}

static void test_relay_channel_count_is_six() {
    TEST_ASSERT_EQUAL_UINT8(6, RELAY_CHANNEL_COUNT);
}

static void test_boiler_room_schema_is_valid() {
    TEST_ASSERT_TRUE(ConfigEngine::validateSchema(BOILER_ROOM_SCHEMA));
}

static void test_ha_flags_persist_across_reboot() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());

    size_t noNeedIdx = static_cast<size_t>(BoilerRoomSetting::HomeNoNeed);
    {
        ConfigEngine engine(cfgStore, log);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
            static_cast<int>(engine.begin(BOILER_ROOM_SCHEMA, 0)));
        engine.setNumber(noNeedIdx, 1, EventReason::Web, 0);   // true
        engine.flushNow();
    }

    ConfigEngine engine2(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine2.begin(BOILER_ROOM_SCHEMA, 0)));
    TEST_ASSERT_TRUE(engine2.getBool(noNeedIdx));
    // heatingEnabled is a home-heating setting, not part of the boiler-room schema.
    TEST_ASSERT_EQUAL_INT(-1, engine2.indexOf("heatingEnabled"));
}

static void test_boiler_room_hw_descriptor_is_valid() {
    TEST_ASSERT_TRUE(validateHwConfig(BOILER_ROOM_HW));
    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(BOILER_ROOM_HW.relayCount));
    TEST_ASSERT_FALSE(BOILER_ROOM_HW.k1.present);
}

static void test_boiler_room_hw_keys_resolve_in_schema() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(BOILER_ROOM_SCHEMA, 0)));

    TEST_ASSERT_TRUE(engine.indexOf(HW_KEY_RELAY_LOCK) >= 0);
    TEST_ASSERT_TRUE(engine.indexOf(HW_KEY_AS_INTERVAL) >= 0);
    TEST_ASSERT_TRUE(engine.indexOf(HW_KEY_AS_TIME) >= 0);
    TEST_ASSERT_TRUE(engine.indexOf(HW_KEY_AS_DURATION) >= 0);

    for (size_t i = 0; i < BOILER_ROOM_HW.sensorCount; ++i) {
        int idx = engine.indexOf(BOILER_ROOM_HW.sensors[i].settingKey);
        TEST_ASSERT_TRUE(idx >= 0);
        const SettingDescriptor* d = engine.descriptor(static_cast<size_t>(idx));
        TEST_ASSERT_NOT_NULL(d);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingType::Text), static_cast<int>(d->type));
        TEST_ASSERT_EQUAL_UINT8(16, d->maxLen);
    }

    for (size_t i = 0; i < BOILER_ROOM_HW.antiSeizeCount; ++i) {
        int idx = engine.indexOf(BOILER_ROOM_HW.antiSeize[i].enableKey);
        TEST_ASSERT_TRUE(idx >= 0);
        TEST_ASSERT_TRUE(engine.getBool(static_cast<size_t>(idx)));  // enables default ON
    }
}

static void test_boiler_room_hw_runtime_starts_with_real_schema_and_descriptor() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));

    CommonState state{};
    FakeOneWireBus bus;
    FakeRelayPort port;
    HwRuntime hw(state, config, log, port, bus);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(hw.begin(BOILER_ROOM_HW, 0)));

    hw.fastTick(100);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(port.written.size()));
    TEST_ASSERT_EQUAL_UINT8(RELAY_ALL_OFF_BYTE, port.written[0]);  // nothing requested ON yet

    TEST_ASSERT_EQUAL_UINT8(6, state.sensors.count);
    TEST_ASSERT_FALSE(state.k1.present);
}

static int findHaKey(const HaEntityRegistry& reg, const char* key) {
    return reg.findByKey(key, strlen(key));
}

static void test_net_identity_valid() {
    TEST_ASSERT_TRUE(validateNetIdentity(BOILER_ROOM_NET));
    TEST_ASSERT_EQUAL_STRING("boiler-room", BOILER_ROOM_NET.prefix);
    TEST_ASSERT_EQUAL_STRING("boiler_room", BOILER_ROOM_NET.uniquePrefix);
    TEST_ASSERT_EQUAL_STRING("BoilerRoom-Setup", BOILER_ROOM_NET.apSsid);
}

static void test_ha_registry_builds_for_project() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));

    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, BOILER_ROOM_HW, nullptr, 0)));

    // HA_SWITCH bool -> dashboard switch (not config category).
    int noNeed = findHaKey(reg, "home_no_need");
    TEST_ASSERT_TRUE(noNeed >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Switch), static_cast<int>(reg.entity(noNeed).component));
    TEST_ASSERT_FALSE(reg.entity(noNeed).configCategory);

    int lock = findHaKey(reg, "relay_lock");
    TEST_ASSERT_TRUE(lock >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Number), static_cast<int>(reg.entity(lock).component));
    TEST_ASSERT_TRUE(reg.entity(lock).configCategory);

    // Plain bool -> config switch (D8 deviation).
    int asEn = findHaKey(reg, "as_en_p1");
    TEST_ASSERT_TRUE(asEn >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Switch), static_cast<int>(reg.entity(asEn).component));
    TEST_ASSERT_TRUE(reg.entity(asEn).configCategory);

    const char* sensors[] = {"temp_t1", "temp_t2", "temp_t3", "temp_t4", "temp_t5", "temp_t6"};
    for (const char* key : sensors) {
        int i = findHaKey(reg, key);
        TEST_ASSERT_TRUE_MESSAGE(i >= 0, key);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Sensor), static_cast<int>(reg.entity(i).component));
    }
    const char* relays[] = {"relay_p1", "relay_p2", "relay_p3"};
    for (const char* key : relays) {
        int i = findHaKey(reg, key);
        TEST_ASSERT_TRUE_MESSAGE(i >= 0, key);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(HaComponent::BinarySensor), static_cast<int>(reg.entity(i).component));
    }
    TEST_ASSERT_TRUE(findHaKey(reg, "rssi") >= 0);

    // Never exposed: connection/access settings and sensor-address Text settings.
    const char* excluded[] = {"mqtt_port", "wifi_ssid", "web_pass", "sensor_t1", "mqtt_host", "mqtt_user",
        "mqtt_pass", "web_user", "wifi_pass", "tz", "ntp_server"};
    for (const char* key : excluded) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, findHaKey(reg, key), key);
    }
}

// Builds the project registry (with the given custom table) and checks every
// discovery payload fits and every topic uses the boiler-room identity.
static void checkDiscoveryPayloadsFit(const HaCustomEntity* custom, size_t customCount) {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, BOILER_ROOM_HW, custom, customCount)));
    TEST_ASSERT_TRUE(reg.count() > 0);
    TEST_ASSERT_TRUE(reg.count() <= HA_MAX_ENTITIES);

    static char payload[HA_DISCOVERY_PAYLOAD_MAX];
    char topic[HA_TOPIC_MAX];
    for (size_t i = 0; i < reg.count(); ++i) {
        const HaEntity& e = reg.entity(i);
        size_t n = buildDiscoveryPayload(reg, i, config, BOILER_ROOM_NET, "1.2.3-test", payload, sizeof(payload));
        TEST_ASSERT_TRUE_MESSAGE(n > 0 && n < HA_DISCOVERY_PAYLOAD_MAX, e.key);

        TEST_ASSERT_TRUE(buildDiscoveryTopic(BOILER_ROOM_NET, e, topic, sizeof(topic)));
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(topic, "/boiler-room/"), topic);
        TEST_ASSERT_TRUE(buildStateTopic(BOILER_ROOM_NET, e, topic, sizeof(topic)));
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(topic, "boiler-room/", 12), topic);
        if (reg.isCommandable(i)) {
            TEST_ASSERT_TRUE(buildCommandTopic(BOILER_ROOM_NET, e, topic, sizeof(topic)));
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(topic, "boiler-room/", 12), topic);
        }
        // Never the generic reference-project names.
        TEST_ASSERT_NULL_MESSAGE(strstr(payload, "\"boiler/"), e.key);
        TEST_ASSERT_NULL_MESSAGE(strstr(payload, "\"home/"), e.key);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "boiler_room_"), e.key);  // unique_id prefix
    }
    TEST_ASSERT_TRUE(buildAvailabilityTopic(BOILER_ROOM_NET, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("boiler-room/status", topic);
    TEST_ASSERT_TRUE(buildEventTopic(BOILER_ROOM_NET, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("boiler-room/event", topic);
    TEST_ASSERT_TRUE(buildCommandSubscription(BOILER_ROOM_NET, topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("boiler-room/+/set", topic);
}

static void test_discovery_payloads_fit() {
    checkDiscoveryPayloadsFit(nullptr, 0);
}

// Stage 07: the same loop over the registry with the 15 controller entities.
static void test_discovery_payloads_fit_with_custom() {
    checkDiscoveryPayloadsFit(BOILER_ROOM_HA_ENTITIES, BOILER_ROOM_HA_ENTITY_COUNT);
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, BOILER_ROOM_HW, BOILER_ROOM_HA_ENTITIES, BOILER_ROOM_HA_ENTITY_COUNT)));
    static char payload[HA_DISCOVERY_PAYLOAD_MAX];
    char topic[HA_TOPIC_MAX];
    const int energy = findHaKey(reg, "energy");
    TEST_ASSERT_TRUE(energy >= 0);
    TEST_ASSERT_TRUE(buildDiscoveryPayload(reg, static_cast<size_t>(energy), config, BOILER_ROOM_NET, "1.2.3-test",
                         payload, sizeof(payload)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"energy_storage\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "boiler_room_energy"));
    TEST_ASSERT_TRUE(buildDiscoveryTopic(BOILER_ROOM_NET, reg.entity(static_cast<size_t>(energy)), topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/boiler-room/energy/config", topic);
    const int alarm = findHaKey(reg, "alarm_t6_fault");
    TEST_ASSERT_TRUE(alarm >= 0);
    TEST_ASSERT_TRUE(buildDiscoveryTopic(BOILER_ROOM_NET, reg.entity(static_cast<size_t>(alarm)), topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("homeassistant/binary_sensor/boiler-room/alarm_t6_fault/config", topic);
    TEST_ASSERT_TRUE(buildStateTopic(BOILER_ROOM_NET, reg.entity(static_cast<size_t>(alarm)), topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING("boiler-room/alarm_t6_fault/state", topic);
}

static void test_home_no_need_gated_until_mqtt() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));

    const size_t noNeedIdx = static_cast<size_t>(BoilerRoomSetting::HomeNoNeed);
    config.setNumber(noNeedIdx, 1, EventReason::Mqtt, 0);  // persisted true
    TEST_ASSERT_TRUE(config.getBool(noNeedIdx));

    CommonState state{};
    TEST_ASSERT_FALSE(state.network.mqttConnected);  // boot: no session yet
    TEST_ASSERT_FALSE(haGatedFlag(config.getBool(noNeedIdx), state.network));

    state.network.mqttConnected = true;
    TEST_ASSERT_TRUE(haGatedFlag(config.getBool(noNeedIdx), state.network));

    state.network.mqttConnected = false;  // link lost
    TEST_ASSERT_FALSE(haGatedFlag(config.getBool(noNeedIdx), state.network));
    TEST_ASSERT_TRUE(config.getBool(noNeedIdx));  // persisted value untouched
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
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));

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

    const SettingDescriptor* first = config.descriptor(static_cast<size_t>(BoilerRoomSetting::HomeNoNeed));
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_STRING("homeNoNeed", first->key);

    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, BOILER_ROOM_HW, nullptr, 0)));
    TEST_ASSERT_TRUE(findHaKey(reg, "disp_rotate_s") >= 0);
    TEST_ASSERT_TRUE(findHaKey(reg, "disp_bright") >= 0);
}

// Stage 07: the controller settings table is appended LAST (tables[4]).
static void test_control_settings_table_appended_last() {
    TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(BOILER_ROOM_SCHEMA.tableCount));
    TEST_ASSERT_EQUAL_PTR(BOILER_ROOM_CONTROL_SETTINGS, BOILER_ROOM_SCHEMA.tables[4].items);
    TEST_ASSERT_EQUAL_UINT32(BOILER_ROOM_CONTROL_SETTING_COUNT, static_cast<uint32_t>(BOILER_ROOM_SCHEMA.tables[4].count));
    TEST_ASSERT_EQUAL_UINT16(1, BOILER_ROOM_CONFIG_VERSION);

    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));
    TEST_ASSERT_EQUAL_UINT32(43, static_cast<uint32_t>(config.count()));

    // Earlier indices did not move.
    const SettingDescriptor* first = config.descriptor(static_cast<size_t>(BoilerRoomSetting::HomeNoNeed));
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_STRING("homeNoNeed", first->key);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BoilerRoomSetting::HomeNoNeed), config.indexOf(BR_KEY_HOME_NO_NEED));
    TEST_ASSERT_TRUE(config.indexOf(DISPLAY_KEY_ROTATE_S) >= 0);
    TEST_ASSERT_TRUE(config.indexOf(DISPLAY_KEY_BRIGHTNESS) >= 0);
}

struct ControlKeyExpectation {
    const char* key;
    SettingType type;
    float def;
};

static void test_control_keys_resolve_with_type_and_default() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));

    const ControlKeyExpectation expected[] = {
        {BR_KEY_P1_DELTA_ON, SettingType::Float, 5},
        {BR_KEY_P1_DELTA_OFF, SettingType::Float, 2},
        {BR_KEY_P1_T1_MIN, SettingType::Int, 60},
        {BR_KEY_P1_HYST, SettingType::Float, 2},
        {BR_KEY_OH_ON, SettingType::Int, 90},
        {BR_KEY_OH_CLEAR, SettingType::Int, 87},
        {BR_KEY_P2_T2_OFF, SettingType::Int, 60},
        {BR_KEY_P2_HYST, SettingType::Float, 3},
        {BR_KEY_P2_T1_BURN, SettingType::Int, 40},
        {BR_KEY_P3_T3_OFFER, SettingType::Int, 60},
        {BR_KEY_P3_OFFER_WIN, SettingType::Int, 10},
        {BR_KEY_P3_OFFER_WAIT, SettingType::Int, 60},
        {BR_KEY_AF_ENABLE, SettingType::Bool, 1},
        {BR_KEY_AF_INTERVAL, SettingType::Int, 30},
        {BR_KEY_AF_DURATION, SettingType::Int, 60},
        {BR_KEY_ACC_VOLUME, SettingType::Int, 500},
        {BR_KEY_ACC_T_BASE, SettingType::Int, 30},
    };
    TEST_ASSERT_EQUAL_UINT32(BOILER_ROOM_CONTROL_SETTING_COUNT, sizeof(expected) / sizeof(expected[0]));
    for (const ControlKeyExpectation& e : expected) {
        int idx = config.indexOf(e.key);
        TEST_ASSERT_TRUE_MESSAGE(idx >= 0, e.key);
        const SettingDescriptor* d = config.descriptor(static_cast<size_t>(idx));
        TEST_ASSERT_NOT_NULL_MESSAGE(d, e.key);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.type), static_cast<int>(d->type), e.key);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.key, d->nvsKey, e.key);  // key == nvsKey
        TEST_ASSERT_TRUE_MESSAGE(strlen(d->nvsKey) <= 15, e.key);
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(e.def, config.getNumber(static_cast<size_t>(idx)), e.key);
    }
}

// Final-check Should 1: the min/max of all 17 control settings, pinned to the
// plan's C2 table (a separate table so the type/default table above stays untouched).
struct ControlKeyRange {
    const char* key;
    float min;
    float max;
};

static void test_control_keys_min_max() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));

    const ControlKeyRange expected[] = {
        {BR_KEY_P1_DELTA_ON, 2, 20},
        {BR_KEY_P1_DELTA_OFF, 0, 15},
        {BR_KEY_P1_T1_MIN, 40, 80},
        {BR_KEY_P1_HYST, 1, 10},
        {BR_KEY_OH_ON, 80, 95},
        {BR_KEY_OH_CLEAR, 70, 94},
        {BR_KEY_P2_T2_OFF, 55, 65},
        {BR_KEY_P2_HYST, 1, 5},
        {BR_KEY_P2_T1_BURN, 20, 60},
        {BR_KEY_P3_T3_OFFER, 40, 85},
        {BR_KEY_P3_OFFER_WIN, 1, 60},
        {BR_KEY_P3_OFFER_WAIT, 0, 480},
        {BR_KEY_AF_ENABLE, 0, 1},
        {BR_KEY_AF_INTERVAL, 5, 240},
        {BR_KEY_AF_DURATION, 10, 600},
        {BR_KEY_ACC_VOLUME, 100, 2000},
        {BR_KEY_ACC_T_BASE, 10, 60},
    };
    TEST_ASSERT_EQUAL_UINT32(BOILER_ROOM_CONTROL_SETTING_COUNT, sizeof(expected) / sizeof(expected[0]));
    for (const ControlKeyRange& e : expected) {
        int idx = config.indexOf(e.key);
        TEST_ASSERT_TRUE_MESSAGE(idx >= 0, e.key);
        const SettingDescriptor* d = config.descriptor(static_cast<size_t>(idx));
        TEST_ASSERT_NOT_NULL_MESSAGE(d, e.key);
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(e.min, d->minValue, e.key);
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(e.max, d->maxValue, e.key);
    }
}

// Owner decision FN-1: overheat set-point capped at 95 °C (ohClear at 94).
static void test_overheat_cap_95() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));
    const size_t on = static_cast<size_t>(config.indexOf(BR_KEY_OH_ON));
    const size_t clr = static_cast<size_t>(config.indexOf(BR_KEY_OH_CLEAR));

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Clamped),
        static_cast<int>(config.setNumber(on, 96, EventReason::Mqtt, 0)));
    TEST_ASSERT_EQUAL_FLOAT(95.0f, config.getNumber(on));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
        static_cast<int>(config.setNumber(on, 94, EventReason::Mqtt, 0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
        static_cast<int>(config.setNumber(on, 95, EventReason::Mqtt, 0)));
    TEST_ASSERT_EQUAL_FLOAT(95.0f, config.getNumber(on));

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Clamped),
        static_cast<int>(config.setNumber(clr, 95, EventReason::Mqtt, 0)));
    TEST_ASSERT_EQUAL_FLOAT(94.0f, config.getNumber(clr));
}

static void test_control_settings_ha_entities() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, BOILER_ROOM_HW, nullptr, 0)));

    int af = findHaKey(reg, "af_enable");
    TEST_ASSERT_TRUE(af >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Switch), static_cast<int>(reg.entity(af).component));
    TEST_ASSERT_FALSE(reg.entity(af).configCategory);

    int dOn = findHaKey(reg, "p1_delta_on");
    TEST_ASSERT_TRUE(dOn >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaComponent::Number), static_cast<int>(reg.entity(dOn).component));
    TEST_ASSERT_TRUE(reg.entity(dOn).configCategory);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_relay_channel_count_is_six);
    RUN_TEST(test_boiler_room_schema_is_valid);
    RUN_TEST(test_ha_flags_persist_across_reboot);
    RUN_TEST(test_boiler_room_hw_descriptor_is_valid);
    RUN_TEST(test_boiler_room_hw_keys_resolve_in_schema);
    RUN_TEST(test_boiler_room_hw_runtime_starts_with_real_schema_and_descriptor);
    RUN_TEST(test_net_identity_valid);
    RUN_TEST(test_ha_registry_builds_for_project);
    RUN_TEST(test_discovery_payloads_fit);
    RUN_TEST(test_discovery_payloads_fit_with_custom);
    RUN_TEST(test_home_no_need_gated_until_mqtt);
    RUN_TEST(test_display_settings_resolve_in_schema);
    RUN_TEST(test_control_settings_table_appended_last);
    RUN_TEST(test_control_keys_resolve_with_type_and_default);
    RUN_TEST(test_control_keys_min_max);
    RUN_TEST(test_overheat_cap_95);
    RUN_TEST(test_control_settings_ha_entities);
    return UNITY_END();
}
