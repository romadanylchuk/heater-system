#include "HomeHeatingRuntime.h"
#include <math.h>
#include <string.h>
#include "HomeHeatingAlarms.h"
#include "HomeHeatingEvents.h"

namespace {

const char* const RUNTIME_KEYS[] = {
    HH_KEY_H2_SET, HH_KEY_P4_OFF_DELAY, HH_KEY_K1_TRAVEL, HH_KEY_K1_PERIOD, HH_KEY_K1_DEADBAND, HH_KEY_K1_GAIN,
    HH_KEY_K1_MAX_PULSE, HH_KEY_K1_MIN_PULSE, HH_KEY_K1_RESYNC, HH_KEY_K1_SMALL_DIFF, HH_KEY_K1_FAIL_POS,
    HH_KEY_K1_FF_STEP, HH_KEY_K2_DELTA, HH_KEY_K2_DELTA_HYST, HH_KEY_K2_H3_MIN, HH_KEY_K2_H3_MIN_HYST,
    HH_KEY_K2_H4_MAX, HH_KEY_K2_H4_MAX_HYST, HH_KEY_HEATING_ENABLED,
};
static_assert(sizeof(RUNTIME_KEYS) / sizeof(RUNTIME_KEYS[0]) == HH_RUNTIME_KEY_COUNT,
    "RUNTIME_KEYS must list exactly HH_RUNTIME_KEY_COUNT keys, in HhRuntimeKey order");

const char* const DIAG_KEYS[] = {
    HH_KEY_H1_EN, HH_KEY_H1_MIN_ON, HH_KEY_H1_K1_MIN, HH_KEY_H1_DELTA, HH_KEY_H1_MIN_DIFF, HH_KEY_K1_STEP_PULSE,
};
static_assert(sizeof(DIAG_KEYS) / sizeof(DIAG_KEYS[0]) == HH_DIAG_KEY_COUNT,
    "DIAG_KEYS must list exactly HH_DIAG_KEY_COUNT keys, in HhDiagKey order");
static_assert(HH_DIAG_KEY_COUNT == HOME_HEATING_DIAG_SETTING_COUNT, "one HhDiagKey per C10 row");

size_t at(HhRuntimeKey k) { return static_cast<size_t>(k); }
size_t at(HhDiagKey k) { return static_cast<size_t>(k); }

// Expected descriptor type of a diag key: its C10 table row.
bool expectedDiagType(const char* key, SettingType& out) {
    for (size_t i = 0; i < HOME_HEATING_DIAG_SETTING_COUNT; ++i) {
        if (strcmp(HOME_HEATING_DIAG_SETTINGS[i].key, key) == 0) {
            out = HOME_HEATING_DIAG_SETTINGS[i].type;
            return true;
        }
    }
    return false;
}

// Expected descriptor type: the C2 table row for the controller keys, Bool for heatingEnabled.
bool expectedType(const char* key, SettingType& out) {
    if (strcmp(key, HH_KEY_HEATING_ENABLED) == 0) {
        out = SettingType::Bool;
        return true;
    }
    for (size_t i = 0; i < HOME_HEATING_CONTROL_SETTING_COUNT; ++i) {
        if (strcmp(HOME_HEATING_CONTROL_SETTINGS[i].key, key) == 0) {
            out = HOME_HEATING_CONTROL_SETTINGS[i].type;
            return true;
        }
    }
    return false;
}

uint32_t readU32(const ConfigEngine& c, size_t index) {
    const int32_t v = c.getInt(index);
    return v < 0 ? 0u : static_cast<uint32_t>(v);
}

float clampPct(float v) {
    return v < 0.0f ? 0.0f : (v > 100.0f ? 100.0f : v);
}

}  // namespace

HomeHeatingRuntime::HomeHeatingRuntime(CommonState& state, HomeHeatingStatus& status, ConfigEngine& config,
    EventSink& events, RelayBank& relays, K1Driver& k1, AntiSeizeScheduler& antiSeize)
    : _state(state), _status(status), _config(config), _events(events), _relays(relays), _k1(k1),
      _antiSeize(antiSeize) {}

HomeHeatingRuntimeStatus HomeHeatingRuntime::begin(uint64_t nowMs) {
    _ready = false;
    _diagReady = false;
    for (size_t k = 0; k < HH_RUNTIME_KEY_COUNT; ++k) {
        const int index = _config.indexOf(RUNTIME_KEYS[k]);
        const SettingDescriptor* d = index < 0 ? nullptr : _config.descriptor(static_cast<size_t>(index));
        SettingType want{};
        if (d == nullptr || !expectedType(RUNTIME_KEYS[k], want) || d->type != want) {
            // D20: visible in the field log too, not only on Serial -- once per boot.
            if (!_missingLogged) {
                _events.logEvent(toU16(EventType::DiagnosticWarning),
                    static_cast<uint16_t>(EVENT_SOURCE_DIAG_BASE + HH_DIAG_CODE_SETTING_MISSING),
                    static_cast<float>(k), 0.0f, EventReason::Logic);
                _missingLogged = true;
            }
            return HomeHeatingRuntimeStatus::SettingMissing;
        }
        _idx[k] = static_cast<size_t>(index);
    }
    _controller.reset(nowMs);
    _prevP4On = false;
    _prevK2Bypass = false;
    _prevFail = FailMode::None;
    _prevNoNeed = false;
    _prevMask = 0;
    _ready = true;

    // Stage 09 (C15, D2): the diag keys are resolved AFTER, and independently of,
    // the control keys. A failure only disables the diagnostics and the step test;
    // it never makes the runtime not-ready and never touches P4/K1/K2/no-need/alarms.
    _diagReady = resolveDiagKeys();
    _diag.reset();
    _step.reset();
    _pulses.reset(_k1.runStarts());   // review-8: a re-begin must not dump earlier runs into "today"
    _startPending = false;
    _cancelPending = false;
    _lastPulseDir = 0;
    _lastPulseMs = 0;
    // review-9: a START before the first tick is Rejected (the status may still hold
    // a value-initialised None or a previous run's block); the first tick overwrites it.
    _status.step.block = StepBlock::Unavailable;
    return HomeHeatingRuntimeStatus::Ok;
}

bool HomeHeatingRuntime::resolveDiagKeys() {
    for (size_t k = 0; k < HH_DIAG_KEY_COUNT; ++k) {
        const int index = _config.indexOf(DIAG_KEYS[k]);
        const SettingDescriptor* d = index < 0 ? nullptr : _config.descriptor(static_cast<size_t>(index));
        SettingType want{};
        if (d == nullptr || !expectedDiagType(DIAG_KEYS[k], want) || d->type != want) {
            _events.logEvent(toU16(EventType::DiagnosticWarning),
                static_cast<uint16_t>(EVENT_SOURCE_DIAG_BASE + HH_DIAG_CODE_DIAG_SETTING_MISSING),
                static_cast<float>(k), 0.0f, EventReason::Logic);
            return false;   // _diagIdx may be partly filled; it is meaningless while !_diagReady
        }
        _diagIdx[k] = static_cast<size_t>(index);
    }
    return true;
}

HomeHeatingDiagSettings HomeHeatingRuntime::readDiagSettings(const ConfigEngine& c, const size_t idx[]) {
    HomeHeatingDiagSettings s{};
    s.h1.enabled = c.getBool(idx[at(HhDiagKey::H1En)]);
    s.h1.minOnMin = readU32(c, idx[at(HhDiagKey::H1MinOn)]);
    s.h1.k1MinPct = readU32(c, idx[at(HhDiagKey::H1K1Min)]);
    s.h1.deltaC = c.getNumber(idx[at(HhDiagKey::H1Delta)]);
    s.h1.minDiffC = c.getNumber(idx[at(HhDiagKey::H1MinDiff)]);
    s.k1StepPulseS = readU32(c, idx[at(HhDiagKey::K1StepPulse)]);
    return s;
}

void HomeHeatingRuntime::publishWarnings(uint32_t mask) {
    // D6: the runtime owns diag.warningMask bits 0..7 only; the others are preserved.
    mask &= HH_WARN_OWNED_MASK;
    _state.diag.warningMask = (_state.diag.warningMask & ~HH_WARN_OWNED_MASK) | mask;
    const uint32_t changed = (mask ^ _prevWarn) & HH_WARN_OWNED_MASK;
    for (uint8_t bit = 0; bit < 8; ++bit) {
        const uint32_t m = 1u << bit;
        if ((changed & m) == 0) {
            continue;
        }
        const uint16_t raised = (mask & m) != 0 ? 1u : 0u;
        // D7: a distinct source per check and direction (60 s limiter safe for one
        // raise/clear pair). diag.warningMask stays authoritative over the log.
        logEvent(HH_EVENT_DIAG_WARNING, static_cast<uint16_t>(HH_EVENT_SOURCE_DIAG_BASE + bit * 2u + raised),
            static_cast<float>(bit), static_cast<float>(raised));
    }
    _prevWarn = mask;
}

void HomeHeatingRuntime::tickDiagnostics(const HomeHeatingInputs& in, const HomeHeatingOutputs& out,
        uint64_t nowMs) {
    if (!_diagReady) {
        publishWarnings(0);
        return;
    }
    // D2: inputs only (sanitized sensors, P4 relay ACTUAL, the K1 estimator value);
    // the result is a mask that is published, never fed back into the controller.
    HomeHeatingDiagInputs d{};
    for (uint8_t i = 0; i < HH_SENSOR_COUNT; ++i) {
        d.sensor[i] = in.sensor[i];
    }
    d.p4Actual = in.p4RelayActual;
    d.k1Known = out.k1.known;
    d.k1PosPct = out.k1.posPct;
    publishWarnings(_diag.update(d, readDiagSettings(_config, _diagIdx), nowMs));
}

StepTestOutput HomeHeatingRuntime::tickStepTest(const HomeHeatingInputs& in, const HomeHeatingOutputs& out,
        const HomeHeatingSettings& raw, uint64_t nowMs) {
    const SensorInput& h1 = in.sensor[HH_SENSOR_H1];
    const SensorInput& h2 = in.sensor[HH_SENSOR_H2];   // sanitized: Ok implies finite
    const SensorInput& h3 = in.sensor[HH_SENSOR_H3];
    StepTestInputs si{};
    si.available = _diagReady;
    si.startRequest = _startPending;    // the latches are consumed by this tick (D15)
    si.cancelRequest = _cancelPending;
    _startPending = false;
    _cancelPending = false;
    si.heatingEnabled = raw.heatingEnabled;
    si.p4Requested = out.p4.on;
    si.p4RelayActual = in.p4RelayActual;  // K1StepTest checks the OTA inhibit before P4
    si.inhibited = in.inhibited;
    si.k1AntiSeizeOwned = in.k1AntiSeizeOwned;
    si.k1Busy = in.k1Busy;
    si.k1CmdThisTick = out.k1.cmd.issue;   // D13: never replace or extend a Control move
    si.k1Mode = out.k1.mode;
    si.k1Known = out.k1.known;
    si.k1PosPct = out.k1.posPct;
    si.recalStarted = out.k1.recalStarted;
    si.h1 = classifySensor(h1.state);
    si.h2 = classifySensor(h2.state);
    si.h3 = classifySensor(h3.state);
    si.h2C = h2.tempC;
    si.h3C = h3.tempC;
    si.steady = _diagReady ? stepSteadiness(_diag.history(), h2.tempC, h3.tempC) : StepSteadiness{false, false, false};

    StepTestSettings ss{};
    ss.pulseS = _diagReady ? readDiagSettings(_config, _diagIdx).k1StepPulseS : 0u;
    ss.travelS = guardHomeHeatingSettings(raw).k1TravelS;

    const uint32_t elapsedBeforeS = _step.elapsedS(nowMs);   // for the abort event (idle after update)
    const StepTestOutput so = _step.update(si, ss, nowMs);

    if (so.pulse.issue) {
        // The one OPEN pulse, through the Control owner like every controller move.
        // Start required !k1Busy && !out.k1.cmd.issue, so nothing is replaced.
        if (_k1.requestPulse(so.pulse.dir, so.pulse.ms, nowMs, K1Owner::Control)) {
            _lastPulseDir = 1;               // D11 / A8: the step pulse is a tuning move
            _lastPulseMs = so.pulse.ms;
        } else {
            _cancelPending = true;           // defensive: never measure a pulse that did not run
        }
        logEvent(HH_EVENT_STEP_START, static_cast<uint16_t>(HH_EVENT_SOURCE_STEP_BASE + 0),
            static_cast<float>(_step.pulseS()), h2.tempC);   // aux = H2 at start (= h2Start)
    }
    if (so.ended) {
        const StepTestResult& r = _step.last();
        if (r.outcome == StepOutcome::Aborted) {
            logEvent(HH_EVENT_STEP_ABORT, static_cast<uint16_t>(HH_EVENT_SOURCE_STEP_BASE + 2),
                static_cast<float>(static_cast<uint8_t>(r.abort)), static_cast<float>(elapsedBeforeS));
        } else {
            // deadTimeS > 0 means the dead time was seen; otherwise -1 (review-7).
            const float dead = r.deadTimeS > 0.0f ? r.deadTimeS : -1.0f;
            const float resp = r.outcome == StepOutcome::Result ? r.responseCps : 0.0f;
            logEvent(HH_EVENT_STEP_RESULT, static_cast<uint16_t>(HH_EVENT_SOURCE_STEP_BASE + 1), dead, resp);
        }
    }
    return so;
}

CommandStatus HomeHeatingRuntime::handleCommand(const Command& cmd, uint64_t nowMs) {
    (void)nowMs;
    if (cmd.type != CommandType::Project) {
        return CommandStatus::InvalidCommand;
    }
    const size_t op = cmd.settingIndex;
    if (op == HH_CMD_STEP_START) {
        if (!_ready || !_diagReady) {
            return CommandStatus::Rejected;
        }
        if (_step.running() || _startPending) {
            return CommandStatus::Rejected;   // D15: a second Start while running / pending
        }
        if (_status.step.block != StepBlock::None) {
            return CommandStatus::Rejected;   // validated against the last tick's reason
        }
        _startPending = true;
        return CommandStatus::Ok;
    }
    if (op == HH_CMD_STEP_CANCEL) {
        if (_step.running()) {
            _cancelPending = true;
            return CommandStatus::Ok;
        }
        if (_startPending) {
            _startPending = false;          // review-9: Cancel beats a latched, not yet started Start
            return CommandStatus::Ok;
        }
        return CommandStatus::Unchanged;
    }
    return CommandStatus::InvalidCommand;
}

CommandStatus HomeHeatingRuntime::commandHook(Command& cmd, uint64_t monoMs, void* ctx) {
    if (ctx == nullptr) {
        return CommandStatus::InvalidCommand;
    }
    return static_cast<HomeHeatingRuntime*>(ctx)->handleCommand(cmd, monoMs);
}

HomeHeatingSettings HomeHeatingRuntime::readSettings(const ConfigEngine& c, const size_t idx[]) {
    HomeHeatingSettings s{};
    s.heatingEnabled = c.getBool(idx[at(HhRuntimeKey::HeatingEnabled)]);
    s.h2Set = c.getNumber(idx[at(HhRuntimeKey::H2Set)]);
    s.p4OffDelayMin = readU32(c, idx[at(HhRuntimeKey::P4OffDelay)]);
    s.k1TravelS = readU32(c, idx[at(HhRuntimeKey::K1Travel)]);
    s.k1PeriodS = readU32(c, idx[at(HhRuntimeKey::K1Period)]);
    s.k1Deadband = c.getNumber(idx[at(HhRuntimeKey::K1Deadband)]);
    s.k1Gain = c.getNumber(idx[at(HhRuntimeKey::K1Gain)]);
    s.k1MaxPulseS = readU32(c, idx[at(HhRuntimeKey::K1MaxPulse)]);
    s.k1MinPulseS = c.getNumber(idx[at(HhRuntimeKey::K1MinPulse)]);
    s.k1ResyncPct = readU32(c, idx[at(HhRuntimeKey::K1Resync)]);
    s.k1SmallDiff = c.getNumber(idx[at(HhRuntimeKey::K1SmallDiff)]);
    s.k1FailPosPct = readU32(c, idx[at(HhRuntimeKey::K1FailPos)]);
    s.k1FfStepPct = readU32(c, idx[at(HhRuntimeKey::K1FfStep)]);
    s.k2Delta = c.getNumber(idx[at(HhRuntimeKey::K2Delta)]);
    s.k2DeltaHyst = c.getNumber(idx[at(HhRuntimeKey::K2DeltaHyst)]);
    s.k2H3Min = c.getNumber(idx[at(HhRuntimeKey::K2H3Min)]);
    s.k2H3MinHyst = c.getNumber(idx[at(HhRuntimeKey::K2H3MinHyst)]);
    s.k2H4Max = c.getNumber(idx[at(HhRuntimeKey::K2H4Max)]);
    s.k2H4MaxHyst = c.getNumber(idx[at(HhRuntimeKey::K2H4MaxHyst)]);
    return s;
}

void HomeHeatingRuntime::logEvent(uint16_t type, uint16_t source, float value, float aux) {
    _events.logEvent(type, source, value, aux, EventReason::Logic);
}

void HomeHeatingRuntime::tick(uint64_t nowMs, const LocalTimeInfo& local) {
    // 1. Not ready (a key is missing, D20): outputs to their passive state, alarms untouched.
    if (!_ready) {
        _status = HomeHeatingStatus{};
        _status.ready = false;
        _status.step.block = StepBlock::Unavailable;
        _relays.requestControl(HH_RELAY_P4, false, RelayReason::Control);
        _relays.requestControl(HH_RELAY_K2, false, RelayReason::Control);   // de-energised = BYPASS
        _startPending = false;
        _cancelPending = false;
        publishWarnings(0);   // stage 09: no diagnostics while not ready (owned bits only)
        return;
    }

    // 2. Raw settings (the controller applies the guard itself).
    const HomeHeatingSettings raw = readSettings(_config, _idx);

    // 3. Inputs.
    HomeHeatingInputs in{};
    for (uint8_t i = 0; i < HH_SENSOR_COUNT; ++i) {
        if (i < _state.sensors.count) {
            const LogicalSensorStatus& s = _state.sensors.sensor[i];
            // An Ok sensor with a non-finite reading is not working -> Fault (the ONE
            // place; the controller never sees Ok+NaN/Inf).
            const bool bad = s.state == SensorState::Ok && !isfinite(s.tempC);
            in.sensor[i] = bad ? SensorInput{SensorState::Fault, 0.0f, s.missing}
                               : SensorInput{s.state, s.tempC, s.missing};
        } else {
            in.sensor[i] = SensorInput{SensorState::Unassigned, 0.0f, false};  // not provided = not working
        }
    }
    in.p4RelayActual = _relays.actual(HH_RELAY_P4);
    in.k2BypassActual = hhK2BypassFromRelay(_relays.actual(HH_RELAY_K2));
    in.p4ExerciseRunning = HH_AS_P4 < _state.antiSeize.count && _state.antiSeize.output[HH_AS_P4].running;
    in.k2ExerciseRunning = HH_AS_K2 < _state.antiSeize.count && _state.antiSeize.output[HH_AS_K2].running;
    in.k1Busy = _k1.busy();
    in.k1AntiSeizeOwned = _k1.owner() == K1Owner::AntiSeize;
    in.inhibited = _relays.inhibited();
    in.k1Motion = _k1.takeMotion();   // D6: the runtime is the sole consumer
    in.k1Hold = _step.running();      // stage 09 (C13, D12): hold periodic FF/feedback while testing

    // 4. Decide.
    const HomeHeatingOutputs out = _controller.update(in, raw, nowMs);

    // 5. Control slot only, every tick (A5/D3): the lock applies to forced outputs too.
    _relays.requestControl(HH_RELAY_P4, out.p4.on, RelayReason::Control);
    _relays.requestControl(HH_RELAY_K2, hhK2RelayForBypass(out.k2.bypass), RelayReason::Control);

    // 6. K1 (the controller already withholds commands while inhibited / anti-seize owned).
    if (out.k1.cmd.issue) {
        _k1.requestPulse(out.k1.cmd.dir, out.k1.cmd.ms, nowMs, K1Owner::Control);
        // Stage 09 (D11, A8): "last pulse" = tuning moves only. Recal and Position
        // moves (close to 0 %, fail-position anchor) are excluded.
        if (out.k1.cmdKind == K1CmdKind::Feedforward || out.k1.cmdKind == K1CmdKind::Feedback) {
            _lastPulseDir = out.k1.cmd.dir == K1Direction::Open ? 1 : -1;
            _lastPulseMs = out.k1.cmd.ms;
        }
    }

    // 6a. Stage 09 diagnostics (D2): after the outputs are applied, warnings only.
    tickDiagnostics(in, out, nowMs);

    // 6a'. Stage 09 K1 step test (C14, D13): after the controller, on this tick's decision.
    const StepTestOutput so = tickStepTest(in, out, raw, nowMs);

    // 6b. Stage 09 K1 pulse counter (D8: counted at the driver's power-ON site; A9).
    _pulses.update(_k1.runStarts(), local);

    // 7. Anti-seize K1 stroke = full recal length of the guarded settings (D16).
    const uint32_t stroke = k1RecalMs(guardHomeHeatingSettings(raw));
    if (stroke != _lastStrokeMs) {
        _antiSeize.setK1StrokeMs(stroke);
        _lastStrokeMs = stroke;
    }

    // 8. Controller alarm bits 0..23 (bits 24..31 belong to stage 03, never touched).
    const uint32_t mask = out.alarmMask & HH_ALARM_OWNED_MASK;
    _state.alarms.activeMask = (_state.alarms.activeMask & ~HH_ALARM_OWNED_MASK) | mask;
    const uint32_t changed = mask ^ _prevMask;
    for (uint8_t bit = 0; bit < 24; ++bit) {
        const uint32_t m = 1u << bit;
        if ((changed & m) == 0) {
            continue;
        }
        const bool raised = (mask & m) != 0;
        logEvent(toU16(raised ? EventType::AlarmRaised : EventType::AlarmCleared),
            static_cast<uint16_t>(EVENT_SOURCE_ALARM_BASE + bit), 0.0f, 0.0f);
    }
    _prevMask = mask;

    // 9. Events (C11).
    if (out.p4.on != _prevP4On) {
        logEvent(HH_EVENT_P4_REQUEST, static_cast<uint16_t>(EVENT_SOURCE_RELAY_BASE + HH_RELAY_P4),
            static_cast<float>(static_cast<uint8_t>(out.p4.reason)), out.p4.on ? 1.0f : 0.0f);
        _prevP4On = out.p4.on;
    }
    if (out.k2.bypass != _prevK2Bypass) {
        logEvent(HH_EVENT_K2_REQUEST, static_cast<uint16_t>(EVENT_SOURCE_RELAY_BASE + HH_RELAY_K2),
            static_cast<float>(static_cast<uint8_t>(out.k2.reason)), out.k2.bypass ? 1.0f : 0.0f);
        _prevK2Bypass = out.k2.bypass;
    }
    if (out.fail != _prevFail) {
        const uint8_t mode = static_cast<uint8_t>(out.fail);
        logEvent(HH_EVENT_FAILSAFE, static_cast<uint16_t>(HH_EVENT_SOURCE_FAILSAFE_BASE + mode),
            static_cast<float>(mode), static_cast<float>(static_cast<uint8_t>(_prevFail)));
        _prevFail = out.fail;
    }
    if (out.k1.recalEnded) {
        logEvent(HH_EVENT_K1_RECAL, static_cast<uint16_t>(HH_EVENT_SOURCE_K1_RECAL_BASE + 0), 0.0f, 0.0f);
    }
    if (out.k1.recalStarted) {
        logEvent(HH_EVENT_K1_RECAL, static_cast<uint16_t>(HH_EVENT_SOURCE_K1_RECAL_BASE + 1), 1.0f, 0.0f);
    }
    if (out.noNeed != _prevNoNeed) {
        const uint8_t v = out.noNeed ? 1 : 0;
        logEvent(HH_EVENT_NO_NEED, static_cast<uint16_t>(HH_EVENT_SOURCE_NO_NEED_BASE + v), static_cast<float>(v), 0.0f);
        _prevNoNeed = out.noNeed;
    }

    // 10. Status slice.
    writeStatus(raw, in, out, so, nowMs);
}

void HomeHeatingRuntime::writeStatus(const HomeHeatingSettings& raw, const HomeHeatingInputs& in,
        const HomeHeatingOutputs& out, const StepTestOutput& so, uint64_t nowMs) {
    HomeHeatingStatus st{};
    st.ready = true;
    st.heatingEnabled = raw.heatingEnabled;
    st.h2Set = raw.h2Set;
    st.p4On = out.p4.on;
    st.p4Reason = out.p4.reason;
    st.p4OffDelayLeftS = out.p4.offDelayLeftS;
    st.k2Bypass = out.k2.bypass;
    st.k2Reason = out.k2.reason;
    st.k1Mode = out.k1.mode;
    st.k1Known = out.k1.known;
    const bool running = _k1.running();
    const bool opening = running && _k1.direction() == K1Direction::Open;
    st.k1Moving = running ? (opening ? 1 : -1) : 0;
    float pos = out.k1.posPct;
    const uint32_t travelMs = k1TravelMs(guardHomeHeatingSettings(raw));
    if (running && travelMs > 0) {
        // Live projection: motion is only accounted at run end (takeMotion), so add
        // the running part here (display only; the estimator is never changed).
        const float deltaPct = 100.0f * static_cast<float>(_k1.currentRunMs(nowMs)) / static_cast<float>(travelMs);
        pos += opening ? deltaPct : -deltaPct;
    }
    st.k1PosPct = clampPct(pos);
    st.k1FfValid = out.k1.ffValid;
    st.k1FfPct = out.k1.ffAppliedPct;
    st.fail = out.fail;
    st.noNeed = out.noNeed;
    st.alarmMask = out.alarmMask & HH_ALARM_OWNED_MASK;
    // Stage 09 (C15).
    const SensorInput& h2 = in.sensor[HH_SENSOR_H2];   // sanitized: Ok implies finite
    st.h2ErrValid = h2.state == SensorState::Ok && raw.heatingEnabled;
    st.h2ErrC = st.h2ErrValid ? h2.tempC - raw.h2Set : 0.0f;
    st.lastPulseDir = _lastPulseDir;
    st.lastPulseS = static_cast<float>(_lastPulseMs) / 1000.0f;
    st.pulsesToday = _pulses.today();
    st.pulsesYesterdayValid = _pulses.yesterdayValid();
    st.pulsesYesterday = _pulses.yesterday();
    st.step.block = so.block;
    st.step.running = _step.running();
    st.step.elapsedS = _step.elapsedS(nowMs);
    st.step.pulseS = _step.pulseS();
    st.step.deadSeen = _step.deadTimeSeen();
    st.step.deadTimeS = _step.deadTimeS();
    st.step.last = _step.last();
    _status = st;
}
