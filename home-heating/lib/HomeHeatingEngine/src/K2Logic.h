#pragma once
#include "HomeHeatingControlSettings.h"
#include "HomeHeatingTypes.h"

// K2 DHW diverter with three hysteresis latches (stage 08, C4). Pure.
// bypass = R1 energised (BYPASS); de-energised = TANK (boot state).
//  - H4 Failed -> bypass H4Fault; H3 Failed -> bypass H3Fault (D13);
//    H3 or H4 Pending -> hold the previous request, reason SensorWait (A4).
//    Failed / Pending invalidate the latches.
//  - Both Ok: latches initialise from the first reading (deltaOk = H3 > H4+D,
//    h3Ok = H3 >= H3min, h4Ok = H4 < H4max), then follow their hysteresis:
//      deltaOk on at H3 > H4+D,   off at H3 <= H4+D-DHyst
//      h3Ok    on at H3 >= H3min, off at H3 <  H3min-H3minHyst
//      h4Ok    off at H4 >= H4max, on at H4 <= H4max-H4maxHyst
//    TANK (Charging) iff all three; else bypass by priority H4Full > H3Low > DeltaLow.
// Independent of heatingEnabled.
struct K2Decision {
    bool bypass;
    K2Reason reason;
};

class K2Logic {
public:
    void reset();   // _bypass = false (TANK), latches invalid
    // g must already be guarded.
    K2Decision update(SensorHealth h3, float h3C, SensorHealth h4, float h4C, const HomeHeatingSettings& g);

private:
    bool _bypass = false;
    bool _valid = false;
    bool _deltaOk = false, _h3Ok = false, _h4Ok = false;
};
