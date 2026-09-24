#pragma once
#include <unity.h>
#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/NetEngine/src/NetJson.h"
#include "../lib/NetEngine/src/OtaBoot.h"
#include "../lib/NetEngine/src/OtaGuard.h"
#include "../lib/NetEngine/src/OtaPlatform.h"
#include "../lib/NetEngine/src/WifiScanList.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/RecordingEventSink.h"

// Native-safe Unity tests for stage 04 phase 6: the OTA session guard
// (OtaGuard, D14), boot classification + marker persistence + the 60 s/300 s
// health confirmation (OtaBoot.h, D15), and the admin JSON snapshot builders
// (NetJson.h, D18/D19). Header-only, run via NetSuite.h. Test names are
// prefixed ota_ (D24).

// ---- classifyOtaBoot: D15's 5-row table ------------------------------------

static void ota_test_classify_table() {
    // running == marker && pending -> Trial
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaBootOutcome::Trial),
        static_cast<int>(classifyOtaBoot(OtaBootInfo{OtaImageState::PendingVerify, "app1", true, "app1"})));
    // running == marker && !pending -> UpdatedNoRollback
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaBootOutcome::UpdatedNoRollback),
        static_cast<int>(classifyOtaBoot(OtaBootInfo{OtaImageState::Valid, "app1", true, "app1"})));
    // marker present && running != marker -> RolledBack
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaBootOutcome::RolledBack),
        static_cast<int>(classifyOtaBoot(OtaBootInfo{OtaImageState::Valid, "app0", true, "app1"})));
    // no marker && pending -> Trial
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaBootOutcome::Trial),
        static_cast<int>(classifyOtaBoot(OtaBootInfo{OtaImageState::PendingVerify, "app0", false, nullptr})));
    // otherwise -> Normal
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaBootOutcome::Normal),
        static_cast<int>(classifyOtaBoot(OtaBootInfo{OtaImageState::Valid, "app0", false, nullptr})));
}

// ---- OtaMarkerStore ---------------------------------------------------------

static void ota_test_marker_roundtrip_and_clear() {
    MemoryKvStore store;
    OtaMarkerStore marker(store);

    char buf[OTA_LABEL_MAX + 1] = {};
    TEST_ASSERT_FALSE(marker.load(buf, sizeof(buf)));  // absent before the first save

    TEST_ASSERT_TRUE(marker.save("app1"));
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_TRUE(marker.load(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("app1", buf);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(store.commitCount));

    TEST_ASSERT_TRUE(marker.clear());
    TEST_ASSERT_FALSE(marker.load(buf, sizeof(buf)));
    // A NotFound remove is not an error (D15's own "clear the marker" is
    // idempotent -- UpdatedNoRollback/RolledBack both call it unconditionally).
    TEST_ASSERT_TRUE(marker.clear());
}

// ---- OtaGuard ---------------------------------------------------------------

static void ota_test_guard_start_inhibits_and_logs() {
    OtaGuard guard;
    RecordingEventSink events;

    OtaGuardResult r = guard.update(OtaSignalSnapshot{OtaSource::Web, false, 0}, 1000, events);

    TEST_ASSERT_FALSE(r.succeeded);
    TEST_ASSERT_FALSE(r.failed);
    TEST_ASSERT_TRUE(guard.active());
    TEST_ASSERT_TRUE(guard.inhibit());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaSource::Web), static_cast<int>(guard.source()));

    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(events.countOf(EventType::OtaUpdate, EVENT_SOURCE_OTA)));
    const RecordingEventSink::Record& rec = events.at(0);
    TEST_ASSERT_EQUAL_FLOAT(OTA_EVENT_STARTED, rec.value);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(static_cast<uint8_t>(OtaSource::Web)), rec.aux);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventReason::Web), static_cast<int>(rec.reason));
}

static void ota_test_guard_start_and_end_same_drain() {
    // espota's blocking handle() can set both onStart and onEnd before the
    // loop task next drains NetSignals -- one update() call must see both.
    OtaGuard guard;
    RecordingEventSink events;

    OtaGuardResult r = guard.update(OtaSignalSnapshot{OtaSource::Espota, false, 1}, 5000, events);

    TEST_ASSERT_TRUE(r.succeeded);
    TEST_ASSERT_FALSE(r.failed);
    TEST_ASSERT_TRUE(guard.active());  // success keeps inhibit until reboot
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(events.countOf(EventType::OtaUpdate, EVENT_SOURCE_OTA)));
    TEST_ASSERT_EQUAL_FLOAT(OTA_EVENT_STARTED, events.at(0).value);
    TEST_ASSERT_EQUAL_FLOAT(OTA_EVENT_SUCCEEDED, events.at(1).value);
}

static void ota_test_guard_failure_releases() {
    OtaGuard guard;
    RecordingEventSink events;
    guard.update(OtaSignalSnapshot{OtaSource::Web, false, 0}, 0, events);

    OtaGuardResult r = guard.update(OtaSignalSnapshot{OtaSource::None, false, 2}, 1000, events);

    TEST_ASSERT_FALSE(r.succeeded);
    TEST_ASSERT_TRUE(r.failed);
    TEST_ASSERT_FALSE(guard.active());
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(events.countOf(EventType::OtaUpdate, EVENT_SOURCE_OTA)));
    TEST_ASSERT_EQUAL_FLOAT(OTA_EVENT_FAILED, events.at(1).value);
}

static void ota_test_guard_stall_timeout_releases() {
    OtaGuard guard;
    RecordingEventSink events;
    guard.update(OtaSignalSnapshot{OtaSource::Web, false, 0}, 0, events);

    OtaGuardResult beforeStall =
        guard.update(OtaSignalSnapshot{OtaSource::None, false, 0}, OtaGuard::STALL_TIMEOUT_MS - 1, events);
    TEST_ASSERT_FALSE(beforeStall.failed);
    TEST_ASSERT_TRUE(guard.active());

    OtaGuardResult atStall =
        guard.update(OtaSignalSnapshot{OtaSource::None, false, 0}, OtaGuard::STALL_TIMEOUT_MS, events);
    TEST_ASSERT_TRUE(atStall.failed);
    TEST_ASSERT_FALSE(guard.active());
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(events.countOf(EventType::OtaUpdate, EVENT_SOURCE_OTA)));
    TEST_ASSERT_EQUAL_FLOAT(OTA_EVENT_FAILED, events.at(1).value);
}

static void ota_test_guard_progress_extends_stall() {
    OtaGuard guard;
    RecordingEventSink events;
    guard.update(OtaSignalSnapshot{OtaSource::Web, false, 0}, 0, events);

    // Progress at 50s pushes the stall deadline out to 50s + 60s = 110s.
    guard.update(OtaSignalSnapshot{OtaSource::None, true, 0}, 50000, events);

    OtaGuardResult stillActive =
        guard.update(OtaSignalSnapshot{OtaSource::None, false, 0}, OtaGuard::STALL_TIMEOUT_MS, events);
    TEST_ASSERT_FALSE(stillActive.failed);
    TEST_ASSERT_TRUE(guard.active());

    OtaGuardResult nowStalled = guard.update(OtaSignalSnapshot{OtaSource::None, false, 0}, 110000, events);
    TEST_ASSERT_TRUE(nowStalled.failed);
    TEST_ASSERT_FALSE(guard.active());
}

static void ota_test_guard_success_keeps_inhibit() {
    OtaGuard guard;
    RecordingEventSink events;
    guard.update(OtaSignalSnapshot{OtaSource::Web, false, 0}, 0, events);
    guard.update(OtaSignalSnapshot{OtaSource::None, false, 1}, 1000, events);
    TEST_ASSERT_TRUE(guard.active());

    // A success never lapses via the stall timeout, however long the caller
    // waits before rebooting.
    OtaGuardResult r = guard.update(
        OtaSignalSnapshot{OtaSource::None, false, 0}, 1000 + static_cast<uint64_t>(OtaGuard::STALL_TIMEOUT_MS) * 10,
        events);
    TEST_ASSERT_FALSE(r.failed);
    TEST_ASSERT_TRUE(guard.active());
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(events.countOf(EventType::OtaUpdate, EVENT_SOURCE_OTA)));
}

static void ota_test_web_end_code_requires_written_bytes() {
    TEST_ASSERT_EQUAL_UINT8(2, webOtaEndCode(true, 0));  // onEnd(true) but nothing written (S4)
    TEST_ASSERT_EQUAL_UINT8(1, webOtaEndCode(true, 1));
    TEST_ASSERT_EQUAL_UINT8(1, webOtaEndCode(true, 1500000));
    TEST_ASSERT_EQUAL_UINT8(2, webOtaEndCode(false, 0));
    TEST_ASSERT_EQUAL_UINT8(2, webOtaEndCode(false, 4096));
}

static void ota_test_guard_result_reports_session_source() {
    OtaGuard guard;
    RecordingEventSink events;
    guard.update(OtaSignalSnapshot{OtaSource::Web, false, 0}, 0, events);
    OtaGuardResult failed = guard.update(OtaSignalSnapshot{OtaSource::None, false, 2}, 100, events);
    TEST_ASSERT_TRUE(failed.failed);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaSource::Web), static_cast<int>(failed.source));

    guard.update(OtaSignalSnapshot{OtaSource::Web, false, 0}, 1000, events);
    OtaGuardResult stalled = guard.update(
        OtaSignalSnapshot{OtaSource::None, false, 0}, 1000 + OtaGuard::STALL_TIMEOUT_MS, events);
    TEST_ASSERT_TRUE(stalled.failed);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaSource::Web), static_cast<int>(stalled.source));

    OtaGuardResult ok = guard.update(OtaSignalSnapshot{OtaSource::Espota, false, 1}, 200000, events);
    TEST_ASSERT_TRUE(ok.succeeded);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaSource::Espota), static_cast<int>(ok.source));
}

// ---- OtaHealthMonitor --------------------------------------------------------

static void ota_test_health_confirms_after_60s_ticks_and_cycles() {
    OtaHealthMonitor mon;
    mon.begin(true, 0, 0);
    TEST_ASSERT_TRUE(mon.pending());

    OtaHealthMonitor::Verdict v = OtaHealthMonitor::Verdict::None;
    uint32_t cycles = 0;
    for (uint64_t t = 1000; t <= 60000; t += 1000) {
        ++cycles;  // 60 cycles by t=60000: well over MIN_SENSOR_CYCLES (10)
        v = mon.tick(t, cycles);
    }

    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaHealthMonitor::Verdict::Confirm), static_cast<int>(v));
    TEST_ASSERT_FALSE(mon.pending());
    // The verdict is returned exactly once; later ticks are inert.
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(OtaHealthMonitor::Verdict::None), static_cast<int>(mon.tick(61000, cycles + 1)));
}

static void ota_test_health_no_confirm_without_cycles_then_rollback_at_deadline() {
    OtaHealthMonitor mon;
    mon.begin(true, 0, 100);  // baseline 100, never advances -> cycle delta stays 0

    OtaHealthMonitor::Verdict v = OtaHealthMonitor::Verdict::None;
    for (uint64_t t = 1000; t < OtaHealthMonitor::DEADLINE_MS; t += 1000) {
        v = mon.tick(t, 100);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaHealthMonitor::Verdict::None), static_cast<int>(v));
    }
    TEST_ASSERT_TRUE(mon.pending());

    v = mon.tick(OtaHealthMonitor::DEADLINE_MS, 100);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaHealthMonitor::Verdict::Rollback), static_cast<int>(v));
    TEST_ASSERT_FALSE(mon.pending());
}

static void ota_test_health_missing_sensors_still_confirm() {
    // SensorService.busCycle() (Phase 1) advances readCycleCount once per
    // completed bus-read cycle even with zero attached/assigned sensors -- the
    // monitor only ever sees that counter, so a missing or faulty sensor
    // cannot block (or cause) a rollback.
    OtaHealthMonitor mon;
    mon.begin(true, 0, 5);  // e.g. cycles already ran pre-OTA

    OtaHealthMonitor::Verdict v = OtaHealthMonitor::Verdict::None;
    for (uint64_t t = 1000; t <= 60000; t += 1000) {
        uint32_t cyclesSoFar = 5 + static_cast<uint32_t>(t / 1000);  // +1/tick, no sensors involved
        v = mon.tick(t, cyclesSoFar);
    }

    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaHealthMonitor::Verdict::Confirm), static_cast<int>(v));
}

static void ota_test_health_not_trial_never_verdict() {
    OtaHealthMonitor mon;
    mon.begin(false, 0, 0);
    TEST_ASSERT_FALSE(mon.pending());

    for (uint64_t t = 1000; t <= OtaHealthMonitor::DEADLINE_MS; t += 1000) {
        OtaHealthMonitor::Verdict v = mon.tick(t, static_cast<uint32_t>(t / 1000) + 50);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaHealthMonitor::Verdict::None), static_cast<int>(v));
    }
    TEST_ASSERT_EQUAL_UINT16(0, mon.remainingS(1000));
}

// ---- NetJson ------------------------------------------------------------------

static void ota_test_json_status_has_no_password() {
    CommonState state{};
    state.network.wifiMode = NetWifiMode::Station;
    state.network.wifiConnected = true;
    state.network.wifiRssi = -55;
    strncpy(state.network.ssid, "MyHomeNet", sizeof(state.network.ssid) - 1);
    strncpy(state.network.ip, "192.168.1.42", sizeof(state.network.ip) - 1);
    strncpy(state.network.hostname, "boiler-room", sizeof(state.network.hostname) - 1);
    state.network.setupApActive = false;
    state.network.mqttEnabled = true;
    state.network.mqttConnected = true;
    state.network.scanRunning = false;

    char buf[256];
    size_t len = buildWifiStatusJson(state, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));
    TEST_ASSERT_EQUAL_STRING("station", doc["mode"].as<const char*>());
    TEST_ASSERT_TRUE(doc["connected"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("MyHomeNet", doc["ssid"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("192.168.1.42", doc["ip"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(-55, doc["rssi"].as<int>());
    TEST_ASSERT_EQUAL_STRING("boiler-room", doc["hostname"].as<const char*>());
    TEST_ASSERT_FALSE(doc["apActive"].as<bool>());
    TEST_ASSERT_TRUE(doc["mqttEnabled"].as<bool>());
    TEST_ASSERT_TRUE(doc["mqttConnected"].as<bool>());
    TEST_ASSERT_FALSE(doc["scanRunning"].as<bool>());

    // NetworkStatus carries no password field at all; guard the payload
    // itself never grows one (the setup page's POST body has the password,
    // this read-only status snapshot must not).
    TEST_ASSERT_NULL(strstr(buf, "pass"));
    TEST_ASSERT_NULL(strstr(buf, "Pass"));
    TEST_ASSERT_NULL(strstr(buf, "secret"));
    TEST_ASSERT_TRUE(doc["pass"].isNull());
    TEST_ASSERT_TRUE(doc["password"].isNull());
}

static void ota_test_json_scan_and_version() {
    WifiScanList list;
    list.add("NetA", -40, true);
    list.add("NetB", -70, false);

    char sbuf[256];
    size_t slen = buildScanJson(list, true, sbuf, sizeof(sbuf));
    TEST_ASSERT_TRUE(slen > 0);
    JsonDocument sdoc;
    TEST_ASSERT_FALSE(deserializeJson(sdoc, sbuf, slen));
    TEST_ASSERT_TRUE(sdoc["running"].as<bool>());
    JsonArray nets = sdoc["networks"].as<JsonArray>();
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(nets.size()));
    TEST_ASSERT_EQUAL_STRING("NetA", nets[0]["ssid"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(-40, nets[0]["rssi"].as<int>());
    TEST_ASSERT_TRUE(nets[0]["secure"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("NetB", nets[1]["ssid"].as<const char*>());
    TEST_ASSERT_FALSE(nets[1]["secure"].as<bool>());
    TEST_ASSERT_NULL(strstr(sbuf, "pass"));

    CommonState state{};
    strncpy(state.versions.fw, "1.2.3", sizeof(state.versions.fw) - 1);
    strncpy(state.versions.web, "1.2.3", sizeof(state.versions.web) - 1);
    state.versions.webMismatch = false;
    state.ota.inProgress = true;
    state.ota.pendingVerify = true;
    state.ota.bootOutcome = OtaBootOutcome::Trial;

    char vbuf[256];
    size_t vlen = buildVersionJson(state, "boiler-room", vbuf, sizeof(vbuf));
    TEST_ASSERT_TRUE(vlen > 0);
    JsonDocument vdoc;
    TEST_ASSERT_FALSE(deserializeJson(vdoc, vbuf, vlen));
    TEST_ASSERT_EQUAL_STRING("boiler-room", vdoc["project"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("1.2.3", vdoc["fw"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("1.2.3", vdoc["web"].as<const char*>());
    TEST_ASSERT_FALSE(vdoc["mismatch"].as<bool>());
    TEST_ASSERT_TRUE(vdoc["ota"]["inProgress"].as<bool>());
    TEST_ASSERT_TRUE(vdoc["ota"]["pendingVerify"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("trial", vdoc["ota"]["bootOutcome"].as<const char*>());
}

static void ota_test_json_scan_escaped_ssids_truncated_to_fit_snapshot() {
    // == JSON_SNAPSHOT_CAP (NetEsp32/JsonSnapshot.h, Arduino-only header).
    constexpr size_t SNAPSHOT_CAP = 1536;

    // 16 distinct 32-char SSIDs, each made of characters JSON must escape:
    // quotes, backslashes, newlines and tabs, 2 JSON bytes each (ArduinoJson
    // passes other control chars through raw, so they would not grow).
    WifiScanList list;
    const char pattern[] = {'"', '\\', '\n', '\t'};
    for (size_t i = 0; i < WIFI_SCAN_MAX; ++i) {
        char ssid[NET_NAME_TEXT_LEN + 1] = {};
        for (size_t k = 0; k < NET_NAME_TEXT_LEN - 1; ++k) {
            ssid[k] = pattern[k % 4];
        }
        ssid[NET_NAME_TEXT_LEN - 1] = static_cast<char>('A' + i);  // distinct
        TEST_ASSERT_TRUE(list.add(ssid, static_cast<int8_t>(-30 - static_cast<int>(i)), (i % 2) == 0));
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WIFI_SCAN_MAX), static_cast<int>(list.count()));

    // Sanity: the full list really does not fit the snapshot.
    static char big[8192];
    size_t fullLen = buildScanJson(list, true, big, sizeof(big));
    TEST_ASSERT_TRUE(fullLen > SNAPSHOT_CAP);

    for (int pass = 0; pass < 2; ++pass) {
        const bool running = pass == 0;
        char buf[SNAPSHOT_CAP];
        memset(buf, 0x7f, sizeof(buf));
        size_t len = buildScanJson(list, running, buf, sizeof(buf));
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_TRUE(len < SNAPSHOT_CAP);  // room for the terminator
        TEST_ASSERT_TRUE(buf[len] == '\0');

        JsonDocument doc;
        TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));  // valid JSON
        TEST_ASSERT_TRUE(doc["running"].as<bool>() == running);  // accurate running flag
        JsonArray nets = doc["networks"].as<JsonArray>();
        TEST_ASSERT_TRUE(nets.size() >= 1);
        TEST_ASSERT_TRUE(nets.size() < WIFI_SCAN_MAX);  // trailing (weakest) entries dropped
        for (size_t i = 0; i < nets.size(); ++i) {       // kept entries are the strongest, in order
            TEST_ASSERT_EQUAL_STRING(list.entry(i).ssid, nets[i]["ssid"].as<const char*>());
            TEST_ASSERT_EQUAL_INT(list.entry(i).rssi, nets[i]["rssi"].as<int>());
        }
    }
}

// Runs every test in this suite. Call from runNetSuite().
inline void runOtaSuite() {
    RUN_TEST(ota_test_classify_table);
    RUN_TEST(ota_test_marker_roundtrip_and_clear);
    RUN_TEST(ota_test_guard_start_inhibits_and_logs);
    RUN_TEST(ota_test_guard_start_and_end_same_drain);
    RUN_TEST(ota_test_guard_failure_releases);
    RUN_TEST(ota_test_guard_stall_timeout_releases);
    RUN_TEST(ota_test_guard_progress_extends_stall);
    RUN_TEST(ota_test_guard_success_keeps_inhibit);
    RUN_TEST(ota_test_web_end_code_requires_written_bytes);
    RUN_TEST(ota_test_guard_result_reports_session_source);
    RUN_TEST(ota_test_health_confirms_after_60s_ticks_and_cycles);
    RUN_TEST(ota_test_health_no_confirm_without_cycles_then_rollback_at_deadline);
    RUN_TEST(ota_test_health_missing_sensors_still_confirm);
    RUN_TEST(ota_test_health_not_trial_never_verdict);
    RUN_TEST(ota_test_json_status_has_no_password);
    RUN_TEST(ota_test_json_scan_and_version);
    RUN_TEST(ota_test_json_scan_escaped_ssids_truncated_to_fit_snapshot);
}
