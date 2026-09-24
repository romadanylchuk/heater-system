#pragma once
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "../lib/CoreEngine/src/Command.h"
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/CoreEngine/src/CoreRuntime.h"
#include "../lib/CoreEngine/src/EventEntry.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/HwEngine/src/HwSettings.h"
#include "../lib/NetEngine/src/ConnectivityRuntime.h"
#include "../lib/NetEngine/src/HaDiscovery.h"
#include "../lib/NetEngine/src/HaEntityRegistry.h"
#include "../lib/NetEngine/src/HaLinkGate.h"
#include "../lib/NetEngine/src/MqttInbound.h"
#include "../lib/NetEngine/src/NetIdentity.h"
#include "../lib/NetEngine/src/NetSignals.h"
#include "../lib/NetEngine/src/OtaBoot.h"
#include "../lib/NetEngine/src/OtaGuard.h"
#include "../lib/NetEngine/src/OtaPlatform.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeMqttTransport.h"
#include "fakes/FakeOtaPlatform.h"
#include "fakes/FakeWifiPort.h"
#include "fakes/InMemoryCommandQueue.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/RecordingEventSink.h"

// Native-safe Unity tests for stage 04 phase 7: ConnectivityRuntime, the pure
// orchestrator that drives WifiSupervisor/MqttSession/MqttPublisher/
// OtaGuard/OtaBoot behind WifiPort/MqttTransport/OtaPlatform/KvStore.
// Header-only, run via NetSuite.h. Test names are prefixed net_ (D24).
//
// A local schema (COMMON_SETTINGS_TABLE + HW_SETTINGS_TABLE + a dedicated
// test table with one HA_SWITCH bool) plus a 1-relay (Pump)/1-sensor test
// HwProjectConfig, kept local to this file so it never affects other suites
// (mirrors PublishSuite.h).

namespace {

inline constexpr SettingDescriptor NET_TEST_SETTINGS[] = {
    boolSetting("haSwitchBool", "haSwBl", "HA switch bool", "HA-перемикач", "test", false, SETTING_FLAG_HA_SWITCH),
};
inline constexpr SettingsTable NET_TEST_SETTINGS_TABLE = makeTable(NET_TEST_SETTINGS);
inline constexpr SettingsTable NET_TEST_SCHEMA_TABLES[] = {
    COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE, NET_TEST_SETTINGS_TABLE};
inline constexpr ConfigSchema NET_TEST_SCHEMA = {"test-net", 1, NET_TEST_SCHEMA_TABLES, 3, nullptr, 0};

inline constexpr RelayChannelDesc NET_TEST_RELAYS[] = {
    {0, "P1", RelayRole::Pump, true, true},
};
inline constexpr LogicalSensorDesc NET_TEST_SENSORS[] = {
    {"T1", "sensorT1"},
};
inline constexpr HwProjectConfig NET_TEST_HW = {
    NET_TEST_RELAYS, 1, NET_TEST_SENSORS, 1, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};

inline constexpr NetIdentity NET_TEST_ID = {
    "boiler-room", "boiler_room", "Boiler room", "KC868-A6 boiler-room", "Test-Setup"};

// Full fixture: config over a fresh MemoryKvStore/RecordingEventSink, a
// separate MemoryKvStore for the OTA marker namespace, the fakes, NetSignals
// and CommonState, and a ConnectivityRuntime wired over all of them. begin()
// is left to each test (some need to seed the OTA marker/config first).
struct NetFixture {
    MemoryKvStore cfgStore;
    MemoryKvStore otaStore;
    RecordingEventSink events;
    ConfigEngine config;
    FakeWifiPort wifi;
    FakeMqttTransport mqtt;
    FakeOtaPlatform ota;
    NetSignals signals;
    CommonState state{};
    ConnectivityRuntime runtime;

    NetFixture()
        : config(cfgStore, events),
          runtime(state, config, events, wifi, mqtt, ota, otaStore, signals) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(NET_TEST_SCHEMA, 0)));
    }

    NetBeginStatus begin(uint64_t nowMs = 0, const char* fw = "1.0.0", const char* web = "1.0.0",
        const char* clientId = "boiler-room-abc123") {
        return runtime.begin(NET_TEST_ID, NET_TEST_HW, nullptr, 0, fw, web, clientId, nowMs);
    }

    void setSsid(const char* ssid, const char* pass = "") {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
            static_cast<int>(config.setText(commonIndex(CommonSetting::WifiSsid), ssid, EventReason::Web, 0)));
        TEST_ASSERT_TRUE(static_cast<int>(ConfigStatus::Rejected) !=
            static_cast<int>(config.setText(commonIndex(CommonSetting::WifiPass), pass, EventReason::Web, 0)));
    }

    void setMqttHost(const char* host, uint16_t port = 1883) {
        config.setText(commonIndex(CommonSetting::MqttHost), host, EventReason::Web, 0);
        config.setNumber(commonIndex(CommonSetting::MqttPort), static_cast<float>(port), EventReason::Web, 0);
    }
};

int netFindKey(const HaEntityRegistry& reg, const char* key) { return reg.findByKey(key, strlen(key)); }

// Ticks until the MQTT transport reports connected (or a generous guard
// trips), driving both the ~1s tick() and the ~100ms fastTick() each step so
// the publish pipeline (driven from fastTick) actually advances.
void netAdvanceToMqttConnected(NetFixture& f, uint64_t& nowMs) {
    int guard = 0;
    while (!f.mqtt.connected() && guard < 200) {
        f.runtime.tick(nowMs);
        if (f.mqtt.configured && !f.mqtt.connectedFlag && f.mqtt.connectCount > 0) {
            f.mqtt.connectedFlag = true;  // simulate the broker accepting the connection
        }
        f.runtime.fastTick(nowMs);
        nowMs += 100;
        ++guard;
    }
    // One more slow tick so MqttSession observes the connected transport and
    // reports sessionStarted (the publisher only starts on that edge).
    f.runtime.tick(nowMs);
    TEST_ASSERT_TRUE(f.state.network.mqttConnected);
}

void netPumpUntilLive(NetFixture& f, uint64_t& nowMs, size_t guardMax = 2000) {
    size_t guard = 0;
    while (guard < guardMax) {
        f.runtime.fastTick(nowMs);
        nowMs += 100;
        ++guard;
    }
}

}  // namespace

// ---- begin(): identity / setup-AP / join --------------------------------

static void net_test_invalid_identity_inert() {
    NetFixture f;
    const size_t eventsBefore = f.events.count();
    NetIdentity bad{"boiler", "boiler", "Boiler room", "model", "Setup"};  // "boiler" is rejected (D-Global)
    NetBeginStatus st = f.runtime.begin(bad, NET_TEST_HW, nullptr, 0, "1.0.0", "", "cid", 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NetBeginStatus::InvalidIdentity), static_cast<int>(st));
    TEST_ASSERT_EQUAL_INT(0, f.wifi.initCount);
    TEST_ASSERT_EQUAL_INT(0, f.wifi.connectCount);
    TEST_ASSERT_EQUAL_INT(0, f.wifi.startApCount);
    TEST_ASSERT_FALSE(f.mqtt.configured);

    // tick()/fastTick() after a failed begin() stay inert too: no port touched.
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    NetTickEvents ev = f.runtime.tick(1000);
    f.runtime.fastTick(1100);
    TEST_ASSERT_FALSE(ev.wifiUp || ev.wifiDown || ev.adminCredsChanged);
    TEST_ASSERT_EQUAL_INT(0, f.wifi.initCount + f.wifi.connectCount + f.wifi.startApCount + f.wifi.startScanCount);
    TEST_ASSERT_EQUAL_INT(0, f.mqtt.connectCount);
    TEST_ASSERT_FALSE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(eventsBefore), static_cast<int>(f.events.count()));
}

static void net_test_no_ssid_ap_state_for_oled() {
    NetFixture f;
    // WifiSsid defaults to "" (COMMON_SETTINGS default).
    NetBeginStatus st = f.begin();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NetBeginStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_TRUE(f.state.network.setupApActive);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NetWifiMode::SetupAp), static_cast<int>(f.state.network.wifiMode));
    TEST_ASSERT_EQUAL_STRING("Test-Setup", f.state.network.apSsid);
    TEST_ASSERT_EQUAL_STRING(f.wifi.apIpValue.c_str(), f.state.network.apIp);
    TEST_ASSERT_EQUAL_INT(1, f.wifi.startApCount);
}

static void net_test_save_creds_via_config_joins_and_ap_off() {
    NetFixture f;
    f.begin();
    TEST_ASSERT_TRUE(f.state.network.setupApActive);

    f.setSsid("MyHomeNet", "supersecret");
    uint64_t now = 1000;
    f.runtime.tick(now);

    TEST_ASSERT_EQUAL_INT(1, f.wifi.connectCount);
    TEST_ASSERT_EQUAL_STRING("MyHomeNet", f.wifi.connectSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("supersecret", f.wifi.connectPass.c_str());
    TEST_ASSERT_TRUE(f.state.network.setupApActive);  // AP stays up while joining (D4)

    // Station links: the AP lingers AP_LINGER_MS so the setup page can show the IP, then goes off.
    f.wifi.link = true;
    now += 1000;
    f.runtime.tick(now);
    TEST_ASSERT_TRUE(f.state.network.wifiConnected);
    TEST_ASSERT_EQUAL_INT(0, f.wifi.stopApCount);
    for (uint64_t end = now + WifiSupervisor::AP_LINGER_MS + 1000; now < end;) {
        now += 1000;
        f.runtime.tick(now);
    }
    TEST_ASSERT_EQUAL_INT(1, f.wifi.stopApCount);
    TEST_ASSERT_FALSE(f.state.network.setupApActive);
    TEST_ASSERT_EQUAL_STRING("", f.state.network.apSsid);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NetWifiMode::Station), static_cast<int>(f.state.network.wifiMode));
}

static void net_test_connected_fills_ip_rssi_and_mdns_started_once_per_up() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.begin();

    f.wifi.link = true;
    f.wifi.localIpValue = "192.168.1.55";
    f.wifi.rssiValue = -42;

    uint64_t now = 1000;
    f.runtime.tick(now);
    TEST_ASSERT_TRUE(f.state.network.wifiConnected);
    TEST_ASSERT_EQUAL_STRING("192.168.1.55", f.state.network.ip);
    TEST_ASSERT_EQUAL_INT(-42, f.state.network.wifiRssi);
    TEST_ASSERT_EQUAL_INT(1, f.wifi.mdnsCount);

    now += 1000;
    f.runtime.tick(now);
    now += 1000;
    f.runtime.tick(now);
    TEST_ASSERT_EQUAL_INT(1, f.wifi.mdnsCount);  // only once, on the up edge
}

static void net_test_mqtt_disabled_with_empty_host() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.begin();
    f.wifi.link = true;

    uint64_t now = 1000;
    f.runtime.tick(now);
    TEST_ASSERT_FALSE(f.state.network.mqttEnabled);
    TEST_ASSERT_EQUAL_INT(0, f.mqtt.connectCount);
}

// ---- MQTT session lifecycle -----------------------------------------------

static void net_test_mqtt_session_publishes_discovery_then_availability_then_states_then_subscribe() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.setMqttHost("broker.local");
    f.begin();
    f.wifi.link = true;

    uint64_t now = 0;
    netAdvanceToMqttConnected(f, now);
    TEST_ASSERT_TRUE(f.mqtt.connected());
    TEST_ASSERT_EQUAL_STRING("boiler-room/status", f.mqtt.lastWillTopic.c_str());

    // Pump until the subscription is made; at that point exactly discovery +
    // availability + all states must have been published, in that order.
    int guard = 0;
    while (f.mqtt.subscriptions.empty() && guard < 2000) {
        f.runtime.fastTick(now);
        now += 100;
        ++guard;
    }
    size_t total = f.runtime.registry().count();
    TEST_ASSERT_TRUE(total > 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(2 * total + 1), static_cast<int>(f.mqtt.publishes.size()));
    for (size_t i = 0; i < total; ++i) {
        TEST_ASSERT_TRUE(f.mqtt.publishes[i].topic.find("/config") != std::string::npos);
    }
    TEST_ASSERT_EQUAL_STRING("boiler-room/status", f.mqtt.publishes[total].topic.c_str());
    TEST_ASSERT_EQUAL_STRING("online", f.mqtt.publishes[total].payload.c_str());
    for (size_t i = total + 1; i < 2 * total + 1; ++i) {
        TEST_ASSERT_TRUE(f.mqtt.publishes[i].topic.find("/state") != std::string::npos);
    }
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.mqtt.subscriptions.size()));
    TEST_ASSERT_EQUAL_STRING("boiler-room/+/set", f.mqtt.subscriptions[0].c_str());
    TEST_ASSERT_TRUE(f.state.network.mqttConnected);
}

static void net_test_wifi_drop_clears_mqtt_connected_and_gate() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.setMqttHost("broker.local");
    f.begin();
    f.wifi.link = true;

    uint64_t now = 0;
    netAdvanceToMqttConnected(f, now);
    TEST_ASSERT_TRUE(f.state.network.mqttConnected);
    TEST_ASSERT_TRUE(haGatedFlag(true, f.state.network));

    f.wifi.link = false;
    f.runtime.tick(now);
    TEST_ASSERT_FALSE(f.state.network.wifiConnected);
    TEST_ASSERT_FALSE(f.state.network.mqttConnected);  // same tick, per D12
    TEST_ASSERT_FALSE(haGatedFlag(true, f.state.network));
    TEST_ASSERT_TRUE(f.mqtt.disconnectCount >= 1);  // transport force-closed on Wi-Fi down (D6)
}

static void net_test_inbound_command_applied_via_queue_and_echoed() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.setMqttHost("broker.local");
    f.begin();
    f.wifi.link = true;

    uint64_t now = 0;
    netAdvanceToMqttConnected(f, now);
    netPumpUntilLive(f, now, 2000);
    size_t before = f.mqtt.publishes.size();

    int idx = netFindKey(f.runtime.registry(), "ha_switch_bool");
    TEST_ASSERT_TRUE(idx >= 0);

    InMemoryCommandQueue queue;
    InboundResult r = handleMqttCommand(
        f.runtime.registry(), "boiler-room", "boiler-room/ha_switch_bool/set", "ON", 2, queue, f.signals);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InboundResult::Posted), static_cast<int>(r));

    // Apply the queued command the way CoreRuntime would (D13), over the same
    // config/state the ConnectivityRuntime reads for publishing.
    Command cmd{};
    TEST_ASSERT_TRUE(queue.tryReceive(cmd));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::SetNumber), static_cast<int>(cmd.type));
    f.config.setNumber(cmd.settingIndex, cmd.number, cmd.origin, now);

    f.runtime.tick(now);
    netPumpUntilLive(f, now, 2000);

    TEST_ASSERT_TRUE(f.mqtt.publishes.size() > before);
    bool found = false;
    for (size_t i = before; i < f.mqtt.publishes.size(); ++i) {
        if (f.mqtt.publishes[i].topic == "boiler-room/ha_switch_bool/state") {
            TEST_ASSERT_EQUAL_STRING("ON", f.mqtt.publishes[i].payload.c_str());
            found = true;
        }
    }
    TEST_ASSERT_TRUE(found);
}

static void net_test_dropped_command_reported() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.begin();
    f.wifi.link = true;

    f.signals.droppedCommands.store(3);
    uint64_t now = 1000;
    f.runtime.tick(now);

    TEST_ASSERT_EQUAL_UINT32(3, f.state.network.mqttDroppedCommands);
    TEST_ASSERT_EQUAL_INT(1,
        static_cast<int>(f.events.countOf(
            EventType::DiagnosticWarning, EVENT_SOURCE_DIAG_BASE + DIAG_CODE_MQTT_CMD_DROPPED)));

    // No further log on a tick where the count did not increase.
    f.runtime.tick(now + 1000);
    TEST_ASSERT_EQUAL_INT(1,
        static_cast<int>(f.events.countOf(
            EventType::DiagnosticWarning, EVENT_SOURCE_DIAG_BASE + DIAG_CODE_MQTT_CMD_DROPPED)));
}

static void net_test_event_hook_publishes_event() {
    MemoryKvStore logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());

    MemoryKvStore cfgStore;
    MemoryKvStore otaStore;
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(NET_TEST_SCHEMA, 0)));
    FakeWifiPort wifi;
    FakeMqttTransport mqtt;
    FakeOtaPlatform ota;
    NetSignals signals;
    CommonState state{};
    ConnectivityRuntime runtime(state, config, log, wifi, mqtt, ota, otaStore, signals);
    log.setPublishHook(&ConnectivityRuntime::eventHook, &runtime);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
        static_cast<int>(config.setText(
            commonIndex(CommonSetting::WifiSsid), "MyHomeNet", EventReason::Web, 0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
        static_cast<int>(config.setText(commonIndex(CommonSetting::MqttHost), "broker.local", EventReason::Web, 0)));
    NetBeginStatus st = runtime.begin(NET_TEST_ID, NET_TEST_HW, nullptr, 0, "1.0.0", "", "cid", 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NetBeginStatus::Ok), static_cast<int>(st));

    wifi.link = true;
    uint64_t now = 0;
    int guard = 0;
    while (!mqtt.connected() && guard < 200) {
        runtime.tick(now);
        if (mqtt.configured && !mqtt.connectedFlag && mqtt.connectCount > 0) {
            mqtt.connectedFlag = true;
        }
        runtime.fastTick(now);
        now += 100;
        ++guard;
    }
    TEST_ASSERT_TRUE(mqtt.connected());
    runtime.tick(now);  // session observes the connected transport -> sessionStarted
    TEST_ASSERT_TRUE(state.network.mqttConnected);
    for (int i = 0; i < 2000; ++i) {
        runtime.fastTick(now);
        now += 100;
    }
    size_t before = mqtt.publishes.size();

    log.logEvent(toU16(EventType::WifiConnected), EVENT_SOURCE_WIFI, 5, -50, EventReason::Logic);
    for (int i = 0; i < 20; ++i) {
        runtime.fastTick(now);
        now += 100;
    }

    bool found = false;
    for (size_t i = before; i < mqtt.publishes.size(); ++i) {
        if (mqtt.publishes[i].topic == "boiler-room/event") {
            found = true;
        }
    }
    TEST_ASSERT_TRUE(found);
}

// ---- OTA -------------------------------------------------------------------

static void net_test_ota_web_start_inhibit_then_failure_restores() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.begin();

    uint64_t now = 0;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)OtaSource::None, (uint8_t)f.runtime.activeOtaSource());
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    f.runtime.fastTick(now);
    TEST_ASSERT_TRUE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)OtaSource::Web, (uint8_t)f.runtime.activeOtaSource());

    now += 1000;
    f.signals.otaEnded.store(2);  // failure
    f.runtime.fastTick(now);
    TEST_ASSERT_FALSE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)OtaSource::None, (uint8_t)f.runtime.activeOtaSource());
    TEST_ASSERT_TRUE(f.events.countOf(EventType::OtaUpdate, EVENT_SOURCE_OTA) >= 2);
}

static void net_test_ota_success_saves_marker_and_requests_reboot() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.ota.running = "app0";
    f.ota.booting = "app1";  // the successful update selected the other partition
    f.begin();

    uint64_t now = 0;
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Espota));
    f.runtime.fastTick(now);
    now += 1000;
    f.signals.otaEnded.store(1);  // success
    f.runtime.fastTick(now);

    TEST_ASSERT_TRUE(f.state.system.rebootRequested);
    TEST_ASSERT_TRUE(f.state.system.rebootAtMs == now + OTA_REBOOT_DELAY_MS);

    char marker[OTA_LABEL_MAX + 1] = {};
    OtaMarkerStore store(f.otaStore);
    TEST_ASSERT_TRUE(store.load(marker, sizeof(marker)));
    TEST_ASSERT_EQUAL_STRING("app1", marker);
}

static void net_test_ota_fs_success_no_marker() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.ota.running = "app0";
    f.ota.booting = "app0";  // web/FS-only OTA: boot partition unchanged
    f.begin();

    uint64_t now = 0;
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    f.runtime.fastTick(now);
    now += 1000;
    f.signals.otaEnded.store(1);
    f.runtime.fastTick(now);

    TEST_ASSERT_TRUE(f.state.system.rebootRequested);
    char marker[OTA_LABEL_MAX + 1] = {};
    OtaMarkerStore store(f.otaStore);
    TEST_ASSERT_FALSE(store.load(marker, sizeof(marker)));
}

// OtaUpdate events carrying `value` (OTA_EVENT_*).
size_t netCountOta(const RecordingEventSink& ev, float value) {
    size_t n = 0;
    for (size_t i = 0; i < ev.count(); ++i) {
        const RecordingEventSink::Record& r = ev.at(i);
        if (r.type == toU16(EventType::OtaUpdate) && r.source == EVENT_SOURCE_OTA && r.value == value) {
            ++n;
        }
    }
    return n;
}

// final-check S3: ElegantOTA calls onStart, then Update.begin(); when begin
// fails (e.g. "already running") it sends no onEnd. The session must fail
// right after the grace period, not hold the relays OFF for the 60 s stall.
static void net_test_ota_web_begin_failed_fails_fast_and_aborts() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.begin();
    f.ota.updateRunningFlag = false;  // Update.begin() refused

    uint64_t now = 1000;
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    f.runtime.fastTick(now);  // the drain that sees the start never checks (begin may still be running)
    TEST_ASSERT_TRUE(f.runtime.outputsInhibited());
    f.runtime.fastTick(now + OTA_WEB_BEGIN_CHECK_MS - 1);
    TEST_ASSERT_TRUE(f.runtime.outputsInhibited());

    f.runtime.fastTick(now + OTA_WEB_BEGIN_CHECK_MS);
    TEST_ASSERT_FALSE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)OtaSource::None, (uint8_t)f.runtime.activeOtaSource());
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(netCountOta(f.events, OTA_EVENT_FAILED)));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(netCountOta(f.events, OTA_EVENT_SUCCEEDED)));
    TEST_ASSERT_EQUAL_INT(1, f.ota.abortUpdateCount);
    TEST_ASSERT_FALSE(f.state.system.rebootRequested);

    // Checked once only: later ticks neither re-fail nor re-abort.
    f.runtime.fastTick(now + 10000);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(netCountOta(f.events, OTA_EVENT_FAILED)));
    TEST_ASSERT_EQUAL_INT(1, f.ota.abortUpdateCount);
}

// A healthy /ota/start (Update running) is left alone by the begin check; and
// once progress was seen, Update no longer running (e.g. Update.end() already
// ran, onEnd still in flight) must never be mistaken for a failed begin.
static void net_test_ota_web_begin_check_ignores_running_or_progressed_session() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.begin();

    uint64_t now = 0;
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    f.runtime.fastTick(now);
    for (int i = 1; i <= 20; ++i) {
        f.runtime.fastTick(now + static_cast<uint64_t>(i) * 100);
    }
    TEST_ASSERT_TRUE(f.runtime.outputsInhibited());  // running Update: still in the session
    TEST_ASSERT_EQUAL_INT(0, f.ota.abortUpdateCount);

    // Second session: progress arrives, then Update closes before the check.
    NetFixture g;
    g.setSsid("MyHomeNet", "pw");
    g.begin();
    g.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    g.runtime.fastTick(0);
    g.signals.otaProgress.fetch_add(1);
    g.runtime.fastTick(100);
    g.ota.updateRunningFlag = false;
    for (int i = 2; i <= 20; ++i) {
        g.runtime.fastTick(static_cast<uint64_t>(i) * 100);
    }
    TEST_ASSERT_TRUE(g.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(netCountOta(g.events, OTA_EVENT_FAILED)));
    g.signals.otaEnded.store(1);
    g.runtime.fastTick(2100);
    TEST_ASSERT_TRUE(g.state.system.rebootRequested);
    TEST_ASSERT_EQUAL_INT(0, g.ota.abortUpdateCount);
}

// final-check S3: an abandoned web upload stalls out after 60 s; the stale
// Update session must be aborted so the next OTA can begin without a reboot.
static void net_test_ota_web_stall_aborts_update() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.begin();

    uint64_t now = 0;
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    f.runtime.fastTick(now);
    f.signals.otaProgress.fetch_add(1);
    now = 1000;
    f.runtime.fastTick(now);  // last activity

    f.runtime.fastTick(now + OtaGuard::STALL_TIMEOUT_MS - 1);
    TEST_ASSERT_TRUE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_INT(0, f.ota.abortUpdateCount);  // never aborted under a live session

    f.runtime.fastTick(now + OtaGuard::STALL_TIMEOUT_MS);
    TEST_ASSERT_FALSE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_INT(1, f.ota.abortUpdateCount);
    TEST_ASSERT_FALSE(f.ota.updateRunningFlag);

    // A new web OTA now begins normally (Update.begin() succeeds again).
    f.ota.updateRunningFlag = true;
    uint64_t t2 = now + OtaGuard::STALL_TIMEOUT_MS + 1000;
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    f.runtime.fastTick(t2);
    f.runtime.fastTick(t2 + OTA_WEB_BEGIN_CHECK_MS);
    TEST_ASSERT_TRUE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)OtaSource::Web, (uint8_t)f.runtime.activeOtaSource());
}

// final-check S3: a web failure (onEnd(false)) aborts Update; an espota
// failure does not touch it (ArduinoOTA owns its own session).
static void net_test_ota_web_failure_aborts_update_espota_does_not() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.begin();

    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    f.runtime.fastTick(0);
    f.signals.otaEnded.store(2);
    f.runtime.fastTick(100);
    TEST_ASSERT_FALSE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_INT(1, f.ota.abortUpdateCount);

    f.ota.updateRunningFlag = true;
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Espota));
    f.signals.otaEnded.store(2);
    f.runtime.fastTick(1000);
    TEST_ASSERT_FALSE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_INT(1, f.ota.abortUpdateCount);
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(netCountOta(f.events, OTA_EVENT_FAILED)));
}

// final-check S4: ElegantOTA onEnd(true) after a session that wrote nothing
// (the WebServerHost callbacks: onStart resets otaWebBytes, onEnd posts
// webOtaEndCode(ok, otaWebBytes)) is a failure: no OtaUpdate(1), no reboot.
static void net_test_ota_web_end_ok_with_zero_bytes_is_failure_no_reboot() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.ota.running = "app0";
    f.ota.booting = "app0";
    f.begin();

    f.signals.otaWebBytes.store(12345);  // left over from an earlier session
    // onStart
    f.signals.otaWebBytes.store(0);
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    f.runtime.fastTick(0);
    // empty upload body: no onProgress, then onEnd(true)
    f.signals.otaEnded.store(webOtaEndCode(true, f.signals.otaWebBytes.load()));
    f.runtime.fastTick(100);

    TEST_ASSERT_FALSE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(netCountOta(f.events, OTA_EVENT_SUCCEEDED)));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(netCountOta(f.events, OTA_EVENT_FAILED)));
    TEST_ASSERT_FALSE(f.state.system.rebootRequested);
    TEST_ASSERT_EQUAL_INT(1, f.ota.abortUpdateCount);  // the still-open session is closed too
    char marker[OTA_LABEL_MAX + 1] = {};
    OtaMarkerStore store(f.otaStore);
    TEST_ASSERT_FALSE(store.load(marker, sizeof(marker)));

    // With bytes written the same onEnd(true) is a success.
    f.ota.updateRunningFlag = true;
    f.signals.otaWebBytes.store(0);
    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    f.runtime.fastTick(1000);
    f.signals.otaWebBytes.store(4096);
    f.signals.otaProgress.fetch_add(1);
    f.signals.otaEnded.store(webOtaEndCode(true, f.signals.otaWebBytes.load()));
    f.runtime.fastTick(1100);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(netCountOta(f.events, OTA_EVENT_SUCCEEDED)));
    TEST_ASSERT_TRUE(f.state.system.rebootRequested);
}

static void net_test_boot_rollback_detected_logged_marker_cleared() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.ota.state = OtaImageState::Valid;
    f.ota.running = "app0";
    {
        OtaMarkerStore store(f.otaStore);
        TEST_ASSERT_TRUE(store.save("app1"));  // marker points at the OTHER partition -> rollback happened
    }

    NetBeginStatus st = f.begin();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(NetBeginStatus::Ok), static_cast<int>(st));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(OtaBootOutcome::RolledBack), static_cast<int>(f.state.ota.bootOutcome));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::OtaRollback, EVENT_SOURCE_OTA)));

    char marker[OTA_LABEL_MAX + 1] = {};
    OtaMarkerStore store(f.otaStore);
    TEST_ASSERT_FALSE(store.load(marker, sizeof(marker)));
}

static void net_test_trial_confirm_marks_valid() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.ota.state = OtaImageState::PendingVerify;
    f.ota.running = "app1";
    {
        OtaMarkerStore store(f.otaStore);
        TEST_ASSERT_TRUE(store.save("app1"));  // running == marker && pending -> Trial
    }

    f.begin(0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaBootOutcome::Trial), static_cast<int>(f.state.ota.bootOutcome));
    TEST_ASSERT_TRUE(f.state.ota.pendingVerify);

    uint64_t now = 0;
    for (uint64_t t = 1000; t <= 60000; t += 1000) {
        f.state.oneWire.readCycleCount += 1;
        now = t;
        f.runtime.tick(now);
    }

    TEST_ASSERT_EQUAL_INT(1, f.ota.markRunningValidCount);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::OtaUpdate, EVENT_SOURCE_OTA)));
    TEST_ASSERT_EQUAL_FLOAT(OTA_EVENT_CONFIRMED, f.events.at(f.events.count() - 1).value);

    char marker[OTA_LABEL_MAX + 1] = {};
    OtaMarkerStore store(f.otaStore);
    TEST_ASSERT_FALSE(store.load(marker, sizeof(marker)));
}

static void net_test_health_timeout_rollback_fails_closed() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.ota.state = OtaImageState::PendingVerify;
    f.ota.running = "app1";
    {
        OtaMarkerStore store(f.otaStore);
        TEST_ASSERT_TRUE(store.save("app1"));
    }
    f.begin(0);
    TEST_ASSERT_TRUE(f.state.ota.pendingVerify);

    uint64_t now = 0;
    // Sensor read-cycle count never advances -> the health monitor can never confirm.
    for (uint64_t t = 1000; t < OtaHealthMonitor::DEADLINE_MS; t += 1000) {
        now = t;
        f.runtime.tick(now);
    }
    TEST_ASSERT_FALSE(f.runtime.outputsInhibited());

    now = OtaHealthMonitor::DEADLINE_MS;
    f.runtime.tick(now);
    TEST_ASSERT_TRUE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_INT(0, f.ota.rollbackAndRebootCount);

    size_t diagBefore =
        f.events.countOf(EventType::DiagnosticWarning, EVENT_SOURCE_DIAG_BASE + DIAG_CODE_OTA_HEALTH_TIMEOUT);

    // The fake's rollbackAndReboot()/restart() return (a failed rollback on
    // hardware): fall back to restart(), stay inhibited (fail closed).
    now += 1000;
    f.runtime.tick(now);
    TEST_ASSERT_EQUAL_INT(1, f.ota.rollbackAndRebootCount);
    TEST_ASSERT_EQUAL_INT(1, f.ota.restartCount);
    TEST_ASSERT_TRUE(f.runtime.outputsInhibited());
    TEST_ASSERT_EQUAL_UINT32(diagBefore + 1,
        f.events.countOf(EventType::DiagnosticWarning, EVENT_SOURCE_DIAG_BASE + DIAG_CODE_OTA_HEALTH_TIMEOUT));

    // Fast ticks never retry (no spin): at most one attempt per slow tick.
    for (int i = 0; i < 10; ++i) {
        f.runtime.fastTick(now + 100 * (i + 1));
    }
    TEST_ASSERT_EQUAL_INT(1, f.ota.rollbackAndRebootCount);
    TEST_ASSERT_EQUAL_INT(1, f.ota.restartCount);

    // Each further slow tick retries exactly once; still inhibited; warning logged only once.
    for (int i = 0; i < 3; ++i) {
        now += 1000;
        f.runtime.tick(now);
        TEST_ASSERT_EQUAL_INT(2 + i, f.ota.rollbackAndRebootCount);
        TEST_ASSERT_EQUAL_INT(2 + i, f.ota.restartCount);
        TEST_ASSERT_TRUE(f.runtime.outputsInhibited());
    }
    TEST_ASSERT_EQUAL_UINT32(diagBefore + 1,
        f.events.countOf(EventType::DiagnosticWarning, EVENT_SOURCE_DIAG_BASE + DIAG_CODE_OTA_HEALTH_TIMEOUT));
}

static void net_test_no_publish_during_ota() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.setMqttHost("broker.local");
    f.begin();
    f.wifi.link = true;

    uint64_t now = 0;
    netAdvanceToMqttConnected(f, now);
    netPumpUntilLive(f, now, 2000);
    size_t before = f.mqtt.publishes.size();

    int idx = netFindKey(f.runtime.registry(), "ha_switch_bool");
    TEST_ASSERT_TRUE(idx >= 0);
    f.signals.republish.set(static_cast<size_t>(idx));

    f.signals.otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    f.runtime.fastTick(now);
    now += 100;
    f.runtime.tick(now);  // marks the entity dirty, but pump() must not run while OTA is active

    for (int i = 0; i < 20; ++i) {
        f.runtime.fastTick(now);
        now += 100;
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(before), static_cast<int>(f.mqtt.publishes.size()));
}

// ---- Admin creds / endpoint change -----------------------------------------

static void net_test_admin_creds_change_flag() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.begin();

    uint64_t now = 1000;
    NetTickEvents ev = f.runtime.tick(now);
    TEST_ASSERT_FALSE(ev.adminCredsChanged);

    f.config.setText(commonIndex(CommonSetting::WebPass), "newpass1", EventReason::Web, now);
    now += 1000;
    ev = f.runtime.tick(now);
    TEST_ASSERT_TRUE(ev.adminCredsChanged);

    now += 1000;
    ev = f.runtime.tick(now);
    TEST_ASSERT_FALSE(ev.adminCredsChanged);
}

static void net_test_endpoint_change_publishes_offline_before_disconnect() {
    NetFixture f;
    f.setSsid("MyHomeNet", "pw");
    f.setMqttHost("broker.local");
    f.begin();
    f.wifi.link = true;

    uint64_t now = 0;
    netAdvanceToMqttConnected(f, now);
    netPumpUntilLive(f, now, 2000);
    TEST_ASSERT_TRUE(f.mqtt.connected());

    const size_t disconnectsBefore = f.mqtt.publishCountAtDisconnect.size();
    f.setMqttHost("other-broker.local");
    now += 1000;
    f.runtime.tick(now);

    TEST_ASSERT_TRUE(f.mqtt.publishCountAtDisconnect.size() > disconnectsBefore);
    const size_t publishedBeforeDisconnect = f.mqtt.publishCountAtDisconnect[disconnectsBefore];
    TEST_ASSERT_TRUE(publishedBeforeDisconnect >= 1);
    const FakeMqttTransport::Publish& last = f.mqtt.publishes[publishedBeforeDisconnect - 1];
    TEST_ASSERT_EQUAL_STRING("boiler-room/status", last.topic.c_str());
    TEST_ASSERT_EQUAL_STRING("offline", last.payload.c_str());
    TEST_ASSERT_TRUE(last.retain);
    TEST_ASSERT_FALSE(f.mqtt.lastDisconnectForce);  // graceful close on an endpoint change
}

// Runs every test in this suite. Call from runNetSuite().
inline void runConnectivitySuite() {
    RUN_TEST(net_test_invalid_identity_inert);
    RUN_TEST(net_test_no_ssid_ap_state_for_oled);
    RUN_TEST(net_test_save_creds_via_config_joins_and_ap_off);
    RUN_TEST(net_test_connected_fills_ip_rssi_and_mdns_started_once_per_up);
    RUN_TEST(net_test_mqtt_disabled_with_empty_host);
    RUN_TEST(net_test_mqtt_session_publishes_discovery_then_availability_then_states_then_subscribe);
    RUN_TEST(net_test_wifi_drop_clears_mqtt_connected_and_gate);
    RUN_TEST(net_test_inbound_command_applied_via_queue_and_echoed);
    RUN_TEST(net_test_dropped_command_reported);
    RUN_TEST(net_test_event_hook_publishes_event);
    RUN_TEST(net_test_ota_web_start_inhibit_then_failure_restores);
    RUN_TEST(net_test_ota_success_saves_marker_and_requests_reboot);
    RUN_TEST(net_test_ota_fs_success_no_marker);
    RUN_TEST(net_test_ota_web_begin_failed_fails_fast_and_aborts);
    RUN_TEST(net_test_ota_web_begin_check_ignores_running_or_progressed_session);
    RUN_TEST(net_test_ota_web_stall_aborts_update);
    RUN_TEST(net_test_ota_web_failure_aborts_update_espota_does_not);
    RUN_TEST(net_test_ota_web_end_ok_with_zero_bytes_is_failure_no_reboot);
    RUN_TEST(net_test_boot_rollback_detected_logged_marker_cleared);
    RUN_TEST(net_test_trial_confirm_marks_valid);
    RUN_TEST(net_test_health_timeout_rollback_fails_closed);
    RUN_TEST(net_test_no_publish_during_ota);
    RUN_TEST(net_test_admin_creds_change_flag);
    RUN_TEST(net_test_endpoint_change_publishes_offline_before_disconnect);
}
