#include <unity.h>
#include <ArduinoJson.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <BoilerRoomAlarms.h>
#include <BoilerRoomDiagnostics.h>
#include <BoilerRoomHa.h>
#include <BoilerRoomJson.h>
#include <BoilerRoomPages.h>
#include <BoilerRoomStatus.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <DisplayFormat.h>
#include <DisplayFrame.h>
#include <EventLog.h>
#include <HaEntityRegistry.h>
#include <WebJson.h>
#include "../../../common/test/fakes/FakeClock.h"
#include "../../../common/test/fakes/MemoryKvStore.h"
#include "../../src/BoilerRoomHardware.h"
#include "../../src/BoilerRoomSchema.h"

// Stage 07 phase 5: the pure views over BoilerRoomStatus -- the "ctl"
// /api/state extension, the three OLED pages and the HA custom entities.

void setUp() {
    bindBoilerRoomHaStatus(nullptr);
}
void tearDown() {
    bindBoilerRoomHaStatus(nullptr);
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
    s.sensors.count = BR_SENSOR_COUNT;
    setSensor(s, BR_SENSOR_T1, SensorState::Ok, 72.4f);
    setSensor(s, BR_SENSOR_T2, SensorState::Ok, 58.0f);
    setSensor(s, BR_SENSOR_T3, SensorState::Ok, 81.3f);
    setSensor(s, BR_SENSOR_T4, SensorState::Ok, 64.0f);
    setSensor(s, BR_SENSOR_T5, SensorState::Ok, 40.5f);
    setSensor(s, BR_SENSOR_T6, SensorState::Ok, 55.0f);
    s.antiSeize.count = 3;
    return s;
}

static BoilerRoomStatus sampleStatus() {
    BoilerRoomStatus st{};
    st.ready = true;
    st.pump[BR_PUMP_P1] = {true, false, PumpReason::Charge};
    st.pump[BR_PUMP_P2] = {false, false, PumpReason::None};
    st.pump[BR_PUMP_P3] = {false, false, PumpReason::SupplyOff};
    st.p3Mode = P3Mode::Off;
    st.noNeedSaved = true;
    st.noNeedEffective = true;
    st.linkUp = true;
    st.offerWaitLeftS = 1234;
    st.afInS = 900;
    st.p3IdleS = 300;
    st.energyQuality = EnergyQuality::Estimated;
    st.energyKWh = 18.6f;
    st.t6Usable = true;
    return st;
}

static size_t buildState(const CommonState& s, void* ctx, char* buf, size_t cap) {
    const WebJsonContext c{"boiler-room", &BOILER_ROOM_HW, nullptr, nullptr, boilerRoomStateJson, ctx};
    return buildStateJson(s, c, buf, cap);
}

static void expectBoolKey(JsonObjectConst o, const char* k, bool v) {
    TEST_ASSERT_TRUE_MESSAGE(o[k].is<bool>(), k);
    TEST_ASSERT_EQUAL_MESSAGE(v, o[k].as<bool>(), k);
}

static void expectUintKey(JsonObjectConst o, const char* k, uint32_t v) {
    TEST_ASSERT_TRUE_MESSAGE(o[k].is<uint32_t>(), k);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(v, o[k].as<uint32_t>(), k);
}

static void expectStrKey(JsonObjectConst o, const char* k, const char* v) {
    TEST_ASSERT_TRUE_MESSAGE(o[k].is<const char*>(), k);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v, o[k].as<const char*>(), k);
}

// ---- JSON ---------------------------------------------------------------------

static void test_ctl_json_full_shape() {
    static char buf[STATE_CAP];
    CommonState s = sampleState();
    s.antiSeize.output[BR_PUMP_P2].running = true;
    BoilerRoomStatus st = sampleStatus();
    st.pump[BR_PUMP_P2] = {true, true, PumpReason::T1T2FaultForced};
    st.dumpActive = true;
    st.offerDisabled = true;

    const size_t n = buildState(s, &st, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT32(strlen(buf), n);

    JsonDocument doc;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DeserializationError::Ok), static_cast<int>(deserializeJson(doc, buf).code()));
    JsonObjectConst ctl = doc["ctl"].as<JsonObjectConst>();
    TEST_ASSERT_FALSE(ctl.isNull());
    expectBoolKey(ctl, "ok", true);

    JsonArrayConst pumps = ctl["pumps"].as<JsonArrayConst>();
    TEST_ASSERT_EQUAL_UINT32(3, pumps.size());
    const char* names[3] = {"P1", "P2", "P3"};
    const bool on[3] = {true, true, false};
    const bool sf[3] = {false, true, false};
    const char* r[3] = {"charge", "t1t2_fault", "off"};
    const bool as[3] = {false, true, false};
    for (size_t i = 0; i < 3; ++i) {
        JsonObjectConst p = pumps[i].as<JsonObjectConst>();
        TEST_ASSERT_EQUAL_UINT32(5, p.size());
        expectStrKey(p, "n", names[i]);
        expectBoolKey(p, "on", on[i]);
        expectBoolKey(p, "sf", sf[i]);
        expectStrKey(p, "r", r[i]);
        expectBoolKey(p, "as", as[i]);
    }

    JsonObjectConst p3 = ctl["p3"].as<JsonObjectConst>();
    TEST_ASSERT_EQUAL_UINT32(11, p3.size());
    expectStrKey(p3, "mode", "off");
    expectBoolKey(p3, "af", false);
    expectBoolKey(p3, "dump", true);
    expectBoolKey(p3, "noOffer", true);
    expectBoolKey(p3, "saved", true);
    expectBoolKey(p3, "flag", true);
    expectBoolKey(p3, "link", true);
    expectUintKey(p3, "winS", 0);
    expectUintKey(p3, "waitS", 1234);
    expectUintKey(p3, "afInS", 900);
    expectUintKey(p3, "idleS", 300);

    JsonObjectConst e = ctl["energy"].as<JsonObjectConst>();
    expectStrKey(e, "q", "est");
    TEST_ASSERT_TRUE(e["kwh"].is<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 18.6f, e["kwh"].as<float>());

    expectBoolKey(ctl, "oh", false);
    expectBoolKey(ctl, "t6", true);
    TEST_ASSERT_EQUAL_UINT32(6, ctl.size());
    // Base members are untouched by the extension.
    TEST_ASSERT_TRUE(doc["relays"].is<JsonArrayConst>());
}

static void test_ctl_json_energy_na_is_null_and_exact_one_decimal() {
    static char buf[STATE_CAP];
    CommonState s = sampleState();
    BoilerRoomStatus st = sampleStatus();
    st.energyQuality = EnergyQuality::Unavailable;
    st.energyKWh = 0.0f;
    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"energy\":{\"q\":\"na\",\"kwh\":null}"));
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf));
    TEST_ASSERT_TRUE(doc["ctl"]["energy"]["kwh"].isNull());

    st.energyQuality = EnergyQuality::Exact;
    st.energyKWh = 16.476f;
    TEST_ASSERT_TRUE(buildState(s, &st, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"energy\":{\"q\":\"exact\",\"kwh\":16.5}"));
}

static void test_ctl_json_null_or_not_ready_is_ok_false() {
    static char buf[STATE_CAP];
    CommonState s = sampleState();
    TEST_ASSERT_TRUE(buildState(s, nullptr, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ctl\":{\"ok\":false}}"));
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf));
    TEST_ASSERT_FALSE(doc["ctl"]["ok"].as<bool>());

    BoilerRoomStatus st = sampleStatus();
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
    // every counter at the maximum, every flag set, 6 Ok sensors at 5-char temps.
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
    s.sensors.count = BR_SENSOR_COUNT;
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
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

    BoilerRoomStatus st{};
    st.ready = true;
    st.pump[BR_PUMP_P1] = {true, true, PumpReason::T1FaultForced};
    st.pump[BR_PUMP_P2] = {true, true, PumpReason::T1T2FaultForced};
    st.pump[BR_PUMP_P3] = {true, true, PumpReason::AntiFreeze};   // "anti_freeze": the longest reason key
    st.p3Mode = P3Mode::Normal;                                   // "normal": the longest mode key
    st.overheat = st.antiFreezeRunning = st.dumpActive = st.offerDisabled = true;
    st.noNeedSaved = st.noNeedEffective = st.linkUp = true;
    st.offerWindowLeftS = st.offerWaitLeftS = st.afInS = st.p3IdleS = UINT32_MAX;
    st.energyQuality = EnergyQuality::Exact;                      // "exact" + a number beats "na"/null
    st.energyKWh = 123456.7f;
    st.t6Usable = true;
    st.alarmMask = BR_ALARM_OWNED_MASK;

    static char buf[STATE_CAP];
    const WebJsonContext c{"boiler-room", &BOILER_ROOM_HW, fmtWorst, nullptr, boilerRoomStateJson, &st};
    const size_t n = buildStateJson(s, c, buf, sizeof(buf));
    char msg[48];
    snprintf(msg, sizeof(msg), "worst-case /api/state = %u bytes", static_cast<unsigned>(n));
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(n > 0 && n < STATE_CAP, msg);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf));
    TEST_ASSERT_TRUE(doc["ctl"]["ok"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("anti_freeze", doc["ctl"]["pumps"][2]["r"].as<const char*>());
}

// ---- Pages --------------------------------------------------------------------

// Every line is printable ASCII, <= 21 chars (Normal) and never cut.
static void expectCleanFrame(const DisplayFrame& f) {
    for (uint8_t i = 0; i < f.lineCount; ++i) {
        const char* t = f.line[i].text;
        TEST_ASSERT_TRUE_MESSAGE(strlen(t) <= BR_PAGE_ROW_CHARS, t);
        // displayFitText marks a cut by ending a full-width line with '~'.
        const size_t len = strlen(t);
        TEST_ASSERT_FALSE_MESSAGE(len == BR_PAGE_ROW_CHARS && t[len - 1] == '~', t);
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

static void test_page_boiler_sample() {
    CommonState s = sampleState();
    s.relays.on[BR_PUMP_P1] = true;
    BoilerRoomStatus st = sampleStatus();
    renderBoilerPage(s, g_frame, &st);
    const char* rows[] = {"T1 flow   72.4", "T2 return 58.0", "P1 ON charge", "P2 OFF -"};
    expectFrame(g_frame, "Boiler", rows, 4);
}

static void test_page_boiler_overheat_safety_and_fault() {
    CommonState s = sampleState();
    setSensor(s, BR_SENSOR_T2, SensorState::Fault, 0.0f);
    s.relays.on[BR_PUMP_P1] = true;
    s.relays.on[BR_PUMP_P2] = false;   // requested but lock-delayed: the ACTUAL relay is shown
    BoilerRoomStatus st = sampleStatus();
    st.overheat = true;
    st.pump[BR_PUMP_P1] = {true, true, PumpReason::Overheat};
    st.pump[BR_PUMP_P2] = {true, false, PumpReason::ReturnBurnGate};
    renderBoilerPage(s, g_frame, &st);
    const char* rows[] = {"T1 flow   72.4", "T2 return --", "P1 ON overheat!", "P2 OFF burn gate", "OVERHEAT"};
    expectFrame(g_frame, "Boiler", rows, 5);
}

static void test_page_accu_energy_variants() {
    CommonState s = sampleState();
    BoilerRoomStatus st = sampleStatus();
    st.energyQuality = EnergyQuality::Exact;
    st.energyKWh = 16.476f;
    renderAccuPage(s, g_frame, &st);
    const char* exact[] = {"T3 top    81.3", "T4 mid    64.0", "T5 bottom 40.5", "E 16.5 kWh"};
    expectFrame(g_frame, "Accumulator", exact, 4);

    setSensor(s, BR_SENSOR_T4, SensorState::Unknown, 0.0f);
    st.energyQuality = EnergyQuality::Estimated;
    st.energyKWh = 18.6f;
    renderAccuPage(s, g_frame, &st);
    const char* est[] = {"T3 top    81.3", "T4 mid    --", "T5 bottom 40.5", "E ~18.6 kWh est"};
    expectFrame(g_frame, "Accumulator", est, 4);

    setSensor(s, BR_SENSOR_T3, SensorState::Unassigned, 0.0f);
    setSensor(s, BR_SENSOR_T5, SensorState::Fault, 0.0f);
    st.energyQuality = EnergyQuality::Unavailable;
    st.energyKWh = 0.0f;
    renderAccuPage(s, g_frame, &st);
    const char* na[] = {"T3 top    --", "T4 mid    --", "T5 bottom --", "E n/a"};
    expectFrame(g_frame, "Accumulator", na, 4);
}

static void test_page_supply_modes_af_and_flag() {
    CommonState s = sampleState();
    BoilerRoomStatus st = sampleStatus();   // Off, wait 1234 s, AF in 900 s, flag saved + link up
    renderSupplyPage(s, g_frame, &st);
    const char* off[] = {"T6 supply 55.0", "P3 OFF off", "Mode OFF 21m", "AF in 15m", "No-need set"};
    expectFrame(g_frame, "Supply P3", off, 5);

    st.p3Mode = P3Mode::Offer;
    st.offerWindowLeftS = 599;
    st.offerWaitLeftS = 0;
    st.pump[BR_PUMP_P3] = {true, false, PumpReason::SupplyOffer};
    st.noNeedSaved = false;
    st.noNeedEffective = false;
    s.relays.on[BR_PUMP_P3] = true;
    renderSupplyPage(s, g_frame, &st);
    const char* offer[] = {"T6 supply 55.0", "P3 ON offer", "Mode OFFER 10m", "AF in 15m", "No-need clear"};
    expectFrame(g_frame, "Supply P3", offer, 5);

    // Anti-freeze run on the safety slot, T6 faulted, flag saved but link down (ignored), no wait.
    setSensor(s, BR_SENSOR_T6, SensorState::Fault, 0.0f);
    st.p3Mode = P3Mode::Normal;
    st.offerWindowLeftS = 0;
    st.antiFreezeRunning = true;
    st.afInS = 0;
    st.pump[BR_PUMP_P3] = {true, true, PumpReason::AntiFreeze};
    st.noNeedSaved = true;
    st.linkUp = false;
    renderSupplyPage(s, g_frame, &st);
    const char* af[] = {"T6 supply --", "P3 ON antifrz!", "Mode NORMAL", "AF RUN", "No-need ign"};
    expectFrame(g_frame, "Supply P3", af, 5);

    // Anti-freeze disabled / relay ON: no countdown.
    st.antiFreezeRunning = false;
    renderSupplyPage(s, g_frame, &st);
    TEST_ASSERT_EQUAL_STRING("AF off", g_frame.line[4].text);
}

static void test_pages_starting_when_null_or_not_ready() {
    CommonState s = sampleState();
    BoilerRoomStatus st = sampleStatus();
    st.ready = false;
    const char* rows[] = {"starting..."};
    renderBoilerPage(s, g_frame, nullptr);
    expectFrame(g_frame, "Boiler", rows, 1);
    renderAccuPage(s, g_frame, &st);
    expectFrame(g_frame, "Accumulator", rows, 1);
    renderSupplyPage(s, g_frame, nullptr);
    expectFrame(g_frame, "Supply P3", rows, 1);
}

// No-clip invariant with worst-case values: every reason with '!' and OFF,
// -55.0 temps, uint32 minutes, a huge (clamped) energy; every row kept whole.
static void test_pages_worst_case_never_clip() {
    CommonState s = sampleState();
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) setSensor(s, i, SensorState::Ok, -55.0f);
    BoilerRoomStatus st = sampleStatus();
    st.overheat = true;
    st.antiFreezeRunning = false;
    st.offerWindowLeftS = st.offerWaitLeftS = st.afInS = UINT32_MAX;
    st.noNeedSaved = true;
    st.linkUp = true;
    for (uint8_t r = 0; r <= static_cast<uint8_t>(PumpReason::OverheatDump); ++r) {
        for (uint8_t p = 0; p < BR_PUMP_COUNT; ++p) st.pump[p] = {true, true, static_cast<PumpReason>(r)};
        TEST_ASSERT_TRUE(strlen(pumpReasonShort(static_cast<PumpReason>(r))) <= 9);
        renderBoilerPage(s, g_frame, &st);
        expectCleanFrame(g_frame);
        TEST_ASSERT_EQUAL_UINT8(6, g_frame.lineCount);
        for (uint8_t m = 0; m <= static_cast<uint8_t>(P3Mode::Offer); ++m) {
            st.p3Mode = static_cast<P3Mode>(m);
            renderSupplyPage(s, g_frame, &st);
            expectCleanFrame(g_frame);
            TEST_ASSERT_EQUAL_UINT8(6, g_frame.lineCount);
        }
    }
    TEST_ASSERT_EQUAL_STRING("Mode OFFER 71582789m", g_frame.line[3].text);
    TEST_ASSERT_EQUAL_STRING("AF in 71582789m", g_frame.line[4].text);
    TEST_ASSERT_EQUAL_STRING("T6 supply -55.0", g_frame.line[1].text);

    const EnergyQuality qs[] = {EnergyQuality::Exact, EnergyQuality::Estimated};
    const float kwh[] = {1.0e9f, INFINITY, NAN, 99999.94f};
    for (EnergyQuality q : qs) {
        for (float e : kwh) {
            st.energyQuality = q;
            st.energyKWh = e;
            renderAccuPage(s, g_frame, &st);
            expectCleanFrame(g_frame);
            TEST_ASSERT_EQUAL_UINT8(5, g_frame.lineCount);
        }
    }
    st.energyKWh = 1.0e9f;
    renderAccuPage(s, g_frame, &st);
    TEST_ASSERT_EQUAL_STRING("E ~99999.9 kWh est", g_frame.line[4].text);
}

static void test_alarm_labels_from_table() {
    const DisplayLabels labels{BOILER_ROOM_ALARMS, BOILER_ROOM_ALARM_COUNT, BOILER_ROOM_SENSORS, 6};
    char out[48];
    alarmLabel(0, labels, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(BOILER_ROOM_ALARMS[0].labelEn, out);
    TEST_ASSERT_EQUAL_STRING("Boiler overheat", out);
    alarmLabel(8, labels, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(BOILER_ROOM_ALARMS[8].labelEn, out);
    TEST_ASSERT_EQUAL_STRING("T1 fault/unassigned", out);
    alarmLabel(24, labels, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Sensor T1 missing", out);
    TEST_ASSERT_EQUAL_UINT32(14, BOILER_ROOM_ALARM_COUNT);
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
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(BOILER_ROOM_SCHEMA, 0)));
        return reg.build(config, BOILER_ROOM_HW, BOILER_ROOM_HA_ENTITIES, BOILER_ROOM_HA_ENTITY_COUNT);
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
    {"energy", HaComponent::Sensor, "kWh", "energy_storage", "measurement", nullptr},
    {"energy_estimated", HaComponent::BinarySensor, nullptr, nullptr, nullptr, "diagnostic"},
    {"p3_mode", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr},
    {"anti_freeze_run", HaComponent::BinarySensor, nullptr, "running", nullptr, nullptr},
    {"alarm_overheat", HaComponent::BinarySensor, nullptr, "heat", nullptr, nullptr},
    {"alarm_t1_failsafe", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"alarm_t1t2_failsafe", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"alarm_t2_failsafe", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"alarm_t3_failsafe", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"alarm_t1_fault", HaComponent::BinarySensor, nullptr, "problem", nullptr, "diagnostic"},
    {"alarm_t2_fault", HaComponent::BinarySensor, nullptr, "problem", nullptr, "diagnostic"},
    {"alarm_t3_fault", HaComponent::BinarySensor, nullptr, "problem", nullptr, "diagnostic"},
    {"alarm_t4_fault", HaComponent::BinarySensor, nullptr, "problem", nullptr, "diagnostic"},
    {"alarm_t5_fault", HaComponent::BinarySensor, nullptr, "problem", nullptr, "diagnostic"},
    {"alarm_t6_fault", HaComponent::BinarySensor, nullptr, "problem", nullptr, "diagnostic"},
    // Stage 09 (C9): pump-response warnings, CommonState.diag.warningMask bits 0..2.
    {"warn_p3_no_flow", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"warn_p1_not_charging", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
    {"warn_p2_no_effect", HaComponent::BinarySensor, nullptr, "problem", nullptr, nullptr},
};
constexpr size_t FIRST_ALARM_ENTITY = 4;
static const uint8_t EXPECTED_ALARM_BIT[] = {0, 1, 2, 3, 4, 8, 9, 10, 11, 12, 13};   // EXPECTED[4..14]
constexpr size_t FIRST_WARN_ENTITY = 15;
static const uint8_t EXPECTED_WARN_BIT[] = {BR_WARN_B1, BR_WARN_B3, BR_WARN_B6};   // EXPECTED[15..17]

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
    TEST_ASSERT_EQUAL_UINT32(18, BOILER_ROOM_HA_ENTITY_COUNT);
    TEST_ASSERT_EQUAL_UINT32(BOILER_ROOM_HA_ENTITY_TOTAL, BOILER_ROOM_HA_ENTITY_COUNT);
    TEST_ASSERT_EQUAL_UINT32(sizeof(EXPECTED) / sizeof(EXPECTED[0]), BOILER_ROOM_HA_ENTITY_COUNT);
    for (size_t a = 0; a < BOILER_ROOM_HA_ENTITY_COUNT; ++a) {   // table order and unique keys
        TEST_ASSERT_EQUAL_STRING(EXPECTED[a].key, BOILER_ROOM_HA_ENTITIES[a].key);
        for (size_t b = a + 1; b < BOILER_ROOM_HA_ENTITY_COUNT; ++b) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(0, strcmp(BOILER_ROOM_HA_ENTITIES[a].key, BOILER_ROOM_HA_ENTITIES[b].key),
                BOILER_ROOM_HA_ENTITIES[a].key);
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
}

static void test_ha_status_entities_unbound_and_bound() {
    HaFixture fx;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(fx.begin()));
    g_ha = &fx;
    CommonState s = sampleState();

    bindBoilerRoomHaStatus(nullptr);   // unbound: unavailable
    expectHaState("energy", s, false, "None");
    expectHaState("energy_estimated", s, false, "None");
    expectHaState("p3_mode", s, false, "None");
    expectHaState("anti_freeze_run", s, false, "None");

    BoilerRoomStatus st = sampleStatus();
    st.ready = false;                  // bound but not ready: unavailable too
    bindBoilerRoomHaStatus(&st);
    expectHaState("energy", s, false, "None");
    expectHaState("p3_mode", s, false, "None");

    st = sampleStatus();
    st.energyQuality = EnergyQuality::Unavailable;
    st.energyKWh = 0.0f;
    expectHaState("energy", s, false, "None");
    expectHaState("energy_estimated", s, false, "None");
    expectHaState("p3_mode", s, true, "off");
    expectHaState("anti_freeze_run", s, true, "OFF");

    st.energyQuality = EnergyQuality::Exact;
    st.energyKWh = 17.4f;
    st.p3Mode = P3Mode::Offer;
    st.antiFreezeRunning = true;
    expectHaState("energy", s, true, "17.4");
    expectHaState("energy_estimated", s, true, "OFF");
    expectHaState("p3_mode", s, true, "offer");
    expectHaState("anti_freeze_run", s, true, "ON");

    st.energyQuality = EnergyQuality::Estimated;
    st.p3Mode = P3Mode::Normal;
    expectHaState("energy", s, true, "17.4");
    expectHaState("energy_estimated", s, true, "ON");
    expectHaState("p3_mode", s, true, "normal");
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

// Stage 09 (C9): the warning entities follow diag.warningMask bits 0..2 only,
// and are always available (like the alarms), bound or not.
static void test_ha_warning_entities_follow_warning_mask() {
    HaFixture fx;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HaRegistryStatus::Ok), static_cast<int>(fx.begin()));
    g_ha = &fx;
    CommonState s = sampleState();
    bindBoilerRoomHaStatus(nullptr);
    TEST_ASSERT_EQUAL_UINT(3, sizeof(EXPECTED_WARN_BIT));
    for (size_t w = 0; w < sizeof(EXPECTED_WARN_BIT); ++w) {
        const char* key = EXPECTED[FIRST_WARN_ENTITY + w].key;
        s.alarms.activeMask = 0xFFFFFFFFu;   // alarms never leak into warnings
        s.diag.warningMask = 0;
        expectHaState(key, s, true, "OFF");
        s.alarms.activeMask = 0;
        s.diag.warningMask = 1u << EXPECTED_WARN_BIT[w];
        expectHaState(key, s, true, "ON");
        s.diag.warningMask = ~(1u << EXPECTED_WARN_BIT[w]);   // every other bit set
        expectHaState(key, s, true, "OFF");
    }
    g_ha = nullptr;
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ctl_json_full_shape);
    RUN_TEST(test_ctl_json_energy_na_is_null_and_exact_one_decimal);
    RUN_TEST(test_ctl_json_null_or_not_ready_is_ok_false);
    RUN_TEST(test_ctl_json_worst_case_fits_state_cap);
    RUN_TEST(test_page_boiler_sample);
    RUN_TEST(test_page_boiler_overheat_safety_and_fault);
    RUN_TEST(test_page_accu_energy_variants);
    RUN_TEST(test_page_supply_modes_af_and_flag);
    RUN_TEST(test_pages_starting_when_null_or_not_ready);
    RUN_TEST(test_pages_worst_case_never_clip);
    RUN_TEST(test_alarm_labels_from_table);
    RUN_TEST(test_ha_registry_builds_with_custom_entities);
    RUN_TEST(test_ha_status_entities_unbound_and_bound);
    RUN_TEST(test_ha_alarm_entities_follow_mask_bits);
    RUN_TEST(test_ha_warning_entities_follow_warning_mask);
    return UNITY_END();
}
