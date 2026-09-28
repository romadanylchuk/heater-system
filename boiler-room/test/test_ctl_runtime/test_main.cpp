#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <optional>
#include <BoilerRoomEvents.h>
#include <BoilerRoomRuntime.h>
#include <BoilerRoomStatus.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <EventLog.h>
#include <RelayBank.h>
#include "../../../common/test/fakes/FakeClock.h"
#include "../../../common/test/fakes/MemoryKvStore.h"
#include "../../../common/test/fakes/RecordingEventSink.h"
#include "../../../common/test/fakes/TestSchema.h"
#include "../../src/BoilerRoomHardware.h"
#include "../../src/BoilerRoomSchema.h"
#include "../test_ctl_matrix/SensorMatrixTable.h"

// Stage 07 phase 4: BoilerRoomRuntime against the REAL BOILER_ROOM_SCHEMA
// (ConfigEngine), a REAL RelayBank (lock + safety bypass + inhibit), the real
// HA gate (haGatedFlag) and the event sink. Step = runtime.tick(now) then
// relays.update(now) -- the firmware order (ctl.tick in the slow block, the
// relay update in the next hw fast tick).

void setUp() {}
void tearDown() {}

constexpr uint32_t LOCK_MS = 60000;
constexpr uint32_t MISSING_BIT_BASE = 24;   // stage-03 "sensor missing" bits 24+i (HwRuntime owns them)

struct Fixture {
    MemoryKvStore kv;
    RecordingEventSink sink;
    ConfigEngine config{kv, sink};
    RelayBank relays;
    CommonState state{};
    BoilerRoomStatus status{};
    BoilerRoomRuntime rt{state, status, config, sink, relays};

    explicit Fixture(const ConfigSchema& schema = BOILER_ROOM_SCHEMA, uint64_t runtimeBootMs = 0) {
        config.begin(schema, 0);
        relays.configure(BOILER_ROOM_RELAYS, 3, 0);
        relays.setLockMs(LOCK_MS);
        state.sensors.count = 6;
        setOk(BR_SENSOR_T1, 75.0f);
        setOk(BR_SENSOR_T2, 50.0f);
        setOk(BR_SENSOR_T3, 60.0f);
        setOk(BR_SENSOR_T4, 55.0f);
        setOk(BR_SENSOR_T5, 50.0f);
        setOk(BR_SENSOR_T6, 45.0f);
        runtimeStatus = rt.begin(runtimeBootMs);
        sink.clear();
    }

    BoilerRoomRuntimeStatus runtimeStatus;

    void setSensor(uint8_t i, SensorState s, float t = NAN, bool missing = false) {
        LogicalSensorStatus& x = state.sensors.sensor[i];
        x.state = s;
        x.tempC = (s == SensorState::Ok) ? t : NAN;
        x.assigned = (s != SensorState::Unassigned);
        x.missing = missing;
    }
    void setOk(uint8_t i, float t) { setSensor(i, SensorState::Ok, t); }

    size_t idx(const char* key) const { return static_cast<size_t>(config.indexOf(key)); }
    void setFlag(bool on, uint64_t now = 0) { config.setNumber(idx(BR_KEY_HOME_NO_NEED), on ? 1.0f : 0.0f, EventReason::Web, now); }
    bool flag() const { return config.getBool(idx(BR_KEY_HOME_NO_NEED)); }
    void setSetting(const char* key, float v, uint64_t now = 0) { config.setNumber(idx(key), v, EventReason::Web, now); }
    void link(bool up) { state.network.mqttConnected = up; }

    void step(uint64_t now) {
        rt.tick(now);
        relays.update(now, sink);
    }
    // Steps every 1 s over (from, to] (both in ms, multiples of 1000).
    void run(uint64_t from, uint64_t to) {
        for (uint64_t t = from + 1000; t <= to; t += 1000) {
            step(t);
        }
    }
};

static size_t countEvents(const RecordingEventSink& s, uint16_t type, uint16_t source) {
    size_t n = 0;
    for (size_t i = 0; i < s.count(); ++i) {
        if (s.at(i).type == type && s.at(i).source == source) {
            ++n;
        }
    }
    return n;
}
static size_t countType(const RecordingEventSink& s, uint16_t type) {
    size_t n = 0;
    for (size_t i = 0; i < s.count(); ++i) {
        if (s.at(i).type == type) {
            ++n;
        }
    }
    return n;
}

// ---- begin -----------------------------------------------------------------------

static void test_begin_ok_on_real_schema() {
    Fixture f;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BoilerRoomRuntimeStatus::Ok), static_cast<int>(f.runtimeStatus));
    TEST_ASSERT_TRUE(f.rt.ready());
    f.step(1000);
    TEST_ASSERT_TRUE(f.status.ready);
}

static void test_begin_missing_on_test_schema_forces_p1_safety() {
    Fixture f(TEST_SCHEMA_A_V1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BoilerRoomRuntimeStatus::SettingMissing), static_cast<int>(f.runtimeStatus));
    TEST_ASSERT_FALSE(f.rt.ready());
    f.step(1000);   // inside the 60 s boot lock
    TEST_ASSERT_FALSE(f.status.ready);
    TEST_ASSERT_TRUE(f.relays.safetyActive(BR_PUMP_P1));
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));
    TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P2));
    TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P3));
    TEST_ASSERT_FALSE(f.relays.requested(BR_PUMP_P2));
    TEST_ASSERT_FALSE(f.relays.requested(BR_PUMP_P3));
    TEST_ASSERT_EQUAL_HEX32(0, f.state.alarms.activeMask);
}

// ---- lock / bypass matrix (relay level, inside the 60 s boot lock) -----------------

// F variants: 0 = Fault(missing=false), 1 = Unassigned, 2 = Fault(missing=true)
// (the usual field state of a dead DS18B20; review-3 suggestion). All three must
// give identical pump outcomes; the missing variant is alarmed by stage-03 bit
// 24+i (pre-set here as HwRuntime would) instead of controller bit 8+i.
constexpr int F_VARIANTS = 3;
static const char* const F_VARIANT_NAME[F_VARIANTS] = {"Fault", "Unassigned", "FaultMissing"};

static void applyClass(Fixture& f, uint8_t i, char cls, float tempC, int variant) {
    if (cls == 'O') {
        f.setOk(i, tempC);
    } else if (cls == 'W') {
        f.setSensor(i, SensorState::Unknown);
    } else if (variant == 1) {
        f.setSensor(i, SensorState::Unassigned);
    } else {
        f.setSensor(i, SensorState::Fault, NAN, variant == 2);
    }
}

static void assertCellAtBoot(Fixture& f, uint8_t ch, const MxPump& e, const char* msg) {
    const BoilerRoomPumpStatus& s = f.status.pump[ch];
    TEST_ASSERT_EQUAL_MESSAGE(e.on, s.on, msg);
    TEST_ASSERT_EQUAL_MESSAGE(e.safety, s.safety, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.reason), static_cast<int>(s.reason), msg);
    TEST_ASSERT_EQUAL_MESSAGE(e.safety, f.relays.safetyActive(ch), msg);
    // Actual ON inside the boot lock exactly for the ON S cells.
    TEST_ASSERT_EQUAL_MESSAGE(e.on && e.safety, f.relays.actual(ch), msg);
    TEST_ASSERT_EQUAL_MESSAGE(e.on, f.relays.requested(ch), msg);
    if (e.on && !e.safety) {   // ON C: requested, held by the lock
        TEST_ASSERT_TRUE_MESSAGE(f.relays.lockDelayed(ch), msg);
    } else {
        TEST_ASSERT_FALSE_MESSAGE(f.relays.lockDelayed(ch), msg);
    }
}

static void test_lock_bypass_matrix() {
    static const char* const SCN[MX_SCENARIO_COUNT] = {"HOT", "COLD", "OH"};
    int baseCombos = 0;
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
        baseCombos += 1 << fCount;
        allCombos += variants;
        for (int sc = 0; sc < MX_SCENARIO_COUNT; ++sc) {
            const MxScenario& exp = row.sc[sc];
            for (int v = 0; v < variants; ++v) {
                int variantAt[3] = {0, 0, 0};
                int rest = v;
                for (int k = 0; k < fCount; ++k) {
                    variantAt[fPos[k]] = rest % F_VARIANTS;
                    rest /= F_VARIANTS;
                }
                Fixture f;
                f.setFlag(true);   // persisted flag AND link up -> gated flag true, as in the matrix
                f.link(true);
                applyClass(f, BR_SENSOR_T1, cls[0], MX_T1_C[sc], variantAt[0]);
                applyClass(f, BR_SENSOR_T2, cls[1], MX_T2_C, variantAt[1]);
                applyClass(f, BR_SENSOR_T3, cls[2], MX_T3_C, variantAt[2]);
                uint32_t missingBits = 0;   // stage 03 raises 24+i for a missing sensor
                uint32_t sensorBits = 0;    // controller 8+i for Fault(!missing) / Unassigned
                for (int i = 0; i < 3; ++i) {
                    if (cls[i] != 'F') {
                        continue;
                    }
                    if (variantAt[i] == 2) {
                        missingBits |= 1u << (MISSING_BIT_BASE + i);
                    } else {
                        sensorBits |= 1u << (BR_ALARM_SENSOR_BASE + i);
                    }
                }
                f.state.alarms.activeMask = missingBits;

                char msg[160];
                snprintf(msg, sizeof(msg), "row #%d %c%c%c %s variants T1=%s T2=%s T3=%s", r + 1, cls[0], cls[1],
                    cls[2], SCN[sc], cls[0] == 'F' ? F_VARIANT_NAME[variantAt[0]] : "-",
                    cls[1] == 'F' ? F_VARIANT_NAME[variantAt[1]] : "-", cls[2] == 'F' ? F_VARIANT_NAME[variantAt[2]] : "-");

                f.step(MX_NOW_MS);   // t = 1 s, deep inside the 60 s boot lock
                assertCellAtBoot(f, BR_PUMP_P1, exp.p1, msg);
                assertCellAtBoot(f, BR_PUMP_P2, exp.p2, msg);
                assertCellAtBoot(f, BR_PUMP_P3, exp.p3, msg);
                TEST_ASSERT_EQUAL_HEX32_MESSAGE(exp.bits04 | sensorBits | missingBits, f.state.alarms.activeMask, msg);
                TEST_ASSERT_EQUAL_HEX32_MESSAGE(exp.bits04 | sensorBits, f.status.alarmMask, msg);
                TEST_ASSERT_TRUE_MESSAGE(f.flag(), msg);   // no clear on the NORMAL->OFF tick

                // t = 61 s: the lock has expired, every ON C cell is actually ON now;
                // S cells stay ON; P1/P2 OFF cells stay OFF. (P3 OFF cells may enter
                // OFFER on this tick -- T3 60 >= T3_offer 60 -- so they are not pinned.)
                f.step(61000);
                const MxPump* cells[BR_PUMP_COUNT] = {&exp.p1, &exp.p2, &exp.p3};
                for (uint8_t ch = 0; ch < BR_PUMP_COUNT; ++ch) {
                    if (cells[ch]->on) {
                        TEST_ASSERT_TRUE_MESSAGE(f.relays.actual(ch), msg);
                    } else if (ch != BR_PUMP_P3) {
                        TEST_ASSERT_FALSE_MESSAGE(f.relays.actual(ch), msg);
                    }
                }
            }
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(64, baseCombos, "64 Fault/Unassigned combinations");
    TEST_ASSERT_EQUAL_INT_MESSAGE(125, allCombos, "125 with the Fault(missing) variant");
}

// ---- real HA gate -------------------------------------------------------------------

static void test_real_ha_gate() {
    Fixture f;
    f.setOk(BR_SENSOR_T3, 55.0f);   // below T3_offer: no offer, only the gate is under test
    f.setFlag(true);
    f.link(false);
    f.step(1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Normal), static_cast<int>(f.status.p3Mode));
    TEST_ASSERT_TRUE(f.relays.requested(BR_PUMP_P3));
    TEST_ASSERT_TRUE(f.status.noNeedSaved);
    TEST_ASSERT_FALSE(f.status.noNeedEffective);
    TEST_ASSERT_FALSE(f.status.linkUp);
    f.run(1000, 30000);
    TEST_ASSERT_TRUE(f.flag());   // the gate never touches the persisted value

    f.link(true);
    f.step(31000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Off), static_cast<int>(f.status.p3Mode));
    TEST_ASSERT_FALSE(f.relays.requested(BR_PUMP_P3));
    TEST_ASSERT_TRUE(f.status.noNeedEffective);
    TEST_ASSERT_TRUE(f.flag());

    f.link(false);   // MQTT drops
    f.step(32000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Normal), static_cast<int>(f.status.p3Mode));
    TEST_ASSERT_TRUE(f.relays.requested(BR_PUMP_P3));
    TEST_ASSERT_TRUE(f.flag());
}

// ---- offer self-clear through the settings engine ------------------------------------

static void test_offer_self_clears_flag_via_config_engine() {
    Fixture f;
    const size_t noNeedIdx = f.idx(BR_KEY_HOME_NO_NEED);
    const uint16_t flagSource = static_cast<uint16_t>(EVENT_SOURCE_SETTING_BASE + noNeedIdx);
    f.setOk(BR_SENSOR_T3, 70.0f);
    f.setFlag(true);
    f.link(true);
    f.sink.clear();

    f.step(1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Off), static_cast<int>(f.status.p3Mode));
    TEST_ASSERT_TRUE(f.flag());
    f.step(2000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Offer), static_cast<int>(f.status.p3Mode));
    TEST_ASSERT_FALSE(f.flag());   // self-cleared through ConfigEngine
    size_t logicClears = 0;
    for (size_t i = 0; i < f.sink.count(); ++i) {
        const RecordingEventSink::Record& e = f.sink.at(i);
        if (e.type == toU16(EventType::ConfigChanged) && e.source == flagSource && e.reason == EventReason::Logic) {
            ++logicClears;
            TEST_ASSERT_EQUAL_FLOAT(0.0f, e.value);
        }
    }
    TEST_ASSERT_EQUAL_UINT(1, logicClears);

    // The user re-sets the flag during the 10 min window: it stays set, never cleared again.
    f.setFlag(true, 3000);
    f.run(2000, 30 * 60000ull);
    TEST_ASSERT_TRUE(f.flag());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Off), static_cast<int>(f.status.p3Mode));   // window over, wait armed
    logicClears = 0;
    for (size_t i = 0; i < f.sink.count(); ++i) {
        const RecordingEventSink::Record& e = f.sink.at(i);
        if (e.type == toU16(EventType::ConfigChanged) && e.source == flagSource && e.reason == EventReason::Logic) {
            ++logicClears;
        }
    }
    TEST_ASSERT_EQUAL_UINT(1, logicClears);
}

// ---- overheat with the flag set --------------------------------------------------------

static void test_overheat_with_flag_bypasses_lock_and_keeps_flag() {
    Fixture f;
    f.setOk(BR_SENSOR_T1, 95.0f);
    f.setOk(BR_SENSOR_T3, 55.0f);   // no offer possible
    f.setFlag(true);
    f.link(true);
    f.sink.clear();

    f.step(1000);   // inside the boot lock
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));
    TEST_ASSERT_TRUE(f.status.overheat);
    TEST_ASSERT_TRUE(f.status.dumpActive);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::OverheatDump), static_cast<int>(f.status.pump[BR_PUMP_P3].reason));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, toU16(EventType::AlarmRaised), EVENT_SOURCE_ALARM_BASE + 0));
    for (size_t i = 0; i < f.sink.count(); ++i) {
        if (f.sink.at(i).type == toU16(EventType::AlarmRaised) && f.sink.at(i).source == EVENT_SOURCE_ALARM_BASE) {
            TEST_ASSERT_EQUAL_FLOAT(95.0f, f.sink.at(i).value);
            TEST_ASSERT_EQUAL_INT(static_cast<int>(EventReason::Logic), static_cast<int>(f.sink.at(i).reason));
        }
    }

    for (uint64_t t = 2000; t <= 30 * 60000ull; t += 1000) {   // 30 min overheat
        f.step(t);
        TEST_ASSERT_TRUE(f.flag());
        TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));
        TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Off), static_cast<int>(f.status.p3Mode));
    }
    TEST_ASSERT_TRUE(f.state.alarms.activeMask & 1u);

    f.setOk(BR_SENSOR_T1, 80.0f);   // < ohClear 87
    const uint64_t tClear = 30 * 60000ull + 1000;
    f.step(tClear);
    TEST_ASSERT_FALSE(f.status.overheat);
    TEST_ASSERT_FALSE(f.relays.requested(BR_PUMP_P3));
    TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P3));      // control OFF, min-ON lock long satisfied
    TEST_ASSERT_FALSE(f.relays.safetyActive(BR_PUMP_P3));
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));       // T1 80, T3 55: normal charge continues
    TEST_ASSERT_FALSE(f.relays.safetyActive(BR_PUMP_P1));
    TEST_ASSERT_EQUAL_HEX32(0, f.state.alarms.activeMask & 1u);
    TEST_ASSERT_TRUE(f.flag());
    f.run(tClear, tClear + 100000);
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, toU16(EventType::AlarmCleared), EVENT_SOURCE_ALARM_BASE + 0));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, toU16(EventType::AlarmRaised), EVENT_SOURCE_ALARM_BASE + 0));
}

// ---- anti-freeze --------------------------------------------------------------------------

static void setupModeOff(Fixture& f) {
    f.setOk(BR_SENSOR_T3, 50.0f);   // below T3_offer: mode stays OFF
    f.setFlag(true);
    f.link(true);
    f.sink.clear();
}

static void test_anti_freeze_runs_after_interval_default_settings() {
    Fixture f;
    setupModeOff(f);
    const uint64_t afAt = 30 * 60000ull;   // P3 never ran: last run = boot (0)
    for (uint64_t t = 1000; t < afAt; t += 1000) {
        f.step(t);
        TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P3));
    }
    TEST_ASSERT_EQUAL_UINT(0, countType(f.sink, toU16(EventType::AntiFreezeRun)));
    f.step(afAt);
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));
    TEST_ASSERT_TRUE(f.relays.safetyActive(BR_PUMP_P3));
    TEST_ASSERT_TRUE(f.status.antiFreezeRunning);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::AntiFreeze), static_cast<int>(f.status.pump[BR_PUMP_P3].reason));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, toU16(EventType::AntiFreezeRun), EVENT_SOURCE_RELAY_BASE + 2));
    for (size_t i = 0; i < f.sink.count(); ++i) {
        if (f.sink.at(i).type == toU16(EventType::AntiFreezeRun)) {
            TEST_ASSERT_EQUAL_FLOAT(60.0f, f.sink.at(i).value);
        }
    }
    for (uint64_t t = afAt + 1000; t < afAt + 60000; t += 1000) {   // runs 60 s
        f.step(t);
        TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Off), static_cast<int>(f.status.p3Mode));
        TEST_ASSERT_TRUE(f.flag());
    }
    f.step(afAt + 60000);   // run over; min-ON lock (60 s) satisfied -> OFF
    TEST_ASSERT_FALSE(f.status.antiFreezeRunning);
    TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P3));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Off), static_cast<int>(f.status.p3Mode));
    TEST_ASSERT_TRUE(f.flag());
    f.run(afAt + 60000, afAt + 120000);
    TEST_ASSERT_EQUAL_UINT(1, countType(f.sink, toU16(EventType::AntiFreezeRun)));
}

static void test_anti_freeze_bypasses_min_off_lock() {
    Fixture f;
    f.setOk(BR_SENSOR_T3, 50.0f);
    f.setSetting(BR_KEY_AF_INTERVAL, 5);
    f.setFlag(true);
    f.link(false);           // NORMAL: P3 runs on the control slot first
    f.run(0, 100000);
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));
    f.link(true);            // -> OFF
    const uint64_t t0 = 200000;
    f.run(100000, t0);
    TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P3));
    const uint64_t offAt = f.relays.lastChangeMs(BR_PUMP_P3);
    f.relays.setLockMs(600000);   // 10 min min-OFF lock
    for (uint64_t t = t0 + 1000; t < offAt + 5 * 60000ull; t += 1000) {
        f.step(t);
        TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P3));
    }
    const uint64_t afAt = offAt + 5 * 60000ull;
    TEST_ASSERT_TRUE(afAt - offAt < 600000);   // the min-OFF lock is still active
    f.step(afAt);
    TEST_ASSERT_TRUE(f.status.antiFreezeRunning);
    TEST_ASSERT_TRUE(f.relays.safetyActive(BR_PUMP_P3));
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));   // safety bypasses the lock
    TEST_ASSERT_EQUAL_UINT64(afAt, f.relays.lastChangeMs(BR_PUMP_P3));
}

// ---- shared P3 last-run timer (anti-seize exercise slot, reboot) ---------------------------

static void test_anti_freeze_timer_shared_with_exercise() {
    Fixture f;
    setupModeOff(f);
    const uint64_t exAt = 20 * 60000ull;
    f.run(0, exAt - 1000);
    f.relays.setExercise(BR_PUMP_P3, true);   // anti-seize runs P3 at 20 min
    f.step(exAt);
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));
    f.run(exAt, exAt + 30000);
    f.relays.setExercise(BR_PUMP_P3, std::nullopt);   // released after 30 s
    uint64_t t = exAt + 30000;
    while (f.relays.actual(BR_PUMP_P3)) {   // min-ON lock governs the OFF switch
        t += 1000;
        f.step(t);
        TEST_ASSERT_TRUE(t < exAt + 120000);
    }
    const uint64_t lastRun = f.relays.lastChangeMs(BR_PUMP_P3);
    TEST_ASSERT_EQUAL_UINT64(exAt + 60000, lastRun);
    const uint64_t afAt = lastRun + 30 * 60000ull;
    for (t += 1000; t < afAt; t += 1000) {   // not at 30 min after boot
        f.step(t);
        TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P3));
    }
    TEST_ASSERT_EQUAL_UINT(0, countType(f.sink, toU16(EventType::AntiFreezeRun)));
    f.step(afAt);
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));
    TEST_ASSERT_EQUAL_UINT(1, countType(f.sink, toU16(EventType::AntiFreezeRun)));
}

static void test_anti_freeze_not_before_interval_after_late_begin() {
    const uint64_t bootMs = 5000000;   // runtime begins late (relays configured at 0, never ON)
    Fixture f(BOILER_ROOM_SCHEMA, bootMs);
    setupModeOff(f);
    const uint64_t afAt = bootMs + 30 * 60000ull;
    for (uint64_t t = bootMs + 1000; t < afAt; t += 1000) {
        f.step(t);
        TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P3));
    }
    TEST_ASSERT_EQUAL_UINT(0, countType(f.sink, toU16(EventType::AntiFreezeRun)));
    f.step(afAt);
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));
    TEST_ASSERT_EQUAL_UINT(1, countType(f.sink, toU16(EventType::AntiFreezeRun)));
}

// ---- alarm bits and edges ------------------------------------------------------------------

static void test_alarm_bits_preserve_stage03_and_log_edges_once() {
    Fixture f;
    f.setSensor(BR_SENSOR_T1, SensorState::Fault);   // bits 1 and 8
    f.state.alarms.activeMask = 0xFF000000u | (1u << 20);   // stage-03 bits kept, stale controller bit dropped
    f.step(1000);
    const uint32_t ctl = (1u << BR_ALARM_T1_FAILSAFE) | (1u << (BR_ALARM_SENSOR_BASE + 0));
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u | ctl, f.state.alarms.activeMask);
    TEST_ASSERT_EQUAL_HEX32(ctl, f.status.alarmMask);
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, toU16(EventType::AlarmRaised), EVENT_SOURCE_ALARM_BASE + 1));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, toU16(EventType::AlarmRaised), EVENT_SOURCE_ALARM_BASE + 8));
    TEST_ASSERT_EQUAL_UINT(2, countType(f.sink, toU16(EventType::AlarmRaised)));
    for (size_t i = 0; i < f.sink.count(); ++i) {
        if (f.sink.at(i).type == toU16(EventType::AlarmRaised)) {
            TEST_ASSERT_EQUAL_FLOAT(0.0f, f.sink.at(i).value);
            TEST_ASSERT_EQUAL_INT(static_cast<int>(EventReason::Logic), static_cast<int>(f.sink.at(i).reason));
        }
    }
    f.run(1000, 70000);   // lock-delayed control requests settle
    const size_t before = f.sink.count();
    f.run(70000, 170000);   // 100 stable ticks log nothing
    TEST_ASSERT_EQUAL_UINT(before, f.sink.count());
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u | ctl, f.state.alarms.activeMask);

    f.setOk(BR_SENSOR_T1, 75.0f);   // recovery: both bits clear, once each
    f.run(170000, 180000);
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u, f.state.alarms.activeMask);
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, toU16(EventType::AlarmCleared), EVENT_SOURCE_ALARM_BASE + 1));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, toU16(EventType::AlarmCleared), EVENT_SOURCE_ALARM_BASE + 8));
}

// ---- P3 mode and pump-request events ------------------------------------------------------

static void test_p3_mode_events_distinct_sources() {
    Fixture f;
    f.setOk(BR_SENSOR_T3, 70.0f);
    f.setFlag(true);
    f.link(true);
    f.sink.clear();
    f.step(1000);   // NORMAL -> OFF
    f.step(2000);   // OFF -> OFFER
    TEST_ASSERT_EQUAL_UINT(2, countType(f.sink, BR_EVENT_P3_MODE));
    const uint16_t srcOff = EVENT_SOURCE_PROJECT_BASE + static_cast<uint16_t>(P3Mode::Off);
    const uint16_t srcOffer = EVENT_SOURCE_PROJECT_BASE + static_cast<uint16_t>(P3Mode::Offer);
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, BR_EVENT_P3_MODE, srcOff));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, BR_EVENT_P3_MODE, srcOffer));
    for (size_t i = 0; i < f.sink.count(); ++i) {
        const RecordingEventSink::Record& e = f.sink.at(i);
        if (e.type == BR_EVENT_P3_MODE && e.source == srcOff) {
            TEST_ASSERT_EQUAL_FLOAT(1.0f, e.value);   // new Off
            TEST_ASSERT_EQUAL_FLOAT(0.0f, e.aux);     // old Normal
        } else if (e.type == BR_EVENT_P3_MODE && e.source == srcOffer) {
            TEST_ASSERT_EQUAL_FLOAT(2.0f, e.value);
            TEST_ASSERT_EQUAL_FLOAT(1.0f, e.aux);
        }
    }
    f.run(2000, 300000);   // stable inside the 10 min window
    TEST_ASSERT_EQUAL_UINT(2, countType(f.sink, BR_EVENT_P3_MODE));
}

// With the real EventLog (60 s per type+source limiter) both mode events of
// NORMAL->OFF->OFFER, one second apart, are accepted because their sources differ.
static void test_p3_mode_events_survive_real_rate_limiter() {
    MemoryKvStore cfgKv, logKv;
    FakeClock clock;
    EventLog log(logKv, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgKv, log);
    config.begin(BOILER_ROOM_SCHEMA, 0);
    RelayBank relays;
    relays.configure(BOILER_ROOM_RELAYS, 3, 0);
    relays.setLockMs(LOCK_MS);
    CommonState state{};
    BoilerRoomStatus status{};
    state.sensors.count = 6;
    const float temps[BR_SENSOR_COUNT] = {75.0f, 50.0f, 70.0f, 55.0f, 50.0f, 45.0f};
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        state.sensors.sensor[i].state = SensorState::Ok;
        state.sensors.sensor[i].tempC = temps[i];
    }
    state.network.mqttConnected = true;
    config.setNumber(static_cast<size_t>(config.indexOf(BR_KEY_HOME_NO_NEED)), 1, EventReason::Web, 0);
    BoilerRoomRuntime rt(state, status, config, log, relays);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BoilerRoomRuntimeStatus::Ok), static_cast<int>(rt.begin(0)));
    for (uint64_t t = 1000; t <= 2000; t += 1000) {
        clock.mono = t;
        rt.tick(t);
        relays.update(t, log);
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Offer), static_cast<int>(status.p3Mode));
    size_t modeEntries = 0;
    for (size_t i = 0; i < log.count(); ++i) {
        const EventEntry* e = log.newest(i);
        if (e != nullptr && e->type == BR_EVENT_P3_MODE) {
            ++modeEntries;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2, modeEntries);
}

static void test_pump_request_events_only_on_on_or_safety_flip() {
    Fixture f;
    f.setOk(BR_SENSOR_T1, 75.0f);   // HOT: P1 charge ON (control)
    f.step(1000);
    const uint16_t srcP1 = EVENT_SOURCE_RELAY_BASE + BR_PUMP_P1;
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, BR_EVENT_PUMP_REQUEST, srcP1));
    f.run(1000, 70000);
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, BR_EVENT_PUMP_REQUEST, srcP1));

    f.setSensor(BR_SENSOR_T3, SensorState::Fault);   // charge -> chargeT1: reason-only change
    f.step(71000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::ChargeT1Only), static_cast<int>(f.status.pump[BR_PUMP_P1].reason));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, BR_EVENT_PUMP_REQUEST, srcP1));

    f.setOk(BR_SENSOR_T1, 95.0f);   // overheat: safety flips
    f.step(72000);
    TEST_ASSERT_EQUAL_UINT(2, countEvents(f.sink, BR_EVENT_PUMP_REQUEST, srcP1));
    const RecordingEventSink::Record* last = nullptr;
    for (size_t i = 0; i < f.sink.count(); ++i) {
        if (f.sink.at(i).type == BR_EVENT_PUMP_REQUEST && f.sink.at(i).source == srcP1) {
            last = &f.sink.at(i);
        }
    }
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(PumpReason::Overheat), last->value);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, last->aux);

    f.setOk(BR_SENSOR_T1, 80.0f);   // overheat clears, still charging: safety flips back
    f.step(73000);
    TEST_ASSERT_EQUAL_UINT(3, countEvents(f.sink, BR_EVENT_PUMP_REQUEST, srcP1));
    f.run(73000, 90000);
    TEST_ASSERT_EQUAL_UINT(3, countEvents(f.sink, BR_EVENT_PUMP_REQUEST, srcP1));
}

// ---- recovery / reassignment ---------------------------------------------------------------

static void test_t1_fault_recovery_lock_governs_switch() {
    Fixture f;
    f.setSensor(BR_SENSOR_T1, SensorState::Fault);
    f.step(1000);
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));   // T1FaultForced, safety, inside the boot lock
    TEST_ASSERT_TRUE(f.relays.safetyActive(BR_PUMP_P1));

    f.setOk(BR_SENSOR_T1, 30.0f);   // recovered, cold: the rule wants P1 OFF
    f.step(10000);
    TEST_ASSERT_FALSE(f.relays.safetyActive(BR_PUMP_P1));
    TEST_ASSERT_FALSE(f.relays.requested(BR_PUMP_P1));
    TEST_ASSERT_TRUE(f.relays.lockDelayed(BR_PUMP_P1));   // min-ON lock (ON since 1 s)
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));
    f.run(10000, 60000);
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));
    f.step(61000);
    TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P1));
}

static void test_t1_reassignment_unassigned_to_unknown() {
    Fixture f;
    f.setSensor(BR_SENSOR_T1, SensorState::Unassigned);
    f.step(1000);
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));
    TEST_ASSERT_TRUE(f.relays.safetyActive(BR_PUMP_P1));
    f.run(1000, 99000);
    f.setSensor(BR_SENSOR_T1, SensorState::Unknown);   // address assigned, first read pending
    f.step(100000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::SensorWait), static_cast<int>(f.status.pump[BR_PUMP_P1].reason));
    TEST_ASSERT_FALSE(f.relays.requested(BR_PUMP_P1));
    TEST_ASSERT_FALSE(f.relays.actual(BR_PUMP_P1));   // min-ON lock already satisfied
}

// ---- OTA inhibit: stage-04 semantics win over safety ----------------------------------------

static void test_ota_inhibit_keeps_all_relays_off_during_overheat() {
    Fixture f;
    f.setOk(BR_SENSOR_T1, 95.0f);
    f.step(1000);
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));
    f.relays.setInhibited(true, 1500, f.sink);
    for (uint64_t t = 2000; t <= 30000; t += 1000) {
        f.step(t);
        for (uint8_t ch = 0; ch < RELAY_CHANNEL_COUNT; ++ch) {
            TEST_ASSERT_FALSE(f.relays.actual(ch));
        }
        TEST_ASSERT_TRUE(f.status.overheat);                  // the controller still wants safety ON
        TEST_ASSERT_TRUE(f.relays.safetyActive(BR_PUMP_P1));  // request kept in the slot
    }
    f.relays.setInhibited(false, 30500, f.sink);
    f.step(31000);   // safety bypasses the lock again at once
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P3));
}

// ---- runtime setting change ------------------------------------------------------------------

static void test_setting_change_takes_effect_next_tick() {
    Fixture f;
    f.setOk(BR_SENSOR_T2, 60.0f);   // >= p2T2Off 60: P2 OFF
    f.step(1000);
    TEST_ASSERT_FALSE(f.status.pump[BR_PUMP_P2].on);
    f.setSetting(BR_KEY_P2_T2_OFF, 65, 1500);   // ON now needs T2 < 62
    f.step(2000);
    TEST_ASSERT_TRUE(f.status.pump[BR_PUMP_P2].on);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::Return), static_cast<int>(f.status.pump[BR_PUMP_P2].reason));
    TEST_ASSERT_TRUE(f.relays.requested(BR_PUMP_P2));
}

// ---- status slice ------------------------------------------------------------------------------

static void test_status_fields_scripted_scenario() {
    Fixture f;
    f.setOk(BR_SENSOR_T3, 70.0f);   // T3 70, T4 55, T5 50: avg 58.33 -> 500*1.163*28.33/1000 = 16.476 kWh
    f.setSensor(BR_SENSOR_T6, SensorState::Fault);
    f.setFlag(true);
    f.link(true);

    f.step(1000);   // NORMAL -> OFF
    TEST_ASSERT_TRUE(f.status.ready);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Off), static_cast<int>(f.status.p3Mode));
    TEST_ASSERT_TRUE(f.status.noNeedSaved);
    TEST_ASSERT_TRUE(f.status.noNeedEffective);
    TEST_ASSERT_TRUE(f.status.linkUp);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EnergyQuality::Exact), static_cast<int>(f.status.energyQuality));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 16.476f, f.status.energyKWh);
    TEST_ASSERT_FALSE(f.status.t6Usable);
    TEST_ASSERT_EQUAL_HEX32(1u << (BR_ALARM_SENSOR_BASE + BR_SENSOR_T6), f.status.alarmMask);
    TEST_ASSERT_EQUAL_UINT32(0, f.status.offerWindowLeftS);
    TEST_ASSERT_EQUAL_UINT32(0, f.status.offerWaitLeftS);
    TEST_ASSERT_EQUAL_UINT32(1, f.status.p3IdleS);   // last run = boot (0)
    TEST_ASSERT_EQUAL_UINT32(30 * 60 - 1, f.status.afInS);

    f.step(2000);   // OFF -> OFFER, flag self-cleared
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Offer), static_cast<int>(f.status.p3Mode));
    TEST_ASSERT_FALSE(f.status.noNeedSaved);      // persisted value after the clear
    TEST_ASSERT_TRUE(f.status.noNeedEffective);   // the gated value this tick's logic used
    TEST_ASSERT_EQUAL_UINT32(600, f.status.offerWindowLeftS);
    TEST_ASSERT_TRUE(f.status.pump[BR_PUMP_P3].on);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::SupplyOffer), static_cast<int>(f.status.pump[BR_PUMP_P3].reason));

    f.setFlag(true, 2500);   // the home says "no need" again during the window
    f.step(3000);
    TEST_ASSERT_TRUE(f.status.noNeedSaved);
    TEST_ASSERT_EQUAL_UINT32(599, f.status.offerWindowLeftS);
    f.run(3000, 602000);     // window over: OFFER -> OFF, wait armed (60 min)
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Off), static_cast<int>(f.status.p3Mode));
    TEST_ASSERT_EQUAL_UINT32(0, f.status.offerWindowLeftS);
    TEST_ASSERT_EQUAL_UINT32(3600, f.status.offerWaitLeftS);
    f.step(603000);
    TEST_ASSERT_EQUAL_UINT32(3599, f.status.offerWaitLeftS);
    TEST_ASSERT_TRUE(f.status.noNeedSaved);
}

// ---- final-check Should 2: Ok sensor with a non-finite reading = Fault ------------

static int reasonOf(const Fixture& f, uint8_t ch) { return static_cast<int>(f.status.pump[ch].reason); }
static bool alarmBit(const Fixture& f, uint8_t bit) { return (f.status.alarmMask & (1u << bit)) != 0; }

static void test_nan_t1_ok_is_t1_fault() {
    const float bad[] = {NAN, INFINITY, -INFINITY};
    for (float v : bad) {
        Fixture f;
        f.setOk(BR_SENSOR_T1, v);
        f.step(1000);   // inside the boot lock: the forced state must bypass it
        TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::T1FaultForced), reasonOf(f, BR_PUMP_P1));
        TEST_ASSERT_TRUE(f.status.pump[BR_PUMP_P1].safety);
        TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::ReturnT2Only), reasonOf(f, BR_PUMP_P2));
        TEST_ASSERT_TRUE(alarmBit(f, BR_ALARM_T1_FAILSAFE));
        TEST_ASSERT_TRUE(alarmBit(f, BR_ALARM_SENSOR_BASE + BR_SENSOR_T1));
        TEST_ASSERT_FALSE(f.status.overheat);
    }
}

static void test_nan_t1_ok_does_not_freeze_overheat_latch() {
    Fixture f;
    f.setOk(BR_SENSOR_T1, 95.0f);
    f.step(1000);
    TEST_ASSERT_TRUE(f.status.overheat);
    TEST_ASSERT_TRUE(f.status.dumpActive);

    f.setOk(BR_SENSOR_T1, NAN);   // reading goes non-finite while latched
    f.sink.clear();
    f.step(2000);
    TEST_ASSERT_FALSE(f.status.overheat);          // latch clears on Failed (D4), not frozen
    TEST_ASSERT_FALSE(f.status.dumpActive);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::T1FaultForced), reasonOf(f, BR_PUMP_P1));
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P1));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, toU16(EventType::AlarmCleared), EVENT_SOURCE_ALARM_BASE + BR_ALARM_OVERHEAT));
    for (size_t i = 0; i < f.sink.count(); ++i) {
        TEST_ASSERT_FALSE(isnan(f.sink.at(i).value));   // no NaN reaches the log
    }

    f.setOk(BR_SENSOR_T1, 95.0f);  // a finite hot reading latches again
    f.step(3000);
    TEST_ASSERT_TRUE(f.status.overheat);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::Overheat), reasonOf(f, BR_PUMP_P1));
}

static void test_nan_t2_ok_is_t2_fault() {
    Fixture f;
    f.setOk(BR_SENSOR_T2, NAN);
    f.step(1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::ReturnBurnGate), reasonOf(f, BR_PUMP_P2));
    TEST_ASSERT_TRUE(f.status.pump[BR_PUMP_P2].on);    // T1 75 > t1Burn: burning
    TEST_ASSERT_FALSE(f.status.pump[BR_PUMP_P2].safety);
    TEST_ASSERT_TRUE(alarmBit(f, BR_ALARM_T2_FAILSAFE));
    TEST_ASSERT_TRUE(alarmBit(f, BR_ALARM_SENSOR_BASE + BR_SENSOR_T2));

    f.setOk(BR_SENSOR_T1, NAN);    // T1 + T2 non-finite -> both forced
    f.step(2000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::T1T2FaultForced), reasonOf(f, BR_PUMP_P2));
    TEST_ASSERT_TRUE(f.status.pump[BR_PUMP_P2].safety);
    TEST_ASSERT_TRUE(f.relays.actual(BR_PUMP_P2));
}

static void test_nan_t3_ok_is_t3_fault() {
    Fixture f;
    f.setOk(BR_SENSOR_T3, NAN);
    f.link(true);
    f.step(1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PumpReason::ChargeT1Only), reasonOf(f, BR_PUMP_P1));
    TEST_ASSERT_TRUE(f.status.pump[BR_PUMP_P1].on);    // T1 75 >= p1T1Min
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P3Mode::Normal), static_cast<int>(f.status.p3Mode));
    TEST_ASSERT_TRUE(f.status.offerDisabled);
    TEST_ASSERT_TRUE(alarmBit(f, BR_ALARM_T3_FAILSAFE));
    TEST_ASSERT_TRUE(alarmBit(f, BR_ALARM_SENSOR_BASE + BR_SENSOR_T3));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EnergyQuality::Estimated), static_cast<int>(f.status.energyQuality));
    TEST_ASSERT_TRUE(isfinite(f.status.energyKWh));
}

static void test_nan_t4_t5_ok_energy_stays_finite() {
    Fixture f;
    f.setOk(BR_SENSOR_T4, NAN);
    f.step(1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EnergyQuality::Estimated), static_cast<int>(f.status.energyQuality));
    TEST_ASSERT_TRUE(isfinite(f.status.energyKWh));
    f.setOk(BR_SENSOR_T3, INFINITY);
    f.setOk(BR_SENSOR_T5, NAN);
    f.step(2000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EnergyQuality::Unavailable), static_cast<int>(f.status.energyQuality));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.status.energyKWh);
}

// ---- final-check Should 5: settings-missing DiagnosticWarning ----------------------

static void test_begin_missing_logs_diag_once_per_boot() {
    MemoryKvStore kv;
    RecordingEventSink sink;
    ConfigEngine config{kv, sink};
    RelayBank relays;
    CommonState state{};
    BoilerRoomStatus status{};
    BoilerRoomRuntime rt{state, status, config, sink, relays};
    config.begin(TEST_SCHEMA_A_V1, 0);
    relays.configure(BOILER_ROOM_RELAYS, 3, 0);
    sink.clear();

    const uint16_t src = static_cast<uint16_t>(EVENT_SOURCE_DIAG_BASE + BR_DIAG_CODE_SETTING_MISSING);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BoilerRoomRuntimeStatus::SettingMissing), static_cast<int>(rt.begin(0)));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(sink, toU16(EventType::DiagnosticWarning), src));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sink.at(0).value);   // BrRuntimeKey::P1DeltaOn failed first
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventReason::Logic), static_cast<int>(sink.at(0).reason));

    rt.begin(1000);   // a second begin in the same boot: no second event
    for (uint64_t t = 1000; t <= 10000; t += 1000) {
        rt.tick(t);
        relays.update(t, sink);
    }
    TEST_ASSERT_EQUAL_UINT(1, countEvents(sink, toU16(EventType::DiagnosticWarning), src));
}

static void test_begin_ok_logs_no_diag() {
    MemoryKvStore kv;
    RecordingEventSink sink;
    ConfigEngine config{kv, sink};
    RelayBank relays;
    CommonState state{};
    BoilerRoomStatus status{};
    BoilerRoomRuntime rt{state, status, config, sink, relays};
    config.begin(BOILER_ROOM_SCHEMA, 0);
    sink.clear();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BoilerRoomRuntimeStatus::Ok), static_cast<int>(rt.begin(0)));
    TEST_ASSERT_EQUAL_UINT(0, countType(sink, toU16(EventType::DiagnosticWarning)));
}

// ---- stage 09 (C9): pump-response diagnostics wiring -------------------------------

constexpr uint16_t DIAG_MISSING_SRC = static_cast<uint16_t>(EVENT_SOURCE_DIAG_BASE + BR_DIAG_CODE_DIAG_SETTING_MISSING);

static uint16_t warnSrc(uint8_t bit, bool raised) {
    return static_cast<uint16_t>(BR_EVENT_SOURCE_DIAG_BASE + bit * 2u + (raised ? 1u : 0u));
}

// The boiler-room schema WITHOUT the diag table (the stage-07 shape, 5 tables).
inline constexpr ConfigSchema SCHEMA_NO_DIAG = {"boiler-room", BOILER_ROOM_CONFIG_VERSION, BOILER_ROOM_TABLES, 5,
    nullptr, 0};

// A diag table whose first key (b1En) has the wrong type (Int instead of Bool).
inline constexpr SettingDescriptor MISTYPED_DIAG_SETTINGS[] = {
    intSetting("b1En", "b1En", "B1 enable (mistyped)", "B1 (mistyped)", "", "diag", 0, 1, 1),
};
inline constexpr SettingsTable MISTYPED_TABLES[] = {COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE,
    makeTable(BOILER_ROOM_SETTINGS), DISPLAY_SETTINGS_TABLE, BOILER_ROOM_CONTROL_SETTINGS_TABLE,
    makeTable(MISTYPED_DIAG_SETTINGS)};
inline constexpr ConfigSchema SCHEMA_MISTYPED_DIAG = {"boiler-room", BOILER_ROOM_CONFIG_VERSION, MISTYPED_TABLES, 6,
    nullptr, 0};

// B1 scenario: T3 70 / T6 40 constant, P3 NORMAL (link down -> supply NORMAL = ON).
static void setupB1(Fixture& f) {
    f.setOk(BR_SENSOR_T3, 70.0f);
    f.setOk(BR_SENSOR_T6, 40.0f);
    f.sink.clear();
}

// Steps until P3 is actually ON (after the boot lock); returns that time.
static uint64_t runUntilP3Actual(Fixture& f, uint64_t from) {
    uint64_t t = from;
    while (!f.relays.actual(BR_PUMP_P3)) {
        t += 1000;
        f.step(t);
        TEST_ASSERT_TRUE_MESSAGE(t < 5 * 60000ull, "P3 never went actually ON");
    }
    return t;
}

// Steps until warning bit 0 is set; returns that time.
static uint64_t runUntilB1(Fixture& f, uint64_t from, uint64_t limit) {
    uint64_t t = from;
    while ((f.state.diag.warningMask & 1u) == 0) {
        t += 1000;
        f.step(t);
        TEST_ASSERT_TRUE_MESSAGE(t < limit, "B1 never raised");
    }
    return t;
}

static void test_diag_b1_raises_after_min_on_and_clears_on_rise() {
    Fixture f;
    TEST_ASSERT_TRUE(f.rt.diagReady());
    setupB1(f);
    f.state.diag.warningMask = 0xABCD0100u;   // bits 8..31 belong to others (D6)
    f.step(1000);
    TEST_ASSERT_TRUE(f.status.pump[BR_PUMP_P3].on);   // requested by the supply logic
    TEST_ASSERT_EQUAL_HEX32(0xABCD0100u, f.state.diag.warningMask);
    const uint64_t onAt = runUntilP3Actual(f, 1000);
    TEST_ASSERT_EQUAL_UINT(0, countType(f.sink, BR_EVENT_DIAG_WARNING));
    // Relay-actual only (A1): nothing tracked before the lock released P3.
    TEST_ASSERT_TRUE(onAt >= LOCK_MS);
    const uint64_t raisedAt = runUntilB1(f, onAt, onAt + 5 * 60000ull);
    TEST_ASSERT_TRUE(raisedAt >= onAt + 3 * 60000ull);
    TEST_ASSERT_TRUE(raisedAt <= onAt + 3 * 60000ull + 1000);
    TEST_ASSERT_EQUAL_HEX32(0xABCD0100u, f.state.diag.warningMask & ~BR_WARN_OWNED_MASK);
    TEST_ASSERT_EQUAL_UINT(1, countType(f.sink, BR_EVENT_DIAG_WARNING));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, BR_EVENT_DIAG_WARNING, 0x1000 + 0x40 + 0 * 2 + 1));
    for (size_t i = 0; i < f.sink.count(); ++i) {
        if (f.sink.at(i).type == BR_EVENT_DIAG_WARNING) {
            TEST_ASSERT_EQUAL_FLOAT(0.0f, f.sink.at(i).value);
            TEST_ASSERT_EQUAL_FLOAT(1.0f, f.sink.at(i).aux);
            TEST_ASSERT_EQUAL_INT(static_cast<int>(EventReason::Logic), static_cast<int>(f.sink.at(i).reason));
        }
    }
    f.run(raisedAt, raisedAt + 10000);
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, BR_EVENT_DIAG_WARNING, warnSrc(BR_WARN_B1, true)));

    f.setOk(BR_SENSOR_T6, 42.0f);   // T6 rose by 2 C: B1 clears
    f.step(raisedAt + 11000);
    TEST_ASSERT_EQUAL_HEX32(0, f.state.diag.warningMask & (1u << BR_WARN_B1));
    TEST_ASSERT_EQUAL_HEX32(0xABCD0100u, f.state.diag.warningMask & ~BR_WARN_OWNED_MASK);
    const uint16_t clearSrc = warnSrc(BR_WARN_B1, false);
    TEST_ASSERT_EQUAL_HEX16(0x1040, clearSrc);
    TEST_ASSERT_NOT_EQUAL(warnSrc(BR_WARN_B1, true), clearSrc);
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, BR_EVENT_DIAG_WARNING, clearSrc));
    const RecordingEventSink::Record* last = nullptr;
    for (size_t i = 0; i < f.sink.count(); ++i) {
        if (f.sink.at(i).type == BR_EVENT_DIAG_WARNING) {
            last = &f.sink.at(i);
        }
    }
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, last->value);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, last->aux);
}

// NOTE: RecordingEventSink has no rate limiter, so both raises are counted here.
// The real EventLog drops the second raise (same type+source within 60 s) -- see
// test_diag_reraise_within_60s_rate_limited_in_real_log for the stored sequence.
static void test_diag_disable_b1_clears_next_tick_and_logs() {
    Fixture f;
    setupB1(f);
    const uint64_t onAt = runUntilP3Actual(f, 0);
    const uint64_t raisedAt = runUntilB1(f, onAt, onAt + 5 * 60000ull);
    f.setSetting(BR_KEY_B1_EN, 0, raisedAt + 500);
    f.step(raisedAt + 1000);
    TEST_ASSERT_EQUAL_HEX32(0, f.state.diag.warningMask & 1u);
    TEST_ASSERT_EQUAL_UINT(1, countEvents(f.sink, BR_EVENT_DIAG_WARNING, warnSrc(BR_WARN_B1, false)));
    f.setSetting(BR_KEY_B1_EN, 1, raisedAt + 1500);   // D4: re-enable keeps the ON-edge baseline
    f.step(raisedAt + 2000);
    TEST_ASSERT_EQUAL_HEX32(1u, f.state.diag.warningMask & 1u);
    TEST_ASSERT_EQUAL_UINT(2, countEvents(f.sink, BR_EVENT_DIAG_WARNING, warnSrc(BR_WARN_B1, true)));
}

// Review-4 carry-in: with the REAL EventLog (60 s type+source limiter) a
// raise -> clear -> raise of B1 within 60 s stores only raise + clear; the log's
// last B1 entry reads "cleared" while the bit is set (mask/HA are authoritative).
// After the window a new edge is stored again. D7 semantics are unchanged.
static void test_diag_reraise_within_60s_rate_limited_in_real_log() {
    MemoryKvStore cfgKv, logKv;
    FakeClock clock;
    EventLog log(logKv, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgKv, log);
    config.begin(BOILER_ROOM_SCHEMA, 0);
    RelayBank relays;
    relays.configure(BOILER_ROOM_RELAYS, 3, 0);
    relays.setLockMs(LOCK_MS);
    CommonState state{};
    BoilerRoomStatus status{};
    state.sensors.count = 6;
    const float temps[BR_SENSOR_COUNT] = {75.0f, 50.0f, 70.0f, 55.0f, 50.0f, 40.0f};   // B1: T3 70 / T6 40
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        state.sensors.sensor[i].state = SensorState::Ok;
        state.sensors.sensor[i].tempC = temps[i];
        state.sensors.sensor[i].assigned = true;
    }
    BoilerRoomRuntime rt(state, status, config, log, relays);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BoilerRoomRuntimeStatus::Ok), static_cast<int>(rt.begin(0)));
    TEST_ASSERT_TRUE(rt.diagReady());
    const size_t en = static_cast<size_t>(config.indexOf(BR_KEY_B1_EN));
    auto step = [&](uint64_t t) {
        clock.mono = t;
        rt.tick(t);
        relays.update(t, log);
    };
    uint64_t t = 0;
    while ((state.diag.warningMask & 1u) == 0) {
        t += 1000;
        step(t);
        TEST_ASSERT_TRUE_MESSAGE(t < 10 * 60000ull, "B1 never raised");
    }
    const uint64_t raisedAt = t;
    clock.mono = raisedAt + 500;
    config.setNumber(en, 0, EventReason::Web, raisedAt + 500);
    step(raisedAt + 1000);   // clear: accepted (distinct source)
    clock.mono = raisedAt + 1500;
    config.setNumber(en, 1, EventReason::Web, raisedAt + 1500);
    step(raisedAt + 2000);   // re-raise within 60 s of the first raise: rate-limited
    TEST_ASSERT_EQUAL_HEX32(1u, state.diag.warningMask & 1u);

    // Stored B1 sequence, oldest first.
    uint16_t src[8];
    size_t n = 0;
    for (size_t i = log.count(); i > 0 && n < 8; --i) {
        const EventEntry* e = log.newest(i - 1);
        if (e != nullptr && e->type == BR_EVENT_DIAG_WARNING && e->value == 0.0f) {
            src[n++] = e->source;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_HEX16(warnSrc(BR_WARN_B1, true), src[0]);
    TEST_ASSERT_EQUAL_HEX16(warnSrc(BR_WARN_B1, false), src[1]);   // last entry reads "cleared"

    // Past the window a new clear/raise pair is stored again.
    const uint64_t later = raisedAt + 70000;
    for (uint64_t x = raisedAt + 3000; x < later; x += 1000) {
        step(x);
    }
    clock.mono = later;
    config.setNumber(en, 0, EventReason::Web, later);
    step(later + 1000);
    clock.mono = later + 1500;
    config.setNumber(en, 1, EventReason::Web, later + 1500);
    step(later + 2000);
    TEST_ASSERT_EQUAL_HEX32(1u, state.diag.warningMask & 1u);
    const EventEntry* last = nullptr;
    size_t b1 = 0;
    for (size_t i = 0; i < log.count(); ++i) {
        const EventEntry* e = log.newest(i);
        if (e != nullptr && e->type == BR_EVENT_DIAG_WARNING && e->value == 0.0f) {
            if (last == nullptr) {
                last = e;
            }
            ++b1;
        }
    }
    TEST_ASSERT_EQUAL_UINT(4, b1);
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_HEX16(warnSrc(BR_WARN_B1, true), last->source);
}

// All three checks would fire in this scenario (T1 hot, T2/T3/T6 flat).
static void setupAllWarnings(Fixture& f) {
    f.setOk(BR_SENSOR_T1, 80.0f);
    f.setOk(BR_SENSOR_T2, 50.0f);
    f.setOk(BR_SENSOR_T3, 62.0f);
    f.setOk(BR_SENSOR_T6, 40.0f);
    f.sink.clear();
}

static void assertSameControl(const Fixture& a, const Fixture& b, uint64_t t) {
    char msg[64];
    snprintf(msg, sizeof(msg), "control diverged at t=%llu", static_cast<unsigned long long>(t));
    for (uint8_t ch = 0; ch < BR_PUMP_COUNT; ++ch) {
        TEST_ASSERT_EQUAL_MESSAGE(a.relays.requested(ch), b.relays.requested(ch), msg);
        TEST_ASSERT_EQUAL_MESSAGE(a.relays.actual(ch), b.relays.actual(ch), msg);
        TEST_ASSERT_EQUAL_MESSAGE(a.relays.safetyActive(ch), b.relays.safetyActive(ch), msg);
        TEST_ASSERT_EQUAL_MESSAGE(a.status.pump[ch].on, b.status.pump[ch].on, msg);
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(a.status.pump[ch].reason), static_cast<int>(b.status.pump[ch].reason),
            msg);
    }
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(a.state.alarms.activeMask, b.state.alarms.activeMask, msg);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(a.status.p3Mode), static_cast<int>(b.status.p3Mode), msg);
    TEST_ASSERT_EQUAL_MESSAGE(a.status.ready, b.status.ready, msg);
}

static void test_diag_does_not_change_control() {
    Fixture on;                  // diag table present, all enables on (defaults)
    Fixture off;                 // all enables off
    Fixture none(SCHEMA_NO_DIAG);   // no diag table at all
    off.setSetting(BR_KEY_B1_EN, 0);
    off.setSetting(BR_KEY_B3_EN, 0);
    off.setSetting(BR_KEY_B6_EN, 0);
    setupAllWarnings(on);
    setupAllWarnings(off);
    setupAllWarnings(none);
    uint32_t seen = 0;
    for (uint64_t t = 1000; t <= 10 * 60000ull; t += 1000) {
        if (t == 7 * 60000ull) {   // mid-run changes, applied identically
            on.setOk(BR_SENSOR_T1, 92.0f);
            off.setOk(BR_SENSOR_T1, 92.0f);
            none.setOk(BR_SENSOR_T1, 92.0f);
        }
        on.step(t);
        off.step(t);
        none.step(t);
        seen |= on.state.diag.warningMask;
        assertSameControl(on, off, t);
        assertSameControl(on, none, t);
        TEST_ASSERT_EQUAL_HEX32(0, off.state.diag.warningMask);
        TEST_ASSERT_EQUAL_HEX32(0, none.state.diag.warningMask);
    }
    TEST_ASSERT_TRUE(on.relays.actual(BR_PUMP_P3));
    TEST_ASSERT_TRUE_MESSAGE((seen & (1u << BR_WARN_B1)) != 0, "the diag run must actually raise B1");
    TEST_ASSERT_TRUE_MESSAGE((seen & (1u << BR_WARN_B6)) != 0, "the diag run must actually raise B6");
    TEST_ASSERT_EQUAL_UINT(0, countType(off.sink, BR_EVENT_DIAG_WARNING));
    TEST_ASSERT_EQUAL_UINT(0, countType(none.sink, BR_EVENT_DIAG_WARNING));
}

// D2: a missing diag table only disables the diagnostics -- control stays ready.
static void test_diag_table_missing_keeps_control_ready() {
    MemoryKvStore kv;
    RecordingEventSink sink;
    ConfigEngine config{kv, sink};
    RelayBank relays;
    CommonState state{};
    BoilerRoomStatus status{};
    BoilerRoomRuntime rt{state, status, config, sink, relays};
    config.begin(SCHEMA_NO_DIAG, 0);
    relays.configure(BOILER_ROOM_RELAYS, 3, 0);
    relays.setLockMs(LOCK_MS);
    sink.clear();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BoilerRoomRuntimeStatus::Ok), static_cast<int>(rt.begin(0)));
    TEST_ASSERT_TRUE(rt.ready());
    TEST_ASSERT_FALSE(rt.diagReady());
    TEST_ASSERT_EQUAL_UINT(1, countType(sink, toU16(EventType::DiagnosticWarning)));
    TEST_ASSERT_EQUAL_UINT(1, countEvents(sink, toU16(EventType::DiagnosticWarning), DIAG_MISSING_SRC));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sink.at(0).value);   // BrDiagKey::B1En failed first
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventReason::Logic), static_cast<int>(sink.at(0).reason));
    TEST_ASSERT_NOT_EQUAL(DIAG_MISSING_SRC,
        static_cast<uint16_t>(EVENT_SOURCE_DIAG_BASE + BR_DIAG_CODE_SETTING_MISSING));

    state.sensors.count = 6;
    const float temps[BR_SENSOR_COUNT] = {80.0f, 50.0f, 62.0f, 55.0f, 50.0f, 40.0f};
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        state.sensors.sensor[i].state = SensorState::Ok;
        state.sensors.sensor[i].tempC = temps[i];
        state.sensors.sensor[i].assigned = true;
    }
    state.diag.warningMask = 0x00000107u;   // stale owned bits 0..2 are cleared, bit 8 kept
    for (uint64_t t = 1000; t <= 10 * 60000ull; t += 1000) {
        rt.tick(t);
        relays.update(t, sink);
        TEST_ASSERT_TRUE(status.ready);
        TEST_ASSERT_EQUAL_HEX32(0x00000100u, state.diag.warningMask);
    }
    TEST_ASSERT_FALSE(relays.safetyActive(BR_PUMP_P1));   // no not-ready P1 force
    TEST_ASSERT_TRUE(relays.actual(BR_PUMP_P3));          // control runs: P3 NORMAL
    TEST_ASSERT_EQUAL_UINT(0, countType(sink, BR_EVENT_DIAG_WARNING));
    TEST_ASSERT_EQUAL_UINT(1, countType(sink, toU16(EventType::DiagnosticWarning)));
}

static void test_diag_key_mistyped_keeps_control_ready() {
    Fixture f(SCHEMA_MISTYPED_DIAG);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BoilerRoomRuntimeStatus::Ok), static_cast<int>(f.runtimeStatus));
    TEST_ASSERT_TRUE(f.rt.ready());
    TEST_ASSERT_FALSE(f.rt.diagReady());
    setupAllWarnings(f);
    f.run(0, 10 * 60000ull);
    TEST_ASSERT_TRUE(f.status.ready);
    TEST_ASSERT_FALSE(f.relays.safetyActive(BR_PUMP_P1));
    TEST_ASSERT_EQUAL_HEX32(0, f.state.diag.warningMask);
    TEST_ASSERT_EQUAL_UINT(0, countType(f.sink, BR_EVENT_DIAG_WARNING));
}

// Not ready (a control key is missing): only the owned warning bits 0..7 are
// cleared (bits 8..31 kept) and no warning events are logged, since nothing was
// ever published. (A ready -> not-ready transition cannot happen today: begin()
// runs once, so the "clear logged once" path in tick() is defensive only.)
static void test_diag_not_ready_clears_owned_bits() {
    Fixture f(TEST_SCHEMA_A_V1);
    TEST_ASSERT_FALSE(f.rt.ready());
    TEST_ASSERT_FALSE(f.rt.diagReady());
    f.state.diag.warningMask = 0xFF0000FFu;
    f.run(0, 5000);
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u, f.state.diag.warningMask);
    TEST_ASSERT_EQUAL_UINT(0, countType(f.sink, BR_EVENT_DIAG_WARNING));   // nothing was published
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_ok_on_real_schema);
    RUN_TEST(test_begin_missing_on_test_schema_forces_p1_safety);
    RUN_TEST(test_begin_missing_logs_diag_once_per_boot);
    RUN_TEST(test_begin_ok_logs_no_diag);
    RUN_TEST(test_nan_t1_ok_is_t1_fault);
    RUN_TEST(test_nan_t1_ok_does_not_freeze_overheat_latch);
    RUN_TEST(test_nan_t2_ok_is_t2_fault);
    RUN_TEST(test_nan_t3_ok_is_t3_fault);
    RUN_TEST(test_nan_t4_t5_ok_energy_stays_finite);
    RUN_TEST(test_lock_bypass_matrix);
    RUN_TEST(test_real_ha_gate);
    RUN_TEST(test_offer_self_clears_flag_via_config_engine);
    RUN_TEST(test_overheat_with_flag_bypasses_lock_and_keeps_flag);
    RUN_TEST(test_anti_freeze_runs_after_interval_default_settings);
    RUN_TEST(test_anti_freeze_bypasses_min_off_lock);
    RUN_TEST(test_anti_freeze_timer_shared_with_exercise);
    RUN_TEST(test_anti_freeze_not_before_interval_after_late_begin);
    RUN_TEST(test_alarm_bits_preserve_stage03_and_log_edges_once);
    RUN_TEST(test_p3_mode_events_distinct_sources);
    RUN_TEST(test_p3_mode_events_survive_real_rate_limiter);
    RUN_TEST(test_pump_request_events_only_on_on_or_safety_flip);
    RUN_TEST(test_t1_fault_recovery_lock_governs_switch);
    RUN_TEST(test_t1_reassignment_unassigned_to_unknown);
    RUN_TEST(test_ota_inhibit_keeps_all_relays_off_during_overheat);
    RUN_TEST(test_setting_change_takes_effect_next_tick);
    RUN_TEST(test_status_fields_scripted_scenario);
    RUN_TEST(test_diag_b1_raises_after_min_on_and_clears_on_rise);
    RUN_TEST(test_diag_disable_b1_clears_next_tick_and_logs);
    RUN_TEST(test_diag_reraise_within_60s_rate_limited_in_real_log);
    RUN_TEST(test_diag_does_not_change_control);
    RUN_TEST(test_diag_table_missing_keeps_control_ready);
    RUN_TEST(test_diag_key_mistyped_keeps_control_ready);
    RUN_TEST(test_diag_not_ready_clears_owned_bits);
    return UNITY_END();
}
