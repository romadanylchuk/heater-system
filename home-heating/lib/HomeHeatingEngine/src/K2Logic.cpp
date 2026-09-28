#include "K2Logic.h"

void K2Logic::reset() {
    _bypass = false;
    _valid = false;
    _deltaOk = false;
    _h3Ok = false;
    _h4Ok = false;
}

K2Decision K2Logic::update(SensorHealth h3, float h3C, SensorHealth h4, float h4C, const HomeHeatingSettings& g) {
    if (h4 == SensorHealth::Failed) {
        _valid = false;
        _bypass = true;
        return K2Decision{true, K2Reason::H4Fault};
    }
    if (h3 == SensorHealth::Failed) {
        _valid = false;
        _bypass = true;
        return K2Decision{true, K2Reason::H3Fault};
    }
    if (h3 == SensorHealth::Pending || h4 == SensorHealth::Pending) {
        _valid = false;
        return K2Decision{_bypass, K2Reason::SensorWait};
    }

    if (!_valid) {
        _deltaOk = h3C > h4C + g.k2Delta;
        _h3Ok = h3C >= g.k2H3Min;
        _h4Ok = h4C < g.k2H4Max;
        _valid = true;
    } else {
        if (!_deltaOk && h3C > h4C + g.k2Delta) {
            _deltaOk = true;
        } else if (_deltaOk && h3C <= h4C + g.k2Delta - g.k2DeltaHyst) {
            _deltaOk = false;
        }
        if (!_h3Ok && h3C >= g.k2H3Min) {
            _h3Ok = true;
        } else if (_h3Ok && h3C < g.k2H3Min - g.k2H3MinHyst) {
            _h3Ok = false;
        }
        if (_h4Ok && h4C >= g.k2H4Max) {
            _h4Ok = false;
        } else if (!_h4Ok && h4C <= g.k2H4Max - g.k2H4MaxHyst) {
            _h4Ok = true;
        }
    }

    K2Reason reason = K2Reason::Charging;
    if (!_h4Ok) {
        reason = K2Reason::H4Full;
    } else if (!_h3Ok) {
        reason = K2Reason::H3Low;
    } else if (!_deltaOk) {
        reason = K2Reason::DeltaLow;
    }
    _bypass = reason != K2Reason::Charging;
    return K2Decision{_bypass, reason};
}
