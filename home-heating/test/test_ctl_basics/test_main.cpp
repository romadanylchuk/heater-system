#include <unity.h>
#include <HomeHeatingControlSettings.h>
#include <HomeHeatingTypes.h>
#include <string.h>

// Stage 08, phase 1: shared types (C1) and the controller settings table (C2).

void setUp() {}
void tearDown() {}

static void test_classify_sensor() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorHealth::Ok), static_cast<int>(classifySensor(SensorState::Ok)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SensorHealth::Pending), static_cast<int>(classifySensor(SensorState::Unknown)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SensorHealth::Failed), static_cast<int>(classifySensor(SensorState::Fault)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SensorHealth::Failed), static_cast<int>(classifySensor(SensorState::Unassigned)));
}

// Literal "Fail" column of the plan's fail-safe matrix (rows 1..27; Fault =
// Failed, Unknown = Pending). Never computed by the code under test.
struct FailRow {
    SensorHealth h1, h2, h3;
    FailMode fail;
};

static constexpr SensorHealth O = SensorHealth::Ok;
static constexpr SensorHealth F = SensorHealth::Failed;
static constexpr SensorHealth U = SensorHealth::Pending;

static const FailRow FAIL_ROWS[] = {
    {O, O, O, FailMode::None},  {O, O, F, FailMode::H3},    {O, O, U, FailMode::None},
    {O, F, O, FailMode::H2},    {O, F, F, FailMode::Multi}, {O, F, U, FailMode::H2},
    {O, U, O, FailMode::None},  {O, U, F, FailMode::H3},    {O, U, U, FailMode::None},
    {F, O, O, FailMode::H1},    {F, O, F, FailMode::Multi}, {F, O, U, FailMode::H1},
    {F, F, O, FailMode::Multi}, {F, F, F, FailMode::Multi}, {F, F, U, FailMode::Multi},
    {F, U, O, FailMode::H1},    {F, U, F, FailMode::Multi}, {F, U, U, FailMode::H1},
    {U, O, O, FailMode::None},  {U, O, F, FailMode::H3},    {U, O, U, FailMode::None},
    {U, F, O, FailMode::H2},    {U, F, F, FailMode::Multi}, {U, F, U, FailMode::H2},
    {U, U, O, FailMode::None},  {U, U, F, FailMode::H3},    {U, U, U, FailMode::None},
};

static void test_compute_fail_mode_matrix() {
    TEST_ASSERT_EQUAL_UINT32(27, sizeof(FAIL_ROWS) / sizeof(FAIL_ROWS[0]));
    char msg[16];
    for (size_t i = 0; i < 27; ++i) {
        const FailRow& r = FAIL_ROWS[i];
        msg[0] = 'r';
        msg[1] = static_cast<char>('0' + (i + 1) / 10);
        msg[2] = static_cast<char>('0' + (i + 1) % 10);
        msg[3] = '\0';
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            static_cast<int>(r.fail), static_cast<int>(computeFailMode(r.h1, r.h2, r.h3)), msg);
    }
}

static bool isAscii(const char* s) {
    for (; *s; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c < 0x20 || c > 0x7E) {
            return false;
        }
    }
    return true;
}

static void checkKey(const char* key) {
    TEST_ASSERT_NOT_NULL(key);
    TEST_ASSERT_TRUE(strlen(key) > 0);
    TEST_ASSERT_TRUE_MESSAGE(isAscii(key), key);
    for (const char* p = key; *p; ++p) {
        TEST_ASSERT_TRUE_MESSAGE((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_', key);
    }
}

static void checkShort(const char* s) {
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(strlen(s) > 0);
    TEST_ASSERT_TRUE_MESSAGE(isAscii(s), s);
    TEST_ASSERT_TRUE_MESSAGE(strlen(s) <= 8, s);
}

static void test_p4_reason_strings() {
    const char* keys[] = {"none", "heating_off", "sensor_wait", "demand", "off_delay", "supply_cold", "h3_fault",
        "multi_fault", "multi_fault_off"};
    for (uint8_t i = 0; i <= 8; ++i) {
        const P4Reason r = static_cast<P4Reason>(i);
        checkKey(p4ReasonKey(r));
        checkShort(p4ReasonShort(r));
        TEST_ASSERT_EQUAL_STRING(keys[i], p4ReasonKey(r));
    }
    TEST_ASSERT_EQUAL_STRING("none", p4ReasonKey(static_cast<P4Reason>(200)));
    TEST_ASSERT_EQUAL_STRING("?", p4ReasonShort(static_cast<P4Reason>(200)));
}

static void test_k2_reason_strings() {
    const char* keys[] = {"none", "sensor_wait", "charging", "delta_low", "h3_low", "h4_full", "h3_fault", "h4_fault"};
    for (uint8_t i = 0; i <= 7; ++i) {
        const K2Reason r = static_cast<K2Reason>(i);
        checkKey(k2ReasonKey(r));
        checkShort(k2ReasonShort(r));
        TEST_ASSERT_EQUAL_STRING(keys[i], k2ReasonKey(r));
    }
    TEST_ASSERT_EQUAL_STRING("none", k2ReasonKey(static_cast<K2Reason>(200)));
    TEST_ASSERT_EQUAL_STRING("?", k2ReasonShort(static_cast<K2Reason>(200)));
}

static void test_k1_mode_strings() {
    const char* keys[] = {
        "unknown", "recal", "closed", "wait", "normal", "fb_only", "ff_only", "failpos_fb", "failpos_fixed"};
    for (uint8_t i = 0; i <= 8; ++i) {
        const K1Mode m = static_cast<K1Mode>(i);
        checkKey(k1ModeKey(m));
        checkShort(k1ModeShort(m));
        TEST_ASSERT_EQUAL_STRING(keys[i], k1ModeKey(m));
    }
    TEST_ASSERT_EQUAL_STRING("none", k1ModeKey(static_cast<K1Mode>(200)));
    TEST_ASSERT_EQUAL_STRING("?", k1ModeShort(static_cast<K1Mode>(200)));
}

static void test_fail_mode_keys() {
    const char* keys[] = {"none", "h1", "h2", "h3", "multi"};
    for (uint8_t i = 0; i <= 4; ++i) {
        checkKey(failModeKey(static_cast<FailMode>(i)));
        TEST_ASSERT_EQUAL_STRING(keys[i], failModeKey(static_cast<FailMode>(i)));
    }
    TEST_ASSERT_EQUAL_STRING("none", failModeKey(static_cast<FailMode>(200)));
}

static void test_index_constants() {
    TEST_ASSERT_EQUAL_UINT8(4, HH_SENSOR_COUNT);
    TEST_ASSERT_EQUAL_UINT8(3, HH_SENSOR_H4);
    TEST_ASSERT_EQUAL_UINT8(0, HH_RELAY_K2);
    TEST_ASSERT_EQUAL_UINT8(1, HH_RELAY_K1_POWER);
    TEST_ASSERT_EQUAL_UINT8(2, HH_RELAY_K1_DIR);
    TEST_ASSERT_EQUAL_UINT8(3, HH_RELAY_P4);
    TEST_ASSERT_EQUAL_UINT8(0, HH_AS_P4);
    TEST_ASSERT_EQUAL_UINT8(1, HH_AS_K2);
    TEST_ASSERT_EQUAL_UINT8(2, HH_AS_K1);
}

// Defaults equal the plan's C2 table (literal values).
static void test_defaults_match_c2_table() {
    const HomeHeatingSettings s = defaultHomeHeatingSettings();
    TEST_ASSERT_TRUE(s.heatingEnabled);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, s.h2Set);
    TEST_ASSERT_EQUAL_UINT32(5, s.p4OffDelayMin);
    TEST_ASSERT_EQUAL_UINT32(120, s.k1TravelS);
    TEST_ASSERT_EQUAL_UINT32(30, s.k1PeriodS);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, s.k1Deadband);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, s.k1Gain);
    TEST_ASSERT_EQUAL_UINT32(10, s.k1MaxPulseS);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, s.k1MinPulseS);
    TEST_ASSERT_EQUAL_UINT32(10, s.k1ResyncPct);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, s.k1SmallDiff);
    TEST_ASSERT_EQUAL_UINT32(30, s.k1FailPosPct);
    TEST_ASSERT_EQUAL_UINT32(5, s.k1FfStepPct);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, s.k2Delta);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, s.k2DeltaHyst);
    TEST_ASSERT_EQUAL_FLOAT(65.0f, s.k2H3Min);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, s.k2H3MinHyst);
    TEST_ASSERT_EQUAL_FLOAT(70.0f, s.k2H4Max);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, s.k2H4MaxHyst);
}

static void test_table_rows_key_equals_nvs_key() {
    TEST_ASSERT_EQUAL_UINT32(18, static_cast<uint32_t>(HOME_HEATING_CONTROL_SETTING_COUNT));
    TEST_ASSERT_EQUAL_PTR(HOME_HEATING_CONTROL_SETTINGS, HOME_HEATING_CONTROL_SETTINGS_TABLE.items);
    TEST_ASSERT_EQUAL_UINT32(18, static_cast<uint32_t>(HOME_HEATING_CONTROL_SETTINGS_TABLE.count));
    for (size_t i = 0; i < HOME_HEATING_CONTROL_SETTING_COUNT; ++i) {
        const SettingDescriptor& d = HOME_HEATING_CONTROL_SETTINGS[i];
        TEST_ASSERT_EQUAL_STRING(d.key, d.nvsKey);
        TEST_ASSERT_TRUE_MESSAGE(strlen(d.nvsKey) <= 15, d.key);
        TEST_ASSERT_TRUE_MESSAGE(d.type != SettingType::Bool, d.key);  // no new Bool settings
        TEST_ASSERT_NOT_NULL_MESSAGE(d.labelEn, d.key);
        TEST_ASSERT_NOT_NULL_MESSAGE(d.labelUa, d.key);
    }
}

static void test_guard_clamps_delta_hyst() {
    HomeHeatingSettings s = defaultHomeHeatingSettings();
    s.k2Delta = 3.0f;
    s.k2DeltaHyst = 5.0f;
    TEST_ASSERT_EQUAL_FLOAT(2.5f, guardHomeHeatingSettings(s).k2DeltaHyst);

    s.k2Delta = 1.0f;
    s.k2DeltaHyst = 10.0f;
    TEST_ASSERT_EQUAL_FLOAT(0.5f, guardHomeHeatingSettings(s).k2DeltaHyst);
}

static void test_guard_raises_max_pulse() {
    HomeHeatingSettings s = defaultHomeHeatingSettings();
    s.k1MaxPulseS = 1;
    s.k1MinPulseS = 2.5f;
    TEST_ASSERT_EQUAL_UINT32(3, guardHomeHeatingSettings(s).k1MaxPulseS);

    s.k1MinPulseS = 2.0f;
    TEST_ASSERT_EQUAL_UINT32(2, guardHomeHeatingSettings(s).k1MaxPulseS);
}

// Field-by-field (the struct has padding, so no memcmp).
static void assertSameSettings(const HomeHeatingSettings& a, const HomeHeatingSettings& b) {
    TEST_ASSERT_EQUAL(a.heatingEnabled, b.heatingEnabled);
    TEST_ASSERT_EQUAL_FLOAT(a.h2Set, b.h2Set);
    TEST_ASSERT_EQUAL_UINT32(a.p4OffDelayMin, b.p4OffDelayMin);
    TEST_ASSERT_EQUAL_UINT32(a.k1TravelS, b.k1TravelS);
    TEST_ASSERT_EQUAL_UINT32(a.k1PeriodS, b.k1PeriodS);
    TEST_ASSERT_EQUAL_FLOAT(a.k1Deadband, b.k1Deadband);
    TEST_ASSERT_EQUAL_FLOAT(a.k1Gain, b.k1Gain);
    TEST_ASSERT_EQUAL_UINT32(a.k1MaxPulseS, b.k1MaxPulseS);
    TEST_ASSERT_EQUAL_FLOAT(a.k1MinPulseS, b.k1MinPulseS);
    TEST_ASSERT_EQUAL_UINT32(a.k1ResyncPct, b.k1ResyncPct);
    TEST_ASSERT_EQUAL_FLOAT(a.k1SmallDiff, b.k1SmallDiff);
    TEST_ASSERT_EQUAL_UINT32(a.k1FailPosPct, b.k1FailPosPct);
    TEST_ASSERT_EQUAL_UINT32(a.k1FfStepPct, b.k1FfStepPct);
    TEST_ASSERT_EQUAL_FLOAT(a.k2Delta, b.k2Delta);
    TEST_ASSERT_EQUAL_FLOAT(a.k2DeltaHyst, b.k2DeltaHyst);
    TEST_ASSERT_EQUAL_FLOAT(a.k2H3Min, b.k2H3Min);
    TEST_ASSERT_EQUAL_FLOAT(a.k2H3MinHyst, b.k2H3MinHyst);
    TEST_ASSERT_EQUAL_FLOAT(a.k2H4Max, b.k2H4Max);
    TEST_ASSERT_EQUAL_FLOAT(a.k2H4MaxHyst, b.k2H4MaxHyst);
}

static void test_guard_leaves_valid_settings_unchanged() {
    const HomeHeatingSettings d = defaultHomeHeatingSettings();
    assertSameSettings(d, guardHomeHeatingSettings(d));

    HomeHeatingSettings s = d;
    s.k2Delta = 3.0f;
    s.k2DeltaHyst = 2.5f;   // exactly at the bound
    s.k1MaxPulseS = 5;
    s.k1MinPulseS = 5.0f;   // exactly at the bound
    assertSameSettings(s, guardHomeHeatingSettings(s));

    // A guard violation only touches its own field.
    HomeHeatingSettings v = d;
    v.k2DeltaHyst = 9.0f;
    HomeHeatingSettings expected = d;
    expected.k2DeltaHyst = 2.5f;
    assertSameSettings(expected, guardHomeHeatingSettings(v));
}

static void test_k1_stroke_lengths() {
    const HomeHeatingSettings s = defaultHomeHeatingSettings();
    TEST_ASSERT_EQUAL_UINT32(120000, k1TravelMs(s));
    TEST_ASSERT_EQUAL_UINT32(12000, k1OverdriveMs(s));
    TEST_ASSERT_EQUAL_UINT32(132000, k1RecalMs(s));

    HomeHeatingSettings m = s;
    m.k1TravelS = 300;
    m.k1ResyncPct = 25;
    TEST_ASSERT_EQUAL_UINT32(375000, k1RecalMs(m));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_classify_sensor);
    RUN_TEST(test_compute_fail_mode_matrix);
    RUN_TEST(test_p4_reason_strings);
    RUN_TEST(test_k2_reason_strings);
    RUN_TEST(test_k1_mode_strings);
    RUN_TEST(test_fail_mode_keys);
    RUN_TEST(test_index_constants);
    RUN_TEST(test_defaults_match_c2_table);
    RUN_TEST(test_table_rows_key_equals_nvs_key);
    RUN_TEST(test_guard_clamps_delta_hyst);
    RUN_TEST(test_guard_raises_max_pulse);
    RUN_TEST(test_guard_leaves_valid_settings_unchanged);
    RUN_TEST(test_k1_stroke_lengths);
    return UNITY_END();
}
