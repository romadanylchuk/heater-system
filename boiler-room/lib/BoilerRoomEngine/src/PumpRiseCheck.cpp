#include "PumpRiseCheck.h"

void PumpRiseCheck::reset() {
    _tracking = false;
    _active = false;
    _startMs = 0;
    _baseline = 0.0f;
}

bool PumpRiseCheck::update(const RiseCheckInputs& in, const RiseCheckParams& p, uint64_t nowMs) {
    if (!in.pumpOn || !(in.hotOk && in.coldOk)) {
        // Pump off, or a pause on a non-Ok sensor (A4/D5): the timer and the
        // baseline restart at the next ON edge / recovery.
        _tracking = false;
        _active = false;
        return _active;
    }
    if (!_tracking) {
        _tracking = true;
        _startMs = nowMs;
        _baseline = in.coldC;
    }
    const uint64_t minOnMs = static_cast<uint64_t>(p.minOnMin) * 60000ull;
    const bool longEnough = nowMs >= _startMs && (nowMs - _startMs) >= minOnMs;
    const bool cond = longEnough && (in.hotC - in.coldC > p.deltaC) && (in.coldC - _baseline < p.minRiseC);
    _active = p.enabled && cond;
    return _active;
}
