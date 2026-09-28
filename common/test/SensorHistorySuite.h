#pragma once
#include <unity.h>
#include <stdint.h>
#include <math.h>
#include "../lib/HwEngine/src/SensorHistory.h"

// Native-safe Unity tests for SensorHistory (stage 09 phase 1, C1): the 30 s
// drift-free sampling cadence, the stall rule (no catch-up burst), the ring
// wrap-around, NAN for missing readings and steady(). Header-only, run via
// HwSuite.h. Test names are prefixed sh_ (D24).

namespace {

constexpr uint64_t SH_T0 = 5000;

// Shared scratch history: SensorHistory is ~3.8 KB, keep it off the stack.
SensorHistory g_sh;

void shFill(float values[], bool ok[], float v) {
    for (uint8_t i = 0; i < SENSOR_HISTORY_MAX_SENSORS; ++i) {
        values[i] = v + static_cast<float>(i);
        ok[i] = true;
    }
}

// Ticks sensor 0 with value v at nowMs (all sensors ok).
bool shTick(SensorHistory& h, uint64_t nowMs, float v) {
    float values[SENSOR_HISTORY_MAX_SENSORS];
    bool ok[SENSOR_HISTORY_MAX_SENSORS];
    shFill(values, ok, v);
    return h.tick(nowMs, values, ok);
}

}  // namespace

static void sh_test_first_tick_samples_immediately() {
    g_sh.reset(3);
    TEST_ASSERT_EQUAL_UINT16(0, g_sh.size());
    TEST_ASSERT_TRUE(shTick(g_sh, SH_T0, 20.0f));
    TEST_ASSERT_EQUAL_UINT16(1, g_sh.size());
    TEST_ASSERT_EQUAL_FLOAT(20.0f, g_sh.at(0, 0));
    TEST_ASSERT_EQUAL_FLOAT(22.0f, g_sh.at(2, 0));
}

static void sh_test_no_sample_before_period_one_at_period() {
    g_sh.reset(2);
    TEST_ASSERT_TRUE(shTick(g_sh, SH_T0, 20.0f));
    TEST_ASSERT_FALSE(shTick(g_sh, SH_T0 + 100, 21.0f));
    TEST_ASSERT_FALSE(shTick(g_sh, SH_T0 + SENSOR_HISTORY_PERIOD_MS - 1, 21.0f));
    TEST_ASSERT_EQUAL_UINT16(1, g_sh.size());
    TEST_ASSERT_TRUE(shTick(g_sh, SH_T0 + SENSOR_HISTORY_PERIOD_MS, 22.0f));
    TEST_ASSERT_EQUAL_UINT16(2, g_sh.size());
    TEST_ASSERT_EQUAL_FLOAT(22.0f, g_sh.at(0, 0));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, g_sh.at(0, 1));
}

static void sh_test_cadence_drift_free_over_ten_samples() {
    g_sh.reset(1);
    TEST_ASSERT_TRUE(shTick(g_sh, SH_T0, 0.0f));
    // Ticks arrive 700 ms late every time; the due grid must not drift.
    for (uint16_t k = 1; k <= 10; ++k) {
        const uint64_t due = SH_T0 + static_cast<uint64_t>(k) * SENSOR_HISTORY_PERIOD_MS;
        TEST_ASSERT_FALSE(shTick(g_sh, due - 1, 1.0f));
        TEST_ASSERT_TRUE(shTick(g_sh, due + 700, static_cast<float>(k)));
    }
    TEST_ASSERT_EQUAL_UINT16(11, g_sh.size());
    TEST_ASSERT_EQUAL_FLOAT(10.0f, g_sh.at(0, 0));
    // Next due is on the grid (T0 + 11 periods), not 700 ms later.
    const uint64_t next = SH_T0 + 11ULL * SENSOR_HISTORY_PERIOD_MS;
    TEST_ASSERT_FALSE(shTick(g_sh, next - 1, 1.0f));
    TEST_ASSERT_TRUE(shTick(g_sh, next, 11.0f));
}

static void sh_test_stall_gives_one_sample_then_next_due_plus_period() {
    g_sh.reset(1);
    TEST_ASSERT_TRUE(shTick(g_sh, SH_T0, 1.0f));
    const uint64_t late = SH_T0 + 95000;  // stalled 95 s
    TEST_ASSERT_TRUE(shTick(g_sh, late, 2.0f));
    TEST_ASSERT_FALSE(shTick(g_sh, late, 3.0f));  // no catch-up burst
    TEST_ASSERT_FALSE(shTick(g_sh, late + SENSOR_HISTORY_PERIOD_MS - 1, 3.0f));
    TEST_ASSERT_EQUAL_UINT16(2, g_sh.size());
    TEST_ASSERT_TRUE(shTick(g_sh, late + SENSOR_HISTORY_PERIOD_MS, 4.0f));
    TEST_ASSERT_EQUAL_UINT16(3, g_sh.size());
}

static void sh_test_wrap_around_after_125_samples() {
    g_sh.reset(2);
    for (uint16_t k = 0; k < 125; ++k) {
        TEST_ASSERT_TRUE(shTick(g_sh, SH_T0 + static_cast<uint64_t>(k) * SENSOR_HISTORY_PERIOD_MS,
                                static_cast<float>(k)));
    }
    TEST_ASSERT_EQUAL_UINT16(SENSOR_HISTORY_DEPTH, g_sh.size());
    TEST_ASSERT_EQUAL_FLOAT(124.0f, g_sh.at(0, 0));   // newest
    TEST_ASSERT_EQUAL_FLOAT(5.0f, g_sh.at(0, 119));   // oldest kept
    TEST_ASSERT_EQUAL_FLOAT(125.0f, g_sh.at(1, 0));   // sensor 1 = v + 1
    TEST_ASSERT_TRUE(isnan(g_sh.at(0, 120)));         // samples 0..4 are gone
    for (uint16_t age = 0; age < SENSOR_HISTORY_DEPTH; ++age) {
        TEST_ASSERT_TRUE(g_sh.at(0, age) >= 5.0f);
    }
}

static void sh_test_not_ok_stores_nan() {
    g_sh.reset(3);
    float values[SENSOR_HISTORY_MAX_SENSORS];
    bool ok[SENSOR_HISTORY_MAX_SENSORS];
    shFill(values, ok, 30.0f);
    ok[1] = false;
    TEST_ASSERT_TRUE(g_sh.tick(SH_T0, values, ok));
    TEST_ASSERT_EQUAL_FLOAT(30.0f, g_sh.at(0, 0));
    TEST_ASSERT_TRUE(isnan(g_sh.at(1, 0)));
    TEST_ASSERT_EQUAL_FLOAT(32.0f, g_sh.at(2, 0));
}

static void sh_test_at_out_of_range_is_nan() {
    g_sh.reset(2);
    TEST_ASSERT_TRUE(isnan(g_sh.at(0, 0)));  // empty
    TEST_ASSERT_TRUE(shTick(g_sh, SH_T0, 1.0f));
    TEST_ASSERT_TRUE(isnan(g_sh.at(0, 1)));  // age beyond size
    TEST_ASSERT_TRUE(isnan(g_sh.at(2, 0)));  // sensor beyond count
    TEST_ASSERT_TRUE(isnan(g_sh.at(0, SENSOR_HISTORY_DEPTH)));
}

// Fills sensor 0 with `n` samples on the 30 s grid; value = base + dev[k] for
// the k-th sample (oldest first) when dev is given, else base.
static void shFillSeries(uint16_t n, float base, const float* dev) {
    g_sh.reset(1);
    for (uint16_t k = 0; k < n; ++k) {
        const float v = dev != nullptr ? base + dev[k] : base;
        TEST_ASSERT_TRUE(shTick(g_sh, SH_T0 + static_cast<uint64_t>(k) * SENSOR_HISTORY_PERIOD_MS, v));
    }
}

static void sh_test_steady_true_within_band() {
    const float dev[6] = {0.5f, -0.3f, 0.2f, -0.5f, 0.0f, 0.4f};
    shFillSeries(6, 40.0f, dev);
    TEST_ASSERT_TRUE(g_sh.steady(0, 40.0f, 0.5f, 6));
    TEST_ASSERT_TRUE(g_sh.steady(0, 40.0f, 0.5f, 3));
}

static void sh_test_steady_false_with_one_outlier() {
    const float dev[6] = {0.1f, 0.1f, 0.1f, 1.5f, 0.1f, 0.1f};
    shFillSeries(6, 40.0f, dev);
    TEST_ASSERT_FALSE(g_sh.steady(0, 40.0f, 0.5f, 6));
    TEST_ASSERT_TRUE(g_sh.steady(0, 40.0f, 0.5f, 2));  // outlier older than the window
}

static void sh_test_steady_false_with_one_nan() {
    g_sh.reset(1);
    float values[SENSOR_HISTORY_MAX_SENSORS];
    bool ok[SENSOR_HISTORY_MAX_SENSORS];
    for (uint16_t k = 0; k < 5; ++k) {
        shFill(values, ok, 40.0f);
        ok[0] = (k != 2);
        TEST_ASSERT_TRUE(g_sh.tick(SH_T0 + static_cast<uint64_t>(k) * SENSOR_HISTORY_PERIOD_MS, values, ok));
    }
    TEST_ASSERT_FALSE(g_sh.steady(0, 40.0f, 1.0f, 5));
    TEST_ASSERT_TRUE(g_sh.steady(0, 40.0f, 1.0f, 2));
}

static void sh_test_steady_false_with_too_few_samples() {
    shFillSeries(4, 40.0f, nullptr);
    TEST_ASSERT_TRUE(g_sh.steady(0, 40.0f, 0.1f, 4));
    TEST_ASSERT_FALSE(g_sh.steady(0, 40.0f, 0.1f, 5));
}

static void sh_test_steady_zero_or_over_depth_samples_false() {
    shFillSeries(SENSOR_HISTORY_DEPTH, 40.0f, nullptr);
    TEST_ASSERT_TRUE(g_sh.steady(0, 40.0f, 0.1f, SENSOR_HISTORY_DEPTH));
    TEST_ASSERT_FALSE(g_sh.steady(0, 40.0f, 0.1f, 0));
    TEST_ASSERT_FALSE(g_sh.steady(0, 40.0f, 0.1f, SENSOR_HISTORY_DEPTH + 1));
}

static void sh_test_reset_empties() {
    shFillSeries(10, 40.0f, nullptr);
    TEST_ASSERT_EQUAL_UINT16(10, g_sh.size());
    g_sh.reset(1);
    TEST_ASSERT_EQUAL_UINT16(0, g_sh.size());
    TEST_ASSERT_TRUE(isnan(g_sh.at(0, 0)));
    TEST_ASSERT_FALSE(g_sh.steady(0, 40.0f, 0.1f, 1));
    // After reset the first tick samples immediately again, even at an
    // earlier-than-due time.
    TEST_ASSERT_TRUE(shTick(g_sh, SH_T0, 41.0f));
    TEST_ASSERT_EQUAL_FLOAT(41.0f, g_sh.at(0, 0));
}

static void sh_test_sensor_count_clamps_to_max() {
    g_sh.reset(20);
    TEST_ASSERT_EQUAL_UINT8(SENSOR_HISTORY_MAX_SENSORS, g_sh.sensorCount());
    TEST_ASSERT_TRUE(shTick(g_sh, SH_T0, 10.0f));
    TEST_ASSERT_EQUAL_FLOAT(17.0f, g_sh.at(SENSOR_HISTORY_MAX_SENSORS - 1, 0));
    TEST_ASSERT_TRUE(isnan(g_sh.at(SENSOR_HISTORY_MAX_SENSORS, 0)));
    g_sh.reset(3);
    TEST_ASSERT_EQUAL_UINT8(3, g_sh.sensorCount());
}

// Runs every test in this suite. Call from runHwSuite().
inline void runSensorHistorySuite() {
    RUN_TEST(sh_test_first_tick_samples_immediately);
    RUN_TEST(sh_test_no_sample_before_period_one_at_period);
    RUN_TEST(sh_test_cadence_drift_free_over_ten_samples);
    RUN_TEST(sh_test_stall_gives_one_sample_then_next_due_plus_period);
    RUN_TEST(sh_test_wrap_around_after_125_samples);
    RUN_TEST(sh_test_not_ok_stores_nan);
    RUN_TEST(sh_test_at_out_of_range_is_nan);
    RUN_TEST(sh_test_steady_true_within_band);
    RUN_TEST(sh_test_steady_false_with_one_outlier);
    RUN_TEST(sh_test_steady_false_with_one_nan);
    RUN_TEST(sh_test_steady_false_with_too_few_samples);
    RUN_TEST(sh_test_steady_zero_or_over_depth_samples_false);
    RUN_TEST(sh_test_reset_empties);
    RUN_TEST(sh_test_sensor_count_clamps_to_max);
}
