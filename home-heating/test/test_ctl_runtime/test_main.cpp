#include <unity.h>
#include <math.h>
#include <optional>
#include <AntiSeizeScheduler.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <HomeHeatingAlarms.h>
#include <HomeHeatingEvents.h>
#include <HomeHeatingRuntime.h>
#include <HomeHeatingStatus.h>
#include <K1Driver.h>
#include <RelayBank.h>
#include "../../../common/test/fakes/MemoryKvStore.h"
#include "../../../common/test/fakes/RecordingEventSink.h"
#include "../../../common/test/fakes/TestSchema.h"
#include "../../src/HomeHeatingHardware.h"
#include "../../src/HomeHeatingSchema.h"

// Stage 08 phase 6: HomeHeatingRuntime against the REAL HOME_HEATING_SCHEMA
// (ConfigEngine), a REAL RelayBank (60 s lock, inhibit), a REAL K1Driver and a
// REAL AntiSeizeScheduler. The fixture mirrors the firmware order: a fast tick
// every 100 ms (k1.tick + K1 power/direction replay + relays.update, or
// k1.cancel while the OTA flag is set -- as HwRuntime::fastTick does) and
// runtime.tick every 1 s right after the fast tick of the same millisecond.

void setUp() {}
void tearDown() {}

constexpr uint32_t LOCK_MS = 60000;
constexpr uint16_t DIAG_SOURCE = EVENT_SOURCE_DIAG_BASE + HH_DIAG_CODE_SETTING_MISSING;

using KvPrepare = void (*)(MemoryKvStore&);

struct Fixture {
    MemoryKvStore kv;
    RecordingEventSink sink;
    ConfigEngine config{kv, sink};
    RelayBank relays;
    K1Driver k1;
    AntiSeizeScheduler antiSeize{relays, k1, sink};
    CommonState state{};
    HomeHeatingStatus status{};
    HomeHeatingRuntime rt{state, status, config, sink, relays, k1, antiSeize};

    HomeHeatingRuntimeStatus runtimeStatus;
    size_t beginDiagCount = 0;
    uint64_t now = 0;
    bool ota = false;
    bool k1BusyWhileOta = false;   // any K1 activity observed while inhibited
    bool safetySeen = false;       // the safety slot must never be used (A5)

    explicit Fixture(const ConfigSchema& schema = HOME_HEATING_SCHEMA, KvPrepare prepare = nullptr) {
        if (prepare != nullptr) {
            prepare(kv);
        }
        config.begin(schema, 0);
        relays.configure(HOME_HEATING_RELAYS, 4, 0);
        relays.setLockMs(LOCK_MS);
        k1.begin(0);
        antiSeize.configure(HOME_HEATING_ANTI_SEIZE, 3, 0);
        state.sensors.count = 4;
        setOk(HH_SENSOR_H1, 30.0f);
        setOk(HH_SENSOR_H2, 40.0f);
        setOk(HH_SENSOR_H3, 70.0f);
        setOk(HH_SENSOR_H4, 50.0f);
        runtimeStatus = rt.begin(0);
        beginDiagCount = countEvents(toU16(EventType::DiagnosticWarning), DIAG_SOURCE);
        sink.clear();
    }

    size_t countEvents(uint16_t type, uint16_t source) const {
        size_t n = 0;
        for (size_t i = 0; i < sink.count(); ++i) {
            if (sink.at(i).type == type && sink.at(i).source == source) {
                ++n;
            }
        }
        return n;
    }
    size_t countType(uint16_t type) const {
        size_t n = 0;
        for (size_t i = 0; i < sink.count(); ++i) {
            if (sink.at(i).type == type) {
                ++n;
            }
        }
        return n;
    }

    void setSensor(uint8_t i, SensorState s, float t = NAN, bool missing = false) {
        LogicalSensorStatus& x = state.sensors.sensor[i];
        x.state = s;
        x.tempC = (s == SensorState::Ok) ? t : NAN;
        x.assigned = (s != SensorState::Unassigned);
        x.missing = missing;
    }
    void setOk(uint8_t i, float t) { setSensor(i, SensorState::Ok, t); }

    size_t idx(const char* key) const { return static_cast<size_t>(config.indexOf(key)); }
    void setSetting(const char* key, float v) { config.setNumber(idx(key), v, EventReason::Web, now); }

    void fast(uint64_t t) {
        if (ota) {
            if (k1.busy()) {
                k1.cancel(t);
            }
        } else {
            k1.tick(t);
            relays.requestControl(HH_RELAY_K1_POWER, k1.powerOn(), RelayReason::K1Drive);
            relays.requestControl(HH_RELAY_K1_DIR, k1.directionOpen(), RelayReason::K1Drive);
        }
        relays.update(t, sink);
        for (uint8_t ch = 0; ch < 4; ++ch) {
            safetySeen = safetySeen || relays.safetyActive(ch);
        }
    }
    void slow(uint64_t t) {
        rt.tick(t);
        if (ota && k1.busy()) {
            k1BusyWhileOta = true;
        }
    }
    // The t = 0 step (boot): fast tick then the first runtime tick.
    void start() {
        now = 0;
        fast(0);
        slow(0);
    }
    // Advances in 100 ms fast ticks to `target` (ms), runtime tick on every whole second.
    void runTo(uint64_t target) {
        while (now < target) {
            now += 100;
            fast(now);
            if (now % 1000 == 0) {
                slow(now);
            }
        }
    }
    void setOta(bool on) {
        ota = on;
        relays.setInhibited(on, now, sink);
        if (on && k1.busy()) {
            k1.cancel(now);   // HwRuntime::setOutputsInhibited
        }
    }
};

// ---- begin / not ready -------------------------------------------------------------

static void test_begin_ok_on_real_schema() {
    Fixture f;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HomeHeatingRuntimeStatus::Ok), static_cast<int>(f.runtimeStatus));
    TEST_ASSERT_TRUE(f.rt.ready());
    TEST_ASSERT_EQUAL_UINT32(0, f.beginDiagCount);
    TEST_ASSERT_EQUAL_UINT32(0, f.rt.k1StrokeMs());
    f.start();
    TEST_ASSERT_TRUE(f.status.ready);
    TEST_ASSERT_TRUE(f.status.heatingEnabled);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, f.status.h2Set);
}

static void test_begin_missing_not_ready_outputs_off() {
    Fixture f(TEST_SCHEMA_A_V1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HomeHeatingRuntimeStatus::SettingMissing), static_cast<int>(f.runtimeStatus));
    TEST_ASSERT_FALSE(f.rt.ready());
    TEST_ASSERT_EQUAL_UINT32(1, f.beginDiagCount);
    // Once per boot: a second begin() does not log again.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HomeHeatingRuntimeStatus::SettingMissing), static_cast<int>(f.rt.begin(0)));
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(toU16(EventType::DiagnosticWarning)));
    f.state.alarms.activeMask = 0x80000001u;   // untouched while not ready (D20)
    f.start();
    f.runTo(90000);
    TEST_ASSERT_FALSE(f.status.ready);
    TEST_ASSERT_FALSE(f.status.noNeed);
    TEST_ASSERT_FALSE(f.relays.requested(HH_RELAY_P4));
    TEST_ASSERT_FALSE(f.relays.requested(HH_RELAY_K2));
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_P4));
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_K2));
    TEST_ASSERT_FALSE(f.k1.busy());
    TEST_ASSERT_FALSE(f.k1.hasMoved());
    TEST_ASSERT_EQUAL_UINT32(0, f.rt.k1StrokeMs());
    TEST_ASSERT_EQUAL_HEX32(0x80000001u, f.state.alarms.activeMask);
    TEST_ASSERT_FALSE(f.safetySeen);
}

// ---- lock / slot matrix (A5: control slot only, 60 s lock) --------------------------

static void test_hot_h3_p4_waits_for_boot_lock() {
    Fixture f;
    f.start();
    TEST_ASSERT_TRUE(f.status.p4On);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P4Reason::Demand), static_cast<int>(f.status.p4Reason));
    TEST_ASSERT_TRUE(f.relays.requested(HH_RELAY_P4));
    f.runTo(59900);
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_P4));
    TEST_ASSERT_TRUE(f.relays.lockDelayed(HH_RELAY_P4));
    f.runTo(61000);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_P4));
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_K2));   // TANK (charging)
    TEST_ASSERT_FALSE(f.safetySeen);
}

static void test_h3_fault_forced_p4_still_waits_for_lock() {
    Fixture f;
    f.setSensor(HH_SENSOR_H3, SensorState::Fault);
    f.start();
    TEST_ASSERT_TRUE(f.status.p4On);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P4Reason::H3FaultForced), static_cast<int>(f.status.p4Reason));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(FailMode::H3), static_cast<int>(f.status.fail));
    f.runTo(59900);
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_P4));
    f.runTo(61000);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_P4));
    // H3 fault -> K2 BYPASS (D13), through the control slot; the boot lock applies too.
    TEST_ASSERT_TRUE(f.status.k2Bypass);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_K2));
    TEST_ASSERT_FALSE(f.safetySeen);
}

static void test_k2_flip_back_within_lock_is_delayed() {
    Fixture f;
    f.start();
    f.runTo(70000);
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_K2));
    f.setOk(HH_SENSOR_H4, 71.0f);   // tank full -> BYPASS; boot lock long expired -> immediate
    f.runTo(71000);
    TEST_ASSERT_TRUE(f.status.k2Bypass);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K2Reason::H4Full), static_cast<int>(f.status.k2Reason));
    f.runTo(71100);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_K2));
    f.setOk(HH_SENSOR_H4, 50.0f);   // back to TANK within 60 s of the last switch
    f.runTo(80000);
    TEST_ASSERT_FALSE(f.status.k2Bypass);
    TEST_ASSERT_FALSE(f.relays.requested(HH_RELAY_K2));
    f.runTo(131000);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_K2));
    TEST_ASSERT_TRUE(f.relays.lockDelayed(HH_RELAY_K2));
    f.runTo(132000);
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_K2));
    TEST_ASSERT_FALSE(f.safetySeen);
}

static void test_h4_fault_k2_bypass_via_control_slot() {
    Fixture f;
    f.start();
    f.runTo(70000);
    f.setSensor(HH_SENSOR_H4, SensorState::Fault);
    f.runTo(71100);
    TEST_ASSERT_TRUE(f.status.k2Bypass);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K2Reason::H4Fault), static_cast<int>(f.status.k2Reason));
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_K2));
    TEST_ASSERT_FALSE(f.relays.safetyActive(HH_RELAY_K2));
    TEST_ASSERT_FALSE(f.safetySeen);
}

// ---- K1 ----------------------------------------------------------------------------

// Boot with H1 30, H2 40 (= set, no feedback), H3 70: FF x = (40-30)/(70-30) = 25 %.
static void test_k1_boot_recal_then_feedforward() {
    Fixture f;
    f.start();
    TEST_ASSERT_TRUE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::Control), static_cast<int>(f.k1.owner()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Close), static_cast<int>(f.k1.direction()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Mode::Recalibrating), static_cast<int>(f.status.k1Mode));
    TEST_ASSERT_FALSE(f.status.k1Known);
    f.runTo(60000);
    TEST_ASSERT_EQUAL_INT8(-1, f.status.k1Moving);
    f.runTo(132000);
    TEST_ASSERT_FALSE(f.status.k1Known);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Mode::Recalibrating), static_cast<int>(f.status.k1Mode));
    f.runTo(133000);   // CLOSE 132 000 ms ended at 132.1 s -> known 0 %, FF applied at once
    TEST_ASSERT_TRUE(f.status.k1Known);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Mode::Normal), static_cast<int>(f.status.k1Mode));
    TEST_ASSERT_TRUE(f.status.k1FfValid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, f.status.k1FfPct);
    TEST_ASSERT_TRUE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Open), static_cast<int>(f.k1.direction()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::Control), static_cast<int>(f.k1.owner()));
    // Live projection while the OPEN pulse runs.
    f.runTo(150000);
    TEST_ASSERT_EQUAL_INT8(1, f.status.k1Moving);
    const float live = 100.0f * static_cast<float>(f.k1.currentRunMs(150000)) / 120000.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, live, f.status.k1PosPct);
    TEST_ASSERT_TRUE(f.status.k1PosPct > 5.0f && f.status.k1PosPct < 20.0f);
    // OPEN 30 000 ms (25 % of 120 s) -> estimate 25 % once takeMotion() is integrated.
    f.runTo(170000);
    TEST_ASSERT_FALSE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT8(0, f.status.k1Moving);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, f.status.k1PosPct);
    f.runTo(260000);   // FF unchanged, H2 on set -> no further moves
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, f.status.k1PosPct);
}

static void test_k1_p4_stop_recal_replaces_running_pulse() {
    Fixture f;
    f.start();
    f.runTo(140000);
    TEST_ASSERT_TRUE(f.k1.running());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Open), static_cast<int>(f.k1.direction()));
    f.setSetting(HH_KEY_P4_OFF_DELAY, 0.0f);
    f.setOk(HH_SENSOR_H3, 35.0f);   // below H2_set, no off-delay -> P4 request falls -> recal
    f.sink.clear();
    f.runTo(141000);
    TEST_ASSERT_FALSE(f.status.p4On);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Mode::Recalibrating), static_cast<int>(f.status.k1Mode));
    TEST_ASSERT_TRUE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Close), static_cast<int>(f.k1.direction()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::Control), static_cast<int>(f.k1.owner()));
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_K1_RECAL, HH_EVENT_SOURCE_K1_RECAL_BASE + 1));
}

// ---- OTA inhibit (D15) -------------------------------------------------------------

static void test_ota_cancel_counts_partial_motion_once_then_reapplies() {
    Fixture f;
    f.start();
    f.runTo(140000);
    TEST_ASSERT_TRUE(f.k1.running());
    const uint32_t partialMs = f.k1.currentRunMs(140000);
    TEST_ASSERT_TRUE(partialMs > 1000);
    f.setOta(true);
    TEST_ASSERT_FALSE(f.k1.busy());
    f.runTo(141000);
    const float partialPct = 100.0f * static_cast<float>(partialMs) / 120000.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, partialPct, f.status.k1PosPct);
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_P4));
    f.runTo(150000);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, partialPct, f.status.k1PosPct);   // counted exactly once
    TEST_ASSERT_TRUE(f.status.p4On);                                   // the request is kept
    TEST_ASSERT_FALSE(f.k1BusyWhileOta);
    TEST_ASSERT_FALSE(f.k1.hasMoved() && f.k1.lastMoveMs() > 140000);
    f.setOta(false);
    // P4 re-applies by itself, but only after the lock (the inhibit drop was a switch).
    f.runTo(199000);
    TEST_ASSERT_TRUE(f.relays.requested(HH_RELAY_P4));
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_P4));
    f.runTo(201000);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_P4));
    // K1 held closed until P4 runs again (D10), then FF re-applied -> 25 %.
    f.runTo(300000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Mode::Normal), static_cast<int>(f.status.k1Mode));
    TEST_ASSERT_TRUE(f.status.k1FfValid);
    TEST_ASSERT_FALSE(f.k1.busy());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, f.status.k1PosPct);
    TEST_ASSERT_FALSE(f.safetySeen);
}

// ---- anti-seize (D14/D16) -----------------------------------------------------------

static void test_anti_seize_stroke_follows_travel() {
    Fixture f;
    f.start();
    TEST_ASSERT_EQUAL_UINT32(132000, f.rt.k1StrokeMs());
    f.runTo(5000);
    f.setSetting(HH_KEY_K1_TRAVEL, 150.0f);
    TEST_ASSERT_EQUAL_UINT32(132000, f.rt.k1StrokeMs());
    f.runTo(6000);
    TEST_ASSERT_EQUAL_UINT32(165000, f.rt.k1StrokeMs());
}

// ---- no-need end-to-end (A2, D14) -------------------------------------------------

static void test_no_need_end_to_end_with_exercise_transparency() {
    Fixture f;
    f.setSetting(HH_KEY_P4_OFF_DELAY, 0.0f);
    f.setOk(HH_SENSOR_H3, 35.0f);   // P4 OFF supply cold; K2 BYPASS (H3 low)
    f.start();
    TEST_ASSERT_FALSE(f.status.p4On);
    TEST_ASSERT_TRUE(f.status.k2Bypass);
    f.runTo(59000);
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_K2));
    TEST_ASSERT_FALSE(f.status.noNeed);   // requested, but the relay is not energised yet
    f.runTo(61000);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_K2));
    TEST_ASSERT_TRUE(f.status.noNeed);
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_NO_NEED, HH_EVENT_SOURCE_NO_NEED_BASE + 1));

    // K2 anti-seize exercise holds TANK: transparent while antiSeize reports it running.
    f.runTo(130000);
    f.state.antiSeize.count = 3;
    f.state.antiSeize.output[HH_AS_K2].running = true;
    f.relays.setExercise(HH_RELAY_K2, false);
    f.runTo(131000);
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_K2));
    TEST_ASSERT_TRUE(f.status.noNeed);
    // Same relay state without the running flag: no-need drops (actual TANK).
    f.state.antiSeize.output[HH_AS_K2].running = false;
    f.runTo(132000);
    TEST_ASSERT_FALSE(f.status.noNeed);
    f.relays.setExercise(HH_RELAY_K2, std::nullopt);
    f.runTo(200000);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_K2));
    TEST_ASSERT_TRUE(f.status.noNeed);

    // H3 rises -> P4 requested ON -> no-need false the same tick (before any relay moves).
    f.setOk(HH_SENSOR_H3, 70.0f);
    f.slow(f.now);
    TEST_ASSERT_TRUE(f.status.p4On);
    TEST_ASSERT_FALSE(f.relays.actual(HH_RELAY_P4));
    TEST_ASSERT_FALSE(f.status.noNeed);
    TEST_ASSERT_EQUAL_UINT32(2, f.countEvents(HH_EVENT_NO_NEED, HH_EVENT_SOURCE_NO_NEED_BASE + 0));
}

// ---- events (C11) ------------------------------------------------------------------

static void test_events_logged_once_with_c11_codes() {
    Fixture f;
    f.start();
    const uint16_t p4Src = EVENT_SOURCE_RELAY_BASE + HH_RELAY_P4;
    const uint16_t k2Src = EVENT_SOURCE_RELAY_BASE + HH_RELAY_K2;
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_P4_REQUEST, p4Src));
    for (size_t i = 0; i < f.sink.count(); ++i) {
        if (f.sink.at(i).type == HH_EVENT_P4_REQUEST) {
            TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(P4Reason::Demand), f.sink.at(i).value);
            TEST_ASSERT_EQUAL_FLOAT(1.0f, f.sink.at(i).aux);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_K1_RECAL, HH_EVENT_SOURCE_K1_RECAL_BASE + 1));
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_K2_REQUEST));
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_FAILSAFE));
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_NO_NEED));
    f.runTo(133000);
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_K1_RECAL, HH_EVENT_SOURCE_K1_RECAL_BASE + 0));

    // H3 fault: fail-mode H3 entered, P4 reason-only change (Demand -> H3FaultForced) not logged.
    f.setSensor(HH_SENSOR_H3, SensorState::Fault);
    f.runTo(140000);
    TEST_ASSERT_EQUAL_UINT32(1, f.countType(HH_EVENT_P4_REQUEST));
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_FAILSAFE, HH_EVENT_SOURCE_FAILSAFE_BASE + 3));
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_K2_REQUEST, k2Src));
    for (size_t i = 0; i < f.sink.count(); ++i) {
        const auto& e = f.sink.at(i);
        const bool ours = e.type >= EVENT_TYPE_PROJECT_BASE || e.type == toU16(EventType::AlarmRaised);
        if (ours) {
            TEST_ASSERT_EQUAL_INT(static_cast<int>(EventReason::Logic), static_cast<int>(e.reason));
        }
        if (e.type == HH_EVENT_FAILSAFE) {
            TEST_ASSERT_EQUAL_FLOAT(3.0f, e.value);
            TEST_ASSERT_EQUAL_FLOAT(0.0f, e.aux);
        }
        if (e.type == HH_EVENT_K2_REQUEST) {
            TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(K2Reason::H3Fault), e.value);
            TEST_ASSERT_EQUAL_FLOAT(1.0f, e.aux);
        }
    }
    const uint16_t bit0 = EVENT_SOURCE_ALARM_BASE + HH_ALARM_H3_FAILSAFE;
    const uint16_t bitH3 = EVENT_SOURCE_ALARM_BASE + HH_ALARM_SENSOR_BASE + HH_SENSOR_H3;
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(toU16(EventType::AlarmRaised), bit0));
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(toU16(EventType::AlarmRaised), bitH3));
    TEST_ASSERT_EQUAL_UINT32(2, f.countType(toU16(EventType::AlarmRaised)));

    // Recovery: fail-mode exit (value 0, aux 3), K2 back to TANK, alarms cleared once.
    f.setOk(HH_SENSOR_H3, 70.0f);
    f.runTo(150000);
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_FAILSAFE, HH_EVENT_SOURCE_FAILSAFE_BASE + 0));
    TEST_ASSERT_EQUAL_UINT32(2, f.countEvents(HH_EVENT_K2_REQUEST, k2Src));
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(toU16(EventType::AlarmCleared), bit0));
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(toU16(EventType::AlarmCleared), bitH3));
    TEST_ASSERT_EQUAL_UINT32(2, f.countType(toU16(EventType::AlarmCleared)));
    TEST_ASSERT_EQUAL_UINT32(1, f.countType(HH_EVENT_P4_REQUEST));
    TEST_ASSERT_EQUAL_UINT32(2, f.countType(HH_EVENT_FAILSAFE));
    for (size_t i = 0; i < f.sink.count(); ++i) {
        const auto& e = f.sink.at(i);
        if (e.type == HH_EVENT_FAILSAFE && e.source == HH_EVENT_SOURCE_FAILSAFE_BASE) {
            TEST_ASSERT_EQUAL_FLOAT(0.0f, e.value);
            TEST_ASSERT_EQUAL_FLOAT(3.0f, e.aux);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_NO_NEED));
}

// ---- alarms / NaN guard / settings ---------------------------------------------------

static void test_alarm_merge_preserves_stage03_bits() {
    Fixture f;
    f.state.alarms.activeMask = 0xFF000000u;
    f.setSensor(HH_SENSOR_H3, SensorState::Fault);
    f.start();
    const uint32_t ours = (1u << HH_ALARM_H3_FAILSAFE) | (1u << (HH_ALARM_SENSOR_BASE + HH_SENSOR_H3));
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u | ours, f.state.alarms.activeMask);
    TEST_ASSERT_EQUAL_HEX32(ours, f.status.alarmMask);
    f.setOk(HH_SENSOR_H3, 70.0f);
    f.runTo(1000);
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u, f.state.alarms.activeMask);
    TEST_ASSERT_EQUAL_HEX32(0, f.status.alarmMask);
}

static void checkNonFiniteH3(float bad) {
    Fixture f;
    f.start();
    f.state.sensors.sensor[HH_SENSOR_H3].state = SensorState::Ok;
    f.state.sensors.sensor[HH_SENSOR_H3].tempC = bad;
    f.runTo(1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(FailMode::H3), static_cast<int>(f.status.fail));
    TEST_ASSERT_TRUE(f.status.p4On);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(P4Reason::H3FaultForced), static_cast<int>(f.status.p4Reason));
    TEST_ASSERT_TRUE(f.status.k2Bypass);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K2Reason::H3Fault), static_cast<int>(f.status.k2Reason));
    TEST_ASSERT_TRUE((f.state.alarms.activeMask & (1u << (HH_ALARM_SENSOR_BASE + HH_SENSOR_H3))) != 0);
}

static void test_nan_h3_ok_is_h3_fault() { checkNonFiniteH3(NAN); }
static void test_pos_inf_h3_ok_is_h3_fault() { checkNonFiniteH3(INFINITY); }
static void test_neg_inf_h3_ok_is_h3_fault() { checkNonFiniteH3(-INFINITY); }

static void test_sensor_index_beyond_count_is_unassigned() {
    Fixture f;
    f.state.sensors.count = 3;   // H4 not provided
    f.start();
    TEST_ASSERT_TRUE(f.status.k2Bypass);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K2Reason::H4Fault), static_cast<int>(f.status.k2Reason));
    TEST_ASSERT_TRUE((f.status.alarmMask & (1u << HH_ALARM_H4_FAILSAFE)) != 0);
}

static void prepareBadNvs(MemoryKvStore& kv) {
    kv.setI32(CONFIG_VERSION_KEY, 1);
    kv.setI32(HH_KEY_H2_SET, 55);        // Float row stored as I32 -> rejected, default kept
    kv.setFloat(HH_KEY_K1_TRAVEL, 90.0f); // Int row stored as Float -> rejected, default kept
    kv.commit();
}

static void test_bad_nvs_values_fall_back_to_defaults() {
    Fixture f(HOME_HEATING_SCHEMA, prepareBadNvs);
    TEST_ASSERT_TRUE(f.rt.ready());
    size_t idx[HH_RUNTIME_KEY_COUNT];
    const char* const keys[HH_RUNTIME_KEY_COUNT] = {HH_KEY_H2_SET, HH_KEY_P4_OFF_DELAY, HH_KEY_K1_TRAVEL,
        HH_KEY_K1_PERIOD, HH_KEY_K1_DEADBAND, HH_KEY_K1_GAIN, HH_KEY_K1_MAX_PULSE, HH_KEY_K1_MIN_PULSE,
        HH_KEY_K1_RESYNC, HH_KEY_K1_SMALL_DIFF, HH_KEY_K1_FAIL_POS, HH_KEY_K1_FF_STEP, HH_KEY_K2_DELTA,
        HH_KEY_K2_DELTA_HYST, HH_KEY_K2_H3_MIN, HH_KEY_K2_H3_MIN_HYST, HH_KEY_K2_H4_MAX, HH_KEY_K2_H4_MAX_HYST,
        HH_KEY_HEATING_ENABLED};
    for (size_t k = 0; k < HH_RUNTIME_KEY_COUNT; ++k) {
        idx[k] = f.idx(keys[k]);
    }
    const HomeHeatingSettings s = HomeHeatingRuntime::readSettings(f.config, idx);
    const HomeHeatingSettings d = defaultHomeHeatingSettings();
    TEST_ASSERT_EQUAL(d.heatingEnabled, s.heatingEnabled);
    TEST_ASSERT_EQUAL_FLOAT(d.h2Set, s.h2Set);
    TEST_ASSERT_EQUAL_UINT32(d.p4OffDelayMin, s.p4OffDelayMin);
    TEST_ASSERT_EQUAL_UINT32(d.k1TravelS, s.k1TravelS);
    TEST_ASSERT_EQUAL_UINT32(d.k1PeriodS, s.k1PeriodS);
    TEST_ASSERT_EQUAL_FLOAT(d.k1Deadband, s.k1Deadband);
    TEST_ASSERT_EQUAL_FLOAT(d.k1Gain, s.k1Gain);
    TEST_ASSERT_EQUAL_UINT32(d.k1MaxPulseS, s.k1MaxPulseS);
    TEST_ASSERT_EQUAL_FLOAT(d.k1MinPulseS, s.k1MinPulseS);
    TEST_ASSERT_EQUAL_UINT32(d.k1ResyncPct, s.k1ResyncPct);
    TEST_ASSERT_EQUAL_FLOAT(d.k1SmallDiff, s.k1SmallDiff);
    TEST_ASSERT_EQUAL_UINT32(d.k1FailPosPct, s.k1FailPosPct);
    TEST_ASSERT_EQUAL_UINT32(d.k1FfStepPct, s.k1FfStepPct);
    TEST_ASSERT_EQUAL_FLOAT(d.k2Delta, s.k2Delta);
    TEST_ASSERT_EQUAL_FLOAT(d.k2DeltaHyst, s.k2DeltaHyst);
    TEST_ASSERT_EQUAL_FLOAT(d.k2H3Min, s.k2H3Min);
    TEST_ASSERT_EQUAL_FLOAT(d.k2H3MinHyst, s.k2H3MinHyst);
    TEST_ASSERT_EQUAL_FLOAT(d.k2H4Max, s.k2H4Max);
    TEST_ASSERT_EQUAL_FLOAT(d.k2H4MaxHyst, s.k2H4MaxHyst);
    f.start();
    TEST_ASSERT_EQUAL_FLOAT(40.0f, f.status.h2Set);
    TEST_ASSERT_EQUAL_UINT32(132000, f.rt.k1StrokeMs());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_ok_on_real_schema);
    RUN_TEST(test_begin_missing_not_ready_outputs_off);
    RUN_TEST(test_hot_h3_p4_waits_for_boot_lock);
    RUN_TEST(test_h3_fault_forced_p4_still_waits_for_lock);
    RUN_TEST(test_k2_flip_back_within_lock_is_delayed);
    RUN_TEST(test_h4_fault_k2_bypass_via_control_slot);
    RUN_TEST(test_k1_boot_recal_then_feedforward);
    RUN_TEST(test_k1_p4_stop_recal_replaces_running_pulse);
    RUN_TEST(test_ota_cancel_counts_partial_motion_once_then_reapplies);
    RUN_TEST(test_anti_seize_stroke_follows_travel);
    RUN_TEST(test_no_need_end_to_end_with_exercise_transparency);
    RUN_TEST(test_events_logged_once_with_c11_codes);
    RUN_TEST(test_alarm_merge_preserves_stage03_bits);
    RUN_TEST(test_nan_h3_ok_is_h3_fault);
    RUN_TEST(test_pos_inf_h3_ok_is_h3_fault);
    RUN_TEST(test_neg_inf_h3_ok_is_h3_fault);
    RUN_TEST(test_sensor_index_beyond_count_is_unassigned);
    RUN_TEST(test_bad_nvs_values_fall_back_to_defaults);
    return UNITY_END();
}
