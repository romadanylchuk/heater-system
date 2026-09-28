#pragma once
#include <stdint.h>
#include <SensorHistory.h>
#include "BoilerRoomDiagSettings.h"
#include "BoilerRoomTypes.h"
#include "PumpRiseCheck.h"

// boiler-room pump-response diagnostics (stage 09, C8): the T1..T6 history
// plus the B1/B3/B6 checks, folded into a warning mask. Pure, time injected.
// Warnings only: the mask never feeds a controller input (D2).
constexpr uint8_t BR_WARN_B1 = 0, BR_WARN_B3 = 1, BR_WARN_B6 = 2;
constexpr uint32_t BR_WARN_OWNED_MASK = 0x000000FFu;  // diag.warningMask bits 0..7 (D6)

struct BoilerRoomDiagInputs {
    SensorInput sensor[BR_SENSOR_COUNT];
    bool pumpActual[BR_PUMP_COUNT];  // relay ACTUAL state per pump (A1)
};

class BoilerRoomDiagnostics {
public:
    void reset();  // history.reset(BR_SENSOR_COUNT), checks reset

    // 1. history.tick(now, tempC[], ok[]) with ok = (state == SensorState::Ok) (D3);
    // 2. B1: pump P3, hot T3, cold T6; B3: pump P1, hot T1, cold T3;
    //    B6: pump P2, hot T1, cold T2;
    // 3. returns the mask (bit BR_WARN_x set while that check is active).
    uint32_t update(const BoilerRoomDiagInputs& in, const BoilerRoomDiagSettings& s, uint64_t nowMs);

    const SensorHistory& history() const { return _hist; }
    // The check behind a warning bit (tests/diagnostics); an unknown bit
    // returns an idle check.
    const PumpRiseCheck& check(uint8_t warnBit) const;

private:
    SensorHistory _hist;
    PumpRiseCheck _b1, _b3, _b6;
};
