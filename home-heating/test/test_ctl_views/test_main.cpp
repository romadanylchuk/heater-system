#include <unity.h>
#include <ArduinoJson.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <DisplayFrame.h>
#include <EventLog.h>
#include <HaDiscovery.h>
#include <HaEntityRegistry.h>
#include <HomeHeatingAlarms.h>
#include <HomeHeatingDiagnostics.h>
#include <HomeHeatingHa.h>
#include <HomeHeatingJson.h>
#include <HomeHeatingPages.h>
#include <HomeHeatingStatus.h>
#include <WebJson.h>
#include "../../../common/test/fakes/FakeClock.h"
#include "../../../common/test/fakes/MemoryKvStore.h"
#include "../../src/HomeHeatingHardware.h"
#include "../../src/HomeHeatingNet.h"
#include "../../src/HomeHeatingSchema.h"

// Stage 08 phase 7: the pure views over HomeHeatingStatus -- the "ctl"
// /api/state extension, the two OLED pages and the HA custom entities.

void setUp() {
    bindHomeHeatingHaStatus(nullptr);
}
void tearDown() {
    bindHomeHeatingHaStatus(nullptr);
}

// Mirrors WEB_STATE_CAP in common/lib/WebEsp32/src/WebServices.h (the
// firmware /api/state snapshot buffer); ESP32-only header, so not included.
constexpr size_t STATE_CAP = 3072;

// ---- fixtures ---------------------------------------------------------------

static void setSensor(CommonState& s, uint8_t i, SensorState st, float t) {
    s.sensors.sensor[i].state = st;
    s.sensors.sensor[i].tempC = st == SensorState::Ok ? t : NAN;
    s.sensors.sensor[i].assigned = st != SensorState::Unassigned;
}

static CommonState sampleState() {
    CommonState s{};
    s.sensors.count = HH_SENSOR_COUNT;
    setSensor(s, HH_SENSOR_H1, SensorState::Ok, 30.0f);
    setSensor(s, HH_SENSOR_H2, SensorState::Ok, 41.2f);
    setSensor(s, HH_SENSOR_H3, SensorState::Ok, 70.1f);
    setSensor(s, HH_SENSOR_H4, SensorState::Ok, 50.0f);
    s.antiSeize.count = 3;
    return s;
}

static HomeHeatingStatus sampleStatus() {
    HomeHeatingStatus st{};
    st.ready = true;
    st.heatingEnabled = true;
    st.h2Set = 40.0f;
    st.p4On = true;
    st.p4Reason = P4Reason::Demand;
    st.k2Bypass = false;
    st.k2Reason = K2Reason::Charging;
    st.k1Mode = K1Mode::Normal;
    st.k1Known = true;
    st.k1PosPct = 35.2f;
    st.k1Moving = 1;
    st.k1FfValid = true;
    st.k1FfPct = 41.6f;
    st.fail = FailMode::None;
    st.noNeed = false;
    return st;
}

static size_t buildState(const CommonState& s, void* ctx, char* buf, size_t cap) {
    const WebJsonContext c{"home-heating", &HOME_HEATING_HW, nullptr, nullptr, homeHeatingStateJson, ctx};
    return buildStateJson(s, c, buf, cap);
}

static void expectBoolKey(JsonObjectConst o, const char* k, bool v) {
    TEST_ASSERT_TRUE_MESSAGE(o[k].is<bool>(), k);
    TEST_ASSERT_EQUAL_MESSAGE(v, o[k].as<bool>(), k);
}

static void expectIntKey(JsonObjectConst o, const char* k, int32_t v) {
    TEST_ASSERT_TRUE_MESSAGE(o[k].is<int32_t>(), k);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(v, o[k].as<int32_t>(), k);
}

static void expectStrKey(JsonObjectConst o, const char* k, const char* v) {
    TEST_ASSERT_TRUE_MESSAGE(o[k].is<const char*>(), k);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v, o[k].as<const char*>(), k);
}

static void expectNullKey(JsonObjectConst o, const char* k) {
    TEST_ASSERT_TRUE_MESSAGE(o[k].isNull(), k);
    TEST_ASSERT_TRUE_MESSAGE(o[k].is<std::nullptr_t>(), k);
}

static JsonObjectConst parseCtl(JsonDocument& doc, const char* buf) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DeserializationError::Ok), static_cast<int>(deserializeJson(doc, buf).code()));
    JsonObjectConst ctl = doc["ctl"].as<JsonObjectConst>();
    TEST_ASSERT_FALSE(ctl.isNull());
    return ctl;
}

// ---- JSON ---------------------------------------------------------------------

static void test_ctl_json_full_shape() {
    static char buf[STATE_CAP];
    CommonState s = sampleState();
    s.antiSeize.output[HH_AS_K2].running = true;
    HomeHeatingStatus st = sampleStatus();
    st.p4OffDelayLeftS = 123;

    const size_t n = buildState(s, &st, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT32(strlen(buf), n);
    JsonDocument doc;
    JsonObjectConst ctl = parseCtl(doc, buf);
    expectBoolKey(ctl, "ok", true);
    expectBoolKey(ctl, "en", true);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"set\":40,"));   // JsonOut trims a zero fraction
    TEST_ASSERT_EQUAL_FLOAT(40.0f, ctl["set"].as<float>());
    TEST_ASSERT_EQUAL_UINT32(10, ctl.size());   // stage 09 (C15): + "tu", "st"

    JsonObjectConst p4 = ctl["p4"].as<JsonObjectConst>();
    expectBoolKey(p4, "on", true);
    expectStrKey(p4, "r", "demand");
    expectIntKey(p4, "dlyS", 123);
    expectBoolKey(p4, "as", false);
    TEST_ASSERT_EQUAL_UINT32(4, p4.size());

    JsonObjectConst k2 = ctl["k2"].as<JsonObjectConst>();
    expectBoolKey(k2, "byp", false);
    expectStrKey(k2, "r", "charging");
    expectBoolKey(k2, "as", true);
    TEST_ASSERT_EQUAL_UINT32(3, k2.size());

    JsonObjectConst k1 = ctl["k1"].as<JsonObjectConst>();
    expectIntKey(k1, "pos", 35);
    expectIntKey(k1, "mv", 1);
    expectStrKey(k1, "m", "normal");
    expectIntKey(k1, "ff", 42);
    expectBoolKey(k1, "as", false);
    TEST_ASSERT_EQUAL_UINT32(5, k1.size());

    expectStrKey(ctl, "fs", "none");
    expectBoolKey(ctl, "nn", false);
}

static void test_ctl_json_nulls_modes_and_flags() {
    static char buf[STATE_CAP];
    CommonState s = sampleState();
    s.antiSeize.output[HH_AS_P4].running = true;
    s.antiSeize.output[HH_AS_K1].running = true;
    HomeHeatingStatus st = sampleStatus();
    st.heatingEnabled = false;
    st.h2Set = 52.5f;
    st.p4On = false;
    st.p4Reason = P4Reason::MultiFaultOff;
    st.k2Bypass = true;
    st.k2Reason = K2Reason::H4Full;
    st.k1Mode = K1Mode::Recalibrating;
    st.k1Known = false;
    st.k1Moving = -1;
    st.k1FfValid = false;
    st.fail = FailMode::Multi;
    st.noNeed = true;

    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    JsonDocument doc;
    JsonObjectConst ctl = parseCtl(doc, buf);
    expectBoolKey(ctl, "en", false);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"set\":52.5"));
    expectBoolKey(ctl["p4"], "on", false);
    expectStrKey(ctl["p4"], "r", "multi_fault_off");
    expectBoolKey(ctl["p4"], "as", true);
    expectBoolKey(ctl["k2"], "byp", true);
    expectStrKey(ctl["k2"], "r", "h4_full");
    expectBoolKey(ctl["k2"], "as", false);
    expectNullKey(ctl["k1"], "pos");
    expectNullKey(ctl["k1"], "ff");
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"pos\":null,"));   // present as null, not missing
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ff\":null,"));
    expectIntKey(ctl["k1"], "mv", -1);
    expectStrKey(ctl["k1"], "m", "recal");
    expectBoolKey(ctl["k1"], "as", true);
    expectStrKey(ctl, "fs", "multi");
    expectBoolKey(ctl, "nn", true);

    // Clamped whole percent; idle; anti-seize index beyond count reads false.
    st.k1Known = true;
    st.k1PosPct = 100.7f;
    st.k1FfValid = true;
    st.k1FfPct = -3.0f;
    st.k1Moving = 0;
    s.antiSeize.count = 2;   // HH_AS_K1 (2) is out of range now
    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    JsonDocument doc2;
    JsonObjectConst ctl2 = parseCtl(doc2, buf);
    expectIntKey(ctl2["k1"], "pos", 100);
    expectIntKey(ctl2["k1"], "ff", 0);
    expectIntKey(ctl2["k1"], "mv", 0);
    expectBoolKey(ctl2["k1"], "as", false);
}

static void test_ctl_json_null_or_not_ready_is_ok_false() {
    static char buf[STATE_CAP];
    CommonState s = sampleState();
    TEST_ASSERT_TRUE(buildState(s, nullptr, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ctl\":{\"ok\":false}}"));
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf));
    TEST_ASSERT_FALSE(doc["ctl"]["ok"].as<bool>());

    HomeHeatingStatus st = sampleStatus();
    st.ready = false;
    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ctl\":{\"ok\":false}}"));
}

static bool fmtWorst(uint32_t, char* out, size_t cap, void*) {
    snprintf(out, cap, "2099-12-31 23:59:59");
    return true;
}

static void fillText(char* dst, size_t len) {   // '"' escapes to 2 chars: the longest printable encoding
    memset(dst, '"', len);
    dst[len] = '\0';
}

static void test_ctl_json_worst_case_fits_state_cap() {
    static CommonState s;
    s = CommonState{};
    // Base state at its longest: every text field full of escaped quotes,
    // every counter at the maximum, every flag set, 4 Ok sensors at 6-char temps.
    s.network.wifiConnected = s.network.mqttConnected = s.network.setupApActive = true;
    s.network.mqttEnabled = s.network.scanRunning = true;
    s.network.wifiRssi = -128;
    fillText(s.network.ip, NET_IP_TEXT_LEN);
    fillText(s.network.ssid, NET_NAME_TEXT_LEN);
    fillText(s.network.hostname, NET_NAME_TEXT_LEN);
    fillText(s.network.apSsid, NET_NAME_TEXT_LEN);
    fillText(s.network.apIp, NET_IP_TEXT_LEN);
    s.network.wifiDownS = s.network.wifiConnectCount = s.network.mqttConnectCount = UINT32_MAX;
    s.network.mqttDroppedCommands = UINT32_MAX;
    s.network.scanCount = UINT8_MAX;
    s.time = {UINT32_MAX, true, TimeSourceKind::Ntp, true, true, UINT32_MAX};
    s.alarms.activeMask = UINT32_MAX;
    s.diag = {UINT32_MAX, true, true, true, true, true, true};
    for (size_t i = 0; i < RELAY_CHANNEL_COUNT; ++i) {
        s.relays.on[i] = true;
        s.relays.channel[i] = {true, true, true, UINT16_MAX, RelayReason::AntiSeize};
    }
    s.relays.ioError = s.relays.inhibited = true;
    s.relays.ioErrorCount = UINT32_MAX;
    s.sensors.count = HH_SENSOR_COUNT;
    for (uint8_t i = 0; i < HH_SENSOR_COUNT; ++i) {
        setSensor(s, i, SensorState::Ok, -54.99f);
        s.sensors.sensor[i].missing = true;
    }
    s.k1 = {true, true, true, true, UINT32_MAX};
    s.antiSeize.count = 3;
    for (size_t i = 0; i < 3; ++i) s.antiSeize.output[i] = {true, true, true, UINT32_MAX};
    s.ota = {true, OtaSource::Espota, OtaBootOutcome::UpdatedNoRollback, true, UINT16_MAX};
    fillText(s.versions.fw, VERSION_TEXT_LEN);
    fillText(s.versions.web, VERSION_TEXT_LEN);
    s.versions.webMismatch = true;
    s.system = {ResetCause{}, UINT32_MAX, true, true, UINT64_MAX, ResetGatePhase::Countdown, UINT8_MAX,
        UINT32_MAX, UINT8_MAX};

    // ctl at its longest: the longest key of every enum, every number present
    // (a number beats null) at its widest, every flag true.
    HomeHeatingStatus st{};
    st.ready = st.heatingEnabled = st.p4On = st.k2Bypass = true;
    st.h2Set = -99999.9f;
    st.p4Reason = P4Reason::MultiFaultOff;      // "multi_fault_off"
    st.p4OffDelayLeftS = UINT32_MAX;
    st.k2Reason = K2Reason::SensorWait;         // "sensor_wait"
    st.k1Mode = K1Mode::FailPosFixed;           // "failpos_fixed"
    st.k1Known = st.k1FfValid = true;
    st.k1PosPct = st.k1FfPct = 100.0f;
    st.k1Moving = -1;
    st.fail = FailMode::Multi;                  // "multi"
    st.noNeed = true;
    st.alarmMask = HH_ALARM_OWNED_MASK;
    // Stage 09 (C15): "tu" + "st" at their widest -- every nullable present as a
    // number, max counters, the longest block key, a Result with all numbers.
    st.h2ErrValid = true;
    st.h2ErrC = -99999.9f;
    st.lastPulseDir = -1;
    st.lastPulseS = -99999.9f;
    st.pulsesToday = UINT32_MAX;
    st.pulsesYesterdayValid = true;
    st.pulsesYesterday = UINT32_MAX;
    st.step.block = StepBlock::K1Headroom;     // "k1_headroom" (11, the longest with "unavailable"/"h3_unsteady")
    st.step.running = true;
    st.step.elapsedS = UINT32_MAX;
    st.step.pulseS = UINT32_MAX;
    st.step.deadSeen = true;
    st.step.deadTimeS = 99999.9f;
    st.step.last.outcome = StepOutcome::Result;
    st.step.last.deadTimeS = 99999.9f;
    st.step.last.responseCps = -9999.999f;
    st.step.last.pulseS = UINT32_MAX;
    st.step.last.suggest = {true, UINT32_MAX, -99999.9f};

    static char buf[STATE_CAP];
    const WebJsonContext c{"home-heating", &HOME_HEATING_HW, fmtWorst, nullptr, homeHeatingStateJson, &st};
    const size_t n = buildStateJson(s, c, buf, sizeof(buf));
    char msg[48];
    snprintf(msg, sizeof(msg), "worst-case /api/state = %u bytes", static_cast<unsigned>(n));
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(n > 0 && n < STATE_CAP, msg);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf));
    TEST_ASSERT_TRUE(doc["ctl"]["ok"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("multi_fault_off", doc["ctl"]["p4"]["r"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("failpos_fixed", doc["ctl"]["k1"]["m"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, doc["ctl"]["p4"]["dlyS"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, doc["ctl"]["tu"]["pt"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, doc["ctl"]["tu"]["py"].as<uint32_t>());
    TEST_ASSERT_EQUAL_STRING("k1_headroom", doc["ctl"]["st"]["blk"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, doc["ctl"]["st"]["res"]["p"].as<uint32_t>());
    TEST_ASSERT_TRUE(doc["ctl"]["st"]["res"]["r"].is<float>());

    // The longest aborted shape ("aborted" + "heating_off" + a dead time) is shorter
    // than the Result above; it fits too.
    st.step.last.outcome = StepOutcome::Aborted;
    st.step.last.abort = StepAbort::HeatingOff;
    st.step.last.suggest.valid = false;
    const size_t nAborted = buildStateJson(s, c, buf, sizeof(buf));
    TEST_ASSERT_TRUE(nAborted > 0 && nAborted <= n);
}

// ---- stage 09 (C15): "tu" + "st" ------------------------------------------------

static HomeHeatingStatus tuningStatus() {
    HomeHeatingStatus st = sampleStatus();
    st.h2ErrValid = true;
    st.h2ErrC = -0.44f;
    st.lastPulseDir = 1;
    st.lastPulseS = 4.5f;
    st.pulsesToday = 12;
    st.pulsesYesterdayValid = true;
    st.pulsesYesterday = 7;
    st.step.block = StepBlock::P4Off;
    st.step.running = false;
    st.step.elapsedS = 0;
    st.step.pulseS = 10;
    st.step.last.outcome = StepOutcome::Result;
    st.step.last.abort = StepAbort::None;
    st.step.last.deadTimeS = 20.0f;
    st.step.last.responseCps = 0.25f;
    st.step.last.pulseS = 10;
    st.step.last.suggest = {true, 30, 2.0f};
    return st;
}

static void test_ctl_json_tuning_and_step_shape() {
    static char buf[STATE_CAP];
    CommonState s = sampleState();
    HomeHeatingStatus st = tuningStatus();
    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    JsonDocument doc;
    JsonObjectConst ctl = parseCtl(doc, buf);
    // Order: after "nn".
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"nn\":false,\"tu\":{"));

    JsonObjectConst tu = ctl["tu"].as<JsonObjectConst>();
    TEST_ASSERT_EQUAL_UINT32(5, tu.size());
    TEST_ASSERT_TRUE(tu["err"].is<float>());
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"err\":-0.4,"));   // 1 decimal
    expectIntKey(tu, "lpd", 1);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"lps\":4.5,"));
    expectIntKey(tu, "pt", 12);
    expectIntKey(tu, "py", 7);

    JsonObjectConst stp = ctl["st"].as<JsonObjectConst>();
    TEST_ASSERT_EQUAL_UINT32(6, stp.size());
    expectBoolKey(stp, "run", false);
    expectStrKey(stp, "blk", "p4_off");
    expectIntKey(stp, "el", 0);
    expectIntKey(stp, "ps", 10);
    expectNullKey(stp, "dt");                              // not running: not seen
    JsonObjectConst res = stp["res"].as<JsonObjectConst>();
    TEST_ASSERT_FALSE(res.isNull());
    TEST_ASSERT_EQUAL_UINT32(6, res.size());
    expectStrKey(res, "o", "result");
    expectNullKey(res, "ab");
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"res\":{\"o\":\"result\",\"ab\":null,\"dt\":20,\"r\":0.25,\"p\":30,\"g\":2}"));
    TEST_ASSERT_EQUAL_FLOAT(0.25f, res["r"].as<float>());
    expectIntKey(res, "p", 30);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, res["g"].as<float>());

    // Running: live dead time once seen (1 decimal), direction close, 3-decimal r.
    st.step.running = true;
    st.step.block = StepBlock::Running;
    st.step.elapsedS = 42;
    st.step.deadSeen = true;
    st.step.deadTimeS = 18.26f;
    st.lastPulseDir = -1;
    st.step.last.responseCps = 0.1234f;
    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    JsonDocument doc2;
    JsonObjectConst ctl2 = parseCtl(doc2, buf);
    expectBoolKey(ctl2["st"], "run", true);
    expectStrKey(ctl2["st"], "blk", "running");
    expectIntKey(ctl2["st"], "el", 42);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ps\":10,\"dt\":18.3,"));
    expectIntKey(ctl2["tu"], "lpd", -1);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"r\":0.123,"));
}

static void test_ctl_json_tuning_and_step_nulls() {
    static char buf[STATE_CAP];
    CommonState s = sampleState();
    HomeHeatingStatus st = tuningStatus();
    st.h2ErrValid = false;                 // err null when invalid
    st.pulsesYesterdayValid = false;       // py null before the first rollover
    st.lastPulseDir = 0;
    st.lastPulseS = 0.0f;
    st.step.last = StepTestResult{};       // res null while no outcome
    st.step.block = StepBlock::Unavailable;
    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    JsonDocument doc;
    JsonObjectConst ctl = parseCtl(doc, buf);
    expectNullKey(ctl["tu"], "err");
    expectNullKey(ctl["tu"], "py");
    expectIntKey(ctl["tu"], "lpd", 0);
    expectIntKey(ctl["tu"], "lps", 0);
    expectStrKey(ctl["st"], "blk", "unavailable");
    expectNullKey(ctl["st"], "res");
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"res\":null}"));   // present as null

    // NoResponse: dead time 0 = not seen -> null; r / p / g null.
    st.step.last.outcome = StepOutcome::NoResponse;
    st.step.last.deadTimeS = 0.0f;
    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    JsonDocument doc2;
    JsonObjectConst res = parseCtl(doc2, buf)["st"]["res"].as<JsonObjectConst>();
    expectStrKey(res, "o", "no_response");
    expectNullKey(res, "ab");
    expectNullKey(res, "dt");
    expectNullKey(res, "r");
    expectNullKey(res, "p");
    expectNullKey(res, "g");

    // Aborted after the dead time was seen: ab present, dt kept, r / p / g null.
    st.step.last.outcome = StepOutcome::Aborted;
    st.step.last.abort = StepAbort::Cancel;
    st.step.last.deadTimeS = 12.0f;
    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    JsonDocument doc3;
    JsonObjectConst res3 = parseCtl(doc3, buf)["st"]["res"].as<JsonObjectConst>();
    expectStrKey(res3, "o", "aborted");
    expectStrKey(res3, "ab", "cancel");
    expectIntKey(res3, "dt", 12);
    expectNullKey(res3, "r");
    expectNullKey(res3, "p");
    expectNullKey(res3, "g");

    // A Result without a valid suggestion: r present, p / g null.
    st.step.last.outcome = StepOutcome::Result;
    st.step.last.responseCps = 0.5f;
    st.step.last.suggest = {false, 0, 0.0f};
    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    JsonDocument doc4;
    JsonObjectConst res4 = parseCtl(doc4, buf)["st"]["res"].as<JsonObjectConst>();
    expectNullKey(res4, "ab");
    TEST_ASSERT_EQUAL_FLOAT(0.5f, res4["r"].as<float>());
    expectNullKey(res4, "p");
    expectNullKey(res4, "g");
}

// ---- Pages --------------------------------------------------------------------

// Every line is printable ASCII, <= 21 chars (Normal) and never cut.
static void expectCleanFrame(const DisplayFrame& f) {
    for (uint8_t i = 0; i < f.lineCount; ++i) {
        const char* t = f.line[i].text;
        const size_t len = strlen(t);
        TEST_ASSERT_TRUE_MESSAGE(len <= HH_PAGE_ROW_CHARS, t);
        // displayFitText marks a cut by ending a full-width line with '~'.
        TEST_ASSERT_FALSE_MESSAGE(len == HH_PAGE_ROW_CHARS && t[len - 1] == '~', t);
        for (const char* p = t; *p != '\0'; ++p) {
            TEST_ASSERT_TRUE_MESSAGE(*p >= 0x20 && *p <= 0x7E, t);
        }
    }
}

// Title + rows, in order, exactly (no row dropped or cut).
static void expectFrame(const DisplayFrame& f, const char* title, const char* const* rows, size_t n) {
    expectCleanFrame(f);
    TEST_ASSERT_EQUAL_UINT8(n + 1, f.lineCount);
    TEST_ASSERT_TRUE(f.titleRule);
    TEST_ASSERT_EQUAL_STRING(title, f.line[0].text);
    for (size_t i = 0; i < n; ++i) {
        TEST_ASSERT_EQUAL_STRING(rows[i], f.line[i + 1].text);
        TEST_ASSERT_EQUAL_UINT8(DISPLAY_BODY_BASELINE[i], f.line[i + 1].baseline);
    }
}

static DisplayFrame g_frame;

static void test_page_heating_normal() {
    CommonState s = sampleState();
    s.relays.on[HH_RELAY_P4] = true;
    HomeHeatingStatus st = sampleStatus();
    renderHeatingPage(s, g_frame, &st);
    const char* rows[] = {"H2 41.2 set 40.0", "H3 70.1", "K1 35% >", "P4 ON demand"};
    expectFrame(g_frame, "Heating", rows, 4);

    st.k1Moving = -1;
    st.k1PosPct = 0.0f;
    renderHeatingPage(s, g_frame, &st);
    TEST_ASSERT_EQUAL_STRING("K1 0% <", g_frame.line[3].text);
    st.k1Moving = 0;
    st.k1PosPct = 100.0f;
    renderHeatingPage(s, g_frame, &st);
    TEST_ASSERT_EQUAL_STRING("K1 100%", g_frame.line[3].text);
}

static void test_page_heating_recal_unknown_and_actual_relay() {
    CommonState s = sampleState();
    s.relays.on[HH_RELAY_P4] = false;   // requested but lock-delayed: the ACTUAL relay is shown
    HomeHeatingStatus st = sampleStatus();
    st.k1Mode = K1Mode::Recalibrating;
    st.k1Known = false;
    st.k1Moving = 0;
    renderHeatingPage(s, g_frame, &st);
    const char* rows[] = {"H2 41.2 set 40.0", "H3 70.1", "K1 recal", "P4 OFF demand"};
    expectFrame(g_frame, "Heating", rows, 4);

    st.k1Moving = -1;
    renderHeatingPage(s, g_frame, &st);
    TEST_ASSERT_EQUAL_STRING("K1 recal <", g_frame.line[3].text);

    st.k1Mode = K1Mode::Unknown;
    st.k1Moving = 0;
    renderHeatingPage(s, g_frame, &st);
    TEST_ASSERT_EQUAL_STRING("K1 ?", g_frame.line[3].text);
}

static void test_page_heating_fault_temps_and_fail_row() {
    CommonState s = sampleState();
    setSensor(s, HH_SENSOR_H2, SensorState::Fault, 0.0f);
    setSensor(s, HH_SENSOR_H3, SensorState::Unassigned, 0.0f);
    s.relays.on[HH_RELAY_P4] = false;
    HomeHeatingStatus st = sampleStatus();
    st.p4On = false;
    st.p4Reason = P4Reason::MultiFaultOff;
    st.k1Mode = K1Mode::FailPosFixed;
    st.k1Moving = 0;
    st.k1PosPct = 30.0f;
    st.fail = FailMode::Multi;
    renderHeatingPage(s, g_frame, &st);
    const char* rows[] = {"H2 --.- set 40.0", "H3 --.-", "K1 30%", "P4 OFF multi", "Sensors fail: K1 fix"};
    expectFrame(g_frame, "Heating", rows, 5);

    // Unknown (Pending) and a sensor index beyond count also read "--.-".
    setSensor(s, HH_SENSOR_H2, SensorState::Unknown, 0.0f);
    s.sensors.count = 2;
    st.fail = FailMode::H3;
    renderHeatingPage(s, g_frame, &st);
    TEST_ASSERT_EQUAL_STRING("H2 --.- set 40.0", g_frame.line[1].text);
    TEST_ASSERT_EQUAL_STRING("H3 --.-", g_frame.line[2].text);
    TEST_ASSERT_EQUAL_STRING("H3 fail: K1 fixed", g_frame.line[5].text);

    const FailMode modes[] = {FailMode::H2, FailMode::H1};
    const char* texts[] = {"H2 fail: K1 FF only", "H1 fail: K1 FB only"};
    for (size_t i = 0; i < 2; ++i) {
        st.fail = modes[i];
        renderHeatingPage(s, g_frame, &st);
        TEST_ASSERT_EQUAL_STRING(texts[i], g_frame.line[5].text);
    }
}

static void test_page_dhw_tank_bypass_and_no_need() {
    CommonState s = sampleState();
    HomeHeatingStatus st = sampleStatus();
    renderDhwPage(s, g_frame, &st);
    const char* tank[] = {"H3 70.1", "H4 50.0", "K2 TANK charging", "No need: no"};
    expectFrame(g_frame, "DHW", tank, 4);

    s.relays.on[HH_RELAY_K2] = true;   // energised = bypass (actual relay)
    setSensor(s, HH_SENSOR_H4, SensorState::Fault, 0.0f);
    st.k2Bypass = true;
    st.k2Reason = K2Reason::H4Full;
    st.noNeed = true;
    renderDhwPage(s, g_frame, &st);
    const char* byp[] = {"H3 70.1", "H4 --.-", "K2 BYP H4 full", "No need: yes"};
    expectFrame(g_frame, "DHW", byp, 4);
}

static void test_pages_starting_when_null_or_not_ready() {
    CommonState s = sampleState();
    HomeHeatingStatus st = sampleStatus();
    st.ready = false;
    const char* rows[] = {"starting..."};
    renderHeatingPage(s, g_frame, nullptr);
    expectFrame(g_frame, "Heating", rows, 1);
    renderHeatingPage(s, g_frame, &st);
    expectFrame(g_frame, "Heating", rows, 1);
    renderDhwPage(s, g_frame, nullptr);
    expectFrame(g_frame, "DHW", rows, 1);
    renderDhwPage(s, g_frame, &st);
    expectFrame(g_frame, "DHW", rows, 1);
}

// No-clip invariant with worst-case values: every reason / mode / fail mode,
// -55.0 temps, out-of-range setpoint and positions; every row kept whole.
static void test_pages_worst_case_never_clip() {
    CommonState s = sampleState();
    for (uint8_t i = 0; i < HH_SENSOR_COUNT; ++i) setSensor(s, i, SensorState::Ok, -55.0f);
    s.relays.on[HH_RELAY_P4] = false;
    s.relays.on[HH_RELAY_K2] = false;
    HomeHeatingStatus st = sampleStatus();
    const float sets[] = {-1.0e9f, 1.0e9f, NAN, 75.0f};
    const float pos[] = {-5.0f, 1.0e9f, NAN, 99.5f};
    for (uint8_t r = 0; r <= static_cast<uint8_t>(P4Reason::MultiFaultOff); ++r) {
        for (uint8_t m = 0; m <= static_cast<uint8_t>(K1Mode::FailPosFixed); ++m) {
            for (uint8_t fm = 0; fm <= static_cast<uint8_t>(FailMode::Multi); ++fm) {
                for (size_t v = 0; v < 4; ++v) {
                    st.p4Reason = static_cast<P4Reason>(r);
                    st.k1Mode = static_cast<K1Mode>(m);
                    st.fail = static_cast<FailMode>(fm);
                    st.h2Set = sets[v];
                    st.k1PosPct = pos[v];
                    st.k1Moving = -1;
                    renderHeatingPage(s, g_frame, &st);
                    expectCleanFrame(g_frame);
                    TEST_ASSERT_EQUAL_UINT8(fm == 0 ? 5 : 6, g_frame.lineCount);
                }
            }
        }
    }
    st.h2Set = -1.0e9f;
    renderHeatingPage(s, g_frame, &st);
    TEST_ASSERT_EQUAL_STRING("H2 -55.0 set -999.9", g_frame.line[1].text);
    st.k1Mode = K1Mode::Normal;
    st.k1PosPct = 1.0e9f;
    renderHeatingPage(s, g_frame, &st);
    TEST_ASSERT_EQUAL_STRING("K1 100% <", g_frame.line[3].text);

    for (uint8_t r = 0; r <= static_cast<uint8_t>(K2Reason::H4Fault); ++r) {
        st.k2Reason = static_cast<K2Reason>(r);
        renderDhwPage(s, g_frame, &st);
        expectCleanFrame(g_frame);
        TEST_ASSERT_EQUAL_UINT8(5, g_frame.lineCount);
    }
}

// ---- HA custom entities -------------------------------------------------------

struct HaFixture {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log{logStore, clock};
    ConfigEngine config{cfgStore, log};
    HaEntityRegistry reg;

    HaRegistryStatus begin() {
        TEST_ASSERT_TRUE(log.begin());
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HOME_HEATING_SCHEMA, 0)));
        return reg.build(config, HOME_HEATING_HW, HOME_HEATING_HA_ENTITIES, HOME_HEATING_HA_ENTITY_COUNT);
    }
    int find(const char* key) const { return reg.findByKey(key, strlen(key)); }
    // formatState's availability; the text is left in out.
    bool state(const char* key, const CommonState& s, char* out, size_t cap) {
        const int i = find(key);
        TEST_ASSERT_TRUE_MESSAGE(i >= 0, key);
        return reg.formatState(static_cast<size_t>(i), s, config, out, cap);
    }
};

static HaFixture* g_ha = nullptr;

static void expectHaState(const char* key, const CommonState& s, bool avail, const char* text) {
    char out[HA_STATE_MAX_LEN + 1] = {};
    TEST_ASSERT_EQUAL_MESSAGE(avail, g_ha->state(key, s, out, sizeof(out)), key);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(text, out, key);
}

struct ExpectedEntity {
    const char* key;
    HaComponent component;
    const char* unit;
    const char* deviceClass;
    const char* stateClass;
    const char* category;
};

static const ExpectedEntity EXPECTED[] = {
    {"no_need", HaComponent::BinarySensor, nullptr, nullptr, nullptr, nullptr},
    {"p4_reason", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr},
    {"k2_mode", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr},
    {"k2_reason", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr},
    {"k1_position", HaComponent::Sensor, "%", nullptr, "measurement", nullptr},
    {"k1_mode", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr},
    {"failsafe", HaComponent::Sensor, nullptr, nullptr, nullptr, "diagnostic"},
    {"alarm_h3_failsafe", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"alarm_h2_failsafe", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"alarm_h1_failsafe", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"alarm_multi_failsafe", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"alarm_h4_failsafe", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"alarm_h1_fault", HaComponent::BinarySensor, nullptr, "problem", nullptr, "diagnostic"},
    {"alarm_h2_fault", HaComponent::BinarySensor, nullptr, "problem", nullptr, "diagnostic"},
    {"alarm_h3_fault", HaComponent::BinarySensor, nullptr, "problem", nullptr, "diagnostic"},
    {"alarm_h4_fault", HaComponent::BinarySensor, nullptr, "problem", nullptr, "diagnostic"},
    // stage 09 (C15), appended
    {"warn_p4_no_flow", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"h2_error", HaComponent::Sensor, "\xC2\xB0" "C", nullptr, "measurement", nullptr},
    {"k1_last_pulse", HaComponent::Sensor, "s", nullptr, "measurement", nullptr},
    {"k1_last_pulse_dir", HaComponent::Sensor, nullptr, nullptr, nullptr, "diagnostic"},
    {"k1_pulses_today", HaComponent::Sensor, nullptr, nullptr, "total_increasing", "diagnostic"},
    {"k1_pulses_yesterday", HaComponent::Sensor, nullptr, nullptr, nullptr, "diagnostic"},
};
constexpr size_t FIRST_ALARM_ENTITY = 7;
static const uint8_t EXPECTED_ALARM_BIT[] = {0, 1, 2, 3, 4, 8, 9, 10, 11};   // EXPECTED[7..]
static const char* const STATUS_KEYS[] = {"no_need", "p4_reason", "k2_mode", "k2_reason", "k1_position",
    "k1_mode", "failsafe"};

static void expectNullableStr(const char* want, const char* got, const char* key) {
    if (want == nullptr) {
        TEST_ASSERT_NULL_MESSAGE(got, key);
    } else {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, key);
    }
}

static void test_ha_registry_builds_with_custom_entities() {
    HaFixture fx;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(fx.begin()));
    TEST_ASSERT_TRUE(fx.reg.count() <= HA_MAX_ENTITIES);
    TEST_ASSERT_EQUAL_UINT32(22, HOME_HEATING_HA_ENTITY_COUNT);   // stage 09 (C15): 16 -> 22
    TEST_ASSERT_EQUAL_UINT32(sizeof(EXPECTED) / sizeof(EXPECTED[0]), HOME_HEATING_HA_ENTITY_COUNT);
    for (size_t a = 0; a < HOME_HEATING_HA_ENTITY_COUNT; ++a) {
        TEST_ASSERT_EQUAL_STRING(EXPECTED[a].key, HOME_HEATING_HA_ENTITIES[a].key);   // order is binding too
        for (size_t b = a + 1; b < HOME_HEATING_HA_ENTITY_COUNT; ++b) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(HOME_HEATING_HA_ENTITIES[a].key, HOME_HEATING_HA_ENTITIES[b].key));
        }
    }
    for (const ExpectedEntity& x : EXPECTED) {
        const int i = fx.find(x.key);
        TEST_ASSERT_TRUE_MESSAGE(i >= 0, x.key);
        const HaEntity& e = fx.reg.entity(static_cast<size_t>(i));
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(HaSource::Custom), static_cast<int>(e.source), x.key);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(x.component), static_cast<int>(e.component), x.key);
        TEST_ASSERT_FALSE_MESSAGE(fx.reg.isCommandable(static_cast<size_t>(i)), x.key);
        TEST_ASSERT_NOT_NULL(e.custom);
        TEST_ASSERT_NOT_NULL(e.custom->name);
        expectNullableStr(x.unit, e.custom->unit, x.key);
        expectNullableStr(x.deviceClass, e.custom->deviceClass, x.key);
        expectNullableStr(x.stateClass, e.custom->stateClass, x.key);
        expectNullableStr(x.category, e.custom->entityCategory, x.key);
    }
    TEST_ASSERT_EQUAL_STRING("No need", HOME_HEATING_HA_ENTITIES[0].name);
    TEST_ASSERT_EQUAL_STRING("P4 reason", HOME_HEATING_HA_ENTITIES[1].name);
    TEST_ASSERT_EQUAL_STRING("K1 position", HOME_HEATING_HA_ENTITIES[4].name);
    // The existing registry still provides the temperatures, relays, switch and number.
    const char* existing[] = {"temp_h1", "temp_h2", "temp_h3", "temp_h4", "relay_p4", "relay_k2",
        "heating_enabled", "h2_set"};
    for (const char* k : existing) {
        TEST_ASSERT_TRUE_MESSAGE(fx.find(k) >= 0, k);
    }
}

static void test_ha_discovery_payloads_fit() {
    HaFixture fx;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(fx.begin()));
    static char payload[HA_DISCOVERY_PAYLOAD_MAX];
    char topic[HA_TOPIC_MAX];
    for (size_t i = 0; i < fx.reg.count(); ++i) {
        const HaEntity& e = fx.reg.entity(i);
        const size_t n = buildDiscoveryPayload(fx.reg, i, fx.config, HOME_HEATING_NET, "1.2.3-test", payload,
            sizeof(payload));
        TEST_ASSERT_TRUE_MESSAGE(n > 0 && n < HA_DISCOVERY_PAYLOAD_MAX, e.key);
        TEST_ASSERT_TRUE_MESSAGE(buildDiscoveryTopic(HOME_HEATING_NET, e, topic, sizeof(topic)), e.key);
        TEST_ASSERT_TRUE_MESSAGE(buildStateTopic(HOME_HEATING_NET, e, topic, sizeof(topic)), e.key);
    }
    const int k1 = fx.find("k1_position");
    TEST_ASSERT_TRUE(k1 >= 0);
    TEST_ASSERT_TRUE(buildDiscoveryPayload(fx.reg, static_cast<size_t>(k1), fx.config, HOME_HEATING_NET,
        "1.2.3-test", payload, sizeof(payload)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"unit_of_measurement\":\"%\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"state_class\":\"measurement\""));
}

static void test_ha_status_entities_unbound_not_ready_and_bound() {
    HaFixture fx;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(fx.begin()));
    g_ha = &fx;
    CommonState s = sampleState();

    bindHomeHeatingHaStatus(nullptr);   // unbound: unavailable
    for (const char* k : STATUS_KEYS) expectHaState(k, s, false, "None");

    HomeHeatingStatus st = sampleStatus();
    st.ready = false;                   // bound but not ready: unavailable too
    bindHomeHeatingHaStatus(&st);
    for (const char* k : STATUS_KEYS) expectHaState(k, s, false, "None");

    st = sampleStatus();
    expectHaState("no_need", s, true, "OFF");
    expectHaState("p4_reason", s, true, "demand");
    expectHaState("k2_mode", s, true, "tank");
    expectHaState("k2_reason", s, true, "charging");
    expectHaState("k1_position", s, true, "35");
    expectHaState("k1_mode", s, true, "normal");
    expectHaState("failsafe", s, true, "none");

    st.noNeed = true;
    st.p4Reason = P4Reason::MultiFaultOff;
    st.k2Bypass = true;
    st.k2Reason = K2Reason::H4Fault;
    st.k1PosPct = 100.6f;
    st.k1Mode = K1Mode::FailPosFixed;
    st.fail = FailMode::Multi;
    expectHaState("no_need", s, true, "ON");
    expectHaState("p4_reason", s, true, "multi_fault_off");
    expectHaState("k2_mode", s, true, "bypass");
    expectHaState("k2_reason", s, true, "h4_fault");
    expectHaState("k1_position", s, true, "100");
    expectHaState("k1_mode", s, true, "failpos_fixed");
    expectHaState("failsafe", s, true, "multi");

    // k1_position is unavailable while the estimate is unknown (recal / boot).
    st.k1Known = false;
    st.k1Mode = K1Mode::Recalibrating;
    expectHaState("k1_position", s, false, "None");
    expectHaState("k1_mode", s, true, "recal");
    g_ha = nullptr;
}

static void test_ha_alarm_entities_follow_mask_bits() {
    HaFixture fx;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(fx.begin()));
    g_ha = &fx;
    CommonState s = sampleState();
    // Alarms read CommonState only: available even while the status is unbound.
    for (size_t a = 0; a < sizeof(EXPECTED_ALARM_BIT); ++a) {
        const char* key = EXPECTED[FIRST_ALARM_ENTITY + a].key;
        s.alarms.activeMask = 0;
        expectHaState(key, s, true, "OFF");
        s.alarms.activeMask = 1u << EXPECTED_ALARM_BIT[a];
        expectHaState(key, s, true, "ON");
        s.alarms.activeMask = ~(1u << EXPECTED_ALARM_BIT[a]);   // every other bit set
        expectHaState(key, s, true, "OFF");
    }
    g_ha = nullptr;
}

// Existing stage-04 behaviour: a logical sensor entity is unavailable while its
// sensor is not Ok.
static void test_ha_temp_h3_unavailable_on_fault() {
    HaFixture fx;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(fx.begin()));
    g_ha = &fx;
    CommonState s = sampleState();
    char out[HA_STATE_MAX_LEN + 1] = {};
    TEST_ASSERT_TRUE(fx.state("temp_h3", s, out, sizeof(out)));
    TEST_ASSERT_EQUAL_FLOAT(70.1f, static_cast<float>(atof(out)));
    setSensor(s, HH_SENSOR_H3, SensorState::Fault, 0.0f);
    expectHaState("temp_h3", s, false, "None");
    g_ha = nullptr;
}

// Stage 09 (C15): the 6 appended entities.
static void test_ha_stage09_entities() {
    HaFixture fx;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(fx.begin()));
    g_ha = &fx;
    CommonState s = sampleState();

    // warn_p4_no_flow reads CommonState only: available while unbound, follows bit 0.
    bindHomeHeatingHaStatus(nullptr);
    s.diag.warningMask = 0;
    expectHaState("warn_p4_no_flow", s, true, "OFF");
    s.diag.warningMask = 1u << HH_WARN_H1;
    expectHaState("warn_p4_no_flow", s, true, "ON");
    s.diag.warningMask = ~(1u << HH_WARN_H1);
    expectHaState("warn_p4_no_flow", s, true, "OFF");

    const char* statusKeys[] = {"h2_error", "k1_last_pulse", "k1_last_pulse_dir", "k1_pulses_today",
        "k1_pulses_yesterday"};
    for (const char* k : statusKeys) expectHaState(k, s, false, "None");   // unbound
    HomeHeatingStatus st = sampleStatus();
    st.h2ErrValid = true;
    st.h2ErrC = -0.44f;
    st.lastPulseDir = 1;
    st.lastPulseS = 4.5f;
    st.pulsesToday = 12;
    st.pulsesYesterdayValid = true;
    st.pulsesYesterday = 7;
    st.ready = false;
    bindHomeHeatingHaStatus(&st);
    for (const char* k : statusKeys) expectHaState(k, s, false, "None");   // not ready

    st.ready = true;
    expectHaState("h2_error", s, true, "-0.4");
    expectHaState("k1_last_pulse", s, true, "4.5");
    expectHaState("k1_last_pulse_dir", s, true, "open");
    expectHaState("k1_pulses_today", s, true, "12");
    expectHaState("k1_pulses_yesterday", s, true, "7");

    st.lastPulseDir = -1;
    expectHaState("k1_last_pulse_dir", s, true, "close");
    st.pulsesToday = UINT32_MAX;
    expectHaState("k1_pulses_today", s, true, "4294967295");

    // h2_error unavailable when invalid; k1_last_pulse while dir 0; yesterday until valid.
    st.h2ErrValid = false;
    expectHaState("h2_error", s, false, "None");
    st.lastPulseDir = 0;
    expectHaState("k1_last_pulse", s, false, "None");
    expectHaState("k1_last_pulse_dir", s, true, "none");
    st.pulsesYesterdayValid = false;
    expectHaState("k1_pulses_yesterday", s, false, "None");
    g_ha = nullptr;
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ctl_json_full_shape);
    RUN_TEST(test_ctl_json_nulls_modes_and_flags);
    RUN_TEST(test_ctl_json_null_or_not_ready_is_ok_false);
    RUN_TEST(test_ctl_json_worst_case_fits_state_cap);
    RUN_TEST(test_ctl_json_tuning_and_step_shape);
    RUN_TEST(test_ctl_json_tuning_and_step_nulls);
    RUN_TEST(test_page_heating_normal);
    RUN_TEST(test_page_heating_recal_unknown_and_actual_relay);
    RUN_TEST(test_page_heating_fault_temps_and_fail_row);
    RUN_TEST(test_page_dhw_tank_bypass_and_no_need);
    RUN_TEST(test_pages_starting_when_null_or_not_ready);
    RUN_TEST(test_pages_worst_case_never_clip);
    RUN_TEST(test_ha_registry_builds_with_custom_entities);
    RUN_TEST(test_ha_discovery_payloads_fit);
    RUN_TEST(test_ha_status_entities_unbound_not_ready_and_bound);
    RUN_TEST(test_ha_alarm_entities_follow_mask_bits);
    RUN_TEST(test_ha_temp_h3_unavailable_on_fault);
    RUN_TEST(test_ha_stage09_entities);
    return UNITY_END();
}
