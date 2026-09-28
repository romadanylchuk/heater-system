#pragma once
#include <stdint.h>
#include "BoilerRoomControlSettings.h"
#include "BoilerRoomTypes.h"

// Boiler loop logic (stage 07, C3): the overheat latch, P1 charging and P2
// return protection with their fail-safes. Pure; one update() per tick.
//  - Overheat latch: set at T1 > ohOn, cleared at T1 < ohClear (T1 Ok only);
//    held while T1 is Pending, cleared when T1 has Failed (D4).
//  - Burning latch (D10): set at T1 > p2T1Burn, cleared at T1 < p2T1Burn - p2Hyst;
//    false whenever T1 is not Ok.
//  - Rule states keep their hysteresis while a safety state overrides the
//    output, and reset to OFF when their inputs are not evaluable (D11).
struct LoopDecision { PumpDecision p1; PumpDecision p2; bool overheat; };

class BoilerLoopLogic {
public:
    void reset();                                           // overheat=false, rule states OFF, burning=false
    // t[] indexed by BR_SENSOR_*; only T1..T3 are read. s must already be guarded.
    LoopDecision update(const SensorInput t[BR_SENSOR_COUNT], const BoilerRoomSettings& s);
    bool overheat() const;

private:
    bool _overheat = false, _p1RuleOn = false, _p2RuleOn = false, _burning = false;
};
