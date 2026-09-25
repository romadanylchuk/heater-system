#pragma once
#include <unity.h>
#include <ArduinoJson.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "../lib/CoreEngine/src/BackupCodec.h"
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/CoreEngine/src/EventLog.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/WebEngine/src/BackupPrecheck.h"
#include "../lib/WebEngine/src/CommandResultBoard.h"
#include "../lib/WebEngine/src/SchemaJsonStream.h"
#include "../lib/WebEngine/src/WebInput.h"
#include "../lib/WebEngine/src/WebJson.h"
#include "fakes/FakeClock.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/TestSchema.h"

// Native-safe Unity tests for stage 05 phase 2: the pure WebEngine request
// parsers (WebInput), the backup precheck, the JSON builders (WebJson) and
// the resumable schema stream. Header-only, run via WebSuite.h. Test names
// are prefixed web_api_.

namespace {

size_t webApiTotalSettings(const ConfigSchema& s) {
    size_t n = 0;
    for (size_t t = 0; t < s.tableCount; ++t) n += s.tables[t].count;
    return n;
}

InputError webApiParse(const char* key, const char* value, ParsedSetting& out) {
    return parseSettingInput(TEST_SCHEMA_A_V1, key, value, strlen(value), out);
}

bool webApiFakeFmt(uint32_t utc, char* out, size_t cap, void* ctx) {
    (void)ctx;
    snprintf(out, cap, "L%u", static_cast<unsigned>(utc));
    return true;
}

bool webApiCtlExt(const CommonState& s, JsonOut& out, void* ctx) {
    (void)s;
    (void)ctx;
    out.beginObject();
    out.key("x");
    out.integer(7);
    out.endObject();
    return true;
}

constexpr RelayChannelDesc WEB_API_RELAYS[] = {
    {2, "P1", RelayRole::Pump, true, true},
    {0, "K2", RelayRole::Diverter, true, true},
};
constexpr LogicalSensorDesc WEB_API_SENSORS[] = {
    {"T1", "sensorT1"},
    {"T2", "sensorT2"},
    {"T3", "sensorT3"},
};
constexpr HwProjectConfig WEB_API_HW = {
    WEB_API_RELAYS, 2, WEB_API_SENSORS, 3, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL},
};

// One long-label table that cannot fit the 512-B schema scratch.
constexpr const char* WEB_API_LONG_LABEL =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
constexpr SettingDescriptor WEB_API_LONG_ITEMS[] = {
    intSetting("longOne", "longOne", WEB_API_LONG_LABEL, "u", nullptr, "test", 0, 1, 0),
};
constexpr SettingsTable WEB_API_LONG_TABLES[] = {makeTable(WEB_API_LONG_ITEMS)};
constexpr ConfigSchema WEB_API_LONG_SCHEMA = {"long", 1, WEB_API_LONG_TABLES, 1, nullptr, 0};

// Reads the whole stream with a fixed chunk cap.
std::string webApiDrainSchema(const ConfigSchema& schema, size_t cap, bool& failed) {
    SchemaJsonStream stream(schema, "test-a");
    std::string all;
    char buf[4096];
    for (int guard = 0; guard < 200000; ++guard) {
        size_t n = stream.read(buf, cap);
        if (n == 0) break;
        TEST_ASSERT_TRUE(n <= cap);
        all.append(buf, n);
    }
    failed = stream.failed();
    return all;
}

}  // namespace

// ---- WebInput: settings --------------------------------------------------

static void web_api_test_setting_index_matches_config_engine() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    size_t checked = 0;
    for (size_t t = 0; t < TEST_SCHEMA_A_V1.tableCount; ++t) {
        const SettingsTable& table = TEST_SCHEMA_A_V1.tables[t];
        for (size_t i = 0; i < table.count; ++i) {
            const SettingDescriptor& d = table.items[i];
            const char* value = d.type == SettingType::Text ? "ab" : "1";
            ParsedSetting p{};
            InputError e = parseSettingInput(TEST_SCHEMA_A_V1, d.key, value, strlen(value), p);
            if (strcmp(d.key, "wifiSsid") == 0 || strcmp(d.key, "wifiPass") == 0) {
                TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::WifiKey), static_cast<int>(e));
                continue;
            }
            TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(InputError::Ok), static_cast<int>(e), d.key);
            TEST_ASSERT_EQUAL_INT_MESSAGE(engine.indexOf(d.key), static_cast<int>(p.index), d.key);
            TEST_ASSERT_EQUAL_INT(static_cast<int>(d.type), static_cast<int>(p.type));
            ++checked;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(webApiTotalSettings(TEST_SCHEMA_A_V1) - 2, checked);
}

static void web_api_test_setting_unknown_and_wifi_keys() {
    ParsedSetting p{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::UnknownKey), static_cast<int>(webApiParse("nope", "1", p)));
    // exact match only: no prefix/case tolerance
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::UnknownKey), static_cast<int>(webApiParse("testIn", "1", p)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::UnknownKey), static_cast<int>(webApiParse("TESTINT", "1", p)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::WifiKey), static_cast<int>(webApiParse("wifiSsid", "x", p)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::WifiKey), static_cast<int>(webApiParse("wifiPass", "x", p)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Missing),
        static_cast<int>(parseSettingInput(TEST_SCHEMA_A_V1, nullptr, "1", 1, p)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Missing),
        static_cast<int>(parseSettingInput(TEST_SCHEMA_A_V1, "testInt", nullptr, 0, p)));
}

static void web_api_test_setting_int_parse() {
    ParsedSetting p{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(webApiParse("testInt", "12", p)));
    TEST_ASSERT_EQUAL_FLOAT(12.0f, p.number);
    TEST_ASSERT_EQUAL_UINT16(TEST_INDEX_INT, p.index);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(webApiParse("testInt", "-3", p)));
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, p.number);
    // out-of-range values still parse; clamping belongs to ConfigEngine
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(webApiParse("testInt", "+999", p)));
    TEST_ASSERT_EQUAL_FLOAT(999.0f, p.number);
    const char* bad[] = {"1.5", "x", "", "-", "1e3", " 1", "1 ", "99999999999"};
    for (const char* b : bad) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(InputError::BadNumber),
            static_cast<int>(webApiParse("testInt", b, p)), b);
    }
    // an embedded NUL inside an int value is not silently truncated
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::BadNumber),
        static_cast<int>(parseSettingInput(TEST_SCHEMA_A_V1, "testInt", "1\0" "2", 3, p)));
}

// Length cap (final-check Should 3): at most 16 characters, sign included,
// refused before scanning whatever length the caller passes.
static void web_api_test_setting_int_length_cap() {
    ParsedSetting p{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok),
        static_cast<int>(webApiParse("testInt", "0000000000000012", p)));   // 16 chars
    TEST_ASSERT_EQUAL_FLOAT(12.0f, p.number);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok),
        static_cast<int>(webApiParse("testInt", "-000000000000012", p)));   // 16 chars with sign
    TEST_ASSERT_EQUAL_FLOAT(-12.0f, p.number);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::BadNumber),
        static_cast<int>(webApiParse("testInt", "00000000000000012", p)));  // 17 chars
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::BadNumber),
        static_cast<int>(webApiParse("testInt", "+0000000000000012", p)));  // 17 chars with sign
    static char longZeros[1025];
    memset(longZeros, '0', 1023);
    longZeros[1023] = '1';
    longZeros[1024] = '\0';
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::BadNumber),
        static_cast<int>(parseSettingInput(TEST_SCHEMA_A_V1, "testInt", longZeros, 1024, p)));
}

static void web_api_test_setting_float_parse() {
    ParsedSetting p{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(webApiParse("testFloat", "2.5", p)));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, p.number);
    TEST_ASSERT_EQUAL_UINT16(TEST_INDEX_FLOAT, p.index);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(webApiParse("testFloat", "-7", p)));
    TEST_ASSERT_EQUAL_FLOAT(-7.0f, p.number);
    const char* bad[] = {"nan", "NaN", "inf", "-inf", "", "2.5x", "x", " 2.5", "1e99"};
    for (const char* b : bad) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(InputError::BadNumber),
            static_cast<int>(webApiParse("testFloat", b, p)), b);
    }
}

static void web_api_test_setting_bool_parse() {
    ParsedSetting p{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(webApiParse("testBool", "1", p)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, p.number);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingType::Bool), static_cast<int>(p.type));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(webApiParse("testBool", "true", p)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, p.number);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(webApiParse("testBool", "0", p)));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.number);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(webApiParse("testBool", "false", p)));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.number);
    const char* bad[] = {"", "2", "yes", "TRUE", "truex", "on"};
    for (const char* b : bad) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(InputError::BadBool),
            static_cast<int>(webApiParse("testBool", b, p)), b);
    }
}

static void web_api_test_setting_text_parse() {
    ParsedSetting p{};
    const char* value = "hello";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok),
        static_cast<int>(parseSettingInput(TEST_SCHEMA_A_V1, "oldTemp", value, 5, p)));
    TEST_ASSERT_EQUAL_PTR(value, p.text);   // no copy
    TEST_ASSERT_EQUAL_UINT32(5, p.textLen);
    TEST_ASSERT_EQUAL_UINT16(TEST_INDEX_TEXT, p.index);

    // maxLen 16: exactly 16 is fine, 17 is too long
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok),
        static_cast<int>(webApiParse("oldTemp", "0123456789abcdef", p)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::TooLong),
        static_cast<int>(webApiParse("oldTemp", "0123456789abcdefg", p)));

    // valueLen (String::length()) longer than strlen -> embedded NUL
    const char withNul[] = {'a', 'b', '\0', 'c', 'd', '\0'};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::EmbeddedNul),
        static_cast<int>(parseSettingInput(TEST_SCHEMA_A_V1, "oldTemp", withNul, 5, p)));

    // empty text is accepted by the parser (min length is ConfigEngine's job)
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(webApiParse("testSecret", "", p)));
    TEST_ASSERT_EQUAL_UINT32(0, p.textLen);
}

// ---- WebInput: ROM / index / u32 ------------------------------------------

static void web_api_test_rom_address_parse() {
    const uint8_t expected[8] = {0x28, 0xFF, 0x0A, 0x1B, 0x2C, 0x3D, 0x4E, 0x5F};
    uint8_t out[8] = {};
    const char* good[] = {"28ff0a1b2c3d4e5f", "28FF0A1B2C3D4E5F", "28:ff:0a:1b:2c:3d:4e:5f", "28-FF-0A-1B-2C-3D-4E-5F"};
    for (const char* g : good) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(InputError::Ok),
            static_cast<int>(parseRomAddress(g, strlen(g), out)), g);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 8);
    }
    const uint8_t zero[8] = {};
    const char* bad[] = {"28ff0a1b2c3d4e5", "28ff0a1b2c3d4e5f00", "28ff0a1b2c3d4e5g", "2:8ff0a1b2c3d4e5f",
        ":28ff0a1b2c3d4e5f", "28ff0a1b2c3d4e5f:", "28 ff0a1b2c3d4e5f"};
    for (const char* b : bad) {
        memset(out, 0xAA, sizeof(out));
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(InputError::BadAddress),
            static_cast<int>(parseRomAddress(b, strlen(b), out)), b);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, out, 8);
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Missing), static_cast<int>(parseRomAddress("", 0, out)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Missing), static_cast<int>(parseRomAddress(nullptr, 0, out)));
}

static void web_api_test_logical_index_and_u32() {
    uint8_t idx = 0xFF;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(parseLogicalIndex("0", 4, idx)));
    TEST_ASSERT_EQUAL_UINT8(0, idx);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Ok), static_cast<int>(parseLogicalIndex("3", 4, idx)));
    TEST_ASSERT_EQUAL_UINT8(3, idx);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::BadIndex), static_cast<int>(parseLogicalIndex("4", 4, idx)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::BadIndex), static_cast<int>(parseLogicalIndex("-1", 4, idx)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::BadIndex), static_cast<int>(parseLogicalIndex("x", 4, idx)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::BadIndex), static_cast<int>(parseLogicalIndex("0", 0, idx)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Missing), static_cast<int>(parseLogicalIndex("", 4, idx)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InputError::Missing), static_cast<int>(parseLogicalIndex(nullptr, 4, idx)));

    uint32_t v = 0;
    TEST_ASSERT_TRUE(parseU32("123", v));
    TEST_ASSERT_EQUAL_UINT32(123, v);
    TEST_ASSERT_TRUE(parseU32("4294967295", v));
    TEST_ASSERT_EQUAL_UINT32(4294967295u, v);
    TEST_ASSERT_FALSE(parseU32("4294967296", v));
    TEST_ASSERT_FALSE(parseU32("", v));
    TEST_ASSERT_FALSE(parseU32("12a", v));
    TEST_ASSERT_FALSE(parseU32("-1", v));
    TEST_ASSERT_FALSE(parseU32(nullptr, v));
    // Length cap: at most 16 characters, checked before scanning.
    TEST_ASSERT_TRUE(parseU32("0000000000000007", v));   // 16 chars
    TEST_ASSERT_EQUAL_UINT32(7, v);
    TEST_ASSERT_FALSE(parseU32("00000000000000007", v));  // 17 chars
    static char longZeros[1025];
    memset(longZeros, '0', 1024);
    longZeros[1024] = '\0';
    TEST_ASSERT_FALSE(parseU32(longZeros, v));
}

static void web_api_test_input_error_keys() {
    TEST_ASSERT_EQUAL_STRING("ok", inputErrorKey(InputError::Ok));
    TEST_ASSERT_EQUAL_STRING("unknown_key", inputErrorKey(InputError::UnknownKey));
    TEST_ASSERT_EQUAL_STRING("wifi_key", inputErrorKey(InputError::WifiKey));
    TEST_ASSERT_EQUAL_STRING("bad_number", inputErrorKey(InputError::BadNumber));
    TEST_ASSERT_EQUAL_STRING("bad_bool", inputErrorKey(InputError::BadBool));
    TEST_ASSERT_EQUAL_STRING("too_long", inputErrorKey(InputError::TooLong));
    TEST_ASSERT_EQUAL_STRING("embedded_nul", inputErrorKey(InputError::EmbeddedNul));
    TEST_ASSERT_EQUAL_STRING("bad_index", inputErrorKey(InputError::BadIndex));
    TEST_ASSERT_EQUAL_STRING("bad_address", inputErrorKey(InputError::BadAddress));
    TEST_ASSERT_EQUAL_STRING("missing", inputErrorKey(InputError::Missing));
}

// ---- BackupPrecheck --------------------------------------------------------

static void web_api_test_precheck_simple_results() {
    char got[16] = "zz";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PrecheckResult::Empty),
        static_cast<int>(precheckBackup("", 0, "test-a", got, sizeof(got))));
    TEST_ASSERT_EQUAL_STRING("", got);

    static char big[BACKUP_MAX_BYTES + 1];
    memset(big, ' ', sizeof(big));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PrecheckResult::TooLarge),
        static_cast<int>(precheckBackup(big, sizeof(big), "test-a", got, sizeof(got))));

    const char* corrupt = "{\"type\":\"test-a\",";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PrecheckResult::Corrupt),
        static_cast<int>(precheckBackup(corrupt, strlen(corrupt), "test-a", got, sizeof(got))));

    const char* missing[] = {"[1,2]", "{\"format\":1}", "{\"type\":5,\"format\":1}", "\"test-a\""};
    for (const char* m : missing) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(PrecheckResult::MissingType),
            static_cast<int>(precheckBackup(m, strlen(m), "test-a", got, sizeof(got))), m);
    }

    const char* wrong = "{\"type\":\"boiler-room-with-a-long-name\",\"format\":1}";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PrecheckResult::WrongType),
        static_cast<int>(precheckBackup(wrong, strlen(wrong), "test-a", got, sizeof(got))));
    TEST_ASSERT_EQUAL_STRING("boiler-room-wit", got);   // truncated to cap - 1

    const char* fmt2 = "{\"type\":\"test-a\",\"format\":2,\"settings\":{}}";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PrecheckResult::UnsupportedFormat),
        static_cast<int>(precheckBackup(fmt2, strlen(fmt2), "test-a", got, sizeof(got))));
    const char* noFmt = "{\"type\":\"test-a\"}";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PrecheckResult::UnsupportedFormat),
        static_cast<int>(precheckBackup(noFmt, strlen(noFmt), "test-a", got, sizeof(got))));

    TEST_ASSERT_EQUAL_STRING("ok", precheckKey(PrecheckResult::Ok));
    TEST_ASSERT_EQUAL_STRING("empty", precheckKey(PrecheckResult::Empty));
    TEST_ASSERT_EQUAL_STRING("too_large", precheckKey(PrecheckResult::TooLarge));
    TEST_ASSERT_EQUAL_STRING("corrupt", precheckKey(PrecheckResult::Corrupt));
    TEST_ASSERT_EQUAL_STRING("missing_type", precheckKey(PrecheckResult::MissingType));
    TEST_ASSERT_EQUAL_STRING("wrong_type", precheckKey(PrecheckResult::WrongType));
    TEST_ASSERT_EQUAL_STRING("unsupported_format", precheckKey(PrecheckResult::UnsupportedFormat));
}

static void web_api_test_precheck_real_export() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    static char buf[BACKUP_MAX_BYTES];
    size_t len = BackupCodec::exportJson(engine, "1.2.3", buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    char got[24] = {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PrecheckResult::Ok),
        static_cast<int>(precheckBackup(buf, len, "test-a", got, sizeof(got))));
    TEST_ASSERT_EQUAL_STRING("test-a", got);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PrecheckResult::WrongType),
        static_cast<int>(precheckBackup(buf, len, "test-b", got, sizeof(got))));
    TEST_ASSERT_EQUAL_STRING("test-a", got);
}

// ---- WebJson ------------------------------------------------------------------

static void web_api_test_state_json() {
    static CommonState st{};
    st = CommonState{};
    st.system.uptimeS = 1234;
    st.time.valid = true;
    st.time.utcNow = 1700000000u;
    st.time.source = TimeSourceKind::Ntp;
    st.time.rtcPresent = true;
    st.time.rtcValid = false;
    st.time.lastNtpSyncUtc = 0;
    st.network.wifiConnected = true;
    st.network.wifiRssi = -61;
    strcpy(st.network.ip, "192.168.1.5");
    strcpy(st.network.ssid, "home\"net");
    st.alarms.activeMask = 0x01000002u;
    st.diag.warningMask = 4;
    st.diag.tzInvalid = true;
    st.relays.on[2] = true;
    st.relays.channel[2].requested = true;
    st.relays.channel[0].requested = true;
    st.relays.channel[0].lockDelayed = true;
    st.relays.channel[0].lockRemainingS = 17;
    st.sensors.count = 3;
    st.sensors.sensor[0].state = SensorState::Fault;
    st.sensors.sensor[0].tempC = 55.0f;   // stale value must not leak while faulted
    st.sensors.sensor[1].state = SensorState::Ok;
    st.sensors.sensor[1].tempC = NAN;
    st.sensors.sensor[2].state = SensorState::Ok;
    st.sensors.sensor[2].tempC = 21.5f;
    st.sensors.sensor[2].missing = true;
    st.ota.source = OtaSource::Espota;
    strcpy(st.versions.fw, "1.0.0");
    st.system.resetPhase = ResetGatePhase::Countdown;
    st.system.resetSecondsLeft = 9;
    st.system.lastCommandId = 77;
    st.system.lastCommandStatus = static_cast<uint8_t>(CommandStatus::Clamped);

    WebJsonContext ctx{"boiler-room", &WEB_API_HW, webApiFakeFmt, nullptr, nullptr, nullptr};
    static char buf[3072];
    size_t len = buildStateJson(st, ctx, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_UINT32(strlen(buf), len);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));
    TEST_ASSERT_EQUAL_STRING("boiler-room", doc["project"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(1234, doc["uptimeS"].as<uint32_t>());
    TEST_ASSERT_TRUE(doc["time"]["valid"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("L1700000000", doc["time"]["local"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("ntp", doc["time"]["source"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("invalid", doc["time"]["rtc"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("", doc["time"]["lastNtp"].as<const char*>());
    TEST_ASSERT_TRUE(doc["net"]["wifi"].as<bool>());
    TEST_ASSERT_EQUAL_INT(-61, doc["net"]["rssi"].as<int>());
    TEST_ASSERT_EQUAL_STRING("home\"net", doc["net"]["ssid"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(0x01000002u, doc["alarms"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(4, doc["warnings"].as<uint32_t>());
    TEST_ASSERT_TRUE(doc["diag"]["tzInvalid"].as<bool>());
    TEST_ASSERT_FALSE(doc["diag"]["nvs"].as<bool>());

    // relays follow the hw descriptor order, not the channel order
    TEST_ASSERT_EQUAL_UINT32(2, doc["relays"].size());
    TEST_ASSERT_EQUAL_STRING("P1", doc["relays"][0]["name"].as<const char*>());
    TEST_ASSERT_TRUE(doc["relays"][0]["on"].as<bool>());
    TEST_ASSERT_TRUE(doc["relays"][0]["req"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("K2", doc["relays"][1]["name"].as<const char*>());
    TEST_ASSERT_FALSE(doc["relays"][1]["on"].as<bool>());
    TEST_ASSERT_EQUAL_UINT32(17, doc["relays"][1]["lockS"].as<uint32_t>());

    TEST_ASSERT_EQUAL_UINT32(3, doc["sensors"].size());
    TEST_ASSERT_EQUAL_STRING("T1", doc["sensors"][0]["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("fault", doc["sensors"][0]["state"].as<const char*>());
    TEST_ASSERT_TRUE(doc["sensors"][0]["t"].isNull());
    TEST_ASSERT_EQUAL_STRING("ok", doc["sensors"][1]["state"].as<const char*>());
    TEST_ASSERT_TRUE(doc["sensors"][1]["t"].isNull());   // NaN -> null
    TEST_ASSERT_EQUAL_FLOAT(21.5f, doc["sensors"][2]["t"].as<float>());
    TEST_ASSERT_TRUE(doc["sensors"][2]["missing"].as<bool>());

    TEST_ASSERT_EQUAL_STRING("espota", doc["ota"]["source"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("1.0.0", doc["ver"]["fw"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("countdown", doc["reset"]["phase"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(9, doc["reset"]["left"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(77, doc["lastCmd"]["id"].as<uint32_t>());
    TEST_ASSERT_EQUAL_STRING("clamped", doc["lastCmd"]["status"].as<const char*>());
    TEST_ASSERT_FALSE(doc["ctl"].is<JsonVariantConst>() && !doc["ctl"].isNull());
    TEST_ASSERT_NULL(strstr(buf, "\"ctl\""));
    TEST_ASSERT_NULL(strstr(buf, "55"));   // faulted sensor's stale value is not exposed

    // time invalid and no formatter -> "" texts; extension writes "ctl"
    st.time.valid = false;
    st.time.lastNtpSyncUtc = 1600000000u;
    WebJsonContext ctx2{"boiler-room", &WEB_API_HW, nullptr, nullptr, webApiCtlExt, nullptr};
    len = buildStateJson(st, ctx2, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    JsonDocument doc2;
    TEST_ASSERT_FALSE(deserializeJson(doc2, buf, len));
    TEST_ASSERT_EQUAL_STRING("", doc2["time"]["local"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("", doc2["time"]["lastNtp"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(7, doc2["ctl"]["x"].as<int>());
}

static void web_api_test_sensors_json() {
    static CommonState st{};
    st = CommonState{};
    st.sensors.count = 3;
    const uint8_t addrA[8] = {0x28, 0xAB, 0x00, 0x01, 0x02, 0x03, 0x04, 0xCD};
    st.sensors.sensor[0].assigned = true;
    st.sensors.sensor[0].state = SensorState::Ok;
    st.sensors.sensor[0].tempC = 40.25f;
    memcpy(st.sensors.sensor[0].address, addrA, 8);
    st.sensors.sensor[1].assigned = true;
    st.sensors.sensor[1].state = SensorState::Unknown;
    st.sensors.sensor[1].tempC = NAN;
    st.sensors.sensor[1].missing = true;
    st.sensors.sensor[1].address[0] = 0x28;
    st.sensors.sensor[2].state = SensorState::Unassigned;

    st.oneWire.count = 2;
    memcpy(st.oneWire.address[0], addrA, 8);
    st.oneWire.logical[0] = 0;
    st.oneWire.tempC[0] = 40.25f;
    st.oneWire.tempValid[0] = true;
    const uint8_t addrB[8] = {0x28, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    memcpy(st.oneWire.address[1], addrB, 8);
    st.oneWire.logical[1] = NO_LOGICAL_SENSOR;
    st.oneWire.tempValid[1] = false;
    st.oneWire.done = true;
    st.oneWire.scanCount = 5;

    static char buf[2048];
    size_t len = buildSensorsJson(st, WEB_API_HW, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));

    TEST_ASSERT_EQUAL_UINT32(3, doc["logical"].size());
    TEST_ASSERT_EQUAL_INT(0, doc["logical"][0]["i"].as<int>());
    TEST_ASSERT_EQUAL_STRING("T1", doc["logical"][0]["name"].as<const char*>());
    TEST_ASSERT_TRUE(doc["logical"][0]["assigned"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("28ab0001020304cd", doc["logical"][0]["addr"].as<const char*>());
    TEST_ASSERT_EQUAL_FLOAT(40.25f, doc["logical"][0]["t"].as<float>());
    TEST_ASSERT_EQUAL_STRING("unknown", doc["logical"][1]["state"].as<const char*>());
    TEST_ASSERT_TRUE(doc["logical"][1]["t"].isNull());
    TEST_ASSERT_TRUE(doc["logical"][1]["missing"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("unassigned", doc["logical"][2]["state"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("", doc["logical"][2]["addr"].as<const char*>());

    TEST_ASSERT_EQUAL_UINT32(2, doc["bus"].size());
    TEST_ASSERT_EQUAL_STRING("28ab0001020304cd", doc["bus"][0]["addr"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(0, doc["bus"][0]["logical"].as<int>());
    TEST_ASSERT_FALSE(doc["bus"][0]["new"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("2811223344556677", doc["bus"][1]["addr"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(-1, doc["bus"][1]["logical"].as<int>());
    TEST_ASSERT_TRUE(doc["bus"][1]["new"].as<bool>());
    TEST_ASSERT_TRUE(doc["bus"][1]["t"].isNull());
    TEST_ASSERT_TRUE(doc["scanDone"].as<bool>());
    TEST_ASSERT_FALSE(doc["overflow"].as<bool>());
    TEST_ASSERT_EQUAL_UINT32(5, doc["scanCount"].as<uint32_t>());

    // fewer logical sensors in state than in hw -> min() of the two
    st.sensors.count = 1;
    len = buildSensorsJson(st, WEB_API_HW, buf, sizeof(buf));
    JsonDocument doc2;
    TEST_ASSERT_FALSE(deserializeJson(doc2, buf, len));
    TEST_ASSERT_EQUAL_UINT32(1, doc2["logical"].size());
}

static void web_api_test_config_json_masks_secret() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));
    engine.setText(TEST_INDEX_SECRET, "Sup3rSecretValue", EventReason::Web, 0);
    engine.setNumber(TEST_INDEX_FLOAT, 12.5f, EventReason::Web, 0);
    engine.setNumber(TEST_INDEX_BOOL, 1.0f, EventReason::Web, 0);

    static char buf[4096];
    size_t len = buildConfigValuesJson(engine, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NULL(strstr(buf, "Sup3rSecretValue"));

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));
    JsonObject v = doc["v"];
    JsonObject s = doc["s"];
    TEST_ASSERT_TRUE(v["testInt"].is<int>());
    TEST_ASSERT_EQUAL_INT(50, v["testInt"].as<int>());
    TEST_ASSERT_EQUAL_FLOAT(12.5f, v["testFloat"].as<float>());
    TEST_ASSERT_TRUE(v["testBool"].is<bool>());
    TEST_ASSERT_TRUE(v["testBool"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("hi", v["oldTemp"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("admin", v["webUser"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(1883, v["mqttPort"].as<int>());
    TEST_ASSERT_TRUE(v["testNoBackup"].is<int>());
    // secrets appear only in "s"
    TEST_ASSERT_FALSE(v["testSecret"].is<const char*>());
    TEST_ASSERT_FALSE(v["wifiPass"].is<const char*>());
    TEST_ASSERT_FALSE(v["mqttPass"].is<const char*>());
    TEST_ASSERT_FALSE(v["webPass"].is<const char*>());
    TEST_ASSERT_TRUE(s["testSecret"].as<bool>());
    TEST_ASSERT_TRUE(s["webPass"].as<bool>());      // default "admin"
    TEST_ASSERT_FALSE(s["mqttPass"].as<bool>());
    TEST_ASSERT_FALSE(s["wifiPass"].as<bool>());
    TEST_ASSERT_TRUE(s["mqttPass"].is<bool>());
    TEST_ASSERT_EQUAL_UINT32(4, s.size());
    TEST_ASSERT_EQUAL_UINT32(engine.count() - 4, v.size());
}

static void web_api_test_log_json_newest_first() {
    MemoryKvStore logStore;
    FakeClock clock;
    clock.utc = 42;   // uptime seconds, not real time
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    TEST_ASSERT_TRUE(log.logEvent(static_cast<uint16_t>(EventType::Reboot), EVENT_SOURCE_SYSTEM, 3.0f, 0.0f,
        EventReason::Boot));
    clock.realTime = true;
    clock.utc = 1700000000u;
    TEST_ASSERT_TRUE(log.logEvent(EVENT_TYPE_PROJECT_BASE + 5, EVENT_SOURCE_PROJECT_BASE, 1.5f, -2.25f,
        EventReason::Web));

    WebJsonContext ctx{"test", nullptr, webApiFakeFmt, nullptr, nullptr, nullptr};
    static char buf[6144];
    size_t len = buildLogJson(log, ctx, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));
    JsonArray ev = doc["events"];
    TEST_ASSERT_EQUAL_UINT32(2, ev.size());

    TEST_ASSERT_EQUAL_UINT32(2, ev[0]["seq"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(EVENT_TYPE_PROJECT_BASE + 5, ev[0]["type"].as<uint32_t>());
    TEST_ASSERT_TRUE(ev[0]["key"].isNull());
    TEST_ASSERT_EQUAL_UINT32(EVENT_SOURCE_PROJECT_BASE, ev[0]["src"].as<uint32_t>());
    TEST_ASSERT_EQUAL_FLOAT(1.5f, ev[0]["val"].as<float>());
    TEST_ASSERT_EQUAL_FLOAT(-2.25f, ev[0]["aux"].as<float>());
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint8_t>(EventReason::Web), ev[0]["rsn"].as<uint32_t>());
    TEST_ASSERT_TRUE(ev[0]["rt"].as<bool>());
    TEST_ASSERT_EQUAL_UINT32(1700000000u, ev[0]["ts"].as<uint32_t>());
    TEST_ASSERT_EQUAL_STRING("L1700000000", ev[0]["lt"].as<const char*>());

    TEST_ASSERT_EQUAL_UINT32(1, ev[1]["seq"].as<uint32_t>());
    TEST_ASSERT_EQUAL_STRING("reboot", ev[1]["key"].as<const char*>());
    TEST_ASSERT_FALSE(ev[1]["rt"].as<bool>());
    TEST_ASSERT_EQUAL_UINT32(42, ev[1]["ts"].as<uint32_t>());
    TEST_ASSERT_EQUAL_STRING("", ev[1]["lt"].as<const char*>());

    // empty log
    MemoryKvStore emptyStore;
    EventLog empty(emptyStore, clock);
    TEST_ASSERT_TRUE(empty.begin());
    len = buildLogJson(empty, ctx, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"events\":[]}", buf);
}

// Max-width "lt": the formatter fills its whole 31-char budget.
static bool webApiWideFmt(uint32_t, char* out, size_t cap, void*) {
    if (cap < 32) return false;
    memset(out, '9', 31);
    out[31] = '\0';
    return true;
}

// Seeds `store` with a full ring of sealed, max-width entries (10-digit seq and
// ts, the longest common key, 5-digit src, 3-digit rsn, real time) whose
// value/aux are `v`, so EventLog::begin() loads all CAPACITY of them.
static void webApiSeedWideLog(MemoryKvStore& store, float v) {
    for (size_t i = 0; i < EventLog::CAPACITY; ++i) {
        EventEntry e{};
        e.seq = 4294967295u - static_cast<uint32_t>(i);
        e.timestamp = 4294967295u;
        e.type = static_cast<uint16_t>(EventType::BackupNewerVersion);
        e.source = 65535;
        e.value = v;
        e.aux = v;
        e.reason = 255;
        e.flags = EVENT_FLAG_REAL_TIME;
        sealEventEntry(e);
        char key[6];
        EventLog::slotKey(e.seq % EventLog::CAPACITY, key);
        TEST_ASSERT_TRUE(store.setBlob(key, &e, sizeof(e)) == StoreStatus::Ok);
    }
}

static void web_api_test_log_json_full_max_width_fits() {
    TEST_ASSERT_TRUE(strlen(eventTypeKey(static_cast<uint16_t>(EventType::BackupNewerVersion))) <= WEB_LOG_KEY_MAX);
    for (uint16_t t = 0; t < EVENT_TYPE_PROJECT_BASE; ++t) {
        const char* k = eventTypeKey(t);
        TEST_ASSERT_TRUE(k == nullptr || strlen(k) <= WEB_LOG_KEY_MAX);
    }
    MemoryKvStore store;
    webApiSeedWideLog(store, -999999.99f);
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());
    TEST_ASSERT_EQUAL_UINT32(EventLog::CAPACITY, log.count());

    WebJsonContext ctx{"test", nullptr, webApiWideFmt, nullptr, nullptr, nullptr};
    constexpr size_t CAP = webLogCapFor(EventLog::CAPACITY);   // == WEB_LOG_CAP (static_assert in WebServices.h)
    static char buf[CAP];
    size_t len = buildLogJson(log, ctx, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(len < CAP);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));
    TEST_ASSERT_EQUAL_UINT32(EventLog::CAPACITY, doc["events"].as<JsonArray>().size());
    TEST_ASSERT_TRUE(doc["truncated"].isNull());   // everything fit
    TEST_ASSERT_EQUAL_STRING("backup_newer_version", doc["events"][0]["key"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(31, strlen(doc["events"][0]["lt"].as<const char*>()));
    // Each rendered entry (plus its comma) stays within the documented bound.
    TEST_ASSERT_TRUE(len <= EventLog::CAPACITY * WEB_LOG_ENTRY_MAX + WEB_LOG_FRAME);
    TEST_ASSERT_TRUE((len - strlen("{\"events\":[]}")) / EventLog::CAPACITY <= WEB_LOG_ENTRY_MAX);
}

static void web_api_test_log_json_truncates_to_valid_json() {
    MemoryKvStore store;
    webApiSeedWideLog(store, -999999.99f);
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());
    WebJsonContext ctx{"test", nullptr, webApiWideFmt, nullptr, nullptr, nullptr};

    // Too small for all 50: the newest ones are kept, the oldest dropped.
    static char buf[webLogCapFor(10)];
    size_t len = buildLogJson(log, ctx, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(len < sizeof(buf));
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));
    TEST_ASSERT_TRUE(doc["truncated"].as<bool>());
    JsonArray ev = doc["events"];
    TEST_ASSERT_TRUE(ev.size() >= 1 && ev.size() < EventLog::CAPACITY);
    for (size_t i = 0; i < ev.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32(4294967295u - static_cast<uint32_t>(i), ev[i]["seq"].as<uint32_t>());
    }

    // Room for the frame only: an empty, truncated, still valid document.
    char frame[48];
    len = buildLogJson(log, ctx, frame, sizeof(frame));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("{\"events\":[],\"truncated\":true}", frame);

    // Out-of-bound floats (wider than WEB_LOG_ENTRY_MAX assumes) still give
    // valid JSON at the real cap: truncated if needed, never 0.
    MemoryKvStore hugeStore;
    webApiSeedWideLog(hugeStore, -1.0e30f);   // 32-char values
    EventLog huge(hugeStore, clock);
    TEST_ASSERT_TRUE(huge.begin());
    static char big[webLogCapFor(EventLog::CAPACITY)];
    len = buildLogJson(huge, ctx, big, sizeof(big));
    TEST_ASSERT_TRUE(len > 0);
    JsonDocument doc2;
    TEST_ASSERT_FALSE(deserializeJson(doc2, big, len));
    TEST_ASSERT_TRUE(doc2["events"].as<JsonArray>().size() >= 1);
    TEST_ASSERT_TRUE(doc2["truncated"].as<bool>());   // wider than the bound: some dropped
}

static void web_api_test_cmd_json() {
    char buf[128];
    CmdResult r{};
    JsonDocument doc;

    TEST_ASSERT_TRUE(buildCmdResultJson(CmdLookup::Pending, r, 1000001u, buf, sizeof(buf)) > 0);
    TEST_ASSERT_FALSE(deserializeJson(doc, buf));
    TEST_ASSERT_EQUAL_UINT32(1000001u, doc["id"].as<uint32_t>());
    TEST_ASSERT_FALSE(doc["done"].as<bool>());
    TEST_ASSERT_TRUE(doc["done"].is<bool>());

    r.id = 5;
    r.status = CommandStatus::Clamped;
    r.hasValue = true;
    r.value = 55.0f;
    TEST_ASSERT_TRUE(buildCmdResultJson(CmdLookup::Done, r, 5, buf, sizeof(buf)) > 0);
    TEST_ASSERT_FALSE(deserializeJson(doc, buf));
    TEST_ASSERT_TRUE(doc["done"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("clamped", doc["status"].as<const char*>());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, doc["value"].as<float>());

    r.status = CommandStatus::Ok;
    r.hasValue = false;
    TEST_ASSERT_TRUE(buildCmdResultJson(CmdLookup::Done, r, 5, buf, sizeof(buf)) > 0);
    TEST_ASSERT_FALSE(deserializeJson(doc, buf));
    TEST_ASSERT_EQUAL_STRING("ok", doc["status"].as<const char*>());
    TEST_ASSERT_FALSE(doc["value"].is<float>());

    TEST_ASSERT_TRUE(buildCmdResultJson(CmdLookup::Unknown, r, 9, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"id\":9,\"error\":\"unknown\"}", buf);

    TEST_ASSERT_EQUAL_STRING("ok", commandStatusKey(CommandStatus::Ok));
    TEST_ASSERT_EQUAL_STRING("clamped", commandStatusKey(CommandStatus::Clamped));
    TEST_ASSERT_EQUAL_STRING("unchanged", commandStatusKey(CommandStatus::Unchanged));
    TEST_ASSERT_EQUAL_STRING("rejected", commandStatusKey(CommandStatus::Rejected));
    TEST_ASSERT_EQUAL_STRING("invalid", commandStatusKey(CommandStatus::InvalidCommand));
    TEST_ASSERT_EQUAL_STRING("import_failed", commandStatusKey(CommandStatus::ImportFailed));
    TEST_ASSERT_EQUAL_STRING("queue_full", commandStatusKey(CommandStatus::QueueFull));
}

static void web_api_test_builders_overflow_return_zero() {
    static CommonState st{};
    st = CommonState{};
    st.sensors.count = 3;
    st.oneWire.count = 2;
    WebJsonContext ctx{"boiler-room", &WEB_API_HW, webApiFakeFmt, nullptr, nullptr, nullptr};
    char small[24];

    TEST_ASSERT_EQUAL_UINT32(0, buildStateJson(st, ctx, small, sizeof(small)));
    TEST_ASSERT_TRUE(strlen(small) < sizeof(small));
    TEST_ASSERT_EQUAL_UINT32(0, buildSensorsJson(st, WEB_API_HW, small, sizeof(small)));
    TEST_ASSERT_TRUE(strlen(small) < sizeof(small));

    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));
    TEST_ASSERT_EQUAL_UINT32(0, buildConfigValuesJson(engine, small, sizeof(small)));
    TEST_ASSERT_TRUE(strlen(small) < sizeof(small));

    TEST_ASSERT_TRUE(log.logEvent(static_cast<uint16_t>(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0,
        EventReason::Boot));
    TEST_ASSERT_EQUAL_UINT32(0, buildLogJson(log, ctx, small, sizeof(small)));
    TEST_ASSERT_TRUE(strlen(small) < sizeof(small));

    CmdResult r{};
    r.hasValue = true;
    r.status = CommandStatus::QueueFull;
    char tiny[12];
    TEST_ASSERT_EQUAL_UINT32(0, buildCmdResultJson(CmdLookup::Done, r, 123456, tiny, sizeof(tiny)));
    TEST_ASSERT_TRUE(strlen(tiny) < sizeof(tiny));
}

// ---- SchemaJsonStream -------------------------------------------------------

static void web_api_test_schema_stream_chunking_identical() {
    bool failed1 = true, failed7 = true, failedBig = true;
    std::string a = webApiDrainSchema(TEST_SCHEMA_A_V1, 1, failed1);
    std::string b = webApiDrainSchema(TEST_SCHEMA_A_V1, 7, failed7);
    std::string c = webApiDrainSchema(TEST_SCHEMA_A_V1, 4096, failedBig);
    TEST_ASSERT_FALSE(failed1);
    TEST_ASSERT_FALSE(failed7);
    TEST_ASSERT_FALSE(failedBig);
    TEST_ASSERT_TRUE(a.size() > 0);
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_TRUE(a == c);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, c.data(), c.size()));
    TEST_ASSERT_EQUAL_STRING("test-a", doc["project"].as<const char*>());
    JsonArray settings = doc["settings"];
    TEST_ASSERT_EQUAL_UINT32(webApiTotalSettings(TEST_SCHEMA_A_V1), settings.size());

    // global order and per-type fields
    TEST_ASSERT_EQUAL_STRING("wifiSsid", settings[0]["k"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("network", settings[0]["g"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Wi-Fi network", settings[0]["en"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Мережа Wi-Fi", settings[0]["uk"].as<const char*>());
    JsonObject intItem = settings[TEST_INDEX_INT];
    TEST_ASSERT_EQUAL_STRING("testInt", intItem["k"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("int", intItem["t"].as<const char*>());
    TEST_ASSERT_TRUE(intItem["u"].isNull());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, intItem["min"].as<float>());
    TEST_ASSERT_EQUAL_FLOAT(100.0f, intItem["max"].as<float>());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, intItem["step"].as<float>());
    TEST_ASSERT_EQUAL_FLOAT(50.0f, intItem["def"].as<float>());
    TEST_ASSERT_FALSE(intItem["defText"].is<const char*>());
    TEST_ASSERT_FALSE(intItem["secret"].as<bool>());
    JsonObject floatItem = settings[TEST_INDEX_FLOAT];
    TEST_ASSERT_EQUAL_STRING("float", floatItem["t"].as<const char*>());
    TEST_ASSERT_EQUAL_FLOAT(-10.5f, floatItem["min"].as<float>());
    JsonObject textItem = settings[TEST_INDEX_TEXT];
    TEST_ASSERT_EQUAL_STRING("text", textItem["t"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("hi", textItem["defText"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(16, textItem["ml"].as<int>());
    TEST_ASSERT_FALSE(textItem["def"].is<float>());
    TEST_ASSERT_EQUAL_STRING("bool", settings[TEST_INDEX_BOOL]["t"].as<const char*>());
    TEST_ASSERT_TRUE(settings[TEST_INDEX_SECRET]["secret"].as<bool>());
    TEST_ASSERT_TRUE(settings[commonIndex(CommonSetting::WebPass)]["secret"].as<bool>());

    // a finished stream stays finished
    SchemaJsonStream stream(TEST_SCHEMA_A_V1, "test-a");
    static char big[8192];
    size_t total = 0;
    size_t n = 0;
    while ((n = stream.read(big + total, sizeof(big) - total)) > 0) total += n;
    TEST_ASSERT_EQUAL_UINT32(c.size(), total);
    TEST_ASSERT_EQUAL_UINT32(0, stream.read(big, sizeof(big)));
}

static void web_api_test_schema_stream_item_too_big_fails() {
    bool failed = false;
    std::string out = webApiDrainSchema(WEB_API_LONG_SCHEMA, 64, failed);
    TEST_ASSERT_TRUE(failed);
    JsonDocument doc;
    TEST_ASSERT_TRUE(static_cast<bool>(deserializeJson(doc, out.data(), out.size())));
}

// Runs every test in this suite. Call from runWebSuite().
inline void runWebApiSuite() {
    RUN_TEST(web_api_test_setting_index_matches_config_engine);
    RUN_TEST(web_api_test_setting_unknown_and_wifi_keys);
    RUN_TEST(web_api_test_setting_int_parse);
    RUN_TEST(web_api_test_setting_int_length_cap);
    RUN_TEST(web_api_test_setting_float_parse);
    RUN_TEST(web_api_test_setting_bool_parse);
    RUN_TEST(web_api_test_setting_text_parse);
    RUN_TEST(web_api_test_rom_address_parse);
    RUN_TEST(web_api_test_logical_index_and_u32);
    RUN_TEST(web_api_test_input_error_keys);

    RUN_TEST(web_api_test_precheck_simple_results);
    RUN_TEST(web_api_test_precheck_real_export);

    RUN_TEST(web_api_test_state_json);
    RUN_TEST(web_api_test_sensors_json);
    RUN_TEST(web_api_test_config_json_masks_secret);
    RUN_TEST(web_api_test_log_json_newest_first);
    RUN_TEST(web_api_test_log_json_full_max_width_fits);
    RUN_TEST(web_api_test_log_json_truncates_to_valid_json);
    RUN_TEST(web_api_test_cmd_json);
    RUN_TEST(web_api_test_builders_overflow_return_zero);

    RUN_TEST(web_api_test_schema_stream_chunking_identical);
    RUN_TEST(web_api_test_schema_stream_item_too_big_fails);
}
