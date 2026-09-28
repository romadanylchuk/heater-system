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
    size_t beginDiagMissingCount = 0;   // stage 09: DIAG_BASE + 33 at begin()
    bool useLocal = false;              // stage 09: tick(now, local) instead of tick(now)
    LocalTimeInfo local = NO_LOCAL_TIME;
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
        beginDiagMissingCount = countEvents(toU16(EventType::DiagnosticWarning),
            static_cast<uint16_t>(EVENT_SOURCE_DIAG_BASE + HH_DIAG_CODE_DIAG_SETTING_MISSING));
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
        if (useLocal) {
            rt.tick(t, local);
        } else {
            rt.tick(t);
        }
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

// ---- stage 09 phase 8 (C15): diagnostics, pulse counter, last pulse, H2 error ------

static uint16_t warnSrc(uint8_t bit, bool raised) {
    return static_cast<uint16_t>(HH_EVENT_SOURCE_DIAG_BASE + bit * 2u + (raised ? 1u : 0u));
}
constexpr uint16_t DIAG_MISSING_SRC =
    static_cast<uint16_t>(EVENT_SOURCE_DIAG_BASE + HH_DIAG_CODE_DIAG_SETTING_MISSING);

// The home-heating schema WITHOUT the diag table (the stage-08 shape, 5 tables).
inline constexpr ConfigSchema SCHEMA_NO_DIAG = {"home-heating", HOME_HEATING_CONFIG_VERSION, HOME_HEATING_TABLES, 5,
    nullptr, 0};

// A diag table whose first key (h1En) has the wrong type (Int instead of Bool).
inline constexpr SettingDescriptor MISTYPED_DIAG_SETTINGS[] = {
    intSetting("h1En", "h1En", "H1 enable (mistyped)", "H1 (mistyped)", "", "diag", 0, 1, 1),
};
inline constexpr SettingsTable MISTYPED_TABLES[] = {COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE,
    makeTable(HOME_HEATING_SETTINGS), DISPLAY_SETTINGS_TABLE, HOME_HEATING_CONTROL_SETTINGS_TABLE,
    makeTable(MISTYPED_DIAG_SETTINGS)};
inline constexpr ConfigSchema SCHEMA_MISTYPED_DIAG = {"home-heating", HOME_HEATING_CONFIG_VERSION, MISTYPED_TABLES,
    6, nullptr, 0};

// H1 scenario: H1 30, H2 = H1 + 1 held, H3 = H1 + 20 -> P4 Demand (H3 >= 40), FF
// target (40-30)/(50-30) = 50 % > 30 %, H2 - H1 = 1 < 2.
static void setupH1(Fixture& f) {
    f.setOk(HH_SENSOR_H1, 30.0f);
    f.setOk(HH_SENSOR_H2, 31.0f);
    f.setOk(HH_SENSOR_H3, 50.0f);
}

static void test_diag_ready_on_real_schema() {
    Fixture f;
    TEST_ASSERT_TRUE(f.rt.ready());
    TEST_ASSERT_TRUE(f.rt.diagReady());
    TEST_ASSERT_EQUAL_UINT32(0, f.beginDiagMissingCount);
    TEST_ASSERT_NOT_EQUAL(DIAG_MISSING_SRC, DIAG_SOURCE);
}

static void test_h1_no_flow_end_to_end() {
    Fixture f;
    f.state.diag.warningMask = 0xFF000100u;   // other owners' bits (8+) must be preserved
    setupH1(f);
    f.start();
    f.runTo(61000);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_P4));
    f.runTo(200000);
    TEST_ASSERT_TRUE(f.status.k1Known);
    TEST_ASSERT_TRUE(f.status.k1PosPct > 30.0f);
    // K1 known at 133 s -> the check tracks from there; 5 min later it raises.
    f.runTo(432000);
    TEST_ASSERT_EQUAL_HEX32(0xFF000100u, f.state.diag.warningMask);
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_DIAG_WARNING));
    f.runTo(433000);
    TEST_ASSERT_EQUAL_HEX32(0xFF000101u, f.state.diag.warningMask);
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_DIAG_WARNING, warnSrc(HH_WARN_H1, true)));
    TEST_ASSERT_EQUAL_UINT16(EVENT_SOURCE_PROJECT_BASE + 0x40 + 1, warnSrc(HH_WARN_H1, true));
    for (size_t i = 0; i < f.sink.count(); ++i) {
        if (f.sink.at(i).type == HH_EVENT_DIAG_WARNING) {
            TEST_ASSERT_EQUAL_FLOAT(0.0f, f.sink.at(i).value);
            TEST_ASSERT_EQUAL_FLOAT(1.0f, f.sink.at(i).aux);
            TEST_ASSERT_EQUAL_INT(static_cast<int>(EventReason::Logic), static_cast<int>(f.sink.at(i).reason));
        }
    }
    // Held: no repeated raise.
    f.runTo(500000);
    TEST_ASSERT_EQUAL_UINT32(1, f.countType(HH_EVENT_DIAG_WARNING));
    // H2 rises (H2 - H1 = 5 >= 2) -> cleared on the next tick, other bits intact.
    f.setOk(HH_SENSOR_H2, 35.0f);
    f.runTo(501000);
    TEST_ASSERT_EQUAL_HEX32(0xFF000100u, f.state.diag.warningMask);
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_DIAG_WARNING, warnSrc(HH_WARN_H1, false)));
    TEST_ASSERT_EQUAL_UINT32(2, f.countType(HH_EVENT_DIAG_WARNING));
    TEST_ASSERT_NOT_EQUAL(warnSrc(HH_WARN_H1, true), warnSrc(HH_WARN_H1, false));
    // The diag sources never collide with the other home-heating project sources.
    TEST_ASSERT_TRUE(warnSrc(HH_WARN_H1, false) > HH_EVENT_SOURCE_NO_NEED_BASE + 1);
    TEST_ASSERT_TRUE(warnSrc(7, true) < HH_EVENT_SOURCE_STEP_BASE);
}

static void test_h1_disabled_never_raises() {
    Fixture f;
    f.setSetting(HH_KEY_H1_EN, 0.0f);
    setupH1(f);
    f.start();
    f.runTo(600000);
    TEST_ASSERT_EQUAL_HEX32(0, f.state.diag.warningMask);
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_DIAG_WARNING));
}

static void assertSameControl(const Fixture& a, const Fixture& b) {
    TEST_ASSERT_EQUAL(a.status.ready, b.status.ready);
    TEST_ASSERT_EQUAL(a.status.p4On, b.status.p4On);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(a.status.p4Reason), static_cast<int>(b.status.p4Reason));
    TEST_ASSERT_EQUAL(a.status.k2Bypass, b.status.k2Bypass);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(a.status.k1Mode), static_cast<int>(b.status.k1Mode));
    TEST_ASSERT_EQUAL(a.status.k1Known, b.status.k1Known);
    TEST_ASSERT_EQUAL_FLOAT(a.status.k1PosPct, b.status.k1PosPct);
    TEST_ASSERT_EQUAL(a.status.noNeed, b.status.noNeed);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(a.status.fail), static_cast<int>(b.status.fail));
    TEST_ASSERT_EQUAL_HEX32(a.status.alarmMask, b.status.alarmMask);
    TEST_ASSERT_EQUAL_HEX32(a.state.alarms.activeMask, b.state.alarms.activeMask);
    TEST_ASSERT_EQUAL(a.relays.actual(HH_RELAY_P4), b.relays.actual(HH_RELAY_P4));
    TEST_ASSERT_EQUAL(a.relays.actual(HH_RELAY_K2), b.relays.actual(HH_RELAY_K2));
    TEST_ASSERT_EQUAL(a.k1.busy(), b.k1.busy());
}

// D2: a missing diag table only disables the diagnostics; control is identical.
static void test_diag_table_missing_keeps_control_ready_and_identical() {
    Fixture on;                    // diag present -> H1 raises during the run
    Fixture none(SCHEMA_NO_DIAG);  // 5 tables: no diag keys
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HomeHeatingRuntimeStatus::Ok), static_cast<int>(none.runtimeStatus));
    TEST_ASSERT_TRUE(none.rt.ready());
    TEST_ASSERT_FALSE(none.rt.diagReady());
    TEST_ASSERT_EQUAL_UINT32(0, none.beginDiagCount);          // control keys all resolved
    TEST_ASSERT_EQUAL_UINT32(1, none.beginDiagMissingCount);   // one DIAG_BASE + 33
    setupH1(on);
    setupH1(none);
    on.start();
    none.start();
    assertSameControl(on, none);
    uint32_t seen = 0;
    for (uint64_t t = 1000; t <= 480000; t += 1000) {
        if (t == 450000) {
            on.setOk(HH_SENSOR_H3, 60.0f);
            none.setOk(HH_SENSOR_H3, 60.0f);
        }
        on.runTo(t);
        none.runTo(t);
        seen |= on.state.diag.warningMask;
        assertSameControl(on, none);
        TEST_ASSERT_EQUAL_HEX32(0, none.state.diag.warningMask);
        TEST_ASSERT_TRUE(none.status.ready);
    }
    TEST_ASSERT_TRUE_MESSAGE((seen & (1u << HH_WARN_H1)) != 0, "the diag run must actually raise H1");
    TEST_ASSERT_EQUAL_UINT32(0, none.countType(HH_EVENT_DIAG_WARNING));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::Unavailable), static_cast<int>(none.status.step.block));
}

static void test_diag_key_mistyped_keeps_control_ready() {
    Fixture f(SCHEMA_MISTYPED_DIAG);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HomeHeatingRuntimeStatus::Ok), static_cast<int>(f.runtimeStatus));
    TEST_ASSERT_TRUE(f.rt.ready());
    TEST_ASSERT_FALSE(f.rt.diagReady());
    TEST_ASSERT_EQUAL_UINT32(1, f.beginDiagMissingCount);
    f.state.diag.warningMask = 0x00000300u;   // bit 8 foreign, bit 9 foreign
    setupH1(f);
    f.start();
    f.runTo(480000);
    TEST_ASSERT_TRUE(f.status.ready);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_P4));
    TEST_ASSERT_EQUAL_HEX32(0x00000300u, f.state.diag.warningMask);
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_DIAG_WARNING));
}

static void test_pulse_counter_accumulates_without_local_time() {
    Fixture f;   // default: H3 70 -> boot recal, then FF OPEN to 25 %
    f.start();
    TEST_ASSERT_EQUAL_UINT32(0, f.status.pulsesToday);   // requested at 0, powered at 0.1 s
    f.runTo(1000);
    TEST_ASSERT_EQUAL_UINT32(1, f.status.pulsesToday);   // the boot recal CLOSE
    TEST_ASSERT_FALSE(f.status.pulsesYesterdayValid);
    f.runTo(132000);
    TEST_ASSERT_EQUAL_UINT32(1, f.status.pulsesToday);
    f.runTo(135000);
    TEST_ASSERT_EQUAL_UINT32(2, f.status.pulsesToday);   // + the FF OPEN
    f.runTo(260000);
    TEST_ASSERT_EQUAL_UINT32(2, f.status.pulsesToday);
    TEST_ASSERT_FALSE(f.status.pulsesYesterdayValid);
    TEST_ASSERT_EQUAL_UINT32(0, f.status.pulsesYesterday);
}

static void test_pulse_counter_local_day_rollover() {
    Fixture f;
    f.start();
    f.runTo(135000);
    TEST_ASSERT_EQUAL_UINT32(2, f.status.pulsesToday);
    f.useLocal = true;
    f.local = LocalTimeInfo{true, 20000u, 1439u};   // first valid day: no rollover (A9)
    f.runTo(136000);
    TEST_ASSERT_EQUAL_UINT32(2, f.status.pulsesToday);
    TEST_ASSERT_FALSE(f.status.pulsesYesterdayValid);
    f.local = LocalTimeInfo{true, 20001u, 0u};      // day + 1
    f.runTo(137000);
    TEST_ASSERT_TRUE(f.status.pulsesYesterdayValid);
    TEST_ASSERT_EQUAL_UINT32(2, f.status.pulsesYesterday);
    TEST_ASSERT_EQUAL_UINT32(0, f.status.pulsesToday);
    f.local = NO_LOCAL_TIME;                        // time lost: keep accumulating, no rollover
    f.runTo(140000);
    TEST_ASSERT_TRUE(f.status.pulsesYesterdayValid);
    TEST_ASSERT_EQUAL_UINT32(2, f.status.pulsesYesterday);
}

static void test_last_pulse_ff_only_recal_and_position_excluded() {
    Fixture f;
    f.start();
    TEST_ASSERT_EQUAL_INT8(0, f.status.lastPulseDir);    // the boot recal is excluded
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.status.lastPulseS);
    f.runTo(132000);
    TEST_ASSERT_EQUAL_INT8(0, f.status.lastPulseDir);
    f.runTo(133000);                                     // FF OPEN 30 s (25 % of 120 s)
    TEST_ASSERT_TRUE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT8(1, f.status.lastPulseDir);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, f.status.lastPulseS);
    f.runTo(170000);
    TEST_ASSERT_FALSE(f.k1.busy());
    // Heating off -> K1 Closed: a Control CLOSE of kind Position, not a tuning move.
    const uint32_t before = f.status.pulsesToday;
    f.setSetting(HH_KEY_HEATING_ENABLED, 0.0f);
    f.runTo(172000);
    TEST_ASSERT_TRUE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Close), static_cast<int>(f.k1.direction()));
    TEST_ASSERT_EQUAL_INT8(1, f.status.lastPulseDir);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, f.status.lastPulseS);
    TEST_ASSERT_EQUAL_UINT32(before + 1, f.status.pulsesToday);   // still counted as a motor run
}

static void test_h2_error_validity() {
    Fixture f;
    f.start();
    TEST_ASSERT_TRUE(f.status.h2ErrValid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.status.h2ErrC);       // H2 40, set 40
    f.setOk(HH_SENSOR_H2, 39.6f);
    f.runTo(1000);
    TEST_ASSERT_TRUE(f.status.h2ErrValid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.4f, f.status.h2ErrC);
    f.setSetting(HH_KEY_H2_SET, 38.0f);
    f.runTo(2000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.6f, f.status.h2ErrC);
    f.setSensor(HH_SENSOR_H2, SensorState::Fault);
    f.runTo(3000);
    TEST_ASSERT_FALSE(f.status.h2ErrValid);
    f.setOk(HH_SENSOR_H2, NAN);                           // Ok + NaN is sanitized to Fault
    f.runTo(4000);
    TEST_ASSERT_FALSE(f.status.h2ErrValid);
    f.setOk(HH_SENSOR_H2, 40.0f);
    f.runTo(5000);
    TEST_ASSERT_TRUE(f.status.h2ErrValid);
    f.setSetting(HH_KEY_HEATING_ENABLED, 0.0f);
    f.runTo(6000);
    TEST_ASSERT_FALSE(f.status.h2ErrValid);
}

static void test_not_ready_status_new_fields_default() {
    Fixture f(TEST_SCHEMA_A_V1);
    f.state.diag.warningMask = 0xFFFFFFFFu;
    f.start();
    f.runTo(5000);
    TEST_ASSERT_FALSE(f.status.ready);
    TEST_ASSERT_FALSE(f.status.h2ErrValid);
    TEST_ASSERT_EQUAL_INT8(0, f.status.lastPulseDir);
    TEST_ASSERT_EQUAL_UINT32(0, f.status.pulsesToday);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::Unavailable), static_cast<int>(f.status.step.block));
    TEST_ASSERT_FALSE(f.status.step.running);
    // Only the owned bits 0..7 are cleared (they were never raised by us: no events).
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF00u, f.state.diag.warningMask);
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_DIAG_WARNING));
}

// ---- stage 09 phase 9: step test integration, command hook, A12 carry-over ----------

constexpr uint16_t STEP_SRC_START = HH_EVENT_SOURCE_STEP_BASE + 0;
constexpr uint16_t STEP_SRC_RESULT = HH_EVENT_SOURCE_STEP_BASE + 1;
constexpr uint16_t STEP_SRC_ABORT = HH_EVENT_SOURCE_STEP_BASE + 2;
constexpr uint64_t STEADY_READY_MS = 330000;   // > 5.5 min: 11 history samples, K1 at 25 % and idle

static CommandStatus stepCmd(Fixture& f, uint8_t op) {
    Command cmd = makeProjectCommand(op, EventReason::Web, 1);
    return HomeHeatingRuntime::commandHook(cmd, f.now, &f.rt);
}

static int blockOf(const Fixture& f) { return static_cast<int>(f.status.step.block); }

// Steady plant: H1 30, H2 on the 40 degC setpoint, H3 70 constant (Fixture defaults).
static void runSteady(Fixture& f) {
    f.start();
    f.runTo(STEADY_READY_MS);
}

// Steady plant + a started test: START accepted at 330 s, the OPEN pulse issued at 331 s.
static void runStarted(Fixture& f) {
    runSteady(f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::None), blockOf(f));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Ok), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
    f.sink.clear();
    f.runTo(STEADY_READY_MS + 1000);
    TEST_ASSERT_TRUE(f.status.step.running);
}

static const RecordingEventSink::Record* findEvent(const Fixture& f, uint16_t type) {
    for (size_t i = 0; i < f.sink.count(); ++i) {
        if (f.sink.at(i).type == type) {
            return &f.sink.at(i);
        }
    }
    return nullptr;
}

static size_t stepAbortCount(const Fixture& f, StepAbort a) {
    size_t n = 0;
    for (size_t i = 0; i < f.sink.count(); ++i) {
        const auto& r = f.sink.at(i);
        if (r.type == HH_EVENT_STEP_ABORT && r.source == STEP_SRC_ABORT &&
            r.value == static_cast<float>(static_cast<uint8_t>(a))) {
            ++n;
        }
    }
    return n;
}

static void test_step_command_validation() {
    Fixture f;
    Command other = makeSetNumber(0, 1.0f, EventReason::Web, 1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::InvalidCommand),
        static_cast<int>(HomeHeatingRuntime::commandHook(other, 0, &f.rt)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::InvalidCommand), static_cast<int>(stepCmd(f, 3)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::InvalidCommand),
        static_cast<int>(HomeHeatingRuntime::commandHook(other, 0, nullptr)));
    f.start();
    f.runTo(200000);   // < 5 min of history
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::History), blockOf(f));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Rejected), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Unchanged), static_cast<int>(stepCmd(f, HH_CMD_STEP_CANCEL)));
    f.runTo(210000);
    TEST_ASSERT_FALSE(f.status.step.running);   // the rejected start was not latched
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_STEP_START));
}

static void test_step_start_issues_one_control_open_pulse() {
    Fixture f;
    runSteady(f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::None), blockOf(f));
    TEST_ASSERT_FALSE(f.k1.busy());
    const uint32_t runsBefore = f.status.pulsesToday;
    f.sink.clear();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Ok), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
    // A second START while pending -> Rejected (D15).
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Rejected), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
    f.runTo(STEADY_READY_MS + 1000);
    TEST_ASSERT_TRUE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::Control), static_cast<int>(f.k1.owner()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Open), static_cast<int>(f.k1.direction()));
    TEST_ASSERT_EQUAL_UINT32(1, f.countType(HH_EVENT_STEP_START));
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_STEP_START, STEP_SRC_START));
    const auto* ev = findEvent(f, HH_EVENT_STEP_START);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, ev->value);   // pulse s
    TEST_ASSERT_EQUAL_FLOAT(40.0f, ev->aux);     // H2 at start
    TEST_ASSERT_TRUE(f.status.step.running);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::Running), blockOf(f));
    TEST_ASSERT_EQUAL_UINT32(10, f.status.step.pulseS);
    TEST_ASSERT_EQUAL_UINT32(0, f.status.step.elapsedS);
    // D11 / A8: the step pulse is a tuning move -> last pulse +1 / 10 s.
    TEST_ASSERT_EQUAL_INT8(1, f.status.lastPulseDir);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, f.status.lastPulseS);
    // A second START while running -> Rejected.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Rejected), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
    // Exactly 10 000 ms of OPEN.
    f.runTo(STEADY_READY_MS + 10000);
    TEST_ASSERT_TRUE(f.k1.busy());
    f.runTo(STEADY_READY_MS + 13000);
    TEST_ASSERT_FALSE(f.k1.busy());
    TEST_ASSERT_EQUAL_UINT32(12, f.status.step.elapsedS);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f + 100.0f * 10.0f / 120.0f, f.status.k1PosPct);
    TEST_ASSERT_EQUAL_UINT32(runsBefore + 1, f.status.pulsesToday);
    TEST_ASSERT_EQUAL_UINT32(1, f.countType(HH_EVENT_STEP_START));
}

static void test_step_hold_suppresses_control_pulses_relays_unchanged() {
    Fixture f;
    Fixture ref;                  // the same plant without a test
    runStarted(f);
    runSteady(ref);
    ref.runTo(STEADY_READY_MS + 1000);
    // The plant's response: H2 3 degC below the setpoint (feedback would open ~6 s).
    f.setOk(HH_SENSOR_H2, 37.0f);
    ref.setOk(HH_SENSOR_H2, 37.0f);
    f.runTo(STEADY_READY_MS + 13000);   // the step pulse has ended
    ref.runTo(STEADY_READY_MS + 13000);
    TEST_ASSERT_FALSE(f.k1.busy());
    bool refMoved = false;
    for (uint64_t t = STEADY_READY_MS + 14000; t <= STEADY_READY_MS + 1000 + 3 * 30000; t += 1000) {
        f.runTo(t);
        ref.runTo(t);
        TEST_ASSERT_TRUE(f.status.step.running);
        TEST_ASSERT_FALSE(f.k1.busy());                 // no FF / feedback pulse while held
        TEST_ASSERT_EQUAL_INT8(1, f.status.lastPulseDir);  // no FF/feedback cmdKind recorded
        TEST_ASSERT_EQUAL_FLOAT(10.0f, f.status.lastPulseS);
        TEST_ASSERT_EQUAL(ref.relays.requested(HH_RELAY_P4), f.relays.requested(HH_RELAY_P4));
        TEST_ASSERT_EQUAL(ref.relays.requested(HH_RELAY_K2), f.relays.requested(HH_RELAY_K2));
        refMoved = refMoved || ref.k1.busy();
    }
    TEST_ASSERT_TRUE(refMoved);   // the reference run did react (feedback OPEN)
    TEST_ASSERT_TRUE(f.relays.requested(HH_RELAY_P4));
}

static void test_step_result_event_suggestion_and_resume() {
    Fixture f;
    runStarted(f);                                  // t0 = 331 s, H2 start 40
    f.runTo(STEADY_READY_MS + 22000);               // el 21 s
    TEST_ASSERT_FALSE(f.status.step.deadSeen);
    f.setOk(HH_SENSOR_H2, 42.5f);                   // +2.5 degC at el 22 s
    f.runTo(STEADY_READY_MS + 23000);
    TEST_ASSERT_TRUE(f.status.step.deadSeen);
    TEST_ASSERT_EQUAL_FLOAT(22.0f, f.status.step.deadTimeS);
    f.runTo(STEADY_READY_MS + 1000 + 22000 + 119000);   // flat 119 s: not settled yet
    TEST_ASSERT_TRUE(f.status.step.running);
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_STEP_RESULT));
    f.runTo(STEADY_READY_MS + 1000 + 22000 + 120000);   // flat 120 s -> settled
    TEST_ASSERT_FALSE(f.status.step.running);
    TEST_ASSERT_EQUAL_UINT32(1, f.countEvents(HH_EVENT_STEP_RESULT, STEP_SRC_RESULT));
    const auto* ev = findEvent(f, HH_EVENT_STEP_RESULT);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_FLOAT(22.0f, ev->value);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.25f, ev->aux);
    const StepTestResult& r = f.status.step.last;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Result), static_cast<int>(r.outcome));
    TEST_ASSERT_TRUE(r.suggest.valid);
    TEST_ASSERT_EQUAL_UINT32(33, r.suggest.periodS);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, r.suggest.gain);
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_STEP_ABORT));
    TEST_ASSERT_FALSE(f.k1.busy());
    // Resume (D12): the next tick re-applies FF (25 % target vs ~33 % estimate -> CLOSE).
    f.runTo(STEADY_READY_MS + 1000 + 22000 + 121000);
    TEST_ASSERT_TRUE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::Control), static_cast<int>(f.k1.owner()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Close), static_cast<int>(f.k1.direction()));
    TEST_ASSERT_EQUAL_INT8(-1, f.status.lastPulseDir);   // FF kind
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, f.status.lastPulseS);  // 8.33 % of 120 s
    // The outcome is kept until the next test ends.
    f.runTo(STEADY_READY_MS + 200000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Result), static_cast<int>(f.status.step.last.outcome));
}

static void test_step_cancel_aborts_and_releases_hold() {
    Fixture f;
    runStarted(f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Ok), static_cast<int>(stepCmd(f, HH_CMD_STEP_CANCEL)));
    f.runTo(STEADY_READY_MS + 2000);
    TEST_ASSERT_FALSE(f.status.step.running);
    TEST_ASSERT_EQUAL_UINT32(1, stepAbortCount(f, StepAbort::Cancel));
    const auto* ev = findEvent(f, HH_EVENT_STEP_ABORT);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, ev->aux);   // elapsed s
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Aborted), static_cast<int>(f.status.step.last.outcome));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepAbort::Cancel), static_cast<int>(f.status.step.last.abort));
    // Idle now: CANCEL -> Unchanged.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Unchanged), static_cast<int>(stepCmd(f, HH_CMD_STEP_CANCEL)));
    // Hold released: the re-armed FF waits for the step pulse to end, then CLOSEs back to 25 %.
    f.runTo(STEADY_READY_MS + 14000);
    TEST_ASSERT_EQUAL_INT8(-1, f.status.lastPulseDir);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Close), static_cast<int>(f.k1.direction()));
}

static void test_step_abort_heating_off() {
    Fixture f;
    runStarted(f);
    f.setSetting(HH_KEY_HEATING_ENABLED, 0.0f);
    f.runTo(STEADY_READY_MS + 2000);
    TEST_ASSERT_FALSE(f.status.step.running);
    TEST_ASSERT_EQUAL_UINT32(1, stepAbortCount(f, StepAbort::HeatingOff));
    TEST_ASSERT_EQUAL_UINT32(1, f.countType(HH_EVENT_STEP_ABORT));
    // Control takes K1 back: the heating-off recalibration starts (a CLOSE replaces the step pulse).
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Mode::Recalibrating), static_cast<int>(f.status.k1Mode));
    f.runTo(STEADY_READY_MS + 14000);
    TEST_ASSERT_TRUE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Close), static_cast<int>(f.k1.direction()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::HeatingOff), blockOf(f));
}

static void test_step_abort_h3_fault() {
    Fixture f;
    runStarted(f);
    f.setSensor(HH_SENSOR_H3, SensorState::Fault);
    f.runTo(STEADY_READY_MS + 2000);
    TEST_ASSERT_FALSE(f.status.step.running);
    TEST_ASSERT_EQUAL_UINT32(1, stepAbortCount(f, StepAbort::Sensors));
    TEST_ASSERT_EQUAL_UINT32(1, f.countType(HH_EVENT_STEP_ABORT));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::Sensors), blockOf(f));
}

static void test_step_abort_ota_stops_k1() {
    Fixture f;
    runStarted(f);
    f.runTo(STEADY_READY_MS + 3000);
    TEST_ASSERT_TRUE(f.k1.busy());
    f.setOta(true);   // HwRuntime cancels the running pulse
    f.runTo(STEADY_READY_MS + 4000);
    TEST_ASSERT_FALSE(f.status.step.running);
    TEST_ASSERT_EQUAL_UINT32(1, stepAbortCount(f, StepAbort::Ota));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::Ota), blockOf(f));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Rejected), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
    f.runTo(STEADY_READY_MS + 60000);
    TEST_ASSERT_FALSE(f.k1BusyWhileOta);   // OTA inhibit still stops K1 (no restart)
    TEST_ASSERT_FALSE(f.k1.busy());
}

static void test_step_blocked_while_fail_safe_active() {
    Fixture f;
    runSteady(f);
    f.setSensor(HH_SENSOR_H1, SensorState::Fault);   // H1 lost -> fail-safe mode
    f.runTo(STEADY_READY_MS + 2000);
    TEST_ASSERT_TRUE(f.status.fail != FailMode::None);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::Sensors), blockOf(f));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Rejected), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
    f.runTo(STEADY_READY_MS + 5000);
    TEST_ASSERT_FALSE(f.status.step.running);
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_STEP_START));
}

// A12 / 08-5: anti-seize owns K1 -> no Control pulse replaces the stroke.
static void test_anti_seize_owns_k1_no_control_pulse() {
    Fixture f;
    runSteady(f);
    TEST_ASSERT_TRUE(f.relays.actual(HH_RELAY_P4));
    TEST_ASSERT_FALSE(f.k1.busy());
    const float posBefore = f.status.k1PosPct;         // 25 %
    const uint32_t runsBefore = f.status.pulsesToday;
    const int8_t lastDir = f.status.lastPulseDir;
    TEST_ASSERT_TRUE(f.k1.requestPulse(K1Direction::Open, 60000, f.now, K1Owner::AntiSeize));
    f.setOk(HH_SENSOR_H2, 34.0f);                      // far below the setpoint: feedback wants OPEN
    f.runTo(STEADY_READY_MS + 1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::AntiSeize), blockOf(f));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Rejected), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
    f.runTo(STEADY_READY_MS + 3000);                   // idle rest CLOSE -> direction change + dead time
    TEST_ASSERT_TRUE(f.k1.running());
    TEST_ASSERT_EQUAL_UINT32(runsBefore + 1, f.status.pulsesToday);   // the stroke is a motor run
    uint64_t t = f.now;
    while (f.k1.busy() && f.k1.owner() == K1Owner::AntiSeize) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Open), static_cast<int>(f.k1.direction()));
        TEST_ASSERT_EQUAL_INT8(lastDir, f.status.lastPulseDir);
        t += 1000;
        f.runTo(t);
    }
    // >= 2 k1Periods (30 s) under anti-seize ownership, then the stroke ended by itself.
    TEST_ASSERT_TRUE(t >= STEADY_READY_MS + 60000);
    TEST_ASSERT_FALSE(f.status.step.running);
    // D6: the stroke's motion (60 s of 120 s = 50 %) is in the estimate.
    TEST_ASSERT_TRUE(f.status.k1Known);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, posBefore + 50.0f, f.status.k1PosPct);
    TEST_ASSERT_EQUAL_UINT32(runsBefore + 1, f.status.pulsesToday);   // only the stroke so far
}

static void test_step_reboot_clears_test_and_result() {
    Fixture f;
    runStarted(f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Ok), static_cast<int>(stepCmd(f, HH_CMD_STEP_CANCEL)));
    f.runTo(STEADY_READY_MS + 2000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Aborted), static_cast<int>(f.status.step.last.outcome));
    TEST_ASSERT_TRUE(f.status.pulsesToday > 0);
    f.runTo(STEADY_READY_MS + 20000);
    f.sink.clear();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HomeHeatingRuntimeStatus::Ok), static_cast<int>(f.rt.begin(f.now)));
    f.runTo(STEADY_READY_MS + 21000);
    TEST_ASSERT_FALSE(f.status.step.running);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::None), static_cast<int>(f.status.step.last.outcome));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::K1Mode), blockOf(f));   // boot recal again
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_STEP_START));
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_STEP_ABORT));
    // review-8: the counter baseline is seeded from the driver, earlier runs are not "today".
    TEST_ASSERT_EQUAL_UINT32(0, f.status.pulsesToday);
    TEST_ASSERT_EQUAL_INT8(0, f.status.lastPulseDir);
}

// review-8: a feedback CLOSE sets lastPulseDir -1 with the commanded seconds.
static void test_last_pulse_feedback_close() {
    Fixture f;
    f.start();
    f.runTo(170000);   // boot recal + FF OPEN to 25 % done
    TEST_ASSERT_FALSE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT8(1, f.status.lastPulseDir);
    f.setOk(HH_SENSOR_H2, 42.0f);   // 2 degC above the setpoint -> feedback CLOSE gain 2 * 2 = 4 s
    bool closed = false;
    for (uint64_t t = 171000; t <= 210000 && !closed; t += 1000) {
        f.runTo(t);
        closed = f.status.lastPulseDir == -1;
    }
    TEST_ASSERT_TRUE(closed);
    TEST_ASSERT_TRUE(f.k1.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Owner::Control), static_cast<int>(f.k1.owner()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(K1Direction::Close), static_cast<int>(f.k1.direction()));
    TEST_ASSERT_EQUAL_FLOAT(4.0f, f.status.lastPulseS);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, f.status.k1FfPct);   // FF unchanged: it is a feedback move
}

// ---- stage 09 phase 10: review-9 carry-ins ------------------------------------------

static void test_step_cancel_clears_pending_start() {
    Fixture f;
    runSteady(f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::None), blockOf(f));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Ok), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
    // Cancel before the latched start ran -> Ok, and the test never starts.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Ok), static_cast<int>(stepCmd(f, HH_CMD_STEP_CANCEL)));
    f.sink.clear();
    f.runTo(STEADY_READY_MS + 5000);
    TEST_ASSERT_FALSE(f.status.step.running);
    TEST_ASSERT_FALSE(f.k1.busy());
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_STEP_START));
    TEST_ASSERT_EQUAL_UINT32(0, f.countType(HH_EVENT_STEP_ABORT));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::None), static_cast<int>(f.status.step.last.outcome));
    // Nothing pending any more: a second Cancel is Unchanged, a new Start is Ok again.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Unchanged), static_cast<int>(stepCmd(f, HH_CMD_STEP_CANCEL)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Ok), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
}

static void test_step_start_before_first_tick_rejected() {
    Fixture f;   // begin() done, no tick yet
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::Unavailable), blockOf(f));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Rejected), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
    // A re-begin after a startable tick: the stale None block must not accept a start.
    runSteady(f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::None), blockOf(f));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HomeHeatingRuntimeStatus::Ok), static_cast<int>(f.rt.begin(f.now)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepBlock::Unavailable), blockOf(f));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Rejected), static_cast<int>(stepCmd(f, HH_CMD_STEP_START)));
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
    RUN_TEST(test_diag_ready_on_real_schema);
    RUN_TEST(test_h1_no_flow_end_to_end);
    RUN_TEST(test_h1_disabled_never_raises);
    RUN_TEST(test_diag_table_missing_keeps_control_ready_and_identical);
    RUN_TEST(test_diag_key_mistyped_keeps_control_ready);
    RUN_TEST(test_pulse_counter_accumulates_without_local_time);
    RUN_TEST(test_pulse_counter_local_day_rollover);
    RUN_TEST(test_last_pulse_ff_only_recal_and_position_excluded);
    RUN_TEST(test_h2_error_validity);
    RUN_TEST(test_not_ready_status_new_fields_default);
    RUN_TEST(test_step_command_validation);
    RUN_TEST(test_step_start_issues_one_control_open_pulse);
    RUN_TEST(test_step_hold_suppresses_control_pulses_relays_unchanged);
    RUN_TEST(test_step_result_event_suggestion_and_resume);
    RUN_TEST(test_step_cancel_aborts_and_releases_hold);
    RUN_TEST(test_step_abort_heating_off);
    RUN_TEST(test_step_abort_h3_fault);
    RUN_TEST(test_step_abort_ota_stops_k1);
    RUN_TEST(test_step_blocked_while_fail_safe_active);
    RUN_TEST(test_anti_seize_owns_k1_no_control_pulse);
    RUN_TEST(test_step_reboot_clears_test_and_result);
    RUN_TEST(test_last_pulse_feedback_close);
    RUN_TEST(test_step_cancel_clears_pending_start);
    RUN_TEST(test_step_start_before_first_tick_rejected);
    return UNITY_END();
}
