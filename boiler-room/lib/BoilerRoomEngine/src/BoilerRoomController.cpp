#include "BoilerRoomController.h"

void BoilerRoomController::reset() {
    _loop.reset();
    _supply.reset();
}

BoilerRoomOutputs BoilerRoomController::update(
    const BoilerRoomInputs& in, const BoilerRoomSettings& raw, uint64_t nowMs) {
    const BoilerRoomSettings g = guardBoilerRoomSettings(raw);
    const SensorInput& t3 = in.sensor[BR_SENSOR_T3];

    const LoopDecision loop = _loop.update(in.sensor, g);

    SupplyInputs si{};
    si.t3 = classifySensor(t3.state);
    si.t3C = t3.tempC;
    si.flag = in.noNeedFlag;
    si.linkUp = in.linkUp;
    si.overheat = loop.overheat;
    si.relayOn = in.p3RelayOn;
    si.lastRunMs = in.p3LastRunMs;
    const SupplyDecision sup = _supply.update(si, g, nowMs);

    BoilerRoomOutputs out{};
    out.pump[BR_PUMP_P1] = loop.p1;
    out.pump[BR_PUMP_P2] = loop.p2;
    out.pump[BR_PUMP_P3] = sup.p3;
    out.p3Mode = sup.mode;
    out.overheat = loop.overheat;
    out.requestFlagClear = sup.requestFlagClear;
    out.antiFreezeRunning = sup.antiFreezeRunning;
    out.antiFreezeStarted = sup.antiFreezeStarted;
    out.dumpActive = sup.dumpActive;
    out.offerDisabled = sup.offerDisabled;
    out.offerWindowLeftS = sup.offerWindowLeftS;
    out.offerWaitLeftS = sup.offerWaitLeftS;
    out.afInS = sup.afInS;
    out.energy = computeStoredEnergy(
        t3, in.sensor[BR_SENSOR_T4], in.sensor[BR_SENSOR_T5], g.accVolumeL, g.accTBase);
    out.t6Usable = in.sensor[BR_SENSOR_T6].state == SensorState::Ok;
    out.alarmMask = computeAlarmMask(in.sensor, loop.overheat);
    return out;
}
