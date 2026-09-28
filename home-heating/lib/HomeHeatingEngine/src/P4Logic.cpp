#include "P4Logic.h"

void P4Logic::reset(uint64_t bootMs) {
    _on = false;
    _lastDemandMs = bootMs;
}

P4Decision P4Logic::update(const P4Inputs& in, const HomeHeatingSettings& g, uint64_t nowMs) {
    if (!in.heatingEnabled) {
        _on = false;
        return P4Decision{false, P4Reason::HeatingOff, true, 0};
    }

    const uint64_t delayMs = static_cast<uint64_t>(g.p4OffDelayMin) * 60000ULL;
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
    } else if (in.h3C >= g.h2Set) {
        on = true;
        reason = P4Reason::Demand;
        demand = true;
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
