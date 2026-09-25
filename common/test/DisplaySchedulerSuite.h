#pragma once
#include <unity.h>
#include <stdint.h>
#include "../lib/DisplayEngine/src/DisplayFormat.h"
#include "../lib/DisplayEngine/src/DisplayScheduler.h"

// Native-safe Unity tests for DisplayScheduler (stage 06 phase 2): rotation,
// dwell latching, the dynamic Alarms page, alarm jump/hold, setup/OTA/reset
// override screens, uint32 wrap and the `changed` flag. Header-only, run via
// DisplaySuite.h. Test names are prefixed dsch_.

// ---- helpers ----

static uint8_t dsch_subPages(uint32_t mask) {
    const uint8_t n = alarmCount(mask);
    return static_cast<uint8_t>((n + 4) / 5);
}

static DisplayInputs dsch_inputs(uint32_t nowMs, uint32_t periodMs = 5000) {
    DisplayInputs in = {};
    in.nowMs = nowMs;
    in.alarmMask = 0;
    in.setupActive = false;
    in.otaActive = false;
    in.rotatePeriodMs = periodMs;
    in.alarmSubPages = 0;
    return in;
}

static void dsch_setMask(DisplayInputs& in, uint32_t mask) {
    in.alarmMask = mask;
    in.alarmSubPages = dsch_subPages(mask);
}

// Advances `now` in 100 ms increments, calling update() after each one;
// returns the view of the last update.
static DisplayView dsch_step(DisplayScheduler& s, DisplayInputs& in, uint32_t ms) {
    DisplayView v = {};
    for (uint32_t t = 0; t < ms; t += 100) {
        in.nowMs += 100;
        v = s.update(in);
    }
    return v;
}

// Like dsch_step but fails if the alarm hold is ever active on the way.
static DisplayView dsch_stepNoHold(DisplayScheduler& s, DisplayInputs& in, uint32_t ms) {
    DisplayView v = {};
    for (uint32_t t = 0; t < ms; t += 100) {
        in.nowMs += 100;
        v = s.update(in);
        TEST_ASSERT_FALSE(s.alarmHoldActive());
    }
    return v;
}

static void dsch_assertPage(const DisplayView& v, uint8_t page) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DisplayScreen::Page), static_cast<uint8_t>(v.screen));
    TEST_ASSERT_EQUAL_UINT8(page, v.pageIndex);
}

static void dsch_assertScreen(const DisplayView& v, DisplayScreen screen) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(screen), static_cast<uint8_t>(v.screen));
}

// Brings a scheduler with `pages` pages to the end of an alarm hold started
// by `mask` at the first update (t = start); returns with now = start + 30000
// and the view on page 0.
static DisplayView dsch_passBootHold(DisplayScheduler& s, DisplayInputs& in, uint32_t mask) {
    dsch_setMask(in, mask);
    DisplayView v = s.update(in);
    dsch_assertScreen(v, DisplayScreen::Alarms);
    v = dsch_step(s, in, DISPLAY_ALARM_HOLD_MS);
    dsch_assertPage(v, 0);
    TEST_ASSERT_FALSE(s.alarmHoldActive());
    return v;
}

// ---- rotation ----

static void dsch_test_rotation_three_pages() {
    DisplayScheduler s;
    s.begin(0, 3, false);
    DisplayInputs in = dsch_inputs(0);
    DisplayView v = s.update(in);
    dsch_assertPage(v, 0);
    TEST_ASSERT_TRUE(v.changed);

    v = dsch_step(s, in, 4900);
    dsch_assertPage(v, 0);
    v = dsch_step(s, in, 100);  // exactly 5000 ms
    dsch_assertPage(v, 1);
    TEST_ASSERT_TRUE(v.changed);
    v = dsch_step(s, in, 4900);
    dsch_assertPage(v, 1);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 2);
    v = dsch_step(s, in, 5000);
    dsch_assertPage(v, 0);
}

static void dsch_test_single_page_stable_shift_advances() {
    DisplayScheduler s;
    s.begin(0, 1, false);
    DisplayInputs in = dsch_inputs(0);
    DisplayView v = s.update(in);
    dsch_assertPage(v, 0);
    TEST_ASSERT_EQUAL_UINT32(0, v.shiftStep);

    v = dsch_step(s, in, 4900);
    dsch_assertPage(v, 0);
    TEST_ASSERT_EQUAL_UINT32(0, v.shiftStep);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 0);
    TEST_ASSERT_EQUAL_UINT32(1, v.shiftStep);
    v = dsch_step(s, in, 5000);
    dsch_assertPage(v, 0);
    TEST_ASSERT_EQUAL_UINT32(2, v.shiftStep);
}

static void dsch_test_period_change_applies_next_page() {
    DisplayScheduler s;
    s.begin(0, 3, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    s.update(in);
    dsch_step(s, in, 2000);
    in.rotatePeriodMs = 2000;  // shorter period mid-page
    DisplayView v = dsch_step(s, in, 2900);  // t = 4900
    dsch_assertPage(v, 0);
    v = dsch_step(s, in, 100);  // t = 5000: original dwell honoured
    dsch_assertPage(v, 1);
    v = dsch_step(s, in, 1900);
    dsch_assertPage(v, 1);
    v = dsch_step(s, in, 100);  // new 2 s dwell from the next page on
    dsch_assertPage(v, 2);
}

static void dsch_test_period_clamped() {
    DisplayScheduler s;
    s.begin(0, 2, false);
    DisplayInputs in = dsch_inputs(0, 0);  // behaves as 2000
    s.update(in);
    DisplayView v = dsch_step(s, in, 1900);
    dsch_assertPage(v, 0);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 1);
    TEST_ASSERT_EQUAL_UINT32(1, v.shiftStep);

    DisplayScheduler big;
    big.begin(0, 2, false);
    DisplayInputs in2 = dsch_inputs(0, 100000);  // behaves as 60000
    big.update(in2);
    v = dsch_step(big, in2, 59900);
    dsch_assertPage(v, 0);
    v = dsch_step(big, in2, 100);
    dsch_assertPage(v, 1);
}

// ---- Alarms page in rotation ----

static void dsch_test_alarms_page_only_while_mask_set() {
    DisplayScheduler s;
    s.begin(0, 2, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    dsch_passBootHold(s, in, 0x1);  // t = 30000, page 0
    DisplayView v = dsch_step(s, in, 5000);
    dsch_assertPage(v, 1);
    v = dsch_step(s, in, 5000);  // after the last page: Alarms
    dsch_assertScreen(v, DisplayScreen::Alarms);
    TEST_ASSERT_FALSE(s.alarmHoldActive());
    v = dsch_step(s, in, 5000);  // 1 sub-page -> dwell = period
    dsch_assertPage(v, 0);

    dsch_setMask(in, 0);
    v = dsch_step(s, in, 5000);
    dsch_assertPage(v, 1);
    v = dsch_step(s, in, 5000);  // no Alarms page without alarms
    dsch_assertPage(v, 0);
}

static void dsch_test_alarms_page_cleared_goes_to_page0() {
    DisplayScheduler s;
    s.begin(0, 2, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    dsch_passBootHold(s, in, 0x1);
    DisplayView v = dsch_step(s, in, 10000);
    dsch_assertScreen(v, DisplayScreen::Alarms);
    v = dsch_step(s, in, 1000);
    dsch_assertScreen(v, DisplayScreen::Alarms);

    dsch_setMask(in, 0);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 0);
    TEST_ASSERT_TRUE(v.changed);
    v = dsch_step(s, in, 4900);  // page 0 got a fresh dwell
    dsch_assertPage(v, 0);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 1);
}

static void dsch_test_alarms_dwell_two_subpages() {
    DisplayScheduler s;
    s.begin(0, 2, false);
    DisplayInputs in = dsch_inputs(0, 2000);
    dsch_passBootHold(s, in, 0x7F);  // 7 alarms -> 2 sub-pages
    DisplayView v = dsch_step(s, in, 2000);
    dsch_assertPage(v, 1);
    v = dsch_step(s, in, 2000);  // Alarms entered
    dsch_assertScreen(v, DisplayScreen::Alarms);
    TEST_ASSERT_EQUAL_UINT8(0, v.subPage);
    v = dsch_step(s, in, 4900);  // max(2000, 2 * 2500) = 5000 dwell
    dsch_assertScreen(v, DisplayScreen::Alarms);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 0);
}

static void dsch_test_alarms_subpage_every_2500ms() {
    DisplayScheduler s;
    s.begin(0, 1, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    dsch_setMask(in, 0x7F);  // 2 sub-pages, shown via the hold
    DisplayView v = s.update(in);
    dsch_assertScreen(v, DisplayScreen::Alarms);
    TEST_ASSERT_EQUAL_UINT8(0, v.subPage);
    v = dsch_step(s, in, 2400);
    TEST_ASSERT_EQUAL_UINT8(0, v.subPage);
    v = dsch_step(s, in, 100);
    TEST_ASSERT_EQUAL_UINT8(1, v.subPage);
    TEST_ASSERT_TRUE(v.changed);
    v = dsch_step(s, in, 2400);
    TEST_ASSERT_EQUAL_UINT8(1, v.subPage);
    v = dsch_step(s, in, 100);
    TEST_ASSERT_EQUAL_UINT8(0, v.subPage);
    dsch_assertScreen(v, DisplayScreen::Alarms);
}

// ---- alarm jump and hold ----

static void dsch_test_new_bit_jumps_immediately() {
    DisplayScheduler s;
    s.begin(0, 3, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    s.update(in);
    DisplayView v = dsch_step(s, in, 6000);
    dsch_assertPage(v, 1);
    TEST_ASSERT_FALSE(s.alarmHoldActive());

    dsch_setMask(in, 0x4);
    v = dsch_step(s, in, 100);
    dsch_assertScreen(v, DisplayScreen::Alarms);
    TEST_ASSERT_TRUE(v.changed);
    TEST_ASSERT_TRUE(s.alarmHoldActive());
}

static void dsch_test_hold_lasts_30s_then_page0() {
    DisplayScheduler s;
    s.begin(0, 3, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    s.update(in);
    dsch_step(s, in, 6000);  // on page 1
    dsch_setMask(in, 0x1);
    DisplayView v = dsch_step(s, in, 100);  // hold starts at t = 6100
    dsch_assertScreen(v, DisplayScreen::Alarms);
    const uint32_t shiftAtHold = v.shiftStep;
    v = dsch_step(s, in, 29900);
    dsch_assertScreen(v, DisplayScreen::Alarms);
    TEST_ASSERT_TRUE(s.alarmHoldActive());
    TEST_ASSERT_TRUE(v.shiftStep > shiftAtHold);  // shift keeps running during the hold
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 0);
    TEST_ASSERT_FALSE(s.alarmHoldActive());
    v = dsch_step(s, in, 4900);  // page 0 dwell restarted at hold end
    dsch_assertPage(v, 0);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 1);
}

static void dsch_test_second_bit_extends_hold() {
    DisplayScheduler s;
    s.begin(0, 2, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    s.update(in);
    dsch_step(s, in, 1000);
    dsch_setMask(in, 0x1);
    dsch_step(s, in, 100);  // hold starts at t0 = 1100
    DisplayView v = dsch_step(s, in, 20000);
    dsch_assertScreen(v, DisplayScreen::Alarms);
    dsch_setMask(in, 0x3);  // new bit at t0 + 20100
    v = dsch_step(s, in, 100);
    dsch_assertScreen(v, DisplayScreen::Alarms);
    v = dsch_step(s, in, 29900);  // t0 + 50000: still before the new expiry
    dsch_assertScreen(v, DisplayScreen::Alarms);
    TEST_ASSERT_TRUE(s.alarmHoldActive());
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 0);
}

static void dsch_test_bit_clear_keeps_original_expiry() {
    DisplayScheduler s;
    s.begin(0, 2, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    s.update(in);
    dsch_step(s, in, 1000);
    dsch_setMask(in, 0x3);
    dsch_step(s, in, 100);  // hold starts at t0
    dsch_step(s, in, 10000);
    dsch_setMask(in, 0x1);  // one bit clears: no re-jump, no extension
    DisplayView v = dsch_step(s, in, 19900);  // t0 + 29900
    dsch_assertScreen(v, DisplayScreen::Alarms);
    v = dsch_step(s, in, 100);  // t0 + 30000
    dsch_assertPage(v, 0);
    TEST_ASSERT_FALSE(s.alarmHoldActive());
}

static void dsch_test_all_bits_clear_ends_hold() {
    DisplayScheduler s;
    s.begin(0, 1, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    s.update(in);
    dsch_step(s, in, 1000);
    dsch_setMask(in, 0x3);
    DisplayView v = dsch_step(s, in, 100);
    dsch_assertScreen(v, DisplayScreen::Alarms);
    dsch_step(s, in, 5000);
    dsch_setMask(in, 0);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 0);
    TEST_ASSERT_FALSE(s.alarmHoldActive());
    // No Alarms page in rotation without alarms.
    for (int i = 0; i < 300; ++i) {
        v = dsch_step(s, in, 100);
        dsch_assertPage(v, 0);
    }
}

static void dsch_test_mask_at_first_update_jumps() {
    DisplayScheduler s;
    s.begin(0, 3, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    dsch_setMask(in, 0x1000000);  // a sensor-missing bit already active at boot
    DisplayView v = s.update(in);
    dsch_assertScreen(v, DisplayScreen::Alarms);
    TEST_ASSERT_TRUE(v.changed);
    TEST_ASSERT_TRUE(s.alarmHoldActive());
}

static void dsch_test_steady_mask_no_rejump() {
    DisplayScheduler s;
    s.begin(0, 1, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    dsch_passBootHold(s, in, 0x5);
    // Steady mask: rotation alternates page 0 / Alarms, never a new hold.
    DisplayView v = dsch_stepNoHold(s, in, 5000);
    dsch_assertScreen(v, DisplayScreen::Alarms);
    v = dsch_stepNoHold(s, in, 5000);
    dsch_assertPage(v, 0);
    dsch_stepNoHold(s, in, 60000);
}

// ---- override screens ----

static void dsch_test_setup_interleaves_alarms() {
    DisplayScheduler s;
    s.begin(0, 2, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    in.setupActive = true;
    dsch_setMask(in, 0x1);
    DisplayView v = s.update(in);  // slot 0; no hold in setup
    dsch_assertScreen(v, DisplayScreen::Setup);
    TEST_ASSERT_FALSE(s.alarmHoldActive());
    v = dsch_stepNoHold(s, in, 5000);  // slot 1
    dsch_assertScreen(v, DisplayScreen::Setup);
    v = dsch_stepNoHold(s, in, 4900);
    dsch_assertScreen(v, DisplayScreen::Setup);
    v = dsch_stepNoHold(s, in, 100);  // slot 2
    dsch_assertScreen(v, DisplayScreen::Alarms);
    TEST_ASSERT_TRUE(v.changed);
    v = dsch_stepNoHold(s, in, 5000);  // slot 3
    dsch_assertScreen(v, DisplayScreen::Setup);
    v = dsch_stepNoHold(s, in, 5000);  // slot 4
    dsch_assertScreen(v, DisplayScreen::Setup);
    v = dsch_stepNoHold(s, in, 5000);  // slot 5
    dsch_assertScreen(v, DisplayScreen::Alarms);
}

static void dsch_test_setup_without_alarms_only_setup() {
    DisplayScheduler s;
    s.begin(0, 2, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    in.setupActive = true;
    DisplayView v = s.update(in);
    dsch_assertScreen(v, DisplayScreen::Setup);
    for (int i = 0; i < 400; ++i) {
        v = dsch_step(s, in, 100);
        dsch_assertScreen(v, DisplayScreen::Setup);
    }
}

static void dsch_test_setup_no_hold_and_ends_on_page0() {
    DisplayScheduler s;
    s.begin(0, 3, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    s.update(in);
    DisplayView v = dsch_step(s, in, 6000);
    dsch_assertPage(v, 1);

    in.setupActive = true;
    v = dsch_step(s, in, 100);
    dsch_assertScreen(v, DisplayScreen::Setup);
    dsch_setMask(in, 0x2);  // new bit during setup: no hold
    dsch_stepNoHold(s, in, 2000);
    in.setupActive = false;
    v = dsch_stepNoHold(s, in, 100);  // setup ends: page 0, still no hold
    dsch_assertPage(v, 0);
    TEST_ASSERT_TRUE(v.changed);
    v = dsch_stepNoHold(s, in, 4900);
    dsch_assertPage(v, 0);
    v = dsch_stepNoHold(s, in, 100);
    dsch_assertPage(v, 1);
}

static void dsch_test_ota_shows_and_cancels_hold() {
    DisplayScheduler s;
    s.begin(0, 2, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    s.update(in);
    dsch_step(s, in, 1000);
    dsch_setMask(in, 0x1);
    DisplayView v = dsch_step(s, in, 100);
    TEST_ASSERT_TRUE(s.alarmHoldActive());
    dsch_assertScreen(v, DisplayScreen::Alarms);

    in.otaActive = true;
    v = dsch_step(s, in, 100);
    dsch_assertScreen(v, DisplayScreen::Ota);
    TEST_ASSERT_FALSE(s.alarmHoldActive());
    dsch_setMask(in, 0x3);  // new bit during OTA: no hold
    v = dsch_stepNoHold(s, in, 3000);
    dsch_assertScreen(v, DisplayScreen::Ota);

    in.otaActive = false;
    v = dsch_stepNoHold(s, in, 100);
    dsch_assertPage(v, 0);
}

static void dsch_test_reset_result_three_seconds() {
    DisplayScheduler s;
    s.begin(0, 2, true);
    DisplayInputs in = dsch_inputs(0, 5000);
    in.otaActive = true;  // ResetResult outranks OTA
    DisplayView v = s.update(in);
    dsch_assertScreen(v, DisplayScreen::ResetResult);
    in.otaActive = false;
    v = dsch_step(s, in, 2900);
    dsch_assertScreen(v, DisplayScreen::ResetResult);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 0);
    TEST_ASSERT_TRUE(v.changed);
    v = dsch_step(s, in, 4900);
    dsch_assertPage(v, 0);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 1);

    DisplayScheduler plain;
    plain.begin(0, 2, false);
    DisplayInputs in2 = dsch_inputs(0, 5000);
    v = plain.update(in2);
    dsch_assertPage(v, 0);
    for (int i = 0; i < 40; ++i) {
        v = dsch_step(plain, in2, 100);
        TEST_ASSERT_TRUE(v.screen != DisplayScreen::ResetResult);
    }
}

// ---- wrap ----

static void dsch_test_wrap_timing_identical() {
    const uint32_t start = 0xFFFFF000u;  // 4096 ms before uint32 wrap
    DisplayScheduler s;
    s.begin(start, 3, false);
    DisplayInputs in = dsch_inputs(start, 5000);
    DisplayView v = s.update(in);
    dsch_assertPage(v, 0);
    v = dsch_step(s, in, 4900);  // crosses the wrap
    dsch_assertPage(v, 0);
    TEST_ASSERT_TRUE(in.nowMs < start);
    v = dsch_step(s, in, 100);
    dsch_assertPage(v, 1);
    TEST_ASSERT_EQUAL_UINT32(1, v.shiftStep);
    v = dsch_step(s, in, 5000);
    dsch_assertPage(v, 2);

    // Hold across the wrap (fresh scheduler).
    DisplayScheduler h;
    const uint32_t hs = 0xFFFFC000u;  // 16384 ms before wrap
    h.begin(hs, 2, false);
    DisplayInputs hin = dsch_inputs(hs, 5000);
    h.update(hin);
    dsch_step(h, hin, 1000);
    dsch_setMask(hin, 0x1);
    v = dsch_step(h, hin, 100);  // hold starts at hs + 1100
    dsch_assertScreen(v, DisplayScreen::Alarms);
    v = dsch_step(h, hin, 29900);  // crosses the wrap
    dsch_assertScreen(v, DisplayScreen::Alarms);
    TEST_ASSERT_TRUE(hin.nowMs < hs);
    v = dsch_step(h, hin, 100);
    dsch_assertPage(v, 0);
}

// ---- changed ----

static void dsch_test_changed_flag() {
    DisplayScheduler s;
    s.begin(0, 1, false);
    DisplayInputs in = dsch_inputs(0, 5000);
    DisplayView v = s.update(in);
    TEST_ASSERT_TRUE(v.changed);  // first update always changed
    v = dsch_step(s, in, 100);
    TEST_ASSERT_FALSE(v.changed);
    v = s.update(in);  // same time, same inputs
    TEST_ASSERT_FALSE(v.changed);
    v = dsch_step(s, in, 4800);
    TEST_ASSERT_FALSE(v.changed);
    v = dsch_step(s, in, 100);  // t = 5000: shift step only
    dsch_assertPage(v, 0);
    TEST_ASSERT_EQUAL_UINT32(1, v.shiftStep);
    TEST_ASSERT_TRUE(v.changed);
    v = dsch_step(s, in, 100);
    TEST_ASSERT_FALSE(v.changed);
}

inline void runDisplaySchedulerSuite() {
    RUN_TEST(dsch_test_rotation_three_pages);
    RUN_TEST(dsch_test_single_page_stable_shift_advances);
    RUN_TEST(dsch_test_period_change_applies_next_page);
    RUN_TEST(dsch_test_period_clamped);
    RUN_TEST(dsch_test_alarms_page_only_while_mask_set);
    RUN_TEST(dsch_test_alarms_page_cleared_goes_to_page0);
    RUN_TEST(dsch_test_alarms_dwell_two_subpages);
    RUN_TEST(dsch_test_alarms_subpage_every_2500ms);
    RUN_TEST(dsch_test_new_bit_jumps_immediately);
    RUN_TEST(dsch_test_hold_lasts_30s_then_page0);
    RUN_TEST(dsch_test_second_bit_extends_hold);
    RUN_TEST(dsch_test_bit_clear_keeps_original_expiry);
    RUN_TEST(dsch_test_all_bits_clear_ends_hold);
    RUN_TEST(dsch_test_mask_at_first_update_jumps);
    RUN_TEST(dsch_test_steady_mask_no_rejump);
    RUN_TEST(dsch_test_setup_interleaves_alarms);
    RUN_TEST(dsch_test_setup_without_alarms_only_setup);
    RUN_TEST(dsch_test_setup_no_hold_and_ends_on_page0);
    RUN_TEST(dsch_test_ota_shows_and_cancels_hold);
    RUN_TEST(dsch_test_reset_result_three_seconds);
    RUN_TEST(dsch_test_wrap_timing_identical);
    RUN_TEST(dsch_test_changed_flag);
}
