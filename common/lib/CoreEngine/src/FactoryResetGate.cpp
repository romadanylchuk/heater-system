#include "FactoryResetGate.h"

void FactoryResetGate::begin(bool closedAtPowerOn, uint64_t monoMs) {
    if (closedAtPowerOn) {
        _phase = ResetGatePhase::Countdown;
        _start = monoMs;
    } else {
        _phase = ResetGatePhase::Inactive;
    }
    _openPending = false;
    _openSince = 0;
}

ResetGatePhase FactoryResetGate::update(bool closed, uint64_t monoMs) {
    if (_phase != ResetGatePhase::Countdown) {
        return _phase;  // terminal phases (Aborted/Confirmed) and Inactive stay put
    }

    if (closed) {
        _openPending = false;
        if (monoMs - _start >= HOLD_MS) {
            _phase = ResetGatePhase::Confirmed;
        }
    } else {
        if (!_openPending) {
            _openPending = true;
            _openSince = monoMs;
        }
        if (monoMs - _openSince >= RELEASE_DEBOUNCE_MS) {
            _phase = ResetGatePhase::Aborted;
        }
    }
    return _phase;
}

uint8_t FactoryResetGate::secondsLeft(uint64_t monoMs) const {
    if (_phase != ResetGatePhase::Countdown) {
        return 0;
    }
    uint64_t held = monoMs - _start;
    if (held >= HOLD_MS) {
        return 0;
    }
    uint64_t remainMs = HOLD_MS - held;
    return static_cast<uint8_t>((remainMs + 999) / 1000);
}
