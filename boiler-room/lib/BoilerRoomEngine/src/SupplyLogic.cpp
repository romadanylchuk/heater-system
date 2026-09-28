#include "SupplyLogic.h"

namespace {

// Elapsed ms since `since`, 0 when the clock reads earlier (never underflows).
uint64_t elapsedMs(uint64_t nowMs, uint64_t since) {
    return nowMs > since ? nowMs - since : 0;
}

// Remaining whole seconds (rounded up) of a period, 0 once it has elapsed.
uint32_t leftS(uint64_t periodMs, uint64_t elapsed) {
    if (elapsed >= periodMs) {
        return 0;
    }
    return static_cast<uint32_t>((periodMs - elapsed + 999) / 1000);
}

PumpReason modeReason(P3Mode m) {
    switch (m) {
        case P3Mode::Off: return PumpReason::SupplyOff;
        case P3Mode::Offer: return PumpReason::SupplyOffer;
        case P3Mode::Normal:
        default: return PumpReason::SupplyNormal;
    }
}

}  // namespace

void SupplyLogic::reset() {
    _mode = P3Mode::Normal;
    _offerStartMs = 0;
    _waitArmed = false;
    _waitStartMs = 0;
    _afRunning = false;
    _afStartMs = 0;
}

P3Mode SupplyLogic::mode() const {
    return _mode;
}

SupplyDecision SupplyLogic::update(const SupplyInputs& in, const BoilerRoomSettings& s, uint64_t nowMs) {
    SupplyDecision d{};
    const uint64_t windowMs = static_cast<uint64_t>(s.p3OfferWindowMin) * 60000ull;
    const uint64_t waitMs = static_cast<uint64_t>(s.p3OfferWaitMin) * 60000ull;
    const bool t3Ok = (in.t3 == SensorHealth::Ok);

    // 1. Gates (D6), else at most one transition for the current mode.
    if (!in.linkUp || !t3Ok) {
        _mode = P3Mode::Normal;
    } else {
        switch (_mode) {
            case P3Mode::Normal:
                if (in.flag) {
                    _mode = P3Mode::Off;
                }
                break;
            case P3Mode::Off: {
                const bool waitElapsed =
                    !_waitArmed || s.p3OfferWaitMin == 0 || elapsedMs(nowMs, _waitStartMs) >= waitMs;
                if (!in.flag) {
                    _mode = P3Mode::Normal;   // D6 gap-fill: the home needs heat again
                } else if (!in.overheat && in.t3C >= s.p3T3Offer && waitElapsed) {
                    _mode = P3Mode::Offer;
                    _offerStartMs = nowMs;
                    d.requestFlagClear = true;
                }
                break;
            }
            case P3Mode::Offer:
                if (elapsedMs(nowMs, _offerStartMs) >= windowMs) {
                    if (in.flag) {
                        _mode = P3Mode::Off;
                        _waitArmed = true;
                        _waitStartMs = nowMs;
                    } else {
                        _mode = P3Mode::Normal;
                    }
                }
                break;
        }
    }

    // 2. Anti-freeze (D13).
    const uint64_t afIntervalMs = static_cast<uint64_t>(s.afIntervalMin) * 60000ull;
    if (_afRunning) {
        if (!s.afEnable || elapsedMs(nowMs, _afStartMs) >= static_cast<uint64_t>(s.afDurationS) * 1000ull) {
            _afRunning = false;
        }
    } else if (s.afEnable && !in.relayOn && elapsedMs(nowMs, in.lastRunMs) >= afIntervalMs) {
        _afRunning = true;
        _afStartMs = nowMs;
        d.antiFreezeStarted = true;
    }

    // 3. Output: dump > anti-freeze > state machine.
    const bool smOn = (_mode != P3Mode::Off);
    if (in.overheat) {
        d.p3 = PumpDecision{true, true, PumpReason::OverheatDump, smOn};
    } else if (_afRunning) {
        d.p3 = PumpDecision{true, true, PumpReason::AntiFreeze, smOn};
    } else {
        d.p3 = PumpDecision{smOn, false, modeReason(_mode), smOn};
    }
    d.mode = _mode;
    d.antiFreezeRunning = _afRunning;
    d.dumpActive = in.overheat;
    d.offerDisabled = !t3Ok;
    d.offerWindowLeftS = (_mode == P3Mode::Offer) ? leftS(windowMs, elapsedMs(nowMs, _offerStartMs)) : 0;
    d.offerWaitLeftS =
        (_waitArmed && s.p3OfferWaitMin != 0) ? leftS(waitMs, elapsedMs(nowMs, _waitStartMs)) : 0;
    d.afInS = (s.afEnable && !in.relayOn && !_afRunning)
        ? leftS(afIntervalMs, elapsedMs(nowMs, in.lastRunMs))
        : 0;
    return d;
}
