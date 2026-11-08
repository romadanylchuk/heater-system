#include "P4Logic.h"

void P4Logic::reset(uint64_t bootMs) {
    _on = false;
    _lastDemandMs = bootMs;
    _startArmed = false;
    _startSinceMs = bootMs;
}

P4Decision P4Logic::update(const P4Inputs& in, const HomeHeatingSettings& g, uint64_t nowMs) {
    if (!in.heatingEnabled) {
        _on = false;
        _startArmed = false;
        return P4Decision{false, P4Reason::HeatingOff, true, 0};
    }

    const uint64_t delayMs = static_cast<uint64_t>(g.p4OffDelayMin) * 60000ULL;
    const uint64_t confirmMs = static_cast<uint64_t>(g.p4StartConfirmS) * 1000ULL;
    const bool h3Ok = in.h3 == SensorHealth::Ok && in.fail != FailMode::Multi;

    // Start qualification (rule 5a): only while P4 is off and H3 is a trusted reading.
    bool startReady = false;
    bool startWaiting = false;
    if (!_on && h3Ok && in.h3C >= g.h2Set + g.p4StartMargin) {
        if (!_startArmed) {
            _startArmed = true;
            _startSinceMs = nowMs;
        }
        const uint64_t held = nowMs >= _startSinceMs ? nowMs - _startSinceMs : 0;
        startReady = held >= confirmMs;
        startWaiting = !startReady;
    } else {
        _startArmed = false;
    }
    bool on = false;
    P4Reason reason = P4Reason::SupplyCold;
    bool demand = false;
    bool delayRule = false;   // rule 6/7 applied

    if (in.h3 == SensorHealth::Failed) {
        if (in.fail == FailMode::Multi) {
            reason = P4Reason::MultiFaultOff;
        } else {
            on = true;
            reason = P4Reason::H3FaultForced;
            demand = true;
        }
    } else if (in.h3 == SensorHealth::Pending) {
        reason = P4Reason::SensorWait;
    } else if (in.fail == FailMode::Multi) {
        on = true;
        reason = P4Reason::MultiFaultForced;
        demand = true;
    } else if (_on ? in.h3C >= g.h2Set : startReady) {
        // Running: hold while H3 >= h2Set. Off: start only once qualified (5a).
        on = true;
        reason = P4Reason::Demand;
        demand = true;
        _startArmed = false;
    } else if (startWaiting) {
        reason = P4Reason::StartWait;
    } else {
        delayRule = true;
        const uint64_t since = nowMs >= _lastDemandMs ? nowMs - _lastDemandMs : 0;
        if (_on && since < delayMs) {
            on = true;
            reason = P4Reason::OffDelay;
        } else {
            reason = P4Reason::SupplyCold;
        }
    }

    if (demand) {
        _lastDemandMs = nowMs;
    }
    _on = on;

    const uint64_t since = nowMs >= _lastDemandMs ? nowMs - _lastDemandMs : 0;
    const bool elapsed = !on && since >= delayMs;
    uint32_t leftS = 0;
    if (delayRule && !elapsed && since < delayMs) {
        const uint64_t leftMs = delayMs - since;
        leftS = static_cast<uint32_t>((leftMs + 999ULL) / 1000ULL);
    }
    return P4Decision{on, reason, elapsed, leftS};
}
