#pragma once
#include <unity.h>
#include <stdint.h>
#include "../lib/CoreEngine/src/CivilTime.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/CoreEngine/src/HardwareStatus.h"
#include "../lib/HwEngine/src/AntiSeizeScheduler.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/HwEngine/src/K1Driver.h"
#include "../lib/HwEngine/src/LocalTime.h"
#include "../lib/HwEngine/src/RelayBank.h"
#include "fakes/RecordingEventSink.h"

// Native-safe Unity tests for LocalTime (makeLocalTimeInfo) and
// AntiSeizeScheduler (stage 03 phase 4, D19-D21). Header-only, run via
// HwSuite.h. Test names are prefixed as_ (D24).
//
// The hardware layout mirrors home-heating's real wiring exactly (K1DriverSuite
// already covers K1Driver's own dead-time/interlock in isolation, so here we
// exercise the scheduler's own decisions against real RelayBank/K1Driver
// instances rather than fakes):
//   channel 0: K2  (Diverter, lockable, logs)     -- Toggle output
//   channel 1: K1 power   (K1Power,   not lockable, no log)
//   channel 2: K1 direction (K1Direction, not lockable, no log)
//   channel 3: P4  (Pump, lockable, logs)          -- Pump output; also the
//                                                     K1 stroke's block channel

namespace {

inline constexpr RelayChannelDesc AS_TEST_CHANNELS[] = {
    {0, "K2", RelayRole::Diverter, true, true},
    {1, "K1pwr", RelayRole::K1Power, false, false},
    {2, "K1dir", RelayRole::K1Direction, false, false},
    {3, "P4", RelayRole::Pump, true, true},
};

// Array order matters (D20): P4 before K1 so a same-tick "blocked by a
// running ValveStroke" check sees an already-started Pump/Toggle within the
// same step-5 pass, matching HomeHeatingHardware.h's real descriptor order.
inline constexpr AntiSeizeOutputDesc AS_TEST_OUTPUTS[] = {
    {"P4", AntiSeizeKind::Pump, 3, "asEnP4", NO_RELAY_CHANNEL},
    {"K2", AntiSeizeKind::Toggle, 0, "asEnK2", NO_RELAY_CHANNEL},
    {"K1", AntiSeizeKind::ValveStroke, 1, "asEnK1", 3},
};
constexpr size_t AS_OUT_P4 = 0;
constexpr size_t AS_OUT_K2 = 1;
constexpr size_t AS_OUT_K1 = 2;

constexpr uint64_t DAY_MS = 86400000ull;

struct AsFixture {
    RelayBank bank;
    K1Driver k1;
    RecordingEventSink events;
    AntiSeizeScheduler sched{bank, k1, events};

    void begin(uint64_t bootMs, uint32_t lockMs = 60000) {
        bank.configure(AS_TEST_CHANNELS, 4, bootMs);
        bank.setLockMs(lockMs);
        k1.begin(bootMs);
        sched.configure(AS_TEST_OUTPUTS, 3, bootMs);
    }

    void setSettings(uint16_t intervalDays, uint16_t startMinute, uint16_t durationS, bool enP4 = true,
        bool enK2 = true, bool enK1 = true) {
        AntiSeizeSettings s{};
        s.intervalDays = intervalDays;
        s.startMinute = startMinute;
        s.durationS = durationS;
        s.enabled[AS_OUT_P4] = enP4;
        s.enabled[AS_OUT_K2] = enK2;
        s.enabled[AS_OUT_K1] = enK1;
        sched.setSettings(s);
    }
};

// Replays K1's desired power/direction into the bank exactly like HwRuntime's
// fastTick will (Phase 5), then applies the bank -- so K1Driver's own motion
// (and any Pump/Toggle exercise the scheduler just set) becomes visible
// through RelayBank/K1Driver's own accessors on the next tick, driving the
// scheduler's "any run counts" refresh (D19).
inline void advanceTick(AsFixture& f, uint64_t t, const LocalTimeInfo& local) {
    f.sched.tick(t, local);
    f.k1.tick(t);
    f.bank.requestControl(1, f.k1.powerOn(), RelayReason::K1Drive);
    f.bank.requestControl(2, f.k1.directionOpen(), RelayReason::K1Drive);
    f.bank.update(t, f.events);
}

template <typename LocalFn>
void advanceTo(AsFixture& f, uint64_t& t, uint64_t to, uint64_t stepMs, LocalFn localFn) {
    while (t < to) {
        t += stepMs;
        advanceTick(f, t, localFn(t));
    }
}

// Clock-invalid local-time source (uptime fallback rule, D19b).
inline LocalTimeInfo localInvalid(uint64_t) { return NO_LOCAL_TIME; }

// Synthetic, TZ-independent "clock valid" source: local wall time advances
// 1:1 with the monotonic t, starting at local day `startDay`, minute
// `startMinute`, at t == anchorMs.
struct LocalFromUptime {
    uint64_t anchorMs;
    uint32_t startDay;
    uint16_t startMinute;

    LocalTimeInfo operator()(uint64_t t) const {
        const uint64_t elapsedMs = t >= anchorMs ? t - anchorMs : 0;
        const uint64_t totalMs = static_cast<uint64_t>(startMinute) * 60000ull + elapsedMs;
        LocalTimeInfo info;
        info.valid = true;
        info.dayIndex = startDay + static_cast<uint32_t>(totalMs / DAY_MS);
        info.minuteOfDay = static_cast<uint16_t>((totalMs % DAY_MS) / 60000ull);
        return info;
    }
};

}  // namespace

// ---- LocalTime (makeLocalTimeInfo) -----------------------------------

static void as_test_local_time_midnight() {
    CivilDateTime c{2026, 1, 8, 0, 0, 0, 0};  // 1970-01-01 + 13886 days
    const LocalTimeInfo info = makeLocalTimeInfo(c);
    TEST_ASSERT_TRUE(info.valid);
    TEST_ASSERT_EQUAL_UINT32(civilToEpoch(c) / 86400u, info.dayIndex);
    TEST_ASSERT_EQUAL_UINT16(0, info.minuteOfDay);
}

static void as_test_local_time_of_day() {
    CivilDateTime c{2026, 6, 15, 10, 30, 45, 0};
    const LocalTimeInfo info = makeLocalTimeInfo(c);
    TEST_ASSERT_TRUE(info.valid);
    CivilDateTime midnight = c;
    midnight.hour = 0;
    midnight.minute = 0;
    midnight.second = 0;
    TEST_ASSERT_EQUAL_UINT32(civilToEpoch(midnight) / 86400u, info.dayIndex);
    TEST_ASSERT_EQUAL_UINT16(10 * 60 + 30, info.minuteOfDay);  // seconds are dropped
}

static void as_test_local_time_no_local_time_constant() {
    TEST_ASSERT_FALSE(NO_LOCAL_TIME.valid);
    TEST_ASSERT_EQUAL_UINT32(0, NO_LOCAL_TIME.dayIndex);
    TEST_ASSERT_EQUAL_UINT16(0, NO_LOCAL_TIME.minuteOfDay);
}

// ---- AntiSeizeScheduler: checkpoint / uptime / safety net (D19) -------

static void as_test_idle_seven_days_checkpoint_starts_at_10_not_959() {
    AsFixture f;
    f.begin(0);
    f.setSettings(7, 600, 30);  // 10:00, boot at local day0 00:00
    const LocalFromUptime local{0, 0, 0};

    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS + 599 * 60000, 60000, local);  // day7 09:59
    TEST_ASSERT_FALSE(f.sched.pending(AS_OUT_P4));
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_P4));

    advanceTo(f, t, 7 * DAY_MS + 600 * 60000, 60000, local);  // day7 10:00
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_P4) || f.sched.running(AS_OUT_P4));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 3)));
}

static void as_test_pump_lock_extends_effective_run_to_lock() {
    AsFixture f;
    f.begin(0, 60000);  // 60 s lock
    f.setSettings(7, 0, 30, true, false, false);
    const LocalFromUptime local{0, 0, 0};

    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_P4));

    advanceTo(f, t, t + 30000, 1000, local);  // 30 s of asDuration elapsed
    TEST_ASSERT_TRUE(f.bank.actual(3));
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_P4));  // lock (60s) not yet elapsed since the ON switch

    advanceTo(f, t, t + 30000, 1000, local);  // total 60 s
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_P4));
    TEST_ASSERT_TRUE(f.bank.hasBeenOn(3));
}

static void as_test_controller_run_resets_timer_delays_next_run() {
    AsFixture f;
    f.begin(0, 0);  // no lock, so a manual control run applies instantly
    f.setSettings(7, 0, 30, true, false, false);
    const LocalFromUptime local{0, 0, 0};

    uint64_t t = 0;
    // A control-driven run on day 3 resets P4's idle clock.
    advanceTo(f, t, 3 * DAY_MS, DAY_MS, local);
    f.bank.requestControl(3, true);
    advanceTick(f, t + 1000, local(t + 1000));
    f.bank.requestControl(3, false);
    advanceTick(f, t + 2000, local(t + 2000));
    t += 2000;

    advanceTo(f, t, 7 * DAY_MS, 60000, local);  // day 7: NOT idle 7 days since the day-3 run
    TEST_ASSERT_FALSE(f.sched.pending(AS_OUT_P4));
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_P4));

    advanceTo(f, t, 10 * DAY_MS, 60000, local);  // day 10: 7 days since the day-3 run
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_P4) || f.sched.running(AS_OUT_P4));
}

static void as_test_disabled_output_never_runs_enabling_later_runs_next_checkpoint() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 30, false, false, false);  // P4 disabled
    const LocalFromUptime local{0, 0, 0};

    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_FALSE(f.sched.pending(AS_OUT_P4));
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_P4));

    f.setSettings(7, 0, 30, true, false, false);  // enable P4
    advanceTo(f, t, 8 * DAY_MS, 60000, local);     // next day's checkpoint
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_P4) || f.sched.running(AS_OUT_P4));
}

static void as_test_checkpoint_slack_covers_jitter() {
    AsFixture f;
    const uint64_t bootAt10_00_30 = 10ull * 3600000 + 30000;  // local day0 10:00:30
    f.begin(bootAt10_00_30, 0);
    f.setSettings(7, 600, 30, true, false, false);
    const LocalFromUptime local{0, 0, 0};  // t == local elapsed ms from day0 00:00:00

    uint64_t t = bootAt10_00_30;
    advanceTo(f, t, 7 * DAY_MS + 10 * 3600000, 60000, local);  // day7, 10:00:00 -- 30 s short of a full week
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_P4) || f.sched.running(AS_OUT_P4));
}

static void as_test_no_clock_run_after_seven_days_uptime_not_before() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 600, 30, true, false, false);

    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS - 1000, 60000, localInvalid);
    TEST_ASSERT_FALSE(f.sched.pending(AS_OUT_P4));

    advanceTo(f, t, 7 * DAY_MS, 60000, localInvalid);
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_P4) || f.sched.running(AS_OUT_P4));
}

static void as_test_clock_becomes_valid_mid_interval_no_double_run() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 30, true, false, false);

    uint64_t t = 0;
    advanceTo(f, t, 3 * DAY_MS, 60000, localInvalid);  // clock invalid for 3 days, well under interval
    TEST_ASSERT_FALSE(f.sched.pending(AS_OUT_P4));

    const LocalFromUptime local{t, 3, 0};  // clock becomes valid at day 3, local day 3 00:00
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_P4) || f.sched.running(AS_OUT_P4));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 3)));
}

static void as_test_safety_net_guarantees_run_when_checkpoint_never_recurs() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 600, 30, true, false, false);

    // A frozen local day/minute: the checkpoint fires exactly once (consuming
    // lastCheckDay) and can never recur, yet the clock stays valid the whole
    // time (so the uptime fallback, which requires an invalid clock, never
    // applies either). Only the interval+24h safety net can force a run here.
    auto frozenLocal = [](uint64_t) {
        LocalTimeInfo info;
        info.valid = true;
        info.dayIndex = 500;
        info.minuteOfDay = 600;
        return info;
    };

    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS + 23 * 3600000, 3600000, frozenLocal);  // interval + 23h: not yet
    TEST_ASSERT_FALSE(f.sched.pending(AS_OUT_P4));

    advanceTo(f, t, 7 * DAY_MS + AntiSeizeScheduler::SAFETY_NET_MS + 3600000, 3600000, frozenLocal);
    // The run may already have started and completed within one coarse
    // (1 h) step by the time we check, so assert via the event log rather
    // than the transient pending/running flags.
    TEST_ASSERT_TRUE(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 3) >= 1);
}

static void as_test_dst_backward_jump_across_checkpoint_same_day_one_run_only() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 600, 30, true, false, false);

    // Local time reaches day7/10:05, then jumps back to day7/09:30 (a DST
    // fall-back across the checkpoint minute), on the SAME dayIndex, then
    // proceeds forward again past 10:00 a second time.
    auto dstLocal = [](uint64_t t) {
        LocalTimeInfo info;
        info.valid = true;
        info.dayIndex = 7;
        if (t < 7 * DAY_MS + 605 * 60000) {
            info.minuteOfDay = static_cast<uint16_t>((t - 7 * DAY_MS) / 60000);
        } else if (t < 7 * DAY_MS + 635 * 60000) {
            info.minuteOfDay = 570;  // jumped back to 09:30
        } else {
            info.minuteOfDay = static_cast<uint16_t>(570 + (t - (7 * DAY_MS + 635 * 60000)) / 60000);
        }
        return info;
    };

    uint64_t t = 7 * DAY_MS;
    advanceTo(f, t, 7 * DAY_MS + 700 * 60000, 60000, dstLocal);  // covers both 10:00 crossings
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 3)));
}

static void as_test_day_index_going_backwards_no_second_checkpoint() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 600, 30, true, false, false);

    auto backwardsLocal = [](uint64_t t) {
        LocalTimeInfo info;
        info.valid = true;
        if (t < 7 * DAY_MS + 5 * 60000) {
            info.dayIndex = 7;
            info.minuteOfDay = 600;
        } else {
            info.dayIndex = 6;  // clock stepped backwards a day
            info.minuteOfDay = 600;
        }
        return info;
    };

    uint64_t t = 7 * DAY_MS;
    advanceTo(f, t, 7 * DAY_MS + 60 * 60000, 60000, backwardsLocal);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 3)));
}

// ---- Output kinds and run mechanics (D20) ------------------------------

static void as_test_k2_toggle_flips_holds_returns_and_counts_as_run() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 20, false, true, false);
    const LocalFromUptime local{0, 0, 0};

    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_K2));
    TEST_ASSERT_TRUE(f.bank.actual(0));  // flipped to the opposite of its starting (OFF) state

    // Held for asDuration; +1 extra step covers the one-tick lag between the
    // exercise request landing in RelayBank (applied by bank.update()) and
    // the scheduler observing actual() == target on its next tick.
    advanceTo(f, t, t + 20000 + 1000, 1000, local);
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_K2));
    TEST_ASSERT_TRUE(f.bank.hasChanged(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 0)));
}

static void as_test_k1_blocked_while_p4_on_starts_tick_after_p4_off() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 30, false, false, true);  // only K1 enabled
    const LocalFromUptime local{0, 0, 0};

    f.bank.requestControl(3, true);  // P4 held ON by the controller throughout
    uint64_t t = 0;
    advanceTick(f, 0, local(0));

    advanceTo(f, t, 7 * DAY_MS, 60000, local);  // K1 becomes idle-eligible, but P4 is ON
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_K1));
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_K1));

    advanceTo(f, t, 9 * DAY_MS, 60000, local);  // pending persists across days
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_K1));
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_K1));

    f.bank.requestControl(3, false);  // P4 turns OFF
    advanceTick(f, t + 1000, local(t + 1000));  // this tick: still blocked (actual(3) not yet applied)
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_K1));
    advanceTick(f, t + 2000, local(t + 2000));  // the tick after: unblocked, starts
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_K1));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 1)));
}

static void as_test_k1_pending_cleared_by_controller_pulse() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 30, false, false, true);
    const LocalFromUptime local{0, 0, 0};

    f.bank.requestControl(3, true);  // P4 ON: keeps K1 blocked
    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_K1));

    // A controller-driven pulse moves the valve directly (bypassing the
    // scheduler); K1Driver::lastMoveMs advances, which is exactly the
    // "any run counts" activity source for the ValveStroke output (D19).
    TEST_ASSERT_TRUE(f.k1.requestPulse(K1Direction::Open, 3000, t, K1Owner::Control));
    advanceTo(f, t, t + 5000, 100, local);
    TEST_ASSERT_TRUE(f.k1.hasMoved());

    advanceTick(f, t + 1000, local(t + 1000));
    t += 1000;
    TEST_ASSERT_FALSE(f.sched.pending(AS_OUT_K1));
}

static void as_test_k1_stroke_aborted_when_p4_turns_on_mid_stroke() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 30, false, false, true);  // P4 stays OFF/idle -> K1 free to start
    const LocalFromUptime local{0, 0, 0};

    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_K1));

    advanceTo(f, t, t + 3000, 100, local);  // into the Open leg, actually moving
    TEST_ASSERT_TRUE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::AntiSeize), static_cast<int>(f.k1.owner()));

    f.bank.requestControl(3, true);  // P4 turns ON mid-stroke
    // Two ticks: the first applies P4's actual switch (RelayBank::update()
    // runs after the scheduler reads actual()); the second is when the
    // scheduler observes it and aborts.
    advanceTick(f, t + 1000, local(t + 1000));
    advanceTick(f, t + 1100, local(t + 1100));
    t += 1100;

    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_K1));
    TEST_ASSERT_FALSE(f.k1.busy());  // cancelled
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 1)));
}

static void as_test_k1_close_leg_invalid_stroke_aborts_with_diagnostic() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 30, false, false, true);  // only K1 enabled, P4 idle
    f.sched.setK1StrokeMs(3000);
    const LocalFromUptime local{0, 0, 0};

    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_K1));  // Open leg requested with 3000 ms

    // The stroke duration becomes invalid mid-stroke: the Close leg request
    // must fail and the run must be aborted with a diagnostic, never treated
    // as a completed stroke.
    f.sched.setK1StrokeMs(0);

    bool sawCloseMotion = false;
    int openLegEndTick = -1;  // first tick after which K1 is idle again
    int runEndTick = -1;      // first tick after which the scheduler run is over
    for (int i = 0; i < 100 && runEndTick < 0; ++i) {
        t += 100;
        advanceTick(f, t, local(t));
        if (f.k1.busy() && f.k1.direction() == K1Direction::Close) {
            sawCloseMotion = true;
        }
        if (openLegEndTick < 0 && !f.k1.busy()) {
            openLegEndTick = i;
        }
        if (!f.sched.running(AS_OUT_K1)) {
            runEndTick = i;
        }
    }

    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_K1));
    TEST_ASSERT_FALSE(sawCloseMotion);
    // The run ends on the very next scheduler tick after the Open leg ended
    // (the aborted Close leg), not one tick later as a "completed" stroke.
    TEST_ASSERT_TRUE(openLegEndTick >= 0);
    TEST_ASSERT_EQUAL_INT(openLegEndTick + 1, runEndTick);
    TEST_ASSERT_FALSE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::DiagnosticWarning,
                                 EVENT_SOURCE_DIAG_BASE + DIAG_CODE_ANTISEIZE_K1_STROKE)));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 1)));
}

static void as_test_p4_exercise_blocked_while_k1_stroke_running() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(1, 0, 30);           // a short 1-day interval: fast to simulate under uptime rules
    f.sched.setK1StrokeMs(5000);       // a short but valid stroke (<= K1Driver::MAX_PULSE_MS)

    uint64_t t = 0;
    // Give P4 a real run 5 s after boot (via the controller), staggering its
    // own 1-day idle threshold 5 s behind K1's (whose lastRun stays at boot),
    // so P4 becomes idle-eligible while K1's short stroke is still running.
    f.bank.requestControl(3, true);
    advanceTick(f, 4000, localInvalid(4000));
    f.bank.requestControl(3, false);
    advanceTick(f, 5000, localInvalid(5000));
    t = 5000;

    advanceTo(f, t, DAY_MS - 3600000, 3600000, localInvalid);  // fast-forward to 1 h before the boundary
    advanceTo(f, t, DAY_MS + 1000, 1000, localInvalid);        // fine-grained through K1's trigger
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_K1));
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_P4));

    advanceTo(f, t, DAY_MS + 5000 + 2000, 1000, localInvalid);  // P4 becomes idle-eligible; K1 still running
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_P4));
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_P4));
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_K1));

    advanceTo(f, t, DAY_MS + 20000, 1000, localInvalid);  // K1's stroke (2 x 5 s) has finished
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_K1));
    TEST_ASSERT_TRUE(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 3) >= 1);  // P4 got its turn
}

static void as_test_safety_blocks_start_safety_run_counts_and_clears_pending() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 30, true, false, false);
    const LocalFromUptime local{0, 0, 0};

    f.bank.requestSafety(3, false);  // safety slot active (OFF), bypasses the lock but still "active"
    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_P4));
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_P4));  // blocked: safety active on the channel

    f.bank.requestSafety(3, true);  // safety itself runs the channel
    advanceTick(f, t + 1000, local(t + 1000));
    TEST_ASSERT_TRUE(f.bank.actual(3));
    // One more tick: the scheduler's activity refresh (step 1) reads
    // RelayBank::lastOnMs() as of the START of a tick, so the just-applied
    // safety run becomes visible -- and pending clears -- on the tick after.
    advanceTick(f, t + 2000, local(t + 2000));
    t += 2000;
    TEST_ASSERT_FALSE(f.sched.pending(AS_OUT_P4));  // the safety run counts as "ran" (D19)
}

static void as_test_safety_appears_mid_run_releases_exercise() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 30, true, false, false);
    const LocalFromUptime local{0, 0, 0};

    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_P4));

    f.bank.requestSafety(3, false);  // safety appears mid-run
    advanceTick(f, t + 1000, local(t + 1000));
    t += 1000;
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_P4));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::AntiSeizeRun, EVENT_SOURCE_RELAY_BASE + 3)));
}

static void as_test_set_inhibited_blocks_then_releases_runs_next_tick() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 30, true, false, false);
    const LocalFromUptime local{0, 0, 0};

    f.sched.setInhibited(AS_OUT_P4, true);
    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_P4));
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_P4));

    f.sched.setInhibited(AS_OUT_P4, false);
    advanceTick(f, t + 1000, local(t + 1000));
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_P4));
}

static void as_test_controller_demand_during_pump_run_no_conflict() {
    AsFixture f;
    f.begin(0, 0);
    f.setSettings(7, 0, 30, true, false, false);
    const LocalFromUptime local{0, 0, 0};

    uint64_t t = 0;
    advanceTo(f, t, 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.running(AS_OUT_P4));

    f.bank.requestControl(3, true);  // controller also wants it ON: no conflict, exercise still wins
    advanceTo(f, t, t + 30000 + 1000, 1000, local);  // +1 step for the WaitOn->Hold observation lag
    TEST_ASSERT_TRUE(f.bank.actual(3));
    TEST_ASSERT_FALSE(f.sched.running(AS_OUT_P4));  // exercise released once asDuration elapsed

    advanceTick(f, t + 1000, local(t + 1000));  // the pump stays ON under control afterwards
    TEST_ASSERT_TRUE(f.bank.actual(3));
}

static void as_test_fill_status_fields() {
    AsFixture f;
    f.begin(1000, 0);
    f.setSettings(7, 0, 30, true, false, true);
    const LocalFromUptime local{0, 0, 0};

    AntiSeizeStatus status{};
    f.sched.fillStatus(status, 1000);
    TEST_ASSERT_EQUAL_UINT8(3, status.count);
    TEST_ASSERT_TRUE(status.output[AS_OUT_P4].enabled);
    TEST_ASSERT_FALSE(status.output[AS_OUT_K2].enabled);
    TEST_ASSERT_FALSE(status.output[AS_OUT_P4].pending);
    TEST_ASSERT_EQUAL_UINT32(0, status.output[AS_OUT_P4].idleS);

    uint64_t t = 1000;
    advanceTo(f, t, 1000 + 3600000, 60000, local);
    f.sched.fillStatus(status, t);
    TEST_ASSERT_EQUAL_UINT32(3600, status.output[AS_OUT_P4].idleS);
}

static void as_test_wrap_safety_uptime_above_2_32_ms() {
    AsFixture f;
    const uint64_t bootMs = 5000000000000ull;  // well above 2^32 ms
    f.begin(bootMs, 0);
    f.setSettings(7, 0, 30, true, false, false);
    const LocalFromUptime local{bootMs, 0, 0};

    uint64_t t = bootMs;
    advanceTo(f, t, bootMs + 7 * DAY_MS - 60000, 60000, local);
    TEST_ASSERT_FALSE(f.sched.pending(AS_OUT_P4));

    advanceTo(f, t, bootMs + 7 * DAY_MS, 60000, local);
    TEST_ASSERT_TRUE(f.sched.pending(AS_OUT_P4) || f.sched.running(AS_OUT_P4));
}

// Runs every test in this suite. Call from runHwSuite().
inline void runAntiSeizeSuite() {
    RUN_TEST(as_test_local_time_midnight);
    RUN_TEST(as_test_local_time_of_day);
    RUN_TEST(as_test_local_time_no_local_time_constant);

    RUN_TEST(as_test_idle_seven_days_checkpoint_starts_at_10_not_959);
    RUN_TEST(as_test_pump_lock_extends_effective_run_to_lock);
    RUN_TEST(as_test_controller_run_resets_timer_delays_next_run);
    RUN_TEST(as_test_disabled_output_never_runs_enabling_later_runs_next_checkpoint);
    RUN_TEST(as_test_checkpoint_slack_covers_jitter);
    RUN_TEST(as_test_no_clock_run_after_seven_days_uptime_not_before);
    RUN_TEST(as_test_clock_becomes_valid_mid_interval_no_double_run);
    RUN_TEST(as_test_safety_net_guarantees_run_when_checkpoint_never_recurs);
    RUN_TEST(as_test_dst_backward_jump_across_checkpoint_same_day_one_run_only);
    RUN_TEST(as_test_day_index_going_backwards_no_second_checkpoint);

    RUN_TEST(as_test_k2_toggle_flips_holds_returns_and_counts_as_run);
    RUN_TEST(as_test_k1_blocked_while_p4_on_starts_tick_after_p4_off);
    RUN_TEST(as_test_k1_pending_cleared_by_controller_pulse);
    RUN_TEST(as_test_k1_stroke_aborted_when_p4_turns_on_mid_stroke);
    RUN_TEST(as_test_k1_close_leg_invalid_stroke_aborts_with_diagnostic);
    RUN_TEST(as_test_p4_exercise_blocked_while_k1_stroke_running);
    RUN_TEST(as_test_safety_blocks_start_safety_run_counts_and_clears_pending);
    RUN_TEST(as_test_safety_appears_mid_run_releases_exercise);
    RUN_TEST(as_test_set_inhibited_blocks_then_releases_runs_next_tick);
    RUN_TEST(as_test_controller_demand_during_pump_run_no_conflict);
    RUN_TEST(as_test_fill_status_fields);
    RUN_TEST(as_test_wrap_safety_uptime_above_2_32_ms);
}
