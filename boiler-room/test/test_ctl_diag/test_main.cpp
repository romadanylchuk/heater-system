#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <BoilerRoomDiagSettings.h>
#include <BoilerRoomDiagnostics.h>
#include <BoilerRoomTypes.h>
#include <PumpRiseCheck.h>
#include <SensorHistory.h>

// Stage 09 phase 3: the boiler-room pump-response diagnostics -- the
// PumpRiseCheck primitive (B1/B3/B6), the diag settings defaults and the
// BoilerRoomDiagnostics composition (history + mask), all with injected time.

static const uint64_t SEC = 1000ull;
static const uint64_t MIN = 60000ull;

static PumpRiseCheck check;
static BoilerRoomDiagSettings settings;
static BoilerRoomDiagnostics diag;
static BoilerRoomDiagInputs din;

static SensorInput ok(float t) { return SensorInput{SensorState::Ok, t, false}; }
static SensorInput fault() { return SensorInput{SensorState::Fault, NAN, false}; }
static SensorInput unassigned() { return SensorInput{SensorState::Unassigned, NAN, false}; }
static SensorInput unknown() { return SensorInput{SensorState::Unknown, NAN, false}; }

void setUp() {
    check.reset();
    settings = defaultBoilerRoomDiagSettings();
    diag.reset();
    din = BoilerRoomDiagInputs{};
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        din.sensor[i] = ok(40.0f);
    }
}
void tearDown() {}

// ---- PumpRiseCheck helpers (B1 defaults: 3 min, T3 - T6 > 15, T6 rise < 2) --

static RiseCheckInputs on(float hot, float cold) { return RiseCheckInputs{true, true, hot, true, cold}; }
static RiseCheckInputs off(float hot, float cold) { return RiseCheckInputs{false, true, hot, true, cold}; }
static RiseCheckInputs coldBad(float hot) { return RiseCheckInputs{true, true, hot, false, NAN}; }

static bool b1(const RiseCheckInputs& in, uint64_t t) {
    bool r = check.update(in, settings.b1, t);
    TEST_ASSERT_EQUAL(check.active(), r);
    return r;
}

// ---- settings --------------------------------------------------------------

static void test_defaults_equal_table() {
    const BoilerRoomDiagSettings d = defaultBoilerRoomDiagSettings();
    TEST_ASSERT_TRUE(d.b1.enabled);
    TEST_ASSERT_EQUAL_UINT32(3, d.b1.minOnMin);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, d.b1.deltaC);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, d.b1.minRiseC);
    TEST_ASSERT_TRUE(d.b3.enabled);
    TEST_ASSERT_EQUAL_UINT32(10, d.b3.minOnMin);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, d.b3.deltaC);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, d.b3.minRiseC);
    TEST_ASSERT_TRUE(d.b6.enabled);
    TEST_ASSERT_EQUAL_UINT32(5, d.b6.minOnMin);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, d.b6.deltaC);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, d.b6.minRiseC);

    // Cross-check against the rows themselves (4 rows per check, En first).
    const RiseCheckParams* p[3] = {&d.b1, &d.b3, &d.b6};
    for (size_t c = 0; c < 3; ++c) {
        const SettingDescriptor* r = &BOILER_ROOM_DIAG_SETTINGS[c * 4];
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingType::Bool), static_cast<int>(r[0].type));
        TEST_ASSERT_EQUAL(r[0].defaultValue != 0.0f, p[c]->enabled);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SettingType::Int), static_cast<int>(r[1].type));
        TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(r[1].defaultValue), p[c]->minOnMin);
        TEST_ASSERT_EQUAL_FLOAT(r[2].defaultValue, p[c]->deltaC);
        TEST_ASSERT_EQUAL_FLOAT(r[3].defaultValue, p[c]->minRiseC);
    }
}

static void test_settings_table_shape() {
    const char* keys[BOILER_ROOM_DIAG_SETTING_COUNT] = {BR_KEY_B1_EN, BR_KEY_B1_MIN_ON, BR_KEY_B1_DELTA,
        BR_KEY_B1_MIN_RISE, BR_KEY_B3_EN, BR_KEY_B3_MIN_ON, BR_KEY_B3_DELTA, BR_KEY_B3_MIN_RISE, BR_KEY_B6_EN,
        BR_KEY_B6_MIN_ON, BR_KEY_B6_DELTA, BR_KEY_B6_MIN_RISE};
    for (size_t i = 0; i < BOILER_ROOM_DIAG_SETTING_COUNT; ++i) {
        const SettingDescriptor& d = BOILER_ROOM_DIAG_SETTINGS[i];
        TEST_ASSERT_EQUAL_STRING(keys[i], d.key);
        TEST_ASSERT_EQUAL_STRING(d.key, d.nvsKey);
        TEST_ASSERT_TRUE(strlen(d.key) <= 15);
        TEST_ASSERT_EQUAL_STRING("diag", d.group);
        TEST_ASSERT_EQUAL_UINT8(0, d.flags & SETTING_FLAG_HA_SWITCH);
        for (size_t j = i + 1; j < BOILER_ROOM_DIAG_SETTING_COUNT; ++j) {
            TEST_ASSERT_TRUE(strcmp(d.key, BOILER_ROOM_DIAG_SETTINGS[j].key) != 0);
        }
    }
}

// ---- PumpRiseCheck (B1 parameters) -----------------------------------------

static void test_b1_triggers_at_exactly_min_on() {
    TEST_ASSERT_FALSE(b1(on(60.0f, 40.0f), 0));  // ON edge: baseline 40
    TEST_ASSERT_TRUE(check.tracking());
    TEST_ASSERT_EQUAL_FLOAT(40.0f, check.baselineC());
    TEST_ASSERT_FALSE(b1(on(61.0f, 41.0f), 3 * MIN - SEC));  // 2:59, dT 20, rise 1
    TEST_ASSERT_TRUE(b1(on(61.0f, 41.0f), 3 * MIN));          // 3:00
}

static void test_b1_no_trigger_when_cold_rose_enough() {
    b1(on(60.0f, 40.0f), 0);
    TEST_ASSERT_FALSE(b1(on(62.0f, 42.0f), 3 * MIN));  // rise exactly 2 -> not < 2
    TEST_ASSERT_FALSE(b1(on(65.0f, 43.0f), 10 * MIN));
}

static void test_b1_no_trigger_when_delta_small() {
    b1(on(55.0f, 40.0f), 0);
    TEST_ASSERT_FALSE(b1(on(56.0f, 41.0f), 3 * MIN));  // dT exactly 15 -> not > 15
    TEST_ASSERT_FALSE(b1(on(50.0f, 40.0f), 20 * MIN));
}

static void test_b1_auto_clears_on_rise() {
    b1(on(60.0f, 40.0f), 0);
    TEST_ASSERT_TRUE(b1(on(61.0f, 41.0f), 3 * MIN));
    TEST_ASSERT_FALSE(b1(on(62.0f, 42.0f), 4 * MIN));  // rise reached 2
    TEST_ASSERT_TRUE(check.tracking());
}

static void test_b1_auto_clears_on_pump_stop() {
    b1(on(60.0f, 40.0f), 0);
    TEST_ASSERT_TRUE(b1(on(61.0f, 41.0f), 3 * MIN));
    TEST_ASSERT_FALSE(b1(off(61.0f, 41.0f), 4 * MIN));
    TEST_ASSERT_FALSE(check.tracking());
}

static void test_b1_auto_clears_when_delta_closes() {
    b1(on(60.0f, 40.0f), 0);
    TEST_ASSERT_TRUE(b1(on(61.0f, 41.0f), 3 * MIN));
    TEST_ASSERT_FALSE(b1(on(55.0f, 41.0f), 4 * MIN));  // dT 14
    TEST_ASSERT_TRUE(b1(on(61.0f, 41.0f), 5 * MIN));   // re-opens against the same baseline
}

static void test_b1_disabled_never_raised_reenable_uses_original_baseline() {
    settings.b1.enabled = false;
    b1(on(60.0f, 40.0f), 0);
    TEST_ASSERT_FALSE(b1(on(61.0f, 41.0f), 3 * MIN));
    TEST_ASSERT_FALSE(b1(on(61.0f, 41.0f), 20 * MIN));
    TEST_ASSERT_TRUE(check.tracking());                 // D4: tracking continues
    TEST_ASSERT_EQUAL_FLOAT(40.0f, check.baselineC());
    settings.b1.enabled = true;
    TEST_ASSERT_TRUE(b1(on(61.0f, 41.0f), 20 * MIN + SEC));  // immediately, original baseline
    TEST_ASSERT_EQUAL_FLOAT(40.0f, check.baselineC());
}

static void test_b1_cold_fault_pauses_and_restarts_with_new_baseline() {
    b1(on(60.0f, 40.0f), 0);
    TEST_ASSERT_TRUE(b1(on(61.0f, 41.0f), 3 * MIN));
    TEST_ASSERT_FALSE(b1(coldBad(61.0f), 4 * MIN));  // fault: inactive, timer + baseline reset
    TEST_ASSERT_FALSE(check.tracking());
    TEST_ASSERT_FALSE(b1(coldBad(61.0f), 10 * MIN));
    TEST_ASSERT_FALSE(b1(on(65.0f, 45.0f), 11 * MIN));  // recovery: new start + baseline 45
    TEST_ASSERT_TRUE(check.tracking());
    TEST_ASSERT_EQUAL_FLOAT(45.0f, check.baselineC());
    TEST_ASSERT_FALSE(b1(on(65.0f, 45.5f), 14 * MIN - SEC));
    TEST_ASSERT_TRUE(b1(on(65.0f, 45.5f), 14 * MIN));
}

static void test_b1_hot_fault_pauses() {
    b1(on(60.0f, 40.0f), 0);
    TEST_ASSERT_TRUE(b1(on(61.0f, 41.0f), 3 * MIN));
    RiseCheckInputs in = on(NAN, 41.0f);
    in.hotOk = false;
    TEST_ASSERT_FALSE(b1(in, 4 * MIN));
    TEST_ASSERT_FALSE(check.tracking());
}

static void test_b1_pump_restart_resets_timer_and_baseline() {
    b1(on(60.0f, 40.0f), 0);
    b1(on(60.0f, 40.5f), 2 * MIN);
    b1(off(60.0f, 43.0f), 2 * MIN + 10 * SEC);
    TEST_ASSERT_FALSE(check.tracking());
    TEST_ASSERT_FALSE(b1(on(63.0f, 43.0f), 3 * MIN));  // new ON edge: baseline 43
    TEST_ASSERT_EQUAL_FLOAT(43.0f, check.baselineC());
    TEST_ASSERT_FALSE(b1(on(63.0f, 44.0f), 6 * MIN - SEC));
    TEST_ASSERT_TRUE(b1(on(63.0f, 44.0f), 6 * MIN));
}

static void test_b1_short_exercise_run_never_raises() {
    for (uint64_t t = 0; t <= 30 * SEC; t += SEC) {
        TEST_ASSERT_FALSE(b1(on(80.0f, 30.0f), t));  // worst case: huge dT, no rise
    }
    for (uint64_t t = 31 * SEC; t <= 60 * MIN; t += 30 * SEC) {
        TEST_ASSERT_FALSE(b1(off(80.0f, 30.0f), t));
    }
}

static void test_b1_requested_but_not_actual_never_tracks() {
    // The relay lock delays the actual ON: the check sees pumpOn = false (A1).
    for (uint64_t t = 0; t <= 10 * MIN; t += 30 * SEC) {
        TEST_ASSERT_FALSE(b1(off(80.0f, 30.0f), t));
        TEST_ASSERT_FALSE(check.tracking());
    }
}

// ---- BoilerRoomDiagnostics composition -------------------------------------

static void pumps(bool p1, bool p2, bool p3) {
    din.pumpActual[BR_PUMP_P1] = p1;
    din.pumpActual[BR_PUMP_P2] = p2;
    din.pumpActual[BR_PUMP_P3] = p3;
}
static void temps(float t1, float t2, float t3, float t6) {
    din.sensor[BR_SENSOR_T1] = ok(t1);
    din.sensor[BR_SENSOR_T2] = ok(t2);
    din.sensor[BR_SENSOR_T3] = ok(t3);
    din.sensor[BR_SENSOR_T6] = ok(t6);
}
static uint32_t step(uint64_t t) { return diag.update(din, settings, t); }

static const uint32_t B1_BIT = 1u << BR_WARN_B1;
static const uint32_t B3_BIT = 1u << BR_WARN_B3;
static const uint32_t B6_BIT = 1u << BR_WARN_B6;

static void test_bits_and_owned_mask() {
    TEST_ASSERT_EQUAL_UINT8(0, BR_WARN_B1);
    TEST_ASSERT_EQUAL_UINT8(1, BR_WARN_B3);
    TEST_ASSERT_EQUAL_UINT8(2, BR_WARN_B6);
    TEST_ASSERT_EQUAL_HEX32(0x000000FFu, BR_WARN_OWNED_MASK);
    TEST_ASSERT_EQUAL_HEX32(B1_BIT | B3_BIT | B6_BIT, (B1_BIT | B3_BIT | B6_BIT) & BR_WARN_OWNED_MASK);
    TEST_ASSERT_FALSE(diag.check(7).tracking());  // unknown bit -> idle check
    TEST_ASSERT_FALSE(diag.check(7).active());
}

static void test_diag_b1_uses_p3_t3_t6() {
    pumps(false, false, true);
    temps(70.0f, 40.0f, 60.0f, 40.0f);
    TEST_ASSERT_EQUAL_HEX32(0, step(0));
    TEST_ASSERT_TRUE(diag.check(BR_WARN_B1).tracking());
    TEST_ASSERT_FALSE(diag.check(BR_WARN_B3).tracking());
    TEST_ASSERT_FALSE(diag.check(BR_WARN_B6).tracking());
    temps(70.0f, 40.0f, 61.0f, 41.0f);
    TEST_ASSERT_EQUAL_HEX32(0, step(3 * MIN - SEC));
    TEST_ASSERT_EQUAL_HEX32(B1_BIT, step(3 * MIN));
    pumps(false, false, false);
    TEST_ASSERT_EQUAL_HEX32(0, step(3 * MIN + SEC));
}

static void test_diag_b1_actual_off_never_tracks() {
    // P3 requested but held off by the relay lock: the actual flag stays false.
    pumps(false, false, false);
    temps(70.0f, 40.0f, 60.0f, 40.0f);
    for (uint64_t t = 0; t <= 10 * MIN; t += 30 * SEC) {
        TEST_ASSERT_EQUAL_HEX32(0, step(t));
        TEST_ASSERT_FALSE(diag.check(BR_WARN_B1).tracking());
    }
}

static void test_diag_b1_t6_unknown_and_unassigned_pause() {
    pumps(false, false, true);
    temps(70.0f, 40.0f, 60.0f, 40.0f);
    step(0);
    TEST_ASSERT_EQUAL_HEX32(B1_BIT, step(3 * MIN));
    din.sensor[BR_SENSOR_T6] = unknown();
    TEST_ASSERT_EQUAL_HEX32(0, step(4 * MIN));
    TEST_ASSERT_FALSE(diag.check(BR_WARN_B1).tracking());
    din.sensor[BR_SENSOR_T6] = ok(40.0f);
    TEST_ASSERT_EQUAL_HEX32(0, step(5 * MIN));  // restarted at recovery
    TEST_ASSERT_TRUE(diag.check(BR_WARN_B1).tracking());
    TEST_ASSERT_EQUAL_HEX32(B1_BIT, step(8 * MIN));
    din.sensor[BR_SENSOR_T6] = unassigned();
    TEST_ASSERT_EQUAL_HEX32(0, step(9 * MIN));
    TEST_ASSERT_FALSE(diag.check(BR_WARN_B1).tracking());
    din.sensor[BR_SENSOR_T6] = fault();
    TEST_ASSERT_EQUAL_HEX32(0, step(20 * MIN));
    TEST_ASSERT_FALSE(diag.check(BR_WARN_B1).tracking());
}

static void test_diag_b3_trigger() {
    pumps(true, false, false);
    temps(80.0f, 40.0f, 50.0f, 40.0f);
    step(0);
    temps(80.0f, 40.0f, 50.5f, 40.0f);
    TEST_ASSERT_EQUAL_HEX32(0, step(10 * MIN - SEC));
    TEST_ASSERT_EQUAL_HEX32(B3_BIT, step(10 * MIN));
}

static void test_diag_b3_no_trigger_when_t3_rose() {
    pumps(true, false, false);
    temps(80.0f, 40.0f, 50.0f, 40.0f);
    step(0);
    temps(80.0f, 40.0f, 51.0f, 40.0f);  // rise exactly 1 -> not < 1
    TEST_ASSERT_EQUAL_HEX32(0, step(10 * MIN));
    TEST_ASSERT_EQUAL_HEX32(0, step(30 * MIN));
}

static void test_diag_b3_t1_fault_pauses() {
    pumps(true, false, false);
    temps(80.0f, 40.0f, 50.0f, 40.0f);
    step(0);
    TEST_ASSERT_EQUAL_HEX32(B3_BIT, step(10 * MIN));
    din.sensor[BR_SENSOR_T1] = fault();
    TEST_ASSERT_EQUAL_HEX32(0, step(11 * MIN));
    TEST_ASSERT_FALSE(diag.check(BR_WARN_B3).tracking());
}

static void test_diag_b6_trigger() {
    pumps(false, true, false);
    temps(70.0f, 50.0f, 40.0f, 40.0f);
    step(0);
    temps(70.0f, 51.0f, 40.0f, 40.0f);
    TEST_ASSERT_EQUAL_HEX32(0, step(5 * MIN - SEC));
    TEST_ASSERT_EQUAL_HEX32(B6_BIT, step(5 * MIN));
}

static void test_diag_b6_no_trigger_when_t2_rose() {
    pumps(false, true, false);
    temps(70.0f, 50.0f, 40.0f, 40.0f);
    step(0);
    temps(70.0f, 52.0f, 40.0f, 40.0f);  // rise exactly 2 -> not < 2
    TEST_ASSERT_EQUAL_HEX32(0, step(5 * MIN));
}

static void test_diag_b6_t2_unknown_pauses() {
    pumps(false, true, false);
    temps(70.0f, 50.0f, 40.0f, 40.0f);
    step(0);
    TEST_ASSERT_EQUAL_HEX32(B6_BIT, step(5 * MIN));
    din.sensor[BR_SENSOR_T2] = unknown();
    TEST_ASSERT_EQUAL_HEX32(0, step(6 * MIN));
    TEST_ASSERT_FALSE(diag.check(BR_WARN_B6).tracking());
}

static void test_diag_two_checks_at_once() {
    pumps(true, false, true);
    temps(80.0f, 40.0f, 60.0f, 40.0f);  // B3: T1 - T3 = 20; B1: T3 - T6 = 20
    step(0);
    temps(80.0f, 40.0f, 60.5f, 40.5f);
    TEST_ASSERT_EQUAL_HEX32(B1_BIT, step(3 * MIN));
    TEST_ASSERT_EQUAL_HEX32(B1_BIT | B3_BIT, step(10 * MIN));
    settings.b1.enabled = false;
    TEST_ASSERT_EQUAL_HEX32(B3_BIT, step(11 * MIN));
}

static void test_history_samples_every_30s_with_nan_for_non_ok() {
    TEST_ASSERT_EQUAL_UINT8(BR_SENSOR_COUNT, diag.history().sensorCount());
    TEST_ASSERT_EQUAL_UINT16(0, diag.history().size());
    temps(71.0f, 52.0f, 63.0f, 44.0f);
    din.sensor[BR_SENSOR_T4] = fault();
    din.sensor[BR_SENSOR_T5] = unknown();
    step(0);  // first tick samples immediately
    TEST_ASSERT_EQUAL_UINT16(1, diag.history().size());
    TEST_ASSERT_EQUAL_FLOAT(71.0f, diag.history().at(BR_SENSOR_T1, 0));
    TEST_ASSERT_EQUAL_FLOAT(52.0f, diag.history().at(BR_SENSOR_T2, 0));
    TEST_ASSERT_EQUAL_FLOAT(63.0f, diag.history().at(BR_SENSOR_T3, 0));
    TEST_ASSERT_EQUAL_FLOAT(44.0f, diag.history().at(BR_SENSOR_T6, 0));
    TEST_ASSERT_TRUE(isnan(diag.history().at(BR_SENSOR_T4, 0)));
    TEST_ASSERT_TRUE(isnan(diag.history().at(BR_SENSOR_T5, 0)));

    // A non-Ok state is stored as NAN,
    // whatever tempC carries.
    din.sensor[BR_SENSOR_T1] = SensorInput{SensorState::Unassigned, 99.0f, false};
    step(29 * SEC);
    TEST_ASSERT_EQUAL_UINT16(1, diag.history().size());
    step(30 * SEC);
    TEST_ASSERT_EQUAL_UINT16(2, diag.history().size());
    TEST_ASSERT_TRUE(isnan(diag.history().at(BR_SENSOR_T1, 0)));
    TEST_ASSERT_EQUAL_FLOAT(71.0f, diag.history().at(BR_SENSOR_T1, 1));
    step(59 * SEC);
    TEST_ASSERT_EQUAL_UINT16(2, diag.history().size());
    step(60 * SEC);
    TEST_ASSERT_EQUAL_UINT16(3, diag.history().size());
}

static void test_reset_clears_history_and_checks() {
    pumps(false, false, true);
    temps(70.0f, 40.0f, 60.0f, 40.0f);
    step(0);
    TEST_ASSERT_EQUAL_HEX32(B1_BIT, step(3 * MIN));
    diag.reset();
    TEST_ASSERT_EQUAL_UINT16(0, diag.history().size());
    TEST_ASSERT_FALSE(diag.check(BR_WARN_B1).tracking());
    TEST_ASSERT_FALSE(diag.check(BR_WARN_B1).active());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_equal_table);
    RUN_TEST(test_settings_table_shape);
    RUN_TEST(test_b1_triggers_at_exactly_min_on);
    RUN_TEST(test_b1_no_trigger_when_cold_rose_enough);
    RUN_TEST(test_b1_no_trigger_when_delta_small);
    RUN_TEST(test_b1_auto_clears_on_rise);
    RUN_TEST(test_b1_auto_clears_on_pump_stop);
    RUN_TEST(test_b1_auto_clears_when_delta_closes);
    RUN_TEST(test_b1_disabled_never_raised_reenable_uses_original_baseline);
    RUN_TEST(test_b1_cold_fault_pauses_and_restarts_with_new_baseline);
    RUN_TEST(test_b1_hot_fault_pauses);
    RUN_TEST(test_b1_pump_restart_resets_timer_and_baseline);
    RUN_TEST(test_b1_short_exercise_run_never_raises);
    RUN_TEST(test_b1_requested_but_not_actual_never_tracks);
    RUN_TEST(test_bits_and_owned_mask);
    RUN_TEST(test_diag_b1_uses_p3_t3_t6);
    RUN_TEST(test_diag_b1_actual_off_never_tracks);
    RUN_TEST(test_diag_b1_t6_unknown_and_unassigned_pause);
    RUN_TEST(test_diag_b3_trigger);
    RUN_TEST(test_diag_b3_no_trigger_when_t3_rose);
    RUN_TEST(test_diag_b3_t1_fault_pauses);
    RUN_TEST(test_diag_b6_trigger);
    RUN_TEST(test_diag_b6_no_trigger_when_t2_rose);
    RUN_TEST(test_diag_b6_t2_unknown_pauses);
    RUN_TEST(test_diag_two_checks_at_once);
    RUN_TEST(test_history_samples_every_30s_with_nan_for_non_ok);
    RUN_TEST(test_reset_clears_history_and_checks);
    return UNITY_END();
}
