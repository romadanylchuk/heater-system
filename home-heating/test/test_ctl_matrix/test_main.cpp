#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <HomeHeatingAlarms.h>
#include <HomeHeatingController.h>
#include <HomeHeatingControlSettings.h>
#include <HomeHeatingTypes.h>
#include "FailSafeMatrixTable.h"

// Stage 08 phase 5: HomeHeatingAlarms (mask + table), HomeHeatingController
// composition, and the 27-row fail-safe matrix (+ extra rows a-d) checked
// against the LITERAL table in FailSafeMatrixTable.h.

static HomeHeatingController ctl;
static HomeHeatingSettings g;

static const uint32_t RECAL_MS = 132000u;
static const uint64_t MATRIX_TICKS = 40;   // 1 s ticks: boot recal + more than one K1 period

void setUp() {
    ctl.reset(0);
    g = defaultHomeHeatingSettings();
}
void tearDown() {}

// ---- helpers ---------------------------------------------------------------

static SensorInput ok(float t) { return SensorInput{SensorState::Ok, t, false}; }
static SensorInput fault(bool missing = false) { return SensorInput{SensorState::Fault, NAN, missing}; }
static SensorInput unassigned() { return SensorInput{SensorState::Unassigned, NAN, false}; }
static SensorInput unknown() { return SensorInput{SensorState::Unknown, NAN, false}; }

// 'O' -> Ok(temp), 'W' -> Unknown, 'U' -> Unassigned,
// 'F' -> Fault(missing = false), or Unassigned when failAsUnassigned.
static SensorInput classInput(char cls, float tempC, bool failAsUnassigned) {
    switch (cls) {
        case 'O': return ok(tempC);
        case 'W': return unknown();
        case 'U': return unassigned();
        default: return failAsUnassigned ? unassigned() : fault(false);
    }
}

static HomeHeatingInputs baseInputs() {
    HomeHeatingInputs in{};
    in.sensor[HH_SENSOR_H1] = ok(FS_H1_C);
    in.sensor[HH_SENSOR_H2] = ok(FS_H2_C);
    in.sensor[HH_SENSOR_H3] = ok(FS_H3_C);
    in.sensor[HH_SENSOR_H4] = ok(FS_H4_C);
    in.k1Motion = K1Motion{0u, 0u};
    return in;
}

struct Expect {
    FailMode fail;
    bool p4On;
    P4Reason p4Reason;
    K1Mode k1Mode;
    uint32_t mask;
    bool checkK2, k2Bypass;
    K2Reason k2Reason;
    bool checkNoNeed, noNeed;
};

// Fresh controller, reset(0), ticked at 1 s with the scenario's sensors from the
// start. p4RelayActual = the expected P4 request (no P4 falling edge ever). The
// K1 command of a tick is fed back as fully completed motion on the next tick
// (so the boot recal CLOSE becomes K1Motion{0, 132000} on tick 1). Fail / P4 /
// mask (/ K2 / no-need) are asserted on every tick, the K1 mode from the tick
// the recal ends onwards.
static void runScenario(HomeHeatingInputs in, const Expect& e, const char* msg) {
    ctl.reset(0);
    in.p4RelayActual = e.p4On;
    K1Command prev{false, K1Direction::Close, 0u};
    bool recalDone = false;
    for (uint64_t t = 0; t <= MATRIX_TICKS; ++t) {
        in.k1Motion = K1Motion{0u, 0u};
        if (prev.issue) {
            if (prev.dir == K1Direction::Open) {
                in.k1Motion.openMs = prev.ms;
            } else {
                in.k1Motion.closeMs = prev.ms;
            }
        }
        const HomeHeatingOutputs o = ctl.update(in, g, t * 1000ULL);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.fail), static_cast<int>(o.fail), msg);
        TEST_ASSERT_EQUAL_MESSAGE(e.p4On, o.p4.on, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.p4Reason), static_cast<int>(o.p4.reason), msg);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(e.mask, o.alarmMask, msg);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, o.alarmMask & ~HH_ALARM_OWNED_MASK, msg);
        if (e.checkK2) {
            TEST_ASSERT_EQUAL_MESSAGE(e.k2Bypass, o.k2.bypass, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.k2Reason), static_cast<int>(o.k2.reason), msg);
        }
        if (e.checkNoNeed) {
            TEST_ASSERT_EQUAL_MESSAGE(e.noNeed, o.noNeed, msg);
        }
        if (t == 0) {
            TEST_ASSERT_TRUE_MESSAGE(o.k1.recalStarted, msg);
            TEST_ASSERT_TRUE_MESSAGE(o.k1.cmd.issue, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(K1Direction::Close), static_cast<int>(o.k1.cmd.dir), msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(RECAL_MS, o.k1.cmd.ms, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(K1Mode::Recalibrating), static_cast<int>(o.k1.mode), msg);
        }
        if (o.k1.recalEnded) {
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(t), msg);
            recalDone = true;
        }
        if (recalDone) {
            TEST_ASSERT_TRUE_MESSAGE(o.k1.known, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.k1Mode), static_cast<int>(o.k1.mode), msg);
        }
        prev = o.k1.cmd;
    }
    TEST_ASSERT_TRUE_MESSAGE(recalDone, msg);
}

// ---- the fail-safe matrix ------------------------------------------------------

static void runMatrixVariant(bool failAsUnassigned) {
    char msg[96];
    for (int r = 0; r < FAIL_SAFE_MATRIX_ROWS; ++r) {
        const FsRow& row = FAIL_SAFE_MATRIX[r];
        snprintf(msg, sizeof(msg), "row %d (%c%c%c)%s", r + 1, row.h1, row.h2, row.h3,
                 failAsUnassigned ? " F=Unassigned" : "");
        HomeHeatingInputs in = baseInputs();
        in.sensor[HH_SENSOR_H1] = classInput(row.h1, FS_H1_C, failAsUnassigned);
        in.sensor[HH_SENSOR_H2] = classInput(row.h2, FS_H2_C, failAsUnassigned);
        in.sensor[HH_SENSOR_H3] = classInput(row.h3, FS_H3_C, failAsUnassigned);
        const Expect e{row.fail, row.p4On, row.p4Reason, row.k1Mode, row.mask,
                       false, false, K2Reason::None, false, false};
        runScenario(in, e, msg);
    }
}

void test_fail_safe_matrix_fault() { runMatrixVariant(false); }

// Unassigned is Failed too (A4) and raises the same bit 8+i: identical outcome.
void test_fail_safe_matrix_unassigned() { runMatrixVariant(true); }

void test_fail_safe_extra_rows() {
    for (int r = 0; r < FAIL_SAFE_EXTRA_ROWS; ++r) {
        const FsExtra& x = FAIL_SAFE_EXTRA[r];
        HomeHeatingInputs in = baseInputs();
        in.sensor[HH_SENSOR_H1] = classInput(x.h1, FS_H1_C, false);
        in.sensor[HH_SENSOR_H2] = classInput(x.h2, FS_H2_C, false);
        in.sensor[HH_SENSOR_H3] = classInput(x.h3, FS_H3_C, false);
        in.sensor[HH_SENSOR_H4] = classInput(x.h4, FS_H4_C, false);
        g = defaultHomeHeatingSettings();
        g.heatingEnabled = x.heatingEnabled;
        const Expect e{x.fail, x.p4On, x.p4Reason, x.k1Mode, x.mask,
                       x.checkK2, x.k2Bypass, x.k2Reason, x.checkNoNeed, x.noNeed};
        runScenario(in, e, x.name);
    }
}

// ---- alarms ------------------------------------------------------------------

static bool isAscii(const char* s) {
    for (; *s; ++s) {
        if (static_cast<unsigned char>(*s) > 0x7F) return false;
    }
    return true;
}

void test_alarm_table_labels() {
    for (size_t b = 0; b < HOME_HEATING_ALARM_COUNT; ++b) {
        const AlarmDescriptor& a = HOME_HEATING_ALARMS[b];
        if (b >= 5 && b <= 7) {
            TEST_ASSERT_NULL(a.key);
            TEST_ASSERT_NULL(a.labelEn);
            TEST_ASSERT_NULL(a.labelUa);
            continue;
        }
        TEST_ASSERT_NOT_NULL(a.key);
        TEST_ASSERT_NOT_NULL(a.labelEn);
        TEST_ASSERT_NOT_NULL(a.labelUa);
        TEST_ASSERT_TRUE(isAscii(a.key));
        TEST_ASSERT_TRUE(isAscii(a.labelEn));
        TEST_ASSERT_TRUE(strlen(a.labelEn) <= 21);
        TEST_ASSERT_TRUE(strlen(a.labelUa) > 0);
    }
    TEST_ASSERT_EQUAL_STRING("h3_failsafe", HOME_HEATING_ALARMS[HH_ALARM_H3_FAILSAFE].key);
    TEST_ASSERT_EQUAL_STRING("h2_failsafe", HOME_HEATING_ALARMS[HH_ALARM_H2_FAILSAFE].key);
    TEST_ASSERT_EQUAL_STRING("h1_failsafe", HOME_HEATING_ALARMS[HH_ALARM_H1_FAILSAFE].key);
    TEST_ASSERT_EQUAL_STRING("multi_failsafe", HOME_HEATING_ALARMS[HH_ALARM_MULTI_FAILSAFE].key);
    TEST_ASSERT_EQUAL_STRING("h4_failsafe", HOME_HEATING_ALARMS[HH_ALARM_H4_FAILSAFE].key);
    TEST_ASSERT_EQUAL_STRING("h1_fault", HOME_HEATING_ALARMS[HH_ALARM_SENSOR_BASE + 0].key);
    TEST_ASSERT_EQUAL_STRING("h4_fault", HOME_HEATING_ALARMS[HH_ALARM_SENSOR_BASE + 3].key);
}

// Every combination of 5 input kinds on H1..H4 x every fail mode: the mask
// stays inside bits 0..23 (stage-03 bits 24..31 are never touched).
void test_alarm_mask_never_touches_bits_24_31() {
    const SensorInput kinds[5] = {ok(50.0f), fault(false), fault(true), unassigned(), unknown()};
    const FailMode modes[5] = {FailMode::None, FailMode::H1, FailMode::H2, FailMode::H3, FailMode::Multi};
    for (int a = 0; a < 5; ++a)
        for (int b = 0; b < 5; ++b)
            for (int c = 0; c < 5; ++c)
                for (int d = 0; d < 5; ++d)
                    for (int m = 0; m < 5; ++m) {
                        const SensorInput h[HH_SENSOR_COUNT] = {kinds[a], kinds[b], kinds[c], kinds[d]};
                        TEST_ASSERT_EQUAL_HEX32(0u, computeAlarmMask(h, modes[m]) & 0xFF000000u);
                    }
}

// A missing Fault sensor is stage-03 bit 24+i: no bit 8+i, but the fail-mode bit stays.
void test_missing_fault_sensor_keeps_fail_mode_bit_only() {
    HomeHeatingInputs in = baseInputs();
    in.sensor[HH_SENSOR_H3] = fault(true);
    const HomeHeatingOutputs o = ctl.update(in, g, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(FailMode::H3), static_cast<int>(o.fail));
    TEST_ASSERT_EQUAL_HEX32(0x0001u, o.alarmMask);

    in = baseInputs();
    in.sensor[HH_SENSOR_H4] = fault(true);
    TEST_ASSERT_EQUAL_HEX32(0x0010u, ctl.update(in, g, 1000).alarmMask);

    in = baseInputs();
    in.sensor[HH_SENSOR_H1] = fault(true);
    in.sensor[HH_SENSOR_H2] = fault(false);
    TEST_ASSERT_EQUAL_HEX32(0x0208u, ctl.update(in, g, 2000).alarmMask);
}

// ---- composition -------------------------------------------------------------

// K2 is independent of heatingEnabled.
void test_k2_independent_of_heating() {
    g.heatingEnabled = false;
    HomeHeatingInputs in = baseInputs();
    in.sensor[HH_SENSOR_H3] = ok(80.0f);
    in.sensor[HH_SENSOR_H4] = ok(40.0f);
    const HomeHeatingOutputs o = ctl.update(in, g, 0);
    TEST_ASSERT_FALSE(o.k2.bypass);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K2Reason::Charging), static_cast<int>(o.k2.reason));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P4Reason::HeatingOff), static_cast<int>(o.p4.reason));
}

// Heating off + K2 bypass + P4 off -> no-need true once the actual relays match (A2).
void test_no_need_with_heating_off_after_relays_match() {
    g.heatingEnabled = false;
    HomeHeatingInputs in = baseInputs();
    in.sensor[HH_SENSOR_H3] = ok(50.0f);   // < H3min 65 -> bypass H3Low
    in.sensor[HH_SENSOR_H4] = ok(40.0f);
    in.k2BypassActual = false;               // lock still holds TANK
    in.p4RelayActual = false;
    HomeHeatingOutputs o = ctl.update(in, g, 0);
    TEST_ASSERT_TRUE(o.k2.bypass);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K2Reason::H3Low), static_cast<int>(o.k2.reason));
    TEST_ASSERT_FALSE(o.p4.on);
    TEST_ASSERT_TRUE(o.p4.offDelayElapsed);
    TEST_ASSERT_FALSE(o.noNeed);
    in.k2BypassActual = true;
    o = ctl.update(in, g, 1000);
    TEST_ASSERT_TRUE(o.noNeed);
    in.p4RelayActual = true;                // P4 physically still ON -> not yet
    TEST_ASSERT_FALSE(ctl.update(in, g, 2000).noNeed);
    in.p4RelayActual = false;
    in.sensor[HH_SENSOR_H1] = unknown();    // any Pending -> OFF (A4)
    TEST_ASSERT_FALSE(ctl.update(in, g, 3000).noNeed);
}

// No-need is held OFF while heating is enabled and H3 is Failed (D14).
void test_no_need_off_while_h3_failed_heating() {
    HomeHeatingInputs in = baseInputs();
    in.sensor[HH_SENSOR_H1] = fault(false);
    in.sensor[HH_SENSOR_H3] = fault(false);   // Multi, H3 Failed -> P4 OFF, K2 bypass H3Fault
    in.k2BypassActual = true;
    const HomeHeatingOutputs o = ctl.update(in, g, 400000);
    TEST_ASSERT_FALSE(o.p4.on);
    TEST_ASSERT_TRUE(o.k2.bypass);
    TEST_ASSERT_FALSE(o.noNeed);
    g.heatingEnabled = false;
    TEST_ASSERT_TRUE(ctl.update(in, g, 401000).noNeed);
}

// K1 control needs P4 requested AND the relay actually ON (D10), this tick's P4.
void test_k1_closed_until_p4_relay_actual() {
    HomeHeatingInputs in = baseInputs();
    in.p4RelayActual = false;
    HomeHeatingOutputs o = ctl.update(in, g, 0);
    TEST_ASSERT_TRUE(o.p4.on);
    o = ctl.update(in, g, 1000);            // (no motion fed: recal still running)
    in.k1Motion = K1Motion{0u, RECAL_MS};
    o = ctl.update(in, g, 2000);
    in.k1Motion = K1Motion{0u, 0u};
    TEST_ASSERT_TRUE(o.k1.recalEnded);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Mode::Closed), static_cast<int>(o.k1.mode));
    TEST_ASSERT_FALSE(o.k1.cmd.issue);      // already at 0 %
    in.p4RelayActual = true;
    o = ctl.update(in, g, 3000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Mode::Normal), static_cast<int>(o.k1.mode));
    TEST_ASSERT_TRUE(o.k1.cmd.issue);       // FF 25 % -> OPEN 30 000
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Open), static_cast<int>(o.k1.cmd.dir));
    TEST_ASSERT_UINT32_WITHIN(1u, 30000u, o.k1.cmd.ms);
}

// Settings are guarded before use: k2DeltaHyst 5 with k2Delta 3 -> 2.5.
void test_settings_guarded_first() {
    g.k2Delta = 3.0f;
    g.k2DeltaHyst = 5.0f;
    g.k2H3Min = 40.0f;
    HomeHeatingInputs in = baseInputs();
    in.sensor[HH_SENSOR_H4] = ok(50.0f);
    in.sensor[HH_SENSOR_H3] = ok(54.0f);
    TEST_ASSERT_FALSE(ctl.update(in, g, 0).k2.bypass);
    in.sensor[HH_SENSOR_H3] = ok(50.4f);    // <= 50 + 3 - 2.5 (unguarded would need <= 48)
    const HomeHeatingOutputs o = ctl.update(in, g, 1000);
    TEST_ASSERT_TRUE(o.k2.bypass);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K2Reason::DeltaLow), static_cast<int>(o.k2.reason));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fail_safe_matrix_fault);
    RUN_TEST(test_fail_safe_matrix_unassigned);
    RUN_TEST(test_fail_safe_extra_rows);
    RUN_TEST(test_alarm_table_labels);
    RUN_TEST(test_alarm_mask_never_touches_bits_24_31);
    RUN_TEST(test_missing_fault_sensor_keeps_fail_mode_bit_only);
    RUN_TEST(test_k2_independent_of_heating);
    RUN_TEST(test_no_need_with_heating_off_after_relays_match);
    RUN_TEST(test_no_need_off_while_h3_failed_heating);
    RUN_TEST(test_k1_closed_until_p4_relay_actual);
    RUN_TEST(test_settings_guarded_first);
    return UNITY_END();
}
