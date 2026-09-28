#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <BoilerLoopLogic.h>
#include <BoilerRoomControlSettings.h>
#include <BoilerRoomTypes.h>

// Stage 07 phase 1: BoilerLoopLogic (overheat latch, P1, P2) with every
// fail-safe and hysteresis edge, plus the types/settings helpers.

static BoilerLoopLogic logic;
static BoilerRoomSettings settings;
static LoopDecision last;

void setUp() {
    logic.reset();
    settings = guardBoilerRoomSettings(defaultBoilerRoomSettings());
    last = LoopDecision{};
}
void tearDown() {}

// ---- helpers ---------------------------------------------------------------

static SensorInput ok(float t) { return SensorInput{SensorState::Ok, t, false}; }
static SensorInput fault() { return SensorInput{SensorState::Fault, NAN, false}; }
static SensorInput unassigned() { return SensorInput{SensorState::Unassigned, NAN, false}; }
static SensorInput unknown() { return SensorInput{SensorState::Unknown, NAN, false}; }

static void assertInvariants(const PumpDecision& d) {
    if (d.safety) {
        TEST_ASSERT_TRUE_MESSAGE(d.on, "safety => on");
    } else {
        TEST_ASSERT_EQUAL_MESSAGE(d.on, d.controlOn, "!safety => controlOn == on");
    }
}

static LoopDecision loop(SensorInput t1, SensorInput t2, SensorInput t3) {
    SensorInput t[BR_SENSOR_COUNT] = {t1, t2, t3, ok(55), ok(50), ok(45)};
    last = logic.update(t, settings);
    assertInvariants(last.p1);
    assertInvariants(last.p2);
    TEST_ASSERT_EQUAL(logic.overheat(), last.overheat);
    return last;
}

static void assertPump(const PumpDecision& d, bool on, bool safety, PumpReason reason, int line) {
    char msg[64];
    snprintf(msg, sizeof(msg), "line %d", line);
    TEST_ASSERT_EQUAL_MESSAGE(on, d.on, msg);
    TEST_ASSERT_EQUAL_MESSAGE(safety, d.safety, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(reason), static_cast<int>(d.reason), msg);
}
#define ASSERT_P1(on, safety, reason) assertPump(last.p1, on, safety, PumpReason::reason, __LINE__)
#define ASSERT_P2(on, safety, reason) assertPump(last.p2, on, safety, PumpReason::reason, __LINE__)

// ---- types -----------------------------------------------------------------

static void test_classify_sensor_all_states() {
    TEST_ASSERT_EQUAL(static_cast<int>(SensorHealth::Ok), static_cast<int>(classifySensor(SensorState::Ok)));
    TEST_ASSERT_EQUAL(static_cast<int>(SensorHealth::Failed), static_cast<int>(classifySensor(SensorState::Fault)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(SensorHealth::Failed), static_cast<int>(classifySensor(SensorState::Unassigned)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(SensorHealth::Pending), static_cast<int>(classifySensor(SensorState::Unknown)));
}

static void test_reason_and_mode_keys() {
    const int n = static_cast<int>(PumpReason::OverheatDump) + 1;
    for (int i = 0; i < n; ++i) {
        const char* key = pumpReasonKey(static_cast<PumpReason>(i));
        const char* sh = pumpReasonShort(static_cast<PumpReason>(i));
        TEST_ASSERT_NOT_NULL(key);
        TEST_ASSERT_NOT_NULL(sh);
        TEST_ASSERT_TRUE(strlen(key) > 0);
        TEST_ASSERT_TRUE_MESSAGE(strlen(sh) > 0 && strlen(sh) <= 9, sh);
        for (const char* c = sh; *c; ++c) {
            TEST_ASSERT_TRUE_MESSAGE(*c >= 0x20 && *c < 0x7F, sh);
        }
        for (int j = 0; j < i; ++j) {
            TEST_ASSERT_TRUE_MESSAGE(strcmp(key, pumpReasonKey(static_cast<PumpReason>(j))) != 0, key);
        }
    }
    TEST_ASSERT_EQUAL_STRING("charge_t1", pumpReasonKey(PumpReason::ChargeT1Only));
    TEST_ASSERT_EQUAL_STRING("T1T2 fail", pumpReasonShort(PumpReason::T1T2FaultForced));
    TEST_ASSERT_EQUAL_STRING("none", pumpReasonKey(static_cast<PumpReason>(200)));
    TEST_ASSERT_EQUAL_STRING("-", pumpReasonShort(static_cast<PumpReason>(200)));
    TEST_ASSERT_EQUAL_STRING("normal", p3ModeKey(P3Mode::Normal));
    TEST_ASSERT_EQUAL_STRING("off", p3ModeKey(P3Mode::Off));
    TEST_ASSERT_EQUAL_STRING("offer", p3ModeKey(P3Mode::Offer));
    TEST_ASSERT_EQUAL_STRING("NORMAL", p3ModeShort(P3Mode::Normal));
    TEST_ASSERT_EQUAL_STRING("OFF", p3ModeShort(P3Mode::Off));
    TEST_ASSERT_EQUAL_STRING("OFFER", p3ModeShort(P3Mode::Offer));
}

// ---- settings --------------------------------------------------------------

static void test_defaults_match_c2() {
    BoilerRoomSettings d = defaultBoilerRoomSettings();
    TEST_ASSERT_EQUAL_FLOAT(5.0f, d.p1DeltaOn);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, d.p1DeltaOff);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, d.p1T1Min);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, d.p1Hyst);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, d.ohOn);
    TEST_ASSERT_EQUAL_FLOAT(87.0f, d.ohClear);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, d.p2T2Off);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, d.p2Hyst);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, d.p2T1Burn);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, d.p3T3Offer);
    TEST_ASSERT_EQUAL_UINT32(10, d.p3OfferWindowMin);
    TEST_ASSERT_EQUAL_UINT32(60, d.p3OfferWaitMin);
    TEST_ASSERT_TRUE(d.afEnable);
    TEST_ASSERT_EQUAL_UINT32(30, d.afIntervalMin);
    TEST_ASSERT_EQUAL_UINT32(60, d.afDurationS);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, d.accVolumeL);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, d.accTBase);
}

static void test_guard_ordering() {
    BoilerRoomSettings s = defaultBoilerRoomSettings();
    s.p1DeltaOn = 5.0f;
    s.p1DeltaOff = 6.0f;
    s.ohOn = 90.0f;
    s.ohClear = 95.0f;
    BoilerRoomSettings g = guardBoilerRoomSettings(s);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, g.p1DeltaOff);
    TEST_ASSERT_EQUAL_FLOAT(89.0f, g.ohClear);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, g.p1DeltaOn);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, g.ohOn);

    BoilerRoomSettings v = defaultBoilerRoomSettings();  // valid pair: unchanged
    BoilerRoomSettings gv = guardBoilerRoomSettings(v);
    TEST_ASSERT_EQUAL_FLOAT(v.p1DeltaOff, gv.p1DeltaOff);
    TEST_ASSERT_EQUAL_FLOAT(v.ohClear, gv.ohClear);
    TEST_ASSERT_EQUAL_FLOAT(v.p1DeltaOn, gv.p1DeltaOn);
    TEST_ASSERT_EQUAL_FLOAT(v.ohOn, gv.ohOn);
}

// ---- P1 --------------------------------------------------------------------

static void test_p1_differential_edges() {
    loop(ok(65.0f), ok(50), ok(60));   // T1 == T3 + dOn: not >
    ASSERT_P1(false, false, Charge);
    loop(ok(65.1f), ok(50), ok(60));
    ASSERT_P1(true, false, Charge);
    loop(ok(62.0f), ok(50), ok(60));   // T1 == T3 + dOff: not <
    ASSERT_P1(true, false, Charge);
    loop(ok(61.9f), ok(50), ok(60));
    ASSERT_P1(false, false, Charge);
    loop(ok(64.0f), ok(50), ok(60));   // inside the deadband from OFF: stays OFF
    ASSERT_P1(false, false, Charge);
}

static void test_p1_t1_min_gate() {
    loop(ok(60.0f), ok(50), ok(40));   // T1 == t1Min: not >
    ASSERT_P1(false, false, Charge);
    loop(ok(60.1f), ok(50), ok(40));
    ASSERT_P1(true, false, Charge);
    loop(ok(58.0f), ok(50), ok(40));   // T1 == t1Min - hyst: not <
    ASSERT_P1(true, false, Charge);
    loop(ok(57.9f), ok(50), ok(40));
    ASSERT_P1(false, false, Charge);
}

static void runP1T1OnlyEdges(SensorInput t3Failed) {
    logic.reset();
    loop(ok(60.0f), ok(50), t3Failed);
    ASSERT_P1(false, false, ChargeT1Only);
    loop(ok(60.1f), ok(50), t3Failed);
    ASSERT_P1(true, false, ChargeT1Only);
    loop(ok(58.0f), ok(50), t3Failed);
    ASSERT_P1(true, false, ChargeT1Only);
    loop(ok(57.9f), ok(50), t3Failed);
    ASSERT_P1(false, false, ChargeT1Only);
}

static void test_p1_t3_failed_t1_only() {
    runP1T1OnlyEdges(fault());
    runP1T1OnlyEdges(unassigned());
}

static void test_p1_continuity_across_t3_variants() {
    loop(ok(70.0f), ok(50), ok(60));   // 70 > 65 -> charging (differential)
    ASSERT_P1(true, false, Charge);
    loop(ok(70.0f), ok(50), fault());  // variant switch keeps the state
    ASSERT_P1(true, false, ChargeT1Only);
    loop(ok(70.0f), ok(50), unknown());  // Pending T3: hold OFF, rule resets
    ASSERT_P1(false, false, SensorWait);
    loop(ok(64.0f), ok(50), ok(60));   // back Ok, T1 in the deadband (62..65): stays OFF
    ASSERT_P1(false, false, Charge);
}

static void test_p1_t1_failed_forced() {
    SensorInput failed[] = {fault(), unassigned()};
    SensorInput t3s[] = {ok(60), ok(95), fault(), unassigned(), unknown()};
    for (const SensorInput& f : failed) {
        for (const SensorInput& t3 : t3s) {
            logic.reset();
            loop(f, ok(50), t3);
            ASSERT_P1(true, true, T1FaultForced);
            TEST_ASSERT_FALSE(last.p1.controlOn);
            TEST_ASSERT_FALSE(last.overheat);
        }
    }
}

static void test_p1_t1_pending_waits() {
    loop(ok(70.0f), ok(50), ok(60));
    ASSERT_P1(true, false, Charge);
    loop(unknown(), ok(50), ok(60));
    ASSERT_P1(false, false, SensorWait);
    TEST_ASSERT_FALSE(last.p1.controlOn);
    loop(ok(64.0f), ok(50), ok(60));   // rule was reset: deadband stays OFF
    ASSERT_P1(false, false, Charge);
}

// ---- overheat --------------------------------------------------------------

static void test_overheat_edges() {
    loop(ok(90.0f), ok(50), ok(60));   // == ohOn: not >
    TEST_ASSERT_FALSE(last.overheat);
    ASSERT_P1(true, false, Charge);
    loop(ok(90.1f), ok(50), ok(60));
    TEST_ASSERT_TRUE(last.overheat);
    ASSERT_P1(true, true, Overheat);
    TEST_ASSERT_TRUE(last.p1.controlOn);  // rule state underneath (90.1 > 65)
    loop(ok(87.0f), ok(50), ok(60));   // == ohClear: not <
    TEST_ASSERT_TRUE(last.overheat);
    ASSERT_P1(true, true, Overheat);
    loop(ok(86.9f), ok(50), ok(60));   // clears -> rule decision, still > 65
    TEST_ASSERT_FALSE(last.overheat);
    ASSERT_P1(true, false, Charge);
}

static void test_overheat_controlon_follows_rule_state() {
    // Rule OFF when entering overheat (T3 so hot the differential is not met).
    loop(ok(90.1f), ok(50), ok(88));
    ASSERT_P1(true, true, Overheat);
    TEST_ASSERT_FALSE(last.p1.controlOn);
}

static void test_overheat_forces_with_t3_failed_or_pending() {
    SensorInput t3s[] = {fault(), unassigned(), unknown()};
    for (const SensorInput& t3 : t3s) {
        logic.reset();
        loop(ok(95.0f), ok(50), t3);
        TEST_ASSERT_TRUE(last.overheat);
        ASSERT_P1(true, true, Overheat);
    }
}

static void test_overheat_held_while_t1_pending() {
    loop(ok(95.0f), ok(50), ok(60));
    TEST_ASSERT_TRUE(last.overheat);
    loop(unknown(), ok(50), ok(60));
    TEST_ASSERT_TRUE(last.overheat);
    ASSERT_P1(true, true, Overheat);
    loop(ok(88.0f), ok(50), ok(60));   // Ok again, still >= ohClear: held
    TEST_ASSERT_TRUE(last.overheat);
    loop(ok(80.0f), ok(50), ok(60));
    TEST_ASSERT_FALSE(last.overheat);
}

static void test_overheat_clears_on_t1_failed_same_tick_forced() {
    SensorInput failed[] = {fault(), unassigned()};
    for (const SensorInput& f : failed) {
        logic.reset();
        loop(ok(95.0f), ok(50), ok(60));
        TEST_ASSERT_TRUE(last.overheat);
        loop(f, ok(50), ok(60));
        TEST_ASSERT_FALSE(last.overheat);
        ASSERT_P1(true, true, T1FaultForced);
        TEST_ASSERT_FALSE(last.p1.controlOn);
    }
}

static void test_overheat_never_created_by_pending_at_boot() {
    loop(unknown(), ok(50), ok(60));
    TEST_ASSERT_FALSE(last.overheat);
    ASSERT_P1(false, false, SensorWait);
}

static void test_overheat_guarded_clear() {
    BoilerRoomSettings raw = defaultBoilerRoomSettings();
    raw.ohOn = 90.0f;
    raw.ohClear = 95.0f;
    settings = guardBoilerRoomSettings(raw);   // ohClear -> 89
    loop(ok(90.1f), ok(50), ok(60));
    TEST_ASSERT_TRUE(last.overheat);
    loop(ok(89.0f), ok(50), ok(60));
    TEST_ASSERT_TRUE(last.overheat);
    loop(ok(88.9f), ok(50), ok(60));
    TEST_ASSERT_FALSE(last.overheat);
}

// ---- P2 --------------------------------------------------------------------

static void test_p2_edges() {
    loop(ok(75), ok(57.0f), ok(60));   // T2 == t2Off - hyst: not <
    ASSERT_P2(false, false, Return);
    loop(ok(75), ok(56.9f), ok(60));
    ASSERT_P2(true, false, Return);
    loop(ok(75), ok(59.9f), ok(60));
    ASSERT_P2(true, false, Return);
    loop(ok(75), ok(60.0f), ok(60));   // T2 >= t2Off
    ASSERT_P2(false, false, Return);
    loop(ok(75), ok(58.0f), ok(60));   // deadband from OFF: stays OFF
    ASSERT_P2(false, false, Return);
}

static void test_p2_burn_gate() {
    loop(ok(40.0f), ok(50), ok(60));   // T1 == t1Burn: not burning
    ASSERT_P2(false, false, Return);
    loop(ok(40.1f), ok(50), ok(60));
    ASSERT_P2(true, false, Return);
    loop(ok(37.0f), ok(50), ok(60));   // == t1Burn - hyst: still burning
    ASSERT_P2(true, false, Return);
    loop(ok(36.9f), ok(50), ok(60));   // burning ends -> OFF
    ASSERT_P2(false, false, Return);
    loop(ok(39.0f), ok(50), ok(60));   // burn latch deadband from OFF: stays OFF
    ASSERT_P2(false, false, Return);
}

static void runP2T2OnlyEdges(SensorInput t1Failed) {
    logic.reset();
    loop(t1Failed, ok(57.0f), ok(60));
    ASSERT_P2(false, false, ReturnT2Only);
    loop(t1Failed, ok(56.9f), ok(60));
    ASSERT_P2(true, false, ReturnT2Only);
    loop(t1Failed, ok(59.9f), ok(60));
    ASSERT_P2(true, false, ReturnT2Only);
    loop(t1Failed, ok(60.0f), ok(60));
    ASSERT_P2(false, false, ReturnT2Only);
}

static void test_p2_t1_failed_t2_only() {
    runP2T2OnlyEdges(fault());       // T1 temperature is NaN: ignored
    runP2T2OnlyEdges(unassigned());
    // Even a stale "cold" T1 value is ignored when T1 has failed.
    logic.reset();
    SensorInput staleCold{SensorState::Fault, 10.0f, false};
    loop(staleCold, ok(50), ok(60));
    ASSERT_P2(true, false, ReturnT2Only);
}

static void runP2BurnGateOnly(SensorInput t2Failed) {
    logic.reset();
    loop(ok(40.0f), t2Failed, ok(60));
    ASSERT_P2(false, false, ReturnBurnGate);
    loop(ok(40.1f), t2Failed, ok(60));
    ASSERT_P2(true, false, ReturnBurnGate);
    loop(ok(37.0f), t2Failed, ok(60));
    ASSERT_P2(true, false, ReturnBurnGate);
    loop(ok(36.9f), t2Failed, ok(60));
    ASSERT_P2(false, false, ReturnBurnGate);
}

static void test_p2_t2_failed_burn_gate() {
    runP2BurnGateOnly(fault());
    runP2BurnGateOnly(unassigned());
}

static void test_p2_t1_t2_failed_forced() {
    SensorInput failed[] = {fault(), unassigned()};
    for (const SensorInput& f1 : failed) {
        for (const SensorInput& f2 : failed) {
            logic.reset();
            loop(f1, f2, ok(60));
            ASSERT_P2(true, true, T1T2FaultForced);
            TEST_ASSERT_FALSE(last.p2.controlOn);
            ASSERT_P1(true, true, T1FaultForced);
        }
    }
}

static void test_p2_pending_waits() {
    const SensorInput f = fault();
    const SensorInput u = unassigned();
    const SensorInput w = unknown();
    const SensorInput o = ok(50);
    // Any Pending among T1/T2 without both Failed -> OFF SensorWait.
    SensorInput pairs[][2] = {{w, o}, {o, w}, {w, w}, {w, f}, {f, w}, {w, u}, {u, w}};
    for (auto& p : pairs) {
        logic.reset();
        SensorInput t1 = p[0];
        if (t1.state == SensorState::Ok) {
            t1.tempC = 75.0f;
        }
        loop(t1, p[1], ok(60));
        ASSERT_P2(false, false, SensorWait);
    }
}

static void test_p2_pending_resets_rule() {
    loop(ok(75), ok(50), ok(60));
    ASSERT_P2(true, false, Return);
    loop(ok(75), unknown(), ok(60));
    ASSERT_P2(false, false, SensorWait);
    loop(ok(75), ok(58.0f), ok(60));   // deadband after reset: stays OFF
    ASSERT_P2(false, false, Return);
}

static void test_settings_change_takes_effect_immediately() {
    loop(ok(75), ok(62.0f), ok(60));   // >= t2Off 60: OFF
    ASSERT_P2(false, false, Return);
    settings.p2T2Off = 65.0f;          // ON threshold now 62
    loop(ok(75), ok(61.9f), ok(60));
    ASSERT_P2(true, false, Return);
    loop(ok(75), ok(64.9f), ok(60));
    ASSERT_P2(true, false, Return);
    loop(ok(75), ok(65.0f), ok(60));
    ASSERT_P2(false, false, Return);
}

// ---- final-check Should 1: P2 continuity across T1 Ok <-> Failed -----------

static void test_overheat_tick_leaves_p2_unchanged() {
    loop(ok(90.0f), ok(50), ok(60));   // burning, T2 < t2Off - hyst -> P2 ON
    ASSERT_P2(true, false, Return);
    loop(ok(90.1f), ok(50), ok(60));   // overheat latches: P2 untouched
    TEST_ASSERT_TRUE(last.overheat);
    ASSERT_P1(true, true, Overheat);
    ASSERT_P2(true, false, Return);
    loop(ok(86.9f), ok(50), ok(60));   // overheat clears: P2 untouched
    TEST_ASSERT_FALSE(last.overheat);
    ASSERT_P2(true, false, Return);
}

static void test_p2_continuity_t1_failed_to_ok_hot() {
    loop(fault(), ok(56.9f), ok(60));  // T2-only mode: ON
    ASSERT_P2(true, false, ReturnT2Only);
    loop(fault(), ok(58.0f), ok(60));  // deadband: stays ON
    ASSERT_P2(true, false, ReturnT2Only);
    loop(ok(75), ok(58.0f), ok(60));   // T1 recovers hot: normal rule keeps it ON
    ASSERT_P2(true, false, Return);
    loop(ok(75), ok(60.0f), ok(60));   // T2 >= t2Off
    ASSERT_P2(false, false, Return);
}

static void runP2RecoverCold(float t1C) {
    logic.reset();
    loop(fault(), ok(50), ok(60));
    ASSERT_P2(true, false, ReturnT2Only);
    loop(ok(t1C), ok(50), ok(60));     // T1 recovers below t1Burn: not burning -> OFF this tick
    ASSERT_P2(false, false, Return);
}

static void test_p2_continuity_t1_failed_to_ok_cold_off_same_tick() {
    runP2RecoverCold(30.0f);
    runP2RecoverCold(38.0f);           // inside the burn deadband: burning was reset on failure
    runP2RecoverCold(40.0f);           // == t1Burn: not >
}

static void test_p2_continuity_t1_ok_to_failed() {
    loop(ok(75), ok(56.9f), ok(60));
    ASSERT_P2(true, false, Return);
    loop(ok(75), ok(58.0f), ok(60));   // deadband: ON
    ASSERT_P2(true, false, Return);
    loop(fault(), ok(58.0f), ok(60));  // T1 fails: T2-only keeps it ON
    ASSERT_P2(true, false, ReturnT2Only);
    loop(unassigned(), ok(58.0f), ok(60));
    ASSERT_P2(true, false, ReturnT2Only);
    loop(fault(), ok(60.0f), ok(60));
    ASSERT_P2(false, false, ReturnT2Only);
    loop(fault(), ok(58.0f), ok(60));  // deadband from OFF: stays OFF
    ASSERT_P2(false, false, ReturnT2Only);
    loop(ok(75), ok(58.0f), ok(60));   // T1 recovers: still OFF (deadband)
    ASSERT_P2(false, false, Return);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_classify_sensor_all_states);
    RUN_TEST(test_reason_and_mode_keys);
    RUN_TEST(test_defaults_match_c2);
    RUN_TEST(test_guard_ordering);
    RUN_TEST(test_p1_differential_edges);
    RUN_TEST(test_p1_t1_min_gate);
    RUN_TEST(test_p1_t3_failed_t1_only);
    RUN_TEST(test_p1_continuity_across_t3_variants);
    RUN_TEST(test_p1_t1_failed_forced);
    RUN_TEST(test_p1_t1_pending_waits);
    RUN_TEST(test_overheat_edges);
    RUN_TEST(test_overheat_controlon_follows_rule_state);
    RUN_TEST(test_overheat_forces_with_t3_failed_or_pending);
    RUN_TEST(test_overheat_held_while_t1_pending);
    RUN_TEST(test_overheat_clears_on_t1_failed_same_tick_forced);
    RUN_TEST(test_overheat_never_created_by_pending_at_boot);
    RUN_TEST(test_overheat_guarded_clear);
    RUN_TEST(test_p2_edges);
    RUN_TEST(test_p2_burn_gate);
    RUN_TEST(test_p2_t1_failed_t2_only);
    RUN_TEST(test_p2_t2_failed_burn_gate);
    RUN_TEST(test_p2_t1_t2_failed_forced);
    RUN_TEST(test_p2_pending_waits);
    RUN_TEST(test_p2_pending_resets_rule);
    RUN_TEST(test_settings_change_takes_effect_immediately);
    RUN_TEST(test_overheat_tick_leaves_p2_unchanged);
    RUN_TEST(test_p2_continuity_t1_failed_to_ok_hot);
    RUN_TEST(test_p2_continuity_t1_failed_to_ok_cold_off_same_tick);
    RUN_TEST(test_p2_continuity_t1_ok_to_failed);
    return UNITY_END();
}
