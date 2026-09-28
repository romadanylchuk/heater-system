#include "BoilerRoomDiagnostics.h"

namespace {

bool sensorOk(const SensorInput& s) { return s.state == SensorState::Ok; }

RiseCheckInputs riseInputs(const BoilerRoomDiagInputs& in, uint8_t pump, uint8_t hot, uint8_t cold) {
    RiseCheckInputs r{};
    r.pumpOn = in.pumpActual[pump];
    r.hotOk = sensorOk(in.sensor[hot]);
    r.hotC = in.sensor[hot].tempC;
    r.coldOk = sensorOk(in.sensor[cold]);
    r.coldC = in.sensor[cold].tempC;
    return r;
}

const PumpRiseCheck IDLE_CHECK{};

}  // namespace

void BoilerRoomDiagnostics::reset() {
    _hist.reset(BR_SENSOR_COUNT);
    _b1.reset();
    _b3.reset();
    _b6.reset();
}

uint32_t BoilerRoomDiagnostics::update(
    const BoilerRoomDiagInputs& in, const BoilerRoomDiagSettings& s, uint64_t nowMs) {
    float values[BR_SENSOR_COUNT];
    bool ok[BR_SENSOR_COUNT];
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        ok[i] = sensorOk(in.sensor[i]);
        values[i] = in.sensor[i].tempC;
    }
    _hist.tick(nowMs, values, ok);

    uint32_t mask = 0;
    if (_b1.update(riseInputs(in, BR_PUMP_P3, BR_SENSOR_T3, BR_SENSOR_T6), s.b1, nowMs)) {
        mask |= (1u << BR_WARN_B1);
    }
    if (_b3.update(riseInputs(in, BR_PUMP_P1, BR_SENSOR_T1, BR_SENSOR_T3), s.b3, nowMs)) {
        mask |= (1u << BR_WARN_B3);
    }
    if (_b6.update(riseInputs(in, BR_PUMP_P2, BR_SENSOR_T1, BR_SENSOR_T2), s.b6, nowMs)) {
        mask |= (1u << BR_WARN_B6);
    }
    return mask;
}

const PumpRiseCheck& BoilerRoomDiagnostics::check(uint8_t warnBit) const {
    switch (warnBit) {
        case BR_WARN_B1: return _b1;
        case BR_WARN_B3: return _b3;
        case BR_WARN_B6: return _b6;
        default: return IDLE_CHECK;
    }
}
