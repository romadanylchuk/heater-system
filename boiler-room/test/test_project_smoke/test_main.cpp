#include <unity.h>
#include <BoardConfig.h>
#include <CommonState.h>
#include <ConfigEngine.h>
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

static void test_discovery_payloads_fit() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
        static_cast<int>(reg.build(config, BOILER_ROOM_HW, nullptr, 0)));
    TEST_ASSERT_TRUE(reg.count() > 0);

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
    RUN_TEST(test_home_no_need_gated_until_mqtt);
    return UNITY_END();
}
