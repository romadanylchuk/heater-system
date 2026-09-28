#pragma once
#include <stdint.h>
#include "BoilerRoomTypes.h"
#include "StoredEnergy.h"

// The controller AppState slice (stage 07, C8/D14). A separate POD beside
// CommonState, written only by BoilerRoomRuntime::tick on the loop task; the
// views (web "ctl", OLED pages, HA custom entities) only read it.
// Value-initialise on construction: `BoilerRoomStatus status{};`.
struct BoilerRoomPumpStatus {
    bool on;              // requested (effective controller request, not the relay's actual state)
    bool safety;          // request is on the safety slot (lock bypass)
    PumpReason reason;
};

struct BoilerRoomStatus {
    bool ready;
    BoilerRoomPumpStatus pump[BR_PUMP_COUNT];
    P3Mode p3Mode;
    bool overheat, antiFreezeRunning, dumpActive, offerDisabled;
    bool noNeedSaved;      // persisted setting (display only)
    bool noNeedEffective;  // gated value the logic used
    bool linkUp;
    uint32_t offerWindowLeftS, offerWaitLeftS, afInS, p3IdleS;
    EnergyQuality energyQuality;
    float energyKWh;
    bool t6Usable;
    uint32_t alarmMask;    // controller bits
};
