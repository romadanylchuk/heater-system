#include "SensorDebounce.h"
#include <math.h>

void SensorDebounce::reset() {
    _state = SensorState::Unknown;
    _goodRun = 0;
    _badRun = 0;
    _hasGood = false;
    _lastGood = 0.0f;
    _prevSampleBad = true;
    _lastBad = ReadStatus::NoResponse;
}

SensorDebounce::Transition SensorDebounce::onReading(const SensorReading& reading) {
    SensorReading effective = reading;

    // D14: a decoded Ok reading of exactly 85.0 degC is reclassified
    // PowerOnValue when there has been no good reading since reset, the
    // previous sample was bad, or it jumps more than 5.0 degC from the last
    // good value.
    if (effective.status == ReadStatus::Ok && effective.tempC == POWER_ON_TEMP_C) {
        bool suspect = !_hasGood || _prevSampleBad || fabsf(effective.tempC - _lastGood) > POWER_ON_JUMP_C;
        if (suspect) {
            effective.status = ReadStatus::PowerOnValue;
        }
    }

    bool good = (effective.status == ReadStatus::Ok);
    Transition result = Transition::None;

    if (good) {
        _badRun = 0;
        _goodRun = incSaturating(_goodRun);
        _prevSampleBad = false;
        _lastGood = effective.tempC;
        _hasGood = true;
        if (_state != SensorState::Ok && _goodRun >= RECOVER_COUNT) {
            result = (_state == SensorState::Unknown) ? Transition::BecameOk : Transition::Recovered;
            _state = SensorState::Ok;
        }
    } else {
        _goodRun = 0;
        _badRun = incSaturating(_badRun);
        _prevSampleBad = true;
        _lastBad = effective.status;
        if (_state != SensorState::Fault && _badRun >= FAULT_COUNT) {
            result = Transition::Faulted;
            _state = SensorState::Fault;
        }
    }

    return result;
}

std::optional<float> SensorDebounce::value() const {
    if (_state == SensorState::Ok) {
        return _lastGood;
    }
    return std::nullopt;
}
