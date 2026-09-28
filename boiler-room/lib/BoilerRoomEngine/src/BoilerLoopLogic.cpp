#include "BoilerLoopLogic.h"

namespace {

PumpDecision controlDecision(bool on, PumpReason reason) {
    return PumpDecision{on, false, reason, on};
}

PumpDecision safetyOn(PumpReason reason, bool controlOn) {
    return PumpDecision{true, true, reason, controlOn};
}

}  // namespace

void BoilerLoopLogic::reset() {
    _overheat = false;
    _p1RuleOn = false;
    _p2RuleOn = false;
    _burning = false;
}

bool BoilerLoopLogic::overheat() const {
    return _overheat;
}

LoopDecision BoilerLoopLogic::update(const SensorInput t[BR_SENSOR_COUNT], const BoilerRoomSettings& s) {
    const SensorHealth h1 = classifySensor(t[BR_SENSOR_T1].state);
    const SensorHealth h2 = classifySensor(t[BR_SENSOR_T2].state);
    const SensorHealth h3 = classifySensor(t[BR_SENSOR_T3].state);
    const float t1 = t[BR_SENSOR_T1].tempC;
    const float t2 = t[BR_SENSOR_T2].tempC;
    const float t3 = t[BR_SENSOR_T3].tempC;

    // Overheat latch (D4): Pending T1 holds it, Failed T1 clears it.
    if (h1 == SensorHealth::Ok) {
        if (!_overheat && t1 > s.ohOn) {
            _overheat = true;
        } else if (_overheat && t1 < s.ohClear) {
            _overheat = false;
        }
    } else if (h1 == SensorHealth::Failed) {
        _overheat = false;
    }

    // Burning latch (D10).
    if (h1 == SensorHealth::Ok) {
        if (!_burning && t1 > s.p2T1Burn) {
            _burning = true;
        } else if (_burning && t1 < s.p2T1Burn - s.p2Hyst) {
            _burning = false;
        }
    } else {
        _burning = false;
    }

    // P1 rule state (D11): differential when T3 is Ok, T1-only when T3 has Failed.
    if (h1 == SensorHealth::Ok && h3 == SensorHealth::Ok) {
        if (!_p1RuleOn) {
            if (t1 > t3 + s.p1DeltaOn && t1 > s.p1T1Min) {
                _p1RuleOn = true;
            }
        } else if (t1 < t3 + s.p1DeltaOff || t1 < s.p1T1Min - s.p1Hyst) {
            _p1RuleOn = false;
        }
    } else if (h1 == SensorHealth::Ok && h3 == SensorHealth::Failed) {
        if (!_p1RuleOn) {
            if (t1 > s.p1T1Min) {
                _p1RuleOn = true;
            }
        } else if (t1 < s.p1T1Min - s.p1Hyst) {
            _p1RuleOn = false;
        }
    } else {
        _p1RuleOn = false;
    }

    LoopDecision d{};
    d.overheat = _overheat;

    // P1 decision, in priority order.
    if (_overheat) {
        d.p1 = safetyOn(PumpReason::Overheat, _p1RuleOn);
    } else if (h1 == SensorHealth::Failed) {
        d.p1 = safetyOn(PumpReason::T1FaultForced, false);
    } else if (h1 == SensorHealth::Pending || h3 == SensorHealth::Pending) {
        d.p1 = controlDecision(false, PumpReason::SensorWait);
    } else {
        d.p1 = controlDecision(
            _p1RuleOn, h3 == SensorHealth::Ok ? PumpReason::Charge : PumpReason::ChargeT1Only);
    }

    // P2 rule state (D10/D11): normal needs burning; T2-only (T1 Failed) assumes burning (q1).
    if (h1 == SensorHealth::Ok && h2 == SensorHealth::Ok) {
        if (!_p2RuleOn) {
            if (t2 < s.p2T2Off - s.p2Hyst && _burning) {
                _p2RuleOn = true;
            }
        } else if (t2 >= s.p2T2Off || !_burning) {
            _p2RuleOn = false;
        }
    } else if (h1 == SensorHealth::Failed && h2 == SensorHealth::Ok) {
        if (!_p2RuleOn) {
            if (t2 < s.p2T2Off - s.p2Hyst) {
                _p2RuleOn = true;
            }
        } else if (t2 >= s.p2T2Off) {
            _p2RuleOn = false;
        }
    } else {
        _p2RuleOn = false;
    }

    // P2 decision, in priority order.
    if (h1 == SensorHealth::Failed && h2 == SensorHealth::Failed) {
        d.p2 = safetyOn(PumpReason::T1T2FaultForced, false);
    } else if (h1 == SensorHealth::Pending || h2 == SensorHealth::Pending) {
        d.p2 = controlDecision(false, PumpReason::SensorWait);
    } else if (h1 == SensorHealth::Failed) {
        d.p2 = controlDecision(_p2RuleOn, PumpReason::ReturnT2Only);
    } else if (h2 == SensorHealth::Failed) {
        d.p2 = controlDecision(_burning, PumpReason::ReturnBurnGate);
    } else {
        d.p2 = controlDecision(_p2RuleOn, PumpReason::Return);
    }
    return d;
}
