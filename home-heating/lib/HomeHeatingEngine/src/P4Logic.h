#pragma once
#include <stdint.h>
#include "HomeHeatingControlSettings.h"
#include "HomeHeatingTypes.h"

// P4 radiator pump rule with its off-delay (stage 08, C3). Pure; one update()
// per tick, time injected. Rule order (first match wins); "demand" refreshes
// the off-delay start (D11: every ON rule except OffDelay, forced ON included):
//  1. heating disabled               -> OFF HeatingOff (delay treated as elapsed)
//  2. H3 Failed: Multi               -> OFF MultiFaultOff; else ON H3FaultForced (demand)
//  3. H3 Pending                     -> OFF SensorWait (immediately, A4)
//  4. Multi with H3 Ok               -> ON MultiFaultForced (demand, D12)
//  5. was ON and H3 >= H2_set        -> ON Demand (demand)
//  5a. was OFF and H3 >= H2_set + p4StartMargin held continuously for
//      p4StartConfirmS               -> ON Demand (demand); while still
//                                       holding -> OFF StartWait (no demand)
//  6. was ON and delay still running -> ON OffDelay
//  7. otherwise                      -> OFF SupplyCold
// The delay runs from boot (reset(bootMs)). The start hold re-arms whenever H3
// drops below H2_set + margin, H3 is not Ok, fail mode is Multi or heating is
// off, so a ~2 min P3 anti-freeze slug of lukewarm water cannot start P4
// (2026-11-08). The hysteresis is p4StartMargin; margin 0 + hold 0 = the rule
// before 2026-11-08 (start at H3 >= H2_set).
struct P4Inputs {
    bool heatingEnabled;
    SensorHealth h3;
    float h3C;
    FailMode fail;
};

struct P4Decision {
    bool on;
    P4Reason reason;
    bool offDelayElapsed;       // !on && the off-delay has run out (always true with heating off)
    uint32_t offDelayLeftS;     // ceil seconds left while rule 6/7 applies and not elapsed, else 0
};

class P4Logic {
public:
    void reset(uint64_t bootMs);   // on = false, lastDemandMs = bootMs
    // g must already be guarded.
    P4Decision update(const P4Inputs& in, const HomeHeatingSettings& g, uint64_t nowMs);

private:
    bool _on = false;
    uint64_t _lastDemandMs = 0;
    bool _startArmed = false;      // H3 >= H2_set + margin, hold time running
    uint64_t _startSinceMs = 0;
};
