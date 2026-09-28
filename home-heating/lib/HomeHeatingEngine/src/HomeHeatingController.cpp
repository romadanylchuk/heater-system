#include "HomeHeatingController.h"

void HomeHeatingController::reset(uint64_t bootMs) {
    _p4.reset(bootMs);
    _k1.reset();
    _k2.reset();
}

HomeHeatingOutputs HomeHeatingController::update(
    const HomeHeatingInputs& in, const HomeHeatingSettings& raw, uint64_t nowMs) {
    const HomeHeatingSettings g = guardHomeHeatingSettings(raw);
    const SensorInput& s1 = in.sensor[HH_SENSOR_H1];
    const SensorInput& s2 = in.sensor[HH_SENSOR_H2];
    const SensorInput& s3 = in.sensor[HH_SENSOR_H3];
    const SensorInput& s4 = in.sensor[HH_SENSOR_H4];

    // Classify, fail mode.
    const SensorHealth h1 = classifySensor(s1.state);
    const SensorHealth h2 = classifySensor(s2.state);
    const SensorHealth h3 = classifySensor(s3.state);
    const SensorHealth h4 = classifySensor(s4.state);
    const FailMode fail = computeFailMode(h1, h2, h3);

    // P4.
    P4Inputs pi{};
    pi.heatingEnabled = g.heatingEnabled;
    pi.h3 = h3;
    pi.h3C = s3.tempC;
    pi.fail = fail;
    const P4Decision p4 = _p4.update(pi, g, nowMs);

    // K1 (this tick's P4 request; running needs the actual relay, D10).
    K1Inputs ki{};
    ki.heatingEnabled = g.heatingEnabled;
    ki.p4Requested = p4.on;
    ki.p4Running = p4.on && in.p4RelayActual;
    ki.fail = fail;
    ki.h1 = h1;
    ki.h2 = h2;
    ki.h3 = h3;
    ki.h1C = s1.tempC;
    ki.h2C = s2.tempC;
    ki.h3C = s3.tempC;
    ki.k1Busy = in.k1Busy;
    ki.k1AntiSeizeOwned = in.k1AntiSeizeOwned;
    ki.inhibited = in.inhibited;
    ki.motion = in.k1Motion;
    ki.hold = in.k1Hold;
    const K1Decision k1 = _k1.update(ki, g, nowMs);

    // K2 (independent of heatingEnabled).
    const K2Decision k2 = _k2.update(h3, s3.tempC, h4, s4.tempC, g);

    // No-need (A2, D14).
    NoNeedInputs ni{};
    ni.anyPending = h1 == SensorHealth::Pending || h2 == SensorHealth::Pending ||
                    h3 == SensorHealth::Pending || h4 == SensorHealth::Pending;
    ni.h3FailedHeating = g.heatingEnabled && h3 == SensorHealth::Failed;
    ni.k2BypassRequested = k2.bypass;
    ni.k2RelayActual = in.k2RelayActual;
    ni.k2ExerciseRunning = in.k2ExerciseRunning;
    ni.p4Requested = p4.on;
    ni.p4RelayActual = in.p4RelayActual;
    ni.p4ExerciseRunning = in.p4ExerciseRunning;
    ni.p4OffDelayElapsed = p4.offDelayElapsed;

    HomeHeatingOutputs out{};
    out.p4 = p4;
    out.k2 = k2;
    out.k1 = k1;
    out.fail = fail;
    out.noNeed = computeNoNeed(ni);
    out.alarmMask = computeAlarmMask(in.sensor, fail);
    return out;
}
