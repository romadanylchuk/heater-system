#pragma once
#include <stdint.h>
#include "BoilerRoomControlSettings.h"
#include "BoilerRoomTypes.h"

// P3 supply logic (stage 07, C4): the NORMAL/OFF/OFFER state machine driven by
// the gated "Home: no need" flag, time-based anti-freeze and the overheat heat
// dump. Pure; time is injected as monotonic ms. At most one mode transition per
// update() (D6):
//  - Gates: !linkUp or T3 not Ok -> NORMAL from any mode (an OFFER is abandoned
//    without a clear request; an armed wait keeps counting).
//  - NORMAL: flag -> OFF.
//  - OFF: flag cleared -> NORMAL (D6 gap-fill: the home needs heat again);
//    else T3 >= T3_offer and the wait elapsed and no overheat (D7) -> OFFER,
//    requesting the flag self-clear once.
//  - OFFER: the flag is ignored for the window; afterwards flag set -> OFF and
//    the wait is armed (only here), flag cleared -> NORMAL.
// Anti-freeze (D13): starts when enabled, the relay is actually OFF and P3 has
// been idle for afInterval (shared lastRunMs); runs afDuration from its start;
// disabling stops it at once. Overheat forces P3 ON on the safety slot
// (OverheatDump) ignoring the flag; the state machine keeps running underneath.
struct SupplyInputs {
    SensorHealth t3; float t3C;
    bool flag;          // gated "Home: no need" (haGatedFlag(persisted, net)) -- never the raw setting
    bool linkUp;        // CommonState.network.mqttConnected
    bool overheat;      // BoilerLoopLogic::overheat() of this tick
    bool relayOn;       // RelayBank::actual(P3)
    uint64_t lastRunMs; // shared P3 last-run timer (RelayBank activity, >= boot time)
};

struct SupplyDecision {
    PumpDecision p3;
    P3Mode mode;
    bool requestFlagClear;     // edge: true only on the tick that entered OFFER
    bool antiFreezeRunning;
    bool antiFreezeStarted;    // edge: true only on the tick a run started
    bool dumpActive;           // overheat forcing P3
    bool offerDisabled;        // T3 not Ok
    uint32_t offerWindowLeftS; // OFFER only, else 0
    uint32_t offerWaitLeftS;   // wait armed and running, else 0
    uint32_t afInS;            // enabled && relay OFF && not running: seconds to next run, else 0
};

class SupplyLogic {
public:
    void reset();              // Normal, wait not armed, anti-freeze idle
    SupplyDecision update(const SupplyInputs& in, const BoilerRoomSettings& s, uint64_t nowMs);
    P3Mode mode() const;

private:
    P3Mode _mode = P3Mode::Normal;
    uint64_t _offerStartMs = 0;
    bool _waitArmed = false;
    uint64_t _waitStartMs = 0;
    bool _afRunning = false;
    uint64_t _afStartMs = 0;
};
