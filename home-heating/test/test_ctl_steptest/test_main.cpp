#include <unity.h>
#include <math.h>
#include <string.h>
#include <SensorHistory.h>
#include <HomeHeatingTypes.h>
#include <K1StepTest.h>

// Stage 09 phase 7: K1 step test (C14, A10, D13, D15-D19). Pure unit fed tick
// by tick at 1 s. baseInputs() is a fully startable state: P4 requested and
// running, heating on, Normal, K1 known at 40 %, idle, sensors Ok, history
// full and steady; settings: travel 120 s, pulse 10 s.

static K1StepTest st;
static StepTestInputs in;
static StepTestSettings cfg;

static StepTestInputs baseInputs() {
    StepTestInputs i{};
    i.available = true;
    i.startRequest = false;
    i.cancelRequest = false;
    i.heatingEnabled = true;
    i.p4Requested = true;
    i.p4RelayActual = true;
    i.inhibited = false;
    i.k1AntiSeizeOwned = false;
    i.k1Busy = false;
    i.k1CmdThisTick = false;
    i.k1Mode = K1Mode::Normal;
    i.k1Known = true;
    i.k1PosPct = 40.0f;
    i.recalStarted = false;
    i.h1 = SensorHealth::Ok;
    i.h2 = SensorHealth::Ok;
    i.h3 = SensorHealth::Ok;
    i.h2C = 40.0f;
    i.h3C = 60.0f;
    i.steady = StepSteadiness{true, true, true};
    return i;
}

void setUp() {
    st.reset();
    in = baseInputs();
    cfg = StepTestSettings{10u, 120u};
}
void tearDown() {}

static StepTestOutput tick(uint64_t nowS) {
    return st.update(in, cfg, nowS * 1000ULL);
}

static void assertBlock(StepBlock b, const StepTestOutput& o) {
    TEST_ASSERT_EQUAL_STRING(stepBlockKey(b), stepBlockKey(o.block));
}

// Starts a test at t = 0 s; returns the start output.
static StepTestOutput startAt0() {
    in.startRequest = true;
    const StepTestOutput o = tick(0);
    in.startRequest = false;
    TEST_ASSERT_TRUE(o.started);
    return o;
}

// ---- keys ------------------------------------------------------------------------

void test_keys() {
    TEST_ASSERT_EQUAL_STRING("none", stepBlockKey(StepBlock::None));
    TEST_ASSERT_EQUAL_STRING("unavailable", stepBlockKey(StepBlock::Unavailable));
    TEST_ASSERT_EQUAL_STRING("running", stepBlockKey(StepBlock::Running));
    TEST_ASSERT_EQUAL_STRING("heating_off", stepBlockKey(StepBlock::HeatingOff));
    TEST_ASSERT_EQUAL_STRING("p4_off", stepBlockKey(StepBlock::P4Off));
    TEST_ASSERT_EQUAL_STRING("ota", stepBlockKey(StepBlock::Ota));
    TEST_ASSERT_EQUAL_STRING("anti_seize", stepBlockKey(StepBlock::AntiSeize));
    TEST_ASSERT_EQUAL_STRING("sensors", stepBlockKey(StepBlock::Sensors));
    TEST_ASSERT_EQUAL_STRING("k1_mode", stepBlockKey(StepBlock::K1Mode));
    TEST_ASSERT_EQUAL_STRING("k1_unknown", stepBlockKey(StepBlock::K1Unknown));
    TEST_ASSERT_EQUAL_STRING("k1_busy", stepBlockKey(StepBlock::K1Busy));
    TEST_ASSERT_EQUAL_STRING("k1_headroom", stepBlockKey(StepBlock::K1Headroom));
    TEST_ASSERT_EQUAL_STRING("history", stepBlockKey(StepBlock::History));
    TEST_ASSERT_EQUAL_STRING("h3_unsteady", stepBlockKey(StepBlock::H3Unsteady));
    TEST_ASSERT_EQUAL_STRING("h2_unsteady", stepBlockKey(StepBlock::H2Unsteady));
    TEST_ASSERT_EQUAL_STRING("none", stepAbortKey(StepAbort::None));
    TEST_ASSERT_EQUAL_STRING("cancel", stepAbortKey(StepAbort::Cancel));
    TEST_ASSERT_EQUAL_STRING("heating_off", stepAbortKey(StepAbort::HeatingOff));
    TEST_ASSERT_EQUAL_STRING("p4_off", stepAbortKey(StepAbort::P4Off));
    TEST_ASSERT_EQUAL_STRING("ota", stepAbortKey(StepAbort::Ota));
    TEST_ASSERT_EQUAL_STRING("anti_seize", stepAbortKey(StepAbort::AntiSeize));
    TEST_ASSERT_EQUAL_STRING("recal", stepAbortKey(StepAbort::Recal));
    TEST_ASSERT_EQUAL_STRING("sensors", stepAbortKey(StepAbort::Sensors));
    TEST_ASSERT_EQUAL_STRING("k1_mode", stepAbortKey(StepAbort::K1Mode));
    TEST_ASSERT_EQUAL_STRING("h3_changed", stepAbortKey(StepAbort::H3Changed));
    TEST_ASSERT_EQUAL_STRING("none", stepOutcomeKey(StepOutcome::None));
    TEST_ASSERT_EQUAL_STRING("result", stepOutcomeKey(StepOutcome::Result));
    TEST_ASSERT_EQUAL_STRING("no_response", stepOutcomeKey(StepOutcome::NoResponse));
    TEST_ASSERT_EQUAL_STRING("aborted", stepOutcomeKey(StepOutcome::Aborted));
    // Stable codes.
    TEST_ASSERT_EQUAL_INT(14, static_cast<int>(StepBlock::H2Unsteady));
    TEST_ASSERT_EQUAL_INT(9, static_cast<int>(StepAbort::H3Changed));
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(StepOutcome::Aborted));
}

// ---- blocks ----------------------------------------------------------------------

void test_block_none_when_startable() {
    const StepTestOutput o = tick(0);
    assertBlock(StepBlock::None, o);
    TEST_ASSERT_FALSE(o.pulse.issue);
    TEST_ASSERT_FALSE(st.running());
}

void test_block_unavailable_first() {
    in.available = false;
    in.heatingEnabled = false;
    assertBlock(StepBlock::Unavailable, tick(0));
}

void test_block_heating_off() {
    in.heatingEnabled = false;
    in.p4Requested = false;
    assertBlock(StepBlock::HeatingOff, tick(0));
}

void test_block_p4_off_before_sensors() {
    in.p4Requested = false;
    in.h2 = SensorHealth::Failed;
    assertBlock(StepBlock::P4Off, tick(0));
    in.p4Requested = true;
    in.p4RelayActual = false;
    assertBlock(StepBlock::P4Off, tick(1));
}

void test_block_ota() {
    in.inhibited = true;
    in.k1AntiSeizeOwned = true;
    assertBlock(StepBlock::Ota, tick(0));
}

void test_block_ota_before_p4() {
    in.inhibited = true;
    in.p4RelayActual = false;   // the inhibit forces the P4 relay OFF too
    assertBlock(StepBlock::Ota, tick(0));
}

void test_block_anti_seize() {
    in.k1AntiSeizeOwned = true;
    in.h1 = SensorHealth::Pending;
    assertBlock(StepBlock::AntiSeize, tick(0));
}

void test_block_sensors() {
    in.h1 = SensorHealth::Pending;
    in.k1Mode = K1Mode::Wait;
    assertBlock(StepBlock::Sensors, tick(0));
    in.h1 = SensorHealth::Ok;
    in.h3 = SensorHealth::Failed;
    assertBlock(StepBlock::Sensors, tick(1));
}

void test_block_k1_mode() {
    in.k1Mode = K1Mode::FeedbackOnly;
    in.k1Known = false;
    assertBlock(StepBlock::K1Mode, tick(0));
}

void test_block_k1_unknown() {
    in.k1Known = false;
    in.k1Busy = true;
    assertBlock(StepBlock::K1Unknown, tick(0));
}

void test_block_k1_busy() {
    in.k1Busy = true;
    assertBlock(StepBlock::K1Busy, tick(0));
    in.k1Busy = false;
    in.k1CmdThisTick = true;
    assertBlock(StepBlock::K1Busy, tick(1));
}

void test_block_k1_headroom() {
    in.k1PosPct = 91.6f;                    // 91.6 + 8.33 = 99.93 -> ok
    assertBlock(StepBlock::None, tick(0));
    in.k1PosPct = 92.0f;                    // 100.33 -> blocked
    in.steady.full = false;
    assertBlock(StepBlock::K1Headroom, tick(1));
    in.k1PosPct = 40.0f;
    cfg.travelS = 0u;                       // guard: no travel -> headroom
    assertBlock(StepBlock::K1Headroom, tick(2));
}

void test_block_history() {
    in.steady = StepSteadiness{false, false, false};
    assertBlock(StepBlock::History, tick(0));
}

void test_block_h3_unsteady() {
    in.steady = StepSteadiness{true, false, false};
    assertBlock(StepBlock::H3Unsteady, tick(0));
}

void test_block_h2_unsteady() {
    in.steady = StepSteadiness{true, true, false};
    assertBlock(StepBlock::H2Unsteady, tick(0));
}

void test_start_request_while_blocked_dropped() {
    in.k1Busy = true;
    in.startRequest = true;
    const StepTestOutput o = tick(0);
    assertBlock(StepBlock::K1Busy, o);
    TEST_ASSERT_FALSE(o.pulse.issue);
    TEST_ASSERT_FALSE(o.started);
    TEST_ASSERT_FALSE(st.running());
    in.startRequest = false;                // not latched: unblocking later starts nothing
    in.k1Busy = false;
    TEST_ASSERT_FALSE(tick(1).started);
    TEST_ASSERT_FALSE(st.running());
}

// ---- start -----------------------------------------------------------------------

void test_start_issues_open_pulse() {
    in.startRequest = true;
    const StepTestOutput o = tick(0);
    TEST_ASSERT_TRUE(o.started);
    TEST_ASSERT_FALSE(o.ended);
    TEST_ASSERT_TRUE(o.pulse.issue);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Open), static_cast<int>(o.pulse.dir));
    TEST_ASSERT_EQUAL_UINT32(10000u, o.pulse.ms);
    assertBlock(StepBlock::Running, o);
    TEST_ASSERT_TRUE(st.running());
    TEST_ASSERT_EQUAL_UINT32(10u, st.pulseS());
    TEST_ASSERT_EQUAL_UINT32(0u, st.elapsedS(0));
    TEST_ASSERT_EQUAL_UINT32(5u, st.elapsedS(5500));
    TEST_ASSERT_FALSE(st.deadTimeSeen());
}

void test_running_blocks_and_second_start_ignored() {
    startAt0();
    in.k1Busy = true;                       // the pulse itself: not an abort
    in.startRequest = true;
    const StepTestOutput o = tick(1);
    assertBlock(StepBlock::Running, o);
    TEST_ASSERT_FALSE(o.started);
    TEST_ASSERT_FALSE(o.pulse.issue);
    TEST_ASSERT_TRUE(st.running());
}

void test_cancel_while_idle_ignored() {
    in.cancelRequest = true;
    const StepTestOutput o = tick(0);
    TEST_ASSERT_FALSE(o.ended);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::None), static_cast<int>(st.last().outcome));
}

// ---- measurement -----------------------------------------------------------------

// Normal trace: flat 40.0 up to 18 s, +0.125 degC/s to 42.5 at 38 s, then flat.
static float normalH2(uint64_t t) {
    if (t <= 18u) return 40.0f;
    if (t <= 38u) return 40.0f + 0.125f * static_cast<float>(t - 18u);
    return 42.5f;
}

// Feeds h2(t) from t = 1 until the test ends (max maxS); returns the end tick (0 = none).
static uint64_t runUntilEnd(float (*h2)(uint64_t), uint64_t maxS) {
    for (uint64_t t = 1; t <= maxS; ++t) {
        in.h2C = h2(t);
        const StepTestOutput o = tick(t);
        if (o.ended) {
            TEST_ASSERT_FALSE(st.running());
            return t;
        }
    }
    return 0u;
}

void test_measure_normal_result() {
    startAt0();
    for (uint64_t t = 1; t <= 21u; ++t) {
        in.h2C = normalH2(t);
        TEST_ASSERT_FALSE(tick(t).ended);
        TEST_ASSERT_FALSE(st.deadTimeSeen());
    }
    in.h2C = normalH2(22);                  // 40.5: |dH2| = 0.5 -> dead time 22 s
    TEST_ASSERT_FALSE(tick(22).ended);
    TEST_ASSERT_TRUE(st.deadTimeSeen());
    TEST_ASSERT_EQUAL_FLOAT(22.0f, st.deadTimeS());
    // The window restarts during the ramp (last restart at 38 s) -> settles at 158 s.
    for (uint64_t t = 23; t <= 157u; ++t) {
        in.h2C = normalH2(t);
        TEST_ASSERT_FALSE(tick(t).ended);
    }
    in.h2C = normalH2(158);
    const StepTestOutput o = tick(158);
    TEST_ASSERT_TRUE(o.ended);
    TEST_ASSERT_FALSE(st.running());
    assertBlock(StepBlock::None, o);        // idle again: startable
    const StepTestResult& r = st.last();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Result), static_cast<int>(r.outcome));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepAbort::None), static_cast<int>(r.abort));
    TEST_ASSERT_EQUAL_FLOAT(22.0f, r.deadTimeS);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, r.responseCps);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, r.h2StartC);
    TEST_ASSERT_EQUAL_FLOAT(42.5f, r.h2SettledC);
    TEST_ASSERT_EQUAL_UINT32(10u, r.pulseS);
    TEST_ASSERT_TRUE(r.suggest.valid);
    TEST_ASSERT_EQUAL_UINT32(33u, r.suggest.periodS);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, r.suggest.gain);
    // Kept in last() while idle.
    in.h2C = 42.5f;
    TEST_ASSERT_FALSE(tick(159).ended);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Result), static_cast<int>(st.last().outcome));
    TEST_ASSERT_FALSE(st.deadTimeSeen());
    TEST_ASSERT_EQUAL_UINT32(0u, st.elapsedS(159000));
}

static float flatH2(uint64_t) { return 40.4f; }   // |dH2| = 0.4 < 0.5: no movement

void test_measure_no_response_at_600s() {
    startAt0();
    TEST_ASSERT_EQUAL_UINT64(600u, runUntilEnd(flatH2, 700u));
    const StepTestResult& r = st.last();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::NoResponse), static_cast<int>(r.outcome));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.deadTimeS);
    TEST_ASSERT_FALSE(r.suggest.valid);
}

// Falls 1 degC: 0.125 degC/s from 20 s to 39.0 at 28 s.
static float fallingH2(uint64_t t) {
    if (t <= 20u) return 40.0f;
    if (t <= 28u) return 40.0f - 0.125f * static_cast<float>(t - 20u);
    return 39.0f;
}

void test_measure_negative_no_response_keeps_dead_time() {
    startAt0();
    // Dead at 24 s (39.5); last window restart at 28 s -> settles at 148 s.
    TEST_ASSERT_EQUAL_UINT64(148u, runUntilEnd(fallingH2, 700u));
    const StepTestResult& r = st.last();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::NoResponse), static_cast<int>(r.outcome));
    TEST_ASSERT_EQUAL_FLOAT(24.0f, r.deadTimeS);
    TEST_ASSERT_EQUAL_FLOAT(39.0f, r.h2SettledC);
    TEST_ASSERT_FALSE(r.suggest.valid);
}

// Moves at 30 s, then swings 0.25 degC every tick: the window never spans 120 s.
static float swingingH2(uint64_t t) {
    if (t < 30u) return 40.0f;
    return (t % 2u) == 0u ? 41.0f : 41.25f;
}

void test_measure_slow_settle_takes_h2_at_600s() {
    startAt0();
    TEST_ASSERT_EQUAL_UINT64(600u, runUntilEnd(swingingH2, 700u));
    const StepTestResult& r = st.last();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Result), static_cast<int>(r.outcome));
    TEST_ASSERT_EQUAL_FLOAT(30.0f, r.deadTimeS);
    TEST_ASSERT_EQUAL_FLOAT(41.0f, r.h2SettledC);    // H2 at 600 s (even tick)
    TEST_ASSERT_EQUAL_FLOAT(0.1f, r.responseCps);
    TEST_ASSERT_TRUE(r.suggest.valid);
    TEST_ASSERT_EQUAL_UINT32(45u, r.suggest.periodS);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, r.suggest.gain);
}

// ---- aborts ----------------------------------------------------------------------

// Starts at 0, runs 5 flat ticks, applies `mutate` and ticks once at 6 s:
// the test must end Aborted with `reason`, idle, suggestion invalid.
static void assertAbort(void (*mutate)(), StepAbort reason) {
    startAt0();
    for (uint64_t t = 1; t <= 5u; ++t) {
        TEST_ASSERT_FALSE(tick(t).ended);
    }
    mutate();
    const StepTestOutput o = tick(6);
    TEST_ASSERT_TRUE(o.ended);
    TEST_ASSERT_FALSE(o.started);
    TEST_ASSERT_FALSE(o.pulse.issue);
    TEST_ASSERT_FALSE(st.running());
    TEST_ASSERT_TRUE(o.block != StepBlock::Running);
    const StepTestResult& r = st.last();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Aborted), static_cast<int>(r.outcome));
    TEST_ASSERT_EQUAL_STRING(stepAbortKey(reason), stepAbortKey(r.abort));
    TEST_ASSERT_FALSE(r.suggest.valid);
    TEST_ASSERT_EQUAL_UINT32(10u, r.pulseS);
    TEST_ASSERT_FALSE(tick(7).ended);       // one-tick edge
}

static void mCancel() { in.cancelRequest = true; }
static void mHeatingOff() { in.heatingEnabled = false; }
static void mP4RequestOff() { in.p4Requested = false; }
static void mP4ActualOff() { in.p4RelayActual = false; }
static void mOta() { in.inhibited = true; }
static void mAntiSeize() { in.k1AntiSeizeOwned = true; }
static void mRecalStarted() { in.recalStarted = true; }
static void mModeRecal() { in.k1Mode = K1Mode::Recalibrating; }
static void mH2Failed() { in.h2 = SensorHealth::Failed; }
static void mH1Pending() { in.h1 = SensorHealth::Pending; }
static void mModeLeavesNormal() { in.k1Mode = K1Mode::FeedbackOnly; }
static void mH3Up21() { in.h3C = 62.1f; }
static void mCancelAndHeatingOff() { in.cancelRequest = true; in.heatingEnabled = false; }

void test_abort_cancel() { assertAbort(mCancel, StepAbort::Cancel); }
void test_abort_cancel_first() { assertAbort(mCancelAndHeatingOff, StepAbort::Cancel); }
void test_abort_heating_off() { assertAbort(mHeatingOff, StepAbort::HeatingOff); }
void test_abort_p4_request_off() { assertAbort(mP4RequestOff, StepAbort::P4Off); }
void test_abort_p4_actual_off() { assertAbort(mP4ActualOff, StepAbort::P4Off); }
void test_abort_ota() { assertAbort(mOta, StepAbort::Ota); }
// review-9: the inhibit drops every relay, P4 actual included -> still "ota".
static void mOtaRelaysOff() { in.inhibited = true; in.p4RelayActual = false; }
void test_abort_ota_before_p4() { assertAbort(mOtaRelaysOff, StepAbort::Ota); }
void test_abort_anti_seize() { assertAbort(mAntiSeize, StepAbort::AntiSeize); }
void test_abort_recal_started() { assertAbort(mRecalStarted, StepAbort::Recal); }
void test_abort_mode_recalibrating() { assertAbort(mModeRecal, StepAbort::Recal); }
void test_abort_h2_failed() { assertAbort(mH2Failed, StepAbort::Sensors); }
void test_abort_h1_pending() { assertAbort(mH1Pending, StepAbort::Sensors); }
void test_abort_mode_leaves_normal() { assertAbort(mModeLeavesNormal, StepAbort::K1Mode); }
void test_abort_h3_changed() { assertAbort(mH3Up21, StepAbort::H3Changed); }

void test_h3_change_of_2_does_not_abort() {
    startAt0();
    in.h3C = 62.0f;                         // |dH3| = 2.0: not > 2
    TEST_ASSERT_FALSE(tick(1).ended);
    in.h3C = 58.0f;
    TEST_ASSERT_FALSE(tick(2).ended);
    TEST_ASSERT_TRUE(st.running());
}

void test_abort_after_dead_time_keeps_it() {
    startAt0();
    in.h2C = 40.5f;
    tick(20);
    TEST_ASSERT_TRUE(st.deadTimeSeen());
    in.cancelRequest = true;
    TEST_ASSERT_TRUE(tick(21).ended);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, st.last().deadTimeS);
    TEST_ASSERT_FALSE(st.deadTimeSeen());
}

void test_new_test_after_abort_restarts_measurement() {
    startAt0();
    in.h2C = 40.5f;
    tick(20);
    in.cancelRequest = true;
    tick(21);
    in.cancelRequest = false;
    in.startRequest = true;                 // new start at 40.5
    const StepTestOutput o = tick(22);
    TEST_ASSERT_TRUE(o.started);
    TEST_ASSERT_FALSE(st.deadTimeSeen());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Aborted), static_cast<int>(st.last().outcome));
}

// ---- suggestion (D18) ------------------------------------------------------------

void test_suggest_defaults_cross_check() {
    // R = 0.25 degC/s, Td = 20 s <-> the architecture defaults (gain 2, period 30).
    const StepSuggestion s = suggestK1Tuning(20.0f, 0.25f);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(30u, s.periodS);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, s.gain);
}

void test_suggest_worked_example_22s() {
    const StepSuggestion s = suggestK1Tuning(22.0f, 0.25f);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(33u, s.periodS);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, s.gain);
}

void test_suggest_period_clamps() {
    TEST_ASSERT_EQUAL_UINT32(10u, suggestK1Tuning(3.0f, 0.25f).periodS);     // 4.5 -> min 10
    TEST_ASSERT_EQUAL_UINT32(300u, suggestK1Tuning(250.0f, 0.25f).periodS);  // 375 -> max 300
    TEST_ASSERT_EQUAL_UINT32(300u, suggestK1Tuning(1.0e30f, 0.25f).periodS);
    TEST_ASSERT_EQUAL_UINT32(23u, suggestK1Tuning(15.0f, 0.25f).periodS);    // 22.5 -> 23 (half away)
}

void test_suggest_gain_clamps_and_rounding() {
    TEST_ASSERT_EQUAL_FLOAT(10.0f, suggestK1Tuning(20.0f, 0.01f).gain);      // 50 -> max 10
    TEST_ASSERT_EQUAL_FLOAT(10.0f, suggestK1Tuning(20.0f, 1.0e-30f).gain);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, suggestK1Tuning(20.0f, 2.0f).gain);        // 0.25 -> min 0.5
    TEST_ASSERT_EQUAL_FLOAT(1.5f, suggestK1Tuning(20.0f, 0.3f).gain);        // 1.667 -> 1.5
    TEST_ASSERT_EQUAL_FLOAT(2.5f, suggestK1Tuning(20.0f, 0.2f).gain);        // 2.5 exact
    TEST_ASSERT_EQUAL_FLOAT(1.0f, suggestK1Tuning(20.0f, 0.45f).gain);       // 1.111 -> 1.0
    TEST_ASSERT_EQUAL_FLOAT(3.0f, suggestK1Tuning(20.0f, 0.16f).gain);       // 3.125 -> 3.0
}

void test_suggest_invalid_inputs() {
    TEST_ASSERT_FALSE(suggestK1Tuning(0.0f, 0.25f).valid);
    TEST_ASSERT_FALSE(suggestK1Tuning(-5.0f, 0.25f).valid);
    TEST_ASSERT_FALSE(suggestK1Tuning(NAN, 0.25f).valid);
    TEST_ASSERT_FALSE(suggestK1Tuning(INFINITY, 0.25f).valid);
    TEST_ASSERT_FALSE(suggestK1Tuning(20.0f, 0.0f).valid);
    TEST_ASSERT_FALSE(suggestK1Tuning(20.0f, -0.1f).valid);
    TEST_ASSERT_FALSE(suggestK1Tuning(20.0f, NAN).valid);
    TEST_ASSERT_FALSE(suggestK1Tuning(20.0f, INFINITY).valid);
}

// ---- stepSteadiness with a real SensorHistory ------------------------------------

static SensorHistory hist;

// Feeds n samples (30 s apart from t0S) with H2 / H3 values from the arrays.
static void feed(uint16_t n, const float* h2, const float* h3, const bool* h2Ok) {
    for (uint16_t k = 0; k < n; ++k) {
        const float v[HH_SENSOR_COUNT] = {30.0f, h2[k], h3[k], 50.0f};
        const bool ok[HH_SENSOR_COUNT] = {true, h2Ok == nullptr ? true : h2Ok[k], true, true};
        TEST_ASSERT_TRUE(hist.tick(static_cast<uint64_t>(k) * SENSOR_HISTORY_PERIOD_MS, v, ok));
    }
}

static float h2s[STEP_STEADY_SAMPLES];
static float h3s[STEP_STEADY_SAMPLES];

static void fillSteady() {
    hist.reset(HH_SENSOR_COUNT);
    for (uint16_t k = 0; k < STEP_STEADY_SAMPLES; ++k) {
        h2s[k] = 40.0f;
        h3s[k] = 60.0f;
    }
}

void test_steadiness_ten_samples_not_full() {
    fillSteady();
    feed(10, h2s, h3s, nullptr);
    const StepSteadiness s = stepSteadiness(hist, 40.0f, 60.0f);
    TEST_ASSERT_FALSE(s.full);
    TEST_ASSERT_FALSE(s.h3Steady);
    TEST_ASSERT_FALSE(s.h2Steady);
}

void test_steadiness_eleven_steady() {
    fillSteady();
    h2s[3] = 40.5f;                         // on the band edge: still steady
    h3s[4] = 59.0f;
    feed(11, h2s, h3s, nullptr);
    const StepSteadiness s = stepSteadiness(hist, 40.0f, 60.0f);
    TEST_ASSERT_TRUE(s.full);
    TEST_ASSERT_TRUE(s.h3Steady);
    TEST_ASSERT_TRUE(s.h2Steady);
}

void test_steadiness_h3_off_by_1_1() {
    fillSteady();
    h3s[5] = 61.1f;
    feed(11, h2s, h3s, nullptr);
    const StepSteadiness s = stepSteadiness(hist, 40.0f, 60.0f);
    TEST_ASSERT_TRUE(s.full);
    TEST_ASSERT_FALSE(s.h3Steady);
    TEST_ASSERT_TRUE(s.h2Steady);
}

void test_steadiness_h2_off_by_0_6_vs_live() {
    fillSteady();
    feed(11, h2s, h3s, nullptr);
    const StepSteadiness s = stepSteadiness(hist, 40.6f, 60.0f);   // live H2 moved away
    TEST_ASSERT_TRUE(s.full);
    TEST_ASSERT_TRUE(s.h3Steady);
    TEST_ASSERT_FALSE(s.h2Steady);
}

void test_steadiness_nan_sample() {
    fillSteady();
    bool ok[STEP_STEADY_SAMPLES];
    for (uint16_t k = 0; k < STEP_STEADY_SAMPLES; ++k) ok[k] = true;
    ok[7] = false;                          // stored as NAN
    feed(11, h2s, h3s, ok);
    const StepSteadiness s = stepSteadiness(hist, 40.0f, 60.0f);
    TEST_ASSERT_TRUE(s.full);
    TEST_ASSERT_TRUE(s.h3Steady);
    TEST_ASSERT_FALSE(s.h2Steady);
}

void test_steadiness_older_samples_ignored() {
    fillSteady();
    h2s[0] = 45.0f;                         // the 12th-newest sample is outside the window
    feed(1, h2s, h3s, nullptr);
    for (uint16_t k = 1; k <= STEP_STEADY_SAMPLES; ++k) {
        const float v[HH_SENSOR_COUNT] = {30.0f, 40.0f, 60.0f, 50.0f};
        const bool ok[HH_SENSOR_COUNT] = {true, true, true, true};
        TEST_ASSERT_TRUE(hist.tick(static_cast<uint64_t>(k) * SENSOR_HISTORY_PERIOD_MS, v, ok));
    }
    TEST_ASSERT_TRUE(stepSteadiness(hist, 40.0f, 60.0f).h2Steady);
}

// ---- review-7 carry-in: robustness ----------------------------------------------

void test_block_k1_headroom_non_finite_position() {
    in.k1PosPct = NAN;                      // NaN + x >= 100 is false: must still block
    assertBlock(StepBlock::K1Headroom, tick(0));
    in.k1PosPct = INFINITY;
    assertBlock(StepBlock::K1Headroom, tick(1));
    in.k1PosPct = -INFINITY;
    assertBlock(StepBlock::K1Headroom, tick(2));
    in.startRequest = true;                 // a start is dropped, no pulse
    const StepTestOutput o = tick(3);
    TEST_ASSERT_FALSE(o.started);
    TEST_ASSERT_FALSE(o.pulse.issue);
}

static float stepH2(uint64_t t) { return t < 22u ? 40.0f : 40.5f; }

void test_settle_boundary_119_vs_120s() {
    startAt0();
    for (uint64_t t = 1; t <= 22u; ++t) {   // dead time at 22 s; the window opens at 22 s
        in.h2C = stepH2(t);
        TEST_ASSERT_FALSE(tick(t).ended);
    }
    TEST_ASSERT_TRUE(st.deadTimeSeen());
    for (uint64_t t = 23; t <= 141u; ++t) { // 141 s = window + 119 s: not settled
        in.h2C = stepH2(t);
        TEST_ASSERT_FALSE(tick(t).ended);
    }
    TEST_ASSERT_TRUE(st.running());
    in.h2C = stepH2(142);                   // window + 120 s: settles
    TEST_ASSERT_TRUE(tick(142).ended);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Result), static_cast<int>(st.last().outcome));
    TEST_ASSERT_EQUAL_FLOAT(22.0f, st.last().deadTimeS);
    TEST_ASSERT_EQUAL_FLOAT(40.5f, st.last().h2SettledC);
}

void test_settle_window_backwards_time_guarded() {
    startAt0();
    for (uint64_t t = 1; t <= 22u; ++t) {   // window opens at 22 s
        in.h2C = stepH2(t);
        TEST_ASSERT_FALSE(tick(t).ended);
    }
    // A clock going backwards must not wrap into a "settled" window.
    TEST_ASSERT_FALSE(tick(10).ended);
    TEST_ASSERT_TRUE(st.running());
    TEST_ASSERT_FALSE(tick(23).ended);
    TEST_ASSERT_TRUE(st.running());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_keys);
    RUN_TEST(test_block_none_when_startable);
    RUN_TEST(test_block_unavailable_first);
    RUN_TEST(test_block_heating_off);
    RUN_TEST(test_block_p4_off_before_sensors);
    RUN_TEST(test_block_ota);
    RUN_TEST(test_block_ota_before_p4);
    RUN_TEST(test_block_anti_seize);
    RUN_TEST(test_block_sensors);
    RUN_TEST(test_block_k1_mode);
    RUN_TEST(test_block_k1_unknown);
    RUN_TEST(test_block_k1_busy);
    RUN_TEST(test_block_k1_headroom);
    RUN_TEST(test_block_history);
    RUN_TEST(test_block_h3_unsteady);
    RUN_TEST(test_block_h2_unsteady);
    RUN_TEST(test_start_request_while_blocked_dropped);
    RUN_TEST(test_start_issues_open_pulse);
    RUN_TEST(test_running_blocks_and_second_start_ignored);
    RUN_TEST(test_cancel_while_idle_ignored);
    RUN_TEST(test_measure_normal_result);
    RUN_TEST(test_measure_no_response_at_600s);
    RUN_TEST(test_measure_negative_no_response_keeps_dead_time);
    RUN_TEST(test_measure_slow_settle_takes_h2_at_600s);
    RUN_TEST(test_abort_cancel);
    RUN_TEST(test_abort_cancel_first);
    RUN_TEST(test_abort_heating_off);
    RUN_TEST(test_abort_p4_request_off);
    RUN_TEST(test_abort_p4_actual_off);
    RUN_TEST(test_abort_ota);
    RUN_TEST(test_abort_ota_before_p4);
    RUN_TEST(test_abort_anti_seize);
    RUN_TEST(test_abort_recal_started);
    RUN_TEST(test_abort_mode_recalibrating);
    RUN_TEST(test_abort_h2_failed);
    RUN_TEST(test_abort_h1_pending);
    RUN_TEST(test_abort_mode_leaves_normal);
    RUN_TEST(test_abort_h3_changed);
    RUN_TEST(test_h3_change_of_2_does_not_abort);
    RUN_TEST(test_abort_after_dead_time_keeps_it);
    RUN_TEST(test_new_test_after_abort_restarts_measurement);
    RUN_TEST(test_suggest_defaults_cross_check);
    RUN_TEST(test_suggest_worked_example_22s);
    RUN_TEST(test_suggest_period_clamps);
    RUN_TEST(test_suggest_gain_clamps_and_rounding);
    RUN_TEST(test_suggest_invalid_inputs);
    RUN_TEST(test_steadiness_ten_samples_not_full);
    RUN_TEST(test_steadiness_eleven_steady);
    RUN_TEST(test_steadiness_h3_off_by_1_1);
    RUN_TEST(test_steadiness_h2_off_by_0_6_vs_live);
    RUN_TEST(test_steadiness_nan_sample);
    RUN_TEST(test_steadiness_older_samples_ignored);
    RUN_TEST(test_block_k1_headroom_non_finite_position);
    RUN_TEST(test_settle_boundary_119_vs_120s);
    RUN_TEST(test_settle_window_backwards_time_guarded);
    return UNITY_END();
}
