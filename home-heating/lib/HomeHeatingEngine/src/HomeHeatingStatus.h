#pragma once
#include <stdint.h>
#include "HomeHeatingTypes.h"

// The home-heating controller AppState slice (stage 08, C10/D21). A separate
// POD beside CommonState, written only by HomeHeatingRuntime::tick on the loop
// task; the views (web "ctl", OLED pages, HA custom entities) only read it.
// Value-initialise on construction: `HomeHeatingStatus status{};`.
struct HomeHeatingStatus {
    bool ready;
    bool heatingEnabled;
    float h2Set;
    bool p4On;                  // controller request (not the relay's actual state)
    P4Reason p4Reason;
    uint32_t p4OffDelayLeftS;
    bool k2Bypass;              // controller request
    K2Reason k2Reason;
    K1Mode k1Mode;
    bool k1Known;
    float k1PosPct;             // live: estimate +/- running-pulse projection, clamped [0, 100]
    int8_t k1Moving;            // +1 opening, -1 closing, 0 idle (any owner)
    bool k1FfValid;
    float k1FfPct;
    FailMode fail;
    bool noNeed;
    uint32_t alarmMask;         // controller bits
};
