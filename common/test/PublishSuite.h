#pragma once
#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <ArduinoJson.h>
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/CoreEngine/src/EventEntry.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/HwEngine/src/HwSettings.h"
#include "../lib/NetEngine/src/HaDiscovery.h"
#include "../lib/NetEngine/src/HaEntityRegistry.h"
#include "../lib/NetEngine/src/MqttPublisher.h"
#include "../lib/NetEngine/src/NetIdentity.h"
#include "fakes/FakeMqttTransport.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/RecordingEventSink.h"

// Native-safe Unity tests for stage 04 phase 5: HaDiscovery (topic/discovery/
// event JSON builders, D7/D8/D9/D20) and MqttPublisher (the throttled publish
// pipeline, D10). Header-only, run via NetSuite.h. Test names are prefixed
// pub_ (D24).
//
// A local schema (COMMON_SETTINGS_TABLE + HW_SETTINGS_TABLE + a dedicated
// test table with an HA_SWITCH bool and a plain bool) plus a 2-relay
// (Pump + Diverter)/1-sensor test HwProjectConfig, kept local to this file so
// it never affects other suites (mirrors HaSuite.h).

namespace {

inline constexpr SettingDescriptor PUB_TEST_SETTINGS[] = {
    boolSetting("haSwitchBool", "haSwBl", "HA switch bool", "HA-перемикач", "test", false, SETTING_FLAG_HA_SWITCH),
    boolSetting("plainBool", "plnBl", "Plain bool", "Звичайний bool", "test", false),
};
inline constexpr SettingsTable PUB_TEST_SETTINGS_TABLE = makeTable(PUB_TEST_SETTINGS);
inline constexpr SettingsTable PUB_TEST_SCHEMA_TABLES[] = {
    COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE, PUB_TEST_SETTINGS_TABLE};
inline constexpr ConfigSchema PUB_TEST_SCHEMA = {"test-pub", 1, PUB_TEST_SCHEMA_TABLES, 3, nullptr, 0};

inline constexpr RelayChannelDesc PUB_TEST_RELAYS[] = {
    {0, "P1", RelayRole::Pump, true, true},
    {1, "K2", RelayRole::Diverter, true, true},
};
inline constexpr LogicalSensorDesc PUB_TEST_SENSORS[] = {
    {"T1", "sensorT1"},
};
inline constexpr HwProjectConfig PUB_TEST_HW = {
    PUB_TEST_RELAYS, 2, PUB_TEST_SENSORS, 1, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};

inline constexpr NetIdentity PUB_TEST_ID = {
    "boiler-room", "boiler_room", "Boiler room", "KC868-A6 boiler-room", "BoilerRoom-Setup"};

// Config fixture: a fresh ConfigEngine + HaEntityRegistry over PUB_TEST_SCHEMA/
// PUB_TEST_HW for each test. 4 HW_SETTINGS numbers + 2 test bools + 1 logical
// sensor + 2 relays + rssi = 10 entities.
struct PubFixture {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config;
    HaEntityRegistry registry;

    PubFixture() : config(store, events) {
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(PUB_TEST_SCHEMA, 0)));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
            static_cast<int>(registry.build(config, PUB_TEST_HW, nullptr, 0)));
    }
};

int pubFindKey(const HaEntityRegistry& reg, const char* key) { return reg.findByKey(key, strlen(key)); }

// Pumps until Live (or a generous guard trips), for tests that only care about
// the post-session-establishment behaviour.
void advanceToLive(MqttPublisher& pub, MqttTransport& t, const ConfigEngine& c) {
    int guard = 0;
    while (pub.phase() != MqttPublisher::Phase::Live && guard < 1000) {
        pub.pump(t, c);
        ++guard;
    }
}

}  // namespace

// ---- HaDiscovery: topics --------------------------------------------------

static void pub_test_topics_layout() {
    PubFixture f;
    char buf[HA_TOPIC_MAX];

    int idxLock = pubFindKey(f.registry, "relay_lock");
    TEST_ASSERT_TRUE(idxLock >= 0);
    const HaEntity& lock = f.registry.entity(idxLock);
    TEST_ASSERT_TRUE(buildStateTopic(PUB_TEST_ID, lock, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("boiler-room/relay_lock/state", buf);
    TEST_ASSERT_TRUE(buildCommandTopic(PUB_TEST_ID, lock, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("boiler-room/relay_lock/set", buf);
    TEST_ASSERT_TRUE(buildDiscoveryTopic(PUB_TEST_ID, lock, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("homeassistant/number/boiler-room/relay_lock/config", buf);

    int idxSwitch = pubFindKey(f.registry, "ha_switch_bool");
    TEST_ASSERT_TRUE(idxSwitch >= 0);
    TEST_ASSERT_TRUE(buildDiscoveryTopic(PUB_TEST_ID, f.registry.entity(idxSwitch), buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("homeassistant/switch/boiler-room/ha_switch_bool/config", buf);

    int idxT1 = pubFindKey(f.registry, "temp_t1");
    TEST_ASSERT_TRUE(idxT1 >= 0);
    TEST_ASSERT_TRUE(buildDiscoveryTopic(PUB_TEST_ID, f.registry.entity(idxT1), buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/boiler-room/temp_t1/config", buf);

    int idxP1 = pubFindKey(f.registry, "relay_p1");
    TEST_ASSERT_TRUE(idxP1 >= 0);
    TEST_ASSERT_TRUE(buildDiscoveryTopic(PUB_TEST_ID, f.registry.entity(idxP1), buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("homeassistant/binary_sensor/boiler-room/relay_p1/config", buf);

    TEST_ASSERT_TRUE(buildAvailabilityTopic(PUB_TEST_ID, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("boiler-room/status", buf);
    TEST_ASSERT_TRUE(buildEventTopic(PUB_TEST_ID, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("boiler-room/event", buf);
    TEST_ASSERT_TRUE(buildCommandSubscription(PUB_TEST_ID, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("boiler-room/+/set", buf);
}

// ---- HaDiscovery: discovery payloads --------------------------------------

static void pub_test_discovery_number_payload() {
    PubFixture f;
    int idx = pubFindKey(f.registry, "relay_lock");
    TEST_ASSERT_TRUE(idx >= 0);

    char payload[HA_DISCOVERY_PAYLOAD_MAX];
    size_t len =
        buildDiscoveryPayload(f.registry, idx, f.config, PUB_TEST_ID, "1.2.3", payload, sizeof(payload));
    TEST_ASSERT_TRUE(len > 0);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, payload, len));
    TEST_ASSERT_EQUAL_STRING("Relay minimum ON/OFF time", doc["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("boiler_room_relay_lock", doc["unique_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("boiler-room/relay_lock/state", doc["state_topic"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("boiler-room/status", doc["availability_topic"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("boiler-room/relay_lock/set", doc["command_topic"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(0, doc["min"].as<int>());
    TEST_ASSERT_EQUAL_INT(600, doc["max"].as<int>());
    TEST_ASSERT_EQUAL_INT(1, doc["step"].as<int>());
    TEST_ASSERT_EQUAL_STRING("s", doc["unit_of_measurement"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("box", doc["mode"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("config", doc["entity_category"].as<const char*>());

    JsonObject device = doc["device"].as<JsonObject>();
    TEST_ASSERT_EQUAL_STRING("boiler_room", device["identifiers"][0].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Boiler room", device["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("heater-system", device["manufacturer"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("KC868-A6 boiler-room", device["model"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("1.2.3", device["sw_version"].as<const char*>());
}

static void pub_test_discovery_switch_and_config_switch() {
    PubFixture f;
    char payload[HA_DISCOVERY_PAYLOAD_MAX];

    int idxHa = pubFindKey(f.registry, "ha_switch_bool");
    TEST_ASSERT_TRUE(idxHa >= 0);
    size_t len = buildDiscoveryPayload(f.registry, idxHa, f.config, PUB_TEST_ID, "1.0.0", payload, sizeof(payload));
    TEST_ASSERT_TRUE(len > 0);
    JsonDocument doc1;
    TEST_ASSERT_FALSE(deserializeJson(doc1, payload, len));
    TEST_ASSERT_EQUAL_STRING("ON", doc1["payload_on"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("OFF", doc1["payload_off"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("boiler-room/ha_switch_bool/set", doc1["command_topic"].as<const char*>());
    TEST_ASSERT_TRUE(doc1["entity_category"].isNull());  // HA_SWITCH: dashboard, not config

    int idxPlain = pubFindKey(f.registry, "plain_bool");
    TEST_ASSERT_TRUE(idxPlain >= 0);
    len = buildDiscoveryPayload(f.registry, idxPlain, f.config, PUB_TEST_ID, "1.0.0", payload, sizeof(payload));
    TEST_ASSERT_TRUE(len > 0);
    JsonDocument doc2;
    TEST_ASSERT_FALSE(deserializeJson(doc2, payload, len));
    TEST_ASSERT_EQUAL_STRING("config", doc2["entity_category"].as<const char*>());
}

static void pub_test_discovery_sensor_and_relay_binary() {
    PubFixture f;
    char payload[HA_DISCOVERY_PAYLOAD_MAX];

    int idxT1 = pubFindKey(f.registry, "temp_t1");
    TEST_ASSERT_TRUE(idxT1 >= 0);
    size_t len = buildDiscoveryPayload(f.registry, idxT1, f.config, PUB_TEST_ID, "1.0.0", payload, sizeof(payload));
    TEST_ASSERT_TRUE(len > 0);
    JsonDocument doc1;
    TEST_ASSERT_FALSE(deserializeJson(doc1, payload, len));
    TEST_ASSERT_EQUAL_STRING("temperature", doc1["device_class"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("measurement", doc1["state_class"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(1, doc1["suggested_display_precision"].as<int>());
    TEST_ASSERT_TRUE(doc1["entity_category"].isNull());
    TEST_ASSERT_TRUE(doc1["command_topic"].isNull());  // sensors are not commandable

    int idxP1 = pubFindKey(f.registry, "relay_p1");  // Pump role
    TEST_ASSERT_TRUE(idxP1 >= 0);
    size_t len2 = buildDiscoveryPayload(f.registry, idxP1, f.config, PUB_TEST_ID, "1.0.0", payload, sizeof(payload));
    TEST_ASSERT_TRUE(len2 > 0);
    JsonDocument doc2;
    TEST_ASSERT_FALSE(deserializeJson(doc2, payload, len2));
    TEST_ASSERT_EQUAL_STRING("running", doc2["device_class"].as<const char*>());

    int idxK2 = pubFindKey(f.registry, "relay_k2");  // Diverter role: no device_class
    TEST_ASSERT_TRUE(idxK2 >= 0);
    size_t len3 = buildDiscoveryPayload(f.registry, idxK2, f.config, PUB_TEST_ID, "1.0.0", payload, sizeof(payload));
    TEST_ASSERT_TRUE(len3 > 0);
    JsonDocument doc3;
    TEST_ASSERT_FALSE(deserializeJson(doc3, payload, len3));
    TEST_ASSERT_TRUE(doc3["device_class"].isNull());
}

static void pub_test_no_secret_entities_in_any_payload() {
    PubFixture f;
    char payload[HA_DISCOVERY_PAYLOAD_MAX];
    for (size_t i = 0; i < f.registry.count(); ++i) {
        size_t len =
            buildDiscoveryPayload(f.registry, i, f.config, PUB_TEST_ID, "1.0.0", payload, sizeof(payload));
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_NULL(strstr(payload, "Pass"));
        TEST_ASSERT_NULL(strstr(payload, "wifi"));
        TEST_ASSERT_NULL(strstr(payload, "mqtt"));
    }
}

// ---- MqttPublisher: pipeline -----------------------------------------------

static void pub_test_pipeline_order_discovery_availability_states_subscribe() {
    PubFixture f;
    CommonState state{};
    MqttPublisher pub;
    pub.begin(f.registry, PUB_TEST_ID, "1.0.0");
    pub.onSessionStart(0);
    pub.refresh(state, f.config, 0);

    FakeMqttTransport t;
    size_t total = f.registry.count();

    int guard = 0;
    while (pub.phase() != MqttPublisher::Phase::Live && guard < 1000) {
        pub.pump(t, f.config);
        ++guard;
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MqttPublisher::Phase::Live), static_cast<int>(pub.phase()));

    // Layout: [0, total) discovery configs, [total] availability online,
    // [total+1, 2*total+1) state topics, then exactly one subscription.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(2 * total + 1), static_cast<int>(t.publishes.size()));
    for (size_t i = 0; i < total; ++i) {
        TEST_ASSERT_TRUE(t.publishes[i].topic.find("/config") != std::string::npos);
        TEST_ASSERT_TRUE(t.publishes[i].retain);
    }
    TEST_ASSERT_EQUAL_STRING("boiler-room/status", t.publishes[total].topic.c_str());
    TEST_ASSERT_EQUAL_STRING("online", t.publishes[total].payload.c_str());
    TEST_ASSERT_TRUE(t.publishes[total].retain);
    for (size_t i = total + 1; i < 2 * total + 1; ++i) {
        TEST_ASSERT_TRUE(t.publishes[i].topic.find("/state") != std::string::npos);
        TEST_ASSERT_TRUE(t.publishes[i].retain);
    }
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(t.subscriptions.size()));
    TEST_ASSERT_EQUAL_STRING("boiler-room/+/set", t.subscriptions[0].c_str());
}

static void pub_test_throttle_max_4_per_pump() {
    PubFixture f;
    TEST_ASSERT_TRUE(f.registry.count() >= MqttPublisher::MAX_PER_PUMP);
    CommonState state{};
    MqttPublisher pub;
    pub.begin(f.registry, PUB_TEST_ID, "1.0.0");
    pub.onSessionStart(0);
    pub.refresh(state, f.config, 0);

    FakeMqttTransport t;
    size_t sent = pub.pump(t, f.config);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MqttPublisher::MAX_PER_PUMP), static_cast<int>(sent));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MqttPublisher::MAX_PER_PUMP), static_cast<int>(t.publishes.size()));
}

static void pub_test_publish_failure_retries_same_item() {
    PubFixture f;
    CommonState state{};
    MqttPublisher pub;
    pub.begin(f.registry, PUB_TEST_ID, "1.0.0");
    pub.onSessionStart(0);
    pub.refresh(state, f.config, 0);

    FakeMqttTransport t;
    t.failNextPublishes = 1;
    size_t sent = pub.pump(t, f.config);
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(sent));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(t.publishes.size()));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(pub.cursor()));  // did not advance past the failed item

    // Retried on the very next pump(): the first item published is the one
    // that had failed (entity 0's discovery topic), then the pipeline
    // continues normally up to the per-pump budget.
    sent = pub.pump(t, f.config);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MqttPublisher::MAX_PER_PUMP), static_cast<int>(sent));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MqttPublisher::MAX_PER_PUMP), static_cast<int>(t.publishes.size()));
    char expectedTopic[HA_TOPIC_MAX];
    TEST_ASSERT_TRUE(buildDiscoveryTopic(PUB_TEST_ID, f.registry.entity(0), expectedTopic, sizeof(expectedTopic)));
    TEST_ASSERT_EQUAL_STRING(expectedTopic, t.publishes[0].topic.c_str());
}

static void pub_test_live_failure_retries_and_reaches_later_dirty_entity() {
    PubFixture f;
    CommonState state{};
    MqttPublisher pub;
    pub.begin(f.registry, PUB_TEST_ID, "1.0.0");
    pub.onSessionStart(0);
    pub.refresh(state, f.config, 0);
    FakeMqttTransport t;
    advanceToLive(pub, t, f.config);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MqttPublisher::Phase::Live), static_cast<int>(pub.phase()));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(pub.pump(t, f.config)));  // nothing dirty after the initial states
    size_t before = t.publishes.size();

    // Two dirty entities far apart: A = index 0, B = the last index.
    const size_t a = 0;
    const size_t b = f.registry.count() - 1;
    TEST_ASSERT_TRUE(b > a + 1);
    pub.markDirty(a);
    pub.markDirty(b);

    // First Live publish attempt (entity A) fails: nothing sent, nothing lost.
    t.failNextPublishes = 1;
    size_t sent = pub.pump(t, f.config);
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(sent));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(before), static_cast<int>(t.publishes.size()));

    // Next pump: the retry resumes at A, then B is still reached within the
    // same MAX_PER_PUMP budget.
    sent = pub.pump(t, f.config);
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(sent));
    TEST_ASSERT_TRUE(sent <= MqttPublisher::MAX_PER_PUMP);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(before + 2), static_cast<int>(t.publishes.size()));
    char topicA[HA_TOPIC_MAX];
    char topicB[HA_TOPIC_MAX];
    TEST_ASSERT_TRUE(buildStateTopic(PUB_TEST_ID, f.registry.entity(a), topicA, sizeof(topicA)));
    TEST_ASSERT_TRUE(buildStateTopic(PUB_TEST_ID, f.registry.entity(b), topicB, sizeof(topicB)));
    TEST_ASSERT_EQUAL_STRING(topicA, t.publishes[before].topic.c_str());
    TEST_ASSERT_EQUAL_STRING(topicB, t.publishes[before + 1].topic.c_str());
    TEST_ASSERT_TRUE(t.publishes[before].retain);
    TEST_ASSERT_TRUE(t.publishes[before + 1].retain);

    // Both are clean afterwards: nothing more to send.
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(pub.pump(t, f.config)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(before + 2), static_cast<int>(t.publishes.size()));
}

static void pub_test_change_publishes_only_dirty() {
    PubFixture f;
    CommonState state{};
    state.sensors.sensor[0].state = SensorState::Ok;
    state.sensors.sensor[0].tempC = 20.0f;

    MqttPublisher pub;
    pub.begin(f.registry, PUB_TEST_ID, "1.0.0");
    pub.onSessionStart(0);
    pub.refresh(state, f.config, 0);
    FakeMqttTransport t;
    advanceToLive(pub, t, f.config);
    size_t before = t.publishes.size();

    state.sensors.sensor[0].tempC = 25.5f;
    pub.refresh(state, f.config, 1000);
    size_t sent = pub.pump(t, f.config);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(sent));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(before + 1), static_cast<int>(t.publishes.size()));
    TEST_ASSERT_EQUAL_STRING("boiler-room/temp_t1/state", t.publishes.back().topic.c_str());
    TEST_ASSERT_EQUAL_STRING("25.5", t.publishes.back().payload.c_str());
}

static void pub_test_full_refresh_every_60s() {
    PubFixture f;
    CommonState state{};
    MqttPublisher pub;
    pub.begin(f.registry, PUB_TEST_ID, "1.0.0");
    pub.onSessionStart(0);
    pub.refresh(state, f.config, 0);
    FakeMqttTransport t;
    advanceToLive(pub, t, f.config);
    size_t before = t.publishes.size();

    pub.refresh(state, f.config, 59999);  // unchanged, not yet 60s since begin()
    size_t sent = 0;
    for (int i = 0; i < 5; ++i) {
        sent += pub.pump(t, f.config);
    }
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(sent));

    pub.refresh(state, f.config, 60000);  // full refresh boundary: every entity dirty
    size_t total = f.registry.count();
    size_t sentAfter = 0;
    int guard = 0;
    while (sentAfter < total && guard < 1000) {
        sentAfter += pub.pump(t, f.config);
        ++guard;
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(total), static_cast<int>(sentAfter));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(before + total), static_cast<int>(t.publishes.size()));
}

static void pub_test_mark_dirty_republishes_unchanged_value() {
    PubFixture f;
    CommonState state{};
    MqttPublisher pub;
    pub.begin(f.registry, PUB_TEST_ID, "1.0.0");
    pub.onSessionStart(0);
    pub.refresh(state, f.config, 0);
    FakeMqttTransport t;
    advanceToLive(pub, t, f.config);
    size_t before = t.publishes.size();

    int idx = pubFindKey(f.registry, "relay_lock");
    TEST_ASSERT_TRUE(idx >= 0);
    pub.markDirty(static_cast<size_t>(idx));
    size_t sent = pub.pump(t, f.config);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(sent));
    TEST_ASSERT_EQUAL_STRING("boiler-room/relay_lock/state", t.publishes[before].topic.c_str());
    TEST_ASSERT_EQUAL_STRING("60", t.publishes[before].payload.c_str());  // unchanged default value
}

static void pub_test_events_non_retained_and_outbox_drop_oldest() {
    PubFixture f;
    CommonState state{};
    MqttPublisher pub;
    pub.begin(f.registry, PUB_TEST_ID, "1.0.0");
    pub.onSessionStart(0);
    pub.refresh(state, f.config, 0);
    FakeMqttTransport t;
    advanceToLive(pub, t, f.config);
    size_t before = t.publishes.size();

    for (uint32_t seq = 1; seq <= MqttPublisher::EVENT_OUTBOX + 3; ++seq) {
        EventEntry e{};
        e.seq = seq;
        e.timestamp = seq * 10;
        e.type = toU16(EventType::WifiConnected);
        e.source = EVENT_SOURCE_WIFI;
        e.value = static_cast<float>(seq);
        e.aux = 0;
        e.reason = static_cast<uint8_t>(EventReason::Logic);
        pub.enqueueEvent(e);
    }
    // 11 enqueued, capacity 8: the 3 oldest (seq 1..3) are dropped, 4..11 survive.

    size_t sent = 0;
    int guard = 0;
    while (sent < MqttPublisher::EVENT_OUTBOX && guard < 1000) {
        sent += pub.pump(t, f.config);
        ++guard;
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MqttPublisher::EVENT_OUTBOX), static_cast<int>(sent));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(before + MqttPublisher::EVENT_OUTBOX), static_cast<int>(t.publishes.size()));

    for (size_t i = 0; i < MqttPublisher::EVENT_OUTBOX; ++i) {
        const FakeMqttTransport::Publish& p = t.publishes[before + i];
        TEST_ASSERT_EQUAL_STRING("boiler-room/event", p.topic.c_str());
        TEST_ASSERT_FALSE(p.retain);
        JsonDocument doc;
        TEST_ASSERT_FALSE(deserializeJson(doc, p.payload.c_str()));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(4 + i), doc["seq"].as<int>());
        TEST_ASSERT_EQUAL_STRING("wifi_connected", doc["type"].as<const char*>());
    }
}

static void pub_test_events_ignored_without_session() {
    PubFixture f;
    MqttPublisher pub;
    pub.begin(f.registry, PUB_TEST_ID, "1.0.0");

    // Not yet in a session (Idle): enqueue is ignored.
    EventEntry e{};
    e.seq = 99;
    e.type = toU16(EventType::WifiConnected);
    e.source = EVENT_SOURCE_WIFI;
    pub.enqueueEvent(e);

    CommonState state{};
    pub.onSessionStart(0);
    pub.refresh(state, f.config, 0);
    FakeMqttTransport t;
    advanceToLive(pub, t, f.config);

    // No event topic published: the pre-session enqueue never took effect.
    for (const FakeMqttTransport::Publish& p : t.publishes) {
        TEST_ASSERT_TRUE(p.topic.find("/event") == std::string::npos);
    }
}

static void pub_test_session_restart_republishes_discovery() {
    PubFixture f;
    CommonState state{};
    MqttPublisher pub;
    pub.begin(f.registry, PUB_TEST_ID, "1.0.0");
    pub.onSessionStart(0);
    pub.refresh(state, f.config, 0);
    FakeMqttTransport t;
    advanceToLive(pub, t, f.config);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MqttPublisher::Phase::Live), static_cast<int>(pub.phase()));

    pub.onSessionEnd();
    TEST_ASSERT_FALSE(pub.sessionActive());

    pub.onSessionStart(5000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(MqttPublisher::Phase::Discovery), static_cast<int>(pub.phase()));
    size_t sent = pub.pump(t, f.config);
    TEST_ASSERT_TRUE(sent > 0);
    TEST_ASSERT_TRUE(t.publishes.back().topic.find("/config") != std::string::npos);
}

static void pub_test_discovery_payload_size_budget() {
    static constexpr size_t N = 60;
    static SettingDescriptor bigSettings[N];
    static char keyBuf[N][8];
    static char nvsBuf[N][8];
    for (size_t i = 0; i < N; ++i) {
        snprintf(keyBuf[i], sizeof(keyBuf[i]), "n%03zu", i);
        snprintf(nvsBuf[i], sizeof(nvsBuf[i]), "n%03zu", i);
        bigSettings[i] = intSetting(keyBuf[i], nvsBuf[i], "Budget number", "Бюджетне число", "s", "test", 0, 100, 0);
    }
    SettingsTable bigTable{bigSettings, N};
    SettingsTable tables[] = {COMMON_SETTINGS_TABLE, bigTable};
    ConfigSchema schema{"test-budget", 1, tables, 2, nullptr, 0};

    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(schema, 0)));

    HwProjectConfig hw{nullptr, 0, nullptr, 0, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};
    HaEntityRegistry reg;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(reg.build(config, hw, nullptr, 0)));
    // N numbers plus HA_COMMON_ENTITIES (rssi), always appended (D8).
    TEST_ASSERT_EQUAL_INT(static_cast<int>(N + HA_COMMON_ENTITY_COUNT), static_cast<int>(reg.count()));

    char payload[HA_DISCOVERY_PAYLOAD_MAX];
    for (size_t i = 0; i < reg.count(); ++i) {
        size_t len = buildDiscoveryPayload(reg, i, config, PUB_TEST_ID, "1.0.0", payload, sizeof(payload));
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_TRUE(len < HA_DISCOVERY_PAYLOAD_MAX);
    }
}

// Runs every test in this suite. Call from runNetSuite().
inline void runPublishSuite() {
    RUN_TEST(pub_test_topics_layout);
    RUN_TEST(pub_test_discovery_number_payload);
    RUN_TEST(pub_test_discovery_switch_and_config_switch);
    RUN_TEST(pub_test_discovery_sensor_and_relay_binary);
    RUN_TEST(pub_test_no_secret_entities_in_any_payload);
    RUN_TEST(pub_test_pipeline_order_discovery_availability_states_subscribe);
    RUN_TEST(pub_test_throttle_max_4_per_pump);
    RUN_TEST(pub_test_publish_failure_retries_same_item);
    RUN_TEST(pub_test_live_failure_retries_and_reaches_later_dirty_entity);
    RUN_TEST(pub_test_change_publishes_only_dirty);
    RUN_TEST(pub_test_full_refresh_every_60s);
    RUN_TEST(pub_test_mark_dirty_republishes_unchanged_value);
    RUN_TEST(pub_test_events_non_retained_and_outbox_drop_oldest);
    RUN_TEST(pub_test_events_ignored_without_session);
    RUN_TEST(pub_test_session_restart_republishes_discovery);
    RUN_TEST(pub_test_discovery_payload_size_budget);
}
