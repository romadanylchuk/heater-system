#include "HomeHeatingDiagnostics.h"

namespace {

bool sensorOk(const SensorInput& s) { return s.state == SensorState::Ok; }

}  // namespace

void HomeHeatingDiagnostics::reset() {
    _hist.reset(HH_SENSOR_COUNT);
    _h1.reset();
}

uint32_t HomeHeatingDiagnostics::update(
    const HomeHeatingDiagInputs& in, const HomeHeatingDiagSettings& s, uint64_t nowMs) {
    float values[HH_SENSOR_COUNT];
    bool ok[HH_SENSOR_COUNT];
    for (uint8_t i = 0; i < HH_SENSOR_COUNT; ++i) {
        ok[i] = sensorOk(in.sensor[i]);
        values[i] = in.sensor[i].tempC;
    }
    _hist.tick(nowMs, values, ok);

    P4FlowInputs f{};
    f.p4On = in.p4Actual;
    f.h1Ok = ok[HH_SENSOR_H1];
    f.h2Ok = ok[HH_SENSOR_H2];
    f.h3Ok = ok[HH_SENSOR_H3];
    f.h1C = values[HH_SENSOR_H1];
    f.h2C = values[HH_SENSOR_H2];
    f.h3C = values[HH_SENSOR_H3];
    f.k1Known = in.k1Known;
    f.k1PosPct = in.k1PosPct;

    uint32_t mask = 0;
    if (_h1.update(f, s.h1, nowMs)) {
        mask |= (1u << HH_WARN_H1);
    }
    return mask;
}
