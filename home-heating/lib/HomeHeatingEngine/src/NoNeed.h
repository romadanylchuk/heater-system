#pragma once

// Home-heating "no need" signal (stage 08, C5, A2 + D14). Pure.
// ON needs the requests AND the actual relays (K2 physically in BYPASS, P4
// physically OFF); OFF follows the request immediately. While an anti-seize
// exercise runs on P4/K2 its request stands in for the actual relay (D14).
struct NoNeedInputs {
    bool anyPending;              // any of H1..H4 Pending (A4)
    bool h3FailedHeating;         // heatingEnabled && H3 Failed (D14)
    bool k2BypassRequested, k2BypassActual, k2ExerciseRunning;  // actual = valve physically in BYPASS
    bool p4Requested, p4RelayActual, p4ExerciseRunning;
    bool p4OffDelayElapsed;
};

bool computeNoNeed(const NoNeedInputs& in);
