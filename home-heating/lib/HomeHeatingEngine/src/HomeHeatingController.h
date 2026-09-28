#pragma once
#include <stdint.h>
#include <K1Driver.h>
#include "HomeHeatingAlarms.h"
#include "HomeHeatingControlSettings.h"
#include "HomeHeatingTypes.h"
#include "K1Logic.h"
#include "K2Logic.h"
#include "NoNeed.h"
#include "P4Logic.h"

// Home-heating controller (stage 08, C9): composes the pure units. One update()
// per tick: guard settings -> classify -> fail mode -> P4 -> K1 -> K2 ->
// no-need -> alarms. K1 sees this tick's P4 request (p4Running also needs the
// actual relay, D10). Returns decisions only; it never logs or touches
// hardware (the runtime owns every side effect, D2).
struct HomeHeatingInputs {
    SensorInput sensor[HH_SENSOR_COUNT];
    bool p4RelayActual, p4ExerciseRunning, k2RelayActual, k2ExerciseRunning;
    bool k1Busy, k1AntiSeizeOwned, inhibited;
    K1Motion k1Motion;
    bool k1Hold = false;   // stage 09: K1 step test running -> K1Inputs.hold (C13); appended last
};

struct HomeHeatingOutputs {
    P4Decision p4;
    K2Decision k2;
    K1Decision k1;
    FailMode fail;
    bool noNeed;
    uint32_t alarmMask;   // controller bits only (subset of HH_ALARM_OWNED_MASK)
};

class HomeHeatingController {
public:
    void reset(uint64_t bootMs);
    // Applies guardHomeHeatingSettings(raw) first.
    HomeHeatingOutputs update(const HomeHeatingInputs& in, const HomeHeatingSettings& raw, uint64_t nowMs);

private:
    P4Logic _p4;
    K1Logic _k1;
    K2Logic _k2;
};
