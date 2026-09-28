#pragma once
#include <stdint.h>
#include "HomeHeatingControlSettings.h"
#include "HomeHeatingTypes.h"

// K1 mixing-valve math (stage 08, C6). Pure, no state machine.
// All ms values are rounded to the nearest ms (x + 0.5f, then truncated).

// Position estimate in % open, integrated from K1Driver::takeMotion() (D6).
class K1Estimator {
public:
    void reset();                                              // known = false, pct = 0
    // pct += (open - close) * 100 / travel, clamped [0,100]; integrates while unknown too.
    // travelMs == 0 -> no change.
    void applyMotion(const K1Motion& m, uint32_t travelMs);
    void setKnown(float pct);                                  // known = true, pct = clamp(pct)
    bool known() const;
    float pct() const;

private:
    bool _known = false;
    float _pct = 0.0f;
};

// Feed-forward target (A1): x = (h2Set - h1) / (h3 - h1) * 100, clamped [0,100];
// (h3 - h1) <= smallDiff (including negative) -> 100.
float feedforwardTarget(float h1, float h2Set, float h3, float smallDiff);

struct K1Move {
    bool issue;
    K1Direction dir;
    uint32_t ms;
    bool resync;   // the move ends at 0 % / 100 % and includes the overdrive (D8)
};

// Move from the estimate to targetPct (clamped [0,100]); g must already be guarded.
//  - target <= 0:   from <= 0 -> none; else CLOSE from% of travel + overdrive, resync.
//  - target >= 100: from >= 100 -> none; else OPEN (100-from)% of travel + overdrive, resync.
//  - otherwise |target-from|% of travel; below k1MinPulse -> none (D9).
K1Move planMoveTo(float fromPct, float targetPct, const HomeHeatingSettings& g);

// One feedback pulse: err = h2Set - h2; |err| <= deadband -> none;
// pulse = min(gain*|err|, k1MaxPulse) s; below k1MinPulse -> none; OPEN if err > 0 else CLOSE.
// Already at the end in that direction -> none (D8); reaching the end -> distance-to-end +
// overdrive, resync.
K1Move planFeedback(float fromPct, float h2, float h2Set, const HomeHeatingSettings& g);
