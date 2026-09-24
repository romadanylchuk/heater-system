#pragma once
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "../lib/CoreEngine/src/Command.h"
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/CoreEngine/src/CoreRuntime.h"
#include "../lib/CoreEngine/src/EventLog.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/NetEngine/src/HaEntityRegistry.h"
#include "../lib/NetEngine/src/MqttInbound.h"
#include "../lib/NetEngine/src/MqttSession.h"
#include "../lib/NetEngine/src/NetSignals.h"
#include "fakes/FakeClock.h"
#include "fakes/InMemoryCommandQueue.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/RecordingEventSink.h"
#include "fakes/TestSchema.h"

// Native-safe Unity tests for stage 04 phase 4: MqttSession (the pure MQTT
// connection-lifecycle machine, D6) and the inbound `<prefix>/<key>/set`
// command parser (D11, over a HaEntityRegistry built from TEST_SCHEMA_A_V1).
// HaEntityRegistry itself is covered by HaSuite.h. Header-only, run via
// NetSuite.h. Test names are prefixed mqtt_ (D24).

namespace {

inline constexpr LogicalSensorDesc MQTT_TEST_SENSORS[] = {{"T1", "dummySensorKey"}};
inline constexpr HwProjectConfig MQTT_TEST_HW = {
    nullptr, 0, MQTT_TEST_SENSORS, 1, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};

MqttEndpoint mqttEp(const char* host, uint16_t port = 1883) {
    MqttEndpoint ep{};
    strncpy(ep.host, host, sizeof(ep.host) - 1);
    ep.port = port;
    return ep;
}

// A ConfigEngine (over TEST_SCHEMA_A_V1) plus the HaEntityRegistry built from
// it, ready for handleMqttCommand()/CoreRuntime tests.
struct InboundFixture {
    MemoryKvStore cfgStore;
    MemoryKvStore logStore;
    FakeClock clock;
    EventLog log;
    ConfigEngine config;
    HaEntityRegistry registry;
    InMemoryCommandQueue queue;
    NetSignals signals;

    InboundFixture() : log(logStore, clock), config(cfgStore, log) {
        TEST_ASSERT_TRUE(log.begin());
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok),
            static_cast<int>(registry.build(config, MQTT_TEST_HW, nullptr, 0)));
    }
};

}  // namespace

// ---- MqttSession --------------------------------------------------------

static void mqtt_test_empty_host_disabled_no_attempts_no_logs() {
    MqttEndpoint ep{};  // host empty: disabled
    MqttSession session;
    RecordingEventSink events;
    session.begin(ep, 0);
    TEST_ASSERT_FALSE(session.enabled());

    for (uint64_t t = 0; t <= 200000; t += 20000) {
        MqttSessionActions a = session.tick(ep, true, false, t, events);
        TEST_ASSERT_FALSE(a.connect);
        TEST_ASSERT_FALSE(a.configure);
    }
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(events.count()));
}

static void mqtt_test_connect_only_when_wifi_up() {
    MqttEndpoint ep = mqttEp("broker.local");
    MqttSession session;
    RecordingEventSink events;
    session.begin(ep, 0);

    MqttSessionActions a = session.tick(ep, false, false, 0, events);
    TEST_ASSERT_TRUE(a.configure);  // configuring the transport does not need Wi-Fi
    TEST_ASSERT_FALSE(a.connect);   // but attempting a TCP connect does

    a = session.tick(ep, false, false, 5000, events);
    TEST_ASSERT_FALSE(a.connect);

    a = session.tick(ep, true, false, 6000, events);
    TEST_ASSERT_TRUE(a.connect);  // Wi-Fi up: immediate attempt
}

static void mqtt_test_backoff_5_10_20_40_60_cap() {
    MqttEndpoint ep = mqttEp("broker.local");
    MqttSession session;
    RecordingEventSink events;
    session.begin(ep, 0);
    MqttSessionActions first = session.tick(ep, true, false, 0, events);
    TEST_ASSERT_TRUE(first.connect);  // attempt #0, immediate

    const uint32_t gaps[] = {5000, 10000, 20000, 40000, 60000, 60000};
    uint64_t t = 0;
    for (size_t i = 0; i < 6; ++i) {
        t += gaps[i];
        MqttSessionActions before = session.tick(ep, true, false, t - 1, events);
        TEST_ASSERT_FALSE(before.connect);
        MqttSessionActions fire = session.tick(ep, true, false, t, events);
        TEST_ASSERT_TRUE(fire.connect);
    }
}

static void mqtt_test_failure_logged_once_per_wifi_session() {
    MqttEndpoint ep = mqttEp("broker.local");
    MqttSession session;
    RecordingEventSink events;
    session.begin(ep, 0);

    session.tick(ep, true, false, 0, events);      // attempt #0: no prior attempt, no log
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(events.countOf(EventType::MqttDisconnected)));

    session.tick(ep, true, false, 5000, events);    // attempt #1: #0 failed -> logged once
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(events.countOf(EventType::MqttDisconnected)));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, events.at(events.count() - 1).value);

    session.tick(ep, true, false, 15000, events);   // attempt #2: still failing, no more logs
    session.tick(ep, true, false, 35000, events);   // attempt #3
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(events.countOf(EventType::MqttDisconnected)));
}

static void mqtt_test_connected_logs_and_session_started() {
    MqttEndpoint ep = mqttEp("broker.local");
    MqttSession session;
    RecordingEventSink events;
    session.begin(ep, 0);
    session.tick(ep, true, false, 0, events);

    MqttSessionActions a = session.tick(ep, true, true, 100, events);
    TEST_ASSERT_TRUE(a.sessionStarted);
    TEST_ASSERT_TRUE(session.connected());
    TEST_ASSERT_EQUAL_UINT32(1, session.connectCount());
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(events.countOf(EventType::MqttConnected)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, events.at(events.count() - 1).value);
}

static void mqtt_test_wifi_drop_forces_disconnect_and_effective_false_same_tick() {
    MqttEndpoint ep = mqttEp("broker.local");
    MqttSession session;
    RecordingEventSink events;
    session.begin(ep, 0);
    session.tick(ep, true, false, 0, events);
    session.tick(ep, true, true, 100, events);
    TEST_ASSERT_TRUE(session.connected());

    MqttSessionActions a = session.tick(ep, false, true, 200, events);  // port hasn't executed the force-close yet
    TEST_ASSERT_TRUE(a.forceDisconnect);
    TEST_ASSERT_TRUE(a.sessionEnded);
    TEST_ASSERT_FALSE(session.connected());  // effective false on the very same tick
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(events.countOf(EventType::MqttDisconnected)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, events.at(events.count() - 1).value);  // lost because Wi-Fi went down
}

static void mqtt_test_endpoint_change_reconfigures() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    config.setText(commonIndex(CommonSetting::MqttHost), "broker1.local", EventReason::Web, 0);

    MqttEndpoint ep1{};
    TEST_ASSERT_TRUE(mqttEndpointFromConfig(config, ep1));
    TEST_ASSERT_EQUAL_STRING("broker1.local", ep1.host);

    MqttSession session;
    RecordingEventSink events;
    session.begin(ep1, 0);
    MqttSessionActions a = session.tick(ep1, true, false, 0, events);
    TEST_ASSERT_TRUE(a.configure);
    TEST_ASSERT_TRUE(a.connect);
    a = session.tick(ep1, true, true, 100, events);
    TEST_ASSERT_TRUE(a.sessionStarted);
    TEST_ASSERT_TRUE(session.connected());

    config.setText(commonIndex(CommonSetting::MqttHost), "broker2.local", EventReason::Web, 200);
    MqttEndpoint ep2{};
    TEST_ASSERT_TRUE(mqttEndpointFromConfig(config, ep2));
    TEST_ASSERT_FALSE(mqttEndpointEquals(ep1, ep2));

    a = session.tick(ep2, true, true, 300, events);  // endpoint changed while the transport is still connected
    TEST_ASSERT_TRUE(a.disconnect);

    a = session.tick(ep2, true, false, 400, events);  // port executed the disconnect: reconfigure + reattempt
    TEST_ASSERT_TRUE(a.configure);
    TEST_ASSERT_TRUE(a.connect);
    TEST_ASSERT_FALSE(session.connected());
}

static void mqtt_test_reconnect_after_wifi_return_immediate() {
    MqttEndpoint ep = mqttEp("broker.local");
    MqttSession session;
    RecordingEventSink events;
    session.begin(ep, 0);
    session.tick(ep, true, false, 0, events);
    session.tick(ep, true, true, 100, events);
    session.tick(ep, false, true, 200, events);  // Wi-Fi drop: forceDisconnect, session ends

    MqttSessionActions a = session.tick(ep, true, false, 250, events);  // Wi-Fi returns, port now disconnected
    TEST_ASSERT_TRUE(a.connect);  // immediate, not waiting for the backoff schedule
}

// ---- Inbound command parsing --------------------------------------------

static void mqtt_test_inbound_number_posts_command() {
    InboundFixture f;
    int idx = f.registry.findByKey("test_int", strlen("test_int"));
    TEST_ASSERT_TRUE(idx >= 0);

    InboundResult r = handleMqttCommand(
        f.registry, "boiler-room", "boiler-room/test_int/set", "77", strlen("77"), f.queue, f.signals);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InboundResult::Posted), static_cast<int>(r));

    Command cmd;
    TEST_ASSERT_TRUE(f.queue.tryReceive(cmd));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::SetNumber), static_cast<int>(cmd.type));
    TEST_ASSERT_EQUAL_UINT16(f.registry.entity(static_cast<size_t>(idx)).ref, cmd.settingIndex);
    TEST_ASSERT_EQUAL_FLOAT(77.0f, cmd.number);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventReason::Mqtt), static_cast<int>(cmd.origin));

    uint32_t out[4] = {};
    TEST_ASSERT_TRUE(f.signals.republish.takeAll(out));
}

static void mqtt_test_inbound_switch_on_off_variants() {
    InboundFixture f;
    const char* variants[] = {"ON", "off", "1", "0", "true", "FALSE"};
    const float expected[] = {1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
    for (size_t i = 0; i < 6; ++i) {
        InboundResult r = handleMqttCommand(f.registry, "boiler-room", "boiler-room/test_bool/set", variants[i],
            strlen(variants[i]), f.queue, f.signals);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(InboundResult::Posted), static_cast<int>(r));
        Command cmd;
        TEST_ASSERT_TRUE(f.queue.tryReceive(cmd));
        TEST_ASSERT_EQUAL_FLOAT(expected[i], cmd.number);
    }
}

static void mqtt_test_inbound_nan_inf_text_rejected_republish_set() {
    InboundFixture f;
    const char* bad[] = {"nan", "inf", "abc", "12x", "  "};
    for (const char* p : bad) {
        InboundResult r =
            handleMqttCommand(f.registry, "boiler-room", "boiler-room/test_float/set", p, strlen(p), f.queue,
                f.signals);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(InboundResult::BadPayload), static_cast<int>(r));
    }
    Command cmd;
    TEST_ASSERT_FALSE(f.queue.tryReceive(cmd));  // never posted
    uint32_t out[4] = {};
    TEST_ASSERT_TRUE(f.signals.republish.takeAll(out));  // republish still set so HA gets the real value back
}

static void mqtt_test_inbound_empty_and_oversized_payload_rejected_republish_set() {
    InboundFixture f;
    int idx = f.registry.findByKey("test_float", strlen("test_float"));
    TEST_ASSERT_TRUE(idx >= 0);
    const size_t bit = static_cast<size_t>(idx);

    // 33 bytes (MQTT_CMD_PAYLOAD_MAX + 1) of an otherwise valid number.
    char big[MQTT_CMD_PAYLOAD_MAX + 2];
    memset(big, '1', MQTT_CMD_PAYLOAD_MAX + 1);
    big[MQTT_CMD_PAYLOAD_MAX + 1] = '\0';
    TEST_ASSERT_EQUAL_UINT32(33, strlen(big));

    struct Case {
        const char* payload;
        size_t len;
    };
    const Case cases[] = {
        {"", 0},                          // empty payload
        {"5", 0},                         // non-empty buffer but len 0
        {big, MQTT_CMD_PAYLOAD_MAX + 1},  // oversized (33 B)
    };
    for (const Case& c : cases) {
        InboundResult r = handleMqttCommand(
            f.registry, "boiler-room", "boiler-room/test_float/set", c.payload, c.len, f.queue, f.signals);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(InboundResult::BadPayload), static_cast<int>(r));

        Command cmd;
        TEST_ASSERT_FALSE(f.queue.tryReceive(cmd));  // nothing queued
        uint32_t out[4] = {};
        TEST_ASSERT_TRUE(f.signals.republish.takeAll(out));  // HA still gets the real value echoed back
        TEST_ASSERT_TRUE((out[bit / 32] & (1u << (bit % 32))) != 0);
    }
    TEST_ASSERT_EQUAL_UINT32(0, f.signals.droppedCommands.load());

    // Boundary: exactly MQTT_CMD_PAYLOAD_MAX bytes is still accepted.
    char max[MQTT_CMD_PAYLOAD_MAX + 1];
    memset(max, '0', MQTT_CMD_PAYLOAD_MAX);
    max[MQTT_CMD_PAYLOAD_MAX - 1] = '7';
    max[MQTT_CMD_PAYLOAD_MAX] = '\0';
    InboundResult r = handleMqttCommand(
        f.registry, "boiler-room", "boiler-room/test_float/set", max, MQTT_CMD_PAYLOAD_MAX, f.queue, f.signals);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InboundResult::Posted), static_cast<int>(r));
    Command cmd;
    TEST_ASSERT_TRUE(f.queue.tryReceive(cmd));
    TEST_ASSERT_EQUAL_FLOAT(7.0f, cmd.number);
}

static void mqtt_test_inbound_unknown_topic_ignored() {
    InboundFixture f;
    const char* topics[] = {
        "wrong-prefix/test_int/set",   // wrong prefix
        "boiler-room/test_int/state",  // wrong suffix
        "boiler-room/unknown_key/set",  // unknown key
    };
    for (const char* t : topics) {
        InboundResult r = handleMqttCommand(f.registry, "boiler-room", t, "1", 1, f.queue, f.signals);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(InboundResult::UnknownTopic), static_cast<int>(r));
    }
    uint32_t out[4] = {};
    TEST_ASSERT_FALSE(f.signals.republish.takeAll(out));
    Command cmd;
    TEST_ASSERT_FALSE(f.queue.tryReceive(cmd));
}

static void mqtt_test_inbound_sensor_not_commandable() {
    InboundFixture f;
    InboundResult r =
        handleMqttCommand(f.registry, "boiler-room", "boiler-room/temp_t1/set", "1", 1, f.queue, f.signals);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InboundResult::NotCommandable), static_cast<int>(r));
    uint32_t out[4] = {};
    TEST_ASSERT_FALSE(f.signals.republish.takeAll(out));
}

static void mqtt_test_inbound_queue_full_drops_and_counts() {
    InboundFixture f;
    for (size_t i = 0; i < InMemoryCommandQueue::DEPTH; ++i) {
        TEST_ASSERT_TRUE(f.queue.post(makeSetNumber(0, 0.0f, EventReason::Web, 0)));
    }
    InboundResult r =
        handleMqttCommand(f.registry, "boiler-room", "boiler-room/test_int/set", "5", 1, f.queue, f.signals);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InboundResult::QueueFull), static_cast<int>(r));
    TEST_ASSERT_EQUAL_UINT32(1, f.signals.droppedCommands.load());
    uint32_t out[4] = {};
    TEST_ASSERT_TRUE(f.signals.republish.takeAll(out));
}

static void mqtt_test_inbound_out_of_range_posts_and_engine_clamps() {
    InboundFixture f;
    CommonState state{};
    CoreRuntime runtime(state, f.config, f.log, f.queue);

    InboundResult r =
        handleMqttCommand(f.registry, "boiler-room", "boiler-room/test_int/set", "999", 3, f.queue, f.signals);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InboundResult::Posted), static_cast<int>(r));

    runtime.tick(0);

    TEST_ASSERT_EQUAL_FLOAT(100.0f, f.config.getNumber(TEST_INDEX_INT));  // TEST_SETTINGS_V1: testInt max = 100
    const EventEntry* e = f.log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigClamped), e->type);
}

// Runs every test in this suite. Call from runNetSuite().
inline void runMqttSuite() {
    RUN_TEST(mqtt_test_empty_host_disabled_no_attempts_no_logs);
    RUN_TEST(mqtt_test_connect_only_when_wifi_up);
    RUN_TEST(mqtt_test_backoff_5_10_20_40_60_cap);
    RUN_TEST(mqtt_test_failure_logged_once_per_wifi_session);
    RUN_TEST(mqtt_test_connected_logs_and_session_started);
    RUN_TEST(mqtt_test_wifi_drop_forces_disconnect_and_effective_false_same_tick);
    RUN_TEST(mqtt_test_endpoint_change_reconfigures);
    RUN_TEST(mqtt_test_reconnect_after_wifi_return_immediate);
    RUN_TEST(mqtt_test_inbound_number_posts_command);
    RUN_TEST(mqtt_test_inbound_switch_on_off_variants);
    RUN_TEST(mqtt_test_inbound_nan_inf_text_rejected_republish_set);
    RUN_TEST(mqtt_test_inbound_empty_and_oversized_payload_rejected_republish_set);
    RUN_TEST(mqtt_test_inbound_unknown_topic_ignored);
    RUN_TEST(mqtt_test_inbound_sensor_not_commandable);
    RUN_TEST(mqtt_test_inbound_queue_full_drops_and_counts);
    RUN_TEST(mqtt_test_inbound_out_of_range_posts_and_engine_clamps);
}
