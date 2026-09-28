#pragma once
#include <stdint.h>
#include "HomeHeatingTypes.h"
#include "K1StepTest.h"

// The home-heating controller AppState slice (stage 08, C10/D21). A separate
// POD beside CommonState, written only by HomeHeatingRuntime::tick on the loop
// task; the views (web "ctl", OLED pages, HA custom entities) only read it.
// Value-initialise on construction: `HomeHeatingStatus status{};`.

// Stage 09 (C15): the K1 step-test panel state.
struct HomeHeatingStepStatus {
    StepBlock block;            // why a start is blocked now (None = startable)
    bool running;
    uint32_t elapsedS;
    uint32_t pulseS;            // pulse of the running / last test
    bool deadSeen;
    float deadTimeS;            // live dead time once seen
    StepTestResult last;        // last finished test (outcome None until one ends)
};

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
    // ---- stage 09 (C15), appended; the fields above are unchanged ----
    bool h2ErrValid;            // H2 state Ok && heatingEnabled
    float h2ErrC;               // H2 - h2Set
    int8_t lastPulseDir;        // +1 open / -1 close / 0 none (FF / feedback / step test only, D11)
    float lastPulseS;           // commanded length (A8)
    uint32_t pulsesToday;       // K1 motor runs, local day (A9)
    bool pulsesYesterdayValid;  // false until the first local-day rollover
    uint32_t pulsesYesterday;
    HomeHeatingStepStatus step;
};
