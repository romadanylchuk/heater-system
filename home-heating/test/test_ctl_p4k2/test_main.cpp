#include <unity.h>
#include <stdio.h>
#include <HomeHeatingControlSettings.h>
#include <HomeHeatingTypes.h>
#include <K2Logic.h>
#include <NoNeed.h>
#include <P4Logic.h>

// Stage 08 phase 2: P4 rule + off-delay (C3), K2 diverter latches (C4) and the
// "no need" signal (C5). Simulated uint64_t ms time only.

static const uint64_t BOOT_MS = 1000;
static const uint64_t DELAY_MS = 5ULL * 60000ULL;   // default p4OffDelay 5 min

static P4Logic p4;
static K2Logic k2;
static HomeHeatingSettings settings;
static P4Decision p4Last;
static K2Decision k2Last;

void setUp() {
    p4.reset(BOOT_MS);
    k2.reset();
    settings = guardHomeHeatingSettings(defaultHomeHeatingSettings());
    p4Last = P4Decision{};
    k2Last = K2Decision{};
}
void tearDown() {}

// ---- helpers ---------------------------------------------------------------

static const SensorHealth OK = SensorHealth::Ok;
static const SensorHealth FAILED = SensorHealth::Failed;
static const SensorHealth PENDING = SensorHealth::Pending;

static P4Decision p4Tick(uint64_t nowMs, SensorHealth h3, float h3C, FailMode fail = FailMode::None,
                         bool heatingEnabled = true) {
    p4Last = p4.update(P4Inputs{heatingEnabled, h3, h3C, fail}, settings, nowMs);
    if (p4Last.on) {
        TEST_ASSERT_FALSE_MESSAGE(p4Last.offDelayElapsed, "on => !offDelayElapsed");
    }
    return p4Last;
}

static void assertP4(bool on, P4Reason reason, int line) {
    char msg[48];
    snprintf(msg, sizeof(msg), "line %d", line);
    TEST_ASSERT_EQUAL_MESSAGE(on, p4Last.on, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(reason), static_cast<int>(p4Last.reason), msg);
}
#define ASSERT_P4(on, reason) assertP4(on, P4Reason::reason, __LINE__)

static K2Decision k2Tick(SensorHealth h3, float h3C, SensorHealth h4, float h4C) {
    k2Last = k2.update(h3, h3C, h4, h4C, settings);
    return k2Last;
}

static void assertK2(bool bypass, K2Reason reason, int line) {
    char msg[48];
    snprintf(msg, sizeof(msg), "line %d", line);
    TEST_ASSERT_EQUAL_MESSAGE(bypass, k2Last.bypass, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(reason), static_cast<int>(k2Last.reason), msg);
}
#define ASSERT_K2(bypass, reason) assertK2(bypass, K2Reason::reason, __LINE__)

// ---- P4 --------------------------------------------------------------------

static void test_p4_demand_on() {
    p4Tick(BOOT_MS, OK, 70.0f);
    ASSERT_P4(true, Demand);
    TEST_ASSERT_FALSE(p4Last.offDelayElapsed);
    TEST_ASSERT_EQUAL_UINT32(0, p4Last.offDelayLeftS);
}

static void test_p4_demand_at_exact_setpoint() {
    p4Tick(BOOT_MS, OK, 40.0f);
    ASSERT_P4(true, Demand);
    p4Tick(BOOT_MS + 1000, OK, 39.9f);
    ASSERT_P4(true, OffDelay);
}

static void test_p4_heating_off_immediately() {
    p4Tick(BOOT_MS, OK, 70.0f);
    ASSERT_P4(true, Demand);
    p4Tick(BOOT_MS + 1000, OK, 70.0f, FailMode::None, false);
    ASSERT_P4(false, HeatingOff);
    TEST_ASSERT_TRUE(p4Last.offDelayElapsed);
    TEST_ASSERT_EQUAL_UINT32(0, p4Last.offDelayLeftS);
    // Heating off wins over every fail-safe.
    p4Tick(BOOT_MS + 2000, FAILED, 0.0f, FailMode::H3, false);
    ASSERT_P4(false, HeatingOff);
    p4Tick(BOOT_MS + 3000, OK, 20.0f, FailMode::Multi, false);
    ASSERT_P4(false, HeatingOff);
    TEST_ASSERT_TRUE(p4Last.offDelayElapsed);
}

static void test_p4_off_delay_then_supply_cold() {
    const uint64_t t0 = BOOT_MS + 10000;
    p4Tick(t0, OK, 70.0f);
    ASSERT_P4(true, Demand);
    p4Tick(t0 + 1, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    TEST_ASSERT_FALSE(p4Last.offDelayElapsed);
    TEST_ASSERT_EQUAL_UINT32(300, p4Last.offDelayLeftS);
    p4Tick(t0 + 299000, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    TEST_ASSERT_EQUAL_UINT32(1, p4Last.offDelayLeftS);
    p4Tick(t0 + DELAY_MS - 1, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    p4Tick(t0 + DELAY_MS, OK, 35.0f);
    ASSERT_P4(false, SupplyCold);
    TEST_ASSERT_TRUE(p4Last.offDelayElapsed);
    TEST_ASSERT_EQUAL_UINT32(0, p4Last.offDelayLeftS);
    p4Tick(t0 + DELAY_MS + 60000, OK, 35.0f);
    ASSERT_P4(false, SupplyCold);
    TEST_ASSERT_TRUE(p4Last.offDelayElapsed);
}

static void test_p4_warm_reading_restarts_delay() {
    const uint64_t t0 = BOOT_MS;
    p4Tick(t0, OK, 70.0f);
    p4Tick(t0 + 1000, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    p4Tick(t0 + 200000, OK, 41.0f);
    ASSERT_P4(true, Demand);
    p4Tick(t0 + 201000, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    // The original delay would have ended at t0 + 300 s.
    p4Tick(t0 + DELAY_MS, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    p4Tick(t0 + 200000 + DELAY_MS - 1, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    p4Tick(t0 + 200000 + DELAY_MS, OK, 35.0f);
    ASSERT_P4(false, SupplyCold);
}

static void test_p4_boot_cold_delay_runs_from_boot() {
    p4Tick(BOOT_MS, OK, 35.0f);
    ASSERT_P4(false, SupplyCold);
    TEST_ASSERT_FALSE(p4Last.offDelayElapsed);
    TEST_ASSERT_EQUAL_UINT32(300, p4Last.offDelayLeftS);
    p4Tick(BOOT_MS + 1000, OK, 35.0f);
    TEST_ASSERT_EQUAL_UINT32(299, p4Last.offDelayLeftS);
    p4Tick(BOOT_MS + 1500, OK, 35.0f);
    TEST_ASSERT_EQUAL_UINT32(299, p4Last.offDelayLeftS);   // ceil
    p4Tick(BOOT_MS + DELAY_MS - 1, OK, 35.0f);
    ASSERT_P4(false, SupplyCold);
    TEST_ASSERT_FALSE(p4Last.offDelayElapsed);
    TEST_ASSERT_EQUAL_UINT32(1, p4Last.offDelayLeftS);
    p4Tick(BOOT_MS + DELAY_MS, OK, 35.0f);
    ASSERT_P4(false, SupplyCold);
    TEST_ASSERT_TRUE(p4Last.offDelayElapsed);
    TEST_ASSERT_EQUAL_UINT32(0, p4Last.offDelayLeftS);
}

static void test_p4_zero_delay_off_on_first_cold_tick() {
    settings.p4OffDelayMin = 0;
    p4Tick(BOOT_MS, OK, 70.0f);
    ASSERT_P4(true, Demand);
    p4Tick(BOOT_MS + 1000, OK, 35.0f);
    ASSERT_P4(false, SupplyCold);
    TEST_ASSERT_TRUE(p4Last.offDelayElapsed);
    TEST_ASSERT_EQUAL_UINT32(0, p4Last.offDelayLeftS);
}

static void test_p4_h3_failed_forced_on() {
    p4Tick(BOOT_MS, FAILED, 0.0f, FailMode::H3);
    ASSERT_P4(true, H3FaultForced);
    TEST_ASSERT_FALSE(p4Last.offDelayElapsed);
    TEST_ASSERT_EQUAL_UINT32(0, p4Last.offDelayLeftS);
}

static void test_p4_h3_failed_multi_off() {
    p4Tick(BOOT_MS, OK, 70.0f);
    ASSERT_P4(true, Demand);
    p4Tick(BOOT_MS + 1000, FAILED, 0.0f, FailMode::Multi);
    ASSERT_P4(false, MultiFaultOff);
    TEST_ASSERT_EQUAL_UINT32(0, p4Last.offDelayLeftS);
}

static void test_p4_h3_pending_off_immediately() {
    p4Tick(BOOT_MS, OK, 70.0f);
    ASSERT_P4(true, Demand);
    p4Tick(BOOT_MS + 1000, PENDING, 0.0f);
    ASSERT_P4(false, SensorWait);
    TEST_ASSERT_FALSE(p4Last.offDelayElapsed);   // last demand 1 s ago
    TEST_ASSERT_EQUAL_UINT32(0, p4Last.offDelayLeftS);
    // Back to Ok but cold: P4 was OFF, so no off-delay ON.
    p4Tick(BOOT_MS + 2000, OK, 35.0f);
    ASSERT_P4(false, SupplyCold);
    TEST_ASSERT_FALSE(p4Last.offDelayElapsed);
    TEST_ASSERT_EQUAL_UINT32(298, p4Last.offDelayLeftS);
}

static void test_p4_multi_with_h3_ok_forced_on() {
    p4Tick(BOOT_MS, OK, 20.0f, FailMode::Multi);
    ASSERT_P4(true, MultiFaultForced);
    TEST_ASSERT_FALSE(p4Last.offDelayElapsed);
}

static void test_p4_off_delay_runs_from_last_forced_tick() {
    const uint64_t t0 = BOOT_MS + 5000;
    p4Tick(t0, OK, 20.0f, FailMode::Multi);
    ASSERT_P4(true, MultiFaultForced);
    p4Tick(t0 + 10000, OK, 20.0f, FailMode::Multi);
    ASSERT_P4(true, MultiFaultForced);
    const uint64_t lastForced = t0 + 10000;
    p4Tick(lastForced + 1000, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    TEST_ASSERT_EQUAL_UINT32(299, p4Last.offDelayLeftS);
    p4Tick(lastForced + DELAY_MS - 1, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    p4Tick(lastForced + DELAY_MS, OK, 35.0f);
    ASSERT_P4(false, SupplyCold);
    TEST_ASSERT_TRUE(p4Last.offDelayElapsed);

    // Same for the H3-fault forced ON.
    p4.reset(BOOT_MS);
    const uint64_t t1 = BOOT_MS + 20000;
    p4Tick(t1, FAILED, 0.0f, FailMode::H3);
    ASSERT_P4(true, H3FaultForced);
    p4Tick(t1 + 1000, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    p4Tick(t1 + DELAY_MS - 1, OK, 35.0f);
    ASSERT_P4(true, OffDelay);
    p4Tick(t1 + DELAY_MS, OK, 35.0f);
    ASSERT_P4(false, SupplyCold);
}

static void test_p4_off_delay_left_counts_down() {
    const uint64_t t0 = BOOT_MS;
    p4Tick(t0, OK, 70.0f);
    uint32_t prev = 301;
    for (uint64_t s = 1; s < 300; s += 37) {
        p4Tick(t0 + s * 1000, OK, 35.0f);
        ASSERT_P4(true, OffDelay);
        TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(300 - s), p4Last.offDelayLeftS);
        TEST_ASSERT_TRUE(p4Last.offDelayLeftS < prev);
        prev = p4Last.offDelayLeftS;
    }
}

// ---- K2 --------------------------------------------------------------------

static void test_k2_tank_when_charging() {
    k2Tick(OK, 70.0f, OK, 50.0f);
    ASSERT_K2(false, Charging);
}

static void test_k2_delta_latch() {
    settings.k2H3Min = 40.0f;
    k2Tick(OK, 53.5f, OK, 50.0f);
    ASSERT_K2(false, Charging);
    k2Tick(OK, 52.0f, OK, 50.0f);
    ASSERT_K2(false, Charging);            // still above H4 + 1
    k2Tick(OK, 51.0f, OK, 50.0f);
    ASSERT_K2(true, DeltaLow);             // <= H4 + D - DHyst
    k2Tick(OK, 53.0f, OK, 50.0f);
    ASSERT_K2(true, DeltaLow);             // needs > H4 + D
    k2Tick(OK, 53.1f, OK, 50.0f);
    ASSERT_K2(false, Charging);
}

static void test_k2_h3min_latch() {
    k2Tick(OK, 64.9f, OK, 30.0f);
    ASSERT_K2(true, H3Low);
    k2Tick(OK, 65.0f, OK, 30.0f);
    ASSERT_K2(false, Charging);
    k2Tick(OK, 62.5f, OK, 30.0f);
    ASSERT_K2(false, Charging);
    k2Tick(OK, 62.0f, OK, 30.0f);
    ASSERT_K2(false, Charging);            // off only below H3min - hyst
    k2Tick(OK, 61.9f, OK, 30.0f);
    ASSERT_K2(true, H3Low);
    k2Tick(OK, 64.9f, OK, 30.0f);
    ASSERT_K2(true, H3Low);
}

static void test_k2_h4max_latch() {
    k2Tick(OK, 80.0f, OK, 69.9f);
    ASSERT_K2(false, Charging);
    k2Tick(OK, 80.0f, OK, 70.0f);
    ASSERT_K2(true, H4Full);
    k2Tick(OK, 80.0f, OK, 67.5f);
    ASSERT_K2(true, H4Full);
    k2Tick(OK, 80.0f, OK, 67.0f);
    ASSERT_K2(false, Charging);

    // A first reading at/above H4max initialises the latch to "full".
    k2.reset();
    k2Tick(OK, 80.0f, OK, 70.0f);
    ASSERT_K2(true, H4Full);
}

static void test_k2_reason_priority() {
    k2Tick(OK, 60.0f, OK, 72.0f);          // all three blocked
    ASSERT_K2(true, H4Full);
    k2.reset();
    k2Tick(OK, 60.0f, OK, 59.0f);          // H3 low + delta low
    ASSERT_K2(true, H3Low);
    k2.reset();
    k2Tick(OK, 66.0f, OK, 64.0f);          // delta low only
    ASSERT_K2(true, DeltaLow);
    k2.reset();
    k2Tick(OK, 72.0f, OK, 71.0f);          // H4 full + delta low
    ASSERT_K2(true, H4Full);
}

static void test_k2_faults_bypass() {
    k2Tick(OK, 70.0f, OK, 50.0f);
    ASSERT_K2(false, Charging);
    k2Tick(OK, 70.0f, FAILED, 0.0f);
    ASSERT_K2(true, H4Fault);
    k2.reset();
    k2Tick(FAILED, 0.0f, OK, 50.0f);
    ASSERT_K2(true, H3Fault);
    k2Tick(FAILED, 0.0f, FAILED, 0.0f);    // H4 first
    ASSERT_K2(true, H4Fault);
    k2Tick(PENDING, 0.0f, FAILED, 0.0f);   // Failed beats Pending
    ASSERT_K2(true, H4Fault);
    k2.reset();
    k2Tick(FAILED, 0.0f, PENDING, 0.0f);
    ASSERT_K2(true, H3Fault);
}

static void test_k2_pending_holds_previous() {
    k2Tick(OK, 70.0f, OK, 50.0f);
    ASSERT_K2(false, Charging);
    k2Tick(PENDING, 0.0f, OK, 50.0f);
    ASSERT_K2(false, SensorWait);
    k2Tick(OK, 70.0f, PENDING, 0.0f);
    ASSERT_K2(false, SensorWait);

    k2Tick(OK, 70.0f, OK, 75.0f);
    ASSERT_K2(true, H4Full);
    k2Tick(PENDING, 0.0f, OK, 75.0f);
    ASSERT_K2(true, SensorWait);
    k2Tick(OK, 70.0f, PENDING, 0.0f);
    ASSERT_K2(true, SensorWait);

    // After a fault, Pending keeps the fault's bypass.
    k2.reset();
    k2Tick(FAILED, 0.0f, OK, 50.0f);
    k2Tick(PENDING, 0.0f, OK, 50.0f);
    ASSERT_K2(true, SensorWait);
}

static void test_k2_boot_pending_is_tank() {
    k2Tick(PENDING, 0.0f, PENDING, 0.0f);
    ASSERT_K2(false, SensorWait);
    k2Tick(PENDING, 0.0f, OK, 50.0f);
    ASSERT_K2(false, SensorWait);
}

static void test_k2_latches_reinitialise_after_fault() {
    settings.k2H3Min = 40.0f;
    k2Tick(OK, 53.5f, OK, 50.0f);
    ASSERT_K2(false, Charging);
    k2Tick(OK, 52.0f, OK, 50.0f);
    ASSERT_K2(false, Charging);            // latched inside the hysteresis band
    k2Tick(FAILED, 0.0f, OK, 50.0f);
    ASSERT_K2(true, H3Fault);
    k2Tick(OK, 52.0f, OK, 50.0f);
    ASSERT_K2(true, DeltaLow);             // re-initialised: 52 > 53 is false
    k2Tick(OK, 53.1f, OK, 50.0f);
    ASSERT_K2(false, Charging);

    // Pending invalidates the latches too.
    k2.reset();
    settings = guardHomeHeatingSettings(defaultHomeHeatingSettings());
    k2Tick(OK, 65.0f, OK, 30.0f);
    ASSERT_K2(false, Charging);
    k2Tick(OK, 63.0f, OK, 30.0f);
    ASSERT_K2(false, Charging);            // latched ok (>= 62)
    k2Tick(OK, 63.0f, PENDING, 0.0f);
    ASSERT_K2(false, SensorWait);
    k2Tick(OK, 63.0f, OK, 30.0f);
    ASSERT_K2(true, H3Low);                // re-init: 63 < 65
}

static void test_k2_guarded_delta_hysteresis() {
    HomeHeatingSettings raw = defaultHomeHeatingSettings();
    raw.k2Delta = 3.0f;
    raw.k2DeltaHyst = 5.0f;
    raw.k2H3Min = 40.0f;
    settings = guardHomeHeatingSettings(raw);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, settings.k2DeltaHyst);
    k2Tick(OK, 53.5f, OK, 50.0f);
    ASSERT_K2(false, Charging);
    k2Tick(OK, 50.6f, OK, 50.0f);
    ASSERT_K2(false, Charging);
    k2Tick(OK, 50.5f, OK, 50.0f);
    ASSERT_K2(true, DeltaLow);             // stops at H4 + 0.5
}

static void test_k2_independent_of_heating_enabled() {
    settings.heatingEnabled = false;
    k2Tick(OK, 70.0f, OK, 50.0f);
    ASSERT_K2(false, Charging);
    k2Tick(OK, 70.0f, OK, 75.0f);
    ASSERT_K2(true, H4Full);
}

// ---- no-need ---------------------------------------------------------------

static NoNeedInputs allGood() {
    NoNeedInputs in{};
    in.anyPending = false;
    in.h3FailedHeating = false;
    in.k2BypassRequested = true;
    in.k2BypassActual = true;
    in.k2ExerciseRunning = false;
    in.p4Requested = false;
    in.p4RelayActual = false;
    in.p4ExerciseRunning = false;
    in.p4OffDelayElapsed = true;
    return in;
}

static void test_no_need_all_good() {
    TEST_ASSERT_TRUE(computeNoNeed(allGood()));
}

static void test_no_need_single_blockers() {
    NoNeedInputs in = allGood();
    in.anyPending = true;
    TEST_ASSERT_FALSE(computeNoNeed(in));
    in = allGood();
    in.h3FailedHeating = true;
    TEST_ASSERT_FALSE(computeNoNeed(in));
    in = allGood();
    in.k2BypassRequested = false;
    TEST_ASSERT_FALSE(computeNoNeed(in));
    in = allGood();
    in.k2BypassActual = false;              // actual TANK
    TEST_ASSERT_FALSE(computeNoNeed(in));
    in = allGood();
    in.p4Requested = true;
    TEST_ASSERT_FALSE(computeNoNeed(in));
    in = allGood();
    in.p4RelayActual = true;               // actual ON
    TEST_ASSERT_FALSE(computeNoNeed(in));
    in = allGood();
    in.p4OffDelayElapsed = false;
    TEST_ASSERT_FALSE(computeNoNeed(in));
}

static void test_no_need_a2_actual_and_requested() {
    // K2 requested bypass, actual still TANK (relay lock) -> false.
    NoNeedInputs in = allGood();
    in.k2BypassActual = false;
    TEST_ASSERT_FALSE(computeNoNeed(in));
    // K2 requested TANK while actual still bypass -> false immediately.
    in = allGood();
    in.k2BypassRequested = false;
    in.k2BypassActual = true;
    TEST_ASSERT_FALSE(computeNoNeed(in));
    // P4 requested ON while actual still OFF (lock) -> false immediately.
    in = allGood();
    in.p4Requested = true;
    in.p4RelayActual = false;
    TEST_ASSERT_FALSE(computeNoNeed(in));
}

static void test_no_need_exercise_transparency() {
    // K2 exercise running: actual TANK, request bypass -> true.
    NoNeedInputs in = allGood();
    in.k2ExerciseRunning = true;
    in.k2BypassActual = false;
    TEST_ASSERT_TRUE(computeNoNeed(in));
    in.k2BypassRequested = false;          // the request still decides
    TEST_ASSERT_FALSE(computeNoNeed(in));
    // P4 exercise running: actual ON, request OFF -> true.
    in = allGood();
    in.p4ExerciseRunning = true;
    in.p4RelayActual = true;
    TEST_ASSERT_TRUE(computeNoNeed(in));
    in.p4Requested = true;
    TEST_ASSERT_FALSE(computeNoNeed(in));
    // Both exercising at once.
    in = allGood();
    in.k2ExerciseRunning = true;
    in.k2BypassActual = false;
    in.p4ExerciseRunning = true;
    in.p4RelayActual = true;
    TEST_ASSERT_TRUE(computeNoNeed(in));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_p4_demand_on);
    RUN_TEST(test_p4_demand_at_exact_setpoint);
    RUN_TEST(test_p4_heating_off_immediately);
    RUN_TEST(test_p4_off_delay_then_supply_cold);
    RUN_TEST(test_p4_warm_reading_restarts_delay);
    RUN_TEST(test_p4_boot_cold_delay_runs_from_boot);
    RUN_TEST(test_p4_zero_delay_off_on_first_cold_tick);
    RUN_TEST(test_p4_h3_failed_forced_on);
    RUN_TEST(test_p4_h3_failed_multi_off);
    RUN_TEST(test_p4_h3_pending_off_immediately);
    RUN_TEST(test_p4_multi_with_h3_ok_forced_on);
    RUN_TEST(test_p4_off_delay_runs_from_last_forced_tick);
    RUN_TEST(test_p4_off_delay_left_counts_down);
    RUN_TEST(test_k2_tank_when_charging);
    RUN_TEST(test_k2_delta_latch);
    RUN_TEST(test_k2_h3min_latch);
    RUN_TEST(test_k2_h4max_latch);
    RUN_TEST(test_k2_reason_priority);
    RUN_TEST(test_k2_faults_bypass);
    RUN_TEST(test_k2_pending_holds_previous);
    RUN_TEST(test_k2_boot_pending_is_tank);
    RUN_TEST(test_k2_latches_reinitialise_after_fault);
    RUN_TEST(test_k2_guarded_delta_hysteresis);
    RUN_TEST(test_k2_independent_of_heating_enabled);
    RUN_TEST(test_no_need_all_good);
    RUN_TEST(test_no_need_single_blockers);
    RUN_TEST(test_no_need_a2_actual_and_requested);
    RUN_TEST(test_no_need_exercise_transparency);
    return UNITY_END();
}
