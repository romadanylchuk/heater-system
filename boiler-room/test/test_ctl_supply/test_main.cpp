#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <BoilerRoomControlSettings.h>
#include <BoilerRoomTypes.h>
#include <StoredEnergy.h>
#include <SupplyLogic.h>

// Stage 07 phase 2: SupplyLogic (P3 NORMAL/OFF/OFFER, anti-freeze, overheat
// dump) with simulated time, plus computeStoredEnergy.

static constexpr uint64_t SEC = 1000ull;
static constexpr uint64_t MIN = 60ull * SEC;

// Simulation: holds the inputs, advances time in 1 s steps and emulates the P3
// relay (relayOn = last decision; lastRunMs = now while the relay is on).
struct Sim {
    SupplyLogic logic;
    BoilerRoomSettings s;
    SensorState t3State;
    float t3C;
    bool flag;
    bool linkUp;
    bool overheat;
    bool relayOn;
    uint64_t lastRunMs;
    uint64_t now;
    SupplyDecision d;
    int clearCount;
    int afStarts;
};
static Sim sim;

void setUp() {
    sim.logic.reset();
    sim.s = guardBoilerRoomSettings(defaultBoilerRoomSettings());
    sim.t3State = SensorState::Ok;
    sim.t3C = 50.0f;
    sim.flag = false;
    sim.linkUp = true;
    sim.overheat = false;
    sim.relayOn = false;
    sim.lastRunMs = 0;
    sim.now = 0;
    sim.d = SupplyDecision{};
    sim.clearCount = 0;
    sim.afStarts = 0;
}
void tearDown() {}

// ---- helpers ---------------------------------------------------------------

static void checkInvariants(const SupplyDecision& d, const SupplyInputs& in) {
    if (d.p3.safety) {
        TEST_ASSERT_TRUE_MESSAGE(d.p3.on, "safety => on");
    } else {
        TEST_ASSERT_EQUAL_MESSAGE(d.p3.on, d.p3.controlOn, "!safety => controlOn == on");
    }
    // controlOn always carries the state-machine output underneath.
    TEST_ASSERT_EQUAL_MESSAGE(d.mode != P3Mode::Off, d.p3.controlOn, "controlOn == (mode != Off)");
    TEST_ASSERT_EQUAL(static_cast<int>(sim.logic.mode()), static_cast<int>(d.mode));
    TEST_ASSERT_EQUAL(in.overheat, d.dumpActive);
    TEST_ASSERT_EQUAL(in.t3 != SensorHealth::Ok, d.offerDisabled);
    if (d.requestFlagClear) {
        TEST_ASSERT_TRUE_MESSAGE(d.mode == P3Mode::Offer, "clear request only on OFFER entry");
    }
    if (d.antiFreezeStarted) {
        TEST_ASSERT_TRUE(d.antiFreezeRunning);
    }
    if (d.mode != P3Mode::Offer) {
        TEST_ASSERT_EQUAL_UINT32(0, d.offerWindowLeftS);
    }
    if (d.antiFreezeRunning || in.relayOn || !sim.s.afEnable) {
        TEST_ASSERT_EQUAL_UINT32(0, d.afInS);
    }
}

// Evaluates one tick at the current sim.now.
static const SupplyDecision& eval() {
    if (sim.relayOn) {
        sim.lastRunMs = sim.now;
    }
    SupplyInputs in{};
    in.t3 = classifySensor(sim.t3State);
    in.t3C = sim.t3C;
    in.flag = sim.flag;
    in.linkUp = sim.linkUp;
    in.overheat = sim.overheat;
    in.relayOn = sim.relayOn;
    in.lastRunMs = sim.lastRunMs;
    sim.d = sim.logic.update(in, sim.s, sim.now);
    checkInvariants(sim.d, in);
    sim.relayOn = sim.d.p3.on;
    if (sim.relayOn) {
        sim.lastRunMs = sim.now;
    }
    if (sim.d.requestFlagClear) {
        ++sim.clearCount;
    }
    if (sim.d.antiFreezeStarted) {
        ++sim.afStarts;
    }
    return sim.d;
}

// Advances 1 s and evaluates.
static const SupplyDecision& step() {
    sim.now += SEC;
    return eval();
}

// Steps in 1 s increments until sim.now == targetMs (the last decision is at targetMs).
static const SupplyDecision& runTo(uint64_t targetMs) {
    while (sim.now < targetMs) {
        step();
    }
    return sim.d;
}

static void assertP3(bool on, bool safety, PumpReason reason, int line) {
    char msg[64];
    snprintf(msg, sizeof(msg), "line %d t=%llu", line, static_cast<unsigned long long>(sim.now));
    TEST_ASSERT_EQUAL_MESSAGE(on, sim.d.p3.on, msg);
    TEST_ASSERT_EQUAL_MESSAGE(safety, sim.d.p3.safety, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(reason), static_cast<int>(sim.d.p3.reason), msg);
}
static void assertMode(P3Mode m, int line) {
    char msg[64];
    snprintf(msg, sizeof(msg), "line %d t=%llu", line, static_cast<unsigned long long>(sim.now));
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(m), static_cast<int>(sim.d.mode), msg);
}
#define ASSERT_P3(on, safety, reason) assertP3(on, safety, PumpReason::reason, __LINE__)
#define ASSERT_MODE(m) assertMode(P3Mode::m, __LINE__)

// Brings the sim to OFF (flag set) at t=0: the first tick is NORMAL -> OFF.
static void startOff(float t3C) {
    sim.flag = true;
    sim.t3C = t3C;
    eval();
    ASSERT_MODE(Off);
}

// Brings the sim to OFFER: OFF at t=0 with T3 hot, OFFER on the next tick.
// Returns the OFFER entry time. The flag self-clear is emulated (flag -> false).
static uint64_t enterOffer() {
    startOff(70.0f);
    step();
    ASSERT_MODE(Offer);
    TEST_ASSERT_TRUE(sim.d.requestFlagClear);
    sim.flag = false;
    return sim.now;
}

// ---- state machine ---------------------------------------------------------

static void test_initial_normal() {
    eval();
    ASSERT_MODE(Normal);
    ASSERT_P3(true, false, SupplyNormal);
    TEST_ASSERT_FALSE(sim.d.requestFlagClear);
    TEST_ASSERT_FALSE(sim.d.offerDisabled);
    TEST_ASSERT_FALSE(sim.d.dumpActive);
    runTo(5 * MIN);
    ASSERT_MODE(Normal);
    ASSERT_P3(true, false, SupplyNormal);
}

static void test_normal_to_off_on_flag() {
    eval();
    ASSERT_MODE(Normal);
    sim.flag = true;
    step();
    ASSERT_MODE(Off);
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_FALSE(sim.d.requestFlagClear);
    runTo(10 * MIN);   // T3 50 < 60: stays OFF
    ASSERT_MODE(Off);
    TEST_ASSERT_EQUAL(0, sim.clearCount);
}

// D6 gap-fill: OFF -> NORMAL when the flag clears (the home needs heat again).
static void test_off_to_normal_on_flag_clear() {
    startOff(50.0f);
    runTo(3 * MIN);
    ASSERT_MODE(Off);
    sim.flag = false;
    step();
    ASSERT_MODE(Normal);
    ASSERT_P3(true, false, SupplyNormal);
    TEST_ASSERT_EQUAL(0, sim.clearCount);
}

static void test_offer_threshold_and_single_clear() {
    startOff(59.9f);
    runTo(5 * MIN);
    ASSERT_MODE(Off);
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_EQUAL(0, sim.clearCount);
    sim.t3C = 60.0f;
    step();
    ASSERT_MODE(Offer);
    ASSERT_P3(true, false, SupplyOffer);
    TEST_ASSERT_TRUE(sim.d.requestFlagClear);
    TEST_ASSERT_EQUAL(1, sim.clearCount);
    // Flag not yet cleared by the runtime: still exactly one request.
    step();
    ASSERT_MODE(Offer);
    TEST_ASSERT_FALSE(sim.d.requestFlagClear);
    runTo(sim.now + 5 * MIN);
    TEST_ASSERT_EQUAL(1, sim.clearCount);
}

static void test_offer_window_ignores_flag_then_off() {
    const uint64_t t0 = enterOffer();
    for (uint64_t t = t0 + SEC; t <= t0 + 599 * SEC; t += SEC) {
        sim.flag = ((t / SEC) % 7) < 3;   // toggles inside the window
        runTo(t);
        ASSERT_MODE(Offer);
        ASSERT_P3(true, false, SupplyOffer);
    }
    TEST_ASSERT_EQUAL(1, sim.clearCount);
    sim.flag = true;
    runTo(t0 + 600 * SEC);
    ASSERT_MODE(Off);
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_EQUAL(1, sim.clearCount);
}

static void test_offer_window_flag_cleared_then_normal() {
    const uint64_t t0 = enterOffer();
    runTo(t0 + 599 * SEC);
    ASSERT_MODE(Offer);
    runTo(t0 + 600 * SEC);
    ASSERT_MODE(Normal);
    ASSERT_P3(true, false, SupplyNormal);
    TEST_ASSERT_EQUAL(1, sim.clearCount);
}

static void test_offer_wait_elapses() {
    const uint64_t t0 = enterOffer();
    sim.flag = true;
    runTo(t0 + 600 * SEC);
    ASSERT_MODE(Off);
    const uint64_t tw = sim.now;   // wait armed here, T3 still hot (70)
    runTo(tw + 59 * MIN + 59 * SEC);
    ASSERT_MODE(Off);
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_EQUAL(1, sim.clearCount);
    runTo(tw + 60 * MIN);
    ASSERT_MODE(Offer);
    TEST_ASSERT_TRUE(sim.d.requestFlagClear);
    TEST_ASSERT_EQUAL(2, sim.clearCount);
}

static void test_offer_wait_zero_reoffers_immediately() {
    sim.s.p3OfferWaitMin = 0;
    const uint64_t t0 = enterOffer();
    sim.flag = true;
    runTo(t0 + 600 * SEC);
    ASSERT_MODE(Off);
    TEST_ASSERT_EQUAL_UINT32(0, sim.d.offerWaitLeftS);
    step();
    ASSERT_MODE(Offer);
    TEST_ASSERT_TRUE(sim.d.requestFlagClear);
    TEST_ASSERT_EQUAL(2, sim.clearCount);
}

// Literal spec: NORMAL -> OFF does not start a wait.
static void test_first_normal_to_off_offers_next_tick() {
    sim.t3C = 70.0f;
    eval();
    ASSERT_MODE(Normal);
    step();
    sim.flag = true;
    step();
    ASSERT_MODE(Off);                 // one transition per tick
    TEST_ASSERT_FALSE(sim.d.requestFlagClear);
    TEST_ASSERT_EQUAL_UINT32(0, sim.d.offerWaitLeftS);
    step();
    ASSERT_MODE(Offer);
    TEST_ASSERT_TRUE(sim.d.requestFlagClear);
}

// An armed wait keeps counting across NORMAL and is not restarted by NORMAL -> OFF.
static void test_armed_wait_survives_normal() {
    const uint64_t t0 = enterOffer();
    sim.flag = true;
    runTo(t0 + 600 * SEC);
    ASSERT_MODE(Off);
    const uint64_t tw = sim.now;
    runTo(tw + 10 * MIN);
    sim.flag = false;
    step();
    ASSERT_MODE(Normal);
    TEST_ASSERT_TRUE(sim.d.offerWaitLeftS > 0);
    runTo(tw + 20 * MIN);
    sim.flag = true;
    step();
    ASSERT_MODE(Off);
    runTo(tw + 59 * MIN + 59 * SEC);
    ASSERT_MODE(Off);
    TEST_ASSERT_EQUAL(1, sim.clearCount);
    runTo(tw + 60 * MIN);
    ASSERT_MODE(Offer);
    TEST_ASSERT_EQUAL(2, sim.clearCount);
}

// ---- gates -----------------------------------------------------------------

static void test_mqtt_loss_during_offer() {
    const uint64_t t0 = enterOffer();
    runTo(t0 + 2 * MIN);
    sim.flag = true;           // the (gated) flag would read false while lost anyway
    sim.linkUp = false;
    step();
    ASSERT_MODE(Normal);
    ASSERT_P3(true, false, SupplyNormal);
    runTo(t0 + 30 * MIN);      // stays NORMAL, no clear request
    ASSERT_MODE(Normal);
    TEST_ASSERT_EQUAL(1, sim.clearCount);
    // Link back with the flag set: the mode resumes from the flag.
    sim.linkUp = true;
    step();
    ASSERT_MODE(Off);
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_FALSE(sim.d.requestFlagClear);
}

static void test_mqtt_loss_during_off() {
    startOff(50.0f);
    runTo(5 * MIN);
    sim.linkUp = false;
    step();
    ASSERT_MODE(Normal);
    ASSERT_P3(true, false, SupplyNormal);
    runTo(20 * MIN);
    ASSERT_MODE(Normal);
    // Link back with the flag cleared: stays NORMAL.
    sim.flag = false;
    sim.linkUp = true;
    step();
    ASSERT_MODE(Normal);
    // Flag set again: OFF.
    sim.flag = true;
    step();
    ASSERT_MODE(Off);
    TEST_ASSERT_EQUAL(0, sim.clearCount);
}

// After a reboot the gated flag reads false and MQTT is down: NORMAL.
static void test_reboot_stays_normal_until_link() {
    sim.flag = false;
    sim.linkUp = false;
    sim.t3C = 70.0f;
    eval();
    for (uint64_t t = MIN; t <= 120 * MIN; t += MIN) {
        runTo(t);
        ASSERT_MODE(Normal);
        ASSERT_P3(true, false, SupplyNormal);
    }
    TEST_ASSERT_EQUAL(0, sim.clearCount);
    TEST_ASSERT_EQUAL(0, sim.afStarts);
    sim.linkUp = true;
    sim.flag = true;
    step();
    ASSERT_MODE(Off);
    ASSERT_P3(false, false, SupplyOff);
}

static void checkT3NotOk(SensorState bad) {
    // From OFF with the flag set and T3 hot.
    setUp();
    startOff(50.0f);
    step();
    ASSERT_MODE(Off);
    sim.t3State = bad;
    sim.t3C = 70.0f;
    step();
    ASSERT_MODE(Normal);
    ASSERT_P3(true, false, SupplyNormal);
    TEST_ASSERT_TRUE(sim.d.offerDisabled);
    runTo(sim.now + 90 * MIN);
    ASSERT_MODE(Normal);
    TEST_ASSERT_TRUE(sim.d.offerDisabled);
    TEST_ASSERT_EQUAL(0, sim.clearCount);

    // From OFFER: abandoned, no second clear request.
    setUp();
    const uint64_t t0 = enterOffer();
    sim.flag = true;
    runTo(t0 + MIN);
    sim.t3State = bad;
    step();
    ASSERT_MODE(Normal);
    TEST_ASSERT_TRUE(sim.d.offerDisabled);
    runTo(t0 + 90 * MIN);
    ASSERT_MODE(Normal);
    TEST_ASSERT_EQUAL(1, sim.clearCount);

    // Fresh logic with the flag set: NORMAL from the start.
    setUp();
    sim.t3State = bad;
    sim.flag = true;
    eval();
    ASSERT_MODE(Normal);
    ASSERT_P3(true, false, SupplyNormal);
    TEST_ASSERT_TRUE(sim.d.offerDisabled);
}

static void test_t3_not_ok_disables_offer() {
    checkT3NotOk(SensorState::Fault);
    checkT3NotOk(SensorState::Unassigned);
    checkT3NotOk(SensorState::Unknown);
}

// ---- overheat dump ---------------------------------------------------------

static void test_overheat_forces_dump_in_off() {
    startOff(55.0f);
    step();
    sim.overheat = true;
    step();
    ASSERT_MODE(Off);
    ASSERT_P3(true, true, OverheatDump);
    TEST_ASSERT_FALSE(sim.d.p3.controlOn);
    TEST_ASSERT_TRUE(sim.d.dumpActive);
    TEST_ASSERT_FALSE(sim.d.requestFlagClear);
}

// Flag set, T3 hot, wait elapsed: no OFFER and no clear for the whole overheat;
// the first tick after the clear may enter OFFER (normal state machine).
static void test_overheat_blocks_offer_then_offer_after_clear() {
    sim.overheat = true;
    startOff(70.0f);
    ASSERT_P3(true, true, OverheatDump);
    runTo(45 * MIN);
    ASSERT_MODE(Off);
    ASSERT_P3(true, true, OverheatDump);
    TEST_ASSERT_FALSE(sim.d.p3.controlOn);
    TEST_ASSERT_EQUAL(0, sim.clearCount);
    TEST_ASSERT_EQUAL(0, sim.afStarts);   // relay ON during the dump
    sim.overheat = false;
    step();
    ASSERT_MODE(Offer);
    ASSERT_P3(true, false, SupplyOffer);
    TEST_ASSERT_TRUE(sim.d.requestFlagClear);
    TEST_ASSERT_EQUAL(1, sim.clearCount);
}

// Binding edge case: the flag stays set and P3 returns to OFF after the clear.
static void test_overheat_clear_returns_to_off() {
    startOff(55.0f);
    sim.overheat = true;
    runTo(20 * MIN);
    ASSERT_P3(true, true, OverheatDump);
    ASSERT_MODE(Off);
    sim.overheat = false;
    step();
    ASSERT_MODE(Off);
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_FALSE(sim.d.dumpActive);
    TEST_ASSERT_EQUAL(0, sim.clearCount);
}

// q3 safety guarantee (review-2 should-fix): the dump is independent of the
// link/T3 gates and of NORMAL mode. Each case: overheat -> ON S OverheatDump
// with controlOn true (NORMAL underneath); clear -> back to ON C SupplyNormal.
static void test_overheat_dump_independent_of_gates() {
    for (int c = 0; c < 3; ++c) {
        setUp();
        sim.t3C = 70.0f;
        if (c == 0) {                 // (a) MQTT down, flag set
            sim.linkUp = false;
            sim.flag = true;
        } else if (c == 1) {          // (b) T3 Fault, flag set
            sim.t3State = SensorState::Fault;
            sim.flag = true;
        } else {                      // (c) NORMAL with the flag cleared
            sim.flag = false;
        }
        char msg[32];
        snprintf(msg, sizeof(msg), "case %c", 'a' + c);
        eval();
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(P3Mode::Normal), static_cast<int>(sim.d.mode), msg);
        sim.overheat = true;
        runTo(10 * MIN);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(P3Mode::Normal), static_cast<int>(sim.d.mode), msg);
        TEST_ASSERT_TRUE_MESSAGE(sim.d.p3.on, msg);
        TEST_ASSERT_TRUE_MESSAGE(sim.d.p3.safety, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(PumpReason::OverheatDump),
            static_cast<int>(sim.d.p3.reason), msg);
        TEST_ASSERT_TRUE_MESSAGE(sim.d.p3.controlOn, msg);
        TEST_ASSERT_TRUE_MESSAGE(sim.d.dumpActive, msg);
        TEST_ASSERT_EQUAL_MESSAGE(0, sim.clearCount, msg);
        sim.overheat = false;
        step();
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(P3Mode::Normal), static_cast<int>(sim.d.mode), msg);
        TEST_ASSERT_TRUE_MESSAGE(sim.d.p3.on, msg);
        TEST_ASSERT_FALSE_MESSAGE(sim.d.p3.safety, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(PumpReason::SupplyNormal),
            static_cast<int>(sim.d.p3.reason), msg);
        TEST_ASSERT_FALSE_MESSAGE(sim.d.dumpActive, msg);
    }
}

static void test_overheat_during_offer_window_expires_normally() {
    const uint64_t t0 = enterOffer();
    sim.flag = true;
    runTo(t0 + 2 * MIN);
    sim.overheat = true;
    step();
    ASSERT_MODE(Offer);
    ASSERT_P3(true, true, OverheatDump);
    TEST_ASSERT_TRUE(sim.d.p3.controlOn);
    runTo(t0 + 599 * SEC);
    ASSERT_MODE(Offer);
    runTo(t0 + 600 * SEC);
    ASSERT_MODE(Off);
    ASSERT_P3(true, true, OverheatDump);
    TEST_ASSERT_FALSE(sim.d.p3.controlOn);
    TEST_ASSERT_TRUE(sim.d.offerWaitLeftS > 0);
    runTo(t0 + 20 * MIN);
    sim.overheat = false;
    step();   // wait armed: no OFFER yet
    ASSERT_MODE(Off);
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_EQUAL(1, sim.clearCount);
}

// ---- anti-freeze -----------------------------------------------------------

static void test_anti_freeze_runs_after_idle_interval() {
    startOff(50.0f);   // T3 below the offer threshold: OFF on its own; lastRun = 0
    runTo(29 * MIN + 59 * SEC);
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_EQUAL(0, sim.afStarts);
    runTo(30 * MIN);
    ASSERT_P3(true, true, AntiFreeze);
    TEST_ASSERT_TRUE(sim.d.antiFreezeStarted);
    TEST_ASSERT_TRUE(sim.d.antiFreezeRunning);
    TEST_ASSERT_FALSE(sim.d.p3.controlOn);
    ASSERT_MODE(Off);
    step();
    TEST_ASSERT_FALSE(sim.d.antiFreezeStarted);   // edge only once
    runTo(30 * MIN + 59 * SEC);
    ASSERT_P3(true, true, AntiFreeze);
    TEST_ASSERT_EQUAL(1, sim.afStarts);
    runTo(31 * MIN);   // 60 s after the start
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_FALSE(sim.d.antiFreezeRunning);
    ASSERT_MODE(Off);
    // Next run 30 min after the stop.
    runTo(60 * MIN + 59 * SEC);
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_EQUAL(1, sim.afStarts);
    runTo(61 * MIN);
    ASSERT_P3(true, true, AntiFreeze);
    TEST_ASSERT_EQUAL(2, sim.afStarts);
    runTo(62 * MIN);
    ASSERT_MODE(Off);
    TEST_ASSERT_EQUAL(0, sim.clearCount);
}

static void test_anti_freeze_disabled_never_runs() {
    sim.s.afEnable = false;
    startOff(50.0f);
    for (uint64_t t = MIN; t <= 180 * MIN; t += MIN) {
        runTo(t);
        ASSERT_P3(false, false, SupplyOff);
        TEST_ASSERT_EQUAL_UINT32(0, sim.d.afInS);
    }
    TEST_ASSERT_EQUAL(0, sim.afStarts);
}

static void test_anti_freeze_disable_mid_run_stops() {
    startOff(50.0f);
    runTo(30 * MIN);
    ASSERT_P3(true, true, AntiFreeze);
    runTo(30 * MIN + 10 * SEC);
    sim.s.afEnable = false;
    step();
    ASSERT_P3(false, false, SupplyOff);
    TEST_ASSERT_FALSE(sim.d.antiFreezeRunning);
    runTo(120 * MIN);
    TEST_ASSERT_EQUAL(1, sim.afStarts);
}

static void test_anti_freeze_not_while_relay_on() {
    sim.flag = false;   // NORMAL: P3 ON the whole time
    eval();
    for (uint64_t t = MIN; t <= 180 * MIN; t += MIN) {
        runTo(t);
        ASSERT_P3(true, false, SupplyNormal);
        TEST_ASSERT_EQUAL_UINT32(0, sim.d.afInS);
    }
    TEST_ASSERT_EQUAL(0, sim.afStarts);
}

// An anti-seize run (external relay activity) resets the shared timer.
static void test_anti_freeze_postponed_by_external_run() {
    startOff(50.0f);
    runTo(1000 * SEC);
    sim.lastRunMs = 1000 * SEC;   // anti-seize exercised P3 just now
    runTo(2799 * SEC);
    TEST_ASSERT_EQUAL(0, sim.afStarts);
    ASSERT_P3(false, false, SupplyOff);
    runTo(2800 * SEC);
    ASSERT_P3(true, true, AntiFreeze);
    TEST_ASSERT_EQUAL(1, sim.afStarts);
}

static void test_anti_freeze_with_overheat_reason_dump() {
    startOff(50.0f);
    runTo(30 * MIN);
    ASSERT_P3(true, true, AntiFreeze);
    runTo(30 * MIN + 10 * SEC);
    sim.overheat = true;
    step();
    ASSERT_P3(true, true, OverheatDump);
    TEST_ASSERT_TRUE(sim.d.antiFreezeRunning);
    TEST_ASSERT_TRUE(sim.d.dumpActive);
}

// ---- countdowns ------------------------------------------------------------

static void test_countdowns() {
    startOff(50.0f);
    TEST_ASSERT_EQUAL_UINT32(1800, sim.d.afInS);
    TEST_ASSERT_EQUAL_UINT32(0, sim.d.offerWindowLeftS);
    TEST_ASSERT_EQUAL_UINT32(0, sim.d.offerWaitLeftS);
    runTo(1000 * SEC);
    TEST_ASSERT_EQUAL_UINT32(800, sim.d.afInS);
    runTo(1799 * SEC);
    TEST_ASSERT_EQUAL_UINT32(1, sim.d.afInS);
    runTo(1800 * SEC);   // running
    TEST_ASSERT_EQUAL_UINT32(0, sim.d.afInS);
    runTo(1860 * SEC);   // stop tick: relay still ON as input
    TEST_ASSERT_EQUAL_UINT32(0, sim.d.afInS);
    step();
    TEST_ASSERT_EQUAL_UINT32(1799, sim.d.afInS);

    // Offer window and wait, including round-up of partial seconds.
    setUp();
    const uint64_t t0 = enterOffer();
    TEST_ASSERT_EQUAL_UINT32(600, sim.d.offerWindowLeftS);
    TEST_ASSERT_EQUAL_UINT32(1799, sim.d.afInS);   // entry tick: relay input still OFF (idle since t=0)
    sim.now = t0 + 500;   // half a second in
    eval();
    TEST_ASSERT_EQUAL_UINT32(0, sim.d.afInS);   // relay ON now
    TEST_ASSERT_EQUAL_UINT32(600, sim.d.offerWindowLeftS);
    sim.now = t0 + SEC;
    eval();
    TEST_ASSERT_EQUAL_UINT32(599, sim.d.offerWindowLeftS);
    runTo(t0 + 599 * SEC);
    TEST_ASSERT_EQUAL_UINT32(1, sim.d.offerWindowLeftS);
    sim.flag = true;
    runTo(t0 + 600 * SEC);
    ASSERT_MODE(Off);
    TEST_ASSERT_EQUAL_UINT32(0, sim.d.offerWindowLeftS);
    TEST_ASSERT_EQUAL_UINT32(3600, sim.d.offerWaitLeftS);
    step();
    TEST_ASSERT_EQUAL_UINT32(3599, sim.d.offerWaitLeftS);
    TEST_ASSERT_EQUAL_UINT32(1799, sim.d.afInS);   // relay went OFF at t0+600 s
    runTo(t0 + 600 * SEC + 3599 * SEC);
    TEST_ASSERT_EQUAL_UINT32(1, sim.d.offerWaitLeftS);
    step();
    ASSERT_MODE(Offer);
    TEST_ASSERT_EQUAL_UINT32(0, sim.d.offerWaitLeftS);
    TEST_ASSERT_EQUAL_UINT32(600, sim.d.offerWindowLeftS);
}

// ---- stored energy ---------------------------------------------------------

static SensorInput ok(float t) { return SensorInput{SensorState::Ok, t, false}; }
static SensorInput bad(SensorState s) { return SensorInput{s, NAN, false}; }

static void assertEnergy(EnergyQuality q, float kWh, const EnergyResult& r) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(q), static_cast<int>(r.quality));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, kWh, r.kWh);
}

static void test_energy_exact() {
    assertEnergy(EnergyQuality::Exact, 17.445f, computeStoredEnergy(ok(70), ok(60), ok(50), 500.0f, 30.0f));
}

static void test_energy_below_base_is_zero() {
    assertEnergy(EnergyQuality::Exact, 0.0f, computeStoredEnergy(ok(25), ok(20), ok(15), 500.0f, 30.0f));
}

static void test_energy_one_layer_missing() {
    const SensorState states[3] = {SensorState::Fault, SensorState::Unassigned, SensorState::Unknown};
    for (SensorState s : states) {
        // avg of 2 layers: (60 + 50) / 2 = 55 -> 500 * 1.163 * 25 / 1000 = 14.5375
        assertEnergy(EnergyQuality::Estimated, 14.5375f, computeStoredEnergy(bad(s), ok(60), ok(50), 500.0f, 30.0f));
        // (70 + 50) / 2 = 60 -> 17.445
        assertEnergy(EnergyQuality::Estimated, 17.445f, computeStoredEnergy(ok(70), bad(s), ok(50), 500.0f, 30.0f));
        // (70 + 60) / 2 = 65 -> 20.3525
        assertEnergy(EnergyQuality::Estimated, 20.3525f, computeStoredEnergy(ok(70), ok(60), bad(s), 500.0f, 30.0f));
    }
}

static void test_energy_two_layers_missing() {
    // Only T4 60 -> 500 * 1.163 * 30 / 1000 = 17.445
    assertEnergy(EnergyQuality::Estimated, 17.445f,
        computeStoredEnergy(bad(SensorState::Fault), ok(60), bad(SensorState::Unknown), 500.0f, 30.0f));
    // Only T3 70 -> 23.26
    assertEnergy(EnergyQuality::Estimated, 23.26f,
        computeStoredEnergy(ok(70), bad(SensorState::Unassigned), bad(SensorState::Fault), 500.0f, 30.0f));
}

static void test_energy_none_unavailable() {
    const EnergyResult r = computeStoredEnergy(
        bad(SensorState::Fault), bad(SensorState::Unassigned), bad(SensorState::Unknown), 500.0f, 30.0f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EnergyQuality::Unavailable), static_cast<int>(r.quality));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.kWh);
}

static void test_energy_volume_and_base_change_result() {
    // V 1000 -> 34.89; base 40 -> 500 * 1.163 * 20 / 1000 = 11.63
    assertEnergy(EnergyQuality::Exact, 34.89f, computeStoredEnergy(ok(70), ok(60), ok(50), 1000.0f, 30.0f));
    assertEnergy(EnergyQuality::Exact, 11.63f, computeStoredEnergy(ok(70), ok(60), ok(50), 500.0f, 40.0f));
}

static void test_energy_quality_keys() {
    TEST_ASSERT_EQUAL_STRING("exact", energyQualityKey(EnergyQuality::Exact));
    TEST_ASSERT_EQUAL_STRING("est", energyQualityKey(EnergyQuality::Estimated));
    TEST_ASSERT_EQUAL_STRING("na", energyQualityKey(EnergyQuality::Unavailable));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_initial_normal);
    RUN_TEST(test_normal_to_off_on_flag);
    RUN_TEST(test_off_to_normal_on_flag_clear);
    RUN_TEST(test_offer_threshold_and_single_clear);
    RUN_TEST(test_offer_window_ignores_flag_then_off);
    RUN_TEST(test_offer_window_flag_cleared_then_normal);
    RUN_TEST(test_offer_wait_elapses);
    RUN_TEST(test_offer_wait_zero_reoffers_immediately);
    RUN_TEST(test_first_normal_to_off_offers_next_tick);
    RUN_TEST(test_armed_wait_survives_normal);
    RUN_TEST(test_mqtt_loss_during_offer);
    RUN_TEST(test_mqtt_loss_during_off);
    RUN_TEST(test_reboot_stays_normal_until_link);
    RUN_TEST(test_t3_not_ok_disables_offer);
    RUN_TEST(test_overheat_forces_dump_in_off);
    RUN_TEST(test_overheat_blocks_offer_then_offer_after_clear);
    RUN_TEST(test_overheat_clear_returns_to_off);
    RUN_TEST(test_overheat_during_offer_window_expires_normally);
    RUN_TEST(test_overheat_dump_independent_of_gates);
    RUN_TEST(test_anti_freeze_runs_after_idle_interval);
    RUN_TEST(test_anti_freeze_disabled_never_runs);
    RUN_TEST(test_anti_freeze_disable_mid_run_stops);
    RUN_TEST(test_anti_freeze_not_while_relay_on);
    RUN_TEST(test_anti_freeze_postponed_by_external_run);
    RUN_TEST(test_anti_freeze_with_overheat_reason_dump);
    RUN_TEST(test_countdowns);
    RUN_TEST(test_energy_exact);
    RUN_TEST(test_energy_below_base_is_zero);
    RUN_TEST(test_energy_one_layer_missing);
    RUN_TEST(test_energy_two_layers_missing);
    RUN_TEST(test_energy_none_unavailable);
    RUN_TEST(test_energy_volume_and_base_change_result);
    RUN_TEST(test_energy_quality_keys);
    return UNITY_END();
}
