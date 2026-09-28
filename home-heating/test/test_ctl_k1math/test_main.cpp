#include <unity.h>
#include <HomeHeatingControlSettings.h>
#include <HomeHeatingTypes.h>
#include <K1Math.h>

// Stage 08 phase 3: K1 math (C6) -- position estimator, feed-forward target and
// move planners. Defaults: travel 120 s, overdrive 12 s, min pulse 1 s, gain 2,
// max pulse 10 s, deadband 1 degC.

static K1Estimator est;
static HomeHeatingSettings settings;
static const uint32_t TRAVEL_MS = 120000u;
static const uint32_t OVERDRIVE_MS = 12000u;

void setUp() {
    est.reset();
    settings = guardHomeHeatingSettings(defaultHomeHeatingSettings());
}
void tearDown() {}

static void assertNone(const K1Move& m) {
    TEST_ASSERT_FALSE(m.issue);
}

static void assertMove(const K1Move& m, K1Direction dir, uint32_t ms, bool resync) {
    TEST_ASSERT_TRUE(m.issue);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(dir), static_cast<int>(m.dir));
    TEST_ASSERT_EQUAL_UINT32(ms, m.ms);
    TEST_ASSERT_EQUAL(resync, m.resync);
}

// ---- defaults sanity -------------------------------------------------------

void test_defaults_used_by_this_suite() {
    TEST_ASSERT_EQUAL_UINT32(TRAVEL_MS, k1TravelMs(settings));
    TEST_ASSERT_EQUAL_UINT32(OVERDRIVE_MS, k1OverdriveMs(settings));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, settings.k1MinPulseS);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, settings.k1Gain);
    TEST_ASSERT_EQUAL_UINT32(10u, settings.k1MaxPulseS);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, settings.k1Deadband);
}

// ---- estimator -------------------------------------------------------------

void test_estimator_unknown_after_reset() {
    TEST_ASSERT_FALSE(est.known());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, est.pct());
    est.setKnown(40.0f);
    est.reset();
    TEST_ASSERT_FALSE(est.known());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, est.pct());
}

void test_estimator_open_half() {
    est.setKnown(0.0f);
    est.applyMotion(K1Motion{60000u, 0u}, TRAVEL_MS);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, est.pct());
    TEST_ASSERT_TRUE(est.known());
}

void test_estimator_close_clamps_to_zero() {
    est.setKnown(50.0f);
    est.applyMotion(K1Motion{0u, 90000u}, TRAVEL_MS);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, est.pct());
}

void test_estimator_open_clamps_to_hundred() {
    est.setKnown(0.0f);
    est.applyMotion(K1Motion{200000u, 0u}, TRAVEL_MS);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, est.pct());
}

void test_estimator_mixed_motion_is_net() {
    est.setKnown(10.0f);
    est.applyMotion(K1Motion{30000u, 6000u}, TRAVEL_MS);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, est.pct());
}

void test_estimator_set_known_zero_and_clamp() {
    est.setKnown(0.0f);
    TEST_ASSERT_TRUE(est.known());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, est.pct());
    est.setKnown(150.0f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, est.pct());
    est.setKnown(-3.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, est.pct());
}

void test_estimator_integrates_while_unknown() {
    est.applyMotion(K1Motion{30000u, 0u}, TRAVEL_MS);
    TEST_ASSERT_FALSE(est.known());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, est.pct());
}

void test_estimator_zero_travel_ignored() {
    est.setKnown(20.0f);
    est.applyMotion(K1Motion{30000u, 0u}, 0u);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, est.pct());
}

// ---- feed-forward ----------------------------------------------------------

void test_ff_formula() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, feedforwardTarget(30.0f, 40.0f, 70.0f, 2.0f));
}

void test_ff_small_diff_is_open() {
    TEST_ASSERT_EQUAL_FLOAT(100.0f, feedforwardTarget(30.0f, 40.0f, 32.0f, 2.0f));
}

void test_ff_negative_diff_is_open() {
    TEST_ASSERT_EQUAL_FLOAT(100.0f, feedforwardTarget(50.0f, 40.0f, 45.0f, 2.0f));
}

void test_ff_setpoint_below_h1_is_closed() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, feedforwardTarget(45.0f, 40.0f, 70.0f, 2.0f));
}

void test_ff_setpoint_above_h3_is_open() {
    TEST_ASSERT_EQUAL_FLOAT(100.0f, feedforwardTarget(30.0f, 60.0f, 50.0f, 2.0f));
}

void test_ff_custom_small_diff() {
    TEST_ASSERT_EQUAL_FLOAT(100.0f, feedforwardTarget(30.0f, 32.0f, 34.0f, 5.0f));
}

// ---- planMoveTo ------------------------------------------------------------

void test_move_open_from_closed() {
    assertMove(planMoveTo(0.0f, 30.0f, settings), K1Direction::Open, 36000u, false);
}

void test_move_small_close() {
    assertMove(planMoveTo(30.0f, 25.0f, settings), K1Direction::Close, 6000u, false);
}

void test_move_below_min_pulse_is_skipped() {
    assertNone(planMoveTo(30.0f, 30.5f, settings));   // 600 ms < 1 s
    assertNone(planMoveTo(30.0f, 30.0f, settings));
}

void test_move_to_zero_resyncs() {
    assertMove(planMoveTo(30.0f, 0.0f, settings), K1Direction::Close, 36000u + OVERDRIVE_MS, true);
}

void test_move_zero_to_zero_none() {
    assertNone(planMoveTo(0.0f, 0.0f, settings));
}

void test_move_to_hundred_resyncs() {
    assertMove(planMoveTo(80.0f, 100.0f, settings), K1Direction::Open, 24000u + OVERDRIVE_MS, true);
}

void test_move_hundred_to_hundred_none() {
    assertNone(planMoveTo(100.0f, 100.0f, settings));
}

void test_move_target_clamped() {
    assertMove(planMoveTo(30.0f, -5.0f, settings), K1Direction::Close, 36000u + OVERDRIVE_MS, true);
    assertMove(planMoveTo(80.0f, 120.0f, settings), K1Direction::Open, 24000u + OVERDRIVE_MS, true);
    assertNone(planMoveTo(0.0f, -5.0f, settings));
    assertNone(planMoveTo(100.0f, 120.0f, settings));
}

void test_move_small_move_to_end_still_resyncs() {
    // The min-pulse rule (D9) does not apply to end-stop moves.
    assertMove(planMoveTo(0.5f, 0.0f, settings), K1Direction::Close, 600u + OVERDRIVE_MS, true);
}

// ---- planFeedback ----------------------------------------------------------

void test_fb_open_proportional() {
    assertMove(planFeedback(30.0f, 38.5f, 40.0f, settings), K1Direction::Open, 3000u, false);
}

void test_fb_deadband() {
    assertNone(planFeedback(30.0f, 39.1f, 40.0f, settings));   // err 0.9
    assertNone(planFeedback(30.0f, 41.0f, 40.0f, settings));   // |err| == deadband
}

void test_fb_close_proportional() {
    assertMove(planFeedback(30.0f, 43.0f, 40.0f, settings), K1Direction::Close, 6000u, false);
}

void test_fb_capped_at_max_pulse() {
    assertMove(planFeedback(30.0f, 32.0f, 40.0f, settings), K1Direction::Open, 10000u, false);
}

void test_fb_below_min_pulse_skipped() {
    settings.k1Gain = 0.5f;
    assertNone(planFeedback(30.0f, 38.5f, 40.0f, settings));   // 750 ms < 1 s
}

void test_fb_reaching_open_end_resyncs() {
    assertMove(planFeedback(95.0f, 32.0f, 40.0f, settings), K1Direction::Open, 6000u + OVERDRIVE_MS, true);
}

void test_fb_at_open_end_none() {
    assertNone(planFeedback(100.0f, 32.0f, 40.0f, settings));
}

void test_fb_reaching_closed_end_resyncs() {
    assertMove(planFeedback(3.0f, 45.0f, 40.0f, settings), K1Direction::Close, 3600u + OVERDRIVE_MS, true);
}

void test_fb_at_closed_end_none() {
    assertNone(planFeedback(0.0f, 45.0f, 40.0f, settings));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_used_by_this_suite);
    RUN_TEST(test_estimator_unknown_after_reset);
    RUN_TEST(test_estimator_open_half);
    RUN_TEST(test_estimator_close_clamps_to_zero);
    RUN_TEST(test_estimator_open_clamps_to_hundred);
    RUN_TEST(test_estimator_mixed_motion_is_net);
    RUN_TEST(test_estimator_set_known_zero_and_clamp);
    RUN_TEST(test_estimator_integrates_while_unknown);
    RUN_TEST(test_estimator_zero_travel_ignored);
    RUN_TEST(test_ff_formula);
    RUN_TEST(test_ff_small_diff_is_open);
    RUN_TEST(test_ff_negative_diff_is_open);
    RUN_TEST(test_ff_setpoint_below_h1_is_closed);
    RUN_TEST(test_ff_setpoint_above_h3_is_open);
    RUN_TEST(test_ff_custom_small_diff);
    RUN_TEST(test_move_open_from_closed);
    RUN_TEST(test_move_small_close);
    RUN_TEST(test_move_below_min_pulse_is_skipped);
    RUN_TEST(test_move_to_zero_resyncs);
    RUN_TEST(test_move_zero_to_zero_none);
    RUN_TEST(test_move_to_hundred_resyncs);
    RUN_TEST(test_move_hundred_to_hundred_none);
    RUN_TEST(test_move_target_clamped);
    RUN_TEST(test_move_small_move_to_end_still_resyncs);
    RUN_TEST(test_fb_open_proportional);
    RUN_TEST(test_fb_deadband);
    RUN_TEST(test_fb_close_proportional);
    RUN_TEST(test_fb_capped_at_max_pulse);
    RUN_TEST(test_fb_below_min_pulse_skipped);
    RUN_TEST(test_fb_reaching_open_end_resyncs);
    RUN_TEST(test_fb_at_open_end_none);
    RUN_TEST(test_fb_reaching_closed_end_resyncs);
    RUN_TEST(test_fb_at_closed_end_none);
    return UNITY_END();
}
