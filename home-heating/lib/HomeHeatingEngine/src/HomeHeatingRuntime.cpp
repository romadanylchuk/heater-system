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

size_t at(HhRuntimeKey k) { return static_cast<size_t>(k); }

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
    return HomeHeatingRuntimeStatus::Ok;
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

void HomeHeatingRuntime::tick(uint64_t nowMs) {
    // 1. Not ready (a key is missing, D20): outputs to their passive state, alarms untouched.
    if (!_ready) {
        _status = HomeHeatingStatus{};
        _status.ready = false;
        _relays.requestControl(HH_RELAY_P4, false, RelayReason::Control);
        _relays.requestControl(HH_RELAY_K2, false, RelayReason::Control);
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
    in.k2RelayActual = _relays.actual(HH_RELAY_K2);
    in.p4ExerciseRunning = HH_AS_P4 < _state.antiSeize.count && _state.antiSeize.output[HH_AS_P4].running;
    in.k2ExerciseRunning = HH_AS_K2 < _state.antiSeize.count && _state.antiSeize.output[HH_AS_K2].running;
    in.k1Busy = _k1.busy();
    in.k1AntiSeizeOwned = _k1.owner() == K1Owner::AntiSeize;
    in.inhibited = _relays.inhibited();
    in.k1Motion = _k1.takeMotion();   // D6: the runtime is the sole consumer

    // 4. Decide.
    const HomeHeatingOutputs out = _controller.update(in, raw, nowMs);

    // 5. Control slot only, every tick (A5/D3): the lock applies to forced outputs too.
    _relays.requestControl(HH_RELAY_P4, out.p4.on, RelayReason::Control);
    _relays.requestControl(HH_RELAY_K2, out.k2.bypass, RelayReason::Control);

    // 6. K1 (the controller already withholds commands while inhibited / anti-seize owned).
    if (out.k1.cmd.issue) {
        _k1.requestPulse(out.k1.cmd.dir, out.k1.cmd.ms, nowMs, K1Owner::Control);
    }

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
    writeStatus(raw, out, nowMs);
}

void HomeHeatingRuntime::writeStatus(const HomeHeatingSettings& raw, const HomeHeatingOutputs& out, uint64_t nowMs) {
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
    _status = st;
}
