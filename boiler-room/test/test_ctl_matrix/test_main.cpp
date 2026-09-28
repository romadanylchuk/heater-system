#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <BoilerRoomAlarms.h>
#include <BoilerRoomController.h>
#include <BoilerRoomControlSettings.h>
#include <BoilerRoomTypes.h>
#include "SensorMatrixTable.h"

// Stage 07 phase 3: BoilerRoomAlarms (mask + table), BoilerRoomController
// composition, and the 64-combination x 3-scenario sensor-state matrix checked
// against the LITERAL table in SensorMatrixTable.h.

static BoilerRoomController ctl;

void setUp() { ctl.reset(); }
void tearDown() {}

// ---- helpers ---------------------------------------------------------------

static SensorInput ok(float t) { return SensorInput{SensorState::Ok, t, false}; }
static SensorInput fault(bool missing = false) { return SensorInput{SensorState::Fault, NAN, missing}; }
static SensorInput unassigned() { return SensorInput{SensorState::Unassigned, NAN, false}; }
static SensorInput unknown() { return SensorInput{SensorState::Unknown, NAN, false}; }

static BoilerRoomInputs baseInputs() {
    BoilerRoomInputs in{};
    in.sensor[BR_SENSOR_T1] = ok(75.0f);
    in.sensor[BR_SENSOR_T2] = ok(50.0f);
    in.sensor[BR_SENSOR_T3] = ok(60.0f);
    in.sensor[BR_SENSOR_T4] = ok(55.0f);
    in.sensor[BR_SENSOR_T5] = ok(50.0f);
    in.sensor[BR_SENSOR_T6] = ok(45.0f);
    in.noNeedFlag = true;
    in.linkUp = true;
    in.p3RelayOn = false;
    in.p3LastRunMs = 0;
    return in;
}

static void assertInvariants(const BoilerRoomOutputs& o, const char* msg) {
    for (uint8_t p = 0; p < BR_PUMP_COUNT; ++p) {
        if (o.pump[p].safety) {
            TEST_ASSERT_TRUE_MESSAGE(o.pump[p].on, msg);
        } else {
            TEST_ASSERT_EQUAL_MESSAGE(o.pump[p].on, o.pump[p].controlOn, msg);
        }
    }
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, o.alarmMask & ~BR_ALARM_OWNED_MASK, msg);
}

static void assertPump(const PumpDecision& d, bool on, bool safety, PumpReason reason, const char* msg) {
    TEST_ASSERT_EQUAL_MESSAGE(on, d.on, msg);
    TEST_ASSERT_EQUAL_MESSAGE(safety, d.safety, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(reason), static_cast<int>(d.reason), msg);
}
#define ASSERT_PUMP(d, on, safety, reason, msg) assertPump(d, on, safety, PumpReason::reason, msg)

static bool samePump(const PumpDecision& a, const PumpDecision& b) {
    return a.on == b.on && a.safety == b.safety && a.reason == b.reason && a.controlOn == b.controlOn;
}

// ---- the sensor-state matrix -------------------------------------------------

// Builds the input for one class letter: 'O' -> Ok(temp), 'W' -> Unknown,
// 'F' -> variant 0 Fault(missing=false), 1 Unassigned, 2 Fault(missing=true)
// (the usual field state of a dead sensor, review-3 suggestion: same pump
// outcome, alarmed by stage-03 bit 24+i instead of controller bit 8+i).
constexpr int F_VARIANTS = 3;
static SensorInput classInput(char cls, float tempC, int variant) {
    if (cls == 'O') {
        return ok(tempC);
    }
    if (cls == 'W') {
        return unknown();
    }
    return variant == 1 ? unassigned() : fault(variant == 2);
}

static const char* stateName(SensorState s) {
    switch (s) {
        case SensorState::Ok: return "Ok";
        case SensorState::Fault: return "Fault";
        case SensorState::Unassigned: return "Unassigned";
        default: return "Unknown";
    }
}

static void test_sensor_matrix() {
    static const char* const SCN[MX_SCENARIO_COUNT] = {"HOT", "COLD", "OH"};
    int combos = 0;
    int allCombos = 0;
    for (int r = 0; r < SENSOR_MATRIX_ROWS; ++r) {
        const MxRow& row = SENSOR_MATRIX[r];
        const char cls[3] = {row.t1, row.t2, row.t3};
        int fPos[3];
        int fCount = 0;
        for (int i = 0; i < 3; ++i) {
            if (cls[i] == 'F') {
                fPos[fCount++] = i;
            }
        }
        int variants = 1;
        for (int k = 0; k < fCount; ++k) {
            variants *= F_VARIANTS;
        }
        combos += 1 << fCount;
        allCombos += variants;
        for (int sc = 0; sc < MX_SCENARIO_COUNT; ++sc) {
            const MxScenario& exp = row.sc[sc];
            BoilerRoomOutputs first{};
            for (int v = 0; v < variants; ++v) {
                int variantAt[3] = {0, 0, 0};
                int rest = v;
                for (int k = 0; k < fCount; ++k) {
                    variantAt[fPos[k]] = rest % F_VARIANTS;
                    rest /= F_VARIANTS;
                }
                BoilerRoomInputs in = baseInputs();
                in.sensor[BR_SENSOR_T1] = classInput(cls[0], MX_T1_C[sc], variantAt[0]);
                in.sensor[BR_SENSOR_T2] = classInput(cls[1], MX_T2_C, variantAt[1]);
                in.sensor[BR_SENSOR_T3] = classInput(cls[2], MX_T3_C, variantAt[2]);
                in.sensor[BR_SENSOR_T4] = ok(MX_T4_C);
                in.sensor[BR_SENSOR_T5] = ok(MX_T5_C);
                in.sensor[BR_SENSOR_T6] = ok(MX_T6_C);

                char msg[192];
                snprintf(msg, sizeof(msg), "row #%d %c%c%c %s variant T1=%s%s T2=%s%s T3=%s%s", r + 1, cls[0],
                    cls[1], cls[2], SCN[sc], stateName(in.sensor[0].state), in.sensor[0].missing ? "(missing)" : "",
                    stateName(in.sensor[1].state), in.sensor[1].missing ? "(missing)" : "",
                    stateName(in.sensor[2].state), in.sensor[2].missing ? "(missing)" : "");

                ctl.reset();
                const BoilerRoomOutputs o =
                    ctl.update(in, defaultBoilerRoomSettings(), MX_NOW_MS);
                assertInvariants(o, msg);
                assertPump(o.pump[BR_PUMP_P1], exp.p1.on, exp.p1.safety, exp.p1.reason, msg);
                assertPump(o.pump[BR_PUMP_P2], exp.p2.on, exp.p2.safety, exp.p2.reason, msg);
                assertPump(o.pump[BR_PUMP_P3], exp.p3.on, exp.p3.safety, exp.p3.reason, msg);
                TEST_ASSERT_EQUAL_HEX32_MESSAGE(exp.bits04, o.alarmMask & 0x1Fu, msg);

                // Sensor bits: 8+i exactly for Fault(missing=false) / Unassigned; a
                // missing Fault is left to stage-03 bit 24+i (never set by the controller).
                uint32_t sensorBits = 0;
                for (int i = 0; i < 3; ++i) {
                    if (cls[i] == 'F' && variantAt[i] != 2) {
                        sensorBits |= 1u << (BR_ALARM_SENSOR_BASE + i);
                    }
                }
                TEST_ASSERT_EQUAL_HEX32_MESSAGE(sensorBits, o.alarmMask & 0x3F00u, msg);
                // Alarm scope: nothing outside {0..4, 8..10}; bit 0 only in OH with T1 Ok.
                TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, o.alarmMask & ~0x071Fu, msg);
                TEST_ASSERT_EQUAL_MESSAGE(sc == MX_OH && cls[0] == 'O', (o.alarmMask & 1u) != 0, msg);

                // Every F variant == the Fault variant (full pump decisions incl. controlOn,
                // bits 0..4 and the P3 mode; bits 8..13 differ only by the missing suppression).
                if (v == 0) {
                    first = o;
                } else {
                    for (uint8_t p = 0; p < BR_PUMP_COUNT; ++p) {
                        TEST_ASSERT_TRUE_MESSAGE(samePump(first.pump[p], o.pump[p]), msg);
                    }
                    TEST_ASSERT_EQUAL_HEX32_MESSAGE(first.alarmMask & ~0x3F00u, o.alarmMask & ~0x3F00u, msg);
                    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(first.p3Mode), static_cast<int>(o.p3Mode), msg);
                }
            }
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(64, combos, "all 64 T1/T2/T3 state combinations covered");
    TEST_ASSERT_EQUAL_INT_MESSAGE(125, allCombos, "125 with the Fault(missing=true) variant");
}

// ---- alarm unit tests ----------------------------------------------------------

static SensorInput allOk[BR_SENSOR_COUNT];
static void resetAllOk() {
    const BoilerRoomInputs in = baseInputs();
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        allOk[i] = in.sensor[i];
    }
}

static void test_alarm_all_ok_is_zero() {
    resetAllOk();
    TEST_ASSERT_EQUAL_HEX32(0, computeAlarmMask(allOk, false));
    TEST_ASSERT_EQUAL_HEX32(1u << BR_ALARM_OVERHEAT, computeAlarmMask(allOk, true));
}

static void test_alarm_missing_fault_sets_no_sensor_bit() {
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        resetAllOk();
        allOk[i] = fault(true);
        const uint32_t m = computeAlarmMask(allOk, false);
        TEST_ASSERT_EQUAL_HEX32(0, m & (1u << (BR_ALARM_SENSOR_BASE + i)));
        TEST_ASSERT_EQUAL_HEX32(0, m & 0xFFFFFF00u);
    }
    // The pump fail-safe bit is still raised for a missing T1 (a different condition, D5).
    resetAllOk();
    allOk[BR_SENSOR_T1] = fault(true);
    TEST_ASSERT_EQUAL_HEX32(1u << BR_ALARM_T1_FAILSAFE, computeAlarmMask(allOk, false));
}

static void test_alarm_fault_not_missing_sets_sensor_bit() {
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        resetAllOk();
        allOk[i] = fault(false);
        const uint32_t m = computeAlarmMask(allOk, false);
        TEST_ASSERT_EQUAL_HEX32(1u << (BR_ALARM_SENSOR_BASE + i), m & 0x3F00u);
    }
}

static void test_alarm_unassigned_t4_t6_sets_bits_11_13() {
    resetAllOk();
    allOk[BR_SENSOR_T4] = unassigned();
    allOk[BR_SENSOR_T5] = unassigned();
    allOk[BR_SENSOR_T6] = unassigned();
    TEST_ASSERT_EQUAL_HEX32((1u << 11) | (1u << 12) | (1u << 13), computeAlarmMask(allOk, false));
}

static void test_alarm_unknown_sets_nothing() {
    resetAllOk();
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        allOk[i] = unknown();
    }
    TEST_ASSERT_EQUAL_HEX32(0, computeAlarmMask(allOk, false));
}

static void test_alarm_bit3_only_t2_failed_t1_not_failed() {
    const SensorInput t1s[] = {ok(75.0f), unknown(), fault(false), unassigned()};
    const SensorInput t2s[] = {ok(50.0f), unknown(), fault(false), fault(true), unassigned()};
    for (const SensorInput& t1 : t1s) {
        for (const SensorInput& t2 : t2s) {
            resetAllOk();
            allOk[BR_SENSOR_T1] = t1;
            allOk[BR_SENSOR_T2] = t2;
            const bool t1Failed = t1.state == SensorState::Fault || t1.state == SensorState::Unassigned;
            const bool t2Failed = t2.state == SensorState::Fault || t2.state == SensorState::Unassigned;
            const uint32_t m = computeAlarmMask(allOk, false);
            TEST_ASSERT_EQUAL(t2Failed && !t1Failed, (m & (1u << BR_ALARM_T2_FAILSAFE)) != 0);
            TEST_ASSERT_EQUAL(t1Failed, (m & (1u << BR_ALARM_T1_FAILSAFE)) != 0);
            TEST_ASSERT_EQUAL(t1Failed && t2Failed, (m & (1u << BR_ALARM_T1T2_FAILSAFE)) != 0);
        }
    }
}

static void test_alarm_table() {
    for (size_t b = 0; b < BOILER_ROOM_ALARM_COUNT; ++b) {
        const AlarmDescriptor& a = BOILER_ROOM_ALARMS[b];
        char msg[32];
        snprintf(msg, sizeof(msg), "bit %u", static_cast<unsigned>(b));
        if (b >= 5 && b <= 7) {
            TEST_ASSERT_NULL_MESSAGE(a.key, msg);
            TEST_ASSERT_NULL_MESSAGE(a.labelEn, msg);
            TEST_ASSERT_NULL_MESSAGE(a.labelUa, msg);
            continue;
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(a.key, msg);
        TEST_ASSERT_NOT_NULL_MESSAGE(a.labelEn, msg);
        TEST_ASSERT_NOT_NULL_MESSAGE(a.labelUa, msg);
        TEST_ASSERT_TRUE_MESSAGE(strlen(a.labelEn) <= 21, msg);
        TEST_ASSERT_TRUE_MESSAGE(strlen(a.labelUa) > 0, msg);
        for (const char* p = a.labelEn; *p; ++p) {
            TEST_ASSERT_TRUE_MESSAGE(static_cast<unsigned char>(*p) >= 0x20 && static_cast<unsigned char>(*p) < 0x7F, msg);
        }
        for (size_t o = 0; o < b; ++o) {
            if (BOILER_ROOM_ALARMS[o].key != nullptr) {
                TEST_ASSERT_TRUE_MESSAGE(strcmp(BOILER_ROOM_ALARMS[o].key, a.key) != 0, msg);
            }
        }
    }
    TEST_ASSERT_EQUAL_STRING("overheat", BOILER_ROOM_ALARMS[BR_ALARM_OVERHEAT].key);
    TEST_ASSERT_EQUAL_STRING("t3_failsafe", BOILER_ROOM_ALARMS[BR_ALARM_T3_FAILSAFE].key);
    TEST_ASSERT_EQUAL_STRING("t1_fault", BOILER_ROOM_ALARMS[8].key);
    TEST_ASSERT_EQUAL_STRING("t6_fault", BOILER_ROOM_ALARMS[13].key);
    TEST_ASSERT_EQUAL_STRING("T6 fault/unassigned", BOILER_ROOM_ALARMS[13].labelEn);
    TEST_ASSERT_EQUAL_HEX32(0x00FFFFFFu, BR_ALARM_OWNED_MASK);
}

// ---- composition tests -------------------------------------------------------

// Raw dOFF >= dON (and ohClear >= ohOn) are guarded through the controller (D8).
static void test_guard_applied_through_controller() {
    BoilerRoomSettings raw = defaultBoilerRoomSettings();
    raw.p1DeltaOn = 5.0f;
    raw.p1DeltaOff = 10.0f;   // guarded to 4.5
    raw.ohOn = 90.0f;
    raw.ohClear = 95.0f;      // guarded to 89
    BoilerRoomInputs in = baseInputs();
    in.sensor[BR_SENSOR_T3] = ok(60.0f);
    in.sensor[BR_SENSOR_T1] = ok(70.0f);
    uint64_t now = 1000;
    BoilerRoomOutputs o = ctl.update(in, raw, now);
    ASSERT_PUMP(o.pump[BR_PUMP_P1], true, false, Charge, "dT 10 -> ON");
    in.sensor[BR_SENSOR_T1] = ok(64.8f);   // dT 4.8: raw dOFF 10 would switch OFF
    o = ctl.update(in, raw, now += 1000);
    ASSERT_PUMP(o.pump[BR_PUMP_P1], true, false, Charge, "dT 4.8 stays ON (guarded dOFF 4.5)");
    in.sensor[BR_SENSOR_T1] = ok(64.4f);
    o = ctl.update(in, raw, now += 1000);
    ASSERT_PUMP(o.pump[BR_PUMP_P1], false, false, Charge, "dT 4.4 -> OFF");

    in.sensor[BR_SENSOR_T1] = ok(95.0f);
    o = ctl.update(in, raw, now += 1000);
    TEST_ASSERT_TRUE(o.overheat);
    in.sensor[BR_SENSOR_T1] = ok(92.0f);   // < raw clear 95 but not < guarded 89
    o = ctl.update(in, raw, now += 1000);
    TEST_ASSERT_TRUE(o.overheat);
    in.sensor[BR_SENSOR_T1] = ok(88.5f);
    o = ctl.update(in, raw, now += 1000);
    TEST_ASSERT_FALSE(o.overheat);
}

static void test_request_flag_clear_propagates() {
    BoilerRoomInputs in = baseInputs();   // flag set, T3 60 == offer threshold, T1 75 (no overheat)
    const BoilerRoomSettings s = defaultBoilerRoomSettings();
    BoilerRoomOutputs o = ctl.update(in, s, 1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Off), static_cast<int>(o.p3Mode));
    TEST_ASSERT_FALSE(o.requestFlagClear);
    o = ctl.update(in, s, 2000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Offer), static_cast<int>(o.p3Mode));
    TEST_ASSERT_TRUE(o.requestFlagClear);
    ASSERT_PUMP(o.pump[BR_PUMP_P3], true, false, SupplyOffer, "offer");
    TEST_ASSERT_EQUAL_UINT32(600, o.offerWindowLeftS);
    in.p3RelayOn = true;
    in.p3LastRunMs = 2000;
    o = ctl.update(in, s, 3000);
    TEST_ASSERT_FALSE(o.requestFlagClear);
}

static void test_overheat_reaches_p3_dump_flag_not_cleared() {
    BoilerRoomInputs in = baseInputs();
    in.sensor[BR_SENSOR_T1] = ok(95.0f);
    const BoilerRoomSettings s = defaultBoilerRoomSettings();
    BoilerRoomOutputs o{};
    for (uint64_t t = 1000; t <= 20 * 60 * 1000ull; t += 1000) {
        o = ctl.update(in, s, t);
        TEST_ASSERT_FALSE(o.requestFlagClear);
        in.p3RelayOn = o.pump[BR_PUMP_P3].on;
        in.p3LastRunMs = t;
    }
    TEST_ASSERT_TRUE(o.overheat);
    TEST_ASSERT_TRUE(o.dumpActive);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Off), static_cast<int>(o.p3Mode));
    ASSERT_PUMP(o.pump[BR_PUMP_P1], true, true, Overheat, "P1");
    ASSERT_PUMP(o.pump[BR_PUMP_P3], true, true, OverheatDump, "P3");
    TEST_ASSERT_FALSE(o.pump[BR_PUMP_P3].controlOn);
    TEST_ASSERT_EQUAL_HEX32(1u << BR_ALARM_OVERHEAT, o.alarmMask);
}

static void test_t6_usable_and_no_pump_effect() {
    const SensorInput t6s[] = {ok(45.0f), fault(false), unassigned(), unknown()};
    const BoilerRoomSettings s = defaultBoilerRoomSettings();
    ctl.reset();
    const BoilerRoomOutputs ref = ctl.update(baseInputs(), s, 1000);
    for (const SensorInput& t6 : t6s) {
        BoilerRoomInputs in = baseInputs();
        in.sensor[BR_SENSOR_T6] = t6;
        ctl.reset();
        const BoilerRoomOutputs o = ctl.update(in, s, 1000);
        TEST_ASSERT_EQUAL(t6.state == SensorState::Ok, o.t6Usable);
        for (uint8_t p = 0; p < BR_PUMP_COUNT; ++p) {
            TEST_ASSERT_TRUE(samePump(ref.pump[p], o.pump[p]));
        }
        const bool bit13 = t6.state == SensorState::Fault || t6.state == SensorState::Unassigned;
        TEST_ASSERT_EQUAL_HEX32(bit13 ? (1u << 13) : 0u, o.alarmMask);
    }
}

static void test_fresh_install_all_unassigned() {
    BoilerRoomInputs in = baseInputs();
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        in.sensor[i] = unassigned();
    }
    in.noNeedFlag = false;
    in.linkUp = false;
    const BoilerRoomOutputs o = ctl.update(in, defaultBoilerRoomSettings(), 1000);
    ASSERT_PUMP(o.pump[BR_PUMP_P1], true, true, T1FaultForced, "P1");
    ASSERT_PUMP(o.pump[BR_PUMP_P2], true, true, T1T2FaultForced, "P2");
    ASSERT_PUMP(o.pump[BR_PUMP_P3], true, false, SupplyNormal, "P3");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EnergyQuality::Unavailable), static_cast<int>(o.energy.quality));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o.energy.kWh);
    TEST_ASSERT_FALSE(o.t6Usable);
    TEST_ASSERT_TRUE(o.offerDisabled);
    TEST_ASSERT_FALSE(o.overheat);
    TEST_ASSERT_EQUAL_HEX32((1u << 1) | (1u << 2) | (1u << 4) | 0x3F00u, o.alarmMask);
}

static void test_energy_and_anti_freeze_propagate() {
    BoilerRoomInputs in = baseInputs();   // T3 60, T4 55, T5 50 -> avg 55, V 500, base 30
    in.noNeedFlag = false;
    const uint64_t now = 30ull * 60ull * 1000ull;   // idle for afInterval (lastRun 0, relay OFF)
    const BoilerRoomOutputs o = ctl.update(in, defaultBoilerRoomSettings(), now);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EnergyQuality::Exact), static_cast<int>(o.energy.quality));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.5375f, o.energy.kWh);
    TEST_ASSERT_TRUE(o.antiFreezeStarted);
    TEST_ASSERT_TRUE(o.antiFreezeRunning);
    ASSERT_PUMP(o.pump[BR_PUMP_P3], true, true, AntiFreeze, "AF");
    TEST_ASSERT_TRUE(o.pump[BR_PUMP_P3].controlOn);   // NORMAL underneath
    TEST_ASSERT_TRUE(o.t6Usable);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sensor_matrix);
    RUN_TEST(test_alarm_all_ok_is_zero);
    RUN_TEST(test_alarm_missing_fault_sets_no_sensor_bit);
    RUN_TEST(test_alarm_fault_not_missing_sets_sensor_bit);
    RUN_TEST(test_alarm_unassigned_t4_t6_sets_bits_11_13);
    RUN_TEST(test_alarm_unknown_sets_nothing);
    RUN_TEST(test_alarm_bit3_only_t2_failed_t1_not_failed);
    RUN_TEST(test_alarm_table);
    RUN_TEST(test_guard_applied_through_controller);
    RUN_TEST(test_request_flag_clear_propagates);
    RUN_TEST(test_overheat_reaches_p3_dump_flag_not_cleared);
    RUN_TEST(test_t6_usable_and_no_pump_effect);
    RUN_TEST(test_fresh_install_all_unassigned);
    RUN_TEST(test_energy_and_anti_freeze_propagate);
    return UNITY_END();
}
