#pragma once
#include <unity.h>
#include <stdint.h>
#include "../lib/HwEngine/src/K1Driver.h"

// Native-safe Unity tests for K1Driver (stage 03 phase 2, D11): the power +
// direction pulse state machine, the 1 s dead time around a direction change,
// the replace policy, cancel and motion accounting. Header-only, run via
// HwSuite.h. Test names are prefixed k1_ (D24).
//
// Every test below drives the driver through K1Checked, a thin wrapper that
// asserts the interlock invariant on *every* tick() call: the direction is
// never observed to differ from the previous tick's direction unless both
// that previous tick and the current one have power OFF. Equivalently: power
// is never ON on a tick where the direction changed, and the direction never
// changes while power is (or was, on the immediately preceding tick) ON. This
// is checked at the K1Driver output level (powerOn()/directionOpen()), the
// same signals HwRuntime replays into RelayBank's R2/R3 requests every fast
// tick, so it proves the interlock holds at the relay-output-byte level too.

namespace {

// Begin at a nowMs comfortably larger than K1Driver::DEAD_TIME_MS so begin()'s
// saturate-at-0 boot edge case (see K1Driver.cpp) does not apply, matching the
// realistic firmware case where hw.begin() runs well after boot 0.
constexpr uint64_t K1_BEGIN_MS = 10000;

class K1Checked {
public:
    void begin(uint64_t nowMs) {
        _k1.begin(nowMs);
        _havePrev = false;
    }

    bool requestPulse(K1Direction dir, uint32_t durationMs, uint64_t nowMs, K1Owner owner = K1Owner::Control) {
        return _k1.requestPulse(dir, durationMs, nowMs, owner);
    }

    void cancel(uint64_t nowMs) { _k1.cancel(nowMs); }

    void tick(uint64_t nowMs) {
        _k1.tick(nowMs);
        const bool power = _k1.powerOn();
        const bool dir = _k1.directionOpen();
        if (_havePrev && dir != _prevDir) {
            TEST_ASSERT_FALSE_MESSAGE(_prevPower, "direction changed on the tick right after power was ON");
            TEST_ASSERT_FALSE_MESSAGE(power, "direction changed on a tick where power is ON");
        }
        _prevPower = power;
        _prevDir = dir;
        _havePrev = true;
    }

    bool powerOn() const { return _k1.powerOn(); }
    bool directionOpen() const { return _k1.directionOpen(); }
    bool busy() const { return _k1.busy(); }
    bool running() const { return _k1.running(); }
    K1Direction direction() const { return _k1.direction(); }
    K1Owner owner() const { return _k1.owner(); }
    uint32_t currentRunMs(uint64_t nowMs) const { return _k1.currentRunMs(nowMs); }
    K1Motion takeMotion() { return _k1.takeMotion(); }
    bool hasMoved() const { return _k1.hasMoved(); }
    uint64_t lastMoveMs() const { return _k1.lastMoveMs(); }

private:
    K1Driver _k1;
    bool _havePrev = false;
    bool _prevPower = false;
    bool _prevDir = false;
};

// Ticks k1 in 100ms steps from the current t (exclusive) up to targetMs
// (inclusive, must be a multiple of 100ms above t), advancing t by reference.
void stepTo(K1Checked& k1, uint64_t& t, uint64_t targetMs) {
    while (t < targetMs) {
        t += 100;
        k1.tick(t);
    }
}

}  // namespace

static void k1_test_idle_rest_after_begin() {
    K1Checked k1;
    k1.begin(K1_BEGIN_MS);
    TEST_ASSERT_FALSE(k1.powerOn());
    TEST_ASSERT_FALSE(k1.directionOpen());
    TEST_ASSERT_FALSE(k1.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Close), static_cast<int>(k1.direction()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::None), static_cast<int>(k1.owner()));
}

static void k1_test_close_pulse_from_rest_powers_on_first_tick() {
    K1Checked k1;
    k1.begin(K1_BEGIN_MS);
    uint64_t t = K1_BEGIN_MS;
    TEST_ASSERT_TRUE(k1.requestPulse(K1Direction::Close, 2000, t));

    t += 100;
    k1.tick(t);  // the very first tick after the request
    TEST_ASSERT_TRUE(k1.powerOn());
    TEST_ASSERT_FALSE(k1.directionOpen());
    const uint64_t runStart = t;

    stepTo(k1, t, runStart + 2000);
    TEST_ASSERT_FALSE(k1.powerOn());  // OFF after exactly 2.0s (tick-aligned)
    TEST_ASSERT_FALSE(k1.busy());

    K1Motion m = k1.takeMotion();
    TEST_ASSERT_EQUAL_UINT32(2000, m.closeMs);
    TEST_ASSERT_EQUAL_UINT32(0, m.openMs);
}

static void k1_test_open_pulse_from_rest_waits_dead_time_both_sides() {
    K1Checked k1;
    k1.begin(K1_BEGIN_MS);
    uint64_t t = K1_BEGIN_MS;
    TEST_ASSERT_TRUE(k1.requestPulse(K1Direction::Open, 1000, t));

    // Power stays OFF for the whole dead time after rest; direction flips only
    // once that dead time has elapsed.
    stepTo(k1, t, K1_BEGIN_MS + 900);
    TEST_ASSERT_FALSE(k1.powerOn());
    TEST_ASSERT_FALSE(k1.directionOpen());

    stepTo(k1, t, K1_BEGIN_MS + 1000);
    TEST_ASSERT_TRUE(k1.directionOpen());  // direction now energised (Open)
    TEST_ASSERT_FALSE(k1.powerOn());       // power stays OFF on the same tick

    // A second dead time is required after the direction change before power
    // turns ON.
    stepTo(k1, t, K1_BEGIN_MS + 1900);
    TEST_ASSERT_FALSE(k1.powerOn());

    stepTo(k1, t, K1_BEGIN_MS + 2000);
    TEST_ASSERT_TRUE(k1.powerOn());
    const uint64_t runStart = t;

    stepTo(k1, t, runStart + 1000);
    TEST_ASSERT_FALSE(k1.powerOn());
    const uint64_t offAt = t;

    K1Motion m = k1.takeMotion();
    TEST_ASSERT_EQUAL_UINT32(1000, m.openMs);
    TEST_ASSERT_EQUAL_UINT32(0, m.closeMs);

    // Idle rest: direction stays energised for a full dead time after power
    // OFF, then de-energises to CLOSE.
    stepTo(k1, t, offAt + 900);
    TEST_ASSERT_TRUE(k1.directionOpen());

    stepTo(k1, t, offAt + 1000);
    TEST_ASSERT_FALSE(k1.directionOpen());
}

static void k1_test_reversal_open_to_close_while_running() {
    K1Checked k1;
    k1.begin(K1_BEGIN_MS);
    uint64_t t = K1_BEGIN_MS;
    TEST_ASSERT_TRUE(k1.requestPulse(K1Direction::Open, 5000, t));
    stepTo(k1, t, K1_BEGIN_MS + 2000);  // dead time to energise + dead time to power ON
    TEST_ASSERT_TRUE(k1.powerOn());
    TEST_ASSERT_TRUE(k1.directionOpen());

    stepTo(k1, t, t + 500);  // 500ms into the 5000ms OPEN run
    TEST_ASSERT_TRUE(k1.powerOn());

    TEST_ASSERT_TRUE(k1.requestPulse(K1Direction::Close, 2000, t));  // opposite direction while running
    TEST_ASSERT_FALSE(k1.powerOn());       // power OFF on the same call
    TEST_ASSERT_TRUE(k1.directionOpen());  // direction not yet changed
    TEST_ASSERT_TRUE(k1.busy());
    const uint64_t reqAt = t;

    stepTo(k1, t, reqAt + 900);
    TEST_ASSERT_FALSE(k1.powerOn());
    TEST_ASSERT_TRUE(k1.directionOpen());

    stepTo(k1, t, reqAt + 1000);
    TEST_ASSERT_FALSE(k1.directionOpen());  // direction changed to Close
    TEST_ASSERT_FALSE(k1.powerOn());        // still OFF on the tick the direction changes

    stepTo(k1, t, reqAt + 1900);
    TEST_ASSERT_FALSE(k1.powerOn());

    stepTo(k1, t, reqAt + 2000);
    TEST_ASSERT_TRUE(k1.powerOn());  // power ON >= 1s after the direction change
    TEST_ASSERT_FALSE(k1.directionOpen());

    K1Motion m = k1.takeMotion();
    TEST_ASSERT_EQUAL_UINT32(500, m.openMs);  // the aborted OPEN run's motion accounted
    TEST_ASSERT_EQUAL_UINT32(0, m.closeMs);   // the CLOSE run has just started
}

static void k1_test_same_direction_while_running_extends_without_power_cycle() {
    K1Checked k1;
    k1.begin(K1_BEGIN_MS);
    uint64_t t = K1_BEGIN_MS;
    TEST_ASSERT_TRUE(k1.requestPulse(K1Direction::Close, 5000, t));
    t += 100;
    k1.tick(t);
    TEST_ASSERT_TRUE(k1.powerOn());
    const uint64_t runStart = t;

    stepTo(k1, t, runStart + 1000);  // 1000ms into the original 5000ms run
    TEST_ASSERT_TRUE(k1.powerOn());

    TEST_ASSERT_TRUE(k1.requestPulse(K1Direction::Close, 3000, t));  // same direction, extends
    TEST_ASSERT_TRUE(k1.powerOn());                                  // no power cycle
    const uint64_t extendAt = t;

    stepTo(k1, t, extendAt + 2900);
    TEST_ASSERT_TRUE(k1.powerOn());  // still running: the end moved to extendAt + 3000

    stepTo(k1, t, extendAt + 3000);
    TEST_ASSERT_FALSE(k1.powerOn());

    K1Motion m = k1.takeMotion();
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(extendAt + 3000 - runStart), m.closeMs);
}

static void k1_test_cancel_powers_off_immediately_and_clears_busy() {
    K1Checked running;
    running.begin(K1_BEGIN_MS);
    uint64_t t = K1_BEGIN_MS;
    TEST_ASSERT_TRUE(running.requestPulse(K1Direction::Close, 5000, t));
    t += 100;
    running.tick(t);
    TEST_ASSERT_TRUE(running.powerOn());
    stepTo(running, t, t + 500);  // let the run progress before cancelling
    TEST_ASSERT_TRUE(running.powerOn());

    running.cancel(t);
    TEST_ASSERT_FALSE(running.powerOn());
    TEST_ASSERT_FALSE(running.busy());
    K1Motion m = running.takeMotion();
    TEST_ASSERT_TRUE(m.closeMs > 0);  // the partial run is accounted

    K1Checked pendingOnly;
    pendingOnly.begin(K1_BEGIN_MS);
    TEST_ASSERT_TRUE(pendingOnly.requestPulse(K1Direction::Open, 5000, K1_BEGIN_MS));
    TEST_ASSERT_TRUE(pendingOnly.busy());
    pendingOnly.cancel(K1_BEGIN_MS);
    TEST_ASSERT_FALSE(pendingOnly.busy());
    TEST_ASSERT_FALSE(pendingOnly.powerOn());
}

static void k1_test_invalid_duration_rejected() {
    K1Checked k1;
    k1.begin(K1_BEGIN_MS);

    TEST_ASSERT_FALSE(k1.requestPulse(K1Direction::Open, 0, K1_BEGIN_MS));
    TEST_ASSERT_FALSE(k1.busy());

    TEST_ASSERT_FALSE(k1.requestPulse(K1Direction::Open, K1Driver::MAX_PULSE_MS + 1, K1_BEGIN_MS));
    TEST_ASSERT_FALSE(k1.busy());

    TEST_ASSERT_TRUE(k1.requestPulse(K1Direction::Open, K1Driver::MAX_PULSE_MS, K1_BEGIN_MS));  // boundary: valid
    TEST_ASSERT_TRUE(k1.busy());
}

static void k1_test_long_run_132s_exact_to_tick() {
    K1Checked k1;
    k1.begin(K1_BEGIN_MS);
    uint64_t t = K1_BEGIN_MS;
    TEST_ASSERT_TRUE(k1.requestPulse(K1Direction::Close, 132000, t));
    t += 100;
    k1.tick(t);
    TEST_ASSERT_TRUE(k1.powerOn());
    const uint64_t runStart = t;

    stepTo(k1, t, runStart + 131900);
    TEST_ASSERT_TRUE(k1.powerOn());

    stepTo(k1, t, runStart + 132000);
    TEST_ASSERT_FALSE(k1.powerOn());

    K1Motion m = k1.takeMotion();
    TEST_ASSERT_EQUAL_UINT32(132000, m.closeMs);
}

static void k1_test_owner_and_motion_tracking() {
    K1Checked k1;
    k1.begin(K1_BEGIN_MS);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::None), static_cast<int>(k1.owner()));
    TEST_ASSERT_FALSE(k1.hasMoved());

    uint64_t t = K1_BEGIN_MS;
    TEST_ASSERT_TRUE(k1.requestPulse(K1Direction::Open, 1000, t, K1Owner::AntiSeize));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::AntiSeize), static_cast<int>(k1.owner()));  // pending owner
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Open), static_cast<int>(k1.direction()));

    stepTo(k1, t, K1_BEGIN_MS + 2000);  // dead time to energise + dead time to power ON
    TEST_ASSERT_TRUE(k1.powerOn());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::AntiSeize), static_cast<int>(k1.owner()));
    TEST_ASSERT_TRUE(k1.hasMoved());
    TEST_ASSERT_TRUE(k1.lastMoveMs() == t);

    stepTo(k1, t, t + 1000);
    TEST_ASSERT_FALSE(k1.powerOn());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::None), static_cast<int>(k1.owner()));  // idle again
}

// Runs every test in this suite. Call from runHwSuite().
inline void runK1DriverSuite() {
    RUN_TEST(k1_test_idle_rest_after_begin);
    RUN_TEST(k1_test_close_pulse_from_rest_powers_on_first_tick);
    RUN_TEST(k1_test_open_pulse_from_rest_waits_dead_time_both_sides);
    RUN_TEST(k1_test_reversal_open_to_close_while_running);
    RUN_TEST(k1_test_same_direction_while_running_extends_without_power_cycle);
    RUN_TEST(k1_test_cancel_powers_off_immediately_and_clears_busy);
    RUN_TEST(k1_test_invalid_duration_rejected);
    RUN_TEST(k1_test_long_run_132s_exact_to_tick);
    RUN_TEST(k1_test_owner_and_motion_tracking);
}
