#include <unity.h>
#include <BoardConfig.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <EventLog.h>
#include <HaDiscovery.h>
#include <HaEntityRegistry.h>
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
    return UNITY_END();
}
