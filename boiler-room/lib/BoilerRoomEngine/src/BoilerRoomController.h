#pragma once
#include <stdint.h>
#include "BoilerLoopLogic.h"
#include "BoilerRoomAlarms.h"
#include "BoilerRoomControlSettings.h"
#include "BoilerRoomTypes.h"
#include "StoredEnergy.h"
#include "SupplyLogic.h"

// Boiler-room controller (stage 07, C7): composes the pure units. One update()
// per tick: guard settings -> loop logic (overheat/P1/P2) -> supply (P3) ->
// stored energy -> alarms. Returns decisions and edge flags only; it never
// logs or touches hardware (the runtime owns every side effect, D2).
struct BoilerRoomInputs {
    SensorInput sensor[BR_SENSOR_COUNT];
    bool noNeedFlag;     // gated
    bool linkUp;
    bool p3RelayOn;
    uint64_t p3LastRunMs;
};

struct BoilerRoomOutputs {
    PumpDecision pump[BR_PUMP_COUNT];
    P3Mode p3Mode;
    bool overheat, requestFlagClear, antiFreezeRunning, antiFreezeStarted, dumpActive, offerDisabled;
    uint32_t offerWindowLeftS, offerWaitLeftS, afInS;
    EnergyResult energy;
    bool t6Usable;       // T6 state == Ok (stage 09 pauses B1 while false)
    uint32_t alarmMask;  // controller bits only (subset of BR_ALARM_OWNED_MASK)
};

class BoilerRoomController {
public:
    void reset();
    // Applies guardBoilerRoomSettings(raw) first. Order: loop logic -> supply -> energy -> alarms.
    BoilerRoomOutputs update(const BoilerRoomInputs& in, const BoilerRoomSettings& raw, uint64_t nowMs);

private:
    BoilerLoopLogic _loop;
    SupplyLogic _supply;
};
