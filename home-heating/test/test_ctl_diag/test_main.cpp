#include <unity.h>
#include <math.h>
#include <string.h>
#include <HomeHeatingDiagSettings.h>
#include <HomeHeatingDiagnostics.h>
#include <HomeHeatingTypes.h>
#include <K1PulseCounter.h>
#include <LocalTime.h>
#include <P4FlowCheck.h>
#include <SensorHistory.h>

// Stage 09 phase 5: the home-heating diagnostics -- the P4FlowCheck (H1), the
// diag settings defaults, the HomeHeatingDiagnostics composition (history +
// mask) and the K1PulseCounter (today / yesterday by local day), all with
// injected time.

static const uint64_t SEC = 1000ull;
static const uint64_t MIN = 60000ull;

static P4FlowCheck check;
static HomeHeatingDiagSettings settings;
static HomeHeatingDiagnostics diag;
static HomeHeatingDiagInputs din;
static K1PulseCounter pulses;

static SensorInput ok(float t) { return SensorInput{SensorState::Ok, t, false}; }
static SensorInput fault() { return SensorInput{SensorState::Fault, NAN, false}; }
static SensorInput unknown() { return SensorInput{SensorState::Unknown, NAN, false}; }

// The H1 trigger scenario: H1 40, H2 41 (H2 - H1 = 1), H3 60 (H1 + 20), K1 40 %.
static void setTrigger() {
    din.sensor[HH_SENSOR_H1] = ok(40.0f);
    din.sensor[HH_SENSOR_H2] = ok(41.0f);
    din.sensor[HH_SENSOR_H3] = ok(60.0f);
    din.sensor[HH_SENSOR_H4] = ok(50.0f);
    din.p4Actual = true;
    din.k1Known = true;
    din.k1PosPct = 40.0f;
}

void setUp() {
    check.reset();
    settings = defaultHomeHeatingDiagSettings();
    diag.reset();
    din = HomeHeatingDiagInputs{};
    setTrigger();
    pulses.reset();
}
void tearDown() {}

// ---- P4FlowCheck helpers (defaults: 5 min, K1 > 30 %, H3 > H1 + 10, H2 - H1 < 2) --

static P4FlowInputs flow(float h1c, float h2c, float h3c, float k1 = 40.0f) {
    return P4FlowInputs{true, true, true, true, h1c, h2c, h3c, true, k1};
}
static P4FlowInputs trig() { return flow(40.0f, 41.0f, 60.0f); }

static bool h1(const P4FlowInputs& in, uint64_t t) {
    bool r = check.update(in, settings.h1, t);
    TEST_ASSERT_EQUAL(check.active(), r);
    return r;
}

// ---- settings --------------------------------------------------------------

static void test_defaults_equal_table() {
    const HomeHeatingDiagSettings d = defaultHomeHeatingDiagSettings();
    TEST_ASSERT_TRUE(d.h1.enabled);
    TEST_ASSERT_EQUAL_UINT32(5, d.h1.minOnMin);
    TEST_ASSERT_EQUAL_UINT32(30, d.h1.k1MinPct);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, d.h1.deltaC);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, d.h1.minDiffC);
    TEST_ASSERT_EQUAL_UINT32(10, d.k1StepPulseS);
}

struct DiagRow {
    const char* key;
    SettingType type;
    const char* group;
    float min, max, def;
};

static void test_table_shape_and_unique_keys() {
    const DiagRow rows[] = {
        {HH_KEY_H1_EN, SettingType::Bool, "diag", 0, 1, 1},
        {HH_KEY_H1_MIN_ON, SettingType::Int, "diag", 1, 60, 5},
        {HH_KEY_H1_K1_MIN, SettingType::Int, "diag", 5, 95, 30},
        {HH_KEY_H1_DELTA, SettingType::Float, "diag", 3, 40, 10},
        {HH_KEY_H1_MIN_DIFF, SettingType::Float, "diag", 0.5f, 10, 2},
        {HH_KEY_K1_STEP_PULSE, SettingType::Int, "k1", 2, 30, 10},
    };
    TEST_ASSERT_EQUAL_UINT32(HOME_HEATING_DIAG_SETTING_COUNT, sizeof(rows) / sizeof(rows[0]));
    TEST_ASSERT_EQUAL_UINT32(HOME_HEATING_DIAG_SETTING_COUNT, HOME_HEATING_DIAG_SETTINGS_TABLE.count);
    for (size_t i = 0; i < HOME_HEATING_DIAG_SETTING_COUNT; ++i) {
        const SettingDescriptor& d = HOME_HEATING_DIAG_SETTINGS[i];
        TEST_ASSERT_EQUAL_STRING(rows[i].key, d.key);
        TEST_ASSERT_EQUAL_STRING(d.key, d.nvsKey);
        TEST_ASSERT_TRUE(strlen(d.nvsKey) <= 15);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(rows[i].type), static_cast<int>(d.type));
        TEST_ASSERT_EQUAL_STRING(rows[i].group, d.group);
        TEST_ASSERT_EQUAL_FLOAT(rows[i].def, d.defaultValue);
        if (rows[i].type != SettingType::Bool) {
            TEST_ASSERT_EQUAL_FLOAT(rows[i].min, d.minValue);
            TEST_ASSERT_EQUAL_FLOAT(rows[i].max, d.maxValue);
        }
        TEST_ASSERT_EQUAL_UINT8(0, d.flags & SETTING_FLAG_HA_SWITCH);
        for (size_t j = i + 1; j < HOME_HEATING_DIAG_SETTING_COUNT; ++j) {
            TEST_ASSERT_TRUE(strcmp(d.key, HOME_HEATING_DIAG_SETTINGS[j].key) != 0);
        }
    }
}

// ---- P4FlowCheck (H1) ----------------------------------------------------

static void test_h1_triggers_at_5_min() {
    TEST_ASSERT_FALSE(h1(trig(), 0));
    TEST_ASSERT_TRUE(check.tracking());
    TEST_ASSERT_FALSE(h1(trig(), 5 * MIN - SEC));
    TEST_ASSERT_TRUE(h1(trig(), 5 * MIN));
}

static void test_h1_no_trigger_at_k1_30_pct() {
    h1(flow(40.0f, 41.0f, 60.0f, 30.0f), 0);
    TEST_ASSERT_FALSE(h1(flow(40.0f, 41.0f, 60.0f, 30.0f), 10 * MIN));   // not > 30
    TEST_ASSERT_TRUE(h1(flow(40.0f, 41.0f, 60.0f, 30.5f), 10 * MIN + SEC));
}

static void test_h1_no_trigger_with_diff_2_or_small_delta() {
    h1(trig(), 0);
    TEST_ASSERT_FALSE(h1(flow(40.0f, 42.0f, 60.0f), 6 * MIN));   // H2 - H1 = 2: not < 2
    TEST_ASSERT_FALSE(h1(flow(40.0f, 43.0f, 60.0f), 7 * MIN));
    TEST_ASSERT_FALSE(h1(flow(40.0f, 41.0f, 50.0f), 8 * MIN));   // H3 = H1 + 10: not >
    TEST_ASSERT_FALSE(h1(flow(40.0f, 41.0f, 45.0f), 9 * MIN));
    TEST_ASSERT_TRUE(h1(trig(), 10 * MIN));                       // the timer kept running
}

static void test_h1_p4_off_no_tracking() {
    P4FlowInputs in = trig();
    in.p4On = false;
    TEST_ASSERT_FALSE(h1(in, 0));
    TEST_ASSERT_FALSE(check.tracking());
    TEST_ASSERT_FALSE(h1(in, 10 * MIN));
    TEST_ASSERT_FALSE(check.tracking());
    // The ON edge starts the timer from here.
    TEST_ASSERT_FALSE(h1(trig(), 10 * MIN));
    TEST_ASSERT_FALSE(h1(trig(), 15 * MIN - SEC));
    TEST_ASSERT_TRUE(h1(trig(), 15 * MIN));
    // Turning P4 off clears it at once.
    TEST_ASSERT_FALSE(h1(in, 15 * MIN + SEC));
}

static void test_h1_k1_unknown_pauses_and_restarts() {
    h1(trig(), 0);
    TEST_ASSERT_TRUE(h1(trig(), 5 * MIN));
    P4FlowInputs in = trig();
    in.k1Known = false;
    TEST_ASSERT_FALSE(h1(in, 5 * MIN + SEC));
    TEST_ASSERT_FALSE(check.tracking());
    TEST_ASSERT_FALSE(h1(in, 8 * MIN));
    // Known again at 8:01: the timer restarts there.
    TEST_ASSERT_FALSE(h1(trig(), 8 * MIN + SEC));
    TEST_ASSERT_TRUE(check.tracking());
    TEST_ASSERT_FALSE(h1(trig(), 13 * MIN));
    TEST_ASSERT_TRUE(h1(trig(), 13 * MIN + SEC));
}

static void test_h1_h2_fault_pauses_and_restarts() {
    h1(trig(), 0);
    TEST_ASSERT_TRUE(h1(trig(), 5 * MIN));
    P4FlowInputs in = trig();
    in.h2Ok = false;
    in.h2C = NAN;
    TEST_ASSERT_FALSE(h1(in, 6 * MIN));
    TEST_ASSERT_FALSE(check.tracking());
    TEST_ASSERT_FALSE(h1(trig(), 7 * MIN));   // recovery: restart
    TEST_ASSERT_FALSE(h1(trig(), 12 * MIN - SEC));
    TEST_ASSERT_TRUE(h1(trig(), 12 * MIN));
}

static void test_h1_h1_or_h3_fault_pauses() {
    h1(trig(), 0);
    P4FlowInputs in = trig();
    in.h1Ok = false;
    TEST_ASSERT_FALSE(h1(in, 6 * MIN));
    TEST_ASSERT_FALSE(check.tracking());
    h1(trig(), 7 * MIN);
    in = trig();
    in.h3Ok = false;
    TEST_ASSERT_FALSE(h1(in, 8 * MIN));
    TEST_ASSERT_FALSE(check.tracking());
}

static void test_h1_disabled_never_raised_but_tracks() {
    settings.h1.enabled = false;
    h1(trig(), 0);
    TEST_ASSERT_FALSE(h1(trig(), 10 * MIN));
    TEST_ASSERT_FALSE(h1(trig(), 60 * MIN));
    TEST_ASSERT_TRUE(check.tracking());   // D4: tracking continues while disabled
    settings.h1.enabled = true;
    TEST_ASSERT_TRUE(h1(trig(), 60 * MIN + SEC));   // re-enable: the original start is kept
}

static void test_h1_auto_clears_when_diff_reaches_2() {
    h1(trig(), 0);
    TEST_ASSERT_TRUE(h1(trig(), 5 * MIN));
    TEST_ASSERT_TRUE(h1(flow(40.0f, 41.5f, 60.0f), 5 * MIN + SEC));
    TEST_ASSERT_FALSE(h1(flow(40.0f, 42.0f, 60.0f), 5 * MIN + 2 * SEC));
    TEST_ASSERT_TRUE(check.tracking());
}

static void test_h1_reset() {
    h1(trig(), 0);
    TEST_ASSERT_TRUE(h1(trig(), 5 * MIN));
    check.reset();
    TEST_ASSERT_FALSE(check.active());
    TEST_ASSERT_FALSE(check.tracking());
}

// ---- HomeHeatingDiagnostics composition -----------------------------------

static uint32_t tickDiag(uint64_t t) { return diag.update(din, settings, t); }

static void test_diag_mask_bit0_and_owned_mask() {
    TEST_ASSERT_EQUAL_UINT8(0, HH_WARN_H1);
    TEST_ASSERT_EQUAL_HEX32(0x000000FFu, HH_WARN_OWNED_MASK);
    TEST_ASSERT_EQUAL_HEX32(0, tickDiag(0));
    TEST_ASSERT_EQUAL_HEX32(0, tickDiag(5 * MIN - SEC));
    TEST_ASSERT_EQUAL_HEX32(1u << HH_WARN_H1, tickDiag(5 * MIN));
    TEST_ASSERT_TRUE(diag.h1Check().active());
    TEST_ASSERT_EQUAL_HEX32(0, tickDiag(5 * MIN) & ~HH_WARN_OWNED_MASK);
    din.p4Actual = false;   // relay actual OFF
    TEST_ASSERT_EQUAL_HEX32(0, tickDiag(5 * MIN + SEC));
    TEST_ASSERT_FALSE(diag.h1Check().tracking());
}

static void test_diag_maps_sensors_and_k1() {
    tickDiag(0);
    din.sensor[HH_SENSOR_H4] = fault();   // H4 is not part of H1: no pause
    TEST_ASSERT_EQUAL_HEX32(1u, tickDiag(5 * MIN));
    din.k1PosPct = 30.0f;                 // K1 position feeds the check
    TEST_ASSERT_EQUAL_HEX32(0, tickDiag(5 * MIN + SEC));
    din.k1PosPct = 40.0f;
    din.k1Known = false;                  // pause
    TEST_ASSERT_EQUAL_HEX32(0, tickDiag(5 * MIN + 2 * SEC));
    TEST_ASSERT_FALSE(diag.h1Check().tracking());
    din.k1Known = true;
    din.sensor[HH_SENSOR_H2] = unknown();  // D3: Unknown pauses
    TEST_ASSERT_EQUAL_HEX32(0, tickDiag(5 * MIN + 3 * SEC));
    TEST_ASSERT_FALSE(diag.h1Check().tracking());
    din.sensor[HH_SENSOR_H2] = ok(41.0f);
    TEST_ASSERT_EQUAL_HEX32(0, tickDiag(6 * MIN));
    TEST_ASSERT_EQUAL_HEX32(1u, tickDiag(11 * MIN));
}

static void test_diag_history_samples_h1_h4_with_nan_for_non_ok() {
    TEST_ASSERT_EQUAL_UINT8(HH_SENSOR_COUNT, diag.history().sensorCount());
    TEST_ASSERT_EQUAL_UINT16(0, diag.history().size());
    tickDiag(0);
    TEST_ASSERT_EQUAL_UINT16(1, diag.history().size());
    TEST_ASSERT_EQUAL_FLOAT(40.0f, diag.history().at(HH_SENSOR_H1, 0));
    TEST_ASSERT_EQUAL_FLOAT(41.0f, diag.history().at(HH_SENSOR_H2, 0));
    TEST_ASSERT_EQUAL_FLOAT(60.0f, diag.history().at(HH_SENSOR_H3, 0));
    TEST_ASSERT_EQUAL_FLOAT(50.0f, diag.history().at(HH_SENSOR_H4, 0));
    din.sensor[HH_SENSOR_H3] = fault();
    din.sensor[HH_SENSOR_H4] = SensorInput{SensorState::Unknown, 55.0f, false};   // non-Ok value ignored
    tickDiag(15 * SEC);
    TEST_ASSERT_EQUAL_UINT16(1, diag.history().size());   // not due yet
    tickDiag(30 * SEC);
    TEST_ASSERT_EQUAL_UINT16(2, diag.history().size());
    TEST_ASSERT_TRUE(isnan(diag.history().at(HH_SENSOR_H3, 0)));
    TEST_ASSERT_TRUE(isnan(diag.history().at(HH_SENSOR_H4, 0)));
    TEST_ASSERT_EQUAL_FLOAT(40.0f, diag.history().at(HH_SENSOR_H1, 0));
    TEST_ASSERT_EQUAL_FLOAT(60.0f, diag.history().at(HH_SENSOR_H3, 1));
    diag.reset();
    TEST_ASSERT_EQUAL_UINT16(0, diag.history().size());
    TEST_ASSERT_FALSE(diag.h1Check().tracking());
}

// ---- K1PulseCounter --------------------------------------------------------

static LocalTimeInfo day(uint32_t d) { return LocalTimeInfo{true, d, 600}; }

static void test_pulses_count_deltas() {
    TEST_ASSERT_EQUAL_UINT32(0, pulses.today());
    TEST_ASSERT_FALSE(pulses.yesterdayValid());
    pulses.update(0, day(100));
    pulses.update(1, day(100));
    pulses.update(1, day(100));
    pulses.update(3, day(100));
    TEST_ASSERT_EQUAL_UINT32(3, pulses.today());
    TEST_ASSERT_FALSE(pulses.yesterdayValid());
}

static void test_pulses_run_starts_wrap() {
    pulses.update(0xFFFFFFFEu, day(100));
    pulses.update(0xFFFFFFFEu, day(101));   // roll over: today = 0, lastStarts = 0xFFFFFFFE
    TEST_ASSERT_EQUAL_UINT32(0, pulses.today());
    pulses.update(1u, day(101));   // 0xFFFFFFFE -> 0xFFFFFFFF -> 0 -> 1
    TEST_ASSERT_EQUAL_UINT32(3, pulses.today());
}

static void test_pulses_invalid_time_accumulates_then_first_valid_keeps_today() {
    pulses.update(2, NO_LOCAL_TIME);
    pulses.update(5, NO_LOCAL_TIME);
    TEST_ASSERT_EQUAL_UINT32(5, pulses.today());
    pulses.update(6, day(200));   // first valid time: sets the day, no rollover (A9)
    TEST_ASSERT_EQUAL_UINT32(6, pulses.today());
    TEST_ASSERT_FALSE(pulses.yesterdayValid());
    pulses.update(7, NO_LOCAL_TIME);   // time lost again: keep accumulating
    TEST_ASSERT_EQUAL_UINT32(7, pulses.today());
    TEST_ASSERT_FALSE(pulses.yesterdayValid());
}

static void test_pulses_next_day_rolls_over() {
    pulses.update(4, day(100));
    TEST_ASSERT_EQUAL_UINT32(4, pulses.today());
    pulses.update(4, day(101));
    TEST_ASSERT_TRUE(pulses.yesterdayValid());
    TEST_ASSERT_EQUAL_UINT32(4, pulses.yesterday());
    TEST_ASSERT_EQUAL_UINT32(0, pulses.today());
    pulses.update(6, day(101));
    TEST_ASSERT_EQUAL_UINT32(2, pulses.today());
    pulses.update(7, day(102));   // the delta of the rollover tick counts on the new day
    TEST_ASSERT_EQUAL_UINT32(2, pulses.yesterday());
    TEST_ASSERT_EQUAL_UINT32(1, pulses.today());
}

static void test_pulses_multi_day_gap_gives_zero_yesterday() {
    pulses.update(4, day(100));
    pulses.update(4, day(103));   // D9: +3 days
    TEST_ASSERT_TRUE(pulses.yesterdayValid());
    TEST_ASSERT_EQUAL_UINT32(0, pulses.yesterday());
    TEST_ASSERT_EQUAL_UINT32(0, pulses.today());
}

static void test_pulses_backward_day_gives_zero_yesterday() {
    pulses.update(4, day(100));
    pulses.update(5, day(99));    // D9: the clock stepped back
    TEST_ASSERT_TRUE(pulses.yesterdayValid());
    TEST_ASSERT_EQUAL_UINT32(0, pulses.yesterday());
    TEST_ASSERT_EQUAL_UINT32(1, pulses.today());
}

static void test_pulses_reset_invalidates_yesterday() {
    pulses.update(4, day(100));
    pulses.update(4, day(101));
    TEST_ASSERT_TRUE(pulses.yesterdayValid());
    pulses.reset();
    TEST_ASSERT_FALSE(pulses.yesterdayValid());
    TEST_ASSERT_EQUAL_UINT32(0, pulses.yesterday());
    TEST_ASSERT_EQUAL_UINT32(0, pulses.today());
    pulses.update(2, day(101));   // day unknown again: no rollover
    TEST_ASSERT_FALSE(pulses.yesterdayValid());
    TEST_ASSERT_EQUAL_UINT32(2, pulses.today());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_equal_table);
    RUN_TEST(test_table_shape_and_unique_keys);
    RUN_TEST(test_h1_triggers_at_5_min);
    RUN_TEST(test_h1_no_trigger_at_k1_30_pct);
    RUN_TEST(test_h1_no_trigger_with_diff_2_or_small_delta);
    RUN_TEST(test_h1_p4_off_no_tracking);
    RUN_TEST(test_h1_k1_unknown_pauses_and_restarts);
    RUN_TEST(test_h1_h2_fault_pauses_and_restarts);
    RUN_TEST(test_h1_h1_or_h3_fault_pauses);
    RUN_TEST(test_h1_disabled_never_raised_but_tracks);
    RUN_TEST(test_h1_auto_clears_when_diff_reaches_2);
    RUN_TEST(test_h1_reset);
    RUN_TEST(test_diag_mask_bit0_and_owned_mask);
    RUN_TEST(test_diag_maps_sensors_and_k1);
    RUN_TEST(test_diag_history_samples_h1_h4_with_nan_for_non_ok);
    RUN_TEST(test_pulses_count_deltas);
    RUN_TEST(test_pulses_run_starts_wrap);
    RUN_TEST(test_pulses_invalid_time_accumulates_then_first_valid_keeps_today);
    RUN_TEST(test_pulses_next_day_rolls_over);
    RUN_TEST(test_pulses_multi_day_gap_gives_zero_yesterday);
    RUN_TEST(test_pulses_backward_day_gives_zero_yesterday);
    RUN_TEST(test_pulses_reset_invalidates_yesterday);
    return UNITY_END();
}
