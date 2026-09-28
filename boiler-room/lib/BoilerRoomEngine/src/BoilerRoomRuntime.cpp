#include "BoilerRoomRuntime.h"
#include <math.h>
#include <string.h>
#include <HaLinkGate.h>
#include "BoilerRoomEvents.h"

namespace {

const char* const RUNTIME_KEYS[] = {
    BR_KEY_P1_DELTA_ON, BR_KEY_P1_DELTA_OFF, BR_KEY_P1_T1_MIN, BR_KEY_P1_HYST, BR_KEY_OH_ON, BR_KEY_OH_CLEAR,
    BR_KEY_P2_T2_OFF, BR_KEY_P2_HYST, BR_KEY_P2_T1_BURN, BR_KEY_P3_T3_OFFER, BR_KEY_P3_OFFER_WIN,
    BR_KEY_P3_OFFER_WAIT, BR_KEY_AF_ENABLE, BR_KEY_AF_INTERVAL, BR_KEY_AF_DURATION, BR_KEY_ACC_VOLUME,
    BR_KEY_ACC_T_BASE, BR_KEY_HOME_NO_NEED,
};
static_assert(sizeof(RUNTIME_KEYS) / sizeof(RUNTIME_KEYS[0]) == BR_RUNTIME_KEY_COUNT,
    "RUNTIME_KEYS must list exactly BR_RUNTIME_KEY_COUNT keys, in BrRuntimeKey order");

size_t at(BrRuntimeKey k) { return static_cast<size_t>(k); }

// Expected descriptor type: the C2 table row for the controller keys, Bool for homeNoNeed.
bool expectedType(const char* key, SettingType& out) {
    if (strcmp(key, BR_KEY_HOME_NO_NEED) == 0) {
        out = SettingType::Bool;
        return true;
    }
    for (size_t i = 0; i < BOILER_ROOM_CONTROL_SETTING_COUNT; ++i) {
        if (strcmp(BOILER_ROOM_CONTROL_SETTINGS[i].key, key) == 0) {
            out = BOILER_ROOM_CONTROL_SETTINGS[i].type;
            return true;
        }
    }
    return false;
}

uint32_t readU32(const ConfigEngine& c, size_t index) {
    const int32_t v = c.getInt(index);
    return v < 0 ? 0u : static_cast<uint32_t>(v);
}

}  // namespace

BoilerRoomRuntime::BoilerRoomRuntime(CommonState& state, BoilerRoomStatus& status, ConfigEngine& config,
    EventSink& events, RelayBank& relays)
    : _state(state), _status(status), _config(config), _events(events), _relays(relays) {}

BoilerRoomRuntimeStatus BoilerRoomRuntime::begin(uint64_t nowMs) {
    _ready = false;
    for (size_t k = 0; k < BR_RUNTIME_KEY_COUNT; ++k) {
        const int index = RUNTIME_KEYS[k] == nullptr ? -1 : _config.indexOf(RUNTIME_KEYS[k]);
        const SettingDescriptor* d = index < 0 ? nullptr : _config.descriptor(static_cast<size_t>(index));
        SettingType want{};
        if (d == nullptr || !expectedType(RUNTIME_KEYS[k], want) || d->type != want) {
            // D17: visible in the field log too, not only on Serial -- once per boot.
            if (!_missingLogged) {
                _events.logEvent(toU16(EventType::DiagnosticWarning),
                    static_cast<uint16_t>(EVENT_SOURCE_DIAG_BASE + BR_DIAG_CODE_SETTING_MISSING),
                    static_cast<float>(k), 0.0f, EventReason::Logic);
                _missingLogged = true;
            }
            return BoilerRoomRuntimeStatus::SettingMissing;
        }
        _idx[k] = static_cast<size_t>(index);
    }
    _bootMs = nowMs;
    _controller.reset();
    _prevMode = P3Mode::Normal;
    _prevMask = 0;
    for (uint8_t ch = 0; ch < BR_PUMP_COUNT; ++ch) {
        _prevOn[ch] = false;
        _prevSafety[ch] = false;
    }
    _ready = true;
    return BoilerRoomRuntimeStatus::Ok;
}

BoilerRoomSettings BoilerRoomRuntime::readSettings(const ConfigEngine& c, const size_t idx[]) {
    BoilerRoomSettings s{};
    s.p1DeltaOn = c.getNumber(idx[at(BrRuntimeKey::P1DeltaOn)]);
    s.p1DeltaOff = c.getNumber(idx[at(BrRuntimeKey::P1DeltaOff)]);
    s.p1T1Min = c.getNumber(idx[at(BrRuntimeKey::P1T1Min)]);
    s.p1Hyst = c.getNumber(idx[at(BrRuntimeKey::P1Hyst)]);
    s.ohOn = c.getNumber(idx[at(BrRuntimeKey::OhOn)]);
    s.ohClear = c.getNumber(idx[at(BrRuntimeKey::OhClear)]);
    s.p2T2Off = c.getNumber(idx[at(BrRuntimeKey::P2T2Off)]);
    s.p2Hyst = c.getNumber(idx[at(BrRuntimeKey::P2Hyst)]);
    s.p2T1Burn = c.getNumber(idx[at(BrRuntimeKey::P2T1Burn)]);
    s.p3T3Offer = c.getNumber(idx[at(BrRuntimeKey::P3T3Offer)]);
    s.p3OfferWindowMin = readU32(c, idx[at(BrRuntimeKey::P3OfferWin)]);
    s.p3OfferWaitMin = readU32(c, idx[at(BrRuntimeKey::P3OfferWait)]);
    s.afEnable = c.getBool(idx[at(BrRuntimeKey::AfEnable)]);
    s.afIntervalMin = readU32(c, idx[at(BrRuntimeKey::AfInterval)]);
    s.afDurationS = readU32(c, idx[at(BrRuntimeKey::AfDuration)]);
    s.accVolumeL = c.getNumber(idx[at(BrRuntimeKey::AccVolume)]);
    s.accTBase = c.getNumber(idx[at(BrRuntimeKey::AccTBase)]);
    return s;
}

void BoilerRoomRuntime::logEvent(uint16_t type, uint16_t source, float value, float aux) {
    _events.logEvent(type, source, value, aux, EventReason::Logic);
}

void BoilerRoomRuntime::tick(uint64_t nowMs) {
    // 1. Not ready (a key is missing, D17): the controller is blind, boiler safety wins.
    if (!_ready) {
        _status = BoilerRoomStatus{};
        _status.ready = false;
        _relays.requestSafety(BR_PUMP_P1, true);
        return;
    }

    // 2. Raw settings (the controller applies the ordering guard itself).
    const BoilerRoomSettings raw = readSettings(_config, _idx);

    // 3. Inputs.
    BoilerRoomInputs in{};
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        if (i < _state.sensors.count) {
            const LogicalSensorStatus& s = _state.sensors.sensor[i];
            // Final-check Should 2: an Ok sensor with a non-finite reading is not
            // working -> Fault (the ONE place; the controller never sees Ok+NaN).
            const bool bad = s.state == SensorState::Ok && !isfinite(s.tempC);
            in.sensor[i] = bad ? SensorInput{SensorState::Fault, 0.0f, s.missing}
                               : SensorInput{s.state, s.tempC, s.missing};
        } else {
            in.sensor[i] = SensorInput{SensorState::Unassigned, 0.0f, false};  // not provided = not working
        }
    }
    const size_t noNeedIdx = _idx[at(BrRuntimeKey::HomeNoNeed)];
    const bool persisted = _config.getBool(noNeedIdx);
    in.noNeedFlag = haGatedFlag(persisted, _state.network);
    in.linkUp = _state.network.mqttConnected;
    in.p3RelayOn = _relays.actual(BR_PUMP_P3);
    // D13: shared P3 last-run timer from RelayBank activity (control, safety, exercise), >= boot.
    const uint64_t relayLast = _relays.hasBeenOn(BR_PUMP_P3) ? _relays.lastOnMs(BR_PUMP_P3) : 0;
    in.p3LastRunMs = relayLast > _bootMs ? relayLast : _bootMs;

    // 4. Decide.
    const BoilerRoomOutputs out = _controller.update(in, raw, nowMs);

    // 5. Slot mapping (D3): safety slot only for the bypass states, control slot every tick.
    for (uint8_t ch = 0; ch < BR_PUMP_COUNT; ++ch) {
        const PumpDecision& d = out.pump[ch];
        if (d.safety) {
            _relays.requestSafety(ch, true);
        } else {
            _relays.clearSafety(ch);
        }
        _relays.requestControl(ch, d.controlOn, RelayReason::Control);
    }

    // 6. Flag self-clear on OFFER entry (D24; ConfigEngine logs ConfigChanged/Logic).
    if (out.requestFlagClear) {
        _config.setNumber(noNeedIdx, 0.0f, EventReason::Logic, nowMs);
    }

    // 7. Controller alarm bits 0..23 (bits 24..31 belong to stage 03, never touched).
    _state.alarms.activeMask = (_state.alarms.activeMask & ~BR_ALARM_OWNED_MASK) | (out.alarmMask & BR_ALARM_OWNED_MASK);
    const uint32_t changed = (out.alarmMask ^ _prevMask) & BR_ALARM_OWNED_MASK;
    for (uint8_t bit = 0; bit < 24; ++bit) {
        const uint32_t m = 1u << bit;
        if ((changed & m) == 0) {
            continue;
        }
        const bool raised = (out.alarmMask & m) != 0;
        const SensorInput& t1 = in.sensor[BR_SENSOR_T1];
        const float value = (bit == BR_ALARM_OVERHEAT && t1.state == SensorState::Ok) ? t1.tempC : 0.0f;
        logEvent(toU16(raised ? EventType::AlarmRaised : EventType::AlarmCleared),
            static_cast<uint16_t>(EVENT_SOURCE_ALARM_BASE + bit), value, 0.0f);
    }
    _prevMask = out.alarmMask;

    // 8. Events.
    if (out.p3Mode != _prevMode) {
        logEvent(BR_EVENT_P3_MODE, static_cast<uint16_t>(EVENT_SOURCE_PROJECT_BASE + static_cast<uint16_t>(out.p3Mode)),
            static_cast<float>(static_cast<uint8_t>(out.p3Mode)), static_cast<float>(static_cast<uint8_t>(_prevMode)));
        _prevMode = out.p3Mode;
    }
    if (out.antiFreezeStarted) {
        logEvent(toU16(EventType::AntiFreezeRun), static_cast<uint16_t>(EVENT_SOURCE_RELAY_BASE + BR_PUMP_P3),
            static_cast<float>(raw.afDurationS), 0.0f);
    }
    for (uint8_t ch = 0; ch < BR_PUMP_COUNT; ++ch) {
        const PumpDecision& d = out.pump[ch];
        if (d.on != _prevOn[ch] || d.safety != _prevSafety[ch]) {
            logEvent(BR_EVENT_PUMP_REQUEST, static_cast<uint16_t>(EVENT_SOURCE_RELAY_BASE + ch),
                static_cast<float>(static_cast<uint8_t>(d.reason)), d.on ? 1.0f : 0.0f);
            _prevOn[ch] = d.on;
            _prevSafety[ch] = d.safety;
        }
    }

    // 9. Status slice.
    BoilerRoomStatus st{};
    st.ready = true;
    for (uint8_t ch = 0; ch < BR_PUMP_COUNT; ++ch) {
        st.pump[ch] = BoilerRoomPumpStatus{out.pump[ch].on, out.pump[ch].safety, out.pump[ch].reason};
    }
    st.p3Mode = out.p3Mode;
    st.overheat = out.overheat;
    st.antiFreezeRunning = out.antiFreezeRunning;
    st.dumpActive = out.dumpActive;
    st.offerDisabled = out.offerDisabled;
    st.noNeedSaved = _config.getBool(noNeedIdx);   // after a self-clear: the value now persisted
    st.noNeedEffective = in.noNeedFlag;
    st.linkUp = in.linkUp;
    st.offerWindowLeftS = out.offerWindowLeftS;
    st.offerWaitLeftS = out.offerWaitLeftS;
    st.afInS = out.afInS;
    st.p3IdleS = nowMs > in.p3LastRunMs ? static_cast<uint32_t>((nowMs - in.p3LastRunMs) / 1000) : 0;
    st.energyQuality = out.energy.quality;
    st.energyKWh = out.energy.kWh;
    st.t6Usable = out.t6Usable;
    st.alarmMask = out.alarmMask;
    _status = st;
}
