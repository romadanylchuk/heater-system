#include "K1StepTest.h"
#include <math.h>
#include <string.h>
#include "HomeHeatingControlSettings.h"

namespace {

// Row of HOME_HEATING_CONTROL_SETTINGS with the given key; the key set is
// fixed at compile time and covered by tests, so a miss is a programming error.
const SettingDescriptor* rowOf(const char* key) {
    for (size_t i = 0; i < HOME_HEATING_CONTROL_SETTING_COUNT; ++i) {
        if (strcmp(HOME_HEATING_CONTROL_SETTINGS[i].key, key) == 0) {
            return &HOME_HEATING_CONTROL_SETTINGS[i];
        }
    }
    return nullptr;
}

float clampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

bool allOk(SensorHealth h1, SensorHealth h2, SensorHealth h3) {
    return h1 == SensorHealth::Ok && h2 == SensorHealth::Ok && h3 == SensorHealth::Ok;
}

}  // namespace

const char* stepBlockKey(StepBlock b) {
    switch (b) {
        case StepBlock::None: return "none";
        case StepBlock::Unavailable: return "unavailable";
        case StepBlock::Running: return "running";
        case StepBlock::HeatingOff: return "heating_off";
        case StepBlock::P4Off: return "p4_off";
        case StepBlock::Ota: return "ota";
        case StepBlock::AntiSeize: return "anti_seize";
        case StepBlock::Sensors: return "sensors";
        case StepBlock::K1Mode: return "k1_mode";
        case StepBlock::K1Unknown: return "k1_unknown";
        case StepBlock::K1Busy: return "k1_busy";
        case StepBlock::K1Headroom: return "k1_headroom";
        case StepBlock::History: return "history";
        case StepBlock::H3Unsteady: return "h3_unsteady";
        case StepBlock::H2Unsteady: return "h2_unsteady";
    }
    return "none";
}

const char* stepAbortKey(StepAbort a) {
    switch (a) {
        case StepAbort::None: return "none";
        case StepAbort::Cancel: return "cancel";
        case StepAbort::HeatingOff: return "heating_off";
        case StepAbort::P4Off: return "p4_off";
        case StepAbort::Ota: return "ota";
        case StepAbort::AntiSeize: return "anti_seize";
        case StepAbort::Recal: return "recal";
        case StepAbort::Sensors: return "sensors";
        case StepAbort::K1Mode: return "k1_mode";
        case StepAbort::H3Changed: return "h3_changed";
    }
    return "none";
}

const char* stepOutcomeKey(StepOutcome o) {
    switch (o) {
        case StepOutcome::None: return "none";
        case StepOutcome::Result: return "result";
        case StepOutcome::NoResponse: return "no_response";
        case StepOutcome::Aborted: return "aborted";
    }
    return "none";
}

StepSteadiness stepSteadiness(const SensorHistory& hist, float h2Now, float h3Now) {
    StepSteadiness st{};
    st.full = hist.size() >= STEP_STEADY_SAMPLES;
    st.h3Steady = hist.steady(HH_SENSOR_H3, h3Now, STEP_H3_BAND_C, STEP_STEADY_SAMPLES);
    st.h2Steady = hist.steady(HH_SENSOR_H2, h2Now, STEP_H2_BAND_C, STEP_STEADY_SAMPLES);
    return st;
}

StepSuggestion suggestK1Tuning(float deadTimeS, float responseCps) {
    StepSuggestion sg{false, 0u, 0.0f};
    if (!isfinite(deadTimeS) || !isfinite(responseCps) || deadTimeS <= 0.0f || responseCps <= 0.0f) {
        return sg;
    }
    const SettingDescriptor* periodRow = rowOf(HH_KEY_K1_PERIOD);
    const SettingDescriptor* gainRow = rowOf(HH_KEY_K1_GAIN);
    if (periodRow == nullptr || gainRow == nullptr) {
        return sg;
    }
    // Clamp in float first (so lroundf never sees a huge value); the row
    // bounds are whole seconds / multiples of the 0.5 step, so rounding a
    // clamped value stays inside the range.
    const float period = clampF(STEP_PERIOD_FACTOR * deadTimeS, periodRow->minValue, periodRow->maxValue);
    const float step = gainRow->step > 0.0f ? gainRow->step : 0.5f;
    const float rawGain = clampF(STEP_GAIN_FRACTION / responseCps, gainRow->minValue, gainRow->maxValue);
    float gain = static_cast<float>(lroundf(rawGain / step)) * step;
    gain = clampF(gain, gainRow->minValue, gainRow->maxValue);
    sg.valid = true;
    sg.periodS = static_cast<uint32_t>(lroundf(period));
    sg.gain = gain;
    return sg;
}

void K1StepTest::reset() {
    _running = false;
    _t0Ms = 0;
    _h2StartC = 0.0f;
    _h3StartC = 0.0f;
    _pulseS = 0;
    _deadSeen = false;
    _deadMs = 0;
    _winStartMs = 0;
    _winMin = 0.0f;
    _winMax = 0.0f;
    _last = StepTestResult{};
}

uint32_t K1StepTest::elapsedS(uint64_t nowMs) const {
    if (!_running || nowMs < _t0Ms) {
        return 0u;
    }
    return static_cast<uint32_t>((nowMs - _t0Ms) / 1000u);
}

float K1StepTest::deadTimeS() const {
    return (_running && _deadSeen) ? static_cast<float>(_deadMs) / 1000.0f : 0.0f;
}

StepBlock K1StepTest::evaluateBlock(const StepTestInputs& in, const StepTestSettings& s) const {
    if (!in.available) return StepBlock::Unavailable;
    if (_running) return StepBlock::Running;
    if (!in.heatingEnabled) return StepBlock::HeatingOff;
    // The OTA inhibit forces every relay OFF (the P4 actual drops with it), so it
    // is checked before P4 to report "ota", not "p4_off".
    if (in.inhibited) return StepBlock::Ota;
    if (!(in.p4Requested && in.p4RelayActual)) return StepBlock::P4Off;
    if (in.k1AntiSeizeOwned) return StepBlock::AntiSeize;
    if (!allOk(in.h1, in.h2, in.h3)) return StepBlock::Sensors;
    if (in.k1Mode != K1Mode::Normal) return StepBlock::K1Mode;
    if (!in.k1Known) return StepBlock::K1Unknown;
    if (in.k1Busy || in.k1CmdThisTick) return StepBlock::K1Busy;
    // D19: the OPEN pulse must not reach the end stop (estimator value).
    // Written as !(... < 100) so a NaN / non-finite position blocks too.
    if (s.travelS == 0u || !isfinite(in.k1PosPct) ||
        !(in.k1PosPct + 100.0f * static_cast<float>(s.pulseS) / static_cast<float>(s.travelS) < 100.0f)) {
        return StepBlock::K1Headroom;
    }
    if (!in.steady.full) return StepBlock::History;
    if (!in.steady.h3Steady) return StepBlock::H3Unsteady;
    if (!in.steady.h2Steady) return StepBlock::H2Unsteady;
    return StepBlock::None;
}

StepAbort K1StepTest::abortReason(const StepTestInputs& in) const {
    if (in.cancelRequest) return StepAbort::Cancel;
    if (!in.heatingEnabled) return StepAbort::HeatingOff;
    if (in.inhibited) return StepAbort::Ota;              // before P4, see evaluateBlock
    if (!(in.p4Requested && in.p4RelayActual)) return StepAbort::P4Off;
    if (in.k1AntiSeizeOwned) return StepAbort::AntiSeize;
    if (in.recalStarted || in.k1Mode == K1Mode::Recalibrating) return StepAbort::Recal;
    if (!allOk(in.h1, in.h2, in.h3)) return StepAbort::Sensors;
    if (in.k1Mode != K1Mode::Normal) return StepAbort::K1Mode;
    if (fabsf(in.h3C - _h3StartC) > STEP_H3_ABORT_C) return StepAbort::H3Changed;
    return StepAbort::None;
}

void K1StepTest::finish(StepOutcome outcome, StepAbort abort, float h2SettledC, StepSuggestion suggest,
        float responseCps) {
    StepTestResult r{};
    r.outcome = outcome;
    r.abort = abort;
    r.deadTimeS = _deadSeen ? static_cast<float>(_deadMs) / 1000.0f : 0.0f;  // 0 = no movement seen
    r.responseCps = responseCps;
    r.h2StartC = _h2StartC;
    r.h2SettledC = h2SettledC;
    r.pulseS = _pulseS;
    r.suggest = suggest;
    _last = r;
    _running = false;
}

StepTestOutput K1StepTest::update(const StepTestInputs& in, const StepTestSettings& s, uint64_t nowMs) {
    StepTestOutput out{};
    out.pulse = K1Command{false, K1Direction::Close, 0u};

    // 1. Block evaluation (always).
    out.block = evaluateBlock(in, s);

    if (_running) {
        // 2. Abort check, first match.
        const StepAbort abort = abortReason(in);
        if (abort != StepAbort::None) {
            finish(StepOutcome::Aborted, abort, in.h2C, StepSuggestion{false, 0u, 0.0f}, 0.0f);
            out.ended = true;
        } else {
            // 3. Measure.
            const uint64_t el = nowMs >= _t0Ms ? nowMs - _t0Ms : 0u;
            bool settled = false;
            float h2Settled = 0.0f;
            if (!_deadSeen) {
                if (fabsf(in.h2C - _h2StartC) >= STEP_MOVE_C) {
                    _deadSeen = true;
                    _deadMs = static_cast<uint32_t>(el);
                    _winStartMs = nowMs;
                    _winMin = in.h2C;
                    _winMax = in.h2C;
                }
            } else {
                if (in.h2C < _winMin) _winMin = in.h2C;
                if (in.h2C > _winMax) _winMax = in.h2C;
                if (_winMax - _winMin >= STEP_SETTLE_C) {
                    _winStartMs = nowMs;
                    _winMin = in.h2C;
                    _winMax = in.h2C;
                } else if ((nowMs >= _winStartMs ? nowMs - _winStartMs : 0u) >= STEP_SETTLE_MS) {
                    settled = true;
                    h2Settled = in.h2C;
                }
            }
            if (!settled && el >= STEP_OBSERVE_MS) {
                if (!_deadSeen) {
                    finish(StepOutcome::NoResponse, StepAbort::None, in.h2C, StepSuggestion{false, 0u, 0.0f},
                        0.0f);
                    out.ended = true;
                } else {
                    settled = true;
                    h2Settled = in.h2C;
                }
            }
            if (settled) {
                const float r = _pulseS > 0u ? (h2Settled - _h2StartC) / static_cast<float>(_pulseS) : NAN;
                if (!isfinite(r) || r <= 0.0f) {
                    finish(StepOutcome::NoResponse, StepAbort::None, h2Settled, StepSuggestion{false, 0u, 0.0f},
                        0.0f);
                } else {
                    finish(StepOutcome::Result, StepAbort::None, h2Settled,
                        suggestK1Tuning(static_cast<float>(_deadMs) / 1000.0f, r), r);
                }
                out.ended = true;
            }
        }
        if (out.ended) {
            out.block = evaluateBlock(in, s);   // idle again: report the current start precondition
        }
        return out;                             // no start on a running or end tick
    }

    // 4. Idle + startRequest + block None: start. cancelRequest while idle is ignored.
    if (in.startRequest && out.block == StepBlock::None) {
        _running = true;
        _t0Ms = nowMs;
        _h2StartC = in.h2C;
        _h3StartC = in.h3C;
        _pulseS = s.pulseS;
        _deadSeen = false;
        _deadMs = 0;
        _winStartMs = nowMs;
        _winMin = in.h2C;
        _winMax = in.h2C;
        out.pulse = K1Command{true, K1Direction::Open, s.pulseS * 1000u};
        out.started = true;
        out.block = StepBlock::Running;
    }
    return out;
}
