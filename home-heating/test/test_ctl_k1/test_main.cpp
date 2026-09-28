#include <unity.h>
#include <HomeHeatingControlSettings.h>
#include <HomeHeatingTypes.h>
#include <K1Logic.h>
#include <HomeHeatingController.h>

// Stage 08 phase 4: K1 control state machine (C7). Defaults: travel 120 s,
// overdrive 12 s (recal / anti-seize stroke 132 s), period 30 s, min pulse 1 s,
// gain 2 s/degC, max pulse 10 s, deadband 1 degC, fail position 30 %, FF step 5 %.
// Two styles: direct injection (motion / busy passed by hand, time in seconds)
// and a tiny simulated valve (Sim) that replays issued commands as 1 s motion.

static K1Logic logic;
static HomeHeatingSettings g;
static K1Inputs in;

static const uint32_t RECAL_MS = 132000u;

static K1Inputs baseInputs() {
    K1Inputs i;
    i.heatingEnabled = true;
    i.p4Requested = false;
    i.p4Running = false;
    i.fail = FailMode::None;
    i.h1 = SensorHealth::Ok;
    i.h2 = SensorHealth::Ok;
    i.h3 = SensorHealth::Ok;
    i.h1C = 30.0f;
    i.h2C = 30.0f;
    i.h3C = 70.0f;
    i.k1Busy = false;
    i.k1AntiSeizeOwned = false;
    i.inhibited = false;
    i.motion = K1Motion{0u, 0u};
    return i;
}

void setUp() {
    logic.reset();
    g = guardHomeHeatingSettings(defaultHomeHeatingSettings());
    in = baseInputs();
}
void tearDown() {}

// Direct injection: one update at nowS seconds with the given motion (then cleared).
static K1Decision step(uint64_t nowS, uint32_t openMs = 0u, uint32_t closeMs = 0u) {
    in.motion = K1Motion{openMs, closeMs};
    const K1Decision d = logic.update(in, g, nowS * 1000ULL);
    in.motion = K1Motion{0u, 0u};
    return d;
}

// Boot recal completed by hand at t = 0 / 1 s: estimate known at 0 %.
static void bootKnown() {
    step(0);
    const K1Decision d = step(1, 0u, RECAL_MS);
    TEST_ASSERT_TRUE(d.recalEnded);
}

static void assertNoCmd(const K1Decision& d) {
    TEST_ASSERT_FALSE(d.cmd.issue);
}

static void assertCmd(const K1Decision& d, K1Direction dir, uint32_t ms) {
    TEST_ASSERT_TRUE(d.cmd.issue);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(dir), static_cast<int>(d.cmd.dir));
    TEST_ASSERT_UINT32_WITHIN(1u, ms, d.cmd.ms);
}

static void assertMode(K1Mode m, const K1Decision& d) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(m), static_cast<int>(d.mode));
}

// ---- simulated valve ---------------------------------------------------------
// Each tick advances 1 s: a running pulse yields up to 1000 ms of motion; K1 is
// busy while the pulse has time left; an issued command replaces the pulse.
struct Sim {
    uint64_t nowS = 0;
    bool running = false;
    K1Direction dir = K1Direction::Close;
    uint32_t remaining = 0;

    K1Decision tick() {
        K1Motion m = {0u, 0u};
        if (running) {
            const uint32_t s = remaining < 1000u ? remaining : 1000u;
            if (dir == K1Direction::Open) m.openMs = s; else m.closeMs = s;
            remaining -= s;
            running = remaining > 0u;
        }
        in.motion = m;
        in.k1Busy = running;
        const K1Decision d = logic.update(in, g, nowS * 1000ULL);
        in.motion = K1Motion{0u, 0u};
        in.k1Busy = false;
        if (d.cmd.issue) {
            running = true;
            dir = d.cmd.dir;
            remaining = d.cmd.ms;
        }
        ++nowS;
        return d;
    }

    // Ticks until a command is issued (or maxTicks); returns it, *atS = its tick time.
    K1Decision untilCommand(uint32_t maxTicks, uint64_t* atS) {
        K1Decision d = {};
        for (uint32_t i = 0; i < maxTicks; ++i) {
            const uint64_t t = nowS;
            d = tick();
            if (d.cmd.issue) {
                *atS = t;
                return d;
            }
        }
        *atS = 0;
        return d;
    }

    void ticks(uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) tick();
    }
};

// ---- recalibration -----------------------------------------------------------

void test_boot_recal_close_full_stroke_even_heating_off() {
    in.heatingEnabled = false;
    K1Decision d = step(0);
    assertCmd(d, K1Direction::Close, RECAL_MS);
    assertMode(K1Mode::Recalibrating, d);
    TEST_ASSERT_TRUE(d.recalStarted);
    TEST_ASSERT_FALSE(d.known);

    in.k1Busy = true;
    d = step(1, 0u, 1000u);
    assertNoCmd(d);
    TEST_ASSERT_FALSE(d.recalStarted);
    assertMode(K1Mode::Recalibrating, d);

    in.k1Busy = false;
    d = step(133, 0u, RECAL_MS - 1000u);
    TEST_ASSERT_TRUE(d.recalEnded);
    TEST_ASSERT_TRUE(d.known);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, d.posPct);
    assertMode(K1Mode::Closed, d);
    assertNoCmd(d);

    d = step(134);
    TEST_ASSERT_FALSE(d.recalEnded);
}

void test_boot_recal_simulated_valve() {
    Sim sim;
    K1Decision d = sim.tick();
    assertCmd(d, K1Direction::Close, RECAL_MS);
    for (int i = 0; i < 131; ++i) {
        d = sim.tick();
        assertNoCmd(d);
        assertMode(K1Mode::Recalibrating, d);
    }
    d = sim.tick();   // 132 s of CLOSE motion delivered, K1 idle
    TEST_ASSERT_TRUE(d.recalEnded);
    TEST_ASSERT_TRUE(d.known);
    assertMode(K1Mode::Closed, d);
    assertNoCmd(d);
}

void test_recal_counted_from_actual_motion() {
    step(0);
    K1Decision d = step(50, 0u, 50000u);   // cancelled run, K1 idle
    assertCmd(d, K1Direction::Close, 82000u);
    assertMode(K1Mode::Recalibrating, d);
    TEST_ASSERT_FALSE(d.recalStarted);

    d = step(51, 1000u, 0u);               // OPEN motion restarts the count
    assertCmd(d, K1Direction::Close, RECAL_MS);
    TEST_ASSERT_FALSE(d.known);
}

void test_recal_ends_when_remaining_below_one_second() {
    step(0);
    K1Decision d = step(131, 0u, RECAL_MS - 1000u);   // exactly 1 s left -> not yet
    assertCmd(d, K1Direction::Close, 1000u);
    TEST_ASSERT_FALSE(d.recalEnded);
    d = step(132, 0u, 1u);
    TEST_ASSERT_TRUE(d.recalEnded);
}

void test_recal_waits_for_inhibit() {
    in.inhibited = true;
    K1Decision d = step(0);
    TEST_ASSERT_TRUE(d.recalStarted);
    assertNoCmd(d);
    assertMode(K1Mode::Recalibrating, d);
    d = step(1);
    assertNoCmd(d);
    in.inhibited = false;
    d = step(2);
    assertCmd(d, K1Direction::Close, RECAL_MS);
    TEST_ASSERT_FALSE(d.recalStarted);
}

void test_recal_waits_for_anti_seize() {
    in.k1AntiSeizeOwned = true;
    in.k1Busy = true;
    K1Decision d = step(0);
    TEST_ASSERT_TRUE(d.recalStarted);
    assertNoCmd(d);
    d = step(1, 1000u, 0u);
    assertNoCmd(d);
    in.k1AntiSeizeOwned = false;
    in.k1Busy = false;
    d = step(2);
    assertCmd(d, K1Direction::Close, RECAL_MS);
}

void test_p4_falling_edge_replaces_control_pulse() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    K1Decision d = step(10);
    assertCmd(d, K1Direction::Open, 30000u);
    assertMode(K1Mode::Normal, d);

    in.k1Busy = true;                       // Control OPEN still running
    in.p4Requested = false;
    in.p4Running = false;
    d = step(20, 10000u, 0u);
    TEST_ASSERT_TRUE(d.recalStarted);
    assertCmd(d, K1Direction::Close, RECAL_MS);
    assertMode(K1Mode::Recalibrating, d);

    d = step(21, 0u, 1000u);                // still busy with the recal CLOSE
    assertNoCmd(d);
    TEST_ASSERT_FALSE(d.recalStarted);
}

void test_heating_disable_edge_starts_recal() {
    bootKnown();
    K1Decision d = step(5);
    assertNoCmd(d);
    in.heatingEnabled = false;
    d = step(6);
    TEST_ASSERT_TRUE(d.recalStarted);
    assertCmd(d, K1Direction::Close, RECAL_MS);
    d = step(7, 0u, RECAL_MS);
    TEST_ASSERT_TRUE(d.recalEnded);
    assertMode(K1Mode::Closed, d);
    d = step(8);                            // stays disabled: no new recal
    TEST_ASSERT_FALSE(d.recalStarted);
    assertNoCmd(d);
}

void test_edges_during_recal_do_not_restart_it() {
    in.p4Requested = true;
    step(0);                                // boot recal
    in.p4Requested = false;
    in.heatingEnabled = false;
    K1Decision d = step(1, 0u, 40000u);
    TEST_ASSERT_FALSE(d.recalStarted);
    assertCmd(d, K1Direction::Close, 92000u);
}

// ---- closed / D10 ------------------------------------------------------------

void test_closed_when_p4_not_running() {
    bootKnown();
    K1Decision d = step(5);
    assertMode(K1Mode::Closed, d);
    assertNoCmd(d);
    in.p4Requested = true;                  // relay still locked (D10)
    in.p4Running = false;
    for (uint64_t t = 6; t < 100; ++t) {
        d = step(t);
        assertMode(K1Mode::Closed, d);
        assertNoCmd(d);
    }
}

// ---- normal mode (A1) ----------------------------------------------------------

void test_normal_ff_then_feedback_then_ff_reapply() {
    Sim sim;
    sim.ticks(133);                         // boot recal through the valve
    in.p4Requested = true;
    in.p4Running = true;
    uint64_t at = 0;
    K1Decision d = sim.untilCommand(1, &at);
    assertCmd(d, K1Direction::Open, 30000u);   // FF 25 %, immediate
    assertMode(K1Mode::Normal, d);
    TEST_ASSERT_TRUE(d.ffValid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, d.ffAppliedPct);
    const uint64_t t0 = at;

    in.h2C = 38.5f;                         // err 1.5 -> 3 s feedback; x unchanged
    d = sim.untilCommand(60, &at);
    TEST_ASSERT_EQUAL_UINT64(t0 + 30, at);
    assertCmd(d, K1Direction::Open, 3000u);

    in.h3C = 60.0f;                         // x = 33.3 %: |dx| >= 5 -> FF, no feedback
    d = sim.untilCommand(60, &at);
    TEST_ASSERT_EQUAL_UINT64(t0 + 60, at);
    assertCmd(d, K1Direction::Open, 7000u);    // 27.5 % -> 33.3 %
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 33.333f, d.ffAppliedPct);

    in.h3C = 57.0f;                         // x = 37.0 %: |dx| 3.7 < 5 -> feedback
    d = sim.untilCommand(60, &at);
    TEST_ASSERT_EQUAL_UINT64(t0 + 90, at);
    assertCmd(d, K1Direction::Open, 3000u);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 33.333f, d.ffAppliedPct);
}

void test_period_gating_every_30s_while_idle() {
    Sim sim;
    sim.ticks(133);
    in.p4Requested = true;
    in.p4Running = true;
    uint64_t at = 0;
    sim.untilCommand(1, &at);               // FF 30 s
    const uint64_t t0 = at;
    in.h2C = 38.5f;
    for (int k = 1; k <= 4; ++k) {
        const K1Decision d = sim.untilCommand(100, &at);
        TEST_ASSERT_EQUAL_UINT64(t0 + 30u * static_cast<uint64_t>(k), at);
        assertCmd(d, K1Direction::Open, 3000u);
    }
}

void test_busy_defers_evaluation_without_stacking() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    assertCmd(step(10), K1Direction::Open, 30000u);
    in.h2C = 38.5f;
    in.k1Busy = true;                       // e.g. a long pulse still running
    for (uint64_t t = 11; t < 70; ++t) assertNoCmd(step(t));
    in.k1Busy = false;
    K1Decision d = step(70, 30000u, 0u);    // one evaluation right after idle
    assertCmd(d, K1Direction::Open, 3000u);
    for (uint64_t t = 71; t < 100; ++t) assertNoCmd(step(t));
    assertCmd(step(100, 3000u, 0u), K1Direction::Open, 3000u);
}

void test_min_pulse_skip_through_logic() {
    g.k1MinPulseS = 5.0f;
    g = guardHomeHeatingSettings(g);
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    assertCmd(step(10), K1Direction::Open, 30000u);
    in.h2C = 38.5f;                         // 3 s feedback < 5 s min pulse
    K1Decision d = step(40, 30000u, 0u);
    assertNoCmd(d);
    assertMode(K1Mode::Normal, d);
    assertNoCmd(step(70));
}

void test_end_stop_resync_through_logic() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    in.h3C = 31.0f;                         // H3 - H1 <= smallDiff -> x = 100 %
    K1Decision d = step(10);
    assertCmd(d, K1Direction::Open, 120000u + 12000u);
    d = step(142, 132000u, 0u);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, d.posPct);
    in.h2C = 20.0f;                         // H2 low at 100 %: no pulse toward the end (D8)
    assertNoCmd(step(172));
}

// ---- fail-safe modes -----------------------------------------------------------

void test_feedback_only_when_h1_failed() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    in.fail = FailMode::H1;
    in.h1 = SensorHealth::Failed;
    in.h2C = 38.5f;
    K1Decision d = step(10);                // immediate evaluation, no FF
    assertMode(K1Mode::FeedbackOnly, d);
    assertCmd(d, K1Direction::Open, 3000u);
    TEST_ASSERT_FALSE(d.ffValid);
    d = step(40, 3000u, 0u);
    assertCmd(d, K1Direction::Open, 3000u);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, d.posPct);
    TEST_ASSERT_FALSE(d.ffValid);
}

void test_feedforward_only_when_h2_failed() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    in.fail = FailMode::H2;
    in.h2 = SensorHealth::Failed;
    in.h2C = 20.0f;                         // must never drive feedback
    K1Decision d = step(10);
    assertMode(K1Mode::FeedforwardOnly, d);
    assertCmd(d, K1Direction::Open, 30000u);
    assertNoCmd(step(40, 30000u, 0u));      // x unchanged: nothing, never feedback
    assertNoCmd(step(70));
    in.h3C = 60.0f;                         // |dx| >= 5 -> FF re-applied
    assertCmd(step(100), K1Direction::Open, 10000u);   // 25 % -> 33.3 %
}

void test_feedforward_only_waits_without_h1() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    in.fail = FailMode::H2;
    in.h2 = SensorHealth::Failed;
    in.h1 = SensorHealth::Pending;
    K1Decision d = step(10);
    assertMode(K1Mode::Wait, d);
    assertNoCmd(d);
}

void test_fail_pos_feedback_when_h3_failed() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    in.fail = FailMode::H3;
    in.h3 = SensorHealth::Failed;
    in.h2C = 38.5f;
    K1Decision d = step(10);
    assertMode(K1Mode::FailPosFeedback, d);
    assertCmd(d, K1Direction::Open, 36000u);   // anchor 0 -> 30 %
    in.k1Busy = true;
    assertNoCmd(step(40, 30000u, 0u));
    in.k1Busy = false;
    d = step(46, 6000u, 0u);                // idle, period elapsed -> feedback
    assertCmd(d, K1Direction::Open, 3000u);
    assertNoCmd(step(50, 3000u, 0u));
    assertCmd(step(76), K1Direction::Open, 3000u);
}

// Review-4: FailPosFeedback with H2 Pending (matrix row 8) skips the feedback
// move but still consumes each period; on H2 recovery feedback resumes at the
// next period boundary measured from the last (skipped) evaluation.
void test_fail_pos_feedback_h2_pending_then_recovers() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    in.fail = FailMode::H3;
    in.h3 = SensorHealth::Failed;
    in.h2 = SensorHealth::Pending;
    in.h2C = 38.5f;
    K1Decision d = step(10);
    assertMode(K1Mode::FailPosFeedback, d);
    assertCmd(d, K1Direction::Open, 36000u);   // anchor 0 -> 30 %
    in.k1Busy = true;
    assertNoCmd(step(40, 30000u, 0u));
    in.k1Busy = false;
    d = step(46, 6000u, 0u);                // period 1 evaluated, no feedback
    assertMode(K1Mode::FailPosFeedback, d);
    assertNoCmd(d);
    assertNoCmd(step(60));
    d = step(76);                           // period 2 evaluated, no feedback
    assertMode(K1Mode::FailPosFeedback, d);
    assertNoCmd(d);
    in.h2 = SensorHealth::Ok;
    assertNoCmd(step(77));                  // timestamp advanced at 76: not due yet
    assertNoCmd(step(105));
    d = step(106);                          // 76 + 30 s: feedback resumes
    assertMode(K1Mode::FailPosFeedback, d);
    assertCmd(d, K1Direction::Open, 3000u);
}

void test_fail_pos_fixed_multi_with_p4_off() {
    bootKnown();
    in.fail = FailMode::Multi;
    in.h1 = SensorHealth::Failed;
    in.h2 = SensorHealth::Failed;
    in.h2C = 38.5f;
    K1Decision d = step(10);
    assertMode(K1Mode::FailPosFixed, d);
    assertCmd(d, K1Direction::Open, 36000u);
    d = step(46, 36000u, 0u);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, d.posPct);
    assertNoCmd(d);
    for (uint64_t t = 47; t < 200; ++t) assertNoCmd(step(t));

    in.fail = FailMode::None;               // leave Multi, P4 still off -> Closed
    in.h1 = SensorHealth::Ok;
    in.h2 = SensorHealth::Ok;
    d = step(200);
    assertMode(K1Mode::Closed, d);
    assertCmd(d, K1Direction::Close, 36000u + 12000u);
}

void test_wait_when_h2_pending() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    in.h2 = SensorHealth::Pending;
    for (uint64_t t = 10; t < 200; ++t) {
        const K1Decision d = step(t);
        assertMode(K1Mode::Wait, d);
        assertNoCmd(d);
    }
}

void test_recovery_h1_failed_to_ok_applies_ff_without_recal() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    in.fail = FailMode::H1;
    in.h1 = SensorHealth::Failed;
    in.h2C = 38.5f;
    assertCmd(step(10), K1Direction::Open, 3000u);
    in.fail = FailMode::None;
    in.h1 = SensorHealth::Ok;
    const K1Decision d = step(13, 3000u, 0u);
    assertMode(K1Mode::Normal, d);
    TEST_ASSERT_FALSE(d.recalStarted);
    assertCmd(d, K1Direction::Open, 27000u);   // 2.5 % -> 25 %
    TEST_ASSERT_TRUE(d.ffValid);
}

// ---- OTA inhibit ---------------------------------------------------------------

void test_inhibit_blocks_then_reapplies_ff() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    in.h2C = 40.0f;                         // on setpoint: feedback idle
    assertCmd(step(10), K1Direction::Open, 30000u);
    assertNoCmd(step(40, 30000u, 0u));

    in.inhibited = true;                    // OTA: nothing, even when periods elapse
    in.h3C = 67.0f;                         // x = 27.0 % (|dx| 2 < 5: would be feedback)
    in.h2C = 30.0f;
    for (uint64_t t = 41; t < 200; ++t) assertNoCmd(step(t));

    in.inhibited = false;                   // release: FF re-applied immediately
    const K1Decision d = step(200);
    assertCmd(d, K1Direction::Open, 2432u); // 25 % -> 27.03 %, not a 10 s feedback pulse
    TEST_ASSERT_TRUE(d.ffValid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 27.027f, d.ffAppliedPct);
}

void test_inhibit_interrupted_recal_resumes() {
    assertCmd(step(0), K1Direction::Close, RECAL_MS);
    in.inhibited = true;
    K1Decision d = step(10, 0u, 10000u);    // HwRuntime cancelled the run after 10 s
    assertNoCmd(d);
    assertMode(K1Mode::Recalibrating, d);
    assertNoCmd(step(11));
    in.inhibited = false;
    d = step(12);
    assertCmd(d, K1Direction::Close, RECAL_MS - 10000u);
    TEST_ASSERT_FALSE(d.recalStarted);
}

// ---- anti-seize stroke motion ----------------------------------------------------

void test_anti_seize_full_stroke_returns_to_zero() {
    bootKnown();
    in.k1AntiSeizeOwned = true;
    in.k1Busy = true;
    K1Decision d = step(10, RECAL_MS, 0u);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, d.posPct);
    assertNoCmd(d);
    d = step(142, 0u, RECAL_MS);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, d.posPct);
    in.k1AntiSeizeOwned = false;
    in.k1Busy = false;
    d = step(275);
    assertMode(K1Mode::Closed, d);
    assertNoCmd(d);
}

void test_anti_seize_aborted_stroke_closes_again() {
    bootKnown();
    in.k1AntiSeizeOwned = true;
    in.k1Busy = true;
    K1Decision d = step(50, 40000u, 0u);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 33.333f, d.posPct);
    assertNoCmd(d);
    in.k1AntiSeizeOwned = false;
    in.k1Busy = false;
    d = step(51);
    assertMode(K1Mode::Closed, d);
    assertCmd(d, K1Direction::Close, 40000u + 12000u);
}

// ---- reset -----------------------------------------------------------------------

void test_reset_restarts_boot_recal() {
    bootKnown();
    logic.reset();
    const K1Decision d = step(5);
    TEST_ASSERT_TRUE(d.recalStarted);
    TEST_ASSERT_FALSE(d.known);
    assertCmd(d, K1Direction::Close, RECAL_MS);
}

// ---- stage 09 phase 6: hold + command kind (C13, D11, D12) --------------------

static void assertKind(K1CmdKind k, const K1Decision& d) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(k), static_cast<int>(d.cmdKind));
}

// Sim-driven boot recal, then P4 on: the first Normal tick issues FF 25 %
// (30 s OPEN); the pulse is played out so K1 is idle and at 25 %.
// H2 is held at the setpoint so the following periods issue nothing.
static void simToNormalIdle(Sim& sim) {
    sim.ticks(133);
    in.p4Requested = true;
    in.p4Running = true;
    in.h2C = 40.0f;                         // at the setpoint: no feedback while idling
    uint64_t at = 0;
    const K1Decision d = sim.untilCommand(1, &at);
    assertCmd(d, K1Direction::Open, 30000u);
    assertKind(K1CmdKind::Feedforward, d);
    sim.ticks(31);
}

void test_hold_suppresses_normal_ff_and_feedback() {
    Sim sim;
    sim.ticks(133);
    in.hold = true;
    in.p4Requested = true;
    in.p4Running = true;
    in.h2C = 30.0f;                         // far below the setpoint
    for (int i = 0; i < 100; ++i) {         // > 3 periods
        const K1Decision d = sim.tick();
        assertMode(K1Mode::Normal, d);
        assertNoCmd(d);
        assertKind(K1CmdKind::None, d);
        TEST_ASSERT_FALSE(d.ffValid);
    }
}

void test_hold_release_reapplies_ff_then_feedback() {
    Sim sim;
    simToNormalIdle(sim);
    in.hold = true;
    in.h3C = 66.0f;                         // x = 27.8 %: |dx| < k1FfStep
    in.h2C = 38.5f;
    for (int i = 0; i < 100; ++i) assertNoCmd(sim.tick());
    in.hold = false;
    const uint64_t tRel = sim.nowS;
    K1Decision d = sim.tick();              // same tick: FF re-applied
    assertKind(K1CmdKind::Feedforward, d);
    TEST_ASSERT_TRUE(d.cmd.issue);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Open), static_cast<int>(d.cmd.dir));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 27.778f, d.ffAppliedPct);
    uint64_t at = 0;
    d = sim.untilCommand(60, &at);          // next period: FF within step -> feedback
    TEST_ASSERT_EQUAL_UINT64(tRel + 30, at);
    assertCmd(d, K1Direction::Open, 3000u);
    assertKind(K1CmdKind::Feedback, d);
}

void test_hold_does_not_suppress_recal_on_p4_falling_edge() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    step(10);                               // FF 30 s
    in.hold = true;
    assertNoCmd(step(40, 30000u, 0u));
    in.p4Requested = false;
    in.p4Running = false;
    const K1Decision d = step(41);
    TEST_ASSERT_TRUE(d.recalStarted);
    assertCmd(d, K1Direction::Close, RECAL_MS);
    assertKind(K1CmdKind::Recal, d);
}

void test_hold_does_not_suppress_recal_on_heating_disable() {
    bootKnown();
    in.hold = true;
    in.heatingEnabled = false;
    const K1Decision d = step(5);
    TEST_ASSERT_TRUE(d.recalStarted);
    assertCmd(d, K1Direction::Close, RECAL_MS);
    assertKind(K1CmdKind::Recal, d);
}

// Release while K1 is still busy with the step-test pulse (D12): nothing on the
// release tick (canMove is false), then FF re-applied at the first idle tick.
void test_hold_release_while_busy_waits_for_idle() {
    Sim sim;
    simToNormalIdle(sim);                   // idle at 25 %
    in.hold = true;
    sim.running = true;                     // step-test OPEN pulse, 10 s
    sim.dir = K1Direction::Open;
    sim.remaining = 10000u;
    for (int i = 0; i < 3; ++i) assertNoCmd(sim.tick());
    in.hold = false;
    K1Decision d = sim.tick();              // release tick: K1 still busy
    assertNoCmd(d);
    assertKind(K1CmdKind::None, d);
    for (int i = 0; i < 5; ++i) {           // pulse still running
        TEST_ASSERT_TRUE(sim.running);
        assertNoCmd(sim.tick());
    }
    TEST_ASSERT_TRUE(sim.running);
    d = sim.tick();                         // last 1 s of the pulse: K1 idle now
    TEST_ASSERT_TRUE(d.cmd.issue);
    assertKind(K1CmdKind::Feedforward, d);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Close), static_cast<int>(d.cmd.dir));
    TEST_ASSERT_UINT32_WITHIN(1000u, 10000u, d.cmd.ms);   // 33.3 % back to 25 %
}

void test_hold_does_not_suppress_closed_move() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    step(10);                               // FF 30 s -> 25 %
    in.hold = true;
    assertNoCmd(step(40, 30000u, 0u));
    in.p4Running = false;                   // relay locked: Closed, no recal edge
    const K1Decision d = step(41);
    assertMode(K1Mode::Closed, d);
    TEST_ASSERT_FALSE(d.recalStarted);
    TEST_ASSERT_TRUE(d.cmd.issue);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Close), static_cast<int>(d.cmd.dir));
    assertKind(K1CmdKind::Position, d);
}

void test_hold_does_not_suppress_fail_pos_fixed() {
    bootKnown();
    in.hold = true;
    in.fail = FailMode::Multi;
    in.h1 = SensorHealth::Failed;
    in.h2 = SensorHealth::Failed;
    const K1Decision d = step(10);
    assertMode(K1Mode::FailPosFixed, d);
    assertCmd(d, K1Direction::Open, 36000u);
    assertKind(K1CmdKind::Position, d);
}

void test_fail_pos_feedback_anchor_kind_and_hold() {
    bootKnown();
    in.p4Requested = true;
    in.p4Running = true;
    in.fail = FailMode::H3;
    in.h3 = SensorHealth::Failed;
    in.h2C = 38.5f;
    in.hold = true;
    K1Decision d = step(10);                // anchor is not held
    assertMode(K1Mode::FailPosFeedback, d);
    assertCmd(d, K1Direction::Open, 36000u);
    assertKind(K1CmdKind::Position, d);
    d = step(46, 36000u, 0u);               // periodic feedback is held
    assertNoCmd(d);
    assertKind(K1CmdKind::None, d);
    in.hold = false;
    d = step(47);                           // release: immediate evaluation
    assertCmd(d, K1Direction::Open, 3000u);
    assertKind(K1CmdKind::Feedback, d);
}

void test_boot_recal_and_resume_kind_recal() {
    K1Decision d = step(0);
    assertCmd(d, K1Direction::Close, RECAL_MS);
    assertKind(K1CmdKind::Recal, d);
    d = step(50, 0u, 50000u);               // continuation CLOSE
    assertCmd(d, K1Direction::Close, 82000u);
    assertKind(K1CmdKind::Recal, d);
}

void test_no_command_kind_none() {
    bootKnown();
    const K1Decision d = step(5);           // Closed at 0 %: nothing to do
    assertMode(K1Mode::Closed, d);
    assertNoCmd(d);
    assertKind(K1CmdKind::None, d);
}

// Controller passthrough: HomeHeatingInputs.k1Hold -> K1Inputs.hold.
static HomeHeatingInputs ctlInputs() {
    HomeHeatingInputs ci{};
    ci.sensor[HH_SENSOR_H1] = SensorInput{SensorState::Ok, 30.0f, false};
    ci.sensor[HH_SENSOR_H2] = SensorInput{SensorState::Ok, 30.0f, false};
    ci.sensor[HH_SENSOR_H3] = SensorInput{SensorState::Ok, 70.0f, false};
    ci.sensor[HH_SENSOR_H4] = SensorInput{SensorState::Ok, 50.0f, false};
    ci.p4RelayActual = true;
    ci.k1Motion = K1Motion{0u, 0u};
    return ci;
}

void test_controller_k1_hold_passthrough() {
    HomeHeatingController ref, held;
    ref.reset(0);
    held.reset(0);
    HomeHeatingInputs a = ctlInputs();
    HomeHeatingInputs b = ctlInputs();
    b.k1Hold = true;
    int refPeriodic = 0;
    for (uint64_t t = 0; t <= 100; ++t) {
        const HomeHeatingOutputs oa = ref.update(a, g, t * 1000ULL);
        const HomeHeatingOutputs ob = held.update(b, g, t * 1000ULL);
        a.k1Motion = K1Motion{0u, t == 0 ? RECAL_MS : 0u};
        b.k1Motion = a.k1Motion;
        if (oa.k1.cmdKind == K1CmdKind::Feedforward || oa.k1.cmdKind == K1CmdKind::Feedback) ++refPeriodic;
        TEST_ASSERT_TRUE(ob.k1.cmdKind != K1CmdKind::Feedforward && ob.k1.cmdKind != K1CmdKind::Feedback);
        if (t > 1) TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Mode::Normal), static_cast<int>(ob.k1.mode));
    }
    TEST_ASSERT_TRUE(refPeriodic > 0);      // the reference did issue periodic moves
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_boot_recal_close_full_stroke_even_heating_off);
    RUN_TEST(test_boot_recal_simulated_valve);
    RUN_TEST(test_recal_counted_from_actual_motion);
    RUN_TEST(test_recal_ends_when_remaining_below_one_second);
    RUN_TEST(test_recal_waits_for_inhibit);
    RUN_TEST(test_recal_waits_for_anti_seize);
    RUN_TEST(test_p4_falling_edge_replaces_control_pulse);
    RUN_TEST(test_heating_disable_edge_starts_recal);
    RUN_TEST(test_edges_during_recal_do_not_restart_it);
    RUN_TEST(test_closed_when_p4_not_running);
    RUN_TEST(test_normal_ff_then_feedback_then_ff_reapply);
    RUN_TEST(test_period_gating_every_30s_while_idle);
    RUN_TEST(test_busy_defers_evaluation_without_stacking);
    RUN_TEST(test_min_pulse_skip_through_logic);
    RUN_TEST(test_end_stop_resync_through_logic);
    RUN_TEST(test_feedback_only_when_h1_failed);
    RUN_TEST(test_feedforward_only_when_h2_failed);
    RUN_TEST(test_feedforward_only_waits_without_h1);
    RUN_TEST(test_fail_pos_feedback_when_h3_failed);
    RUN_TEST(test_fail_pos_feedback_h2_pending_then_recovers);
    RUN_TEST(test_fail_pos_fixed_multi_with_p4_off);
    RUN_TEST(test_wait_when_h2_pending);
    RUN_TEST(test_recovery_h1_failed_to_ok_applies_ff_without_recal);
    RUN_TEST(test_inhibit_blocks_then_reapplies_ff);
    RUN_TEST(test_inhibit_interrupted_recal_resumes);
    RUN_TEST(test_anti_seize_full_stroke_returns_to_zero);
    RUN_TEST(test_anti_seize_aborted_stroke_closes_again);
    RUN_TEST(test_reset_restarts_boot_recal);
    RUN_TEST(test_hold_suppresses_normal_ff_and_feedback);
    RUN_TEST(test_hold_release_reapplies_ff_then_feedback);
    RUN_TEST(test_hold_does_not_suppress_recal_on_p4_falling_edge);
    RUN_TEST(test_hold_does_not_suppress_recal_on_heating_disable);
    RUN_TEST(test_hold_release_while_busy_waits_for_idle);
    RUN_TEST(test_hold_does_not_suppress_closed_move);
    RUN_TEST(test_hold_does_not_suppress_fail_pos_fixed);
    RUN_TEST(test_fail_pos_feedback_anchor_kind_and_hold);
    RUN_TEST(test_boot_recal_and_resume_kind_recal);
    RUN_TEST(test_no_command_kind_none);
    RUN_TEST(test_controller_k1_hold_passthrough);
    return UNITY_END();
}
